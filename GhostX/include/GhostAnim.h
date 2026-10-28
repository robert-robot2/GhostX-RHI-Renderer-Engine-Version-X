#pragma once
#include "GhostSceneIO.h"        // GhostLoadTexFn
#include <string>
#include <vector>
#include <utility>

// GhostAnim.h

struct GhostClip
{
    std::string      name;
    std::vector<int> frames;     // frame indices, row-major from the top-left of the sheet
    float            fps = 8.0f;
    bool             loop = true;

    // Per-clip texture (replaces the old asset-level fields)
    std::string      texture;    // stored path of this clip's sheet
    int              textureId = 0;
    int              source = 1;   // 0 = single image, 1 = sprite sheet
    int              cols = 1;
    int              rows = 1;
};

struct GhostAnimAsset
{
    std::string name, path;      // path = absolute, normalized
    std::vector<GhostClip> clips;
    std::vector<std::pair<std::string, std::string>> states;  // state name -> clip name
};

bool GhostAnim_Load(GhostAnimAsset& a, const std::string& projectRoot, const std::string& absFile,
    const GhostLoadTexFn& loadTex, std::string& err, std::string& warn);

int  GhostAnim_FindClip(const GhostAnimAsset& a, const char* name);  // case-insensitive, -1 if missing
int  GhostAnim_FrameAt(const GhostClip& c, float timeSeconds);