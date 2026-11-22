// GhostParticleMan.cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostParticleMan.h"
#include "../include/GhostEngine.h"
#include "../include/GhostScene.h"
#include <map>
#include <math.h>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct Particle { float x, y, z, vx, vy, vz, age, life, size, rot, spin, phase, animOff; };

    // Live state of one emitter (never saved, never undone). Keyed by the emitter's id.
    struct Runtime
    {
        std::vector<Particle> live;
        float    emitAccum = 0.0f;
        uint32_t rng = 1;
        bool     resolved = false;              // the clip below was resolved for rAnim / rClip
        std::string rAnim, rClip;
        int      clipHandle = -1;
        GhostParticleClipInfo clipInfo = {};
    };

    std::vector<GhostParticleDef>* g_defs = nullptr;      // GhostScene::particles
    std::map<int, Runtime>         g_rt;
    int                            g_nextId = 1;
    GhostParticleHost              g_host = {};
    int                            g_defaultTex = 0;

    float CF(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }
    int   CI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    void  Order(float& a, float& b) { if (a > b) { const float t = a; a = b; b = t; } }

    float Rnd(Runtime& r)                       // xorshift32, 0..1
    {
        uint32_t x = r.rng;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        r.rng = x;
        return (float)(x >> 8) * (1.0f / 16777216.0f);
    }
    float RndR(Runtime& r, float a, float b) { return a + (b - a) * Rnd(r); }

    GhostParticleDef* FindDef(int id)
    {
        if (!g_defs) return nullptr;
        for (GhostParticleDef& d : *g_defs) if (d.p.id == id) return &d;
        return nullptr;
    }

    void ResolveClip(const GhostParticleDef& d, Runtime& r)
    {
        r.resolved = true;
        r.rAnim = d.anim; r.rClip = d.clip;
        r.clipHandle = -1;
        r.clipInfo = GhostParticleClipInfo();
        if (d.anim.empty() || !g_host.ResolveClip) return;
        GhostParticleClipInfo info = {};
        const int h = g_host.ResolveClip(d.anim.c_str(), d.clip.c_str(), &info);
        if (h >= 0 && info.cols > 0 && info.rows > 0) { r.clipHandle = h; r.clipInfo = info; }
    }

    void Spawn(const GhostParticleParams& p, Runtime& r)
    {
        Particle q = {};
        const float fx = Rnd(r) - 0.5f, fy = Rnd(r) - 0.5f, fz = Rnd(r) - 0.5f;
        q.x = p.px + fx * p.boxX;
        q.z = p.pz + fz * p.boxZ;
        if (p.spawnMode == 1)      q.y = p.py + p.boxY * 0.5f;
        else if (p.spawnMode == 2) q.y = p.py - p.boxY * 0.5f;
        else                       q.y = p.py + fy * p.boxY;
        q.vx = RndR(r, p.velMinX, p.velMaxX);
        q.vy = RndR(r, p.velMinY, p.velMaxY);
        q.vz = RndR(r, p.velMinZ, p.velMaxZ);
        q.life = RndR(r, p.lifeMin, p.lifeMax);
        q.size = RndR(r, p.sizeMin, p.sizeMax);
        q.rot = RndR(r, p.rotMin, p.rotMax);
        q.spin = RndR(r, p.spinMin, p.spinMax);
        q.phase = Rnd(r) * 6.2831853f;
        q.animOff = Rnd(r);
        r.live.push_back(q);
    }

    void Step(const GhostParticleParams& p, Runtime& r, float dt, bool emit)
    {
        if ((int)r.live.size() > p.maxParticles) r.live.resize((size_t)p.maxParticles);

        if (emit && p.enabled)
        {
            r.emitAccum += p.rate * dt;
            while (r.emitAccum >= 1.0f)
            {
                r.emitAccum -= 1.0f;
                if ((int)r.live.size() < p.maxParticles) Spawn(p, r);
            }
        }

        const float floorY = p.py - p.boxY * 0.5f - 0.01f;
        for (size_t i = 0; i < r.live.size(); )
        {
            Particle& q = r.live[i];
            q.age += dt;
            bool dead = q.age >= q.life;
            if (!dead)
            {
                q.vx += p.accelX * dt; q.vy += p.accelY * dt; q.vz += p.accelZ * dt;
                if (p.drag > 0.0f)
                {
                    float k = 1.0f - p.drag * dt;
                    if (k < 0.0f) k = 0.0f;
                    q.vx *= k; q.vy *= k; q.vz *= k;
                }
                q.x += q.vx * dt; q.y += q.vy * dt; q.z += q.vz * dt;
                if (p.swayAmp > 0.0f)
                {
                    const float w = q.age * p.swayFreq + q.phase;
                    q.x += sinf(w) * p.swayAmp * dt;
                    q.z += cosf(w) * p.swayAmp * dt;
                }
                q.rot += q.spin * dt;
                if (p.killOutside && q.y < floorY) dead = true;
            }
            if (dead) { q = r.live.back(); r.live.pop_back(); }
            else i++;
        }
    }

    // Runs the simulation ahead so the emitter starts full (snow that is already falling)
    void Prewarm(const GhostParticleParams& p, Runtime& r)
    {
        r.live.clear();
        r.emitAccum = 0.0f;
        if (!p.prewarm || !p.enabled) return;
        const float total = p.lifeMax < 15.0f ? p.lifeMax : 15.0f;
        const float h = 1.0f / 20.0f;
        for (float t = 0.0f; t < total; t += h) Step(p, r, h, true);
    }

    // Every emitter in the scene gets live state (new ones start prewarmed), removed ones lose it
    void SyncRuntimes()
    {
        if (!g_defs) return;
        for (GhostParticleDef& d : *g_defs)
        {
            if (d.p.id >= g_nextId) g_nextId = d.p.id + 1;
            auto it = g_rt.find(d.p.id);
            if (it == g_rt.end())
            {
                Runtime n;
                n.rng = 0x9E3779B9u ^ ((uint32_t)d.p.id * 2654435761u);
                if (n.rng == 0) n.rng = 1;
                Prewarm(d.p, n);
                it = g_rt.emplace(d.p.id, std::move(n)).first;
            }
            Runtime& r = it->second;
            if (!r.resolved || r.rAnim != d.anim || r.rClip != d.clip) ResolveClip(d, r);    // may load a .ghostanim: never while rendering
        }
        for (auto it = g_rt.begin(); it != g_rt.end(); )
        {
            if (!FindDef(it->first)) it = g_rt.erase(it);
            else ++it;
        }
    }
}

