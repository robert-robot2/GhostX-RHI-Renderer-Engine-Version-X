#pragma once
#include <string>
#include <vector>

constexpr int          GHOST_MODEL_MAX_SLOTS = 32;          // must equal GHOST_MAX_FACES
constexpr unsigned int GHOST_MODEL_MAX_TRIANGLES = 1000000;

// Layout-identical to GhostVertex (12 floats), so the importer needs no D3D headers
struct GhostMeshVertex { float x, y, z, r, g, b, a, nx, ny, nz, u, v; };

struct GhostModelOptions
{
    float scale = 1.0f;          // multiplier, applied after the fit
    float fitSize = 0.0f;        // > 0: scale so the largest dimension equals this many units
    int   upAxis = 0;            // 0 = auto (from the file), 1 = Y up, 2 = Z up
    bool  flipWinding = false;   // extra flip, for files whose faces come in inside-out
};

struct GhostModelSub { std::string name; unsigned int firstIndex = 0, indexCount = 0; };

struct GhostModelResult
{
    std::string                  name;
    std::vector<GhostMeshVertex> vertices;
    std::vector<unsigned int>    indices;
    std::vector<GhostModelSub>   subs;          // one per material, contiguous index ranges
    float        bmin[3] = { 0, 0, 0 }, bmax[3] = { 0, 0, 0 };
    unsigned int triangleCount = 0;
    std::string  info, warn;
};

// Result is in the engine's space: left-handed, Y up, +Z forward, clockwise front faces, V down.
bool GhostModel_Load(const std::string& utf8Path, const GhostModelOptions& opt,
    GhostModelResult& out, std::string& err);