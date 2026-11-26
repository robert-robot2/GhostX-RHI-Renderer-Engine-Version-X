// GhostFoliage.cpp - foliage layers, chunks, scatter painting, erase, and the exported C API used by GhostShell.
// Put this file in /src. Add it to the GhostX project (it must compile with GHOSTENGINE_EXPORTS like GhostEngine.cpp).
// The exports that need engine internals (paint mode, stats) live in the glue block at the end of GhostEngine.cpp.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostFoliage.h"
#include "../include/GhostEngine.h"
#include <algorithm>
#include <math.h>
#include <string.h>

static GhostFoliageSet  g_set;
static GhostFoliageHost g_host;

void GhostFoliage_SetHost(const GhostFoliageHost* h) { if (h) g_host = *h; }
GhostFoliageSet& GhostFoliage_Set() { return g_set; }
static void Dirty() { g_set.version = GhostTile_NextStamp(); }

static uint32_t RngNext(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static float    Rf(uint32_t& s) { return (float)(RngNext(s) & 0xFFFFFFu) / 16777216.0f; }
static float    Cf(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }
static int      Ci(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ============================================================
//  Layers
// ============================================================
GhostFoliageLayer* GhostFoliage_Find(int id)
{
    for (GhostFoliageLayer& L : g_set.layers) if (L.id == id) return &L;
    return nullptr;
}

int GhostFoliage_FaceCount(const GhostFoliageLayer& L)
{
    if (L.kind == GHOST_FOLIAGE_SPRITE) return 1;
    const int n = GhostMesh_FaceCount(L.meshId);
    return n < 1 ? 1 : n;
}

void GhostFoliage_ResetFaces(GhostFoliageLayer& L)
{
    for (int f = 0; f < GHOST_MAX_FACES; f++) L.faces[f] = GhostMaterial();
    if (L.kind == GHOST_FOLIAGE_MESH)
        if (const GhostMeshInfo* mi = GhostMesh_Info(L.meshId))
        {
            const int n = GhostMesh_FaceCount(L.meshId);
            for (int f = 0; f < n; f++)
            {
                if (!mi->builtin && mi->faces[(size_t)f].hasMat) L.faces[f] = mi->faces[(size_t)f].mat;
                else if (mi->builtin) { L.faces[f].tint[0] = 0.35f; L.faces[f].tint[1] = 0.55f; L.faces[f].tint[2] = 0.25f; }
            }
        }
    for (GhostMaterial& m : L.faces) { m.wpoSway = 0.05f; m.wpoSpeed = 1.5f; m.twoSided = 1; }   // a little wind by default
    if (L.kind == GHOST_FOLIAGE_SPRITE) { L.faces[0].tint[0] = L.faces[0].tint[1] = L.faces[0].tint[2] = 1.0f; }
}

int GhostFoliage_AddLayer(int kind, int source, const std::string& nameIn)
{
    if ((int)g_set.layers.size() >= GHOST_FOLIAGE_MAX_LAYERS) return 0;
    const bool sprite = (kind == GHOST_FOLIAGE_SPRITE);
    if (!sprite && (!GhostMesh_Valid(source) || source == GHOST_MESH_QUAD)) return 0;

    GhostFoliageLayer L;
    L.id = g_set.nextId++;
    L.kind = sprite ? GHOST_FOLIAGE_SPRITE : GHOST_FOLIAGE_MESH;
    if (sprite)
    {
        L.meshId = GHOST_MESH_QUAD;
        L.billboard = true; L.castShadows = 0; L.normalUp = 0.6f; L.cullDistance = 60.0f; L.fadeLength = 15.0f;
        L.amount = 14; L.minSpacing = 0.25f;
    }
    else
    {
        L.meshId = source;
        L.billboard = false; L.castShadows = 1; L.cullDistance = 140.0f; L.fadeLength = 30.0f;
        L.amount = 4; L.minSpacing = 1.5f; L.sizeMin = 0.8f; L.sizeMax = 1.3f; L.scatterRadius = 6.0f;
    }
    if (!nameIn.empty()) L.name = nameIn;
    else if (sprite) L.name = "Sprite Foliage " + std::to_string(L.id);
    else { const GhostMeshInfo* mi = GhostMesh_Info(L.meshId); L.name = (mi ? mi->name : std::string("Mesh")) + " Foliage " + std::to_string(L.id); }

    GhostFoliage_ResetFaces(L);
    if (sprite) L.faces[0].textureId = source > 0 ? source : 0;
    GhostFoliage_Sanitize(L);
    g_set.layers.push_back(std::move(L));
    g_set.activeLayerId = g_set.layers.back().id;
    Dirty();
    return g_set.layers.back().id;
}

bool GhostFoliage_RemoveLayer(int id)
{
    for (size_t i = 0; i < g_set.layers.size(); i++)
        if (g_set.layers[i].id == id)
        {
            g_set.layers.erase(g_set.layers.begin() + (std::ptrdiff_t)i);
            if (g_set.activeLayerId == id) g_set.activeLayerId = g_set.layers.empty() ? 0 : g_set.layers.front().id;
            Dirty();
            return true;
        }
    return false;
}

void GhostFoliage_Clear()
{
    g_set.layers.clear();
    g_set.nextId = 1;
    g_set.activeLayerId = 0;
    g_set.brush.active = false;
    Dirty();
}

void GhostFoliage_Sanitize(GhostFoliageLayer& L)
{
    if (L.name.size() > 63) L.name.resize(63);
    L.kind = L.kind == GHOST_FOLIAGE_SPRITE ? GHOST_FOLIAGE_SPRITE : GHOST_FOLIAGE_MESH;
    if (L.kind == GHOST_FOLIAGE_SPRITE) L.meshId = GHOST_MESH_QUAD;
    L.cols = Ci(L.cols, 1, 64); L.rows = Ci(L.rows, 1, 64);
    L.animFps = Cf(L.animFps, 0.1f, 60.0f);
    L.sizeMin = Cf(L.sizeMin, 0.01f, 100.0f);
    L.sizeMax = Cf(L.sizeMax, L.sizeMin, 100.0f);
    L.yawRange = Cf(L.yawRange, 0.0f, 360.0f);
    L.tiltRandom = Cf(L.tiltRandom, 0.0f, 45.0f);
    L.alignToNormal = Cf(L.alignToNormal, 0.0f, 1.0f);
    L.slopeMax = Cf(L.slopeMax, 0.0f, 90.0f);
    L.heightOffset = Cf(L.heightOffset, -50.0f, 50.0f);
    L.scatterRadius = Cf(L.scatterRadius, 0.25f, 100.0f);
    L.amount = Ci(L.amount, 1, 200);
    L.minSpacing = Cf(L.minSpacing, 0.0f, 50.0f);
    L.cullDistance = Cf(L.cullDistance, 0.0f, 5000.0f);
    L.fadeLength = Cf(L.fadeLength, 0.0f, 2000.0f);
    L.colorVariation = Cf(L.colorVariation, 0.0f, 1.0f);
    L.normalUp = Cf(L.normalUp, 0.0f, 1.0f);
    L.castShadows = Ci(L.castShadows, 0, 2);
    L.shadowDistance = Cf(L.shadowDistance, 0.0f, 2000.0f);
}

size_t GhostFoliage_InstanceCount(const GhostFoliageLayer& L)
{
    size_t n = 0;
    for (const auto& kv : L.chunks) n += kv.second.inst.size();
    return n;
}

size_t GhostFoliage_TotalInstances()
{
    size_t n = 0;
    for (const GhostFoliageLayer& L : g_set.layers) n += GhostFoliage_InstanceCount(L);
    return n;
}

float GhostFoliage_Radius(const GhostFoliageLayer& L)
{
    const GhostVec3 mn = GhostMesh_Min(L.meshId), mx = GhostMesh_Max(L.meshId);
    const float ex = fmaxf(fabsf(mn.x), fabsf(mx.x)), ey = fmaxf(fabsf(mn.y), fabsf(mx.y)), ez = fmaxf(fabsf(mn.z), fabsf(mx.z));
    float r = sqrtf(ex * ex + ey * ey + ez * ez) * L.sizeMax;
    r += L.heightOffset < 0.0f ? -L.heightOffset : L.heightOffset;
    r += fabsf(L.faces[0].wpoSway) * 1.5f + 0.25f;
    return r;
}

void GhostFoliage_MeshBase(const GhostFoliageLayer& L, float& minY, float& height)
{
    minY = GhostMesh_Min(L.meshId).y;
    height = GhostMesh_Max(L.meshId).y - minY;
    if (height < 0.001f) height = 0.001f;
}

// ============================================================
//  Chunks
// ============================================================
GhostFoliageChunk& GhostFoliage_ChunkAt(GhostFoliageLayer& L, int cx, int cz)
{
    GhostFoliageChunk& C = L.chunks[GhostFoliage_Key(cx, cz)];
    C.cx = cx; C.cz = cz;
    return C;
}

void GhostFoliage_Touch(GhostFoliageChunk& C)
{
    if (C.inst.empty()) { for (int k = 0; k < 3; k++) C.bmin[k] = C.bmax[k] = 0.0f; }
    else
    {
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (const GhostFoliageInstance& i : C.inst)
        {
            lo[0] = fminf(lo[0], i.px); lo[1] = fminf(lo[1], i.py); lo[2] = fminf(lo[2], i.pz);
            hi[0] = fmaxf(hi[0], i.px); hi[1] = fmaxf(hi[1], i.py); hi[2] = fmaxf(hi[2], i.pz);
        }
        for (int k = 0; k < 3; k++) { C.bmin[k] = lo[k]; C.bmax[k] = hi[k]; }
    }
    C.stamp = GhostTile_NextStamp();
    Dirty();
}

// ============================================================
//  Painting
// ============================================================
static bool TooClose(GhostFoliageLayer& L, float x, float z, float spacing)
{
    if (spacing <= 0.0f) return false;
    const float s2 = spacing * spacing, CH = GHOST_FOLIAGE_CHUNK_SIZE;
    const int x0 = (int)floorf((x - spacing) / CH), x1 = (int)floorf((x + spacing) / CH);
    const int z0 = (int)floorf((z - spacing) / CH), z1 = (int)floorf((z + spacing) / CH);
    for (int cz = z0; cz <= z1; cz++)
        for (int cx = x0; cx <= x1; cx++)
        {
            auto it = L.chunks.find(GhostFoliage_Key(cx, cz));
            if (it == L.chunks.end()) continue;
            for (const GhostFoliageInstance& i : it->second.inst)
            {
                const float dx = i.px - x, dz = i.pz - z;
                if (dx * dx + dz * dz < s2) return true;
            }
        }
    return false;
}

int GhostFoliage_Paint(GhostFoliageLayer& L, const GhostVec3& c, bool landscape, bool objects)
{
    if (!g_host.Probe || L.amount <= 0 || (!landscape && !objects)) return 0;
    if (L.kind == GHOST_FOLIAGE_MESH && !GhostMesh_Valid(L.meshId)) return 0;

    const float R = L.scatterRadius, d2r = GHOST_PI / 180.0f;
    const float lift0 = L.restOnGround ? -GhostMesh_Min(L.meshId).y : 0.0f;     // sprite quad: 0.5 so it stands on the ground
    const float tiltMax = tanf(L.tiltRandom * d2r);
    size_t total = GhostFoliage_TotalInstances();
    std::vector<GhostFoliageChunk*> touched;
    int added = 0;

    for (int i = 0; i < L.amount * 3 && added < L.amount; i++)                  // spare tries for rejected spots
    {
        if (total >= GHOST_FOLIAGE_MAX_TOTAL) break;
        const float r = R * sqrtf(Rf(g_set.rng)), a = 6.2831853f * Rf(g_set.rng);
        const float x = c.x + cosf(a) * r, z = c.z + sinf(a) * r;

        GhostFoliageHit h;
        if (!g_host.Probe(x, z, c.y + R + 2.0f, c.y - R - 2.0f, landscape, objects, &h)) continue;
        const float ny = h.n.y < -1.0f ? -1.0f : (h.n.y > 1.0f ? 1.0f : h.n.y);
        if (acosf(ny) * (180.0f / GHOST_PI) > L.slopeMax) continue;
        if (TooClose(L, x, z, L.minSpacing)) continue;

        const int cx = (int)floorf(x / GHOST_FOLIAGE_CHUNK_SIZE), cz = (int)floorf(z / GHOST_FOLIAGE_CHUNK_SIZE);
        GhostFoliageChunk& C = GhostFoliage_ChunkAt(L, cx, cz);
        if (C.inst.size() >= GHOST_FOLIAGE_MAX_PER_CHUNK) continue;

        GhostFoliageInstance in;
        in.scale = L.sizeMin + (L.sizeMax - L.sizeMin) * Rf(g_set.rng);
        in.px = h.p.x; in.pz = h.p.z;
        in.py = h.p.y + lift0 * in.scale + L.heightOffset;
        in.yaw = (Rf(g_set.rng) - 0.5f) * L.yawRange * d2r;
        float tx = (Rf(g_set.rng) * 2.0f - 1.0f) * tiltMax, tz = (Rf(g_set.rng) * 2.0f - 1.0f) * tiltMax;
        if (L.alignToNormal > 0.0f && h.n.y > 0.05f) { tx += h.n.x / h.n.y * L.alignToNormal; tz += h.n.z / h.n.y * L.alignToNormal; }
        in.tiltX = Cf(tx, -1.5f, 1.5f); in.tiltZ = Cf(tz, -1.5f, 1.5f);
        in.seed = Rf(g_set.rng);
        C.inst.push_back(in);
        total++; added++;
        if (std::find(touched.begin(), touched.end(), &C) == touched.end()) touched.push_back(&C);
    }
    for (GhostFoliageChunk* C : touched) GhostFoliage_Touch(*C);
    return added;
}

int GhostFoliage_Erase(int layerId, const GhostVec3& c, float radius, float strength)
{
    const float r2 = radius * radius, CH = GHOST_FOLIAGE_CHUNK_SIZE;
    const int x0 = (int)floorf((c.x - radius) / CH), x1 = (int)floorf((c.x + radius) / CH);
    const int z0 = (int)floorf((c.z - radius) / CH), z1 = (int)floorf((c.z + radius) / CH);
    int removed = 0;
    for (GhostFoliageLayer& L : g_set.layers)
    {
        if (layerId >= 0 && L.id != layerId) continue;
        for (int cz = z0; cz <= z1; cz++)
            for (int cx = x0; cx <= x1; cx++)
            {
                auto it = L.chunks.find(GhostFoliage_Key(cx, cz));
                if (it == L.chunks.end()) continue;
                GhostFoliageChunk& C = it->second;
                const size_t before = C.inst.size();
                C.inst.erase(std::remove_if(C.inst.begin(), C.inst.end(), [&](const GhostFoliageInstance& i)
                    {
                        const float dx = i.px - c.x, dz = i.pz - c.z;
                        return dx * dx + dz * dz <= r2 && Rf(g_set.rng) < strength;
                    }), C.inst.end());
                if (C.inst.size() == before) continue;
                removed += (int)(before - C.inst.size());
                if (C.inst.empty()) { L.chunks.erase(it); Dirty(); }
                else GhostFoliage_Touch(C);
            }
    }
    return removed;
}

// ============================================================
//  Exported C API (declared in GhostEngine.h)
// ============================================================
int GhostEngine_AddFoliageLayer(int kind, int source) { return GhostFoliage_AddLayer(kind, source, std::string()); }
int GhostEngine_RemoveFoliageLayer(int id) { return GhostFoliage_RemoveLayer(id) ? 1 : 0; }

int GhostEngine_GetFoliageLayerIds(int* out, int maxCount)
{
    int n = 0;
    for (const GhostFoliageLayer& L : g_set.layers) { if (out && n < maxCount) out[n] = L.id; n++; }
    return n < maxCount || !out ? n : maxCount;
}

int GhostEngine_GetFoliageLayer(int id, GhostFoliageLayerInfo* out)
{
    const GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !out) return 0;
    *out = { L->id, L->kind, L->meshId, L->enabled ? 1 : 0,
             L->billboard ? 1 : 0, L->alphaClip ? 1 : 0, L->unlit ? 1 : 0, L->pixelArt ? 1 : 0,
             L->cols, L->rows, L->animated ? 1 : 0, L->animFps,
             L->sizeMin, L->sizeMax, L->yawRange, L->tiltRandom, L->alignToNormal, L->slopeMax,
             L->scatterRadius, L->amount, L->minSpacing, L->heightOffset, L->restOnGround ? 1 : 0,
             L->cullDistance, L->fadeLength, L->colorVariation, L->normalUp,
             L->castShadows, L->shadowLightId, L->receiveShadows ? 1 : 0, L->shadowDistance,
             (int)GhostFoliage_InstanceCount(*L), (int)L->chunks.size() };
    return 1;
}

void GhostEngine_SetFoliageLayer(const GhostFoliageLayerInfo* in)
{
    if (!in) return;
    GhostFoliageLayer* L = GhostFoliage_Find(in->id);
    if (!L) return;
    L->enabled = in->enabled != 0;
    L->billboard = in->billboard != 0; L->alphaClip = in->alphaClip != 0; L->unlit = in->unlit != 0; L->pixelArt = in->pixelArt != 0;
    L->cols = in->cols; L->rows = in->rows; L->animated = in->animated != 0; L->animFps = in->animFps;
    L->sizeMin = in->sizeMin; L->sizeMax = in->sizeMax; L->yawRange = in->yawRange; L->tiltRandom = in->tiltRandom;
    L->alignToNormal = in->alignToNormal; L->slopeMax = in->slopeMax;
    L->scatterRadius = in->scatterRadius; L->amount = in->amount; L->minSpacing = in->minSpacing;
    L->heightOffset = in->heightOffset; L->restOnGround = in->restOnGround != 0;
    L->cullDistance = in->cullDistance; L->fadeLength = in->fadeLength; L->colorVariation = in->colorVariation; L->normalUp = in->normalUp;
    L->castShadows = in->castShadows; L->receiveShadows = in->receiveShadows != 0; L->shadowDistance = in->shadowDistance;
    L->shadowLightId = in->shadowLightId < 0 ? 0 : in->shadowLightId;
    GhostFoliage_Sanitize(*L);
    Dirty();
}

// Mesh layers: swap the mesh (instances stay, materials go back to the new mesh's defaults)
int GhostEngine_SetFoliageSource(int id, int meshId)
{
    GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || L->kind != GHOST_FOLIAGE_MESH || !GhostMesh_Valid(meshId) || meshId == GHOST_MESH_QUAD) return 0;
    L->meshId = meshId;
    GhostFoliage_ResetFaces(*L);
    for (auto& kv : L->chunks) kv.second.stamp = GhostTile_NextStamp();
    Dirty();
    return 1;
}