// ============================================================
//  Module
// ============================================================
void GhostParticles_Bind(std::vector<GhostParticleDef>* defs) { g_defs = defs; }
void GhostParticles_Shutdown() { g_defs = nullptr; g_rt.clear(); g_nextId = 1; }
void GhostParticles_ResetAll() { g_rt.clear(); }
void GhostParticles_Clear() { if (g_defs) g_defs->clear(); g_rt.clear(); }

void GhostParticles_SetHost(const GhostParticleHost* host)
{
    g_host = host ? *host : GhostParticleHost();
    GhostParticles_InvalidateClips();
}

void GhostParticles_SetDefaultTexture(int textureId) { g_defaultTex = textureId < 0 ? 0 : textureId; }

void GhostParticles_InvalidateClips() { for (auto& kv : g_rt) kv.second.resolved = false; }

// A soft round blob: RGB and alpha both carry the falloff, so it works additive (RGB) and cutout (alpha test)
void GhostParticles_MakeSoftTexture(std::vector<unsigned char>& rgba, int size)
{
    if (size < 4) size = 4;
    rgba.assign((size_t)size * (size_t)size * 4, 0);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
        {
            const float dx = ((float)x + 0.5f) / (float)size * 2.0f - 1.0f;
            const float dy = ((float)y + 0.5f) / (float)size * 2.0f - 1.0f;
            const float r = sqrtf(dx * dx + dy * dy);
            float g = (1.0f - r) / 0.55f;
            g = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g);
            const float f = g * g * (3.0f - 2.0f * g);
            const unsigned char v = (unsigned char)(f * 255.0f + 0.5f);
            unsigned char* px = &rgba[((size_t)y * (size_t)size + (size_t)x) * 4];
            px[0] = px[1] = px[2] = v;
            px[3] = v;
        }
}

