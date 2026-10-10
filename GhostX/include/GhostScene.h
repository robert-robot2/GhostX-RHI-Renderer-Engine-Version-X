// GhostScene.h — backend-agnostic scene model
#pragma once
#include "GhostMath.h"
#include <vector>
#include <string>

enum GhostMeshId { GHOST_MESH_CUBE = 0, GHOST_MESH_TRIANGLE, GHOST_MESH_SHIP, GHOST_MESH_COUNT };
enum class GhostLightType : int { Directional = 0, Point = 1, RectArea = 2 };
constexpr unsigned int GHOST_MAX_LIGHTS = 16;

struct GhostObject
{
    int       id = 0;
    int       meshId = 0;
    std::string name;
    GhostVec3 position = { 0, 0, 0 };
    GhostVec3 rotationDeg = { 0, 0, 0 };
    GhostVec3 scale = { 1, 1, 1 };
    float     color[3] = { 0.6f, 0.6f, 0.6f };   // grey by default
    bool      useVertexColor = false;
};

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

struct GhostScene
{
    std::vector<GhostObject> objects;
    std::vector<GhostLight>  lights;
    float        ambientColor[3] = { 1, 1, 1 };
    float        ambientIntensity = 0.15f;
    int          nextId = 1;          // ONE id space for objects and lights
    int          selectedId = 0;      // 0 = nothing selected
    unsigned int version = 0;         // bumps on any structural change
};

// NOTE: returned pointers are only valid until the next Add/Remove. Use immediately.
inline GhostObject* GhostScene_AddObject(GhostScene& s, int meshId)
{
    if (meshId < 0 || meshId >= GHOST_MESH_COUNT) return nullptr;
    static const char* names[GHOST_MESH_COUNT] = { "Cube", "Triangle", "Ship" };
    static const float defScale[GHOST_MESH_COUNT] = { 1.0f, 1.0f, 0.1f };  // ship mesh is ~10 units long

    GhostObject o;
    o.id = s.nextId++;
    o.meshId = meshId;
    o.scale = { defScale[meshId], defScale[meshId], defScale[meshId] };
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

inline bool GhostScene_Remove(GhostScene& s, int id)
{
    for (size_t i = 0; i < s.objects.size(); i++)
        if (s.objects[i].id == id) { s.objects.erase(s.objects.begin() + i); goto removed; }
    for (size_t i = 0; i < s.lights.size(); i++)
        if (s.lights[i].id == id) { s.lights.erase(s.lights.begin() + i);  goto removed; }
    return false;
removed:
    if (s.selectedId == id) s.selectedId = 0;
    s.version++;
    return true;
}

inline GhostMat4 GhostObjectModel(const GhostObject& o)
{
    const float d2r = GHOST_PI / 180.0f;
    return GhostMat4::Translation(o.position.x, o.position.y, o.position.z)
        * GhostMat4::RotationY(o.rotationDeg.y * d2r)
        * GhostMat4::RotationX(o.rotationDeg.x * d2r)
        * GhostMat4::RotationZ(o.rotationDeg.z * d2r)
        * GhostMat4::Scale(o.scale.x, o.scale.y, o.scale.z);
}