// GhostScene.h — backend-agnostic scene model
#pragma once
#include "GhostMath.h"
#include <vector>
#include <string>
#include <string.h>

enum GhostMeshId { GHOST_MESH_CUBE = 0, GHOST_MESH_TRIANGLE, GHOST_MESH_SHIP, GHOST_MESH_SQUARE, GHOST_MESH_QUAD, GHOST_MESH_COUNT };
enum GhostRole {
    GHOST_ROLE_STATIC = 0, GHOST_ROLE_PLAYER, GHOST_ROLE_ENEMY, GHOST_ROLE_COIN,
    GHOST_ROLE_BLOCK, GHOST_ROLE_GOAL, GHOST_ROLE_CUSTOM, GHOST_ROLE_COUNT
};
enum class GhostLightType : int { Directional = 0, Point = 1, RectArea = 2 };
constexpr unsigned int GHOST_MAX_LIGHTS = 16;
constexpr unsigned int GHOST_MAX_CAMERAS = 4;
constexpr int GHOST_MAX_FACES = 6;
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

// A face is a contiguous range of the mesh's index buffer
struct GhostFaceDef { const char* name; unsigned int firstIndex, indexCount; };

inline int GhostMesh_FaceCount(int meshId)
{
      switch (meshId) { case GHOST_MESH_TRIANGLE: case GHOST_MESH_QUAD: return 1; default: return 6; }
}

inline const GhostFaceDef& GhostMesh_Face(int meshId, int face)
{
    static const GhostFaceDef cube[6] = {
        { "Front", 0, 6 }, { "Back", 6, 6 }, { "Top", 12, 6 },
        { "Bottom", 18, 6 }, { "Left", 24, 6 }, { "Right", 30, 6 } };
    static const GhostFaceDef tri[1] = { { "Face", 0, 3 } };
    static const GhostFaceDef quad[1] = { { "Face", 0, 6 } };
    static const GhostFaceDef ship[6] = {      // one triangle each; names are approximate
        { "Hull Right", 0, 3 }, { "Hull Left", 3, 3 }, { "Hull Rear", 6, 3 },
        { "Hull Bottom", 9, 3 }, { "Wing Right", 12, 3 }, { "Wing Left", 15, 3 } };

    if (face < 0 || face >= GhostMesh_FaceCount(meshId)) face = 0;
    switch (meshId) {
    case GHOST_MESH_TRIANGLE: return tri[face]; case GHOST_MESH_QUAD: return quad[face];
    case GHOST_MESH_SHIP: return ship[face]; default: return cube[face];
    }
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
    bool        visible = true;               // false = hidden while playing
    GhostSprite sprite;                       // only used when meshId == GHOST_MESH_QUAD
    bool      useVertexColor = false;
};
// The tag the game module searches by: the role's name, or the custom text
inline std::string GhostObject_Tag(const GhostObject& o)
{
    static const char* names[] = { "", "Player", "Enemy", "Coin", "Block", "Goal" };
    if (o.role == GHOST_ROLE_CUSTOM) return o.customTag;
    if (o.role >= 1 && o.role <= 5) return names[o.role];
    return "";
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
    unsigned int version = 0;         // bumps on any structural change
};

// NOTE: returned pointers are only valid until the next Add/Remove. Use immediately.
inline GhostObject* GhostScene_AddObject(GhostScene& s, int meshId)
{
    if (meshId < 0 || meshId >= GHOST_MESH_COUNT) return nullptr;
    static const char* names[GHOST_MESH_COUNT] = { "Cube", "Triangle", "Ship", "Square", "Sprite" };
    static const GhostVec3 defScale[GHOST_MESH_COUNT] =
    { { 1, 1, 1 },{ 1, 1, 1 },{ 0.1f, 0.1f, 0.1f },{ 1, 1, 0.25f },{ 1, 1, 1 } };

    GhostObject o;
    o.id = s.nextId++;
    o.meshId = meshId;
    o.scale = defScale[meshId];
    if (meshId == GHOST_MESH_SQUARE || meshId == GHOST_MESH_CUBE || meshId == GHOST_MESH_QUAD) o.collider = true;
    o.name = std::string(names[meshId]) + " " + std::to_string(o.id);
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