int GhostEngine_GetFoliageLayerName(int id, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    buf[0] = 0;
    const GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L) return 0;
    strncpy_s(buf, (size_t)bufSize, L->name.c_str(), _TRUNCATE);
    return 1;
}

void GhostEngine_SetFoliageLayerName(int id, const char* utf8)
{
    GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !utf8 || !*utf8) return;
    L->name = utf8;
    if (L->name.size() > 63) L->name.resize(63);
    Dirty();
}

void GhostEngine_ClearFoliageLayer(int id)           // remove every instance of one layer
{
    GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L) return;
    L->chunks.clear();
    Dirty();
}

int GhostEngine_GetFoliageFaceCount(int id) { const GhostFoliageLayer* L = GhostFoliage_Find(id); return L ? GhostFoliage_FaceCount(*L) : 0; }

int GhostEngine_GetFoliageFaceName(int id, int face, char* buf, int bufSize)
{
    const GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !buf || bufSize <= 0 || face < 0 || face >= GhostFoliage_FaceCount(*L)) return 0;
    strncpy_s(buf, (size_t)bufSize, L->kind == GHOST_FOLIAGE_SPRITE ? "Sprite" : GhostMesh_Face(L->meshId, face).name.c_str(), _TRUNCATE);
    return 1;
}