void GhostParticles_Clamp(GhostParticleParams& p)
{
    p.type = CI(p.type, 0, GHOST_PFX_TYPE_COUNT - 1);
    p.px = CF(p.px, -1e6f, 1e6f); p.py = CF(p.py, -1e6f, 1e6f); p.pz = CF(p.pz, -1e6f, 1e6f);
    p.enabled = p.enabled ? 1 : 0;
    p.rate = CF(p.rate, 0.0f, 2000.0f);
    p.maxParticles = CI(p.maxParticles, 1, GHOST_PFX_MAX_PER_EMITTER);
    p.lifeMin = CF(p.lifeMin, 0.05f, 60.0f); p.lifeMax = CF(p.lifeMax, 0.05f, 60.0f); Order(p.lifeMin, p.lifeMax);
    p.prewarm = p.prewarm ? 1 : 0;
    p.spawnMode = CI(p.spawnMode, 0, 2);
    p.boxX = CF(p.boxX, 0.0f, 1000.0f); p.boxY = CF(p.boxY, 0.0f, 1000.0f); p.boxZ = CF(p.boxZ, 0.0f, 1000.0f);
    p.killOutside = p.killOutside ? 1 : 0;

    p.velMinX = CF(p.velMinX, -1000.0f, 1000.0f); p.velMaxX = CF(p.velMaxX, -1000.0f, 1000.0f); Order(p.velMinX, p.velMaxX);
    p.velMinY = CF(p.velMinY, -1000.0f, 1000.0f); p.velMaxY = CF(p.velMaxY, -1000.0f, 1000.0f); Order(p.velMinY, p.velMaxY);
    p.velMinZ = CF(p.velMinZ, -1000.0f, 1000.0f); p.velMaxZ = CF(p.velMaxZ, -1000.0f, 1000.0f); Order(p.velMinZ, p.velMaxZ);
    p.accelX = CF(p.accelX, -1000.0f, 1000.0f); p.accelY = CF(p.accelY, -1000.0f, 1000.0f); p.accelZ = CF(p.accelZ, -1000.0f, 1000.0f);
    p.drag = CF(p.drag, 0.0f, 50.0f);
    p.swayAmp = CF(p.swayAmp, 0.0f, 20.0f); p.swayFreq = CF(p.swayFreq, 0.0f, 50.0f);

    p.sizeMin = CF(p.sizeMin, 0.001f, 100.0f); p.sizeMax = CF(p.sizeMax, 0.001f, 100.0f); Order(p.sizeMin, p.sizeMax);
    p.sizeEnd = CF(p.sizeEnd, 0.0f, 10.0f);
    p.r0 = CF(p.r0, 0.0f, 1.0f); p.g0 = CF(p.g0, 0.0f, 1.0f); p.b0 = CF(p.b0, 0.0f, 1.0f);
    p.r1 = CF(p.r1, 0.0f, 1.0f); p.g1 = CF(p.g1, 0.0f, 1.0f); p.b1 = CF(p.b1, 0.0f, 1.0f);
    p.alpha0 = CF(p.alpha0, 0.0f, 1.0f); p.alpha1 = CF(p.alpha1, 0.0f, 1.0f);
    p.rotMin = CF(p.rotMin, -3600.0f, 3600.0f); p.rotMax = CF(p.rotMax, -3600.0f, 3600.0f); Order(p.rotMin, p.rotMax);
    p.spinMin = CF(p.spinMin, -3600.0f, 3600.0f); p.spinMax = CF(p.spinMax, -3600.0f, 3600.0f); Order(p.spinMin, p.spinMax);
    p.billboard = p.billboard ? 1 : 0; p.unlit = p.unlit ? 1 : 0; p.additive = p.additive ? 1 : 0;
    p.alphaClip = p.alphaClip ? 1 : 0; p.pixelArt = p.pixelArt ? 1 : 0;

    p.textureId = p.textureId < 0 ? 0 : p.textureId;
    p.source = CI(p.source, 0, 1);
    p.cols = CI(p.cols, 1, 64); p.rows = CI(p.rows, 1, 64);
    p.animFps = CF(p.animFps, 0.0f, 120.0f);
    p.animSpeed = CF(p.animSpeed, 0.0f, 10.0f);
    p.animOverLife = p.animOverLife ? 1 : 0;
    p.randomStartFrame = p.randomStartFrame ? 1 : 0;
    if (p.meshId < 0 || !GhostMesh_Valid(p.meshId)) p.meshId = -1;
    p.drawDistance = CF(p.drawDistance, 1.0f, 500.0f);
}

