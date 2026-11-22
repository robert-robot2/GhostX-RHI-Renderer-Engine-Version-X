// GhostScene.h — backend-agnostic scene model
#pragma once
#include "GhostMath.h"
#include "GhostRoleParams.h"
#include "GhostParticleMan.h"
#include <vector>
#include <string>
#include <string.h>

enum GhostMeshId {
    GHOST_MESH_CUBE = 0,
    GHOST_MESH_TRIANGLE,
    GHOST_MESH_SHIP,
    GHOST_MESH_SQUARE,
    GHOST_MESH_QUAD,
    // New primitives
    GHOST_MESH_CYLINDER,
    GHOST_MESH_CONE,
    GHOST_MESH_CAPSULE,
    GHOST_MESH_PLANE,
    GHOST_MESH_UVSPHERE,
    GHOST_MESH_ICOSPHERE,
    GHOST_MESH_TORUS,
    GHOST_MESH_PYRAMID,
    GHOST_MESH_WEDGE,
    GHOST_MESH_DISC,
    GHOST_MESH_COUNT
};
enum GhostRole {
    GHOST_ROLE_STATIC = 0, GHOST_ROLE_PLAYER, GHOST_ROLE_ENEMY, GHOST_ROLE_COIN,
    GHOST_ROLE_BLOCK, GHOST_ROLE_GOAL, GHOST_ROLE_CUSTOM,
    GHOST_ROLE_PORTAL_ENTRY, GHOST_ROLE_PORTAL_EXIT, GHOST_ROLE_BREAKABLE,
    GHOST_ROLE_MOVER, GHOST_ROLE_PROJECTILE, GHOST_ROLE_BOSS, GHOST_ROLE_HIDDEN, GHOST_ROLE_COUNT
};
enum class GhostLightType : int { Directional = 0, Point = 1, RectArea = 2, Spot = 3 };
constexpr unsigned int GHOST_MAX_LIGHTS = 16;
constexpr unsigned int GHOST_MAX_CAMERAS = 4;
// face cap???
constexpr int GHOST_MAX_FACES = 32;
constexpr int GHOST_FIRST_USER_TEXTURE = 3;     // ids 0..2 are the built-ins


constexpr int GHOST_MAP_COUNT = 13;
enum GhostMapSlot {
    GHOST_MAP_NORMAL = 0, GHOST_MAP_SPECULAR, GHOST_MAP_METALLIC, GHOST_MAP_ROUGHNESS, GHOST_MAP_AO,
    GHOST_MAP_EMISSIVE, GHOST_MAP_OPACITY, GHOST_MAP_MASK, GHOST_MAP_TANGENT, GHOST_MAP_WPO,
    GHOST_MAP_REFRACTION, GHOST_MAP_REFLECTION, GHOST_MAP_CUSTOM
};
inline const char* GhostMapKey(int k)
{
    static const char* n[GHOST_MAP_COUNT] = { "normal", "specular", "metallic", "roughness", "ao", "emissive",
        "opacity", "opacityMask", "tangent", "wpo", "refraction", "reflection", "custom" };
    return (k >= 0 && k < GHOST_MAP_COUNT) ? n[k] : "";
}
struct GhostMapParams
{
    int   textureId = 0;     // 0 = no map
    float amount = 1.0f;     // meaning depends on the map (strength, blend, intensity)
    int   channel = 0;       // 0 R, 1 G, 2 B, 3 A, 4 luminance (scalar maps)
    int   invert = 0;        // scalar maps: 1 - value. Normal map: flip green
};


struct GhostMaterial
{
    int   textureId = 0;                      // 0 = built-in white
    int   mapMode = 0;                        // 0 Mesh UV, 1 Box, 2 Planar XY, 3 Planar XZ, 4 Planar YZ,
    // 5 Cylindrical, 6 Spherical, 7 Cube Cross
    int   wrap = 0;                           // 0 repeat, 1 clamp
    int   filter = 0;                         // 0 linear, 1 point
    float tiling[2] = { 1, 1 };
    float offset[2] = { 0, 0 };
    float rotationDeg = 0.0f;
    float tint[3] = { 0.6f, 0.6f, 0.6f };     // grey by default
    float metallic = 0.0f;                    // 0 = dielectric, 1 = metal
    float roughness = 0.6f;                   // 0 = mirror, 1 = fully rough
    float specular = 0.5f;                    // dielectric reflectance (0.5 = the standard 4%)
    float emissive[3] = { 0, 0, 0 };          // glow color (display colors, like the tint)
    float emissiveStrength = 1.0f;
    int   alphaMode = 0;       // 0 opaque, 1 masked (alpha test), 2 translucent (blended)
    int   alphaSource = 0;
    float alphaCutoff = 0.5f;
    GhostMapParams maps[GHOST_MAP_COUNT];
    float mapTiling[2] = { 1, 1 };            // shared by all extra maps of this slot
    float mapOffset[2] = { 0, 0 };
    float opacity = 1.0f;
    int   twoSided = 1;
    float refractionIOR = 1.5f;
    float reflectionRotDeg = 0.0f;
    float anisotropy = 0.0f;
    float sssColor[3] = { 1.0f, 0.35f, 0.25f };
    float sssAmount = 0.0f;
    float wpoSway = 0.0f, wpoSpeed = 1.0f, wpoMid = 0.5f;
    GhostMaterial() { maps[GHOST_MAP_WPO].amount = 0.02f; }     // 2 cm unless you change it
};

inline bool GhostMaterial_Equal(const GhostMaterial& a, const GhostMaterial& b)
{
    return memcmp(&a, &b, sizeof(GhostMaterial)) == 0;
}



// ============================================================
//  Mesh registry: built-ins first (ids 0..GHOST_MESH_COUNT-1), imported models after
// ============================================================
constexpr int GHOST_MAX_MESHES = 64;

// A face (material slot) is a contiguous range of the mesh's index buffer
struct GhostFaceDef { std::string name; unsigned int firstIndex = 0, indexCount = 0; GhostMaterial mat; bool hasMat = false; };

