// GhostSkin.h - skeletons, CPU skinning and 3D animation clips (put in include/)
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// One vertex's skin data, in the model file's own space (before the engine's axis conversion and scale)
struct GhostSkinVert
{
    float          p[3];     // rest position
    float          n[3];     // rest normal
    unsigned short j[4];     // bone indices
    float          w[4];     // weights (sum to 1)
};

struct GhostSkinData
{
    // Node tree. A parent always comes before its children.
    std::vector<std::string> nodeName;
    std::vector<int>         nodeParent;            // -1 = root
    std::vector<float>       nodeT, nodeR, nodeS;   // rest pose per node: 3 floats, 4 floats (x y z w), 3 floats

    // Bones: one per (mesh, bone) pair, plus one per rigid (unskinned) part
    std::vector<int>         boneNode;              // node the bone follows
    std::vector<float>       boneOffset;            // 16 floats per bone, row-major (assimp layout)

    std::vector<GhostSkinVert> verts;               // parallel to the mesh's vertices

    bool  zUp = false;                              // the import's up axis
    float scale = 1.0f;                             // the import's final scale (fit * scale)

    std::unordered_map<std::string, int> nodeMap;   // name -> node (built by GhostSkin_Finish)
    std::unordered_map<std::string, int> nodeShort; // name without its "prefix:" (so mixamorig: and mixamorig1: both match)

    int NodeCount() const { return (int)nodeName.size(); }
    int BoneCount() const { return (int)boneNode.size(); }
};

// One animated node of a clip. Key times are in seconds.
struct GhostSkelTrack
{
    std::string        node;
    std::vector<float> pt, rt, st;   // key times: position, rotation, scale
    std::vector<float> pv, rv, sv;   // values: 3 floats, 4 floats (x y z w), 3 floats per key
};

struct GhostSkelClip
{
    std::string name;
    float       duration = 0.0f;     // seconds
    std::vector<GhostSkelTrack> tracks;
};

// Reads the (longest) animation of an FBX. Works for Mixamo "Without Skin" files.
bool GhostSkelClip_Load(const std::string& utf8Path, GhostSkelClip& out, std::string& err);

// Builds nodeMap. Call after filling a GhostSkinData.
void GhostSkin_Finish(GhostSkinData& s);
int  GhostSkin_CountMatches(const GhostSkinData& s, const GhostSkelClip& clip);   // how many of a clip's tracks fit this skeleton
// Fills boneMats (16 floats per bone) for a clip at `time` seconds. clip = nullptr gives the rest pose.
void GhostSkin_Pose(const GhostSkinData& s, const GhostSkelClip* clip, float time, std::vector<float>& boneMats);

// Skins every vertex. outVerts = vertices of 12 floats (GhostMeshVertex layout);
// writes position (0..2) and normal (7..9) in engine space, including the import scale.
void GhostSkin_Apply(const GhostSkinData& s, const std::vector<float>& boneMats, float* outVerts);

// .ghostskin file, stored next to the .ghostmesh
bool GhostSkinFile_Save(const std::string& utf8Path, const GhostSkinData& s, std::string& err);
bool GhostSkinFile_Load(const std::string& utf8Path, GhostSkinData& s, std::string& err);