void GhostParticles_FillPreset(int type, GhostParticleParams& p)
{
    const int   id = p.id;
    const float x = p.px, y = p.py, z = p.pz;
    p = GhostParticleParams();
    p.id = id; p.px = x; p.py = y; p.pz = z;
    p.type = CI(type, 0, GHOST_PFX_TYPE_COUNT - 1);

    // ---- Custom (also the starting point of the other two) ----
    p.enabled = 1; p.rate = 20.0f; p.maxParticles = 100; p.lifeMin = 1.0f; p.lifeMax = 2.0f; p.prewarm = 1;
    p.spawnMode = 0; p.boxX = 0.2f; p.boxY = 0.0f; p.boxZ = 0.2f; p.killOutside = 0;
    p.velMinX = -0.2f; p.velMinY = 0.5f; p.velMinZ = -0.2f;
    p.velMaxX = 0.2f;  p.velMaxY = 1.0f; p.velMaxZ = 0.2f;
    p.swayFreq = 1.0f;
    p.sizeMin = 0.2f; p.sizeMax = 0.4f; p.sizeEnd = 1.0f;
    p.r0 = p.g0 = p.b0 = p.r1 = p.g1 = p.b1 = 1.0f;
    p.alpha0 = p.alpha1 = 1.0f;
    p.rotMin = 0.0f; p.rotMax = 360.0f; p.spinMin = -30.0f; p.spinMax = 30.0f;
    p.billboard = 1; p.unlit = 1; p.additive = 0; p.alphaClip = 1; p.pixelArt = 0;
    p.cols = p.rows = 1; p.animFps = 12.0f; p.animSpeed = 1.0f;
    p.meshId = -1; p.drawDistance = 60.0f;

    if (type == GHOST_PFX_FIRE)
    {
        p.rate = 45.0f; p.maxParticles = 90; p.lifeMin = 0.5f; p.lifeMax = 1.1f;
        p.boxX = 0.35f; p.boxY = 0.0f; p.boxZ = 0.35f;
        p.velMinX = -0.15f; p.velMinY = 0.8f; p.velMinZ = -0.15f;
        p.velMaxX = 0.15f;  p.velMaxY = 1.7f; p.velMaxZ = 0.15f;
        p.accelY = 0.6f; p.drag = 0.4f;
        p.swayAmp = 0.35f; p.swayFreq = 5.0f;
        p.sizeMin = 0.55f; p.sizeMax = 0.95f; p.sizeEnd = 0.15f;
        p.r0 = 1.0f; p.g0 = 0.80f; p.b0 = 0.30f;
        p.r1 = 1.0f; p.g1 = 0.25f; p.b1 = 0.05f;
        p.alpha0 = 1.0f; p.alpha1 = 0.0f;
        p.rotMin = -180.0f; p.rotMax = 180.0f; p.spinMin = -60.0f; p.spinMax = 60.0f;
        p.additive = 1; p.alphaClip = 0;
        p.animOverLife = 1;
    }
    else if (type == GHOST_PFX_SNOW)
    {
        p.rate = 28.0f; p.maxParticles = 280; p.lifeMin = 7.0f; p.lifeMax = 11.0f;
        p.spawnMode = 1; p.killOutside = 1;
        p.boxX = 14.0f; p.boxY = 8.0f; p.boxZ = 14.0f;
        p.velMinX = -0.25f; p.velMinY = -1.3f; p.velMinZ = -0.25f;
        p.velMaxX = 0.25f;  p.velMaxY = -0.7f; p.velMaxZ = 0.25f;
        p.swayAmp = 0.35f; p.swayFreq = 1.2f;
        p.sizeMin = 0.06f; p.sizeMax = 0.14f; p.sizeEnd = 1.0f;
        p.r0 = 0.95f; p.g0 = 0.97f; p.b0 = 1.0f; p.r1 = 0.95f; p.g1 = 0.97f; p.b1 = 1.0f;
        p.rotMin = 0.0f; p.rotMax = 360.0f; p.spinMin = -45.0f; p.spinMax = 45.0f;
        p.additive = 0; p.alphaClip = 1;
        p.drawDistance = 50.0f;
    }
    GhostParticles_Clamp(p);
}

