// GhostScene.h — backend-agnostic scene model
#pragma once
#include "GhostMath.h"
#include <vector>
#include <string>
#include <string.h>

enum GhostMeshId { GHOST_MESH_CUBE = 0, GHOST_MESH_TRIANGLE, GHOST_MESH_SHIP, GHOST_MESH_SQUARE, GHOST_MESH_QUAD, GHOST_MESH_COUNT };
enum GhostRole {
    GHOST_ROLE_STATIC = 0, GHOST_ROLE_PLAYER, GHOST_ROLE_ENEMY, GHOST_ROLE_COIN,
    GHOST_ROLE_BLOCK, GHOST_ROLE_GOAL, GHOST_ROLE_CUSTOM,
    GHOST_ROLE_PORTAL_ENTRY, GHOST_ROLE_PORTAL_EXIT, GHOST_ROLE_BREAKABLE,
    GHOST_ROLE_MOVER, GHOST_ROLE_PROJECTILE, GHOST_ROLE_COUNT
};
enum class GhostLightType : int { Directional = 0, Point = 1, RectArea = 2 };
constexpr unsigned int GHOST_MAX_LIGHTS = 16;
constexpr unsigned int GHOST_MAX_CAMERAS = 4;
// face cap???
constexpr int GHOST_MAX_FACES = 32;
constexpr int GHOST_FIRST_USER_TEXTURE = 3;     // ids 0..2 are the built-ins

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
    bool        visible = true;               // false = hidden while playing
    GhostSprite sprite;                       // only used when meshId == GHOST_MESH_QUAD
    int  shadowMode = 1;                      // 0 = off, 1 = casts from every light, 2 = casts only from shadowLightId
    int  shadowLightId = 0;
    bool receiveShadows = true;
    // A model that couldn't be loaded keeps its reference here, so saving the scene doesn't lose it
    std::string missingMesh, missingMeshFile, missingFaces;
    bool      useVertexColor = false;
    bool        locked = false;               // can't be moved in the viewport
    std::string link;                         // portals: an Entry and an Exit with the same link are a pair (max 31 chars)
    GhostVec3   pathOffset = { 0, 0, 0 };     // movers and projectiles: where B is, relative to A (the placed position)
    float       moveSpeed = 2.0f, movePause = 0.0f;
    int         pathMode = 0;                 // 0 ping-pong, 1 restart at A, 2 once
    int         breakEffect = 0;              // breakables: 0 vanish, 1 play "Break" then vanish, 2 play "Break" and leave rubble
    int         contents = 0;                 // blocks, breakables: 0 role default, 1 nothing, 2 coin, 3 power-up
    int         portalTrigger = 1;            // 0 touch, 1 press Down
};
// The tag the game module searches by: the role's name, or the custom text
inline std::string GhostObject_Tag(const GhostObject& o)
{
    static const char* names[GHOST_ROLE_COUNT] = { "", "Player", "Enemy", "Coin", "Block", "Goal", "",
        "PortalEntry", "PortalExit", "Breakable", "Mover", "Projectile" };
    if (o.role == GHOST_ROLE_CUSTOM) return o.customTag;
    if (o.role >= 1 && o.role < GHOST_ROLE_COUNT) return names[o.role];
    return "";
}

// Draw calls the main pass needs for this object: one per run of faces that share a material
inline int GhostObject_DrawRuns(const GhostObject& o)
{
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
struct GhostScene
{
    std::vector<GhostObject> objects;
    std::vector<GhostLight>  lights;
    std::vector<GhostCamera> cameras;
    std::vector<GhostModEntity> mods;
    int activeCameraId = 0;              // the camera the main view renders through
    int gameStartCameraId = 0;           // 0 = Play keeps the current main view camera
    std::vector<GhostTextureInfo> textures;     // id 0 = White, id 1 = Checkerboard, then imports
    float        ambientColor[3] = { 1, 1, 1 };
    float        ambientIntensity = 0.15f;
    int          nextId = 1;          // ONE id space for objects and lights
    int          selectedId = 0;      // 0 = nothing selected
    int          selectedFace = -1;   // -1 = all faces; always belongs to selectedId
    bool         shadowsEnabled = true;
    float        shadowStrength = 1.0f;          // 1 = a shadowed spot gets only ambient light
    unsigned int version = 0;         // bumps on any structural change
};

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
    static const char* names[] = { "Directional Light", "Point Light", "Rect Light" };

    GhostLight l;
    l.id = s.nextId++;
    l.type = type;
    l.name = std::string(names[(int)type]) + " " + std::to_string(l.id);
    if (type == GhostLightType::Directional) l.pitchDeg = -45.0f;
    if (type == GhostLightType::RectArea)    l.pitchDeg = -90.0f;   // faces down
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
    return false;
removed:
    for (GhostObject& o : s.objects)             // an object set to a deleted light goes back to "All Lights"
        if (o.shadowMode == 2 && o.shadowLightId == id) { o.shadowMode = 1; o.shadowLightId = 0; }
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
    const float yaw = yawDegOverride ? *yawDegOverride : o.rotationDeg.y;
    return GhostMat4::Translation(o.position.x, o.position.y, o.position.z)
        * GhostMat4::RotationY(yaw * d2r)
        * GhostMat4::RotationX(o.rotationDeg.x * d2r)
        * GhostMat4::RotationZ(o.rotationDeg.z * d2r)
        * GhostMat4::Scale(o.scale.x, o.scale.y, o.scale.z);
}