int GhostEngine_GetFoliageActiveLayer() { return g_set.activeLayerId; }
void GhostEngine_SetFoliageActiveLayer(int id) { if (id == 0 || GhostFoliage_Find(id)) g_set.activeLayerId = id; }

void GhostEngine_GetFoliageBrush(GhostFoliageBrushInfo* out)
{
    if (!out) return;
    const GhostFoliageBrush& b = g_set.brush;
    *out = { b.active ? 1 : 0, b.erase ? 1 : 0, b.onLandscape ? 1 : 0, b.onObjects ? 1 : 0, b.eraseAll ? 1 : 0, g_set.activeLayerId };
}

void GhostEngine_SetFoliageBrush(const GhostFoliageBrushInfo* in)      // "active" is set through SetFoliagePaintMode
{
    if (!in) return;
    g_set.brush.erase = in->erase != 0;
    g_set.brush.onLandscape = in->onLandscape != 0;
    g_set.brush.onObjects = in->onObjects != 0;
    g_set.brush.eraseAll = in->eraseAll != 0;
    GhostEngine_SetFoliageActiveLayer(in->activeLayerId);
}

// ---- materials: the same data as GhostEngine_Get/SetMaterial, but for a foliage layer ----
int GhostEngine_GetFoliageMaterial(int id, int face, GhostMaterialInfo* out)
{
    const GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !out) return 0;
    if (face < 0) face = 0;
    if (face >= GhostFoliage_FaceCount(*L)) return 0;
    const GhostMaterial& m = L->faces[face];
    *out = { m.textureId, m.mapMode, m.wrap, m.filter, m.tiling[0], m.tiling[1], m.offset[0], m.offset[1], m.rotationDeg,
             m.tint[0], m.tint[1], m.tint[2], m.metallic, m.roughness, m.specular,
             m.emissive[0], m.emissive[1], m.emissive[2], m.emissiveStrength, m.alphaMode, m.alphaSource, m.alphaCutoff };
    return 1;
}