struct GhostMeshInfo
{
    std::string  name;
    std::string  source;                           // file an imported model came from ("" for built-ins)
    std::string  file;                             // the converted .ghostmesh (absolute, normalized); "" = this session only
    bool         builtin = true;
    bool         index32 = false;                  // imported meshes use 32-bit indices
    GhostVec3    bmin = { -0.5f, -0.5f, -0.5f };   // local-space bounds: picking, shadows, drop-to-ground
    GhostVec3    bmax = { 0.5f,  0.5f,  0.5f };
    GhostVec3    defaultScale = { 1, 1, 1 };
    unsigned int vertexCount = 0, triangleCount = 0;
    float        importScale = 1.0f, importFit = 0.0f;   // the options the model was imported with (re-import reuses them)
    int          importUp = 0, importFlags = 0;          // flags: 1 = flip winding, 2 = no materials
    std::vector<GhostFaceDef> faces;
};

inline std::vector<GhostMeshInfo> GhostBuildBuiltinMeshes()
{
    std::vector<GhostMeshInfo> v(GHOST_MESH_COUNT);
    const std::vector<GhostFaceDef> cubeFaces = {
        { "Front", 0, 6 },{ "Back", 6, 6 },{ "Top", 12, 6 },{ "Bottom", 18, 6 },{ "Left", 24, 6 },{ "Right", 30, 6 } };
    const std::vector<GhostFaceDef> shipFaces = {       // one triangle each; names are approximate
        { "Hull Right", 0, 3 },{ "Hull Left", 3, 3 },{ "Hull Rear", 6, 3 },
        { "Hull Bottom", 9, 3 },{ "Wing Right", 12, 3 },{ "Wing Left", 15, 3 } };

    auto def = [&](int id, const char* name, GhostVec3 mn, GhostVec3 mx, GhostVec3 sc,
        unsigned int verts, unsigned int tris, const std::vector<GhostFaceDef>& faces)
        {
            GhostMeshInfo& m = v[(size_t)id];
            m.name = name; m.builtin = true; m.index32 = false;
            m.bmin = mn; m.bmax = mx; m.defaultScale = sc;
            m.vertexCount = verts; m.triangleCount = tris; m.faces = faces;
        };
    def(GHOST_MESH_CUBE, "Cube", { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f }, { 1, 1, 1 }, 24, 12, cubeFaces);
    def(GHOST_MESH_TRIANGLE, "Triangle", { -0.5f, 0.0f, -0.05f }, { 0.5f, 1.0f, 0.05f }, { 1, 1, 1 }, 3, 1, { { "Face", 0, 3 } });
    def(GHOST_MESH_SHIP, "Ship", { -3.2f, -1.0f, -3.0f }, { 3.2f, 3.0f, 11.0f }, { 0.1f, 0.1f, 0.1f }, 10, 6, shipFaces);
    def(GHOST_MESH_SQUARE, "Square", { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f }, { 1, 1, 0.25f }, 24, 12, cubeFaces);
    def(GHOST_MESH_QUAD, "Sprite", { -0.5f, -0.5f, -0.25f }, { 0.5f, 0.5f, 0.25f }, { 1, 1, 1 }, 4, 2, { { "Face", 0, 6 } });
    def(GHOST_MESH_CYLINDER, "Cylinder", { -0.5f,-0.5f,-0.5f }, { 0.5f, 0.5f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Top",0,0 },{ "Bottom",0,0 },{ "Side",0,0 } });
    def(GHOST_MESH_CONE, "Cone", { -0.5f,-0.5f,-0.5f }, { 0.5f, 0.5f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Bottom",0,0 },{ "Side",0,0 } });
    def(GHOST_MESH_CAPSULE, "Capsule", { -0.5f,-1.0f,-0.5f }, { 0.5f, 1.0f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Body",0,0 } });
    def(GHOST_MESH_PLANE, "Plane", { -0.5f, 0.0f,-0.5f }, { 0.5f, 0.0f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Face",0,0 } });
    def(GHOST_MESH_UVSPHERE, "UV Sphere", { -0.5f,-0.5f,-0.5f }, { 0.5f, 0.5f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Body",0,0 } });
    def(GHOST_MESH_ICOSPHERE, "Ico Sphere", { -0.5f,-0.5f,-0.5f }, { 0.5f, 0.5f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Body",0,0 } });
    def(GHOST_MESH_TORUS, "Torus", { -0.5f,-0.15f,-0.5f }, { 0.5f, 0.15f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Body",0,0 } });
    def(GHOST_MESH_PYRAMID, "Pyramid", { -0.5f,-0.5f,-0.5f }, { 0.5f, 0.5f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Bottom",0,0 },{ "Front",0,0 },{ "Back",0,0 },{ "Left",0,0 },{ "Right",0,0 } });
    def(GHOST_MESH_WEDGE, "Wedge", { -0.5f,-0.5f,-0.5f }, { 0.5f, 0.5f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Bottom",0,0 },{ "Back",0,0 },{ "Slope",0,0 },{ "Left",0,0 },{ "Right",0,0 } });
    def(GHOST_MESH_DISC, "Disc", { -0.5f,-0.02f,-0.5f }, { 0.5f, 0.02f, 0.5f }, { 1,1,1 }, 0, 0,
        { { "Top",0,0 },{ "Bottom",0,0 },{ "Side",0,0 } });
    return v;
}

inline std::vector<GhostMeshInfo>& GhostMeshes()
{
    static std::vector<GhostMeshInfo> meshes = GhostBuildBuiltinMeshes();
    return meshes;
}

inline int  GhostMesh_Count() { return (int)GhostMeshes().size(); }
inline bool GhostMesh_Valid(int id) { return id >= 0 && id < GhostMesh_Count(); }

inline const GhostMeshInfo* GhostMesh_Info(int id)
{
    return GhostMesh_Valid(id) ? &GhostMeshes()[(size_t)id] : nullptr;
}

inline int GhostMesh_Register(const GhostMeshInfo& info)    // new id, or -1 if the registry is full
{
    std::vector<GhostMeshInfo>& v = GhostMeshes();
    if ((int)v.size() >= GHOST_MAX_MESHES) return -1;
    v.push_back(info);
    return (int)v.size() - 1;
}

inline int GhostMesh_FindByName(const std::string& name)
{
    const std::vector<GhostMeshInfo>& v = GhostMeshes();
    for (size_t i = 0; i < v.size(); i++)
        if (_stricmp(v[i].name.c_str(), name.c_str()) == 0) return (int)i;
    return -1;
}

inline int GhostMesh_FaceCount(int id)
{
    const GhostMeshInfo* m = GhostMesh_Info(id);
    if (!m) return 0;
    const int n = (int)m->faces.size();
    return n > GHOST_MAX_FACES ? GHOST_MAX_FACES : n;
}

inline const GhostFaceDef& GhostMesh_Face(int id, int face)
{
    static const GhostFaceDef none = { "Face", 0, 0 };
    const GhostMeshInfo* m = GhostMesh_Info(id);
    if (!m || m->faces.empty()) return none;
    if (face < 0 || face >= (int)m->faces.size()) face = 0;
    return m->faces[(size_t)face];
}

inline GhostVec3 GhostMesh_Min(int id) { const GhostMeshInfo* m = GhostMesh_Info(id); return m ? m->bmin : GhostVec3(-0.5f, -0.5f, -0.5f); }
inline GhostVec3 GhostMesh_Max(int id) { const GhostMeshInfo* m = GhostMesh_Info(id); return m ? m->bmax : GhostVec3(0.5f, 0.5f, 0.5f); }

// The collider that fits a mesh exactly: local size and the local-space center
inline void GhostMesh_FitCollider(const GhostMeshInfo& m, GhostVec3& size, GhostVec3& offset)
{
    auto d = [](float v) { v = v < 0.0f ? -v : v; return v < 0.01f ? 0.01f : v; };
    size = { d(m.bmax.x - m.bmin.x), d(m.bmax.y - m.bmin.y), d(m.bmax.z - m.bmin.z) };
    offset = { (m.bmin.x + m.bmax.x) * 0.5f, (m.bmin.y + m.bmax.y) * 0.5f, (m.bmin.z + m.bmax.z) * 0.5f };
}

struct GhostTextureInfo
{
    int          id = 0;           // doubles as the SRV slot index in the pipeline
    std::string  name;
    unsigned int width = 0, height = 0;
    std::string  path;             // absolute UTF-8 source file ("" for built-ins)
};
struct GhostSprite
{
    int   textureId = 0;           // the sheet (0 = white)
    int   source = 0;              // 0 = single image, 1 = sprite sheet
    int   cols = 1, rows = 1;      // used when source = 1
    int   frame = 0;               // shown when no animation is assigned (row-major, 0 = top-left)
    bool  flipX = false;
    bool  billboard = true;        // always faces the camera (Y axis only)
    bool  unlit = false;
    bool  alphaClip = true;        // transparent pixels are discarded
    bool  pixelArt = true;         // point filter
    bool  playing = true;
    float tint[3] = { 1, 1, 1 };
    float animSpeed = 1.0f;
    std::string anim;              // stored path of a .ghostanim ("" = none)
    std::string clip;              // current clip ("" = the animation's first clip)

    // runtime only (not saved, not hashed)
    int   animId = -1;
    bool  animResolved = false;
    float animTime = 0.0f;
    int   curFrame = 0;
};

// 3D skeletal animation state of an imported model (the 3D twin of GhostSprite's animation fields)
struct GhostAnimator
{
    std::string anim;              // stored path of a .ghostanim ("" = none)
    std::string clip;              // current clip ("" = Idle, or the first clip)
    bool  playing = true;
    float speed = 1.0f;

    // runtime only (not saved, not hashed)
    int   animId = -1;
    bool  animResolved = false;
    float time = 0.0f;
    // runtime only: written by the animation blueprint each frame, read by GhostObjectModel
    float     yawOffset = 0.0f;
    float     leanDeg = 0.0f;
    GhostVec3 scaleMul = { 1, 1, 1 };
};


// ---- Tile Map (Maps > Tile Map). A GhostObject with tileMap.enabled = true ----
constexpr int GHOST_TILEMAP_MAX_GRID = 256;
constexpr int GHOST_TILEMAP_MAX_LAYERS = 8;           // material layers per Tile Map (the weight maps hold 8)
constexpr int GHOST_TILEMAP_MAX_VERTS = 262144;       // terrain vertex budget: the detail setting backs off to fit
constexpr int GHOST_TILEMAP_MAX_SPLAT = 512;          // weight map size limit per side

// A new number every time terrain data changes. Every state of the data has its own,
// so undo / redo, the unsaved-changes check and GPU rebuilds can compare numbers instead of data.
inline unsigned int GhostTile_NextStamp() { static unsigned int c = 0; return ++c; }

struct GhostTileMap
{
    bool  enabled = false;
    float tileSize = 1.0f;                  // world units per tile
    int   gridX = 10, gridY = 10;           // tiles along the grid's two axes
    int   gridZ = 1;                        // reserved
    int   direction = 1;                    // axis the grid faces: 0 X (wall), 1 Y (floor), 2 Z (wall toward the default camera)
    int   shadowLightId = 0;                // 0 = every light shadows the landscape, otherwise only this directional light
    int   layerCount = 0;                   // material layers in use (materials are GhostObject::faces[0 .. layerCount-1])
    int   subdiv = 4;                       // terrain quads per tile side (lowered automatically for big maps)
    bool  autoCollider = true;              // collider box follows the terrain bounds
    std::vector<float> height;              // (gridX*sub+1) * (gridY*sub+1), row = V. Displacement along the facing axis, object units
    std::vector<unsigned char> splat;       // splatW * splatH * 8: weight of layers 0..7 per texel (sums to 255)
    int   splatW = 0, splatH = 0;           // the size 'splat' was built for
    float hMin = 0.0f, hMax = 0.0f;         // height range (for picking bounds)
    unsigned int hStamp = 0, sStamp = 0;    // change stamps of the height data and the weight data
};

inline void GhostTileMap_Clamp(GhostTileMap& t)
{
    auto ci = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    if (!(t.tileSize >= 0.01f)) t.tileSize = 0.01f;          // also catches NaN
    if (t.tileSize > 100.0f)    t.tileSize = 100.0f;
    t.gridX = ci(t.gridX, 1, GHOST_TILEMAP_MAX_GRID);
    t.gridY = ci(t.gridY, 1, GHOST_TILEMAP_MAX_GRID);
    t.gridZ = ci(t.gridZ, 1, 64);
    t.direction = ci(t.direction, 0, 2);
    t.layerCount = ci(t.layerCount, 0, GHOST_TILEMAP_MAX_LAYERS);
    t.subdiv = ci(t.subdiv, 1, 16);
}

// The detail actually used: the requested one, lowered until the mesh fits the vertex budget
inline int GhostTileMap_EffSub(int gx, int gy, int req)
{
    int s = req < 1 ? 1 : (req > 16 ? 16 : req);
    while (s > 1 && (long long)(gx * s + 1) * (long long)(gy * s + 1) > GHOST_TILEMAP_MAX_VERTS) s--;
    return s;
}

inline void GhostTileMap_SplatSize(int gx, int gy, int& w, int& h)
{
    auto c = [](int v) { return v < 32 ? 32 : (v > GHOST_TILEMAP_MAX_SPLAT ? GHOST_TILEMAP_MAX_SPLAT : v); };
    w = c(gx * 16);
    h = c(gy * 16);
}

inline void GhostTileMap_UpdateRange(GhostTileMap& t)
{
    float lo = 0.0f, hi = 0.0f;
    for (float h : t.height) { if (h < lo) lo = h; if (h > hi) hi = h; }
    t.hMin = lo; t.hMax = hi;
}

// Rebuilds the height and weight arrays for the current grid / detail. The old arrays (built for oldGX x oldGY tiles,
// oldSub quads per tile, an oldSW x oldSH weight map) are carried over where they still fit; new areas start flat on layer 0.
inline void GhostTileMap_Reshape(GhostTileMap& t, int oldGX, int oldGY, int oldSub, int oldSW, int oldSH)
{
    const int sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    const int vx = t.gridX * sub + 1, vy = t.gridY * sub + 1;
    const int ovx = oldGX * oldSub + 1, ovy = oldGY * oldSub + 1;

    std::vector<float> nh((size_t)vx * (size_t)vy, 0.0f);
    if (oldGX > 0 && oldGY > 0 && oldSub > 0 && t.height.size() == (size_t)ovx * (size_t)ovy)
    {
        for (int q = 0; q < vy; q++)
        {
            const float ty = (float)q / (float)sub;                    // position in tiles
            if (ty > (float)oldGY + 1e-4f) break;
            const float fy = ty * (float)oldSub;
            int y0 = (int)fy; if (y0 > ovy - 2) y0 = ovy - 2; if (y0 < 0) y0 = 0;
            const float wy = fy - (float)y0;
            for (int p = 0; p < vx; p++)
            {
                const float tx = (float)p / (float)sub;
                if (tx > (float)oldGX + 1e-4f) break;
                const float fx = tx * (float)oldSub;
                int x0 = (int)fx; if (x0 > ovx - 2) x0 = ovx - 2; if (x0 < 0) x0 = 0;
                const float wx = fx - (float)x0;
                const float* r0 = &t.height[(size_t)y0 * ovx + x0];
                const float* r1 = r0 + ovx;
                const float a = r0[0] + (r0[1] - r0[0]) * wx, b = r1[0] + (r1[1] - r1[0]) * wx;
                nh[(size_t)q * vx + p] = a + (b - a) * wy;
            }
        }
    }
    t.height.swap(nh);

    int nw, nhh;
    GhostTileMap_SplatSize(t.gridX, t.gridY, nw, nhh);
    std::vector<unsigned char> ns((size_t)nw * (size_t)nhh * 8, 0);
    for (size_t i = 0; i < (size_t)nw * (size_t)nhh; i++) ns[i * 8] = 255;               // everything on layer 0
    if (oldGX > 0 && oldGY > 0 && oldSW > 0 && oldSH > 0 && t.splat.size() == (size_t)oldSW * (size_t)oldSH * 8)
    {
        for (int r = 0; r < nhh; r++)
        {
            const float ty = ((float)r + 0.5f) / (float)nhh * (float)t.gridY;           // tile coordinate in the new map
            if (ty >= (float)oldGY) break;
            int orow = (int)(ty / (float)oldGY * (float)oldSH); if (orow >= oldSH) orow = oldSH - 1;
            for (int c = 0; c < nw; c++)
            {
                const float tx = ((float)c + 0.5f) / (float)nw * (float)t.gridX;
                if (tx >= (float)oldGX) break;
                int ocol = (int)(tx / (float)oldGX * (float)oldSW); if (ocol >= oldSW) ocol = oldSW - 1;
                memcpy(&ns[((size_t)r * nw + c) * 8], &t.splat[((size_t)orow * oldSW + ocol) * 8], 8);
            }
        }
    }
    t.splat.swap(ns);
    t.splatW = nw; t.splatH = nhh;
    GhostTileMap_UpdateRange(t);
    t.hStamp = GhostTile_NextStamp();
    t.sStamp = GhostTile_NextStamp();
}

// Keeps the arrays the right size (new Tile Map, loaded scene, restored undo step)
inline void GhostTileMap_EnsureTiles(GhostTileMap& t)
{
    const int sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    int sw, sh;
    GhostTileMap_SplatSize(t.gridX, t.gridY, sw, sh);
    const bool hOk = t.height.size() == (size_t)(t.gridX * sub + 1) * (size_t)(t.gridY * sub + 1);
    const bool sOk = t.splatW == sw && t.splatH == sh && t.splat.size() == (size_t)sw * (size_t)sh * 8;
    if (hOk && sOk) return;
    GhostTileMap_Reshape(t, t.gridX, t.gridY, sub, t.splatW, t.splatH);
}

// Height at a point of the grid (u, v = object-space coordinates along the grid's two axes). Outside the grid = 0.
inline float GhostTileMap_Height(const GhostTileMap& t, float u, float v)
{
    const int sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    const int vx = t.gridX * sub + 1, vy = t.gridY * sub + 1;
    if (t.height.size() != (size_t)vx * (size_t)vy) return 0.0f;
    const float cell = t.tileSize / (float)sub;
    const float fx = (u + 0.5f * (float)t.gridX * t.tileSize) / cell;
    const float fy = (v + 0.5f * (float)t.gridY * t.tileSize) / cell;
    if (fx < 0.0f || fy < 0.0f || fx >(float)(vx - 1) || fy >(float)(vy - 1)) return 0.0f;
    int x0 = (int)fx, y0 = (int)fy;
    if (x0 > vx - 2) x0 = vx - 2;
    if (y0 > vy - 2) y0 = vy - 2;
    const float wx = fx - (float)x0, wy = fy - (float)y0;
    const float* r0 = &t.height[(size_t)y0 * vx + x0];
    const float* r1 = r0 + vx;
    const float a = r0[0] + (r0[1] - r0[0]) * wx, b = r1[0] + (r1[1] - r1[0]) * wx;
    return a + (b - a) * wy;
}

// Height of the terrain exactly as the mesh is drawn (each cell = 2 triangles, split along the i01-i10 diagonal).
// Use this for collision. GhostTileMap_Height (bilinear) can differ from the drawn surface by a hair.
inline float GhostTileMap_HeightMesh(const GhostTileMap& t, float u, float v)
{
    const int sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    const int vx = t.gridX * sub + 1, vy = t.gridY * sub + 1;
    if (t.height.size() != (size_t)vx * (size_t)vy) return 0.0f;
    const float cell = t.tileSize / (float)sub;
    const float fx = (u + 0.5f * (float)t.gridX * t.tileSize) / cell;
    const float fy = (v + 0.5f * (float)t.gridY * t.tileSize) / cell;
    if (fx < 0.0f || fy < 0.0f || fx >(float)(vx - 1) || fy >(float)(vy - 1)) return 0.0f;
    int x0 = (int)fx, y0 = (int)fy;
    if (x0 > vx - 2) x0 = vx - 2;
    if (y0 > vy - 2) y0 = vy - 2;
    const float wx = fx - (float)x0, wy = fy - (float)y0;
    const float h00 = t.height[(size_t)y0 * vx + x0], h10 = t.height[(size_t)y0 * vx + x0 + 1];
    const float h01 = t.height[(size_t)(y0 + 1) * vx + x0], h11 = t.height[(size_t)(y0 + 1) * vx + x0 + 1];
    if (wx + wy <= 1.0f) return h00 + (h10 - h00) * wx + (h01 - h00) * wy;          // triangle (i00, i01, i10)
    return h11 + (h01 - h11) * (1.0f - wx) + (h10 - h11) * (1.0f - wy);              // triangle (i01, i11, i10)
}

// Half extents of the flat grid in object space (thin on the facing axis)
inline GhostVec3 GhostTileMap_Half(const GhostTileMap& t)
{
    const float hu = t.gridX * t.tileSize * 0.5f, hv = t.gridY * t.tileSize * 0.5f, ht = 0.05f;
    if (t.direction == 0) return GhostVec3(ht, hv, hu);
    if (t.direction == 2) return GhostVec3(hu, hv, ht);
    return GhostVec3(hu, ht, hv);
}

// Object-space box around the sculpted terrain (used for picking, shadows, focus)
inline void GhostTileMap_Box(const GhostTileMap& t, GhostVec3& mn, GhostVec3& mx)
{
    const float hu = t.gridX * t.tileSize * 0.5f, hv = t.gridY * t.tileSize * 0.5f;
    const float lo = (t.hMin < -0.05f ? t.hMin : -0.05f) - 0.02f, hi = (t.hMax > 0.05f ? t.hMax : 0.05f) + 0.02f;
    if (t.direction == 0) { mn = GhostVec3(lo, -hv, -hu);  mx = GhostVec3(hi, hv, hu); }
    else if (t.direction == 2) { mn = GhostVec3(-hu, -hv, -hi); mx = GhostVec3(hu, hv, -lo); }   // this grid faces -Z
    else { mn = GhostVec3(-hu, lo, -hv);  mx = GhostVec3(hu, hi, hv); }
}

struct GhostObject
{
    int       id = 0;
    int       meshId = 0;
    std::string name;
    GhostVec3 position = { 0, 0, 0 };
    GhostVec3 rotationDeg = { 0, 0, 0 };
    GhostVec3 scale = { 1, 1, 1 };
    GhostMaterial faces[GHOST_MAX_FACES];        // one material per face (cube 6, triangle 1, ship 6)
    int         role = 0;                     // GhostRole
    std::string customTag;                    // used when role == GHOST_ROLE_CUSTOM (max 31 chars)
    bool        collider = false;
    GhostVec3   colliderSize = { 1, 1, 1 };   // world size = colliderSize * scale
    GhostVec3   colliderOffset = { 0, 0, 0 }; // local-space center of the collider (multiplied by scale)
    GhostVec3   colliderRotation = { 0, 0, 0 }; // degrees, around the collider center (Ry * Rx * Rz, like objects)
    bool        visible = true;               // false = hidden while playing
    GhostSprite sprite;                       // only used when meshId == GHOST_MESH_QUAD
    GhostAnimator animator;                   // only used by models imported with "Animated character"
    int  shadowMode = 1;                      // 0 = off, 1 = casts from every light, 2 = casts only from shadowLightId
    int  shadowLightId = 0;
    bool receiveShadows = true;
    GhostTileMap tileMap;                     // enabled = this object is a Tile Map (meshId is GHOST_MESH_PLANE, the real mesh is generated)
    // A model that couldn't be loaded keeps its reference here, so saving the scene doesn't lose it
    std::string missingMesh, missingMeshFile, missingFaces;
    bool      useVertexColor = false;
    bool        locked = false;               // can't be moved in the viewport
    std::string link;   // portals: ... (max 63 chars)
    GhostVec3   pathOffset = { 0, 0, 0 };     // movers and projectiles: where B is, relative to A (the placed position)
    float       moveSpeed = 2.0f, movePause = 0.0f;
    float       timeOffset = 0.0f;            // Timed Elevator (pathMode 3): seconds added to the shared clock
    int         pathMode = 0;                 // 0 ping-pong, 1 restart at A, 2 once, 3 timed elevator
    int         enemyPathMode = 0;            // enemies: 0 ping-pong, 1 ping-pong + turn at ledges, 2 once, 3 stand still
    int flying = 0;   // 0 = normal, 1 = no gravity
    int enemyDeathEffect = 0;   // 0 vanish, 1 bounce off screen, 2 flatten
    int         breakEffect = 0;              // breakables: 0 vanish, 1 play "Break" then vanish, 2 play "Break" and leave rubble
    int         contents = 0;                 // blocks, breakables: 0 role default, 1 nothing, 2 coin, 3 power-up
    int         portalTrigger = 1;            // 0 touch, 1 press Down
    int powerType = 0;   // blocks: 0 auto, 1 mushroom, 2 fire flower, 3 star, 4 1-up
    int itemMoves = 0;   // 0 by type, 1 moves, 2 stays put
    int coinCount = 1;   // blocks with contents = coin: coins paid before it is empty
    GhostPlayerParams playerP = GhostPlayerParams_Make();   // used when role == Player
    GhostEnemyParams  enemyP = GhostEnemyParams_Make();    // used when role == Enemy
    GhostBossParams   bossP = GhostBossParams_Make();       // used when role == Boss
    GhostPathParams   pathP = GhostPathParams_Make();       // used by Pathway markers (role == Custom)
};

// Local bounds of an object: a Tile Map's grid, otherwise its mesh
inline GhostVec3 GhostObject_Min(const GhostObject& o)
{
    if (!o.tileMap.enabled) return GhostMesh_Min(o.meshId);
    GhostVec3 mn, mx;
    GhostTileMap_Box(o.tileMap, mn, mx);
    return mn;
}
inline GhostVec3 GhostObject_Max(const GhostObject& o)
{
    if (!o.tileMap.enabled) return GhostMesh_Max(o.meshId);
    GhostVec3 mn, mx;
    GhostTileMap_Box(o.tileMap, mn, mx);
    return mx;
}

inline void GhostObject_FitTileCollider(GhostObject& o)
{
    GhostVec3 mn, mx;
    GhostTileMap_Box(o.tileMap, mn, mx);
    auto d = [](float v) { v = v < 0.0f ? -v : v; return v < 0.01f ? 0.01f : v; };
    o.colliderSize = { d(mx.x - mn.x), d(mx.y - mn.y), d(mx.z - mn.z) };
    o.colliderOffset = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
}
// Material slots of an object: a Tile Map's slots are its material layers (can be 0)
inline int GhostObject_FaceCount(const GhostObject& o)
{
    if (o.tileMap.enabled) return o.tileMap.layerCount;
    return GhostMesh_FaceCount(o.meshId);
}
// Slots the main pass draws: a Tile Map with no layers still draws once (default look)
inline int GhostObject_DrawFaces(const GhostObject& o)
{
    if (o.tileMap.enabled) return o.tileMap.layerCount > 0 ? o.tileMap.layerCount : 1;
    return GhostMesh_FaceCount(o.meshId);
}

// The tag the game module searches by: the role's name, or the custom text
inline std::string GhostObject_Tag(const GhostObject& o)
{
    static const char* names[GHOST_ROLE_COUNT] = { "", "Player", "Enemy", "Coin", "Block", "Goal", "",
       "PortalEntry", "PortalExit", "Breakable", "Mover", "Projectile", "Boss", "HiddenBlock" };
    if (o.role == GHOST_ROLE_CUSTOM) return o.customTag;
    if (o.role >= 1 && o.role < GHOST_ROLE_COUNT) return names[o.role];
    return "";
}

// Draw calls the main pass needs for this object: one per run of faces that share a material.
// A Tile Map draws one pass per material layer.
inline int GhostObject_DrawRuns(const GhostObject& o)
{
    if (o.tileMap.enabled) return o.tileMap.layerCount > 0 ? o.tileMap.layerCount : 1;
    const int n = GhostMesh_FaceCount(o.meshId);
    if (n <= 0) return 0;
    int runs = 1;
    for (int f = 1; f < n; f++) if (!GhostMaterial_Equal(o.faces[f], o.faces[f - 1])) runs++;
    return runs;
}



struct GhostLight
{
    int            id = 0;
    GhostLightType type = GhostLightType::Point;
    std::string    name;
    GhostVec3      position = { 0, 1, 0 };
    float          yawDeg = 0.0f, pitchDeg = 0.0f;   // directional + rect facing
    float          color[3] = { 1, 1, 1 };
    float          intensity = 1.0f;
    float          range = 10.0f;                    // point + rect
    float          width = 1.0f, height = 1.0f;      // rect only
    bool           castShadows = true;
    float          innerDeg = 20.0f, outerDeg = 35.0f;   // spot cone
    bool           shadowObjects = true;                 // spot: objects cast shadows from this light
    bool           shadowLandscape = true;               // spot: Tile Maps cast shadows from this light
    float          spinYaw = 0.0f, spinPitch = 0.0f;     // deg/sec, applied while playing
};
struct GhostCamera
{
    int         id = 0;
    std::string name;
    GhostVec3   position = { 0.0f, 0.75f, -2.0f };   // the old fly-cam start
    float       yawDeg = 0.0f, pitchDeg = 0.0f;
    float       fovDeg = 45.0f;
    int         resW = 0, resH = 0;                  // 0 = Match Window
    int         mode = 0;                         // 0 Free, 1 Orbit
    int         targetId = 0;                     // orbit target object (0 = none)
    std::string targetTag;                        // used when targetId is 0 (e.g. "Player")
    float       distance = 7.0f, heightOffset = 0.6f;
    float       minDist = 2.0f, maxDist = 20.0f;
    float       minPitchDeg = -70.0f, maxPitchDeg = 40.0f;
    float       smoothing = 10.0f;                // 0 = snap
    GhostVec3   orbitPos = { 0, 0, 0 };           // runtime only, not saved
    bool        orbitValid = false;               // runtime only
    float moveSpeed = 3.5f;   // editor fly speed (units/s)
};

// ---- Mods: data-driven entity types (the first one is the Sky Sphere) ----
enum GhostParamKind { GHOST_PARAM_FLOAT = 0, GHOST_PARAM_BOOL, GHOST_PARAM_ENUM, GHOST_PARAM_COLOR, GHOST_PARAM_TEXTURE };
constexpr unsigned int GHOST_MAX_MODS = 8;

struct GhostParamDesc
{
    const char* name;            // also the key in scene files, so keep it stable
    const char* category;
    int         kind;            // GhostParamKind
    float       def[3];          // default (colors use all three)
    float       mn, mx;          // range for floats (ignored when mx <= mn)
    const char* enums;           // "A|B|C" for GHOST_PARAM_ENUM
};

struct GhostModEntity
{
    int         id = 0;
    int         typeId = 0;
    std::string name;
    GhostVec3   position = { 0, 3, 0 };       // only used for the viewport icon
    std::vector<float> values;                // all parameters, packed (colors take 3 floats)
};

// ---- Volumes (Utilities palette) ----
constexpr unsigned int GHOST_MAX_VOLUMES = 16;
constexpr int GHOST_VOL_PARAMS = 16;     // per-type float parameters (meaning defined by each phase)
constexpr int GHOST_VOL_TEXTURES = 4;    // per-type texture ids (cubemap, noise ...)
enum GhostVolumeType { GHOST_VOL_REFLECTION = 0, GHOST_VOL_FOG, GHOST_VOL_COUNT };
enum GhostVolumeShape { GHOST_VOLSHAPE_BOX = 0, GHOST_VOLSHAPE_SPHERE };

struct GhostVolume
{
    int         id = 0;
    int         type = 0;                       // GhostVolumeType
    std::string name;
    GhostVec3   position = { 0, 2, 0 };
    GhostVec3   rotationDeg = { 0, 0, 0 };      // Ry * Rx * Rz, like objects
    GhostVec3   size = { 10, 5, 10 };           // full extents, used when shape == Box
    int         shape = 0;                      // GhostVolumeShape
    float       radius = 5.0f;                  // used when shape == Sphere
    float       blend = 1.0f;                   // soft falloff distance inside the edge
    int         priority = 0;
    bool        enabled = true;
    float       p[GHOST_VOL_PARAMS] = {};
    int         tex[GHOST_VOL_TEXTURES] = {};
};

inline void GhostVolume_Clamp(GhostVolume& v)
{
    auto cf = [](float x, float lo, float hi) { return x != x ? lo : (x < lo ? lo : (x > hi ? hi : x)); };
    v.type = v.type < 0 ? 0 : (v.type >= GHOST_VOL_COUNT ? GHOST_VOL_COUNT - 1 : v.type);
    v.shape = v.shape ? 1 : 0;
    v.position = { cf(v.position.x, -100000.f, 100000.f), cf(v.position.y, -100000.f, 100000.f), cf(v.position.z, -100000.f, 100000.f) };
    v.rotationDeg = { cf(v.rotationDeg.x, -720.f, 720.f), cf(v.rotationDeg.y, -720.f, 720.f), cf(v.rotationDeg.z, -720.f, 720.f) };
    v.size = { cf(v.size.x, 0.1f, 10000.f), cf(v.size.y, 0.1f, 10000.f), cf(v.size.z, 0.1f, 10000.f) };
    v.radius = cf(v.radius, 0.1f, 10000.f);
    v.blend = cf(v.blend, 0.0f, 1000.f);
    v.priority = v.priority < -100 ? -100 : (v.priority > 100 ? 100 : v.priority);
    for (int i = 0; i < GHOST_VOL_PARAMS; i++) if (v.p[i] != v.p[i]) v.p[i] = 0.0f;
}

// ---- Post processing (scene settings) ----
enum GhostToneMode { GHOST_TONE_NONE = 0, GHOST_TONE_REINHARD, GHOST_TONE_ACES, GHOST_TONE_NEUTRAL, GHOST_TONE_COUNT };

struct GhostPostSettings
{
    int   toneMode = GHOST_TONE_NONE;      // None = the old clamp, so existing scenes look the same until you pick a curve
    float exposureEV = 0.0f;               // stops, -5 .. +5
    float whitePoint = 4.0f;               // Reinhard: the brightness that maps to white
};

inline void GhostPostSettings_Clamp(GhostPostSettings& p)
{
    auto cf = [](float x, float lo, float hi) { return x != x ? lo : (x < lo ? lo : (x > hi ? hi : x)); };
    p.toneMode = p.toneMode < 0 ? 0 : (p.toneMode >= GHOST_TONE_COUNT ? GHOST_TONE_COUNT - 1 : p.toneMode);
    p.exposureEV = cf(p.exposureEV, -5.0f, 5.0f);
    p.whitePoint = cf(p.whitePoint, 1.0f, 16.0f);
}

struct GhostScene
{
    std::vector<GhostObject> objects;
    std::vector<GhostLight>  lights;
    std::vector<GhostCamera> cameras;
    std::vector<GhostModEntity> mods;
    std::vector<GhostVolume> volumes;
    std::vector<GhostParticleDef> particles;       // particle systems (the live particles are in GhostParticleMan)
    int activeCameraId = 0;              // the camera the main view renders through
    int gameStartCameraId = 0;           // 0 = Play keeps the current main view camera
    std::vector<GhostTextureInfo> textures;     // id 0 = White, id 1 = Checkerboard, then imports
    float        ambientColor[3] = { 1, 1, 1 };
    float        ambientIntensity = 0.15f;
    GhostPostSettings post;
    int          nextId = 1;          // ONE id space for objects and lights
    int          selectedId = 0;      // 0 = nothing selected
    int          selectedFace = -1;   // -1 = all faces; always belongs to selectedId
    bool         shadowsEnabled = true;
    float        shadowStrength = 1.0f;          // 1 = a shadowed spot gets only ambient light
    int          shadowRes = 1024;               // shadow map size per side: 512, 1024, 2048 or 4096
    int          shadowFilter = 2;        // 0 sharp (bilinear), 1 PCF 3x3, 2 PCF smooth (bilinear 3x3), 3 soft (16 rotated taps)
    float        shadowSoftness = 1.0f;   // filter radius in shadow-map texels (0.25 .. 4)
    float        shadowBias = 1.0f;       // depth bias multiplier (0 .. 8), 1 = the old fixed bias
    float        shadowNormalBias = 1.0f; // normal-offset multiplier (0 .. 8), 1 = the old fixed offset
    float        shadowDistance = 30.0f;  // directional light: how far from the camera shadows reach (0 = whole scene)
    std::string  music, ambience;       // names from Content/Audio/Sounds.ghostaudio, "" = none
    unsigned int version = 0;         // bumps on any structural change
};
inline int GhostShadowRes_Clamp(int v) { return v <= 512 ? 512 : (v <= 1024 ? 1024 : (v <= 2048 ? 2048 : 4096)); }
// NOTE: returned pointers are only valid until the next Add/Remove. Use immediately. old comment

inline GhostObject* GhostScene_AddObject(GhostScene& s, int meshId)
{
    const GhostMeshInfo* mi = GhostMesh_Info(meshId);
    if (!mi) return nullptr;

    GhostObject o;
    o.id = s.nextId++;
    o.meshId = meshId;
    o.scale = mi->defaultScale;
    o.name = mi->name + " " + std::to_string(o.id);
    if (mi->builtin)
        o.collider = (meshId == GHOST_MESH_CUBE || meshId == GHOST_MESH_SQUARE || meshId == GHOST_MESH_QUAD);
    else                                                 // imported model: fitted collider and the model's own materials
    {
        o.collider = true;
        GhostMesh_FitCollider(*mi, o.colliderSize, o.colliderOffset);
        const int n = GhostMesh_FaceCount(meshId);
        for (int f = 0; f < n; f++) if (mi->faces[(size_t)f].hasMat) o.faces[f] = mi->faces[(size_t)f].mat;
    }
    s.objects.push_back(o);
    s.version++;
    return &s.objects.back();
}

inline GhostLight* GhostScene_AddLight(GhostScene& s, GhostLightType type)
{
    if (s.lights.size() >= GHOST_MAX_LIGHTS) return nullptr;
    static const char* names[] = { "Directional Light", "Point Light", "Rect Light", "Spot Light" };

    GhostLight l;
    l.id = s.nextId++;
    l.type = type;
    l.name = std::string(names[(int)type]) + " " + std::to_string(l.id);
    if (type == GhostLightType::Directional) l.pitchDeg = -45.0f;
    if (type == GhostLightType::RectArea)    l.pitchDeg = -90.0f;
    if (type == GhostLightType::Spot) { l.pitchDeg = -90.0f; l.intensity = 3.0f; }
    s.lights.push_back(l);
    s.version++;
    return &s.lights.back();
}
// The first camera added becomes the active one. Pointer valid until the next Add/Remove.
inline GhostCamera* GhostScene_AddCamera(GhostScene& s)
{
    if (s.cameras.size() >= GHOST_MAX_CAMERAS) return nullptr;
    GhostCamera c;
    c.id = s.nextId++;
    c.name = std::string(s.cameras.empty() ? "Default Camera" : "Camera") + " " + std::to_string(c.id);
    s.cameras.push_back(c);
    if (s.activeCameraId == 0) s.activeCameraId = c.id;
    s.version++;
    return &s.cameras.back();
}

inline GhostVolume* GhostScene_AddVolume(GhostScene& s, int type)
{
    if (type < 0 || type >= GHOST_VOL_COUNT || s.volumes.size() >= GHOST_MAX_VOLUMES) return nullptr;
    static const char* names[GHOST_VOL_COUNT] = { "Reflection Capture", "Fog Volume" };
    GhostVolume v;
    v.id = s.nextId++;
    v.type = type;
    v.name = std::string(names[type]) + " " + std::to_string(v.id);
    if (type == GHOST_VOL_REFLECTION) { v.shape = GHOST_VOLSHAPE_SPHERE; v.radius = 5.0f; }
    else { v.shape = GHOST_VOLSHAPE_BOX; v.size = { 10, 5, 10 }; }
    s.volumes.push_back(v);
    s.version++;
    return &s.volumes.back();
}

inline bool GhostScene_Remove(GhostScene& s, int id)
{
    for (size_t i = 0; i < s.objects.size(); i++)
        if (s.objects[i].id == id) { s.objects.erase(s.objects.begin() + i); goto removed; }
    for (size_t i = 0; i < s.lights.size(); i++)
        if (s.lights[i].id == id) { s.lights.erase(s.lights.begin() + i);  goto removed; }
    for (size_t i = 0; i < s.cameras.size(); i++)
        if (s.cameras[i].id == id)
        {
            if (s.cameras.size() <= 1) return false;          // the last camera can never be deleted
            bool wasActive = (s.activeCameraId == id);
            s.cameras.erase(s.cameras.begin() + i);
            if (wasActive) s.activeCameraId = s.cameras.front().id;
            if (s.gameStartCameraId == id) s.gameStartCameraId = 0;
            goto removed;
        }
    for (size_t i = 0; i < s.mods.size(); i++)
        if (s.mods[i].id == id) { s.mods.erase(s.mods.begin() + i); goto removed; }
    for (size_t i = 0; i < s.volumes.size(); i++)
        if (s.volumes[i].id == id) { s.volumes.erase(s.volumes.begin() + i); goto removed; }
    return false;
removed:

    for (GhostObject& o : s.objects)             // an object set to a deleted light goes back to "All Lights"
        if (o.shadowMode == 2 && o.shadowLightId == id) { o.shadowMode = 1; o.shadowLightId = 0; }
    for (GhostObject& o : s.objects)             // a Tile Map assigned to a deleted light goes back to "All Lights"
        if (o.tileMap.shadowLightId == id) o.tileMap.shadowLightId = 0;
    if (s.selectedId == id) s.selectedId = 0;
    s.version++;
    return true;
}

// Y rotation (degrees) that turns a quad's front (-Z) toward the eye
inline float GhostBillboardYawDeg(const GhostVec3& pos, const GhostVec3& eye)
{
    float dx = eye.x - pos.x, dz = eye.z - pos.z;
    if (dx * dx + dz * dz < 1e-8f) return 0.0f;
    return atan2f(-dx, -dz) * (180.0f / GHOST_PI);
}

inline GhostMat4 GhostObjectModel(const GhostObject& o, const float* yawDegOverride = nullptr)
{
    const float d2r = GHOST_PI / 180.0f;
    const float yaw = yawDegOverride ? *yawDegOverride : (o.rotationDeg.y + o.animator.yawOffset);
    const GhostVec3 sm = o.animator.scaleMul;
    return GhostMat4::Translation(o.position.x, o.position.y, o.position.z)
        * GhostMat4::RotationY(yaw * d2r)
        * GhostMat4::RotationX(o.rotationDeg.x * d2r)
        * GhostMat4::RotationZ((o.rotationDeg.z + o.animator.leanDeg) * d2r)
        * GhostMat4::Scale(o.scale.x * sm.x, o.scale.y * sm.y, o.scale.z * sm.z);
}

inline GhostMat4 GhostVolumeModel(const GhostVolume& v)       // no scale: size / radius are real units
{
    const float d2r = GHOST_PI / 180.0f;
    return GhostMat4::Translation(v.position.x, v.position.y, v.position.z)
        * GhostMat4::RotationY(v.rotationDeg.y * d2r)
        * GhostMat4::RotationX(v.rotationDeg.x * d2r)
        * GhostMat4::RotationZ(v.rotationDeg.z * d2r);
}