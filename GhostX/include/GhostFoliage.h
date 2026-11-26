// GhostFoliage.h - foliage layers, instance chunks and painting (CPU side, no D3D12 in here).
// GPU side = GhostPipeline.cpp (instanced draws) + the foliage glue block in GhostEngine.cpp.
// Put this file in /include next to GhostScene.h.
#pragma once
#include "GhostMath.h"
#include "GhostScene.h"
#include "GhostWorldPart.h"      // GhostFrustum
#include <vector>
#include <string>
#include <unordered_map>
#include <stdint.h>

constexpr int    GHOST_FOLIAGE_MAX_LAYERS = 16;
constexpr float  GHOST_FOLIAGE_CHUNK_SIZE = 32.0f;       // world units per chunk side (XZ). One chunk = one instance buffer = one cull unit
constexpr size_t GHOST_FOLIAGE_MAX_PER_CHUNK = 60000;
constexpr size_t GHOST_FOLIAGE_MAX_TOTAL = 1500000;      // all layers together

enum GhostFoliageKind { GHOST_FOLIAGE_MESH = 0, GHOST_FOLIAGE_SPRITE = 1 };

// 32 bytes, uploaded as-is into the second vertex stream (see INSTPOS / INSTROT in GhostVS.hlsl)
struct GhostFoliageInstance
{
    float px, py, pz, scale;          // world position, uniform scale
    float yaw, tiltX, tiltZ, seed;    // yaw in radians, tilt = horizontal offset of the up axis (0 = straight up), seed 0..1
};
static_assert(sizeof(GhostFoliageInstance) == 32, "GhostFoliageInstance must be 32 bytes (matches the HLSL instance layout)");

struct GhostFoliageChunk
{
    int cx = 0, cz = 0;
    std::vector<GhostFoliageInstance> inst;
    float bmin[3] = { 0, 0, 0 }, bmax[3] = { 0, 0, 0 };    // bounds of the instance positions (refresh with GhostFoliage_Touch)
    unsigned int stamp = 0;                                // new number on every edit: the GPU copy compares it
};

// Every scalar setting of a layer. Used by the scene save / load code so a new field is added in ONE place.
#define GHOST_FOLIAGE_FIELDS(X) \
    X(enabled) X(kind) X(billboard) X(alphaClip) X(unlit) X(pixelArt) \
    X(cols) X(rows) X(animated) X(animFps) \
    X(sizeMin) X(sizeMax) X(yawRange) X(tiltRandom) X(alignToNormal) X(slopeMax) \
    X(restOnGround) X(heightOffset) \
    X(scatterRadius) X(amount) X(minSpacing) \
    X(cullDistance) X(fadeLength) X(colorVariation) X(normalUp) \
    X(castShadows) X(shadowLightId) X(receiveShadows) X(shadowDistance)

struct GhostFoliageLayer
{
    int         id = 0;
    std::string name;
    bool        enabled = true;
    int         kind = GHOST_FOLIAGE_MESH;
    int         meshId = GHOST_MESH_QUAD;       // sprite layers draw GHOST_MESH_QUAD

    // ---- sprite (animated sprite sheet) ----
    bool  billboard = true;                     // camera facing (Y axis only). Works for mesh layers too (cross cards)
    bool  alphaClip = true;                     // sprite: transparent pixels are discarded
    bool  unlit = false;
    bool  pixelArt = false;                     // point filter
    int   cols = 1, rows = 1;                   // sprite sheet grid
    bool  animated = false;                     // play the sheet (each instance starts on its own frame)
    float animFps = 8.0f;

    // ---- paint parameters ----
    float sizeMin = 0.8f, sizeMax = 1.2f;       // uniform scale range
    float yawRange = 360.0f;                    // random rotation around Y, degrees (360 = any angle)
    float tiltRandom = 4.0f;                    // random lean, degrees
    float alignToNormal = 0.0f;                 // 0..1 how much the instance leans with the surface
    float slopeMax = 45.0f;                     // no instances on slopes steeper than this (degrees)
    bool  restOnGround = true;                  // lift by the mesh's lowest point, like dropping an object
    float heightOffset = 0.0f;                  // extra lift (negative sinks the roots)
    float scatterRadius = 4.0f;                 // brush radius
    int   amount = 10;                          // instances added per brush stamp
    float minSpacing = 0.35f;                   // no two instances of this layer closer than this

