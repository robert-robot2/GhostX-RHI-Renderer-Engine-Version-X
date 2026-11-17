#pragma once
#include "GhostSceneIO.h"        // GhostLoadTexFn
#include <string>
#include <vector>
#include <utility>

// GhostAnim.h

struct GhostClip
{
    std::string      name;
    std::vector<int> frames;
    float            fps = 8.0f;
    bool             loop = true;
    std::string      texture;
    int              textureId = 0;
    int              source = 1;
    int              cols = 1;
    int              rows = 1;
    bool             skeletal = false;
    std::string      model;
    float            speed = 1.0f;          // NEW
};

// ---- NEW: blueprint graph types (must be above GhostAnimAsset) ----
enum GhostGraphNodeType
{
    GNODE_ENTRY = 0, GNODE_STATE, GNODE_ANY, GNODE_BLEND, GNODE_TURN, GNODE_LEAN,
    GNODE_SQUASH, GNODE_SPEED, GNODE_SOUND, GNODE_RUMBLE, GNODE_COUNT
};
struct GhostGraphNode
{
    int id = 0, type = GNODE_STATE;
    float x = 0, y = 0;
    std::string name, clip;
    float p[4] = {};
    int curve = 3;
};
struct GhostGraphLink { int from = 0, to = 0; };
struct GhostAnimGraph { std::vector<GhostGraphNode> nodes; std::vector<GhostGraphLink> links; };
struct GhostGraphPath { int target = -1; std::vector<int> fx; };

struct GhostAnimAsset
{
    std::string name, path;
    std::vector<GhostClip> clips;
    std::vector<std::pair<std::string, std::string>> states;
    GhostAnimGraph graph;                    // NEW
};

bool GhostAnim_Load(GhostAnimAsset& a, const std::string& projectRoot, const std::string& absFile,
    const GhostLoadTexFn& loadTex, std::string& err, std::string& warn);

int  GhostAnim_FindClip(const GhostAnimAsset& a, const char* name);
int  GhostAnim_FrameAt(const GhostClip& c, float timeSeconds);

bool GhostAnimGraph_Resolve(const GhostAnimGraph& g, const std::string& fromState,
    const std::string& toState, GhostGraphPath& out);
int  GhostAnimGraph_EntryState(const GhostAnimGraph& g);