int GhostParticles_Add(int type, float x, float y, float z)
{
    if (!g_defs) return 0;
    if (type < 0 || type >= GHOST_PFX_TYPE_COUNT) return 0;
    if ((int)g_defs->size() >= GHOST_PFX_MAX_EMITTERS) return 0;

    for (const GhostParticleDef& d : *g_defs) if (d.p.id >= g_nextId) g_nextId = d.p.id + 1;

    GhostParticleDef d;
    GhostParticles_FillPreset(type, d.p);
    d.p.id = g_nextId++;
    d.p.px = x; d.p.py = y; d.p.pz = z;
    if (type == GHOST_PFX_SNOW) d.p.py += d.p.boxY * 0.5f;      // (x,y,z) is a ground point: the snow volume rests on it
    g_defs->push_back(d);                                       // live state is created (and prewarmed) by the next Update
    return d.p.id;
}

// ============================================================
//  Per frame
// ============================================================
void GhostParticles_Update(float dt)
{
    if (!g_defs) return;
    SyncRuntimes();                               // also while paused, so a new emitter exists before it is drawn
    if (!(dt > 0.0f)) return;                     // paused (or NaN)
    if (dt > 0.1f) dt = 0.1f;
    for (const GhostParticleDef& d : *g_defs)
    {
        auto it = g_rt.find(d.p.id);
        if (it != g_rt.end()) Step(d.p, it->second, dt, true);
    }
}