void GhostEngine_SetFoliageMaterial(int id, int face, const GhostMaterialInfo* in)
{
    GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !in) return;
    const int count = GhostFoliage_FaceCount(*L);
    if (face >= count) return;
    auto nz = [](float v) { return fabsf(v) < 0.001f ? (v < 0.0f ? -0.001f : 0.001f) : v; };
    auto c01 = [](float v) { return v != v ? 0.0f : (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)); };
    const int texCount = g_host.TextureCount ? g_host.TextureCount() : 1;
    const int tex = (in->textureId >= 0 && in->textureId < texCount) ? in->textureId : 0;
    for (int f = (face < 0 ? 0 : face); f <= (face < 0 ? count - 1 : face); f++)
    {
        GhostMaterial m = L->faces[f];                      // keeps the maps and the advanced surface values
        m.textureId = tex;
        m.mapMode = Ci(in->mapMode, 0, 7); m.wrap = in->wrap ? 1 : 0; m.filter = in->filter ? 1 : 0;
        m.tiling[0] = nz(in->tilingU); m.tiling[1] = nz(in->tilingV);
        m.offset[0] = in->offsetU; m.offset[1] = in->offsetV; m.rotationDeg = in->rotationDeg;
        m.tint[0] = c01(in->r); m.tint[1] = c01(in->g); m.tint[2] = c01(in->b);
        m.metallic = c01(in->metallic);
        m.roughness = in->roughness < 0.02f ? 0.02f : (in->roughness > 1.0f ? 1.0f : in->roughness);
        m.specular = c01(in->specular);
        m.emissive[0] = c01(in->emR); m.emissive[1] = c01(in->emG); m.emissive[2] = c01(in->emB);
        m.emissiveStrength = Cf(in->emissiveStrength, 0.0f, 20.0f);
        m.alphaMode = Ci(in->alphaMode, 0, 2); m.alphaSource = Ci(in->alphaSource, 0, 2); m.alphaCutoff = c01(in->alphaCutoff);
        L->faces[f] = m;
    }
    Dirty();
}

