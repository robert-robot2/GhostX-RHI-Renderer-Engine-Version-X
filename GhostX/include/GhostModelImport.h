#pragma once
#include <string>
#include <vector>

constexpr int          GHOST_MODEL_MAX_SLOTS = 32;          // must equal GHOST_MAX_FACES
constexpr unsigned int GHOST_MODEL_MAX_TRIANGLES = 1000000;
constexpr unsigned int GHOST_MODEL_WARN_TRIANGLES = 250000;

// Layout-identical to GhostVertex (12 floats), so the importer needs no D3D headers
struct GhostMeshVertex { float x, y, z, r, g, b, a, nx, ny, nz, u, v; };

struct GhostModelOptions
{
    float scale = 1.0f;            // multiplier, applied after the fit
    float fitSize = 0.0f;          // > 0: scale so the largest dimension equals this many units
    int   upAxis = 0;              // 0 = auto (from the file), 1 = Y up, 2 = Z up
    bool  flipWinding = false;     // extra flip, for files whose faces come in inside-out
    bool  importMaterials = true;  // false: every slot gets the default grey
};

// One material slot's defaults (display colours, like the Material panel)
struct GhostModelMat
{
    bool  hasMat = false;          // false = engine defaults
    float tint[3] = { 0.6f, 0.6f, 0.6f };
    float metallic = 0.0f, roughness = 0.6f, specular = 0.5f;
    float emissive[3] = { 0, 0, 0 };
    float emissiveStrength = 1.0f;
    std::string texPath;           // saved: project-relative with '/', or absolute

    // import time only, never saved
    std::string texSource;                 // an image file the importer found (absolute)
    std::vector<unsigned char> texData;    // or an embedded image
    std::string texExt;
};

struct GhostModelSub { std::string name; unsigned int firstIndex = 0, indexCount = 0; GhostModelMat mat; };

struct GhostModelResult
{
    std::string                  name;
    std::vector<GhostMeshVertex> vertices;
    std::vector<unsigned int>    indices;
    std::vector<GhostModelSub>   subs;          // one per material, contiguous index ranges
    float        bmin[3] = { 0, 0, 0 }, bmax[3] = { 0, 0, 0 };
    unsigned int triangleCount = 0;
    GhostModelOptions opts;
    std::string  info, warn;
};

// Result is in the engine's space: left-handed, Y up, +Z forward, clockwise front faces, V down.
bool GhostModel_Load(const std::string& utf8Path, const GhostModelOptions& opt,
    GhostModelResult& out, std::string& err);

// .ghostmesh (version 2): the converted model, with per-slot materials and the import options
bool GhostMeshFile_Save(const std::string& utf8Path, const GhostModelResult& r, const std::string& source, std::string& err);
bool GhostMeshFile_Load(const std::string& utf8Path, GhostModelResult& out, std::string& source, std::string& err);