int GhostParticles_BuildDraws(const GhostVec3& eye, std::vector<GhostParticleDraw>& out, unsigned int maxDraws)
{
    out.clear();
    if (!g_defs) return 0;
    for (const GhostParticleDef& def : *g_defs)
    {
        auto it = g_rt.find(def.p.id);
        if (it == g_rt.end() || it->second.live.empty()) continue;
        const Runtime& e = it->second;
        const GhostParticleParams& p = def.p;
        const bool useMesh = GhostMesh_Valid(p.meshId) && p.meshId != GHOST_MESH_QUAD;
        const int  meshId = useMesh ? p.meshId : (int)GHOST_MESH_QUAD;
        const float maxD2 = p.drawDistance * p.drawDistance;

        for (const Particle& q : e.live)
        {
            if (out.size() >= maxDraws) return (int)out.size();

            const float dx = q.x - eye.x, dy = q.y - eye.y, dz = q.z - eye.z;
            if (dx * dx + dy * dy + dz * dz > maxD2) continue;

            const float t = q.life > 1e-4f ? (q.age / q.life) : 1.0f;
            const float a = p.alpha0 + (p.alpha1 - p.alpha0) * t;
            const float size = q.size * (1.0f + (p.sizeEnd - 1.0f) * t);
            if (a < 0.01f || size < 0.001f) continue;

            GhostParticleDraw d = {};
            d.x = q.x; d.y = q.y; d.z = q.z;
            d.size = size;
            d.rot = q.rot;
            const float k = p.additive ? a : 1.0f;       // additive particles fade by getting darker
            d.r = (p.r0 + (p.r1 - p.r0) * t) * k;
            d.g = (p.g0 + (p.g1 - p.g0) * t) * k;
            d.b = (p.b0 + (p.b1 - p.b0) * t) * k;
            d.meshId = meshId;
            d.billboard = p.billboard; d.unlit = p.unlit; d.additive = p.additive;
            d.alphaClip = p.alphaClip; d.pixelArt = p.pixelArt;

            int tex = p.textureId > 0 ? p.textureId : (useMesh ? 0 : g_defaultTex);
            int cols = 1, rows = 1, frame = 0;
            if (!useMesh)
            {
                if (e.clipHandle >= 0 && g_host.FrameAt)               // a .ghostanim clip
                {
                    const GhostParticleClipInfo& ci = e.clipInfo;
                    if (ci.textureId > 0) tex = ci.textureId;
                    cols = ci.cols; rows = ci.rows;
                    const float dur = ci.duration > 1e-4f ? ci.duration : 1.0f;
                    float tm;
                    if (p.animOverLife) tm = t * dur;
                    else
                    {
                        tm = q.age * p.animSpeed + (p.randomStartFrame ? q.animOff * dur : 0.0f);
                        if (ci.loop) tm = fmodf(tm, dur);
                    }
                    frame = g_host.FrameAt(e.clipHandle, tm);
                }
                else if (p.source == 1 && p.cols * p.rows > 1)         // a plain sprite sheet
                {
                    cols = p.cols; rows = p.rows;
                    const int n = cols * rows;
                    if (p.animOverLife) { frame = (int)(t * (float)n); if (frame > n - 1) frame = n - 1; }
                    else
                    {
                        frame = (int)(q.age * p.animFps * p.animSpeed) + (p.randomStartFrame ? (int)(q.animOff * (float)n) : 0);
                        frame = ((frame % n) + n) % n;
                    }
                }
            }
            if (cols < 1) cols = 1;
            if (rows < 1) rows = 1;
            if (frame < 0) frame = 0;
            if (frame > cols * rows - 1) frame = cols * rows - 1;

            d.textureId = tex;
            d.uvScaleX = 1.0f / (float)cols;
            d.uvScaleY = 1.0f / (float)rows;
            d.uvOffX = ((float)(frame % cols) + 0.5f) / (float)cols - 0.5f;
            d.uvOffY = ((float)(frame / cols) + 0.5f) / (float)rows - 0.5f;
            out.push_back(d);
        }
    }
    return (int)out.size();
}

// Billboards use the camera's own axes (so they face the camera in every direction, rolled by rot).
// A quad's front is -Z, so local Z points away from the camera.
GhostMat4 GhostParticles_Model(const GhostParticleDraw& d, const GhostVec3& camRight, const GhostVec3& camUp, const GhostVec3& camFwd)
{
    const float d2r = GHOST_PI / 180.0f;
    if (d.billboard)
    {
        const float c = cosf(d.rot * d2r), s = sinf(d.rot * d2r);
        const GhostVec3 ax = camRight * c + camUp * s;
        const GhostVec3 ay = camUp * c - camRight * s;
        GhostMat4 m = GhostMat4::Identity();
        m.m[0][0] = ax.x * d.size;      m.m[1][0] = ax.y * d.size;      m.m[2][0] = ax.z * d.size;
        m.m[0][1] = ay.x * d.size;      m.m[1][1] = ay.y * d.size;      m.m[2][1] = ay.z * d.size;
        m.m[0][2] = camFwd.x * d.size;  m.m[1][2] = camFwd.y * d.size;  m.m[2][2] = camFwd.z * d.size;
        m.m[0][3] = d.x;                m.m[1][3] = d.y;                m.m[2][3] = d.z;
        return m;
    }
    return GhostMat4::Translation(d.x, d.y, d.z) * GhostMat4::RotationY(d.rot * d2r) * GhostMat4::Scale(d.size, d.size, d.size);
}