static const float kFoliMapMax[GHOST_MAP_COUNT] = { 4, 1, 1, 1, 1, 20, 1, 1, 1, 5, 1, 10, 1 };

int GhostEngine_GetFoliageMap(int id, int face, int slot, GhostMapInfo* out)
{
    const GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !out || slot < 0 || slot >= GHOST_MAP_COUNT) return 0;
    if (face < 0) face = 0;
    if (face >= GhostFoliage_FaceCount(*L)) return 0;
    const GhostMapParams& p = L->faces[face].maps[slot];
    *out = { p.textureId, p.amount, p.channel, p.invert };
    return 1;
}

void GhostEngine_SetFoliageMap(int id, int face, int slot, const GhostMapInfo* in)
{
    GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !in || slot < 0 || slot >= GHOST_MAP_COUNT) return;
    const int count = GhostFoliage_FaceCount(*L);
    if (face >= count) return;
    const int texCount = g_host.TextureCount ? g_host.TextureCount() : 1;
    const float mx = kFoliMapMax[slot], mn = (slot == GHOST_MAP_WPO) ? -mx : 0.0f;
    GhostMapParams p;
    p.textureId = (in->textureId >= 0 && in->textureId < texCount) ? in->textureId : 0;
    p.amount = Cf(in->amount, mn, mx);
    p.channel = Ci(in->channel, 0, 4);
    p.invert = in->invert ? 1 : 0;
    for (int f = (face < 0 ? 0 : face); f <= (face < 0 ? count - 1 : face); f++) L->faces[f].maps[slot] = p;
    Dirty();
}