    // ---- rendering ----
    float cullDistance = 80.0f;                 // 0 = never culled
    float fadeLength = 20.0f;                   // the last part of the cull distance thins out instead of popping
    float colorVariation = 0.15f;               // random tint brightness per instance (0..1)
    float normalUp = 0.0f;                      // 0..1 blend the lighting normal toward straight up (soft grass lighting)

    // ---- shadows ----
    int   castShadows = 0;                      // 0 off, 1 every light, 2 only shadowLightId
    int   shadowLightId = 0;
    bool  receiveShadows = true;
    float shadowDistance = 40.0f;               // only chunks this close to the camera are drawn into the shadow maps

    // ---- materials (PBR). Sprite layers use faces[0]. Mesh layers use one entry per material slot of the mesh ----
    GhostMaterial faces[GHOST_MAX_FACES];

    std::unordered_map<int64_t, GhostFoliageChunk> chunks;
};

struct GhostFoliageBrush
{
    bool active = false;            // foliage paint mode (O)
    bool erase = false;             // erase instead of paint (Shift while painting does the same)
    bool onLandscape = true;        // Paint on Landscape (Tile Maps)
    bool onObjects = true;          // Paint on Objects (any mesh object)
    bool eraseAll = false;          // erase hits every layer, not only the active one
};

struct GhostFoliageSet
{
    std::vector<GhostFoliageLayer> layers;
    int  nextId = 1;
    int  activeLayerId = 0;
    GhostFoliageBrush brush;
    unsigned int version = 0;       // changes on every edit: dirty detection ("*" in the title)
    uint32_t     rng = 0x9E3779B9u;
};

struct GhostFoliageHit { GhostVec3 p, n; };

// What the engine provides to the painter (set once at Init)
struct GhostFoliageHost
{
    int  (*TextureCount)() = nullptr;
    // A vertical probe: the highest surface between yTop and yBottom at (x, z). Landscape and / or objects.
    bool (*Probe)(float x, float z, float yTop, float yBottom, bool landscape, bool objects, GhostFoliageHit* out) = nullptr;
};
void GhostFoliage_SetHost(const GhostFoliageHost* h);

inline int64_t GhostFoliage_Key(int cx, int cz) { return ((int64_t)(uint32_t)cx << 32) | (int64_t)(uint32_t)cz; }

GhostFoliageSet& GhostFoliage_Set();
GhostFoliageLayer* GhostFoliage_Find(int id);                       // pointer valid until the next Add / Remove
int    GhostFoliage_AddLayer(int kind, int source, const std::string& name);   // source = mesh id (mesh) or texture id (sprite). New id, 0 = failed
bool   GhostFoliage_RemoveLayer(int id);
void   GhostFoliage_Clear();                                        // scene clear / load
void   GhostFoliage_Sanitize(GhostFoliageLayer& L);
void   GhostFoliage_ResetFaces(GhostFoliageLayer& L);               // materials back to the mesh defaults
int    GhostFoliage_FaceCount(const GhostFoliageLayer& L);
size_t GhostFoliage_InstanceCount(const GhostFoliageLayer& L);
size_t GhostFoliage_TotalInstances();
float  GhostFoliage_Radius(const GhostFoliageLayer& L);             // how far an instance reaches from its position (culling)
void   GhostFoliage_MeshBase(const GhostFoliageLayer& L, float& minY, float& height);   // local height range (wind weight)
GhostFoliageChunk& GhostFoliage_ChunkAt(GhostFoliageLayer& L, int cx, int cz);          // creates the chunk if needed
void   GhostFoliage_Touch(GhostFoliageChunk& C);                    // refresh bounds + stamp after editing C.inst

// One brush stamp. Returns how many instances were added.
int GhostFoliage_Paint(GhostFoliageLayer& L, const GhostVec3& center, bool landscape, bool objects);
// Removes instances inside the circle. layerId -1 = every layer. strength 0..1 = chance that an instance in the circle goes.
int GhostFoliage_Erase(int layerId, const GhostVec3& center, float radius, float strength);