// ============================================================
//  Exported API (declared in GhostEngine.h) — unchanged, so the C# side stays as it is
// ============================================================
int GhostEngine_AddParticleAt(int type, float x, float y, float z) { return GhostParticles_Add(type, x, y, z); }

int GhostEngine_RemoveParticle(int id)
{
    if (!g_defs) return 0;
    for (size_t i = 0; i < g_defs->size(); i++)
        if ((*g_defs)[i].p.id == id)
        {
            g_defs->erase(g_defs->begin() + (std::ptrdiff_t)i);
            g_rt.erase(id);
            return 1;
        }
    return 0;
}

int GhostEngine_GetParticleCount() { return g_defs ? (int)g_defs->size() : 0; }

int GhostEngine_GetParticleIds(int* outIds, int maxCount)
{
    if (!g_defs || !outIds || maxCount <= 0) return 0;
    int n = 0;
    for (const GhostParticleDef& d : *g_defs) { if (n >= maxCount) break; outIds[n++] = d.p.id; }
    return n;
}

int GhostEngine_GetParticle(int id, GhostParticleParams* out)
{
    GhostParticleDef* d = FindDef(id);
    if (!d || !out) return 0;
    *out = d->p;
    return 1;
}

void GhostEngine_SetParticle(const GhostParticleParams* in)
{
    if (!in) return;
    GhostParticleDef* d = FindDef(in->id);
    if (!d) return;
    GhostParticleParams n = *in;
    n.id = d->p.id;
    n.type = d->p.type;
    GhostParticles_Clamp(n);
    d->p = n;                                       // the pool is trimmed to maxParticles by the next Step
}

// which: 0 animation file (.ghostanim), 1 clip, 2 (get only) clip names joined with '|'
int GhostEngine_GetParticleText(int id, int which, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    buf[0] = 0;
    GhostParticleDef* d = FindDef(id);
    if (!d) return 0;
    if (which == 2)
        return (g_host.ClipNames && !d->anim.empty()) ? g_host.ClipNames(d->anim.c_str(), buf, bufSize) : 0;
    const std::string& t = (which == 0) ? d->anim : d->clip;
    strncpy_s(buf, bufSize, t.c_str(), _TRUNCATE);
    return 1;
}

void GhostEngine_SetParticleText(int id, int which, const char* utf8)
{
    GhostParticleDef* d = FindDef(id);
    if (!d) return;
    std::string t = utf8 ? utf8 : "";
    if (which == 0)
    {
        if (t.size() > 259) t.resize(259);
        if (t != d->anim) { d->anim = t; d->clip.clear(); }
    }
    else if (which == 1)
    {
        if (t.size() > 47) t.resize(47);
        d->clip = t;
    }
}

void GhostEngine_ApplyParticlePreset(int id, int type)
{
    GhostParticleDef* d = FindDef(id);
    if (!d || type < 0 || type >= GHOST_PFX_TYPE_COUNT) return;
    GhostParticles_FillPreset(type, d->p);          // keeps id and position
    g_rt.erase(id);                                 // starts over (prewarmed) on the next Update
}

void GhostEngine_RestartParticle(int id) { g_rt.erase(id); }

int GhostEngine_GetParticleLive(int id)
{
    auto it = g_rt.find(id);
    return it == g_rt.end() ? 0 : (int)it->second.live.size();
}