int GhostEngine_GetFoliageSurface(int id, int face, GhostSurfaceInfo* out)
{
    const GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !out) return 0;
    if (face < 0) face = 0;
    if (face >= GhostFoliage_FaceCount(*L)) return 0;
    const GhostMaterial& m = L->faces[face];
    *out = { m.twoSided, m.opacity, m.refractionIOR, m.reflectionRotDeg, m.anisotropy,
             m.sssColor[0], m.sssColor[1], m.sssColor[2], m.sssAmount,
             m.mapTiling[0], m.mapTiling[1], m.mapOffset[0], m.mapOffset[1], m.wpoSway, m.wpoSpeed, m.wpoMid };
    return 1;
}

void GhostEngine_SetFoliageSurface(int id, int face, const GhostSurfaceInfo* in)
{
    GhostFoliageLayer* L = GhostFoliage_Find(id);
    if (!L || !in) return;
    const int count = GhostFoliage_FaceCount(*L);
    if (face >= count) return;
    auto nz = [](float v) { return fabsf(v) < 0.001f ? (v < 0.0f ? -0.001f : 0.001f) : v; };
    for (int f = (face < 0 ? 0 : face); f <= (face < 0 ? count - 1 : face); f++)
    {
        GhostMaterial& m = L->faces[f];
        m.twoSided = in->twoSided ? 1 : 0;
        m.opacity = Cf(in->opacity, 0.0f, 1.0f);
        m.refractionIOR = Cf(in->refractionIOR, 1.0f, 3.0f);
        m.reflectionRotDeg = Cf(in->reflectionRotDeg, -720.0f, 720.0f);
        m.anisotropy = Cf(in->anisotropy, -0.95f, 0.95f);
        m.sssColor[0] = Cf(in->sssR, 0, 1); m.sssColor[1] = Cf(in->sssG, 0, 1); m.sssColor[2] = Cf(in->sssB, 0, 1);
        m.sssAmount = Cf(in->sssAmount, 0.0f, 1.0f);
        m.mapTiling[0] = nz(Cf(in->mapTilingU, -1000.0f, 1000.0f)); m.mapTiling[1] = nz(Cf(in->mapTilingV, -1000.0f, 1000.0f));
        m.mapOffset[0] = Cf(in->mapOffsetU, -1000.0f, 1000.0f); m.mapOffset[1] = Cf(in->mapOffsetV, -1000.0f, 1000.0f);
        m.wpoSway = Cf(in->wpoSway, -5.0f, 5.0f);          // this is the foliage wind: amount
        m.wpoSpeed = Cf(in->wpoSpeed, 0.0f, 20.0f);        // wind speed
        m.wpoMid = Cf(in->wpoMid, 0.0f, 1.0f);
    }
    Dirty();
}