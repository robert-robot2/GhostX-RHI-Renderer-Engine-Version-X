// GhostEngine.cpp  — updated for IGhostRHI
#include "../include/GhostEngine.h"
#include "../include/IGhostRHI.h"
#include "../include/Ghost3D1.h"
#include "../include/GhostMath.h"
#include "../include/GhostPipeline.h"
#include "../include/GhostScene.h"
#include "../include/GhostSceneIO.h"
#include "../include/GhostImage.h"
#include "../include/GhostRuntime.h"
#include "../include/GhostGameModule.h"
#include "../include/GhostUI.h"
#include "../include/GhostMods.h"
#include "../include/GhostAnim.h"
#include "../include/GhostModelImport.h"
#include "../include/GhostSkin.h"
#include "../include/GhostOverlay.h"
#include "../include/GhostMSAA.h"
#include "../include/GhostTAA.h"
#include "../include/GhostPad.h"
#include "../include/GhostAudio.h"
#include "../include/GhostAudioBank.h"
#include "../include/GhostVideoSettings.h"
#include "../include/GhostParticleMan.h"
#include "../include/GhostPost.h"
#include "../include/GhostProbe.h"
#include "../include/GhostGpuProf.h"
#include "../include/GhostLightShafts.h"
#include "../include/GhostWorldPart.h"
#include "../include/GhostFoliage.h"
#include "../include/GhostSMAA.h"
#include "../include/GhostFXAA.h"
#include <memory>
#include <string.h>
#include <filesystem>
#include <map>
#include <set> 
#include <fstream>
#include <algorithm>
#include <vector>
#include <utility>
#include <cctype>
#include <functional>

// ---- Engine state ----
static IGhostRHI* g_rhi = nullptr;
static GhostPipeline* g_pipeline = nullptr;
static GhostMSAA  g_msaa;
static int        g_msaaWanted = 1;
static GhostTAA   g_taa;
static int        g_taaOn = 0;
static float      g_taaSpeed = 1.0f;
static GhostSMAA  g_smaa;
static int        g_smaaOn = 0;
static int        g_smaaQuality = 2;
static GhostFXAA  g_fxaa;
static int        g_fxaaOn = 0;
static int        g_fxaaQuality = 1;
static GhostPost  g_post;
static GhostProbeSys g_probe;
static GhostGpuProf g_prof;
static GhostShafts g_shafts;
static unsigned int g_frameNo = 0;                          // advances once per Tick (animates the fog dithering under TAA)
static GhostWorldCfg   g_world;
static GhostWorldStats g_wstats;
struct ProbeRt { int slot = -1; unsigned int sig = 0; float stable = 0.0f, timer = 0.0f; bool dirty = true, request = false, seen = false; };
static std::map<int, ProbeRt> g_probeRt;                   // key = volume id

// ---- Foliage (see the glue block at the end of this file) ----
struct FoliageGpuChunk { GhostInstanceBuffer buf; unsigned int stamp = 0xFFFFFFFFu, count = 0; bool seen = false; };
struct FoliShadowCaster { const GhostFoliageLayer* layer; const FoliageGpuChunk* gpu; int meshId; unsigned int slot, tex, first, count; };
static std::map<std::pair<int, long long>, FoliageGpuChunk> g_foliGpu;            // (layer id, chunk key) -> instance buffer
static bool  g_foliPainting = false, g_foliHavePrev = false, g_foliHaveBackup = false;
static float g_foliTimer = 0.0f;
static GhostVec3 g_foliPrev;
static std::vector<GhostFoliageLayer> g_foliBackup;                               // the editor's foliage while a game runs
static int   g_foliBackupNextId = 1, g_foliBackupActive = 0;
static unsigned int g_foliBackupVersion = 0;
static int   g_foliDrawnChunks = 0, g_foliDrawnInst = 0, g_foliShadowChunks = 0;
static void FoliageTick(float dt);
static void FoliageSync();
static void FoliageReleaseAll();
static void FoliageInstallHost();
static bool FoliPaintBegin(int x, int y);
static void OverlayFoliageBrush(int rw, int rh);
static void FoliageDraw(const GhostMat4& view, const GhostMat4& proj, const GhostVec3& eye, const GhostFrustum& fr, bool cull, bool preview, unsigned int& slot);
static void FoliageBuildShadowCasters(const GhostVec3& eye, const GhostVec3& forward, unsigned int& slot, std::vector<FoliShadowCaster>& out, GhostVec3& bmin, GhostVec3& bmax);
static bool FoliShadowEligible(const GhostFoliageLayer& L, const GhostLight& l);

// ---- Camera state ----
static GhostVec3 g_eye = { 0.0f, 0.75f, -2.0f };
static float     g_yaw = 0.0f;
static float     g_pitch = 0.0f;
static bool      g_looking = false;
static POINT     g_lastMouse = {};

// ---- Timing ----
static LARGE_INTEGER g_freq = {};
static LARGE_INTEGER g_prev = {};

// ---- Scene angles ----
static GhostScene   g_scene;
static unsigned int g_width = 1280;
static unsigned int g_height = 720;
// ---- Interaction state ----
static bool       g_inputEnabled = false;
static bool       g_haveCam = false;
static GhostMat4  g_view = GhostMat4::Identity();
static GhostMat4  g_proj = GhostMat4::Identity();
static GhostVec3  g_camR = { 1, 0, 0 }, g_camU = { 0, 1, 0 }, g_camF = { 0, 0, 1 };
static int        g_hoverId = 0;
static bool       g_faceMode = false;     // F: a click picks a face, no Shift needed
static std::vector<int> g_multi;                 // extra selected ids. The primary is g_scene.selectedId
static bool g_colMode = false;                   // H: the gizmo edits the collider
static bool g_marquee = false, g_marqueeAdd = false;
static int  g_mqX0 = 0, g_mqY0 = 0, g_mqX1 = 0, g_mqY1 = 0, g_collapseId = 0, g_mqClickId = 0;
static bool g_dragMoved = false;
static bool IsSelected(int id) { return id != 0 && (id == g_scene.selectedId || std::find(g_multi.begin(), g_multi.end(), id) != g_multi.end()); }
static std::vector<int> SelectedIds() { std::vector<int> v; if (g_scene.selectedId) v.push_back(g_scene.selectedId); for (int i : g_multi) v.push_back(i); return v; }
static bool       g_dragging = false;
static int        g_dragId = 0;
static GhostVec3  g_dragOffset;
static GhostVec3  g_dragPlanePoint;

// ---- Paint mode (P): sculpt and paint the selected Tile Map (the code is further down, next to the Tile Map mesh code) ----
struct PaintBrush { int tool = 0; float radius = 2.0f, strength = 0.5f, fade = 0.6f, setHeight = 0.0f; };
static bool        g_paintMode = false, g_painting = false, g_paintMouseIn = false, g_strokeHasPrev = false;
static PaintBrush  g_brush;
static int         g_paintMX = 0, g_paintMY = 0;
static float       g_flattenH = 0.0f, g_prevU = 0.0f, g_prevV = 0.0f;
static bool PaintBegin(int x, int y);
static void OverlayPaintBrush(int rw, int rh);

static GhostObject* FindObject(int id);
static int          CurrentFace();
static void UpdateSprites(float dt);
static void UpdateAnimators(float dt);
static void SpriteDrawInfo(const GhostObject& o, int& tex, int& cols, int& rows, int& frame);
static int PickObjectAt(int sx, int sy);
struct EngineViewport
{
    bool alive = false; bool visible = true; int cameraId = 0;
    bool  preview = false;                  // animation preview window: solo object + its own orbit camera
    int   solo = 0;
    float pYaw = 20.0f, pPitch = -8.0f, pZoom = 1.0f;
};
static EngineViewport g_vp[GHOST_MAX_SURFACES];
static int            g_inputCameraId = 0;               // the camera WASD / right-mouse look drives
static GhostVec3      g_mainEye = { 0, 0, 0 };           // main view position, for picking rays
// ---- Play mode ----
static GhostPlayState  g_playState = GhostPlayState::Editing;
static GhostScene      g_playBackup;
static GhostStepper    g_stepper;
static GhostInput      g_input;
static double          g_playTime = 0.0;
static std::string     g_hudText, g_gameModulePath, g_playMessage, g_playFault;
static GhostGameAPI    g_gameApi;
static float g_mouseAccumX = 0.0f, g_mouseAccumY = 0.0f, g_wheelAccum = 0.0f;   // collected between steps
static float g_apiMouseDX = 0.0f, g_apiMouseDY = 0.0f, g_apiWheel = 0.0f;       // what the game reads
static std::vector<GhostUIQuad> g_uiQuads;
static std::vector<GhostUIAsset> g_uiAssets;                    // handle = index + 1
static float g_uiMouseX = -1.0f, g_uiMouseY = -1.0f;            // cursor in reference units
static bool  g_uiMouseInside = false;
static GhostFont                g_font;
static std::vector<GhostFont> g_fonts;                       // slot n lives at g_fonts[n-1]; slot 0 = g_font
static GhostFontProviderFn g_fontProvider = nullptr;
static std::vector<std::pair<std::string, int>> g_fontCache; // absolute path -> slot
static std::string StoredToAbs(const std::string& stored);   // defined further down
static float                    g_uiRefW = 1920.0f;       // reference width of the main view (height is 1080)
static std::string g_projectRoot;
static GhostLoadProgressFn g_loadProgress = nullptr;
static void LoadNote(const char* kind, const std::string& name)
{
    if (g_loadProgress) g_loadProgress(kind, name.c_str());
}


static float g_skyScroll[2] = { 0.0f, 0.0f };      // day / night layer scroll in degrees (play only)
static float g_cloudScroll[2] = { 0.0f, 0.0f };    // cloud U / V offset (play only)

static std::vector<GhostAnimAsset> g_anims;                    // id = index, never removed

// ---- Animated models (3D skeletal animation) ----
static std::map<int, std::shared_ptr<GhostSkinData>> g_skinByMesh;      // mesh id -> skeleton + skin weights
struct SkelClipEntry { std::shared_ptr<GhostSkelClip> clip; std::filesystem::file_time_type mtime; };
static std::map<std::string, SkelClipEntry> g_skelClips;               // absolute FBX path -> clip (null = failed to load)
struct SkinInst
{
    int   slot = -1, meshId = -1;                  // slot = animated vertex buffer in the pipeline
    std::vector<GhostMeshVertex> verts;            // uv / colour from the static mesh, position + normal rewritten each frame
    int   lastAnim = -2, lastClip = -2;            // what the buffer currently shows
    float lastTime = -1.0f;
    bool  seen = false;
};
static std::map<int, SkinInst> g_skinInst;       // key = object id
static bool g_skinSlotUsed[GHOST_MAX_SKINS] = {};
static std::vector<float> g_boneScratch;

static void FreeSkinInstance(int objectId)
{
    auto it = g_skinInst.find(objectId);
    if (it == g_skinInst.end()) return;
    if (it->second.slot >= 0)
    {
        GhostReleaseSkinMesh(g_pipeline, it->second.slot);
        g_skinSlotUsed[it->second.slot] = false;
    }
    g_skinInst.erase(it);
}

static void ReleaseSkinInstancesOfMesh(int meshId)
{
    std::vector<int> ids;
    for (auto& kv : g_skinInst) if (kv.second.meshId == meshId) ids.push_back(kv.first);
    for (int id : ids) FreeSkinInstance(id);
}


// ---- Tile Maps: one generated mesh slot per Tile Map object, plus its weight map textures ----
struct TileInst
{
    int   slot = -1;                                      // -1 = not resident (no GPU mesh)
    float tileSize = 0.0f;
    int   gx = 0, gy = 0, dir = -1, sub = 0;
    unsigned int hStamp = 0, sStamp = 0;                  // the data stamps the GPU copies were built from
    int   splatA = 0, splatB = 0;                         // texture ids of the weight maps (0 = none)
    int   splatW = 0, splatH = 0;
    bool  seen = false;
    // ---- world partition ----
    unsigned int neighSig = 0;                            // signature of the neighbouring cells the border normals were built from
    bool skirts = false;
    float dist = 1e30f;                                   // distance to the nearest streaming focus (last tick)
    int   state = 0;                                      // 0 not wanted, 1 resident, 2 wanted but waiting for a slot
    unsigned int visFrame = 0xFFFFFFFFu;                  // last frame it was drawn
    int   lodCur = 0;
    // ---- mesh layout, filled by BuildTileMeshLod ----
    int   nx = 0, ny = 0, bandRows = 16, bandCount = 0, maxLod = 0;
    float cell = 0.0f, u0 = 0.0f, v0 = 0.0f;
    unsigned int lodFirst[3] = {}, skirtFirst[3] = {}, skirtCount[3] = {};
    std::vector<float> bandMin, bandMax;                  // height range of each row band (local N axis)
    // layers that have paint on them (bit 0 = layer 0, always drawn)
    unsigned int usedMask = 0xFFFFFFFFu, maskStamp = 0xFFFFFFFFu;
    int   maskLayers = -1;
};
static std::map<int, TileInst> g_tileInst;               // key = object id
static bool g_tileSlotUsed[GHOST_MAX_TILEMAPS] = {};
static std::vector<int> g_splatFree;
static bool g_tileBurst = true;                           // first update after a scene load: load everything in range at once
static const int kTileBandRows = 16;                      // terrain rows per cull band
static const int kMaxTileMapObjects = 256;
static void TileAxes(int dir, GhostVec3& U, GhostVec3& V, GhostVec3& N);      // defined with the Tile Map code below                   // weight map texture ids that can be reused



// 0 = every light shadows the surface, n = only light n-1 of the scene does (the Tile Map's "Directional Light")
static int ShadowLightSelector(const GhostObject& o)
{
    if (!o.tileMap.enabled || o.tileMap.shadowLightId == 0) return 0;
    for (size_t i = 0; i < g_scene.lights.size() && i < GHOST_MAX_LIGHTS; i++)      // same order as BuildSceneCB
        if (g_scene.lights[i].id == o.tileMap.shadowLightId) return (int)i + 1;
    return 0;
}

// Index range of a material slot of a normal mesh
struct FaceSpan { unsigned int first, count; };
static FaceSpan FaceSpanOf(const GhostObject& o, int f)
{
    const GhostFaceDef& d = GhostMesh_Face(o.meshId, f);
    return { d.firstIndex, d.indexCount };
}

// The mesh slot to draw an object with: its animated vertex buffer when it has one
static int DrawMeshOf(const GhostObject& o)
{
    if (o.tileMap.enabled)
    {
        auto t = g_tileInst.find(o.id);
        return (t != g_tileInst.end() && t->second.slot >= 0) ? (int)GHOST_TILE_MESH_BASE + t->second.slot : -1;   // -1 draws nothing (not built yet)
    }
    auto it = g_skinInst.find(o.id);
    if (it == g_skinInst.end() || it->second.slot < 0 || it->second.meshId != o.meshId) return o.meshId;
    return GHOST_MAX_MESHES + it->second.slot;
}


static std::string g_pendingScene;       // set by LoadScene(): the switch happens after the current step
static std::string g_scenePath;          // the scene file that is open (or being played)
static std::string g_playBackupPath;
static bool        g_quitRequested = false;

// Main view render size and where it sits inside the window (for mapping the mouse)
static int   g_mainRW = 1280, g_mainRH = 720;
static float g_fitX = 0.0f, g_fitY = 0.0f, g_fitW = 1280.0f, g_fitH = 720.0f;
static GhostVideoSettings g_video = { 0, 0, GHOST_DISPLAY_WINDOWED, GHOST_FILTER_LINEAR, GHOST_FIT_LETTERBOX };
static unsigned int g_lastRW[GHOST_MAX_SURFACES] = {}, g_lastRH[GHOST_MAX_SURFACES] = {};   // for TAA resets

// Forward Declarations? are interaction state
static int LoadModelFile(const std::string& absFile, std::string& err);
static std::string g_modelWarn;                     // warnings from model files loaded during a scene load
static void PerformSceneSwitch();
static void InstallParticleHost();
// ---- Audio helpers ----
static float g_preBusVol[GHOST_BUS_COUNT] = {};
static bool  g_preBusMute[GHOST_BUS_COUNT] = {};
static bool  g_haveBusSnap = false;

// ---- Animation preview (editor only, never saved with the scene) ----
struct AnimPreviewState
{
    bool active = false, loop = true, playing = true;
    int  objectId = 0, matched = 0, total = 0;
    std::string path;
    float speed = 1.0f, time = 0.0f, duration = 0.0f;
};
static AnimPreviewState g_prevstate;

// ---- Blueprint runtime (per animated object, never saved) ----
struct AnimFx { float t, dur, amt; };                 // squash / stretch pulse
struct AnimRt
{
    bool  active = false, entryDone = false;          // active = the blueprint chose the current clip
    std::string state, clip;
    bool  blending = false;                           // crossfade from the previous clip
    std::string fromPath; float fromTime = 0, fromSpeed = 1; bool fromLoop = true;
    float bt = 0, bd = 0; int curve = 3;
    float spFrom = 1, spTo = 1, spT = 0, spD = 0;     // speed ramp
    std::vector<AnimFx> fx;
    float visYaw = 0, lean = 0; bool haveYaw = false;
};
static std::map<int, AnimRt> g_animRt;
static std::vector<GhostMeshVertex> g_blendScratch;
static int ModelTransition(GhostObject& o, const std::string& state, const std::string& clip);



// Plays the scene's Music / Ambience from Scene Settings (an empty or disabled entry stops that channel)
static void PlaySceneAudio(float fade)
{
    const std::string* names[2] = { &g_scene.music, &g_scene.ambience };
    for (int k = 0; k < 2; k++)
    {
        const int i = names[k]->empty() ? -1 : GhostAudioBank_Find(names[k]->c_str());
        if (i >= 0 && GhostAudioBank_PlayIndex(i, fade, false)) continue;
        if (k == 0) GhostAudio_StopMusic(fade); else GhostAudio_StopAmbience(fade);
    }
}

// ---- Gizmo drag state (13.B) ----
struct GizmoState
{
    bool active = false, local = false, snapping = false;
    int  id = 0, axis = -1, tool = 0;                    // axis: 0..2 = X Y Z, 3 = uniform scale
    GhostVec3 startPos, startRot, startScale, dir, u, v;
    float t0 = 0.0f, d0 = 0.0f, ox = 0.0f, oy = 0.0f, lastAngle = 0.0f, accum = 0.0f, value = 0.0f;
    bool col = false;  GhostVec3 startColSize, startColOff, startColRot;
    float startYaw = 0.0f, startPitch = 0.0f;
};
static GizmoState g_gizmo;
struct GroupItem { int id; GhostVec3 pos, rot, scale; };
static std::vector<GroupItem> g_group;           // the other selected objects while the gizmo drags
static int        g_gizmoHover = -1;
static bool GizmoBegin(int x, int y);
static void GizmoUpdate(int x, int y);
static int  GizmoHit(int x, int y);


// Shared State

// ---- Dimension / edge clicks (13.C) ----
struct LabelHit { float rect[4]; float seg[4]; bool hasSeg; int kind, index, objectId; float len; };
static std::vector<LabelHit> g_labelHits;
struct DimClick { bool valid = false; int kind = 0, index = 0, objectId = 0; float len = 0.0f; };
static DimClick g_dimClick;
static int g_selEdgeObj = 0, g_selEdgeLetter = -1;
static bool LabelClick(int x, int y);
static bool SelectToolActive();

// Window pixel -> render pixel. clampOut = keep dragging when the cursor is over the bars.
static bool PanelToRender(int px, int py, int& rx, int& ry, bool clampOut)
{
    if (g_fitW < 1.0f || g_fitH < 1.0f) return false;
    float u = ((float)px - g_fitX) / g_fitW;
    float v = ((float)py - g_fitY) / g_fitH;
    if (!clampOut && (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f)) return false;
    u = u < 0.0f ? 0.0f : (u > 0.9999f ? 0.9999f : u);
    v = v < 0.0f ? 0.0f : (v > 0.9999f ? 0.9999f : v);
    rx = (int)(u * (float)g_mainRW);
    ry = (int)(v * (float)g_mainRH);
    return true;
}

// Spawn coordinates arrive in window pixels; -1, -1 = screen center
static void SpawnCoords(int& sx, int& sy)
{
    if (sx < 0 || sy < 0) { sx = sy = -1; return; }
    int rx, ry;
    if (PanelToRender(sx, sy, rx, ry, false)) { sx = rx; sy = ry; }
    else { sx = sy = -1; }
}

static const float ICON_HALF_PX = 18.0f;
static const float ICON_PICK_PX = ICON_HALF_PX + 4.0f;
// the last 2 * GHOST_MAX_LIGHTS constant-buffer slots are reserved for light icons + arrows

static const unsigned int MAX_SCENE_OBJECTS = GHOST_MAX_OBJECTS - 2 * GHOST_MAX_LIGHTS - 2 * GHOST_MAX_CAMERAS - GHOST_MAX_MODS - GHOST_MAX_VOLUMES - 16 - GHOST_PFX_MAX_DRAWS - (unsigned int)GHOST_PFX_MAX_EMITTERS - (unsigned int)GHOST_FOLIAGE_MAX_LAYERS * 8;

// Hard cap on objects (shadow casters are limited to the same number)
static const unsigned int MAX_SCENE_OBJECT_COUNT = GHOST_MAX_SHADOW_CASTERS;




// Draw slots the main pass needs: one per run of faces that share a material, plus 2 for the face highlight
static unsigned int SceneDrawSlots()
{
    unsigned int n = 2;
    for (const GhostObject& o : g_scene.objects) n += (unsigned int)GhostObject_DrawRuns(o);
    return n;
}

// A new object is counted at its worst case (a different material on every face) until it's actually edited
static bool CanAddObject(int meshId)
{
    if (!GhostMesh_Valid(meshId) || g_scene.objects.size() >= MAX_SCENE_OBJECT_COUNT) return false;
    const int worst = GhostMesh_FaceCount(meshId);
    return SceneDrawSlots() + (unsigned int)(worst > 0 ? worst : 1) <= MAX_SCENE_OBJECTS;
}



// local-space bounds of each mesh (picking only)
/*
static const GhostVec3 kMeshMin[GHOST_MESH_COUNT] = { { -0.5f, -0.5f, -0.5f },{ -0.5f, 0.0f, -0.05f },{ -3.2f, -1.0f, -3.0f },{ -0.5f, -0.5f, -0.5f },{ -0.5f, -0.5f, -0.25f } };
static const GhostVec3 kMeshMax[GHOST_MESH_COUNT] = { { 0.5f,  0.5f,  0.5f },{ 0.5f, 1.0f,  0.05f },{ 3.2f,  3.0f, 11.0f },{ 0.5f,  0.5f,  0.5f },{ 0.5f,  0.5f,  0.25f } };
*/



static GhostColor g_clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };

// may be incorrect
static int LoadSceneTexture(const std::string& absPath, const std::string& name);


// Creates the GPU texture and records it in the scene's registry (id == pipeline slot)
static std::vector<int> g_freeTex;      // texture ids released by GhostEngine_PurgeProjectAssets, reused first

static int RegisterTexture(const char* name, const unsigned char* rgba,
    unsigned int w, unsigned int h)
{
    if (!g_rhi || !g_pipeline) return -1;

    while (!g_freeTex.empty())                           // recycle a purged slot before growing the registry
    {
        const int id = g_freeTex.back();
        g_freeTex.pop_back();
        if (id < GHOST_FIRST_USER_TEXTURE || id >= (int)g_scene.textures.size()) continue;
        if (GhostCreateTexture(g_rhi, g_pipeline, rgba, w, h, id) != id) continue;
        GhostTextureInfo& t = g_scene.textures[(size_t)id];
        t.id = id; t.name = name ? name : "Texture"; t.width = w; t.height = h; t.path.clear();
        return id;
    }

    int idx = GhostCreateTexture(g_rhi, g_pipeline, rgba, w, h);
    if (idx < 0) return -1;
    GhostTextureInfo t;
    t.id = idx;
    t.name = name ? name : "Texture";
    t.width = w;
    t.height = h;
    g_scene.textures.push_back(t);
    return idx;
}

void GhostEngine_SetClearColor(float r, float g, float b)
{
    g_clearColor = { r, g, b, 1.0f };
    if (g_rhi) g_rhi->SetClearColor(r, g, b);
}

static int ParticleAllocId() { return g_scene.nextId++; }


int GhostEngine_Init(HWND hwnd, unsigned int width, unsigned int height)
{
    g_width = width; g_height = height;
    GhostParticles_Bind(&g_scene.particles); GhostParticles_SetIdAllocator(ParticleAllocId);
    g_rhi = CreateRHI(RHIType::DX12);
    if (!g_rhi) return -1;
    if (g_rhi->Init(hwnd, width, height) != GHOST_OK) return -1;

    GhostPipelineDesc pd = {};
    pd.width = width;
    pd.height = height;

    // Pipeline still takes device/cmdlist via the RHI accessors —
    // these are thin wrappers, no D3D12 types leak into Engine.
    if (GhostPipelineCreate(g_rhi, &pd, &g_pipeline) != GHOST_OK) return -1;
    if (GhostUploadMeshes(g_rhi, g_pipeline) != GHOST_OK) return -1;

    //add failure is not fatal, MSAA just stays unavailable??
    GhostMSAA_Init(g_rhi, g_msaa);
    GhostTAA_Init(g_rhi, g_taa);                  // failure is not fatal: TAA just stays unavailable
    GhostSMAA_Init(g_rhi, g_smaa);                // failure is not fatal: SMAA just stays unavailable
    GhostFXAA_Init(g_rhi, g_fxaa);                // failure is not fatal: FXAA just stays unavailable
    if (GhostPost_Init(g_rhi, g_post) != GHOST_OK) return -1;     // the HDR path is not optional
    GhostProbe_Init(g_rhi, g_probe);                          // not fatal: without it reflection captures stay unavailable
    GhostWorld_Load(g_world);                                 // view distance + streaming settings from the last session
    GhostShafts_Init(g_rhi, g_shafts);                        // not fatal: without it shafts stay off
    GhostProf_Init(g_rhi, g_prof);                            // not fatal: no overlay timings without it
    GhostAudio_Init();                            // not fatal: with no sound device every audio call does nothing
    if (GhostAudioBank_Count() == 0) GhostAudioBank_Load(g_projectRoot);
    // ---- Built-in textures: id 0 = white (untextured draws), id 1 = checkerboard (UV debugging) ----
    {
        const unsigned char white[4] = { 255, 255, 255, 255 };
        if (RegisterTexture("White", white, 1, 1) != 0) return -1;

        std::vector<unsigned char> checker(256 * 256 * 4);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++)
            {
                bool light = (((x / 32) + (y / 32)) % 2) == 0;
                unsigned char v = light ? 230 : 70;
                unsigned char r = v, g = v, b = v;
                if (x < 32 && y < 32) { r = 255; g = 140; b = 40; }   // orange top-left cell = orientation marker
                unsigned char* px = &checker[((size_t)y * 256 + x) * 4];
                px[0] = r; px[1] = g; px[2] = b; px[3] = 255;
            }
        if (RegisterTexture("Checkerboard", checker.data(), 256, 256) != 1) return -1;
        // id 2 = "Cube Cross Test": a 4x3 cell layout, one color per face, orange marker = top-left of each cell
        const int CW = 128, TW = CW * 4, TH = CW * 3;
        std::vector<unsigned char> cross((size_t)TW * TH * 4);
        struct Cell { int cx, cy; unsigned char r, g, b; };
        const Cell cells[6] = {
            { 1, 0,  60, 180,  75 },   // Top    green
            { 0, 1,  60, 100, 220 },   // Left   blue
            { 1, 1, 220,  60,  60 },   // Front  red
            { 2, 1, 230, 200,  50 },   // Right  yellow
            { 3, 1, 150,  70, 200 },   // Back   purple
            { 1, 2,  50, 200, 210 },   // Bottom cyan
        };
        for (int y = 0; y < TH; y++)
            for (int x = 0; x < TW; x++)
            {
                unsigned char r = 40, g = 40, b = 40;               // unused cells
                int cellX = x / CW, cellY = y / CW, lx = x % CW, ly = y % CW;
                for (const Cell& c : cells)
                    if (c.cx == cellX && c.cy == cellY)
                    {
                        r = c.r; g = c.g; b = c.b;
                        if (lx < 4 || ly < 4 || lx >= CW - 4 || ly >= CW - 4) { r /= 2; g /= 2; b /= 2; }
                        if (lx < 24 && ly < 24) { r = 255; g = 140; b = 40; }
                    }
                unsigned char* px = &cross[((size_t)y * TW + x) * 4];
                px[0] = r; px[1] = g; px[2] = b; px[3] = 255;
            }
        if (RegisterTexture("Cube Cross Test", cross.data(), TW, TH) != 2) return -1;
        {   // default particle texture (the "[Splat" name keeps it out of the texture dropdowns, like the Tile Map weight maps)
            std::vector<unsigned char> soft;
            GhostParticles_MakeSoftTexture(soft, 64);
            const int softId = RegisterTexture("[Splat Particle Soft]", soft.data(), 64, 64);
            if (softId > 0) GhostParticles_SetDefaultTexture(softId);
            InstallParticleHost();
        }


    }
    // ---- The default camera: always present, gets id 1 ----
    GhostScene_AddCamera(g_scene);
    FoliageInstallHost();
    g_vp[0].alive = true;
    g_vp[0].visible = true;
    /*
    
    {
        GhostObject* o = GhostScene_AddObject(g_scene, GHOST_MESH_CUBE);
        o->useVertexColor = true;                       

        o = GhostScene_AddObject(g_scene, GHOST_MESH_CUBE);
        o->position = { 1.1f, 0.0f, 0.0f };             

        o = GhostScene_AddObject(g_scene, GHOST_MESH_SHIP);
        o->position = { -1.5f, 0.0f, 2.0f };
        o->useVertexColor = true;
    }
  
    {
        const int testMode = 0;   // 0 = all, 1 = directional, 2 = point, 3 = rect, 4 = ambient only

        if (testMode == 0 || testMode == 1)
        {
            GhostLight* l = GhostScene_AddLight(g_scene, GhostLightType::Directional);
            l->yawDeg = 30.0f; l->pitchDeg = -45.0f; l->intensity = 0.6f;
        }
        if (testMode == 0 || testMode == 2)
        {
            GhostLight* l = GhostScene_AddLight(g_scene, GhostLightType::Point);
            l->position = { 1.1f, 1.0f, -1.0f };
            l->color[0] = 1.0f; l->color[1] = 0.6f; l->color[2] = 0.3f;
            l->intensity = 3.0f; l->range = 6.0f;
        }
        if (testMode == 0 || testMode == 3)
        {
            GhostLight* l = GhostScene_AddLight(g_scene, GhostLightType::RectArea);
            l->position = { 1.1f, 2.0f, 0.0f };
            l->color[0] = 0.4f; l->color[1] = 0.7f; l->color[2] = 1.0f;
            l->intensity = 4.0f; l->range = 8.0f;
            l->width = 1.5f; l->height = 1.5f;
        }
    }
    */

    // Test Particle Systems Render

   // GhostParticles_Add(GHOST_PFX_FIRE, 0.0f, 0.1f, 3.0f);
  //  GhostParticles_Add(GHOST_PFX_SNOW, 0.0f, 0.0f, 3.0f);



    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_prev);

    return 0;
}

// ---- Flicker patterns ----
static unsigned int FlHash(unsigned int n) { n = (n << 13) ^ n; return n * (n * n * 15731u + 789221u) + 1376312589u; }
static float FlRand(unsigned int n) { return (float)(FlHash(n) & 0x7fffffffu) / 2147483647.0f; }
static float FlNoise(float x, unsigned int seed)
{
    const float fl = floorf(x);
    float f = x - fl;
    f = f * f * (3.0f - 2.0f * f);
    const unsigned int i = (unsigned int)(int)fl;
    return FlRand(i * 2654435761u + seed) * (1.0f - f) + FlRand((i + 1u) * 2654435761u + seed) * f;
}
static float FlString(const char* s, float step, bool smooth)
{
    const int n = (int)strlen(s);
    if (n <= 0) return 1.0f;
    auto val = [&](int i) { const char c = s[i]; return (float)(c < 'a' ? 0 : (c > 'z' ? 25 : c - 'a')) / 25.0f; };
    const int i0 = (((int)floorf(step)) % n + n) % n;
    const float a = val(i0);
    if (!smooth) return a;
    return a + (val((i0 + 1) % n) - a) * (step - floorf(step));
}

// 0..1 pattern value mapped to [minLevel, 1]
static float GhostFlicker_Level(const GhostFlickerLayer& f, float t, int lightId)
{
    const unsigned int seed = (unsigned int)(lightId * 7919) + (unsigned int)(int)(f.seed * 131.0f);
    const float x = t * f.speed + f.seed * 0.37f + (float)lightId * 1.618f;     // offset, so identical lights don't flicker in sync
    float v = 1.0f;
    switch (f.pattern)
    {
    case 0: v = FlRand((unsigned int)(int)floorf(x) * 2654435761u + seed); break;                 // random steps
    case 1: v = FlNoise(x, seed); break;                                                           // smooth noise
    case 2: v = 0.5f + 0.5f * sinf(2.0f * GHOST_PI * x); break;                                    // sine pulse
    case 3:                                                                                        // telephone: ring-ring, short gap, long pause
        v = FlString("zazazazazaza" "aaaa" "zazazazazaza" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", x, f.smoothSteps); break;
    case 4: v = FlString("zzzzzazzzzzzzzazazzzzzzzzzzzzzzazaaazzzzzzzzzzzazzzzzz", x, f.smoothSteps); break;   // broken bulb
    case 5: v = 0.6f * FlNoise(x, seed) + 0.4f * FlNoise(x * 2.7f + 11.0f, seed + 1u); break;     // candle: two octaves
    default: v = FlString(f.custom.c_str(), x, f.smoothSteps); break;                              // custom letters
    }
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return f.minLevel + (1.0f - f.minLevel) * v;
}


static void BuildSceneCB(const GhostScene& s, GhostSceneCB& out, float timeSec)
{
    const float d2r = GHOST_PI / 180.0f;
    out = {};
    out.flickGlobal[0] = timeSec;

    float amb = s.ambientIntensity;
    for (const GhostModEntity& m : s.mods)
        if (m.typeId == GHOST_MOD_SKY) { amb = GhostSky_AmbientIntensity(m, amb); break; }
    for (int i = 0; i < 3; i++)
        out.ambient[i] = s.ambientColor[i] * amb;
    out.shadowTune[0] = (float)s.shadowFilter;
    out.shadowTune[1] = s.shadowSoftness;
    out.shadowTune[2] = s.shadowBias;
    out.shadowTune[3] = s.shadowNormalBias;
    for (const GhostLight& l : s.lights)
    {
        if (out.lightCount >= GHOST_MAX_LIGHTS) break;

        float yaw = l.yawDeg * d2r, pitch = l.pitchDeg * d2r;
        GhostVec3 fwd = { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
        GhostVec3 right = { cosf(yaw), 0.0f, -sinf(yaw) };    // yaw only, so it never degenerates when facing straight down
        GhostVec3 up = fwd.Cross(right);
        const unsigned int li = out.lightCount;
        float inten = l.intensity;
        if (l.type == GhostLightType::Point)
            for (int k = 0; k < GHOST_FLICKER_LAYERS; k++)
            {
                const GhostFlickerLayer& f = l.flicker[k];
                if (!f.enabled) continue;
                const float lv = GhostFlicker_Level(f, timeSec, l.id);
                inten *= 1.0f + (lv - 1.0f) * f.baseInfluence;                  // dims or pulses the main light
                const unsigned int e = li * (GHOST_FLICKER_LAYERS * 3) + (unsigned int)k * 3;
                out.flick[e][0] = f.color[0]; out.flick[e][1] = f.color[1]; out.flick[e][2] = f.color[2];
                out.flick[e][3] = f.brightness * lv;                             // this layer's own light
                const bool texOk = f.textureId > 0 && f.textureId < (int)s.textures.size() && f.textureId < (int)GHOST_MAX_TEXTURES;
                out.flick[e + 1][0] = texOk ? (float)f.textureId : 0.0f;
                out.flick[e + 1][1] = f.texSpeed; out.flick[e + 1][2] = f.texTiling; out.flick[e + 1][3] = f.texAmount;
                out.flick[e + 2][0] = f.diffuse; out.flick[e + 2][1] = f.specular;
                out.flick[e + 2][2] = f.roughness; out.flick[e + 2][3] = f.metallic;
            }
        GhostGPULight& g = out.lights[out.lightCount++];
        g.posRange[0] = l.position.x; g.posRange[1] = l.position.y; g.posRange[2] = l.position.z;
        g.posRange[3] = l.range;
        g.dirType[0] = fwd.x; g.dirType[1] = fwd.y; g.dirType[2] = fwd.z;
        g.dirType[3] = (float)(int)l.type;
        g.colorIntensity[0] = l.color[0]; g.colorIntensity[1] = l.color[1];
        g.colorIntensity[2] = l.color[2]; g.colorIntensity[3] = l.intensity * l.celestialFade;
        g.rectRight[0] = right.x; g.rectRight[1] = right.y; g.rectRight[2] = right.z;
        g.rectRight[3] = l.width * 0.5f;
        g.rectUp[0] = up.x; g.rectUp[1] = up.y; g.rectUp[2] = up.z;
        g.rectUp[3] = l.height * 0.5f;
        if (l.type == GhostLightType::Spot)               // spots reuse the rect fields for their cone
        {
            const float outer = fminf(fmaxf(l.outerDeg, 1.0f), 89.0f);
            const float inner = fminf(fmaxf(l.innerDeg, 0.0f), outer - 0.5f);
            g.rectRight[3] = cosf(outer * d2r);
            g.rectUp[3] = cosf(inner * d2r);
        }
    }
}

// ============================================================
//  Picking / interaction helpers
// ============================================================
static GhostVec3 XformPoint(const GhostMat4& m, const GhostVec3& p, float w)
{
    return {
        m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3] * w,
        m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3] * w,
        m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3] * w };
}
static GhostCamera* ActiveCamera()
{
    for (GhostCamera& c : g_scene.cameras)
        if (c.id == g_scene.activeCameraId) return &c;
    return nullptr;
}

static GhostVec3 AnglesToDir(float yawDeg, float pitchDeg)
{
    const float d2r = GHOST_PI / 180.0f;
    float yaw = yawDeg * d2r, pitch = pitchDeg * d2r;
    return { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
}
static GhostVec3 LightForward(const GhostLight& l)
{
    const float d2r = GHOST_PI / 180.0f;
    float yaw = l.yawDeg * d2r, pitch = l.pitchDeg * d2r;
    return { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
}

// Inverse of GhostObjectModel (T * Ry * Rx * Rz * S)
static GhostMat4 ObjectInverse(const GhostObject& o, const float* yawOverride = nullptr)
{
    const float d2r = GHOST_PI / 180.0f;
    const float ry = yawOverride ? *yawOverride : o.rotationDeg.y;
    auto inv = [](float s) { return fabsf(s) > 1e-6f ? 1.0f / s : 0.0f; };
    return GhostMat4::Scale(inv(o.scale.x), inv(o.scale.y), inv(o.scale.z))
        * GhostMat4::RotationZ(-o.rotationDeg.z * d2r)
        * GhostMat4::RotationX(-o.rotationDeg.x * d2r)
        * GhostMat4::RotationY(-ry * d2r)
        * GhostMat4::Translation(-o.position.x, -o.position.y, -o.position.z);
}

// non-null only for a billboard sprite
static const float* BillYaw(const GhostObject& o, const GhostVec3& eye, float& storage)
{
    if (o.meshId != GHOST_MESH_QUAD || !o.sprite.billboard) return nullptr;
    storage = GhostBillboardYawDeg(o.position, eye);
    return &storage;
}

static bool ScreenRay(int sx, int sy, GhostVec3& origin, GhostVec3& dir)
{
    if (!g_haveCam || g_mainRW == 0 || g_mainRH == 0) return false;
    float xn = 2.0f * (float)sx / (float)g_mainRW - 1.0f;
    float yn = 1.0f - 2.0f * (float)sy / (float)g_mainRH;
    dir = (g_camR * (xn / g_proj.m[0][0]) + g_camU * (yn / g_proj.m[1][1]) + g_camF).Normalize();
    origin = g_mainEye;                                  // was g_eye
    return true;
}

static bool WorldToScreen(const GhostVec3& p, float& sx, float& sy)
{
    GhostVec3 v = XformPoint(g_view, p, 1.0f);
    if (v.z < 0.1f) return false;
    float nx = g_proj.m[0][0] * v.x / v.z;
    float ny = g_proj.m[1][1] * v.y / v.z;
    sx = (nx * 0.5f + 0.5f) * (float)g_mainRW;
    sy = (1.0f - (ny * 0.5f + 0.5f)) * (float)g_mainRH;
    return true;
}

static bool RayAABB(const GhostVec3& o, const GhostVec3& d,
    const GhostVec3& mn, const GhostVec3& mx, float& tHit)
{
    float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    float lo[3] = { mn.x, mn.y, mn.z }, hi[3] = { mx.x, mx.y, mx.z };
    float t0 = 0.0f, t1 = 1e30f;
    for (int i = 0; i < 3; i++)
    {
        if (fabsf(dd[i]) < 1e-8f)
        {
            if (oo[i] < lo[i] || oo[i] > hi[i]) return false;
            continue;
        }
        float inv = 1.0f / dd[i];
        float a = (lo[i] - oo[i]) * inv, b = (hi[i] - oo[i]) * inv;
        if (a > b) { float t = a; a = b; b = t; }
        if (a > t0) t0 = a;
        if (b < t1) t1 = b;
        if (t0 > t1) return false;
    }
    tHit = t0;
    return true;
}

// Camera-facing plane through planePt
static bool RayPlane(const GhostVec3& o, const GhostVec3& d, const GhostVec3& planePt, GhostVec3& hit)
{
    float denom = d.Dot(g_camF);
    if (denom < 1e-4f) return false;
    float t = (planePt - o).Dot(g_camF) / denom;
    if (t < 0.0f) return false;
    hit = o + d * t;
    return true;
}

static GhostVec3* EntityPosition(int id)
{
    for (GhostObject& o : g_scene.objects) if (o.id == id) return &o.position;
    for (GhostLight& l : g_scene.lights)  if (l.id == id) return &l.position;
    for (GhostCamera& c : g_scene.cameras) if (c.id == id) return &c.position;
    for (GhostModEntity& m : g_scene.mods) if (m.id == id) return &m.position;
    for (GhostVolume& v : g_scene.volumes) if (v.id == id) return &v.position;
    if (GhostVec3* pp = GhostParticles_PositionPtr(id)) return pp;
    return nullptr;
}

static void ToggleSelect(int id)
{
    if (id == 0) return;
    if (id == g_scene.selectedId)
    {
        if (!g_multi.empty()) { g_scene.selectedId = g_multi.back(); g_multi.pop_back(); }
        else g_scene.selectedId = 0;
    }
    else
    {
        auto it = std::find(g_multi.begin(), g_multi.end(), id);
        if (it != g_multi.end()) g_multi.erase(it);
        else { if (g_scene.selectedId) g_multi.push_back(g_scene.selectedId); g_scene.selectedId = id; }
    }
    g_scene.selectedFace = -1;
}

static void PruneMulti()
{
    for (size_t i = 0; i < g_multi.size(); )
        if (g_multi[i] == g_scene.selectedId || !EntityPosition(g_multi[i])) g_multi.erase(g_multi.begin() + i); else i++;
    if (!g_scene.selectedId && !g_multi.empty()) { g_scene.selectedId = g_multi.back(); g_multi.pop_back(); }
}

// Objects whose center is inside the box being dragged (used by the preview and by the final selection)
static void MarqueeHits(std::vector<int>& hits)
{
    hits.clear();
    const float x0 = (float)(g_mqX0 < g_mqX1 ? g_mqX0 : g_mqX1), x1 = (float)(g_mqX0 < g_mqX1 ? g_mqX1 : g_mqX0);
    const float y0 = (float)(g_mqY0 < g_mqY1 ? g_mqY0 : g_mqY1), y1 = (float)(g_mqY0 < g_mqY1 ? g_mqY1 : g_mqY0);
    for (const GhostObject& o : g_scene.objects)
    {
        const GhostVec3 mn = GhostObject_Min(o), mx = GhostObject_Max(o);
        const GhostVec3 c(o.position.x + (mn.x + mx.x) * 0.5f * o.scale.x,
            o.position.y + (mn.y + mx.y) * 0.5f * o.scale.y,
            o.position.z + (mn.z + mx.z) * 0.5f * o.scale.z);
        float sx, sy;
        if (WorldToScreen(c, sx, sy) && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) hits.push_back(o.id);
    }
}

static void FinishMarquee()
{
    const bool tiny = abs(g_mqX1 - g_mqX0) < 5 && abs(g_mqY1 - g_mqY0) < 5;
    if (tiny)                                         // a plain click
    {
        if (g_mqClickId)                              // Step B: the click landed on an object
        {
            g_multi.clear(); g_scene.selectedId = g_mqClickId; g_scene.selectedFace = -1;
            g_mqClickId = 0;
            return;
        }
        if (!g_marqueeAdd) { g_scene.selectedId = 0; g_multi.clear(); }
        return;
    }
    std::vector<int> hits;
    MarqueeHits(hits);
    if (!g_marqueeAdd) { g_scene.selectedId = 0; g_multi.clear(); }
    for (int id : hits)
        if (!IsSelected(id)) { if (g_scene.selectedId) g_multi.push_back(g_scene.selectedId); g_scene.selectedId = id; }
}

// Light icons are tested in screen space (they're drawn on top), then objects by ray vs. box
static int PickAt(int sx, int sy)
{
    int   bestId = 0;
    float bestD2 = ICON_PICK_PX * ICON_PICK_PX;
    for (const GhostLight& l : g_scene.lights)
    {
        float px, py;
        if (!WorldToScreen(l.position, px, py)) continue;
        float dx = px - (float)sx, dy = py - (float)sy;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { bestD2 = d2; bestId = l.id; }
    }
    for (const GhostCamera& c : g_scene.cameras)
    {
        if (c.id == g_scene.activeCameraId) continue;       // no icon on the camera you're looking through
        float px, py;
        if (!WorldToScreen(c.position, px, py)) continue;
        float dx = px - (float)sx, dy = py - (float)sy;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { bestD2 = d2; bestId = c.id; }
    }
    for (const GhostModEntity& m : g_scene.mods)
    {
        float px, py;
        if (!WorldToScreen(m.position, px, py)) continue;
        float dx = px - (float)sx, dy = py - (float)sy;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { bestD2 = d2; bestId = m.id; }
    }
    for (const GhostVolume& v : g_scene.volumes)
    {
        float px, py;
        if (!WorldToScreen(v.position, px, py)) continue;
        float dx = px - (float)sx, dy = py - (float)sy;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { bestD2 = d2; bestId = v.id; }
    }
    for (const GhostParticleDef& pd : g_scene.particles)
    {
        float px, py;
        if (!WorldToScreen(GhostVec3(pd.p.px, pd.p.py, pd.p.pz), px, py)) continue;
        float dx = px - (float)sx, dy = py - (float)sy;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { bestD2 = d2; bestId = pd.p.id; }
    }

    if (bestId) return bestId;

    GhostVec3 o, d;
    if (!ScreenRay(sx, sy, o, d)) return 0;
    float bestT = 1e30f;
    for (const GhostObject& ob : g_scene.objects)
    {
        float byw; GhostMat4 inv = ObjectInverse(ob, BillYaw(ob, g_mainEye, byw));
        float t;
        if (RayAABB(XformPoint(inv, o, 1.0f), XformPoint(inv, d, 0.0f),
            GhostObject_Min(ob), GhostObject_Max(ob), t) && t < bestT)
        {
            bestT = t; bestId = ob.id;
        }
    }
    return bestId;
}
// Moller-Trumbore. The ray is in the object's local space; t is in the same units as the
// world ray, because the local ray is an affine transform of it.
static bool RayTriangle(const GhostVec3& o, const GhostVec3& d,
    const GhostVec3& a, const GhostVec3& b, const GhostVec3& c, float& t)
{
    GhostVec3 e1 = b - a, e2 = c - a;
    GhostVec3 p = d.Cross(e2);
    float det = e1.Dot(p);
    if (fabsf(det) < 1e-9f) return false;            // parallel or degenerate
    float inv = 1.0f / det;
    GhostVec3 s = o - a;
    float u = s.Dot(p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    GhostVec3 q = s.Cross(e1);
    float v = d.Dot(q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = e2.Dot(q) * inv;
    return t > 0.0f;
}

// Exact face under a screen position (nearest triangle, double-sided). -1 = none.
static int PickFace(const GhostObject& ob, int sx, int sy)
{
    if (ob.tileMap.enabled) return -1;
    GhostVec3 o, d;
    if (!ScreenRay(sx, sy, o, d)) return -1;

    float byw; GhostMat4 inv = ObjectInverse(ob, BillYaw(ob, g_mainEye, byw));
    GhostVec3 lo = XformPoint(inv, o, 1.0f);
    GhostVec3 ld = XformPoint(inv, d, 0.0f);

    GhostMeshCPU cpu;
    if (!GhostGetMeshCPU(g_pipeline, ob.meshId, &cpu)) return -1;

    float bestT = 1e30f;
    int   bestTri = -1;
    for (unsigned int i = 0; i + 2 < cpu.indexCount; i += 3)
    {
        const GhostVertex& a = cpu.verts[cpu.Index(i)];
        const GhostVertex& b = cpu.verts[cpu.Index(i + 1)];
        const GhostVertex& c = cpu.verts[cpu.Index(i + 2)];
        float t;
        if (RayTriangle(lo, ld, GhostVec3(a.x, a.y, a.z), GhostVec3(b.x, b.y, b.z),
            GhostVec3(c.x, c.y, c.z), t) && t < bestT)
        {
            bestT = t;
            bestTri = (int)i;
        }
    }
    if (bestTri < 0) return -1;

    const int faces = GhostMesh_FaceCount(ob.meshId);
    for (int f = 0; f < faces; f++)
    {
        const GhostFaceDef& def = GhostMesh_Face(ob.meshId, f);
        if ((unsigned int)bestTri >= def.firstIndex && (unsigned int)bestTri < def.firstIndex + def.indexCount)
            return f;
    }
    return -1;
}
// Ground-plane (y = 0) hit under a screen position, or a point 5 units ahead if the ray misses
static GhostVec3 SpawnPoint(int sx, int sy)
{
    if (sx < 0 || sy < 0) { sx = (int)g_mainRW / 2; sy = (int)g_mainRH / 2; }
    GhostVec3 o, d;
    if (!ScreenRay(sx, sy, o, d)) return { 0, 0, 0 };
    if (d.y < -1e-4f)
    {
        float t = -o.y / d.y;
        if (t > 0.0f && t < 40.0f) return o + d * t;
    }
    return o + d * 5.0f;
}

static void DrawIcon(unsigned int& slot, int iconType, const GhostVec3& pos, const GhostVec3& dir,
    const float* rgb, int state, const GhostMat4& view, const GhostMat4& proj)
{
    if (slot >= GHOST_MAX_OBJECTS) return;
    GhostObjectCB cb = {};
    cb.model.m[0][0] = pos.x; cb.model.m[0][1] = pos.y; cb.model.m[0][2] = pos.z; cb.model.m[0][3] = (float)iconType;
    cb.model.m[1][0] = (float)g_mainRW; cb.model.m[1][1] = (float)g_mainRH;
    cb.model.m[1][2] = (float)state;   cb.model.m[1][3] = ICON_HALF_PX;
    cb.model.m[2][0] = dir.x; cb.model.m[2][1] = dir.y; cb.model.m[2][2] = dir.z;
    cb.view = view;
    cb.proj = proj;
    cb.color[0] = rgb[0]; cb.color[1] = rgb[1]; cb.color[2] = rgb[2]; cb.color[3] = 1.0f;
    GhostUpdateObjectCB(g_pipeline, slot, &cb);
    GhostDrawIcon(g_rhi, g_pipeline, slot);
    slot++;
}
// ---- UI mouse: window pixels -> reference units, forwarded to every loaded UI ----
// ev: 0 move, 1 left down, 2 left up, 3 leave
static void UiMouseEvent(int ev, int px, int py)
{
    if (g_playState != GhostPlayState::Playing) return;

    float ux = -1.0f, uy = -1.0f;
    bool inside = false;
    if (ev != 3)
    {
        int rx, ry;
        inside = PanelToRender(px, py, rx, ry, false);
        if (inside)
        {
            const float s = 1080.0f / (float)g_mainRH;
            ux = (float)rx * s;
            uy = (float)ry * s;
        }
    }
    g_uiMouseX = ux; g_uiMouseY = uy; g_uiMouseInside = inside;
    for (GhostUIAsset& a : g_uiAssets) GhostUI_Mouse(a, ev, ux, uy, inside);
}
// ============================================================
//  Exported: input
// ============================================================
void GhostEngine_SetInputEnabled(int enabled) { g_inputEnabled = (enabled != 0); }

void GhostEngine_MouseDown(int px, int py, int button)
{
    if (button == 0) UiMouseEvent(1, px, py);
    if (button != 0 || g_looking || !g_haveCam || g_playState != GhostPlayState::Editing) return;
    int x, y;
    if (!PanelToRender(px, py, x, y, false)) return;          // clicked the dark bars
    if (g_paintMode && PaintBegin(x, y)) return;              // paint mode: the brush owns the left button
    if (GhostFoliage_Set().brush.active && FoliPaintBegin(x, y)) return;
    if (LabelClick(x, y)) return;
    if (GizmoBegin(x, y)) return;
    int id = PickAt(x, y);
    const bool mod = ((GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_CONTROL)) & 0x8000) != 0;

    g_dragging = false; g_collapseId = 0; g_dragMoved = false; g_marquee = false; g_mqClickId = 0;

    if (g_faceMode && id != 0)                                // F: a click picks a face
        if (GhostObject* ob = FindObject(id))
        {
            g_multi.clear();
            g_scene.selectedId = id;
            CurrentFace();
            int face = PickFace(*ob, x, y);
            if (face >= 0) g_scene.selectedFace = (g_scene.selectedFace == face) ? -1 : face;
            return;
        }
    if (mod && id != 0) { ToggleSelect(id); return; }
    // Select tool: a drag that starts on an unselected object draws a box (a plain click still selects it on release)
    if (!mod && id != 0 && !g_faceMode && SelectToolActive() && FindObject(id) && !IsSelected(id))
    {
        g_marquee = true; g_marqueeAdd = false;
        g_mqX0 = g_mqX1 = x; g_mqY0 = g_mqY1 = y;
        g_mqClickId = id;
        return;
    }

    if (mod || id == 0)                                       // empty space: start a selection box
    {
        g_marquee = true; g_marqueeAdd = mod;
        g_mqX0 = g_mqX1 = x; g_mqY0 = g_mqY1 = y;
        return;
    }

    if (IsSelected(id) && !g_multi.empty()) g_collapseId = id;   // keep the group for dragging
    else g_multi.clear();
    if (id != g_scene.selectedId)
    {
        auto it = std::find(g_multi.begin(), g_multi.end(), id);
        if (it != g_multi.end()) { g_multi.erase(it); g_multi.push_back(g_scene.selectedId); }
        g_scene.selectedId = id;
    }
    if (GhostObject* lo = FindObject(id)) if (lo->locked) return;
    GhostVec3* pos = EntityPosition(id);
    GhostVec3 o, d, hit;
    if (!pos || !ScreenRay(x, y, o, d) || !RayPlane(o, d, *pos, hit)) return;

    g_dragId = id;
    g_dragPlanePoint = *pos;
    g_dragOffset = *pos - hit;
    g_dragging = true;
}

void GhostEngine_MouseMove(int px, int py)
{
    UiMouseEvent(0, px, py);
    {
        int rx, ry;
        g_paintMouseIn = PanelToRender(px, py, rx, ry, false);
        if (PanelToRender(px, py, rx, ry, true)) { g_paintMX = rx; g_paintMY = ry; }
    }
    if (g_looking || !g_haveCam || g_playState != GhostPlayState::Editing) { g_hoverId = 0; return; }

    int x, y;
    if (g_gizmo.active)
    {
        PanelToRender(px, py, x, y, true);
        GizmoUpdate(x, y);
        return;
    }
    if (g_marquee)
    {
        PanelToRender(px, py, x, y, true);
        g_mqX1 = x; g_mqY1 = y;
        return;
    }
    if (g_dragging)
    {
        PanelToRender(px, py, x, y, true);
        GhostVec3* pos = EntityPosition(g_dragId);
        GhostVec3 o, d, hit;
        if (pos && ScreenRay(x, y, o, d) && RayPlane(o, d, g_dragPlanePoint, hit))
        {
            const GhostVec3 np = hit + g_dragOffset, delta = np - *pos;
            if (delta.Length() > 1e-5f) g_dragMoved = true;
            *pos = np;
            for (int mid : g_multi)                           // the rest of the group follows
            {
                if (mid == g_dragId) continue;
                GhostObject* ob = FindObject(mid);
                if (ob && ob->locked) continue;
                if (GhostVec3* q = EntityPosition(mid)) *q = *q + delta;
            }
        }
        return;
    }
    if (!PanelToRender(px, py, x, y, false)) { g_hoverId = 0; g_gizmoHover = -1; return; }
    g_gizmoHover = GizmoHit(x, y);
    g_hoverId = PickAt(x, y);
}

void GhostEngine_MouseUp(int x, int y, int button)
{
    if (button == 0) UiMouseEvent(2, x, y);
    if (button != 0) return;
    if (g_marquee) FinishMarquee();
    else if (g_collapseId && !g_dragMoved) { g_multi.clear(); g_scene.selectedId = g_collapseId; }   // plain click on a group member
    g_marquee = false; g_collapseId = 0; g_mqClickId = 0;
    g_gizmo.active = false; g_painting = false; g_foliPainting = false;
    g_dragging = false;
    g_dragId = 0;
}

void GhostEngine_MouseLeave() { g_hoverId = 0; g_paintMouseIn = false; UiMouseEvent(3, 0, 0); }

// ============================================================
//  Exported: scene
// ============================================================
int GhostEngine_AddObject(int meshId, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    if (!CanAddObject(meshId)) return 0;
    GhostObject* o = GhostScene_AddObject(g_scene, meshId);
    if (!o) return 0;

    SpawnCoords(sx, sy);
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += -GhostMesh_Min(meshId).y * o->scale.y;       // rest the mesh on the ground
    o->position = p;
    g_scene.selectedId = o->id;
    return o->id;
}

int GhostEngine_AddLight(int lightType, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    if (lightType < 0 || lightType > 3) return 0;
    GhostLight* l = GhostScene_AddLight(g_scene, (GhostLightType)lightType);
    if (!l) return 0;
    SpawnCoords(sx, sy);
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += 1.5f;
    l->position = p;
    g_scene.selectedId = l->id;
    return l->id;
}

int GhostEngine_AddTileMap(int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    if (!CanAddObject(GHOST_MESH_PLANE)) return 0;
    int have = 0;
    for (const GhostObject& x : g_scene.objects) if (x.tileMap.enabled) have++;
    if (have >= kMaxTileMapObjects) return 0;

    GhostObject* o = GhostScene_AddObject(g_scene, GHOST_MESH_PLANE);       // the plane only supplies 1 material slot; the real mesh is generated
    if (!o) return 0;
    o->name = "Tile Map " + std::to_string(o->id);
    o->tileMap = GhostTileMap();
    o->tileMap.enabled = true;
    GhostTileMap_EnsureTiles(o->tileMap);
    o->useVertexColor = true;                // the checker shading lives in the vertex colors
    o->shadowMode = 0;                       // the landscape doesn't cast shadows (it only receives them)
    o->receiveShadows = true;
    o->collider = true;
    GhostObject_FitTileCollider(*o);

    SpawnCoords(sx, sy);
    o->position = SpawnPoint(sx, sy);
    g_scene.selectedId = o->id;
    return o->id;
}

int GhostEngine_AddVolume(int type, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    GhostVolume* v = GhostScene_AddVolume(g_scene, type);
    if (!v) return 0;
    SpawnCoords(sx, sy);
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += (v->shape == GHOST_VOLSHAPE_SPHERE) ? 1.5f : v->size.y * 0.5f;     // boxes sit on the ground
    v->position = p;
    g_scene.selectedId = v->id;
    return v->id;
}

int GhostEngine_AddParticleSystem(int type, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    SpawnCoords(sx, sy);
    const GhostVec3 p = SpawnPoint(sx, sy);
    const int id = GhostEngine_AddParticleAt(type, p.x, p.y + 0.05f, p.z);
    if (id != 0) { g_scene.selectedId = id; g_scene.selectedFace = -1; g_scene.version++; }
    return id;
}

int GhostEngine_RemoveEntity(int id)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    if (!GhostScene_Remove(g_scene, id)) return 0;
    if (g_hoverId == id) g_hoverId = 0;
    if (g_dragId == id) { g_dragging = false; g_dragId = 0; }
    return 1;
}

void GhostEngine_SelectEntity(int id)
{
    g_multi.clear();
    g_scene.selectedId = (id != 0 && EntityPosition(id)) ? id : 0;
}

int GhostEngine_GetSelected() { return g_scene.selectedId; }
unsigned int GhostEngine_GetSceneVersion() { return g_scene.version; }

int GhostEngine_GetEntities(GhostEntityInfo* out, int maxCount)
{
    if (!out) return 0;
    int n = 0;
    for (const GhostObject& o : g_scene.objects) { if (n >= maxCount) return n; out[n++] = { o.id, 0, o.meshId }; }
    for (const GhostLight& l : g_scene.lights) { if (n >= maxCount) return n; out[n++] = { l.id, 1, (int)l.type }; }
    for (const GhostCamera& c : g_scene.cameras) { if (n >= maxCount) return n; out[n++] = { c.id, 2, 0 }; }
    for (const GhostModEntity& m : g_scene.mods) { if (n >= maxCount) return n; out[n++] = { m.id, 3, m.typeId }; }
    for (const GhostVolume& v : g_scene.volumes) { if (n >= maxCount) return n; out[n++] = { v.id, 4, v.type }; }
    for (const GhostParticleDef& d : g_scene.particles) { if (n >= maxCount) return n; out[n++] = { d.p.id, 4, d.p.type }; }
    return n;
}

int GhostEngine_GetEntityName(int id, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    const std::string* name = nullptr;
    for (const GhostObject& o : g_scene.objects) if (o.id == id) name = &o.name;
    for (const GhostLight& l : g_scene.lights)  if (l.id == id) name = &l.name;
    for (const GhostCamera& c : g_scene.cameras) if (c.id == id) name = &c.name;
    for (const GhostModEntity& m : g_scene.mods) if (m.id == id) name = &m.name;
    for (const GhostVolume& v : g_scene.volumes) if (v.id == id) name = &v.name;
    if (const GhostParticleDef* pd = GhostParticles_Find(id)) name = &pd->name;
    if (!name) { buf[0] = 0; return 0; }
    strncpy_s(buf, bufSize, name->c_str(), _TRUNCATE);
    return 1;
}

int GhostEngine_GetObject(int id, GhostObjectInfo* out)
{
    if (!out) return 0;
    for (const GhostObject& o : g_scene.objects)
        if (o.id == id)
        {
            *out = { o.id, o.meshId,
                     o.position.x, o.position.y, o.position.z,
                     o.rotationDeg.x, o.rotationDeg.y, o.rotationDeg.z,
                     o.scale.x, o.scale.y, o.scale.z,
                     o.faces[0].tint[0], o.faces[0].tint[1], o.faces[0].tint[2] };
            return 1;
        }
    return 0;
}

void GhostEngine_SetObject(const GhostObjectInfo* in)
{
    if (!in) return;
    auto sc = [](float s) { return fabsf(s) < 0.001f ? 0.001f : s; };
    for (GhostObject& o : g_scene.objects)
        if (o.id == in->id)
        {
            o.position = { in->px, in->py, in->pz };
            o.rotationDeg = { in->rx, in->ry, in->rz };
            o.scale = { sc(in->sx), sc(in->sy), sc(in->sz) };
            return;
        }
}

int GhostEngine_GetLight(int id, GhostLightInfo* out)
{
    if (!out) return 0;
    for (const GhostLight& l : g_scene.lights)
        if (l.id == id)
        {
            *out = { l.id, (int)l.type,
                l.position.x, l.position.y, l.position.z,
                l.yawDeg, l.pitchDeg,
                l.color[0], l.color[1], l.color[2],
                l.intensity, l.range, l.width, l.height,
                l.innerDeg, l.outerDeg, l.shadowObjects ? 1 : 0, l.shadowLandscape ? 1 : 0,
                l.spinYaw, l.spinPitch, l.volumetric, l.celestial };
            return 1;
        }
    return 0;
}

void GhostEngine_SetLight(const GhostLightInfo* in)
{
    if (!in) return;
    for (GhostLight& l : g_scene.lights)
        if (l.id == in->id)
        {
            l.position = { in->px, in->py, in->pz };
            l.yawDeg = in->yawDeg;  l.pitchDeg = in->pitchDeg;
            l.color[0] = in->r; l.color[1] = in->g; l.color[2] = in->b;
            l.intensity = in->intensity < 0.0f ? 0.0f : in->intensity;
            l.range = in->range < 0.01f ? 0.01f : in->range;
            l.width = in->width < 0.01f ? 0.01f : in->width;
            l.height = in->height < 0.01f ? 0.01f : in->height;
            auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
            l.outerDeg = cf(in->outerDeg, 1.0f, 89.0f);
            l.innerDeg = cf(in->innerDeg, 0.0f, l.outerDeg);
            l.shadowObjects = in->shadowObjects != 0;
            l.shadowLandscape = in->shadowLandscape != 0;
            l.spinYaw = cf(in->spinYaw, -720.0f, 720.0f);
            l.spinPitch = cf(in->spinPitch, -720.0f, 720.0f);
            l.volumetric = cf(in->volumetric, 0.0f, 5.0f);
            l.celestial = (in->celestial < 0 || l.type != GhostLightType::Directional) ? 0 : (in->celestial > 2 ? 2 : in->celestial);
            return;                                    // type is fixed at creation
        }
}

void GhostEngine_GetAmbient(float* r, float* g, float* b, float* intensity)
{
    if (r) *r = g_scene.ambientColor[0];
    if (g) *g = g_scene.ambientColor[1];
    if (b) *b = g_scene.ambientColor[2];
    if (intensity) *intensity = g_scene.ambientIntensity;
}

void GhostEngine_SetAmbient(float r, float g, float b, float intensity)
{
    g_scene.ambientColor[0] = r; g_scene.ambientColor[1] = g; g_scene.ambientColor[2] = b;
    g_scene.ambientIntensity = intensity < 0.0f ? 0.0f : intensity;
}
static GhostObject* FindObject(int id)
{
    for (GhostObject& o : g_scene.objects) if (o.id == id) return &o;
    return nullptr;
}

// The target face belongs to the selected object, so it resets whenever the selection changes
static int g_faceOwner = 0;
static int CurrentFace()
{
    if (g_faceOwner != g_scene.selectedId) { g_faceOwner = g_scene.selectedId; g_scene.selectedFace = -1; }
    return g_scene.selectedFace;
}
// ============================================================
//  Exported: textures
// ============================================================
int GhostEngine_CreateTexture(const char* name, const unsigned char* rgba,
    unsigned int width, unsigned int height)
{
    if (!rgba || width == 0 || height == 0 || width > 4096 || height > 4096) return -1;
    return RegisterTexture(name, rgba, width, height);
}

int GhostEngine_SetObjectTexture(int objectId, int textureId)
{
    if (textureId < 0 || textureId >= (int)g_scene.textures.size()) return 0;
    GhostObject* o = FindObject(objectId);
    if (!o) return 0;
    for (GhostMaterial& m : o->faces)
    {
        m.textureId = textureId;
        m.tint[0] = m.tint[1] = m.tint[2] = (textureId != 0) ? 1.0f : 0.6f;
    }
    if (o->meshId == GHOST_MESH_QUAD) o->sprite.textureId = textureId;
    o->useVertexColor = false;
    return 1;
}

int GhostEngine_GetObjectTexture(int objectId)
{
    GhostObject* o = FindObject(objectId);
    return o ? o->faces[0].textureId : 0;
}

int GhostEngine_GetTextureCount() { return (int)g_scene.textures.size(); }

int GhostEngine_GetTextureInfo(int id, char* nameBuf, int nameSize,
    unsigned int* width, unsigned int* height)
{
    if (id < 0 || id >= (int)g_scene.textures.size()) return 0;
    const GhostTextureInfo& t = g_scene.textures[id];
    if (nameBuf && nameSize > 0) strncpy_s(nameBuf, nameSize, t.name.c_str(), _TRUNCATE);
    if (width)  *width = t.width;
    if (height) *height = t.height;
    return 1;
}

static GhostCamera* FindCamera(int id)
{
    for (GhostCamera& c : g_scene.cameras) if (c.id == id) return &c;
    return nullptr;
}

static GhostLight* FindLight(int id)
{
    for (GhostLight& l : g_scene.lights) if (l.id == id) return &l;
    return nullptr;
}

// Viewport 0 shows the main view camera; camera windows show their own camera
static GhostCamera* ViewportCamera(int vp)
{
    return FindCamera(vp == 0 ? g_scene.activeCameraId : g_vp[vp].cameraId);
}

// The camera WASD and right-mouse look currently drive (falls back to the main view camera)
static GhostCamera* InputCamera()
{
    GhostCamera* c = FindCamera(g_inputCameraId);
    return c ? c : FindCamera(g_scene.activeCameraId);
}


// ============================================================
//  Shadows: a depth map per directional light, six (a cube) per point / rect light.
//  Rendered inside every viewport's command list, before the main pass.
// ============================================================
static bool ShadowEligible(const GhostObject& o, const GhostLight& l)
{
    const bool normal = o.shadowMode == 1 || (o.shadowMode == 2 && o.shadowLightId == l.id);
    if (l.type != GhostLightType::Spot) return normal;
    if (o.tileMap.enabled) return l.shadowLandscape;            // the spot decides whether the landscape casts
    return l.shadowObjects && normal;
}

// Does this model have a masked material with a texture (leaf cards, fences, grates)?
static bool HasMaskedFace(const GhostObject& o)
{
    const int n = GhostMesh_FaceCount(o.meshId);
    for (int f = 0; f < n; f++)
        if (o.faces[f].alphaMode == 1 && o.faces[f].textureId > 0) return true;
    return false;
}

static void RenderShadowPass(GhostSceneCB& cb, const GhostVec3& eye, const GhostVec3& forward, unsigned int rw, unsigned int rh)
{
    for (unsigned int i = 0; i < GHOST_MAX_LIGHTS; i++) cb.lights[i].shadow[0] = -1.0f;   // "no shadow" by default
    cb.shadowGlobal[0] = 0.0f;
    cb.shadowGlobal[1] = (float)g_pipeline->shadowSize;
    if (!g_scene.shadowsEnabled || g_scene.shadowStrength <= 0.0f || cb.lightCount == 0) return;

    bool anyLight = false;
    for (const GhostLight& l : g_scene.lights) if (l.castShadows) anyLight = true;
    if (!anyLight) return;

    const bool playing = g_playState != GhostPlayState::Editing;

    // ---- casters: written once into the shadow constant buffer, reused by every slice ----
    struct Caster { const GhostObject* o; unsigned int slot, tex, first, count; };
    static std::vector<Caster> casters;
    casters.clear();

    GhostVec3 bmin = { 1e30f, 1e30f, 1e30f }, bmax = { -1e30f, -1e30f, -1e30f };
    unsigned int slot = 0;
    for (const GhostObject& o : g_scene.objects)
    {
        if (!o.visible && playing) continue;

        const GhostVec3 mn = GhostObject_Min(o), mx = GhostObject_Max(o);
        const float ex = fmaxf(fabsf(mn.x), fabsf(mx.x)) * fabsf(o.scale.x);
        const float ey = fmaxf(fabsf(mn.y), fabsf(mx.y)) * fabsf(o.scale.y);
        const float ez = fmaxf(fabsf(mn.z), fabsf(mx.z)) * fabsf(o.scale.z);
        const float r = sqrtf(ex * ex + ey * ey + ez * ez);
        bmin.x = fminf(bmin.x, o.position.x - r); bmax.x = fmaxf(bmax.x, o.position.x + r);
        bmin.y = fminf(bmin.y, o.position.y - r); bmax.y = fmaxf(bmax.y, o.position.y + r);
        bmin.z = fminf(bmin.z, o.position.z - r); bmax.z = fmaxf(bmax.z, o.position.z + r);

        if ((o.shadowMode == 0 && !o.tileMap.enabled) || slot >= GHOST_SHADOW_CB_SLOTS) continue;

        GhostObjectCB c = {};
        float byw;
        c.model = GhostObjectModel(o, BillYaw(o, eye, byw));

        if (!o.tileMap.enabled && o.meshId != GHOST_MESH_QUAD && HasMaskedFace(o))
        {
            // masked materials: one draw per run of faces that share a material, so each run punches its own holes
            const int faceCount = GhostMesh_FaceCount(o.meshId);
            int f = 0;
            while (f < faceCount && slot < GHOST_SHADOW_CB_SLOTS)
            {
                int g = f + 1;
                while (g < faceCount && GhostMaterial_Equal(o.faces[g], o.faces[f])) g++;

                const GhostMaterial& m = o.faces[f];
                const FaceSpan a = FaceSpanOf(o, f), b = FaceSpanOf(o, g - 1);
                const unsigned int runCount = b.first + b.count - a.first;
                if (runCount > 0)
                {
                    GhostObjectCB cc = c;
                    unsigned int tex = 0;
                    if (m.alphaMode == 1 && m.textureId > 0 && m.textureId < (int)g_scene.textures.size())
                    {
                        cc.pad[1] = 8.0f;                                   // shader flag: masked material
                        cc.uvParams[0] = m.tiling[0]; cc.uvParams[1] = m.tiling[1];
                        cc.uvParams[2] = m.offset[0]; cc.uvParams[3] = m.offset[1];
                        cc.alphaP[1] = (float)m.alphaSource;
                        cc.alphaP[2] = m.alphaCutoff;
                        tex = (unsigned int)m.textureId;
                    }
                    GhostUpdateShadowCB(g_pipeline, slot, &cc);
                    casters.push_back({ &o, slot, tex, a.first, runCount });
                    slot++;
                }
                f = g;
            }
            continue;
        }

        unsigned int texIdx = 0;
        if (o.meshId == GHOST_MESH_QUAD)                                        // sprite: alpha-tested, same frame as the main pass
        {
            const GhostSprite& sp = o.sprite;
            int tex, cols, rows, frame;
            SpriteDrawInfo(o, tex, cols, rows, frame);
            c.pad[1] = sp.alphaClip ? 1.0f : 0.0f;
            const int col = frame % cols, row = frame / cols;
            c.uvParams[0] = (sp.flipX ? -1.0f : 1.0f) / (float)cols;
            c.uvParams[1] = 1.0f / (float)rows;
            c.uvParams[2] = ((float)col + 0.5f) / (float)cols - 0.5f;
            c.uvParams[3] = ((float)row + 0.5f) / (float)rows - 0.5f;
            texIdx = (unsigned int)(tex > 0 ? tex : 0);
        }
        GhostUpdateShadowCB(g_pipeline, slot, &c);
        casters.push_back({ &o, slot, texIdx, 0, 0 });
        slot++;
    }
    static std::vector<FoliShadowCaster> foliCasters;
    FoliageBuildShadowCasters(eye, forward, slot, foliCasters, bmin, bmax);
    if (casters.empty() && foliCasters.empty()) return;

    GhostVec3 center = (bmin + bmax) * 0.5f;
    float R = (bmax - bmin).Length() * 0.5f;
    R = R < 1.0f ? 1.0f : (R > 1000.0f ? 1000.0f : R);
    const float fitR = g_scene.shadowDistance * 0.5f;                  // fit the map to the area in front of the camera
    const bool  fit = fitR >= 1.0f && fitR < R;                        // a small scene already fits, so it keeps the old way
    if (fit) { R = fitR; center = eye + forward * R; }

    // ---- one job per map slice ----
    struct Job { const GhostLight* light; int slice; float vp[16]; float posFar[4]; float mode; };
    static std::vector<Job> jobs;
    jobs.clear();

    auto push = [&](const GhostLight& l, int slice, const GhostMat4& vpm, const GhostVec3& pos, float farW, float mode)
        {
            Job j;
            j.light = &l; j.slice = slice; j.mode = mode;
            memcpy(j.vp, vpm.m, 16 * sizeof(float));
            j.posFar[0] = pos.x; j.posFar[1] = pos.y; j.posFar[2] = pos.z; j.posFar[3] = farW;
            memcpy(cb.shadowVP[slice], vpm.m, 16 * sizeof(float));
            jobs.push_back(j);
        };

    static const GhostVec3 kFaceDir[6] = { { 1, 0, 0 },{ -1, 0, 0 },{ 0, 1, 0 },{ 0, -1, 0 },{ 0, 0, 1 },{ 0, 0, -1 } };
    static const GhostVec3 kFaceUp[6] = { { 0, 1, 0 },{ 0, 1, 0 },{ 0, 0, 1 },{ 0, 0, 1 },{ 0, 1, 0 },{ 0, 1, 0 } };
    const float kTanHalf = 1.05f;                              // a little wider than 90 degrees, so soft edges stay inside a face

    int next = 0;
    for (size_t li = 0; li < g_scene.lights.size() && li < cb.lightCount; li++)
    {
        const GhostLight& l = g_scene.lights[li];
        if (!l.castShadows || l.celestialFade <= 0.002f) continue;

        bool has = false;
        for (const Caster& c : casters) if (ShadowEligible(*c.o, l)) { has = true; break; }

        if (!has) for (const FoliShadowCaster& fc : foliCasters) if (FoliShadowEligible(*fc.layer, l)) { has = true; break; }
        if (!has) continue;

        const bool dirL = (l.type == GhostLightType::Directional);
        const bool spotL = (l.type == GhostLightType::Spot);
        const int need = (dirL || spotL) ? 1 : 6;
        if (next + need > (int)GHOST_SHADOW_SLICES) continue;       // out of maps: this light casts no shadows

        GhostGPULight& g = cb.lights[li];
        g.shadow[0] = (float)next;

        if (dirL)
        {
            const GhostVec3 dir = LightForward(l).Normalize();
            const GhostVec3 up = fabsf(dir.y) > 0.99f ? GhostVec3(0, 0, 1) : GhostVec3(0, 1, 0);
            const float texelW = (2.0f * R) / (float)g_pipeline->shadowSize;
            GhostVec3 c = center;
            if (fit)                                       // snap to the texel grid, so shadows don't shimmer when the camera moves
            {
                const GhostVec3 lr = up.Cross(dir).Normalize();
                const GhostVec3 lu = dir.Cross(lr);
                const float cx = c.Dot(lr), cy = c.Dot(lu);
                c = c + lr * (floorf(cx / texelW + 0.5f) * texelW - cx) + lu * (floorf(cy / texelW + 0.5f) * texelW - cy);
            }
            const GhostVec3 from = c - dir * (R * 2.0f);
            const float farW = R * 4.0f;
            const GhostMat4 vpm = GhostMat4::OrthoLH(2.0f * R, 2.0f * R, 0.0f, farW) * GhostMat4::LookAtLH(from, c, up);
            g.shadow[1] = farW;
            g.shadow[2] = texelW;
            push(l, next, vpm, c, farW, 0.0f);
        }
        else if (spotL)
        {
            const float d2r = GHOST_PI / 180.0f;
            const GhostVec3 dir = LightForward(l).Normalize();
            const GhostVec3 up = fabsf(dir.y) > 0.99f ? GhostVec3(0, 0, 1) : GhostVec3(0, 1, 0);
            const float farW = fmaxf(l.range, 0.5f);
            const float tanHalf = tanf(fminf(l.outerDeg, 85.0f) * d2r) * 1.08f;   // a little wider, so soft edges stay inside
            const GhostMat4 proj = GhostMat4::PerspectiveFovLH(2.0f * atanf(tanHalf), 1.0f, 0.05f, farW);
            g.shadow[1] = farW;
            g.shadow[2] = (2.0f * tanHalf) / (float)g_pipeline->shadowSize;
            push(l, next, proj * GhostMat4::LookAtLH(l.position, l.position + dir, up), l.position, farW, 1.0f);
        }
        else
        {
            const float farW = fmaxf(l.range, 0.5f);
            const GhostMat4 proj = GhostMat4::PerspectiveFovLH(2.0f * atanf(kTanHalf), 1.0f, 0.05f, farW);
            g.shadow[1] = farW;
            g.shadow[2] = (2.0f * kTanHalf) / (float)g_pipeline->shadowSize;
            for (int f = 0; f < 6; f++)
                push(l, next + f, proj * GhostMat4::LookAtLH(l.position, l.position + kFaceDir[f], kFaceUp[f]),
                    l.position, farW, 1.0f);
        }
        next += need;
    }
    if (jobs.empty()) return;

    cb.shadowGlobal[0] = g_scene.shadowStrength;
    cb.shadowGlobal[2] = (fit && g_scene.shadowFilterLod) ? g_scene.shadowDistance * g_scene.shadowFilterLodFrac : 0.0f;
    GhostShadowBegin(g_rhi, g_pipeline);
    for (const Job& j : jobs)
    {
        GhostShadowSlice(g_rhi, g_pipeline, j.slice, j.vp, j.posFar, j.mode);
        for (const Caster& c : casters)
            if (ShadowEligible(*c.o, *j.light)) GhostShadowDraw(g_rhi, g_pipeline, DrawMeshOf(*c.o), c.slot, c.tex, c.first, c.count);
        for (const FoliShadowCaster& fc : foliCasters)
            if (FoliShadowEligible(*fc.layer, *j.light))
                GhostShadowDrawInstanced(g_rhi, g_pipeline, fc.meshId, fc.slot, fc.tex, fc.gpu->buf, fc.gpu->count, fc.first, fc.count);
    }
    GhostShadowEnd(g_rhi, g_pipeline, rw, rh);
}



// ============================================================
//  Viewport overlay: grid, selection gizmo, dimensions, FPS.
//  Main view only. Everything except the FPS counter is editor-only.
// ============================================================
static GhostViewSettings g_viewCfg = { 1, 0, 1, 1,   0, 7, 1,   0, 0,   1.0f,   20,   0, 0, 2, 2 };
static bool SelectToolActive() { return g_viewCfg.tool == 0; }
static GhostOverlay      g_ov;
static float             g_fps = 0.0f;
static float g_timeSec = 0.0f;       // drives WPO sway

static const float kAxisCol[3][4] = { { 0.92f, 0.30f, 0.30f, 1.0f },{ 0.50f, 0.85f, 0.30f, 1.0f },{ 0.30f, 0.55f, 0.95f, 1.0f } };
static const float kUnitMul[5] = { 1000.0f, 100.0f, 1.0f, 39.3701f, 3.28084f };      // 1 engine unit = 1 metre
static const char* kUnitTxt[5] = { "mm", "cm", "m", "in", "ft" };

static void FormatLength(float worldUnits, char* out, int size)
{
    const int u = g_viewCfg.unit < 0 ? 0 : (g_viewCfg.unit > 4 ? 4 : g_viewCfg.unit);
    const int d = g_viewCfg.decimals < 0 ? 0 : (g_viewCfg.decimals > 3 ? 3 : g_viewCfg.decimals);
    snprintf(out, (size_t)size, "%.*f%s", d, worldUnits * kUnitMul[u], kUnitTxt[u]);
}

static void OverlayGrid(const GhostVec3& eye)
{
    const GhostViewSettings& s = g_viewCfg;
    const int   N = s.gridExtent;
    const float step = s.gridSize;
    const float ext = (float)N * step;

    static const GhostVec3 PA[3] = { { 1, 0, 0 },{ 1, 0, 0 },{ 0, 1, 0 } };      // the two axes that span the plane
    static const GhostVec3 PB[3] = { { 0, 0, 1 },{ 0, 1, 0 },{ 0, 0, 1 } };
    static const GhostVec3 PN[3] = { { 0, 1, 0 },{ 0, 0, 1 },{ 1, 0, 0 } };      // plane normal
    static const int AXA[3] = { 0, 0, 1 }, AXB[3] = { 2, 1, 2 };                   // which world axis A and B are
    const int pl = s.gridPlane < 0 ? 0 : (s.gridPlane > 2 ? 2 : s.gridPlane);
    const GhostVec3 A = PA[pl], B = PB[pl];
    const GhostVec3 lift = PN[pl] * 0.004f;                                        // keeps the grid off a floor at the same height

    const float ca = floorf(eye.Dot(A) / step + 0.5f), cb = floorf(eye.Dot(B) / step + 0.5f);   // centered under the camera
    const bool bp = (s.gridStyle == 1);
    const int SEG = 12;                                                            // each line is split so it can fade with distance

    for (int pass = 0; pass < 2; pass++)
    {
        const GhostVec3 along = pass == 0 ? B : A;
        const GhostVec3 across = pass == 0 ? A : B;
        const float cAlong = pass == 0 ? cb : ca;
        const float cAcross = pass == 0 ? ca : cb;

        for (int i = -N; i <= N; i++)
        {
            const long idx = (long)cAcross + i;
            const bool axis = (idx == 0) && s.showWorldAxes;
            const bool major = (idx % 10) == 0;

            float col[4]; float width;
            if (axis) { const float* a = kAxisCol[pass == 0 ? AXB[pl] : AXA[pl]]; col[0] = a[0]; col[1] = a[1]; col[2] = a[2]; col[3] = 0.9f; width = 2.2f; }
            else if (major) { if (bp) { col[0] = 0.60f; col[1] = 0.82f; col[2] = 1.00f; col[3] = 0.60f; } else { col[0] = 0.75f; col[1] = 0.75f; col[2] = 0.80f; col[3] = 0.42f; } width = 1.4f; }
            else { if (bp) { col[0] = 0.35f; col[1] = 0.60f; col[2] = 0.95f; col[3] = 0.30f; } else { col[0] = 0.65f; col[1] = 0.65f; col[2] = 0.70f; col[3] = 0.22f; } width = 1.0f; }

            const GhostVec3 origin = across * ((float)idx * step) + lift;
            for (int g = 0; g < SEG; g++)
            {
                const float t0 = ((cAlong - (float)N) + (2.0f * (float)N) * (float)g / (float)SEG) * step;
                const float t1 = ((cAlong - (float)N) + (2.0f * (float)N) * (float)(g + 1) / (float)SEG) * step;
                const GhostVec3 p0 = origin + along * t0, p1 = origin + along * t1;
                float fade = 1.0f - ((p0 + p1) * 0.5f - eye).Length() / (ext * 1.15f + 1.0f);
                fade = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade);
                const float c[4] = { col[0], col[1], col[2], col[3] * fade };
                if (c[3] < 0.01f) continue;
                g_ov.Line(p0, p1, c, width, true);
            }
        }
    }
}

static void OverlayBounds(const GhostObject& o, const GhostVec3& eye)
{
    float byw;
    const GhostMat4 M = GhostObjectModel(o, BillYaw(o, eye, byw));
    const GhostVec3 mn = GhostObject_Min(o), mx = GhostObject_Max(o);
    GhostVec3 c[8];
    for (int i = 0; i < 8; i++)
        c[i] = XformPoint(M, GhostVec3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z), 1.0f);
    static const int E[12][2] = { { 0,1 },{ 2,3 },{ 4,5 },{ 6,7 },{ 0,2 },{ 1,3 },{ 4,6 },{ 5,7 },{ 0,4 },{ 1,5 },{ 2,6 },{ 3,7 } };
    const float col[4] = { 1.0f, 0.62f, 0.12f, 0.85f };
    for (int e = 0; e < 12; e++) g_ov.Line(c[E[e][0]], c[E[e][1]], col, 1.5f, false);
}

// The gameplay collider: axis-aligned (rotation is ignored, like the game), drawn in red
// The collider's three axes in world space. Returns false when it isn't rotated.
static bool ColliderAxes(const GhostObject& o, GhostVec3 ax[3])
{
    ax[0] = GhostVec3(1, 0, 0); ax[1] = GhostVec3(0, 1, 0); ax[2] = GhostVec3(0, 0, 1);
    const GhostVec3 r = o.colliderRotation;
    if (fabsf(r.x) < 0.01f && fabsf(r.y) < 0.01f && fabsf(r.z) < 0.01f) return false;
    const float d2r = GHOST_PI / 180.0f;
    const GhostMat4 R = GhostMat4::RotationY(r.y * d2r) * GhostMat4::RotationX(r.x * d2r) * GhostMat4::RotationZ(r.z * d2r);
    for (int k = 0; k < 3; k++) ax[k] = GhostVec3(R.m[0][k], R.m[1][k], R.m[2][k]);
    return true;
}

// The gameplay collider, drawn in red (rotated by Collider Rotation)
static void OverlayCollider(const GhostObject& o)
{
    const GhostVec3 h(fabsf(o.colliderSize.x * o.scale.x) * 0.5f,
        fabsf(o.colliderSize.y * o.scale.y) * 0.5f,
        fabsf(o.colliderSize.z * o.scale.z) * 0.5f);
    const GhostVec3 c(o.position.x + o.colliderOffset.x * o.scale.x,
        o.position.y + o.colliderOffset.y * o.scale.y,
        o.position.z + o.colliderOffset.z * o.scale.z);
    GhostVec3 ax[3];
    ColliderAxes(o, ax);
    GhostVec3 p[8];
    for (int i = 0; i < 8; i++)
    {
        const float sx = (i & 1) ? h.x : -h.x, sy = (i & 2) ? h.y : -h.y, sz = (i & 4) ? h.z : -h.z;
        p[i] = c + ax[0] * sx + ax[1] * sy + ax[2] * sz;
    }
    static const int E[12][2] = { { 0,1 },{ 2,3 },{ 4,5 },{ 6,7 },{ 0,2 },{ 1,3 },{ 4,6 },{ 5,7 },{ 0,4 },{ 1,5 },{ 2,6 },{ 3,7 } };
    const float col[4] = { 1.0f, 0.2f, 0.2f, 0.95f };
    for (int e = 0; e < 12; e++) g_ov.Line(p[E[e][0]], p[E[e][1]], col, 2.0f, false);
}

// ---------------- Gizmo: shared frame, hit testing, dragging, drawing ----------------
struct GizmoFrame
{
    GhostVec3 pos;
    GhostVec3 ax[3];
    float     L = 1.0f;          // world length of the handles (constant on screen)
    int       tool = 0;          // the tool that actually applies to this entity
    bool      object = false;
    bool col = false;
};

static GhostGizmoSettings g_gizSet = { 1, 0.0f, 15.0f, 0.1f };

static bool GizmoFrameOf(int selId, GizmoFrame& f)
{
    if (!g_viewCfg.showSelection || !g_haveCam || selId == 0) return false;
    GhostVec3* pp = EntityPosition(selId);
    if (!pp) return false;
    GhostObject* o = FindObject(selId);
    f.object = (o != nullptr);
    f.pos = *pp;

    f.col = false;
    if (g_colMode && o && o->collider)
    {
        f.col = true;                                   // the gizmo sits on the red collider box
        f.pos = GhostVec3(o->position.x + o->colliderOffset.x * o->scale.x,
            o->position.y + o->colliderOffset.y * o->scale.y,
            o->position.z + o->colliderOffset.z * o->scale.z);
    }

    int tool = g_viewCfg.tool;
    if (tool == 4) tool = 0;                                   // the Dimension tool has no handles
    const bool part = (!o && GhostParticles_Find(selId) != nullptr);
    if (!o && !part && tool == 3) tool = 0;                    // only objects and particle systems can be scaled
    if (!o && !part && tool == 2)                              // lights can be aimed (point lights have no direction)                                     // lights can be aimed (point lights have no direction)
    {
        const GhostLight* li = FindLight(selId);
        if (!li || li->type == GhostLightType::Point) tool = 0;
    }
    if (o && o->locked) tool = 0;                              // a locked object shows plain axes and nothing can be grabbed

    f.tool = tool;

    f.ax[0] = GhostVec3(1, 0, 0); f.ax[1] = GhostVec3(0, 1, 0); f.ax[2] = GhostVec3(0, 0, 1);
    if (o && !f.col && (g_viewCfg.space == 1 || tool == 3))
    {
        const GhostMat4 M = GhostObjectModel(*o);
        for (int k = 0; k < 3; k++) f.ax[k] = GhostVec3(M.m[0][k], M.m[1][k], M.m[2][k]).Normalize();
    }
    else if (part && (g_viewCfg.space == 1 || tool == 3))
    {
        GhostVec3 pa[3];
        GhostParticles_Axes(GhostParticles_Find(selId)->p, pa);
        for (int k = 0; k < 3; k++) f.ax[k] = pa[k].Normalize();
    }
    const GhostVec3 v = XformPoint(g_view, f.pos, 1.0f);       // view-space depth
    if (v.z < 0.1f) return false;
    f.L = 90.0f * v.z / (g_proj.m[1][1] * (float)g_mainRH * 0.5f);   // 90 pixels
    return true;
}

static float SnapTo(float v, float step) { return step > 1e-6f ? floorf(v / step + 0.5f) * step : v; }
static float Round3(float v) { return floorf(v * 1000.0f + 0.5f) / 1000.0f; }

static float PtSegDist(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float cx = ax + dx * t - px, cy = ay + dy * t - py;
    return sqrtf(cx * cx + cy * cy);
}

// t on the line (p0 + t*a) that is closest to the ray (o + s*d). false when they are nearly parallel
static bool AxisParam(const GhostVec3& o, const GhostVec3& d, const GhostVec3& p0, const GhostVec3& a, float& t)
{
    const GhostVec3 w0 = o - p0;
    const float b = d.Dot(a), dd = d.Dot(w0), e = a.Dot(w0);
    const float den = 1.0f - b * b;
    if (den < 1e-3f) return false;
    t = (e - b * dd) / den;
    return true;
}

static bool RayPlanePN(const GhostVec3& o, const GhostVec3& d, const GhostVec3& p, const GhostVec3& n, GhostVec3& hit)
{
    const float den = d.Dot(n);
    if (fabsf(den) < 0.03f) return false;                      // the plane is edge-on
    const float t = (p - o).Dot(n) / den;
    if (t < 0.0f) return false;
    hit = o + d * t;
    return true;
}

// -1 = nothing, 0..2 = an axis, 3 = the uniform scale handle
static int GizmoHit(int x, int y)
{
    GizmoFrame f;
    if (!GizmoFrameOf(g_scene.selectedId, f) || f.tool < 1) return -1;
    float ox, oy;
    if (!WorldToScreen(f.pos, ox, oy)) return -1;

    if (f.tool == 3)
    {
        const float dx = (float)x - ox, dy = (float)y - oy;
        if (sqrtf(dx * dx + dy * dy) < 9.0f) return 3;
    }

    float best = 1e9f;
    int hit = -1;
    for (int k = 0; k < 3; k++)
    {
        float d = 1e9f;
        if (f.tool == 2)                                       // the ring
        {
            const GhostVec3 u = f.ax[(k + 1) % 3], v = f.ax[(k + 2) % 3];
            const float R = f.L * 0.8f;
            float px0 = 0, py0 = 0;
            bool have = false;
            for (int s = 0; s <= 48; s++)
            {
                const float a = (float)s / 48.0f * 2.0f * GHOST_PI;
                float px, py;
                if (!WorldToScreen(f.pos + (u * cosf(a) + v * sinf(a)) * R, px, py)) { have = false; continue; }
                if (have) { const float sd = PtSegDist((float)x, (float)y, px0, py0, px, py); if (sd < d) d = sd; }
                px0 = px; py0 = py; have = true;
            }
            if (d < 7.0f && d < best) { best = d; hit = k; }
        }
        else                                                   // the arrow / line (the first 15% is left free for grabbing the object)
        {
            float ax, ay, bx, by;
            if (!WorldToScreen(f.pos + f.ax[k] * (f.L * 0.15f), ax, ay) ||
                !WorldToScreen(f.pos + f.ax[k] * f.L, bx, by)) continue;
            d = PtSegDist((float)x, (float)y, ax, ay, bx, by);
            if (d < 9.0f && d < best) { best = d; hit = k; }
        }
    }
    return hit;
}

static bool GizmoBegin(int x, int y)
{
    g_gizmo.active = false;
    GizmoFrame f;
    if (!GizmoFrameOf(g_scene.selectedId, f) || f.tool < 1) return false;
    const int h = GizmoHit(x, y);
    if (h < 0) return false;

    GhostVec3 ro, rd;
    if (!ScreenRay(x, y, ro, rd)) return false;
    GhostObject* o = FindObject(g_scene.selectedId);

    GizmoState g;
    g.id = g_scene.selectedId;
    g.axis = h;
    g.tool = f.tool;
    g.local = (g_viewCfg.space == 1) || f.tool == 3;
    g.startPos = f.pos;
    if (o) { g.startRot = o->rotationDeg; g.startScale = o->scale; }
    if (!o)                                                    // a particle system keeps rotation and scale in its params
    {
        if (const GhostVec3* pr = GhostParticles_RotationPtr(g.id)) g.startRot = *pr;
        if (const GhostVec3* ps = GhostParticles_ScalePtr(g.id)) g.startScale = *ps;
    }
    if (const GhostLight* li = FindLight(g.id)) { g.startYaw = li->yawDeg; g.startPitch = li->pitchDeg; }
    g.col = f.col;
    if (o) { g.startColSize = o->colliderSize; g.startColOff = o->colliderOffset; g.startColRot = o->colliderRotation; }
    if (h < 3) { g.dir = f.ax[h]; g.u = f.ax[(h + 1) % 3]; g.v = f.ax[(h + 2) % 3]; }

    if (f.tool == 1 || (f.tool == 3 && h < 3))                 // slide along an axis
    {
        float t;
        if (!AxisParam(ro, rd, f.pos, g.dir, t)) return true;               // looking straight down the axis: nothing to grab
        if (f.tool == 3 && fabsf(t) < f.L * 0.05f) return true;
        g.t0 = t;
    }
    else if (f.tool == 2)                                      // turn around an axis
    {
        GhostVec3 hit;
        if (!RayPlanePN(ro, rd, f.pos, g.dir, hit)) return true;
        const GhostVec3 r = hit - f.pos;
        g.lastAngle = atan2f(r.Dot(g.v), r.Dot(g.u));
    }
    else                                                       // uniform scale: the distance from the origin on screen
    {
        float ox, oy;
        if (!WorldToScreen(f.pos, ox, oy)) return true;
        g.ox = ox; g.oy = oy;
        g.d0 = sqrtf(((float)x - ox) * ((float)x - ox) + ((float)y - oy) * ((float)y - oy));
    }
    g_group.clear();
    if (!f.col)
        for (int mid : g_multi)
            if (GhostObject* mo = FindObject(mid)) if (!mo->locked) g_group.push_back({ mid, mo->position, mo->rotationDeg, mo->scale });
    g.active = true;
    g_gizmo = g;
    return true;                                               // the click was consumed by the gizmo
}

// Euler order is the engine's: model = Ry * Rx * Rz
static void ApplyRotationTo(GhostVec3& outDeg, const GizmoState& g, float rad)
{
    const float d2r = GHOST_PI / 180.0f, r2d = 180.0f / GHOST_PI;
    const GhostMat4 R0 = GhostMat4::RotationY(g.startRot.y * d2r) * GhostMat4::RotationX(g.startRot.x * d2r) *
        GhostMat4::RotationZ(g.startRot.z * d2r);

    const GhostVec3 a = g.local ? GhostVec3((float)(g.axis == 0), (float)(g.axis == 1), (float)(g.axis == 2)) : g.dir;
    const float c = cosf(rad), s = sinf(rad), t = 1.0f - c;
    GhostMat4 Rd = GhostMat4::Identity();                      // axis-angle (Rodrigues), same handedness as RotationX/Y/Z
    Rd.m[0][0] = t * a.x * a.x + c;       Rd.m[0][1] = t * a.x * a.y - s * a.z; Rd.m[0][2] = t * a.x * a.z + s * a.y;
    Rd.m[1][0] = t * a.x * a.y + s * a.z; Rd.m[1][1] = t * a.y * a.y + c;       Rd.m[1][2] = t * a.y * a.z - s * a.x;
    Rd.m[2][0] = t * a.x * a.z - s * a.y; Rd.m[2][1] = t * a.y * a.z + s * a.x; Rd.m[2][2] = t * a.z * a.z + c;

    const GhostMat4 Rn = g.local ? (R0 * Rd) : (Rd * R0);

    float ex, ey, ez;
    const float r12 = Rn.m[1][2];
    if (fabsf(r12) < 0.99999f)
    {
        ex = asinf(-r12);
        ey = atan2f(Rn.m[0][2], Rn.m[2][2]);
        ez = atan2f(Rn.m[1][0], Rn.m[1][1]);
    }
    else                                                       // gimbal lock: the Z turn is folded into Y
    {
        ex = (r12 < 0.0f) ? GHOST_PI * 0.5f : -GHOST_PI * 0.5f;
        ey = atan2f(-Rn.m[2][0], Rn.m[0][0]);
        ez = 0.0f;
    }
    outDeg = GhostVec3(Round3(ex * r2d), Round3(ey * r2d), Round3(ez * r2d));
}
static void ApplyRotation(GhostObject& o, const GizmoState& g, float rad) { ApplyRotationTo(o.rotationDeg, g, rad); }
static void ApplyLightRotation(GhostLight& l, const GizmoState& g, float rad)
{
    const GhostVec3 f0 = AnglesToDir(g.startYaw, g.startPitch);
    const GhostVec3 a = g.dir;
    const float c = cosf(rad), s = sinf(rad), t = 1.0f - c;
    GhostVec3 f((t * a.x * a.x + c) * f0.x + (t * a.x * a.y - s * a.z) * f0.y + (t * a.x * a.z + s * a.y) * f0.z,
        (t * a.x * a.y + s * a.z) * f0.x + (t * a.y * a.y + c) * f0.y + (t * a.y * a.z - s * a.x) * f0.z,
        (t * a.x * a.z - s * a.y) * f0.x + (t * a.y * a.z + s * a.x) * f0.y + (t * a.z * a.z + c) * f0.z);
    f = f.Normalize();
    l.yawDeg = Round3(atan2f(f.x, f.z) * (180.0f / GHOST_PI));
    l.pitchDeg = Round3(asinf(f.y < -1.0f ? -1.0f : (f.y > 1.0f ? 1.0f : f.y)) * (180.0f / GHOST_PI));
}

static void GizmoUpdate(int x, int y)
{
    GizmoState& g = g_gizmo;
    if (!g.active) return;
    GhostVec3* pos = EntityPosition(g.id);
    GhostObject* o = FindObject(g.id);
    GhostLight* li = o ? nullptr : FindLight(g.id);
    GhostVec3* pRot = o ? nullptr : GhostParticles_RotationPtr(g.id);
    GhostVec3* pScale = o ? nullptr : GhostParticles_ScalePtr(g.id);
    if (!pos || (g.tool == 3 && !o && !pScale) || (g.col && !o) || (g.tool == 2 && !o && !li && !pRot)) { g.active = false; return; }

    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)                  // cancel: put everything back
    {
        if (g.col) { o->colliderOffset = g.startColOff; o->colliderSize = g.startColSize; o->colliderRotation = g.startColRot; }
        else {
            *pos = g.startPos; if (o) { o->rotationDeg = g.startRot; o->scale = g.startScale; }
            if (li) { li->yawDeg = g.startYaw; li->pitchDeg = g.startPitch; }
            if (pRot) *pRot = g.startRot;
            if (pScale) *pScale = g.startScale;
        }
        for (const GroupItem& it : g_group)
        {
            if (GhostVec3* q = EntityPosition(it.id)) *q = it.pos;
            if (GhostObject* go = FindObject(it.id)) { go->rotationDeg = it.rot; go->scale = it.scale; }
        }
        g.active = false;
        return;
    }

    g.snapping = (g_gizSet.snap != 0) != ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    GhostVec3 ro, rd;
    if (!ScreenRay(x, y, ro, rd)) return;

    if (g.tool == 1)
    {
        float t;
        if (!AxisParam(ro, rd, g.startPos, g.dir, t)) return;
        float s = t - g.t0;
        if (g.snapping)
        {
            const float step = g_gizSet.moveStep > 0.0f ? g_gizSet.moveStep : g_viewCfg.gridSize;
            if (g_viewCfg.space == 0) { const float a0 = g.startPos.Dot(g.dir); s = SnapTo(a0 + s, step) - a0; }
            else s = SnapTo(s, step);
        }
        const GhostVec3 np = g.startPos + g.dir * s;
        if (g.col)                                             // move the collider: its offset changes
        {
            auto inv = [](float v) { return fabsf(v) > 1e-6f ? 1.0f / v : 0.0f; };
            o->colliderOffset = GhostVec3((np.x - o->position.x) * inv(o->scale.x),
                (np.y - o->position.y) * inv(o->scale.y),
                (np.z - o->position.z) * inv(o->scale.z));
        }
        else
        {
            *pos = np;
            for (const GroupItem& it : g_group) if (GhostVec3* q = EntityPosition(it.id)) *q = it.pos + g.dir * s;
        }
        g.value = s;
    }
    else if (g.tool == 2)
    {
        GhostVec3 hit;
        if (!RayPlanePN(ro, rd, g.startPos, g.dir, hit)) return;
        const GhostVec3 r = hit - g.startPos;
        const float a = atan2f(r.Dot(g.v), r.Dot(g.u));
        float d = a - g.lastAngle;
        while (d > GHOST_PI) d -= 2.0f * GHOST_PI;
        while (d < -GHOST_PI) d += 2.0f * GHOST_PI;
        g.accum += d;
        g.lastAngle = a;

        float deg = g.accum * (180.0f / GHOST_PI);
        if (g.snapping) deg = SnapTo(deg, g_gizSet.rotStep);
        const float rad = deg * (GHOST_PI / 180.0f);
        if (g.col)                                             // turn the collider, not the object
        {
            GizmoState gg = g;
            gg.startRot = g.startColRot;
            GhostVec3 cr = o->colliderRotation;
            ApplyRotationTo(cr, gg, rad);
            o->colliderRotation = cr;
        }
        else if (li)                                           // a light: aim it
        {
            ApplyLightRotation(*li, g, rad);
        }
        else if (pRot) { GhostVec3 r = *pRot; ApplyRotationTo(r, g, rad); *pRot = r; }
        else
        {
            ApplyRotation(*o, g, rad);
            for (const GroupItem& it : g_group)
                if (GhostObject* go = FindObject(it.id)) { GizmoState gg = g; gg.startRot = it.rot; ApplyRotation(*go, gg, rad); }
        }
        g.value = deg;
    }
    else                                                       // scale
    {
        float f;
        if (g.axis == 3)
        {
            const float dx = (float)x - g.ox, dy = (float)y - g.oy;
            f = 1.0f + (sqrtf(dx * dx + dy * dy) - g.d0) / 100.0f;
        }
        else
        {
            float t;
            if (!AxisParam(ro, rd, g.startPos, g.dir, t)) return;
            f = t / g.t0;
        }
        if (g.snapping) f = SnapTo(f, g_gizSet.scaleStep);
        f = f < 0.01f ? 0.01f : (f > 100.0f ? 100.0f : f);

        auto apply = [&](GhostVec3 s0) { if (g.axis == 3) s0 = s0 * f; else if (g.axis == 0) s0.x *= f; else if (g.axis == 1) s0.y *= f; else s0.z *= f; return s0; };
        if (g.col) o->colliderSize = apply(g.startColSize);    // resize the collider
        else if (pScale) *pScale = apply(g.startScale);        // a particle system
        else
        {
            o->scale = apply(g.startScale);
            for (const GroupItem& it : g_group) if (GhostObject* go = FindObject(it.id)) go->scale = apply(it.scale);
        }
        g.value = f;
    }
}

static void OverlayGizmo(int selId)
{
    GizmoFrame f;
    if (!GizmoFrameOf(selId, f)) return;
    const GhostVec3 pos = f.pos;
    const float L = f.L;
    const int tool = f.tool;
    static const char* names[3] = { "X", "Y", "Z" };
    static const float kHot[4] = { 1.0f, 0.92f, 0.30f, 1.0f };
    const int act = g_gizmo.active ? g_gizmo.axis : g_gizmoHover;     // the axis that is hovered or being dragged

    auto colorOf = [&](int k, float* c)
        {
            const float* b = (k == act) ? kHot : kAxisCol[k];
            c[0] = b[0]; c[1] = b[1]; c[2] = b[2];
            c[3] = (g_gizmo.active && act >= 0 && act < 3 && k != act) ? 0.30f : 1.0f;   // while dragging, dim the others
        };

    for (int k = 0; k < 3; k++)
    {
        float c[4];
        colorOf(k, c);
        const float w = (k == act) ? 3.6f : 2.4f;

        if (tool == 2)                                          // rotate: one ring per axis
        {
            const GhostVec3 u = f.ax[(k + 1) % 3], v = f.ax[(k + 2) % 3];
            const float R = L * 0.8f;
            for (int s = 0; s < 48; s++)
            {
                const float a0 = (float)s / 48.0f * 2.0f * GHOST_PI, a1 = (float)(s + 1) / 48.0f * 2.0f * GHOST_PI;
                g_ov.Line(pos + (u * cosf(a0) + v * sinf(a0)) * R, pos + (u * cosf(a1) + v * sinf(a1)) * R, c, w, false);
            }
        }
        else
        {
            const GhostVec3 end = pos + f.ax[k] * L;
            if (tool == 1) g_ov.Arrow(pos, end, c, w, 14.0f, false);
            else
            {
                g_ov.Line(pos, end, c, w, false);
                if (tool == 3) g_ov.Point(end, (k == act) ? 13.0f : 10.0f, c);
            }
            g_ov.Label(pos + f.ax[k] * (L * 1.16f), names[k], c, 16.0f, 0.0f, 0.0f);
        }
    }

    if (tool == 3)                                              // the centre handle scales all three axes
    {
        const float white[4] = { 0.92f, 0.92f, 0.92f, 1.0f };
        g_ov.Point(pos, 16.0f, act == 3 ? kHot : white);
    }

    const float black[4] = { 0, 0, 0, 0.9f }, dot[4] = { 1.0f, 0.82f, 0.30f, 1.0f };
    g_ov.Point(pos, 10.0f, black);
    g_ov.Point(pos, 6.0f, dot);                                 // the origin

    if (g_gizmo.active)                                         // readout at the top of the viewport
    {
        const GizmoState& g = g_gizmo;
        const char* ax = (g.axis >= 0 && g.axis < 3) ? names[g.axis] : "";
        const char* sn = g.snapping ? "   (snap)" : "";
        char t[96], l[32];
        if (g.tool == 1) { FormatLength(g.value, l, (int)sizeof l); snprintf(t, sizeof t, "Move %s   %s%s", ax, l, sn); }
        else if (g.tool == 2) snprintf(t, sizeof t, "Rotate %s   %.1f deg%s", ax, g.value, sn);
        else if (g.axis == 3) snprintf(t, sizeof t, "Scale (uniform)   x%.2f%s", g.value, sn);
        else snprintf(t, sizeof t, "Scale %s   x%.2f%s", ax, g.value, sn);
        const float tw = GhostText_Measure(g_font, 20.0f, t);
        g_ov.TextPx(((float)g_ov.w - tw) * 0.5f, 14.0f, 20.0f, t, kHot);
    }
}





// Arrows between the box edges, one per axis, with the length as a label
static void OverlayDims(const GhostObject& o, const GhostVec3& eye)
{
    float byw;
    const GhostMat4 M = GhostObjectModel(o, BillYaw(o, eye, byw));
    const GhostVec3 mn = GhostObject_Min(o), mx = GhostObject_Max(o);
    GhostVec3 ax[3];
    for (int k = 0; k < 3; k++) ax[k] = GhostVec3(M.m[0][k], M.m[1][k], M.m[2][k]).Normalize();

    const float ext[3] = { fabsf((mx.x - mn.x) * o.scale.x), fabsf((mx.y - mn.y) * o.scale.y), fabsf((mx.z - mn.z) * o.scale.z) };
    const float big = fmaxf(ext[0], fmaxf(ext[1], ext[2]));
    const float off = fmaxf(big * 0.12f, 0.08f);

    const GhostVec3 ends[3] = { GhostVec3(mx.x, mn.y, mn.z), GhostVec3(mn.x, mx.y, mn.z), GhostVec3(mn.x, mn.y, mx.z) };
    const GhostVec3 offDir[3] = { (ax[1] + ax[2]) * -1.0f, (ax[0] + ax[2]) * -1.0f, (ax[0] + ax[1]) * -1.0f };
    const bool flat = (o.meshId == GHOST_MESH_QUAD);            // a sprite has no meaningful depth
    const float grey[4] = { 0.75f, 0.75f, 0.78f, 0.55f };

    for (int k = 0; k < 3; k++)
    {
        if (!((g_viewCfg.dimAxes >> k) & 1)) continue;
        if (flat && k == 2) continue;

        const GhostVec3 w0 = XformPoint(M, mn, 1.0f), w1 = XformPoint(M, ends[k], 1.0f);
        const GhostVec3 d = offDir[k].Normalize() * off;
        const GhostVec3 d0 = w0 + d, d1 = w1 + d;

        g_ov.Line(w0, d0 + d * 0.25f, grey, 1.0f, false);       // extension lines
        g_ov.Line(w1, d1 + d * 0.25f, grey, 1.0f, false);
        g_ov.Dimension(d0, d1, kAxisCol[k], 1.6f, 10.0f);

        char t[40];
        FormatLength(ext[k], t, (int)sizeof t);
        float rc[4];
        if (g_ov.Label((d0 + d1) * 0.5f, t, kAxisCol[k], 17.0f, 0.0f, -13.0f, rc))
        {
            LabelHit h = {};
            memcpy(h.rect, rc, sizeof rc);
            h.kind = 0; h.index = k; h.objectId = o.id; h.len = ext[k];
            g_labelHits.push_back(h);
        }
    }
}

static bool NearP(const GhostVec3& a, const GhostVec3& b) { return (a - b).Length() < 1e-4f; }

// A single polygon (the triangle, or a face of up to 64 triangles): label every boundary edge A, B, C...
static void OverlayFaceEdges(const GhostObject& o, const GhostVec3& eye)
{
    if (o.tileMap.enabled) return;
    int face = CurrentFace();
    if (face < 0 && GhostMesh_FaceCount(o.meshId) == 1) face = 0;
    if (face < 0 || face >= GhostMesh_FaceCount(o.meshId)) return;

    GhostMeshCPU cpu;
    if (!GhostGetMeshCPU(g_pipeline, o.meshId, &cpu)) return;
    const GhostFaceDef& fd = GhostMesh_Face(o.meshId, face);
    const unsigned int tris = fd.indexCount / 3;
    if (tris == 0 || tris > 64 || fd.firstIndex + fd.indexCount > cpu.indexCount) return;

    struct Edge { GhostVec3 a, b; bool dead; };
    Edge e[192];
    int n = 0, cn = 0;
    GhostVec3 centroid(0.0f, 0.0f, 0.0f);
    for (unsigned int t = 0; t < tris; t++)
    {
        GhostVec3 p[3];
        for (int k = 0; k < 3; k++)
        {
            const unsigned int vi = cpu.Index(fd.firstIndex + t * 3 + k);
            if (vi >= cpu.vertexCount) return;
            const GhostVertex& v = cpu.verts[vi];
            p[k] = GhostVec3(v.x, v.y, v.z);
            centroid = centroid + p[k];
            cn++;
        }
        for (int k = 0; k < 3; k++) { e[n].a = p[k]; e[n].b = p[(k + 1) % 3]; e[n].dead = false; n++; }
    }
    for (int i = 0; i < n; i++)                                 // an edge used twice is inside the polygon
        for (int j = i + 1; j < n; j++)
            if ((NearP(e[i].a, e[j].a) && NearP(e[i].b, e[j].b)) || (NearP(e[i].a, e[j].b) && NearP(e[i].b, e[j].a)))
                e[i].dead = e[j].dead = true;
    centroid = centroid * (1.0f / (float)cn);

    float byw;
    const GhostMat4 M = GhostObjectModel(o, BillYaw(o, eye, byw));
    const GhostVec3 wc = XformPoint(M, centroid, 1.0f);
    const float cy[4] = { 0.25f, 0.95f, 0.95f, 1.0f };
    int letter = 0;
    for (int i = 0; i < n && letter < 26; i++)
    {
        if (e[i].dead) continue;
        const GhostVec3 a = XformPoint(M, e[i].a, 1.0f), b = XformPoint(M, e[i].b, 1.0f);
        const bool sel = (g_selEdgeObj == o.id && g_selEdgeLetter == letter);
        const float yel[4] = { 1.0f, 0.92f, 0.30f, 1.0f };
        const float* col = sel ? yel : cy;
        g_ov.Line(a, b, col, sel ? 4.5f : 2.5f, false);

        const float len = (b - a).Length();
        const GhostVec3 mid = (a + b) * 0.5f;
        const GhostVec3 away = (mid - wc).Normalize() * fmaxf(len * 0.12f, 0.03f);
        char ls[32], label[56];
        FormatLength(len, ls, (int)sizeof ls);
        snprintf(label, sizeof label, "%c  %s", 'A' + letter, ls);

        float rc[4] = { 0.0f, 0.0f, -1.0f, -1.0f };                 // a negative size never matches a click
        const bool okL = g_ov.Label(mid + away, label, col, 16.0f, 0.0f, 0.0f, rc);
        const GhostOverlay::SP pa = g_ov.Proj(a), pb = g_ov.Proj(b);
        if (okL || (pa.ok && pb.ok))
        {
            LabelHit h = {};
            memcpy(h.rect, rc, sizeof rc);
            h.hasSeg = pa.ok && pb.ok;
            if (h.hasSeg) { h.seg[0] = pa.x; h.seg[1] = pa.y; h.seg[2] = pb.x; h.seg[3] = pb.y; }
            h.kind = 1; h.index = letter; h.objectId = o.id; h.len = len;
            g_labelHits.push_back(h);
        }
        letter++;
    }
}

static bool LabelClick(int x, int y)
{
    if (g_playState != GhostPlayState::Editing) return false;
    for (size_t i = g_labelHits.size(); i-- > 0; )
    {
        const LabelHit& h = g_labelHits[i];
        bool in = (float)x >= h.rect[0] && (float)x <= h.rect[0] + h.rect[2] &&
            (float)y >= h.rect[1] && (float)y <= h.rect[1] + h.rect[3];
        if (!in && h.hasSeg) in = PtSegDist((float)x, (float)y, h.seg[0], h.seg[1], h.seg[2], h.seg[3]) < 8.0f;
        if (!in) continue;

        g_dimClick.valid = true;
        g_dimClick.kind = h.kind; g_dimClick.index = h.index; g_dimClick.objectId = h.objectId; g_dimClick.len = h.len;
        if (h.kind == 1) { g_selEdgeObj = h.objectId; g_selEdgeLetter = h.index; }
        return true;
    }
    g_selEdgeLetter = -1;                                            // clicked elsewhere: no edge selected
    return false;
}


static void OverlaySpotCone(const GhostLight& l)
{
    const float d2r = GHOST_PI / 180.0f;
    const GhostVec3 dir = LightForward(l).Normalize();
    const GhostVec3 up = fabsf(dir.y) > 0.99f ? GhostVec3(0, 0, 1) : GhostVec3(0, 1, 0);
    const GhostVec3 r = up.Cross(dir).Normalize(), u = dir.Cross(r);
    const float len = fmaxf(l.range, 0.5f);
    const GhostVec3 c = l.position + dir * len;
    const float outerCol[4] = { 1.0f, 0.82f, 0.30f, 0.9f }, innerCol[4] = { 1.0f, 0.82f, 0.30f, 0.45f };
    auto ring = [&](float deg, const float* col, bool edges)
        {
            const float rad = tanf(fminf(deg, 80.0f) * d2r) * len;
            GhostVec3 prev = c + r * rad;
            for (int i = 1; i <= 32; i++)
            {
                const float a = (float)i / 32.0f * 2.0f * GHOST_PI;
                const GhostVec3 cur = c + (r * cosf(a) + u * sinf(a)) * rad;
                g_ov.Line(prev, cur, col, 1.5f, false);
                prev = cur;
            }
            if (edges)
                for (int k = 0; k < 4; k++)
                {
                    const float a = (float)k * GHOST_PI * 0.5f;
                    g_ov.Line(l.position, c + (r * cosf(a) + u * sinf(a)) * rad, col, 1.2f, false);
                }
        };
    ring(l.outerDeg, outerCol, true);
    ring(l.innerDeg, innerCol, false);
}

// ---- Utility volumes: box / sphere wireframes ----
static const float kVolColor[GHOST_VOL_COUNT][3] = { { 0.35f, 0.85f, 1.00f },{ 0.75f, 0.60f, 1.00f },{ 1.00f, 0.55f, 0.75f } };
static const char* kVolLabel[GHOST_VOL_COUNT] = { "Reflection Capture", "Fog Volume", "Post Process Volume" };

static void OverlayVolumeShape(const GhostVolume& v, const GhostMat4& M, float inset, const float* col, float width, bool depth)
{
    if (v.shape == GHOST_VOLSHAPE_SPHERE)
    {
        const float r = fmaxf(v.radius - inset, 0.05f);
        for (int axis = 0; axis < 3; axis++)                       // three great circles
        {
            GhostVec3 prev;
            for (int i = 0; i <= 48; i++)
            {
                const float a = (float)i / 48.0f * 2.0f * GHOST_PI;
                const float c = cosf(a) * r, s = sinf(a) * r;
                const GhostVec3 local = axis == 0 ? GhostVec3(0, c, s) : (axis == 1 ? GhostVec3(c, 0, s) : GhostVec3(c, s, 0));
                const GhostVec3 cur = XformPoint(M, local, 1.0f);
                if (i > 0) g_ov.Line(prev, cur, col, width, depth);
                prev = cur;
            }
        }
        return;
    }
    const GhostVec3 h(fmaxf(v.size.x * 0.5f - inset, 0.05f), fmaxf(v.size.y * 0.5f - inset, 0.05f), fmaxf(v.size.z * 0.5f - inset, 0.05f));
    GhostVec3 c[8];
    for (int i = 0; i < 8; i++)
        c[i] = XformPoint(M, GhostVec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z), 1.0f);
    static const int E[12][2] = { { 0,1 },{ 2,3 },{ 4,5 },{ 6,7 },{ 0,2 },{ 1,3 },{ 4,6 },{ 5,7 },{ 0,4 },{ 1,5 },{ 2,6 },{ 3,7 } };
    for (int e = 0; e < 12; e++) g_ov.Line(c[E[e][0]], c[E[e][1]], col, width, depth);
}

static void OverlayVolumes()
{
    for (const GhostVolume& v : g_scene.volumes)
    {
        const bool sel = IsSelected(v.id), hot = (v.id == g_hoverId);
        const float* b = kVolColor[v.type];
        const float alpha = !v.enabled ? 0.15f : (sel ? 0.95f : (hot ? 0.8f : 0.40f));
        const float col[4] = { b[0], b[1], b[2], alpha };
        const GhostMat4 M = GhostVolumeModel(v);
        OverlayVolumeShape(v, M, 0.0f, col, sel ? 2.2f : 1.3f, !sel);            // unselected: depth tested, selected: on top
        if (v.blend > 0.001f && (sel || hot))                                      // inner shape = where the blend starts
        {
            const float inner[4] = { b[0], b[1], b[2], alpha * 0.5f };
            OverlayVolumeShape(v, M, v.blend, inner, 1.0f, false);
        }
        if (sel)
        {
            char t[80];
            bool ok = true;
            if (v.type == GHOST_VOL_REFLECTION)
            {
                auto it = g_probeRt.find(v.id);
                ok = it != g_probeRt.end() && it->second.slot >= 0 && GhostProbe_Valid(g_probe, it->second.slot);
            }
            snprintf(t, sizeof t, "%s%s", kVolLabel[v.type], ok ? "" : "  (not captured)");
            g_ov.Label(v.position, t, col, 15.0f, 0.0f, 26.0f);
        }
    }
}

static void OverlayParticle(const GhostParticleParams& p)
{
    GhostVec3 ax[3];
    GhostParticles_Axes(p, ax);                                  // local axes in world space, already scaled
    const GhostVec3 c(p.px, p.py, p.pz);
    const float hx = p.boxX * 0.5f, hy = p.boxY * 0.5f, hz = p.boxZ * 0.5f;
    GhostVec3 q[8];
    for (int i = 0; i < 8; i++)
        q[i] = c + ax[0] * ((i & 1) ? hx : -hx) + ax[1] * ((i & 2) ? hy : -hy) + ax[2] * ((i & 4) ? hz : -hz);
    static const int E[12][2] = { { 0,1 },{ 2,3 },{ 4,5 },{ 6,7 },{ 0,2 },{ 1,3 },{ 4,6 },{ 5,7 },{ 0,4 },{ 1,5 },{ 2,6 },{ 3,7 } };
    const float col[4] = { 1.0f, 0.62f, 0.2f, 0.95f };
    for (int e = 0; e < 12; e++) g_ov.Line(q[E[e][0]], q[E[e][1]], col, 1.6f, false);

    const GhostVec3 dir = ax[0] * ((p.velMinX + p.velMaxX) * 0.5f) + ax[1] * ((p.velMinY + p.velMaxY) * 0.5f) + ax[2] * ((p.velMinZ + p.velMaxZ) * 0.5f);
    if (dir.Length() > 1e-3f) g_ov.Arrow(c, c + dir.Normalize(), col, 2.0f, 12.0f, false);
}
// Debug: every landscape cell's box. Green = drawn, amber = resident but out of view, red = wanted but waiting, grey = not streamed in
static void OverlayLandscapeCells()
{
    static const int E[12][2] = { { 0,1 },{ 2,3 },{ 4,5 },{ 6,7 },{ 0,2 },{ 1,3 },{ 4,6 },{ 5,7 },{ 0,4 },{ 1,5 },{ 2,6 },{ 3,7 } };
    for (const GhostObject& o : g_scene.objects)
    {
        if (!o.tileMap.enabled) continue;
        auto it = g_tileInst.find(o.id);
        const int state = it == g_tileInst.end() ? 0 : it->second.state;
        const bool drawn = it != g_tileInst.end() && it->second.visFrame == g_frameNo;
        float col[4] = { 0.55f, 0.55f, 0.60f, 0.55f };
        if (state == 2) { col[0] = 1.0f; col[1] = 0.30f; col[2] = 0.30f; col[3] = 0.9f; }
        else if (state == 1) { if (drawn) { col[0] = 0.35f; col[1] = 0.95f; col[2] = 0.45f; } else { col[0] = 1.0f; col[1] = 0.78f; col[2] = 0.25f; } col[3] = 0.9f; }
        GhostVec3 mn, mx;
        GhostTileMap_Box(o.tileMap, mn, mx);
        const GhostMat4 M = GhostObjectModel(o);
        GhostVec3 c[8];
        for (int i = 0; i < 8; i++)
            c[i] = XformPoint(M, GhostVec3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z), 1.0f);
        for (int e = 0; e < 12; e++) g_ov.Line(c[E[e][0]], c[E[e][1]], col, 2.0f, false);
    }
}

// The selection box (works in every mode) plus a preview of what it will select
static void OverlayMarquee(const GhostVec3& eye)
{
    if (!g_marquee || (abs(g_mqX1 - g_mqX0) <= 4 && abs(g_mqY1 - g_mqY0) <= 4)) return;

    std::vector<int> hits;
    MarqueeHits(hits);
    for (int id : hits) if (const GhostObject* o = FindObject(id)) OverlayBounds(*o, eye);

    const int   xs[4] = { g_mqX0, g_mqX1, g_mqX1, g_mqX0 }, ys[4] = { g_mqY0, g_mqY0, g_mqY1, g_mqY1 };
    const float dist = fmaxf(g_world.nearPlane * 3.0f, 1.0f);      // in front of the near plane, so it never clips
    GhostVec3 P[4];
    for (int k = 0; k < 4; k++)
    {
        GhostVec3 ro, rd;
        if (!ScreenRay(xs[k], ys[k], ro, rd)) return;
        P[k] = ro + rd * dist;
    }
    const float bc[4] = { 0.40f, 0.80f, 1.0f, 1.0f };
    for (int k = 0; k < 4; k++) { g_ov.Line(P[k], P[(k + 1) % 4], bc, 1.5f, false); g_ov.Point(P[k], 5.0f, bc); }
}


static void DrawViewportOverlay(const GhostMat4& view, const GhostMat4& proj, const GhostVec3& eye,
    unsigned int rw, unsigned int rh, bool editing)
{
    if (!g_pipeline) return;

    g_ov.Begin(view, proj, rw, rh, &g_font);
    g_labelHits.clear();

    const GhostViewSettings& s = g_viewCfg;

    if (editing)
    {
        OverlayVolumes();
        if (s.showGrid) OverlayGrid(eye);

        const int sel = g_scene.selectedId;
        if (sel != 0)
        {
            GhostObject* o = FindObject(sel);
            if (s.showSelection)
                if (const GhostLight* sl = FindLight(sel))
                    if (sl->type == GhostLightType::Spot) OverlaySpotCone(*sl);
            if (s.showSelection)
            {
                if (o) OverlayBounds(*o, eye);
                if (o && o->collider) OverlayCollider(*o);
                if (!(g_paintMode && o && o->tileMap.enabled)) OverlayGizmo(sel);    // no gizmo while painting a Tile Map
                if (!o) if (const GhostParticleDef* pd = GhostParticles_Find(sel)) OverlayParticle(pd->p);
            }
            if (o && s.showSelection)
            {
                if (o->locked)
                {
                    const float red[4] = { 1.0f, 0.45f, 0.35f, 1.0f };
                    g_ov.Label(o->position, "LOCKED", red, 15.0f, 0.0f, 26.0f);
                }
                if (o->role == GHOST_ROLE_MOVER || o->role == GHOST_ROLE_PROJECTILE || o->role == GHOST_ROLE_ENEMY)
                {
                    const GhostVec3 b = o->position + o->pathOffset;
                    const float c[4] = { 1.0f, 0.62f, 0.20f, 0.95f };
                    g_ov.Line(o->position, b, c, 2.0f, false);
                    g_ov.Point(b, 9.0f, c);
                    g_ov.Label(o->position, "A", c, 15.0f, 0.0f, -22.0f);
                    g_ov.Label(b, "B", c, 15.0f, 0.0f, -16.0f);
                }
                if (o->role == GHOST_ROLE_PORTAL_ENTRY && !o->link.empty())
                    for (const GhostObject& x : g_scene.objects)
                        if (x.role == GHOST_ROLE_PORTAL_EXIT && x.link == o->link)
                        {
                            const float c[4] = { 0.30f, 0.95f, 0.95f, 0.95f };
                            g_ov.Line(o->position, x.position, c, 2.0f, false);
                            g_ov.Label(x.position, "EXIT", c, 15.0f, 0.0f, -22.0f);
                        }
            }
            if (o && (s.showDims || s.tool == 4))
            {
                OverlayDims(*o, eye);
                if (s.edgeLabels) OverlayFaceEdges(*o, eye);
            }

        }
    }
    if (g_world.showCells) OverlayLandscapeCells();

    if (editing)
    {
        if (s.showSelection)
            for (int mid : g_multi) if (GhostObject* mo = FindObject(mid)) OverlayBounds(*mo, eye);   // rest of a group selection
        OverlayMarquee(eye);                                                                          // drag box + preview, every mode
    }
    if (editing && g_faceMode)
    {
        const float c[4] = { 0.30f, 0.95f, 0.95f, 1.0f };
        g_ov.TextPx(14.0f, (float)rh - 34.0f, 20.0f, "FACE PICK MODE  (press F to leave)", c);
    }


    if (editing && g_colMode)
    {
        const float rc[4] = { 1.0f, 0.35f, 0.35f, 1.0f };
        g_ov.TextPx(14.0f, (float)rh - 58.0f, 20.0f, "COLLIDER EDIT MODE  (H to leave. Move = offset, Rotate = angle, Scale = size)", rc);
    }


    if (editing && !g_multi.empty())
    {
        char t[32];
        snprintf(t, sizeof t, "%d selected", (int)g_multi.size() + 1);
        const float mc[4] = { 0.40f, 0.80f, 1.0f, 1.0f };
        g_ov.TextPx(14.0f, (float)rh - 82.0f, 20.0f, t, mc);
    }

    if (editing && g_paintMode) OverlayPaintBrush((int)rw, (int)rh);
    if (editing && GhostFoliage_Set().brush.active) OverlayFoliageBrush((int)rw, (int)rh);

    if (s.showFps)
    {
        char t[48];
        snprintf(t, sizeof t, "%.0f FPS   %.1f ms", g_fps, g_fps > 0.0f ? 1000.0f / g_fps : 0.0f);
        const float col[4] = { g_fps >= 55.0f ? 0.45f : 1.0f, g_fps >= 30.0f ? 0.95f : 0.35f, 0.35f, 1.0f };
        const float tw = GhostText_Measure(g_font, 18.0f, t);
        g_ov.TextPx((float)rw - tw - 14.0f, 46.0f, 18.0f, t, col);
        if (g_wstats.cells > 0)
        {
            const float wc[4] = { 0.75f, 1.0f, 0.80f, 1.0f };
            char w[200];
            snprintf(w, sizeof w, "land: %d cells  %d/%d resident  %d drawn  %dk tris  load %.1f ms%s",
                g_wstats.cells, g_wstats.resident, (int)GHOST_MAX_TILEMAPS, g_wstats.cellsDrawn, g_wstats.trisDrawn / 1000,
                g_wstats.lastLoadMs, g_wstats.starved > 0 ? "  SLOTS FULL" : "");
            g_ov.TextPx((float)rw - GhostText_Measure(g_font, 16.0f, w) - 14.0f, 108.0f, 16.0f, w, wc);
        }
        if (g_prof.valid)
        {
            const float gc[4] = { 0.80f, 0.90f, 1.0f, 1.0f };
            char a[96], b[160];
            snprintf(a, sizeof a, "GPU %.1f ms   CPU record %.1f ms", g_prof.total, g_prof.cpuMs);
            snprintf(b, sizeof b, "clr %.1f  shadow %.1f  scene %.1f  msaa %.1f  fx %.1f  tonemap %.1f  ui %.1f",
                g_prof.seg[1], g_prof.seg[2], g_prof.seg[3], g_prof.seg[4], g_prof.seg[5], g_prof.seg[6], g_prof.seg[7]);
            g_ov.TextPx((float)rw - GhostText_Measure(g_font, 16.0f, a) - 14.0f, 68.0f, 16.0f, a, gc);
            g_ov.TextPx((float)rw - GhostText_Measure(g_font, 16.0f, b) - 14.0f, 88.0f, 16.0f, b, gc);
        }
    }

    GhostOverlayBegin(g_pipeline);
    GhostDrawOverlay(g_rhi, g_pipeline, g_ov.depth, 0, 0);
    GhostDrawOverlay(g_rhi, g_pipeline, g_ov.top, 1, 0);
    GhostDrawOverlay(g_rhi, g_pipeline, g_ov.text, 1, g_font.textureId);
}

static void FillMaterialCB(GhostObjectCB& cb, const GhostMaterial& m)
{
    const float d2r = GHOST_PI / 180.0f;
    cb.alphaP[0] = (float)m.alphaMode; cb.alphaP[1] = (float)m.alphaSource; cb.alphaP[2] = m.alphaCutoff;
    cb.mapUV[0] = m.mapTiling[0]; cb.mapUV[1] = m.mapTiling[1]; cb.mapUV[2] = m.mapOffset[0]; cb.mapUV[3] = m.mapOffset[1];
    for (int k = 0; k < GHOST_MAP_COUNT; k++)
    {
        const GhostMapParams& p = m.maps[k];
        const bool ok = p.textureId > 0 && p.textureId < (int)g_scene.textures.size() && p.textureId < (int)GHOST_MAX_TEXTURES;
        cb.maps[k][0] = ok ? (float)p.textureId : 0.0f;
        cb.maps[k][1] = p.amount; cb.maps[k][2] = (float)p.channel; cb.maps[k][3] = (float)p.invert;
    }
    cb.surfA[0] = (float)m.twoSided; cb.surfA[1] = m.refractionIOR; cb.surfA[2] = m.reflectionRotDeg * d2r; cb.surfA[3] = m.anisotropy;
    cb.surfB[0] = m.sssColor[0]; cb.surfB[1] = m.sssColor[1]; cb.surfB[2] = m.sssColor[2]; cb.surfB[3] = m.sssAmount;
    cb.sssP[0] = m.sssTransmission; cb.sssP[1] = m.sssDensity; cb.sssP[2] = m.sssThickness; cb.sssP[3] = 0.0f;
    cb.wpoP[0] = m.wpoSway; cb.wpoP[1] = m.wpoSpeed; cb.wpoP[2] = m.wpoMid; cb.wpoP[3] = g_timeSec;
    cb.opacityP[0] = m.opacity;
}

struct TransDraw { int meshId; unsigned int slot, tex, first, count; float dist2; };
static std::vector<TransDraw> g_translucent;




static bool BuildPreviewCamera(int vp, GhostCamera& c)
{
    GhostObject* o = FindObject(g_vp[vp].solo);
    if (!o) return false;
    const GhostVec3 mn = GhostObject_Min(*o), mx = GhostObject_Max(*o);
    const GhostVec3 ctr(o->position.x + (mn.x + mx.x) * 0.5f * o->scale.x,
        o->position.y + (mn.y + mx.y) * 0.5f * o->scale.y,
        o->position.z + (mn.z + mx.z) * 0.5f * o->scale.z);
    const float ex = fabsf((mx.x - mn.x) * o->scale.x), ey = fabsf((mx.y - mn.y) * o->scale.y), ez = fabsf((mx.z - mn.z) * o->scale.z);
    const float radius = 0.5f * sqrtf(ex * ex + ey * ey + ez * ez);
    const float dist = fmaxf(radius * 2.4f, 0.5f) * g_vp[vp].pZoom;
    c = GhostCamera();
    c.fovDeg = 35.0f; c.resW = 640; c.resH = 480;                 // the preview always renders at 640 x 480
    c.yawDeg = g_vp[vp].pYaw; c.pitchDeg = g_vp[vp].pPitch;
    c.position = ctr - AnglesToDir(c.yawDeg, c.pitchDeg) * dist;
    return true;
}

// Studio lighting for the preview: no scene lights, no shadows
static void BuildPreviewSceneCB(GhostSceneCB& out)
{
    out = {};
    out.ambient[0] = out.ambient[1] = out.ambient[2] = 0.35f;
    for (unsigned int i = 0; i < GHOST_MAX_LIGHTS; i++) out.lights[i].shadow[0] = -1.0f;
    const float dir[2][3] = { { 0.40f, -0.60f, 0.70f },{ -0.74f, -0.25f, -0.62f } };
    const float inten[2] = { 1.1f, 0.45f };
    for (int i = 0; i < 2; i++)
    {
        GhostGPULight& g = out.lights[out.lightCount++];
        g.dirType[0] = dir[i][0]; g.dirType[1] = dir[i][1]; g.dirType[2] = dir[i][2]; g.dirType[3] = 0.0f;
        g.colorIntensity[0] = g.colorIntensity[1] = g.colorIntensity[2] = 1.0f; g.colorIntensity[3] = inten[i];
    }
    out.shadowGlobal[0] = 0.0f;
}

// ============================================================
//  World partition: frustum culling + terrain bands + LOD
// ============================================================
struct TileRun { unsigned int first, count; };

// World-space AABB of a cell (its local box, through the object transform)
static void TileWorldBox(const GhostObject& o, GhostVec3& wmin, GhostVec3& wmax)
{
    GhostVec3 mn, mx;
    GhostTileMap_Box(o.tileMap, mn, mx);
    const GhostMat4 M = GhostObjectModel(o);
    wmin = { 1e30f, 1e30f, 1e30f };
    wmax = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < 8; i++)
    {
        const GhostVec3 c = XformPoint(M, GhostVec3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z), 1.0f);
        wmin.x = fminf(wmin.x, c.x); wmin.y = fminf(wmin.y, c.y); wmin.z = fminf(wmin.z, c.z);
        wmax.x = fmaxf(wmax.x, c.x); wmax.y = fmaxf(wmax.y, c.y); wmax.z = fmaxf(wmax.z, c.z);
    }
}

// Floor maps are measured on the ground plane (flying high must not unload the ground), wall maps in 3D
static float TileBoxDistance(const GhostObject& o, const GhostVec3& wmin, const GhostVec3& wmax, const GhostVec3& p)
{
    const float dx = fmaxf(fmaxf(wmin.x - p.x, p.x - wmax.x), 0.0f);
    const float dz = fmaxf(fmaxf(wmin.z - p.z, p.z - wmax.z), 0.0f);
    const float dy = (o.tileMap.direction == 1) ? 0.0f : fmaxf(fmaxf(wmin.y - p.y, p.y - wmax.y), 0.0f);
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// Which index ranges of a cell's mesh to draw: visible bands of the chosen LOD, merged into runs, plus the skirt.
// false = the whole cell is out of view.
static bool TileBuildRuns(const GhostObject& o, TileInst& T, const GhostVec3& eye, const GhostFrustum& fr,
    bool doCull, std::vector<TileRun>& runs)
{
    runs.clear();
    if (T.nx <= 0 || T.ny <= 0 || T.bandCount <= 0 || (int)T.bandMin.size() != T.bandCount) return false;

    GhostVec3 wmin, wmax;
    TileWorldBox(o, wmin, wmax);

    // LOD with hysteresis (no flicker at the threshold)
    int lod = 0;
    if (g_world.lod)
    {
        const float dist = TileBoxDistance(o, wmin, wmax, eye);
        const float thr[3] = { 0.0f, g_world.lodDist1, g_world.lodDist2 };
        int cur = T.lodCur < 0 ? 0 : (T.lodCur > T.maxLod ? T.maxLod : T.lodCur);
        while (cur < T.maxLod && dist > thr[cur + 1] * 1.04f) cur++;
        while (cur > 0 && dist < thr[cur] * 0.96f) cur--;
        T.lodCur = cur;
        lod = cur;
    }
    else T.lodCur = 0;

    GhostVec3 U, V, N;
    TileAxes(o.tileMap.direction, U, V, N);
    const GhostMat4 M = GhostObjectModel(o);
    const float uA = T.u0, uB = T.u0 + (float)T.nx * T.cell;
    const int   B = T.bandRows, s = 1 << lod;
    bool any = false;
    unsigned int idxCount = 0;

    for (int b = 0; b < T.bandCount; b++)
    {
        const int r0 = b * B, r1 = (r0 + B < T.ny) ? r0 + B : T.ny;
        bool vis = true;
        if (doCull)
        {
            const float vA = T.v0 + (float)r0 * T.cell, vB = T.v0 + (float)r1 * T.cell;
            GhostVec3 c[8];
            for (int i = 0; i < 8; i++)
            {
                const float pu = (i & 1) ? uB : uA, pv = (i & 2) ? vB : vA, h = (i & 4) ? T.bandMax[b] : T.bandMin[b];
                c[i] = XformPoint(M, U * pu + V * pv + N * h, 1.0f);
            }
            vis = GhostFrustum_Box8(fr, c, 2.0f);
        }
        if (!vis) { g_wstats.bandsCulled++; continue; }
        g_wstats.bandsDrawn++;
        any = true;

        const unsigned int first = T.lodFirst[lod] + (unsigned int)(r0 / s) * (unsigned int)(T.nx / s) * 6u;
        const unsigned int count = (unsigned int)((r1 - r0) / s) * (unsigned int)(T.nx / s) * 6u;
        if (count == 0) continue;
        if (!runs.empty() && runs.back().first + runs.back().count == first) runs.back().count += count;
        else runs.push_back({ first, count });
        idxCount += count;
    }
    if (!any) return false;

   
    if (T.skirtCount[lod] > 0) { runs.push_back({ T.skirtFirst[lod], T.skirtCount[lod] }); idxCount += T.skirtCount[lod]; }

    g_wstats.trisDrawn += (int)(idxCount / 3u);
    return !runs.empty();
}

// Bounding sphere around the pivot (rotation-proof) with room for wind / displacement and animated models
static bool ObjectVisible(const GhostObject& o, const GhostFrustum& fr)
{
    const GhostVec3 mn = GhostObject_Min(o), mx = GhostObject_Max(o);
    const float ex = fmaxf(fabsf(mn.x), fabsf(mx.x)) * fabsf(o.scale.x);
    const float ey = fmaxf(fabsf(mn.y), fabsf(mx.y)) * fabsf(o.scale.y);
    const float ez = fmaxf(fabsf(mn.z), fabsf(mx.z)) * fabsf(o.scale.z);
    float r = sqrtf(ex * ex + ey * ey + ez * ez);
    float infl = 0.0f;
    const int n = GhostObject_FaceCount(o);
    for (int f = 0; f < n && f < GHOST_MAX_FACES; f++)
    {
        const GhostMaterial& m = o.faces[f];
        float w = fabsf(m.wpoSway) * 1.5f;
        if (m.maps[GHOST_MAP_WPO].textureId > 0) w += fabsf(m.maps[GHOST_MAP_WPO].amount);
        infl = fmaxf(infl, w);
    }
    r = r * 1.25f + infl + 0.5f;
    return GhostFrustum_Sphere(fr, o.position, r);
}



// The sky and every object, drawn with the given camera into whatever target is currently set.
// Used by the viewports and by Reflection Capture. slot = the next free draw slot (in / out).
static void DrawSceneColor(const GhostMat4& view, const GhostMat4& proj, const GhostVec3& eye,
    bool drawObjects, bool overlays, bool preview, int soloId, unsigned int& slot)
{
    const float d2r = GHOST_PI / 180.0f;

    for (const GhostModEntity& m : g_scene.mods)
    {
        if (preview || m.typeId != GHOST_MOD_SKY) continue;
        GhostSkyDraw draws[9];
        const int n = GhostSky_Build(m, g_scene.textures, g_skyScroll[0], g_skyScroll[1],
            g_cloudScroll[0], g_cloudScroll[1], g_timeSec, draws);
        for (int i = 0; i < n && slot < MAX_SCENE_OBJECTS; i++)
        {
            GhostObjectCB cb = {};
            cb.view = view;
            cb.proj = proj;
            memcpy(cb.sky, draws[i].sky, sizeof cb.sky);
            GhostUpdateObjectCB(g_pipeline, slot, &cb);
            GhostDrawSky(g_rhi, g_pipeline, slot, (unsigned int)draws[i].textureId);
            slot++;
        }
        break;                                       // one sky per scene
    }

    if (!drawObjects) return;
    GhostFrustum fr;
    GhostFrustum_FromVP(fr, proj * view);
    const bool cullOn = g_world.cull != 0 && !preview;
    g_translucent.clear();
    for (const GhostObject& o : g_scene.objects)
    {
        if (preview && o.id != soloId) continue;
        if (!o.visible && g_playState != GhostPlayState::Editing) continue;   // hidden in game
        if (cullOn && !o.tileMap.enabled && !ObjectVisible(o, fr)) { g_wstats.objectsCulled++; continue; }
        const int       faceCount = GhostObject_DrawFaces(o);
        const GhostMat4 model = GhostObjectModel(o);
        const bool      selected = overlays && IsSelected(o.id);
        const int       hl = (selected && o.id == g_scene.selectedId && !o.tileMap.enabled) ? CurrentFace() : -1;
        if (o.tileMap.enabled)                                 // Tile Map = one streamed landscape cell
        {
            auto ti = g_tileInst.find(o.id);
            if (ti == g_tileInst.end() || ti->second.slot < 0) continue;     // not streamed in
            TileInst& T = ti->second;

            static std::vector<TileRun> runs;
            if (!TileBuildRuns(o, T, eye, fr, cullOn, runs)) continue;       // the whole cell is out of view
            T.visFrame = g_frameNo;
            g_wstats.cellsDrawn++;

            const int layers = o.tileMap.layerCount > 0 ? o.tileMap.layerCount : 1;
            const int passes = (layers > 1 && T.splatA <= 0) ? 1 : layers;   // no weight map (yet): layer 0 only
            const int meshSlot = DrawMeshOf(o);
            for (int L = 0; L < passes && slot < MAX_SCENE_OBJECTS; L++)
            {
                if (L > 0 && L < 32 && (T.usedMask & (1u << L)) == 0) continue;   // nothing painted on this layer
                const GhostMaterial& m = o.faces[L];
                GhostObjectCB cb = {};
                cb.model = model;
                cb.view = view;
                cb.proj = proj;
                cb.color[0] = m.tint[0]; cb.color[1] = m.tint[1]; cb.color[2] = m.tint[2]; cb.color[3] = 1.0f;
                cb.useVertexColor = 0.0f;
                cb.pad[0] = selected ? 1.0f : 0.0f;
                cb.pad[2] = o.receiveShadows ? 0.0f : 1.0f;
                cb.uvParams[0] = m.tiling[0]; cb.uvParams[1] = m.tiling[1];
                cb.uvParams[2] = m.offset[0]; cb.uvParams[3] = m.offset[1];
                cb.mapParams[0] = (float)m.mapMode;
                cb.mapParams[1] = m.rotationDeg * d2r;
                cb.mapParams[2] = (float)m.wrap;
                cb.mapParams[3] = (float)m.filter;
                cb.pbr[0] = m.metallic;  cb.pbr[1] = m.roughness;  cb.pbr[2] = m.specular;  cb.pbr[3] = m.emissiveStrength;
                cb.pbr[4] = m.emissive[0];  cb.pbr[5] = m.emissive[1];  cb.pbr[6] = m.emissive[2];
                cb.pbr[7] = 1.0f;
                FillMaterialCB(cb, m);
                cb.alphaP[0] = 0.0f;                                            // layers are opaque: they blend through the weights
                cb.opacityP[1] = (float)ShadowLightSelector(o);
                cb.tileP[4] = (float)o.tileMap.gridX; cb.tileP[5] = (float)o.tileMap.gridY;
                cb.tileP[6] = (o.tileMap.layerCount == 0) ? 1.0f : 0.0f;
                cb.tileP[3] = o.tileMap.flatShade ? 1.0f : 0.0f;       // PS: tileP[0].w
                if (layers > 1 && T.splatA > 0) { cb.tileP[0] = (float)T.splatA; cb.tileP[1] = (float)T.splatB; cb.tileP[2] = (float)L; }
                GhostUpdateObjectCB(g_pipeline, slot, &cb);
                for (const TileRun& r : runs)                                   // one constant buffer slot, several index ranges
                    GhostDrawMesh(g_rhi, g_pipeline, meshSlot, slot, (unsigned int)m.textureId, r.first, r.count, false, L > 0);
                slot++;
            }
            continue;
        }

        if (o.meshId == GHOST_MESH_QUAD)                       // sprite actor: one billboard quad
        {
            if (slot >= MAX_SCENE_OBJECTS) continue;
            const GhostSprite& sp = o.sprite;
            int tex, cols, rows, frame;
            SpriteDrawInfo(o, tex, cols, rows, frame);
            float byw;

            GhostObjectCB cb = {};
            cb.model = GhostObjectModel(o, BillYaw(o, eye, byw));
            cb.view = view;
            cb.proj = proj;
            cb.color[0] = sp.tint[0]; cb.color[1] = sp.tint[1]; cb.color[2] = sp.tint[2]; cb.color[3] = 1.0f;
            cb.pad[0] = selected ? 1.0f : 0.0f;
            cb.pad[2] = o.receiveShadows ? 0.0f : 1.0f;
            cb.pad[1] = (float)((sp.alphaClip ? 1 : 0) | (sp.unlit ? 2 : 0) | 4);   // shader flags
            const int col = frame % cols, row = frame / cols;
            cb.uvParams[0] = (sp.flipX ? -1.0f : 1.0f) / (float)cols;
            cb.uvParams[1] = 1.0f / (float)rows;
            cb.uvParams[2] = ((float)col + 0.5f) / (float)cols - 0.5f;
            cb.uvParams[3] = ((float)row + 0.5f) / (float)rows - 0.5f;
            cb.mapParams[2] = 1.0f;                            // clamp
            cb.mapParams[3] = sp.pixelArt ? 1.0f : 0.0f;       // point / linear

            GhostUpdateObjectCB(g_pipeline, slot, &cb);
            GhostDrawMesh(g_rhi, g_pipeline, o.meshId, slot, (unsigned int)(tex > 0 ? tex : 0), 0, 0);
            slot++;
            continue;
        }
        int f = 0;
        while (f < faceCount && slot < MAX_SCENE_OBJECTS)
        {
            int g = f + 1;
            while (g < faceCount && GhostMaterial_Equal(o.faces[g], o.faces[f]) && ((g == hl) == (f == hl))) g++;

            const GhostMaterial& m = o.faces[f];
            const FaceSpan a = FaceSpanOf(o, f);
            const FaceSpan b = FaceSpanOf(o, g - 1);
            const unsigned int runCount = b.first + b.count - a.first;
            if (o.tileMap.enabled && runCount == 0) { f = g; continue; }      // a layer with no tiles draws nothing

            GhostObjectCB cb = {};
            cb.model = model;
            cb.view = view;
            cb.proj = proj;
            cb.color[0] = m.tint[0]; cb.color[1] = m.tint[1]; cb.color[2] = m.tint[2]; cb.color[3] = 1.0f;
            cb.useVertexColor = (o.tileMap.enabled ? (o.tileMap.layerCount == 0) : o.useVertexColor) ? 1.0f : 0.0f;
            cb.pad[0] = selected ? ((f == hl) ? 2.0f : 1.0f) : 0.0f;   // 1 = selected, 2 = selected face
            cb.pad[2] = o.receiveShadows ? 0.0f : 1.0f;
            cb.uvParams[0] = m.tiling[0]; cb.uvParams[1] = m.tiling[1];
            cb.uvParams[2] = m.offset[0]; cb.uvParams[3] = m.offset[1];
            cb.mapParams[0] = (float)m.mapMode;
            cb.mapParams[1] = m.rotationDeg * d2r;
            cb.mapParams[2] = (float)m.wrap;
            cb.mapParams[3] = (float)m.filter;
            cb.pbr[0] = m.metallic;  cb.pbr[1] = m.roughness;  cb.pbr[2] = m.specular;  cb.pbr[3] = m.emissiveStrength;
            cb.pbr[4] = m.emissive[0];  cb.pbr[5] = m.emissive[1];  cb.pbr[6] = m.emissive[2];
            cb.pbr[7] = 1.0f;                                  // this draw uses PBR
            FillMaterialCB(cb, m);
            cb.opacityP[1] = (float)ShadowLightSelector(o);          // Tile Map: which light's shadows apply (0 = all)
            GhostUpdateObjectCB(g_pipeline, slot, &cb);
            if (m.alphaMode == 2)                                  // translucent: drawn after all opaque work, back to front
            {
                const GhostVec3 dv = o.position - eye;
                g_translucent.push_back({ DrawMeshOf(o), slot, (unsigned int)m.textureId, a.first, runCount, dv.Dot(dv) });
            }
            else
                GhostDrawMesh(g_rhi, g_pipeline, DrawMeshOf(o), slot, (unsigned int)m.textureId, a.first, runCount);
            slot++;
            f = g;
        }
    }


    FoliageDraw(view, proj, eye, fr, cullOn, preview, slot);
    std::stable_sort(g_translucent.begin(), g_translucent.end(),
        [](const TransDraw& a, const TransDraw& b) { return a.dist2 > b.dist2; });
    for (const TransDraw& t : g_translucent)
        GhostDrawMesh(g_rhi, g_pipeline, t.meshId, t.slot, t.tex, t.first, t.count, true);
   
}

// ============================================================
//  Reflection Capture (Utilities > Reflection Capture)
// ============================================================
static void FreeAllProbes()
{
    for (auto& kv : g_probeRt) if (kv.second.slot >= 0) GhostProbe_ReleaseSlot(g_rhi, g_pipeline, g_probe, kv.second.slot);
    g_probeRt.clear();
}

static unsigned int ProbeSignature(const GhostVolume& v)        // everything that changes what a capture looks like
{
    unsigned int h = 2166136261u;
    auto mix = [&](float f) { unsigned int u; memcpy(&u, &f, 4); h ^= u; h *= 16777619u; };
    mix(v.position.x); mix(v.position.y); mix(v.position.z);
    mix(v.p[PROBE_RES]); mix(v.p[PROBE_NEAR]); mix(v.p[PROBE_FAR]); mix(v.p[PROBE_SOURCE]); mix(v.p[PROBE_ENV_YAW]);
    mix((float)v.tex[0]);
    return h;
}

static bool CaptureProbe(const GhostVolume& v, int slot)
{
    static const unsigned int kRes[3] = { 64, 128, 256 };
    int ri = (int)(v.p[PROBE_RES] + 0.5f);
    ri = ri < 0 ? 0 : (ri > 2 ? 2 : ri);
    const unsigned int res = kRes[ri];
    const int source = (int)(v.p[PROBE_SOURCE] + 0.5f);          // 0 scene, 1 sky only, 2 custom texture
    if (GhostProbe_EnsureSlot(g_rhi, g_pipeline, g_probe, slot, res) != GHOST_OK) return false;
    GhostProbe_SetValid(g_probe, slot, false);

    if (source == 2)                                              // a lat-long texture: no scene drawing at all
    {
        const int tex = v.tex[0];
        if (tex <= 0 || tex >= (int)g_scene.textures.size()) return false;
        g_rhi->BeginOffscreenFrame();
        for (int f = 0; f < 6; f++)
            GhostProbe_EquirectFace(g_rhi, g_pipeline, g_probe, slot, f, tex, v.p[PROBE_ENV_YAW] * (GHOST_PI / 180.0f));
        GhostProbe_Prefilter(g_rhi, g_probe, slot);
        g_rhi->EndOffscreenFrame();
        GhostProbe_SetValid(g_probe, slot, true);
        return true;
    }

    if (GhostProbe_EnsureScratch(g_rhi, g_probe, res, g_pipeline->sampleCount) != GHOST_OK) return false;

    static const GhostVec3 kDir[6] = { { 1, 0, 0 },{ -1, 0, 0 },{ 0, 1, 0 },{ 0, -1, 0 },{ 0, 0, 1 },{ 0, 0, -1 } };
    static const GhostVec3 kUp[6] = { { 0, 1, 0 },{ 0, 1, 0 },{ 0, 0, -1 },{ 0, 0, 1 },{ 0, 1, 0 },{ 0, 1, 0 } };   // the D3D cube face orientation
    const GhostVec3 eye = v.position;
    const GhostMat4 proj = GhostMat4::PerspectiveFovLH(GHOST_PI * 0.5f, 1.0f, v.p[PROBE_NEAR], v.p[PROBE_FAR]);
    const GhostColor clr = g_clearColor;

    GhostSceneCB cb;
    BuildSceneCB(g_scene, cb, g_timeSec);                                    // lights + ambient. No probes: they never see each other

    for (int f = 0; f < 6; f++)
    {
        g_rhi->BeginOffscreenFrame();                             // one submission per face, so draw slots and the scene buffer can be reused
        if (f == 0 && source == 0)                                // one shadow pass, centred on the probe, shared by all six faces
        {
            const float fitR = g_scene.shadowDistance * 0.5f;
            RenderShadowPass(cb, eye - GhostVec3(0, 0, 1) * fitR, GhostVec3(0, 0, 1), res, res);
            GhostUpdateSceneCB(g_pipeline, &cb);
        }
        GhostProbe_BeginFace(g_rhi, g_pipeline, g_probe, &clr, res);
        const GhostMat4 view = GhostMat4::LookAtLH(eye, eye + kDir[f], kUp[f]);
        unsigned int slotCB = 0;
        DrawSceneColor(view, proj, eye, source == 0, false, false, 0, slotCB);
        GhostProbe_EndFace(g_rhi, g_pipeline, g_probe, slot, f);
        g_rhi->EndOffscreenFrame();
    }

    g_rhi->BeginOffscreenFrame();
    GhostProbe_Prefilter(g_rhi, g_probe, slot);
    g_rhi->EndOffscreenFrame();
    GhostProbe_SetValid(g_probe, slot, true);
    return true;
}

// Between frames, before any viewport renders. One capture per tick at most.
static void UpdateProbes(float dt)
{
    if (!g_rhi || !g_pipeline || !g_probe.ready) return;
    for (auto& kv : g_probeRt) kv.second.seen = false;

    const bool busy = g_gizmo.active || g_dragging || ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    bool didOne = false;

    for (const GhostVolume& v : g_scene.volumes)
    {
        if (v.type != GHOST_VOL_REFLECTION || !v.enabled) continue;
        ProbeRt& r = g_probeRt[v.id];
        r.seen = true;
        if (r.slot < 0)
        {
            r.slot = GhostProbe_AllocSlot(g_probe);
            if (r.slot < 0) continue;                             // all 8 slots are taken: shown in Details > Status
            r.dirty = true; r.stable = 0.0f; r.sig = ProbeSignature(v);
        }

        const unsigned int sig = ProbeSignature(v);
        if (sig != r.sig) { r.sig = sig; r.dirty = true; r.stable = 0.0f; }

        const bool realtime = v.p[PROBE_MODE] > 0.5f;
        bool go = false;
        if (r.request) go = true;
        else if (realtime)
        {
            r.timer += dt;
            go = r.timer >= v.p[PROBE_INTERVAL] || !GhostProbe_Valid(g_probe, r.slot);
        }
        else if (r.dirty)
        {
            if (!busy) r.stable += dt;                            // baked: wait until you stop dragging / typing
            go = r.stable >= 0.35f;
        }

        if (go && !didOne)
        {
            CaptureProbe(v, r.slot);
            didOne = true;
            r.request = false; r.dirty = false; r.timer = 0.0f; r.stable = 0.0f;
        }
    }

    for (auto it = g_probeRt.begin(); it != g_probeRt.end(); )    // deleted or disabled volumes give their slot back
    {
        if (!it->second.seen)
        {
            if (it->second.slot >= 0) GhostProbe_ReleaseSlot(g_rhi, g_pipeline, g_probe, it->second.slot);
            it = g_probeRt.erase(it);
        }
        else ++it;
    }
}

// Valid probes into the scene buffer, lowest priority first (the shader blends them in this order, so the last one wins)
static void FillProbeCB(GhostSceneCB& cb)
{
    cb.probeInfo[0] = 0.0f;
    struct Item { const GhostVolume* v; int slot; };
    std::vector<Item> items;
    for (const GhostVolume& v : g_scene.volumes)
    {
        if (v.type != GHOST_VOL_REFLECTION || !v.enabled) continue;
        auto it = g_probeRt.find(v.id);
        if (it == g_probeRt.end() || !GhostProbe_Valid(g_probe, it->second.slot)) continue;
        items.push_back({ &v, it->second.slot });
    }
    auto size = [](const GhostVolume& v) { return v.shape == GHOST_VOLSHAPE_SPHERE ? v.radius * v.radius * v.radius * 4.19f : v.size.x * v.size.y * v.size.z; };
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b)
        { return a.v->priority != b.v->priority ? a.v->priority < b.v->priority : size(*a.v) > size(*b.v); });

    const float d2r = GHOST_PI / 180.0f;
    int n = 0;
    for (const Item& it : items)
    {
        if (n >= (int)GHOST_MAX_PROBES) break;
        const GhostVolume& v = *it.v;
        GhostGPUProbe& g = cb.probes[n++];
        const GhostMat4 inv = GhostMat4::RotationZ(-v.rotationDeg.z * d2r) * GhostMat4::RotationX(-v.rotationDeg.x * d2r)
            * GhostMat4::RotationY(-v.rotationDeg.y * d2r) * GhostMat4::Translation(-v.position.x, -v.position.y, -v.position.z);
        for (int c = 0; c < 4; c++) { g.row0[c] = inv.m[0][c]; g.row1[c] = inv.m[1][c]; g.row2[c] = inv.m[2][c]; }
        if (v.shape == GHOST_VOLSHAPE_SPHERE) { g.shape[0] = 1.0f; g.shape[1] = v.radius; g.shape[2] = g.shape[3] = 0.0f; }
        else { g.shape[0] = 0.0f; g.shape[1] = v.size.x * 0.5f; g.shape[2] = v.size.y * 0.5f; g.shape[3] = v.size.z * 0.5f; }
        g.params[0] = v.blend;
        g.params[1] = v.p[PROBE_INTENSITY];
        g.params[2] = (float)GhostProbe_Mips(g_probe, it.slot) - 1.0f;
        g.params[3] = v.p[PROBE_DIFFUSE];
        g.misc[0] = v.p[PROBE_PARALLAX];
        g.misc[1] = (float)it.slot;
    }
    cb.probeInfo[0] = (float)n;
}

// ============================================================
//  Volumetric fog: fills the fog part of the scene constant buffer
// ============================================================
static bool FogActive()
{
    if (g_scene.fog.enabled) return true;
    for (const GhostVolume& v : g_scene.volumes) if (v.type == GHOST_VOL_FOG && v.enabled) return true;
    return false;
}

// proj = the matrix the scene was drawn with (jittered under TAA), so the ray through each pixel matches the geometry
static void FillFogCB(GhostSceneCB& cb, const GhostVec3& eye, const GhostVec3& fwd, const GhostVec3& right,
    const GhostMat4& proj, unsigned int rw, unsigned int rh, bool animateNoise)
{
    const GhostFogSettings& f = g_scene.fog;
    const float d2r = GHOST_PI / 180.0f;
    auto lin = [](float c) { return powf(c < 0.0f ? 0.0f : c, 2.2f); };
    auto set4 = [](float* d, float a, float b, float c, float e) { d[0] = a; d[1] = b; d[2] = c; d[3] = e; };

    const GhostVec3 up = fwd.Cross(right);
    set4(cb.fogCam[0], eye.x, eye.y, eye.z, 0.0f);
    set4(cb.fogCam[1], right.x, right.y, right.z, 0.0f);
    set4(cb.fogCam[2], up.x, up.y, up.z, 0.0f);
    set4(cb.fogCam[3], fwd.x, fwd.y, fwd.z, 0.0f);
    set4(cb.fogCam[4], proj.m[0][0], proj.m[1][1], proj.m[0][2], proj.m[1][2]);
    set4(cb.fogCam[5], proj.m[2][2], proj.m[2][3], (float)rw, (float)rh);

    set4(cb.fogA, f.enabled ? 1.0f : 0.0f, f.density, f.heightBase, f.heightFalloff);
    set4(cb.fogB, lin(f.color[0]), lin(f.color[1]), lin(f.color[2]), f.brightness);
    set4(cb.fogC, f.startDistance, f.maxDistance, f.skyFog, f.anisotropy);
    set4(cb.fogD, f.volLight ? 1.0f : 0.0f, f.followAmbient, (float)f.steps, animateNoise ? (float)(g_frameNo % 64u) * 5.588238f : 0.0f);
    const int envTex = (f.envTexture > 0 && f.envTexture < (int)g_scene.textures.size()) ? f.envTexture : 0;
    set4(cb.fogE, (float)f.envMode, f.envAmount, (float)envTex, f.envYaw * d2r);
    const int nTex = (f.noiseTexture > 0 && f.noiseTexture < (int)g_scene.textures.size()) ? f.noiseTexture : 0;
    set4(cb.fogF, (float)nTex, f.noiseAmount, f.noiseScale, 0.0f);
    set4(cb.fogG, fmodf(f.scrollX * g_timeSec * f.noiseScale, 1.0f), fmodf(f.scrollZ * g_timeSec * f.noiseScale, 1.0f), 0.0f, 0.0f);

    // god rays: the first 4 lights with a strength > 0 (Light.shadow.w carries it to the shader)
    int volLights = 0;
    for (size_t i = 0; i < g_scene.lights.size() && i < cb.lightCount; i++)
    {
        float k = 0.0f;
        if (f.volLight && volLights < 4 && g_scene.lights[i].volumetric > 0.001f && g_scene.lights[i].celestialFade > 0.002f) { k = g_scene.lights[i].volumetric * f.volIntensity; volLights++; }
        cb.lights[i].shadow[3] = k;
    }

    int n = 0;
    for (const GhostVolume& v : g_scene.volumes)
    {
        if (v.type != GHOST_VOL_FOG || !v.enabled) continue;
        if (n >= (int)GHOST_MAX_FOGVOLS) break;
        GhostGPUFogVol& g = cb.fogVols[n++];
        const GhostMat4 inv = GhostMat4::RotationZ(-v.rotationDeg.z * d2r) * GhostMat4::RotationX(-v.rotationDeg.x * d2r)
            * GhostMat4::RotationY(-v.rotationDeg.y * d2r) * GhostMat4::Translation(-v.position.x, -v.position.y, -v.position.z);
        for (int c = 0; c < 4; c++) { g.row0[c] = inv.m[0][c]; g.row1[c] = inv.m[1][c]; g.row2[c] = inv.m[2][c]; }
        if (v.shape == GHOST_VOLSHAPE_SPHERE) set4(g.shape, 1.0f, v.radius, 0.0f, 0.0f);
        else set4(g.shape, 0.0f, v.size.x * 0.5f, v.size.y * 0.5f, v.size.z * 0.5f);
        set4(g.dens, v.p[FOGV_DENSITY], v.blend, v.p[FOGV_GRADIENT], 0.0f);
        set4(g.color, lin(v.p[FOGV_R]), lin(v.p[FOGV_G]), lin(v.p[FOGV_B]), v.p[FOGV_LIGHT_SCATTER]);
        const int vt = (v.tex[0] > 0 && v.tex[0] < (int)g_scene.textures.size()) ? v.tex[0] : 0;
        set4(g.noise, (float)vt, v.p[FOGV_NOISE_AMOUNT], v.p[FOGV_NOISE_SCALE], 0.0f);
        set4(g.scroll, fmodf(v.p[FOGV_SCROLL_X] * g_timeSec * v.p[FOGV_NOISE_SCALE], 1.0f),
            fmodf(v.p[FOGV_SCROLL_Z] * g_timeSec * v.p[FOGV_NOISE_SCALE], 1.0f), 0.0f, 0.0f);
    }
    cb.fogG[3] = (float)n;
}

// ============================================================
//  SSR: previous-frame camera for the ray march in PSMain
// ============================================================
static void FillSsrCB(GhostSceneCB& cb, int vp, const GhostPostSettings& s)
{
    cb.ssrA[0] = 0.0f;                                         // 0 = off (a first frame, or no history yet)
    if (!g_post.ssrReady || !s.ssrEnabled || !GhostPost_SSRHistoryValid(g_post, vp)) return;
    const float* pv = g_post.ssrPrevView[vp];
    const float* pp = g_post.ssrPrevProj[vp];
    cb.ssrA[0] = 1.0f; cb.ssrA[1] = s.ssrMaxDistance; cb.ssrA[2] = s.ssrThickness; cb.ssrA[3] = (float)s.ssrSteps;
    cb.ssrB[0] = s.ssrIntensity; cb.ssrB[1] = s.ssrMaxRoughness; cb.ssrB[2] = s.ssrEdgeFade;
    cb.ssrB[3] = (float)(g_frameNo % 64u) * 5.588238f;        // moves the step jitter every frame (TAA averages it)
    cb.ssrC[0] = pp[0]; cb.ssrC[1] = pp[1]; cb.ssrC[2] = pp[2]; cb.ssrC[3] = pp[3];
    cb.ssrD[0] = pp[4]; cb.ssrD[1] = pp[5]; cb.ssrD[2] = pp[6]; cb.ssrD[3] = pp[7];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) cb.ssrView[r][c] = pv[r * 4 + c];
}

// ============================================================
//  Light shafts: one job per Directional Light with God Rays Strength > 0
// ============================================================
static int BuildShaftJobs(const GhostMat4& view, const GhostMat4& proj, const GhostPostSettings& s, GhostShaftJob* out)
{
    int n = 0;
    for (const GhostLight& l : g_scene.lights)
    {
        if (n >= (int)GHOST_MAX_SHAFT_LIGHTS) break;
        if (l.type != GhostLightType::Directional || l.volumetric <= 0.001f || l.celestialFade <= 0.002f) continue;

        const GhostVec3 toLight = LightForward(l).Normalize() * -1.0f;      // the light travels along its forward, the sun is the other way
        const GhostVec3 v = XformPoint(view, toLight, 0.0f);                // direction in view space
        if (v.z < 0.05f) continue;                                          // sun is behind the camera
        const float u = (proj.m[0][0] * v.x / v.z) * 0.5f + 0.5f;
        const float w = 0.5f - (proj.m[1][1] * v.y / v.z) * 0.5f;

        const float ox = fmaxf(fabsf(u - 0.5f) - 0.5f, 0.0f), oy = fmaxf(fabsf(w - 0.5f) - 0.5f, 0.0f);
        const float edge = 1.0f - fminf(fmaxf(ox, oy) / 0.75f, 1.0f);       // fade out as the sun leaves the screen
        const float k = s.shaftIntensity * l.volumetric * l.celestialFade * edge;
        if (k <= 0.002f) continue;

        GhostShaftJob& j = out[n++];
        j.sunU = u; j.sunV = w;
        j.color[0] = l.color[0] * k; j.color[1] = l.color[1] * k; j.color[2] = l.color[2] * k;
        j.length = s.shaftLength;       j.decay = s.shaftDecay;
        j.darkness = s.shaftDarkness;   j.depthRange = s.shaftDepthRange;
        j.threshold = s.shaftThreshold; j.maxBright = s.shaftMaxBright;
        j.occlusion = s.shaftOcclusion; j.steps = s.shaftSteps;
    }
    return n;
}
// ============================================================
//  Post Process Volumes: the post settings the camera at `eye` sees
// ============================================================
// 0..1: how far inside the volume the point is (soft edge = Blend Distance, like Fog Volumes). Unbound = 1 everywhere.
static float PostVolumeWeight(const GhostVolume& v, const GhostVec3& p)
{
    if (v.p[PPV_UNBOUND] > 0.5f) return 1.0f;
    const float d2r = GHOST_PI / 180.0f;
    const GhostMat4 inv = GhostMat4::RotationZ(-v.rotationDeg.z * d2r) * GhostMat4::RotationX(-v.rotationDeg.x * d2r)
        * GhostMat4::RotationY(-v.rotationDeg.y * d2r) * GhostMat4::Translation(-v.position.x, -v.position.y, -v.position.z);
    const GhostVec3 lp = XformPoint(inv, p, 1.0f);
    const float blend = fmaxf(v.blend, 0.001f);
    float d;
    if (v.shape == GHOST_VOLSHAPE_SPHERE) d = v.radius - lp.Length();
    else
    {
        const float dx = v.size.x * 0.5f - fabsf(lp.x), dy = v.size.y * 0.5f - fabsf(lp.y), dz = v.size.z * 0.5f - fabsf(lp.z);
        d = fminf(dx, fminf(dy, dz));
    }
    const float w = d / blend;
    return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
}

static GhostPostSettings EffectivePost(const GhostVec3& eye)
{
    GhostPostSettings s = g_scene.post;

    struct Item { const GhostVolume* v; float w, size; };
    static std::vector<Item> items;
    items.clear();
    for (const GhostVolume& v : g_scene.volumes)
    {
        if (v.type != GHOST_VOL_POST || !v.enabled) continue;
        if (((int)(v.p[PPV_MASK] + 0.5f)) == 0) continue;
        const float w = PostVolumeWeight(v, eye) * v.p[PPV_WEIGHT];
        if (w <= 0.0005f) continue;
        const float size = v.p[PPV_UNBOUND] > 0.5f ? 1e30f
            : (v.shape == GHOST_VOLSHAPE_SPHERE ? v.radius * v.radius * v.radius * 4.19f : v.size.x * v.size.y * v.size.z);
        items.push_back({ &v, w, size });
    }
    if (items.empty()) return s;

    // lowest priority first, so the highest wins; equal priority: the bigger volume first, so the smaller one wins
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b)
        { return a.v->priority != b.v->priority ? a.v->priority < b.v->priority : a.size > b.size; });

    auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
    for (const Item& it : items)
    {
        const float* p = it.v->p;
        const float  w = it.w;
        const int    mask = (int)(p[PPV_MASK] + 0.5f);

        if (mask & PPVO_EXPOSURE) s.exposureEV = mix(s.exposureEV, p[PPV_EXPOSURE], w);
        if (mask & PPVO_BLOOM)
        {
            s.bloomIntensity = mix(s.bloomEnabled ? s.bloomIntensity : 0.0f, p[PPV_BLOOM_INTENSITY], w);   // "off" counts as 0
            s.bloomThreshold = mix(s.bloomThreshold, p[PPV_BLOOM_THRESHOLD], w);
            s.bloomEnabled = s.bloomIntensity > 0.001f ? 1 : 0;
        }
        if (mask & PPVO_SSAO)
        {
            s.ssaoStrength = mix(s.ssaoEnabled ? s.ssaoStrength : 0.0f, p[PPV_SSAO_STRENGTH], w);
            s.ssaoRadius = mix(s.ssaoRadius, p[PPV_SSAO_RADIUS], w);
            s.ssaoEnabled = s.ssaoStrength > 0.001f ? 1 : 0;
        }
        if (mask & PPVO_SSGI)
        {
            s.ssgiIntensity = mix(s.ssgiEnabled ? s.ssgiIntensity : 0.0f, p[PPV_SSGI_INTENSITY], w);
            s.ssgiEnabled = s.ssgiIntensity > 0.001f ? 1 : 0;
        }
        if (mask & PPVO_SSR)
        {
            s.ssrIntensity = mix(s.ssrEnabled ? s.ssrIntensity : 0.0f, p[PPV_SSR_INTENSITY], w);
            s.ssrEnabled = s.ssrIntensity > 0.001f ? 1 : 0;
        }
        if (mask & PPVO_SHAFTS)
        {
            s.shaftIntensity = mix(s.shaftEnabled ? s.shaftIntensity : 0.0f, p[PPV_SHAFT_INTENSITY], w);
            s.shaftEnabled = s.shaftIntensity > 0.001f ? 1 : 0;
        }
        if (mask & PPVO_TONE)
        {
            if (w >= 0.5f) s.toneMode = (int)(p[PPV_TONE_MODE] + 0.5f);     // a curve can't be blended: it switches at half weight
            s.whitePoint = mix(s.whitePoint, p[PPV_WHITE_POINT], w);
        }
    }
    GhostPostSettings_Clamp(s);
    return s;
}

// ============================================================
//  One viewport = one surface + one camera. The GPU is idle between viewports
//  (EndFrame waits), so the constant buffer slots are simply reused.
// ============================================================
static void RenderViewport(int vp)
{
    GhostCamera* cam = ViewportCamera(vp);
    static GhostCamera s_previewCam;
    const bool preview = g_vp[vp].preview;
    if (preview) cam = BuildPreviewCamera(vp, s_previewCam) ? &s_previewCam : nullptr;
    if (!cam || !g_vp[vp].visible) return;


    if (!g_rhi->SetCurrentSurface(vp)) return;

    const unsigned int w = g_rhi->GetSurfaceWidth();
    const unsigned int h = g_rhi->GetSurfaceHeight();
    if (w == 0 || h == 0) return;

    const bool  mainView = (vp == 0);
    const bool overlays = mainView && g_playState == GhostPlayState::Editing;   // no icons or selection while playing
    const float d2r = GHOST_PI / 180.0f;
    const GhostColor clr = preview ? GhostColor{ 0.15f, 0.16f, 0.19f, 1.0f } : g_clearColor;
    // ---- Render resolution: the global main-view setting wins on viewport 0, otherwise the camera's own ----
    int wantW = cam->resW, wantH = cam->resH;
    if (mainView && g_video.resW > 0 && g_video.resH > 0) { wantW = g_video.resW; wantH = g_video.resH; }
    const bool         custom = (wantW > 0 && wantH > 0);
    const unsigned int rw = custom ? (unsigned int)wantW : w;
    const unsigned int rh = custom ? (unsigned int)wantH : h;

    // Where the image sits in the window
    float fx = 0.0f, fy = 0.0f, fw = (float)w, fh = (float)h;
    if (custom && g_video.fit != GHOST_FIT_STRETCH)
    {
        float s = fminf((float)w / (float)rw, (float)h / (float)rh);
        if (g_video.fit == GHOST_FIT_INTEGER && s >= 1.0f) s = floorf(s);
        fw = fmaxf(floorf((float)rw * s), 1.0f);
        fh = fmaxf(floorf((float)rh * s), 1.0f);
        fx = floorf(((float)w - fw) * 0.5f);
        fy = floorf(((float)h - fh) * 0.5f);
    }

    // GPU targets follow the render size (between frames the GPU is idle)
    if (custom) { if (GhostEnsureOffscreen(g_rhi, g_pipeline, vp, rw, rh) != GHOST_OK) return; }
    else        GhostReleaseOffscreen(g_pipeline, vp);
    if (GhostEnsureDepth(g_rhi, g_pipeline, vp, rw, rh) != GHOST_OK) return;
    // MSAA targets follow the render size (freed again when MSAA is off)
    const bool msaa = g_msaa.active > 1;
    if (msaa) { if (GhostMSAA_Ensure(g_rhi, g_msaa, vp, rw, rh) != GHOST_OK) return; }
    else      GhostMSAA_Release(g_msaa, vp);
    // TAA targets follow the render size (freed again when TAA is off)
    bool taa = g_taaOn && !g_smaaOn && !g_fxaaOn && !msaa && g_taa.ready && !preview;
    if (taa && GhostTAA_Ensure(g_rhi, g_taa, vp, rw, rh) != GHOST_OK) taa = false;
    if (g_lastRW[vp] != rw || g_lastRH[vp] != rh)      // resolution changed: the old TAA history is the wrong size/content
    {
        if (taa) GhostTAA_ResetHistory(g_taa, vp);
        g_lastRW[vp] = rw; g_lastRH[vp] = rh;
    }
    if (!taa) GhostTAA_Release(g_taa, vp);
    bool smaa = g_smaaOn && !msaa && !taa && g_smaa.ready && !preview;
    if (smaa && GhostSMAA_Ensure(g_rhi, g_smaa, vp, rw, rh) != GHOST_OK) smaa = false;
    if (!smaa) GhostSMAA_Release(g_smaa, vp);
    bool fxaa = g_fxaaOn && !msaa && !taa && !smaa && g_fxaa.ready && !preview;
    if (fxaa && GhostFXAA_Ensure(g_rhi, g_fxaa, vp, rw, rh) != GHOST_OK) fxaa = false;
    if (!fxaa) GhostFXAA_Release(g_fxaa, vp);
    g_fxaa.quality = g_fxaaQuality;


    if (GhostPost_Ensure(g_rhi, g_post, vp, rw, rh) != GHOST_OK) return;    // 1x HDR scene color for this viewport
    const GhostPostSettings postFx = preview ? GhostPostSettings() : EffectivePost(cam->position);   // Post Process Volumes
    const bool ssrWant = !preview && g_post.ssrReady && postFx.ssrEnabled;
    GhostPost_SSRUpdateTargets(g_rhi, g_post, vp, ssrWant, rw, rh);        // history exists only while SSR is on

    float jx = 0.0f, jy = 0.0f;
    // ---- Matrices ----
    GhostVec3 eye = cam->position;
    GhostVec3 forward = AnglesToDir(cam->yawDeg, cam->pitchDeg);
    GhostVec3 worldUp = { 0.0f, 1.0f, 0.0f };
    GhostVec3 right = worldUp.Cross(forward).Normalize();
    GhostMat4 view = GhostMat4::LookAtLH(eye, eye + forward, worldUp);
    const GhostMat4 projClean = GhostMat4::PerspectiveFovLH(cam->fovDeg * d2r, (float)rw / (float)rh, g_world.nearPlane, g_world.farPlane);
    GhostMat4 proj = projClean;                       // what gets drawn with: jittered while TAA is on
    if (taa)
    {
        GhostTAA_NextJitter(g_taa, vp, g_taaSpeed, jx, jy);
        proj.m[0][2] += jx * 2.0f / (float)rw;        // shifts clip x by jx pixels (clip.w = view z)
        proj.m[1][2] += jy * 2.0f / (float)rh;
    }

    if (mainView)                                   // picking and dragging use the main view only
    {
        g_view = view;
        g_proj = projClean;                           // picking never sees the jitter
        g_mainEye = eye;
        g_camR = right;
        g_camU = forward.Cross(right);
        g_camF = forward;
        g_haveCam = true;
        g_mainRW = (int)rw;  g_mainRH = (int)rh;
        g_fitX = fx;  g_fitY = fy;  g_fitW = fw;  g_fitH = fh;
    }

    
      // ---- Begin frame ----
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GhostGetDSV(g_pipeline, vp);
    if (custom)
    {
        const GhostColor bars = { 0.06f, 0.06f, 0.07f, 1.0f };
        g_rhi->BeginFrame(&bars, dsv);                          // window (the bars) + depth. The offscreen target is used after the scene
    }
    else
    {
        g_rhi->BeginFrame(&clr, dsv);
    }
    GhostProf_Begin(g_prof, mainView && !preview);
    GhostProf_Mark(g_rhi, g_prof, GP_BEGIN);

    // The scene itself draws into an HDR target: multisampled, TAA, or the plain 1x one
    if (msaa)      GhostMSAA_Begin(g_rhi, g_pipeline, g_msaa, vp, &clr, rw, rh);
    else if (taa)  GhostTAA_Begin(g_rhi, g_pipeline, g_taa, vp, &clr, rw, rh);
    else           GhostPost_BeginScene(g_rhi, g_pipeline, g_post, vp, &clr, dsv, rw, rh);
    GhostSceneCB sceneCB;
    GhostPost_SSRBind(g_rhi, g_pipeline, g_post, vp);                      // t12 / t13 = this viewport's history (null if none)
    if (preview) BuildPreviewSceneCB(sceneCB);
    else
    {
        BuildSceneCB(g_scene, sceneCB,g_timeSec);
        FillProbeCB(sceneCB);
        FillFogCB(sceneCB, eye, forward, right, proj, rw, rh, taa);
        FillSsrCB(sceneCB, vp, postFx);
        GhostProf_Mark(g_rhi, g_prof, GP_PRESHADOW);
        RenderShadowPass(sceneCB, eye, forward, rw, rh);
        GhostProf_Mark(g_rhi, g_prof, GP_SHADOW);
    }
    GhostUpdateSceneCB(g_pipeline, &sceneCB);

    // ---- Draw scene objects (one draw per run of faces that share a material) ----
    // ---- Sky: drawn first, everything else draws over it ----
    unsigned int slot = 0;


DrawSceneColor(view, proj, eye, true, overlays, preview, preview ? g_vp[vp].solo : 0, slot);


    // ---- Particles: after opaque + translucent objects, before the editor icons ----
    if (!preview)
    {
        static std::vector<GhostParticleDraw> pdraws;
        GhostParticles_BuildDraws(eye, pdraws, GHOST_PFX_MAX_DRAWS);
        const GhostVec3 pUp = forward.Cross(right);
        for (const GhostParticleDraw& d : pdraws)
        {
            if (slot >= MAX_SCENE_OBJECTS + GHOST_PFX_MAX_DRAWS) break;
            const bool quad = (d.meshId == GHOST_MESH_QUAD);
            GhostObjectCB cb = {};
            cb.model = GhostParticles_Model(d, right, pUp, forward);
            cb.view = view;
            cb.proj = proj;
            cb.color[0] = d.r; cb.color[1] = d.g; cb.color[2] = d.b; cb.color[3] = 1.0f;
            cb.pad[1] = (float)((d.alphaClip ? 1 : 0) | (d.unlit ? 2 : 0) | (quad ? 4 : 0));
            cb.pad[2] = 1.0f;                                    // particles don't receive shadows
            cb.uvParams[0] = d.uvScaleX; cb.uvParams[1] = d.uvScaleY;
            cb.uvParams[2] = d.uvOffX;   cb.uvParams[3] = d.uvOffY;
            cb.mapParams[2] = quad ? 1.0f : 0.0f;                // sprites clamp
            cb.mapParams[3] = d.pixelArt ? 1.0f : 0.0f;
            GhostUpdateObjectCB(g_pipeline, slot, &cb);
            GhostDrawMesh(g_rhi, g_pipeline, d.meshId, slot, (unsigned int)d.textureId, 0, 0, false, d.additive != 0);
            slot++;
        }
    }
    GhostProf_Mark(g_rhi, g_prof, GP_SCENE);
    if (!msaa) GhostProf_Mark(g_rhi, g_prof, GP_RESOLVE);

    // ---- End of the HDR scene: bring it down to the 1x HDR target ----
    {
        ID3D12Resource* hdrRes = GhostPost_Resource(g_post, vp);
        const bool fogOn = !preview && g_pipeline->fogPso && FogActive();
        const bool aoOn = !preview && g_post.ssaoReady && postFx.ssaoEnabled;
        const bool giOn = !preview && g_post.ssgiReady && postFx.ssgiEnabled;
        const float proj6[6] = { proj.m[0][0], proj.m[1][1], proj.m[0][2], proj.m[1][2], proj.m[2][2], proj.m[2][3] };   // the matrix the scene was drawn with (jittered under TAA)
        const float aoNoise = taa ? (float)(g_frameNo % 64u) * 5.588238f : 0.0f;
        const float giNoise = (float)(g_frameNo % 64u) * 5.588238f;                // GI noise changes every frame in every AA mode: the temporal pass averages it

        GhostSsgiFrame gf = {};
        {
            const GhostVec3 camUp2 = forward.Cross(right);
            gf.eye[0] = eye.x;       gf.eye[1] = eye.y;       gf.eye[2] = eye.z;
            gf.right[0] = right.x;   gf.right[1] = right.y;   gf.right[2] = right.z;
            gf.up[0] = camUp2.x;     gf.up[1] = camUp2.y;     gf.up[2] = camUp2.z;
            gf.fwd[0] = forward.x;   gf.fwd[1] = forward.y;   gf.fwd[2] = forward.z;
            const GhostMat4 giVP = projClean * view;
            memcpy(gf.vp, giVP.m, 16 * sizeof(float));
        }
        GhostShaftJob shaftJobs[GHOST_MAX_SHAFT_LIGHTS];
        const int  shaftN = (!preview && g_shafts.ready && postFx.shaftEnabled) ? BuildShaftJobs(view, projClean, postFx, shaftJobs) : 0;
        const bool shaftOn = shaftN > 0;

        if (msaa)
        {
            GhostMSAA_End(g_rhi, g_pipeline, g_msaa, vp, hdrRes);                  // colour -> 1x HDR (rests in PIXEL_SHADER_RESOURCE)
            GhostMSAA_ResolveDepth(g_rhi, g_pipeline, g_msaa, vp, rw, rh);         // nearest depth sample -> the 1x depth buffer
            GhostProf_Mark(g_rhi, g_prof, GP_RESOLVE);
            if (aoOn || giOn || fogOn || shaftOn)
            {
                ID3D12Resource* dep = g_pipeline->depthBuffers[vp].Get();
                GhostPost_ToRT(g_rhi, g_post, vp);
                if (aoOn)  GhostPost_SSAO(g_rhi, g_post, vp, hdrRes, GhostPost_RTV(g_post, vp), dep, rw, rh, postFx, proj6, aoNoise);
                if (giOn)  GhostPost_SSGI(g_rhi, g_post, vp, hdrRes, GhostPost_RTV(g_post, vp), dep, rw, rh, postFx, proj6, gf, giNoise);
                if (fogOn) GhostPost_Fog(g_rhi, g_pipeline, g_post, vp, hdrRes, GhostPost_RTV(g_post, vp), dep, rw, rh);
                if (shaftOn) GhostShafts_Draw(g_rhi, g_shafts, vp, hdrRes, GhostPost_RTV(g_post, vp), dep,
                    rw, rh, proj.m[2][2], proj.m[2][3], aoNoise, shaftJobs, shaftN);
                GhostPost_ToSRV(g_rhi, g_post, vp);
            }
        }
        else if (taa)
        {
            if (aoOn)                                                              // on the jittered image: the TAA resolve averages the sampling noise
                GhostPost_SSAO(g_rhi, g_post, vp, g_taa.scene[vp].Get(), GhostTAA_SceneRTV(g_taa, vp),
                    g_taa.depth[vp].Get(), rw, rh, postFx, proj6, aoNoise);
            if (giOn)
                GhostPost_SSGI(g_rhi, g_post, vp, g_taa.scene[vp].Get(), GhostTAA_SceneRTV(g_taa, vp),
                    g_taa.depth[vp].Get(), rw, rh, postFx, proj6, gf, giNoise);
            if (fogOn)
                GhostPost_Fog(g_rhi, g_pipeline, g_post, vp, g_taa.scene[vp].Get(), GhostTAA_SceneRTV(g_taa, vp),
                    g_taa.depth[vp].Get(), rw, rh);
            if(shaftOn)
                GhostShafts_Draw(g_rhi, g_shafts, vp, g_taa.scene[vp].Get(), GhostTAA_SceneRTV(g_taa, vp),
                    g_taa.depth[vp].Get(), rw, rh, proj.m[2][2], proj.m[2][3], aoNoise, shaftJobs, shaftN);
            GhostTAAFrame tf = {};
            const GhostVec3 camUp = forward.Cross(right);
            tf.eye[0] = eye.x;      tf.eye[1] = eye.y;      tf.eye[2] = eye.z;
            tf.right[0] = right.x;  tf.right[1] = right.y;  tf.right[2] = right.z;
            tf.up[0] = camUp.x;     tf.up[1] = camUp.y;     tf.up[2] = camUp.z;
            tf.fwd[0] = forward.x;  tf.fwd[1] = forward.y;  tf.fwd[2] = forward.z;
            tf.p00 = projClean.m[0][0];  tf.p11 = projClean.m[1][1];
            tf.p22 = projClean.m[2][2];  tf.p23 = projClean.m[2][3];
            const GhostMat4 vpClean = projClean * view;
            memcpy(tf.vp, vpClean.m, 16 * sizeof(float));

            GhostPost_ToRT(g_rhi, g_post, vp);                                     // the TAA resolve writes its output here
            GhostTAA_End(g_rhi, g_pipeline, g_taa, vp, GhostPost_RTV(g_post, vp), false, tf);
            GhostPost_ToSRV(g_rhi, g_post, vp);
        }
        else
        {
            ID3D12Resource* dep = g_pipeline->depthBuffers[vp].Get();              // the HDR target is still in RENDER_TARGET state here
            if (aoOn)  GhostPost_SSAO(g_rhi, g_post, vp, hdrRes, GhostPost_RTV(g_post, vp), dep, rw, rh, postFx, proj6, aoNoise);
            if (giOn)  GhostPost_SSGI(g_rhi, g_post, vp, hdrRes, GhostPost_RTV(g_post, vp), dep, rw, rh, postFx, proj6, gf, giNoise);
            if (fogOn) GhostPost_Fog(g_rhi, g_pipeline, g_post, vp, hdrRes, GhostPost_RTV(g_post, vp), dep, rw, rh);
            if (shaftOn) GhostShafts_Draw(g_rhi, g_shafts, vp, hdrRes, GhostPost_RTV(g_post, vp), dep,
                rw, rh, proj.m[2][2], proj.m[2][3], aoNoise, shaftJobs, shaftN);
            GhostPost_EndScene(g_rhi, g_pipeline, g_post, vp);
        }
     

        GhostProf_Mark(g_rhi, g_prof, GP_POSTFX);
        if (ssrWant)                                                       // this finished frame becomes next frame's reflection source
        {
            const float v12[12] = { view.m[0][0], view.m[0][1], view.m[0][2], view.m[0][3],
                                    view.m[1][0], view.m[1][1], view.m[1][2], view.m[1][3],
                                    view.m[2][0], view.m[2][1], view.m[2][2], view.m[2][3] };
            const float p8[8] = { projClean.m[0][0], projClean.m[1][1], projClean.m[0][2], projClean.m[1][2],
                                  projClean.m[2][2], projClean.m[2][3], (float)rw, (float)rh };
            ID3D12Resource* ssrDep = taa ? g_taa.depth[vp].Get() : g_pipeline->depthBuffers[vp].Get();
            GhostPost_SSRCapture(g_rhi, g_post, vp, hdrRes, ssrDep, rw, rh, v12, p8);
        }

        const GhostPostSettings postCfg = preview ? GhostPostSettings() : postFx;     // the preview window never blooms
        GhostPost_Bloom(g_rhi, g_post, vp, rw, rh, postCfg);                                // HDR -> bloom chain (only if enabled)
        // ---- Tone map into the destination (offscreen image, or the window back buffer) ----
        D3D12_CPU_DESCRIPTOR_HANDLE destRTV;
        if (custom)
        {
            GhostBeginOffscreen(g_rhi, g_pipeline, vp, &clr, rw, rh);        // offscreen -> RENDER_TARGET, overrides on
            destRTV = GhostGetOffscreenRTV(g_pipeline, vp);
        }
        else destRTV = g_rhi->GetRTV();
        if (smaa)                                                            // tone map into the LDR target, then SMAA writes the destination
        {
            GhostSMAA_BeginLdr(g_rhi, g_smaa, vp);
            GhostPost_Tonemap(g_rhi, g_post, vp, GhostSMAA_LdrRTV(g_smaa, vp), rw, rh, postCfg);
            GhostSMAA_Run(g_rhi, g_smaa, vp, destRTV, rw, rh);
        }
        else if (fxaa)                                                       // tone map into the LDR target, then FXAA writes the destination
        {
            GhostFXAA_BeginLdr(g_rhi, g_fxaa, vp);
            GhostPost_Tonemap(g_rhi, g_post, vp, GhostFXAA_LdrRTV(g_fxaa, vp), rw, rh, postCfg);
            GhostFXAA_Run(g_rhi, g_fxaa, vp, destRTV, rw, rh);
        }
        else GhostPost_Tonemap(g_rhi, g_post, vp, destRTV, rw, rh, postCfg);
        GhostProf_Mark(g_rhi, g_prof, GP_TONEMAP);
        // Everything below draws display-space colors on the destination: icons, overlays, game UI.
        // Depth-tested overlays use the depth the scene was drawn with (TAA has its own; with MSAA there is none yet).
        if (taa) { g_pipeline->dsvOverride = GhostTAA_GetDSV(g_taa, vp); g_pipeline->dsvOverrideActive = true; }
        proj = projClean;                                                     // overlays and icons are never jittered
    }

    // ---- Icons: main view only ----
    if (overlays)
    {
        for (const GhostLight& l : g_scene.lights)
        {
            int state = (l.id == g_scene.selectedId) ? 2 : (l.id == g_hoverId ? 1 : 0);
            GhostVec3 fwd = LightForward(l);
            DrawIcon(slot, l.type == GhostLightType::Spot ? 6 : (int)l.type, l.position, fwd, l.color, state, view, proj);
            if (l.type == GhostLightType::Directional || l.type == GhostLightType::Spot)
                DrawIcon(slot, 3, l.position, fwd, l.color, state, view, proj);   // direction arrow
        }

        static const float kCamColor[3] = { 0.5f, 0.83f, 1.0f };
        for (const GhostCamera& c : g_scene.cameras)
        {
            if (c.id == g_scene.activeCameraId) continue;       // you're looking through this one
            int state = (c.id == g_scene.selectedId) ? 2 : (c.id == g_hoverId ? 1 : 0);
            GhostVec3 fwd = AnglesToDir(c.yawDeg, c.pitchDeg);
            DrawIcon(slot, 4, c.position, fwd, kCamColor, state, view, proj);   // camera glyph
            DrawIcon(slot, 3, c.position, fwd, kCamColor, state, view, proj);   // direction arrow
        }
        static const float kModColor[3] = { 1.0f, 0.82f, 0.35f };
        for (const GhostModEntity& m : g_scene.mods)
        {
            int state = (m.id == g_scene.selectedId) ? 2 : (m.id == g_hoverId ? 1 : 0);
            DrawIcon(slot, 5, m.position, GhostVec3(0, 0, 1), kModColor, state, view, proj);   // cloud glyph
        }
        for (const GhostVolume& v : g_scene.volumes)
        {
            int state = (v.id == g_scene.selectedId) ? 2 : (v.id == g_hoverId ? 1 : 0);
            DrawIcon(slot, 5, v.position, GhostVec3(0, 0, 1), kVolColor[v.type], state, view, proj);
        }
        static const float kFxColor[3] = { 1.0f, 0.62f, 0.2f };
        for (const GhostParticleDef& pd : g_scene.particles)
        {
            int state = (pd.p.id == g_scene.selectedId) ? 2 : (pd.p.id == g_hoverId ? 1 : 0);
            DrawIcon(slot, 7, GhostVec3(pd.p.px, pd.p.py, pd.p.pz), GhostVec3(0, 0, 1), kFxColor, state, view, proj);
        }

    }

    if (mainView && (overlays || g_viewCfg.showFps)) DrawViewportOverlay(view, proj, eye, rw, rh, overlays);

    // ---- 2D game UI: main view, while a game is running. Retained widgets first, then the game's immediate quads ----
    if (mainView && g_playState != GhostPlayState::Editing)
    {
        g_uiRefW = 1080.0f * (float)rw / (float)rh;

        static std::vector<GhostUIQuad> frameQuads;
        frameQuads.clear();
        for (GhostUIAsset& a : g_uiAssets)
        {
            GhostUI_Layout(a, g_uiRefW, 1080.0f);
            GhostUI_Build(a, g_font, g_fonts, frameQuads);
        }
        frameQuads.insert(frameQuads.end(), g_uiQuads.begin(), g_uiQuads.end());
        if (frameQuads.size() > GHOST_UI_MAX_QUADS) frameQuads.resize(GHOST_UI_MAX_QUADS);

        GhostDrawUI(g_rhi, g_pipeline, frameQuads, rw, rh, (float)rh / 1080.0f);
    }

 
    if (custom)
    {
        GhostEndOffscreen(g_rhi, g_pipeline, vp);
        GhostDrawBlit(g_rhi, g_pipeline, vp, fx, fy, fw, fh, g_video.scaleFilter == GHOST_FILTER_POINT ? 1 : 0);
    }
    g_pipeline->dsvOverrideActive = false;                      // never leak an override into the next viewport
    g_pipeline->rtvOverrideActive = false;

    GhostProf_Mark(g_rhi, g_prof, GP_END);
    GhostProf_Resolve(g_rhi, g_prof);
    g_rhi->EndFrame();
    GhostProf_Collect(g_prof);
}

// ---- Orbit cameras ----
static bool ResolveOrbitTarget(const GhostCamera& c, GhostVec3& out)
{
    if (c.targetId)
        for (const GhostObject& o : g_scene.objects)
            if (o.id == c.targetId) { out = o.position; return true; }
    if (!c.targetTag.empty())
        for (const GhostObject& o : g_scene.objects)
            if (GhostObject_Tag(o) == c.targetTag) { out = o.position; return true; }
    return false;
}

// smooth = true while a game is running; in the editor the camera snaps so it follows a dragged target exactly
static void UpdateOrbitCameras(float dt, bool smooth)
{
    for (GhostCamera& c : g_scene.cameras)
    {
        if (c.mode != 1) continue;
        GhostVec3 t;
        if (!ResolveOrbitTarget(c, t)) continue;       // no target: it behaves like a free camera

        const float targetY = t.y;
        t.y += c.heightOffset;
        GhostVec3 want = t - AnglesToDir(c.yawDeg, c.pitchDeg) * c.distance;

        if (!smooth || c.smoothing <= 0.0f || !c.orbitValid) { c.orbitPos = want; c.orbitValid = true; }
        else
        {
            float k = 1.0f - expf(-c.smoothing * dt);
            c.orbitPos = c.orbitPos + (want - c.orbitPos) * k;
        }
        c.position = c.orbitPos;
        if (smooth)                                                  // ground floor, but it follows Mario underground
        {
            const float floorY = fminf(0.3f, targetY - 0.5f);
            if (c.position.y < floorY) c.position.y = floorY;
        }
    }
}

// ============================================================
//  Undo / redo: automatic snapshots of the editable scene (textures are not rolled back)
// ============================================================
struct UndoSnap
{
    std::vector<GhostObject> objects;
    std::vector<GhostLight> lights;
    std::vector<GhostCamera> cameras;
    std::vector<GhostModEntity> mods;
    float ambientColor[3] = { 1, 1, 1 };
    float ambientIntensity = 0.15f, shadowStrength = 1.0f;
    bool  shadowsEnabled = true;
    int   shadowRes = 1024, shadowFilter = 2;
    float shadowSoftness = 1.0f, shadowBias = 1.0f, shadowNormalBias = 1.0f, shadowDistance = 30.0f;
    bool  shadowFilterLod = false;
    float shadowFilterLodFrac = 0.45f;
    std::string music, ambience;
    int   activeCameraId = 0, gameStartCameraId = 0, nextId = 1;
    std::vector<GhostVolume> volumes;
    GhostPostSettings post;
    std::vector<GhostParticleDef> particles;
    GhostFogSettings fog;
 };
static std::vector<UndoSnap> g_undo, g_redo;
static UndoSnap     g_undoBase;
static unsigned int g_undoBaseHash = 0;
static bool         g_undoHaveBase = false;
static float        g_undoTimer = 0.0f;
static const size_t kUndoMax = 40;

static void UndoCapture(UndoSnap& s)
{
    s.objects = g_scene.objects; s.lights = g_scene.lights; s.cameras = g_scene.cameras; s.mods = g_scene.mods;
    for (int i = 0; i < 3; i++) s.ambientColor[i] = g_scene.ambientColor[i];
    s.ambientIntensity = g_scene.ambientIntensity; s.shadowStrength = g_scene.shadowStrength;
    s.shadowsEnabled = g_scene.shadowsEnabled;
    s.shadowRes = g_scene.shadowRes; s.shadowFilter = g_scene.shadowFilter; s.shadowSoftness = g_scene.shadowSoftness;
    s.shadowBias = g_scene.shadowBias; s.shadowNormalBias = g_scene.shadowNormalBias; s.shadowDistance = g_scene.shadowDistance;
    s.shadowFilterLod = g_scene.shadowFilterLod; s.shadowFilterLodFrac = g_scene.shadowFilterLodFrac;
    s.activeCameraId = g_scene.activeCameraId; s.gameStartCameraId = g_scene.gameStartCameraId; s.nextId = g_scene.nextId;
    s.music = g_scene.music; s.ambience = g_scene.ambience;
    s.volumes = g_scene.volumes;
    s.post = g_scene.post;
    s.particles = g_scene.particles;
    s.fog = g_scene.fog;
}

static void UndoReset() { g_undo.clear(); g_redo.clear(); g_undoHaveBase = false; }

// If the scene changed since the last snapshot, the old state becomes an undo step
static void UndoCommit()
{
    const unsigned int h = GhostScene_Hash(g_scene);
    if (!g_undoHaveBase) { UndoCapture(g_undoBase); g_undoBaseHash = h; g_undoHaveBase = true; return; }
    if (h == g_undoBaseHash) return;
    g_undo.push_back(std::move(g_undoBase));
    if (g_undo.size() > kUndoMax) g_undo.erase(g_undo.begin());
    g_redo.clear();
    UndoCapture(g_undoBase);
    g_undoBaseHash = h;
}

static void UndoTick(float dt)
{
    g_undoTimer += dt;
    if (g_undoTimer < 0.15f) return;
    g_undoTimer = 0.0f;
    if (g_gizmo.active || g_dragging) return;                                            // a drag is one step: wait for the release
    if ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000) return;
    UndoCommit();
}

static void UndoRestore(const UndoSnap& s)
{
    GhostCamera keep;                                   // the hash ignores the main camera's pose, so keep the current one
    bool haveKeep = false;
    if (GhostCamera* cur = ActiveCamera()) { keep = *cur; haveKeep = true; }

    g_scene.objects = s.objects; g_scene.lights = s.lights; g_scene.cameras = s.cameras; g_scene.mods = s.mods; g_scene.particles = s.particles;
    for (int i = 0; i < 3; i++) g_scene.ambientColor[i] = s.ambientColor[i];
    g_scene.ambientIntensity = s.ambientIntensity; g_scene.shadowStrength = s.shadowStrength;
    g_scene.shadowsEnabled = s.shadowsEnabled;
    g_scene.shadowRes = s.shadowRes; g_scene.shadowFilter = s.shadowFilter; g_scene.shadowSoftness = s.shadowSoftness;
    g_scene.shadowBias = s.shadowBias; g_scene.shadowNormalBias = s.shadowNormalBias; g_scene.shadowDistance = s.shadowDistance;
    g_scene.shadowFilterLod = s.shadowFilterLod; g_scene.shadowFilterLodFrac = s.shadowFilterLodFrac;
    g_scene.music = s.music; g_scene.ambience = s.ambience;
    g_scene.activeCameraId = s.activeCameraId; g_scene.gameStartCameraId = s.gameStartCameraId;
    g_scene.volumes = s.volumes;
    g_scene.post = s.post;
    g_scene.fog = s.fog;

    if (s.nextId > g_scene.nextId) g_scene.nextId = s.nextId;      // ids never go backwards

    if (haveKeep)
        for (GhostCamera& c : g_scene.cameras)
            if (c.id == keep.id) { c.position = keep.position; c.yawDeg = keep.yawDeg; c.pitchDeg = keep.pitchDeg; }

    if (!EntityPosition(g_scene.selectedId)) g_scene.selectedId = 0;
    g_multi.clear();
    g_scene.selectedFace = -1;
    g_hoverId = 0; g_dragging = false; g_dragId = 0;
    g_scene.version++;
}

int GhostEngine_Undo()
{
    if (g_playState != GhostPlayState::Editing) return 0;
    UndoCommit();                                        // an edit made in the last fraction of a second counts too
    if (g_undo.empty()) return 0;
    g_redo.push_back(std::move(g_undoBase));
    UndoSnap s = std::move(g_undo.back());
    g_undo.pop_back();
    UndoRestore(s);
    g_undoBase = std::move(s);
    g_undoBaseHash = GhostScene_Hash(g_scene);
    return 1;
}

int GhostEngine_Redo()
{
    if (g_playState != GhostPlayState::Editing) return 0;
    UndoCommit();
    if (g_redo.empty()) return 0;
    g_undo.push_back(std::move(g_undoBase));
    UndoSnap s = std::move(g_redo.back());
    g_redo.pop_back();
    UndoRestore(s);
    g_undoBase = std::move(s);
    g_undoBaseHash = GhostScene_Hash(g_scene);
    return 1;
}

int GhostEngine_UndoCount() { return (int)g_undo.size(); }
int GhostEngine_RedoCount() { return (int)g_redo.size(); }

// ============================================================
//  Tile Map mesh: one quad per tile (own 4 vertices, so each tile can get its own material / data later)
// ============================================================
// ============================================================
//  Tile Map terrain: mesh, weight maps, and the paint-mode brush
// ============================================================
static void TileAxes(int dir, GhostVec3& U, GhostVec3& V, GhostVec3& N)       // U, V = the grid's axes, N = facing. cross(V, U) = N
{
    if (dir == 0) { U = GhostVec3(0, 0, 1); V = GhostVec3(0, 1, 0); N = GhostVec3(1, 0, 0); }
    else if (dir == 2) { U = GhostVec3(1, 0, 0); V = GhostVec3(0, 1, 0); N = GhostVec3(0, 0, -1); }
    else { U = GhostVec3(1, 0, 0); V = GhostVec3(0, 0, 1); N = GhostVec3(0, 1, 0); }
}

// Object scale along the grid's U, V and facing axes
static void TileScales(const GhostObject& o, float& sU, float& sV, float& sN)
{
    const float x = fabsf(o.scale.x), y = fabsf(o.scale.y), z = fabsf(o.scale.z);
    if (o.tileMap.direction == 0) { sU = z; sV = y; sN = x; }
    else if (o.tileMap.direction == 2) { sU = x; sV = y; sN = z; }
    else { sU = x; sV = z; sN = y; }
    if (sU < 1e-3f) sU = 1e-3f;
    if (sV < 1e-3f) sV = 1e-3f;
    if (sN < 1e-3f) sN = 1e-3f;
}

// Height of a vertex that lies just outside this cell's grid, taken from the neighbouring cell. false = no neighbour there.
static bool TileNeighborHeight(const GhostObject& self, int p, int q, float& hLocal)
{
    const GhostTileMap& t = self.tileMap;
    if (t.direction != 1) return false;
    if (self.scale.x <= 0.0f || self.scale.y <= 0.0f || self.scale.z <= 0.0f) return false;
    if (fabsf(self.rotationDeg.x) > 0.01f || fabsf(self.rotationDeg.y) > 0.01f || fabsf(self.rotationDeg.z) > 0.01f) return false;

    const int   sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    const float cell = t.tileSize / (float)sub;
    const float u0 = -0.5f * (float)t.gridX * t.tileSize, v0 = -0.5f * (float)t.gridY * t.tileSize;
    const float wx = self.position.x + (u0 + (float)p * cell) * self.scale.x;
    const float wz = self.position.z + (v0 + (float)q * cell) * self.scale.z;
    const float cwx = cell * self.scale.x, cwz = cell * self.scale.z;

    for (const GhostObject& n : g_scene.objects)
    {
        if (&n == &self || !n.tileMap.enabled) continue;
        const GhostTileMap& nt = n.tileMap;
        if (nt.direction != 1 || n.scale.x <= 0.0f || n.scale.y <= 0.0f || n.scale.z <= 0.0f) continue;
        if (fabsf(n.rotationDeg.x) > 0.01f || fabsf(n.rotationDeg.y) > 0.01f || fabsf(n.rotationDeg.z) > 0.01f) continue;

        const int   nsub = GhostTileMap_EffSub(nt.gridX, nt.gridY, nt.subdiv);
        const float ncell = nt.tileSize / (float)nsub;
        if (fabsf(ncell * n.scale.x - cwx) > 1e-3f * cwx || fabsf(ncell * n.scale.z - cwz) > 1e-3f * cwz) continue;   // different resolution
        const int nvx = nt.gridX * nsub + 1, nvy = nt.gridY * nsub + 1;
        if (nt.height.size() != (size_t)nvx * (size_t)nvy) continue;

        const float nu0 = -0.5f * (float)nt.gridX * nt.tileSize, nv0 = -0.5f * (float)nt.gridY * nt.tileSize;
        const float fu = ((wx - n.position.x) / n.scale.x - nu0) / ncell;
        const float fv = ((wz - n.position.z) / n.scale.z - nv0) / ncell;
        const int iu = (int)floorf(fu + 0.5f), iv = (int)floorf(fv + 0.5f);
        if (fabsf(fu - (float)iu) > 0.02f || fabsf(fv - (float)iv) > 0.02f) continue;                                   // not on the same lattice
        if (iu < 0 || iv < 0 || iu >= nvx || iv >= nvy) continue;

        const float hw = n.position.y + nt.height[(size_t)iv * nvx + iu] * n.scale.y;
        hLocal = (hw - self.position.y) / self.scale.y;
        return true;
    }
    return false;
}

// Builds the cell mesh: shared vertices + skirt vertices; indices = LOD blocks (each: row bands, then the skirt).
// Also fills the layout fields of ti.
static void BuildTileMeshLod(const GhostObject& o, std::vector<GhostVertex>& v, std::vector<unsigned int>& idx, TileInst& ti)
{
    const GhostTileMap& t = o.tileMap;
    GhostVec3 U, V, N;
    TileAxes(t.direction, U, V, N);
    const int   sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    const int   nx = t.gridX * sub, ny = t.gridY * sub, vx = nx + 1, vy = ny + 1;
    const float cell = t.tileSize / (float)sub;
    const float u0 = -0.5f * (float)t.gridX * t.tileSize, v0 = -0.5f * (float)t.gridY * t.tileSize;
    const bool  haveH = t.height.size() == (size_t)vx * (size_t)vy;

    auto Hin = [&](int p, int q) -> float { return haveH ? t.height[(size_t)q * vx + p] : 0.0f; };
    auto Hq = [&](int p, int q, float& h) -> bool          // inside = this cell, outside = the neighbouring cell (if any)
        {
            if (p >= 0 && q >= 0 && p < vx && q < vy) { h = Hin(p, q); return true; }
            return TileNeighborHeight(o, p, q, h);
        };

    const bool skirts = t.skirts;
    const size_t mainCount = (size_t)vx * (size_t)vy;
    const size_t skirtVerts = skirts ? (size_t)(2 * (nx + 1) + 2 * (ny + 1)) : 0;
    v.resize(mainCount + skirtVerts);

    float hMin = 1e30f, hMax = -1e30f;
    for (int q = 0; q < vy; q++)
        for (int p = 0; p < vx; p++)
        {
            const float hc = Hin(p, q);
            float hl = 0, hr = 0, hd = 0, hu = 0;
            const bool okL = Hq(p - 1, q, hl), okR = Hq(p + 1, q, hr), okD = Hq(p, q - 1, hd), okU = Hq(p, q + 1, hu);
            float su = 0.0f, sv = 0.0f;                                                   // slopes along the two axes
            if (okL && okR) su = (hr - hl) / (2.0f * cell); else if (okR) su = (hr - hc) / cell; else if (okL) su = (hc - hl) / cell;
            if (okD && okU) sv = (hu - hd) / (2.0f * cell); else if (okU) sv = (hu - hc) / cell; else if (okD) sv = (hc - hd) / cell;
            const GhostVec3 n = (N - U * su - V * sv).Normalize();                        // normal of the surface u*U + v*V + h*N
            hMin = fminf(hMin, hc); hMax = fmaxf(hMax, hc);

            const float pu = u0 + (float)p * cell, pv = v0 + (float)q * cell;
            GhostVertex g = {};
            g.x = U.x * pu + V.x * pv + N.x * hc;
            g.y = U.y * pu + V.y * pv + N.y * hc;
            g.z = U.z * pu + V.z * pv + N.z * hc;
            g.r = g.g = g.b = 1.0f; g.a = 1.0f;
            g.nx = n.x; g.ny = n.y; g.nz = n.z;
            g.u = (float)p / (float)sub;
            g.v = (float)t.gridY - (float)q / (float)sub;
            v[(size_t)q * vx + p] = g;
        }

    // ---- skirt: a copy of every border vertex, pushed down the N axis. Hides cracks between cells of different LOD ----
    const float depth = fmaxf(1.0f, (hMax - hMin) * 0.1f + cell * 6.0f);
    auto skirtOf = [&](size_t dst, size_t src)
        {
            GhostVertex g = v[src];
            g.x -= N.x * depth; g.y -= N.y * depth; g.z -= N.z * depth;
            v[dst] = g;
        };
    const unsigned int sb = (unsigned int)mainCount;
    if (skirts)
    {

        for (int p = 0; p <= nx; p++) { skirtOf(sb + (size_t)p, (size_t)p);               skirtOf(sb + (size_t)(nx + 1) + p, (size_t)ny * vx + p); }
        for (int q = 0; q <= ny; q++) { skirtOf(sb + (size_t)(2 * (nx + 1)) + q, (size_t)q * vx); skirtOf(sb + (size_t)(2 * (nx + 1) + (ny + 1)) + q, (size_t)q * vx + nx); }
    }

    // ---- LOD levels: stride 1 / 2 / 4, only where the grid divides evenly ----
    int maxLod = 0;
    if (nx % 2 == 0 && ny % 2 == 0 && nx >= 8 && ny >= 8) maxLod = 1;
    if (maxLod == 1 && nx % 4 == 0 && ny % 4 == 0 && nx >= 16 && ny >= 16) maxLod = 2;

    idx.clear();
    idx.reserve((size_t)nx * (size_t)ny * 8 + 4096);
    auto quad = [&](unsigned int a, unsigned int b, unsigned int c, unsigned int d)     // a b / c d
        {
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(c); idx.push_back(d); idx.push_back(b);
        };
    for (int l = 0; l < 3; l++) { ti.lodFirst[l] = 0; ti.skirtFirst[l] = 0; ti.skirtCount[l] = 0; }
    for (int lod = 0; lod <= maxLod; lod++)
    {
        const int s = 1 << lod;
        ti.lodFirst[lod] = (unsigned int)idx.size();
        for (int q = 0; q < ny; q += s)                                                  // rows in order, so a band is one contiguous range
            for (int p = 0; p < nx; p += s)
            {
                const unsigned int i00 = (unsigned int)(q * vx + p);
                quad(i00, i00 + s, i00 + (unsigned int)(s * vx), i00 + (unsigned int)(s * vx) + s);
            }
        ti.skirtFirst[lod] = (unsigned int)idx.size();
        if (skirts)
        {
            for (int p = 0; p < nx; p += s)
            {
                quad(p, p + s, sb + p, sb + p + s);                                                                     // q = 0
                quad(ny * vx + p, ny * vx + p + s, sb + (nx + 1) + p, sb + (nx + 1) + p + s);                          // q = ny
            }
            for (int q = 0; q < ny; q += s)
            {
                quad(q * vx, (q + s) * vx, sb + 2 * (nx + 1) + q, sb + 2 * (nx + 1) + q + s);                          // p = 0
                quad(q * vx + nx, (q + s) * vx + nx, sb + 2 * (nx + 1) + (ny + 1) + q, sb + 2 * (nx + 1) + (ny + 1) + q + s);  // p = nx
            }
        }
        ti.skirtCount[lod] = (unsigned int)idx.size() - ti.skirtFirst[lod];
    }

    // ---- row bands for culling ----
    const int B = kTileBandRows;
    const int bands = (ny + B - 1) / B;
    ti.bandMin.assign((size_t)bands, 0.0f);
    ti.bandMax.assign((size_t)bands, 0.0f);
    for (int b = 0; b < bands; b++)
    {
        const int r0 = b * B, r1 = (r0 + B < ny) ? r0 + B : ny;
        float lo = 1e30f, hi = -1e30f;
        for (int q = r0; q <= r1; q++)
            for (int p = 0; p < vx; p++) { const float h = Hin(p, q); lo = fminf(lo, h); hi = fmaxf(hi, h); }
        ti.bandMin[b] = lo - 0.02f;
        ti.bandMax[b] = hi + 0.02f;
    }
    ti.nx = nx; ti.ny = ny; ti.bandRows = B; ti.bandCount = bands; ti.maxLod = maxLod;
    ti.cell = cell; ti.u0 = u0; ti.v0 = v0;
}

// ---- weight map textures (hidden from the texture lists: their names start with "[Splat") ----
static int TakeSplatTexture(const std::vector<unsigned char>& px, int w, int h)
{
    while (!g_splatFree.empty())
    {
        const int id = g_splatFree.back();
        g_splatFree.pop_back();
        if (GhostCreateTexture(g_rhi, g_pipeline, px.data(), (unsigned int)w, (unsigned int)h, id) == id)
        {
            g_scene.textures[(size_t)id].width = (unsigned int)w;
            g_scene.textures[(size_t)id].height = (unsigned int)h;
            return id;
        }
    }
    const int id = RegisterTexture("[Splat]", px.data(), (unsigned int)w, (unsigned int)h);
    return id > 0 ? id : 0;
}

static void ReleaseSplat(TileInst& ti)
{
    if (ti.splatA > 0) g_splatFree.push_back(ti.splatA);
    if (ti.splatB > 0) g_splatFree.push_back(ti.splatB);
    ti.splatA = ti.splatB = 0;
    ti.splatW = ti.splatH = 0;
}
static void FreeAllTileInstances()
{
    for (auto& kv : g_tileInst) ReleaseSplat(kv.second);
    for (int k = 0; k < (int)GHOST_MAX_TILEMAPS; k++)
    {
        if (g_pipeline) GhostReleaseTileMesh(g_pipeline, k);
        g_tileSlotUsed[k] = false;
    }
    g_tileInst.clear();
    g_tileBurst = true;                      // the next update loads everything in range at once (no pop-in after a scene load)
}
static void UpdateSplat(const GhostObject& o, TileInst& ti)
{
    const GhostTileMap& t = o.tileMap;
    if (t.layerCount < 2)
    {
        if (ti.splatA > 0 || ti.splatB > 0) ReleaseSplat(ti);
        ti.sStamp = t.sStamp;
        return;
    }
    const int w = t.splatW, h = t.splatH;
    if (w <= 0 || h <= 0 || t.splat.size() != (size_t)w * (size_t)h * 8) return;
    const bool needB = t.layerCount > 4;
    const bool resized = (ti.splatW != w || ti.splatH != h);
    if (!resized && ti.sStamp == t.sStamp && ti.splatA > 0 && (ti.splatB > 0) == needB) return;

    static std::vector<unsigned char> pa, pb;
    const size_t n = (size_t)w * (size_t)h;
    auto push = [&](int& id, const std::vector<unsigned char>& px) -> bool
        {
            if (id <= 0) { id = TakeSplatTexture(px, w, h); return id > 0; }
            if (resized)
            {
                if (GhostCreateTexture(g_rhi, g_pipeline, px.data(), (unsigned int)w, (unsigned int)h, id) != id) return false;
                g_scene.textures[(size_t)id].width = (unsigned int)w;
                g_scene.textures[(size_t)id].height = (unsigned int)h;
                return true;
            }
            return GhostUpdateTextureLevel0(g_rhi, g_pipeline, id, px.data(), (unsigned int)w, (unsigned int)h);
        };

    bool ok = true;
    pa.resize(n * 4);
    for (size_t i = 0; i < n; i++) memcpy(&pa[i * 4], &t.splat[i * 8], 4);          // layers 0..3
    ok &= push(ti.splatA, pa);
    if (needB)
    {
        pb.resize(n * 4);
        for (size_t i = 0; i < n; i++) memcpy(&pb[i * 4], &t.splat[i * 8 + 4], 4);  // layers 4..7
        ok &= push(ti.splatB, pb);
    }
    else if (ti.splatB > 0) { g_splatFree.push_back(ti.splatB); ti.splatB = 0; }

    if (!ok) return;                                  // leave stamps/size untouched so the next frame retries
    ti.splatW = w; ti.splatH = h; ti.sStamp = t.sStamp;
}

// ---------------- world partition: which cells are resident ----------------
static unsigned int TileNeighborSig(const GhostObject& self)        // changes when a touching cell changes, so border normals rebuild
{
    const GhostTileMap& t = self.tileMap;
    if (t.direction != 1) return 0;
    const float W = (float)t.gridX * t.tileSize * fabsf(self.scale.x), D = (float)t.gridY * t.tileSize * fabsf(self.scale.z);
    unsigned int h = 0;
    for (const GhostObject& n : g_scene.objects)
    {
        if (&n == &self || !n.tileMap.enabled || n.tileMap.direction != 1) continue;
        const float nW = (float)n.tileMap.gridX * n.tileMap.tileSize * fabsf(n.scale.x);
        const float nD = (float)n.tileMap.gridY * n.tileMap.tileSize * fabsf(n.scale.z);
        const float dx = n.position.x - self.position.x, dz = n.position.z - self.position.z;
        if (fabsf(dx) > 0.5f * (W + nW) + 0.05f || fabsf(dz) > 0.5f * (D + nD) + 0.05f) continue;
        h = (h * 16777619u) ^ (unsigned int)n.id;
        h = (h * 16777619u) ^ (unsigned int)n.tileMap.hStamp;
        h = (h * 16777619u) ^ (unsigned int)(int)(dx * 100.0f);
        h = (h * 16777619u) ^ (unsigned int)(int)(dz * 100.0f);
        h = (h * 16777619u) ^ (unsigned int)(int)((n.position.y - self.position.y) * 100.0f);
    }
    return h;
}

static int FindFreeTileSlot()
{
    for (int k = 0; k < (int)GHOST_MAX_TILEMAPS; k++) if (!g_tileSlotUsed[k]) return k;
    return -1;
}

// The slot's GPU buffers stay allocated: the next cell that takes the slot reuses them (no allocation, no hitch)
static void UnloadTileCell(TileInst& ti)
{
    if (ti.slot >= 0) { g_tileSlotUsed[ti.slot] = false; ti.slot = -1; }
    ReleaseSplat(ti);
    ti.state = 0;
    ti.maskStamp = 0xFFFFFFFFu;
}

static bool LoadTileCell(GhostObject& o, TileInst& ti, int slot)
{
    static std::vector<GhostVertex> verts;
    static std::vector<unsigned int> idx;
    const GhostTileMap& t = o.tileMap;
    BuildTileMeshLod(o, verts, idx, ti);
    if (GhostUploadTileMesh(g_rhi, g_pipeline, slot, verts.data(), (unsigned int)verts.size(),
        idx.data(), (unsigned int)idx.size()) != GHOST_OK) return false;
    g_tileSlotUsed[slot] = true;
    ti.slot = slot;
    ti.tileSize = t.tileSize; ti.gx = t.gridX; ti.gy = t.gridY; ti.dir = t.direction;
    ti.sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    ti.hStamp = t.hStamp; ti.skirts = t.skirts;
    ti.neighSig = TileNeighborSig(o);
    return true;
}

static void GatherStreamFocus(std::vector<GhostVec3>& out)          // every camera that is looking at the world
{
    out.clear();
    for (int vp = 0; vp < GHOST_MAX_SURFACES; vp++)
    {
        if (!g_vp[vp].alive || !g_vp[vp].visible || g_vp[vp].preview) continue;
        if (GhostCamera* c = ViewportCamera(vp)) out.push_back(c->position);
    }
    if (out.empty()) { if (GhostCamera* c = ActiveCamera()) out.push_back(c->position); else out.push_back(GhostVec3(0, 0, 0)); }
}

// Between frames: stream cells in / out, rebuild the ones whose data changed, refresh weight maps, free removed ones
static void UpdateTileMaps()
{
    if (!g_rhi || !g_pipeline) return;
    for (auto& kv : g_tileInst) kv.second.seen = false;

    GhostWorldStats& st = g_wstats;
    st.cells = st.resident = st.starved = st.loadsLast = st.cellsDrawn = 0;
    st.bandsDrawn = st.bandsCulled = st.trisDrawn = st.objectsCulled = 0;     // the draw code accumulates these during the frame

    struct Cell { GhostObject* o; TileInst* ti; bool forced; bool want; };
    static std::vector<Cell> cells;
    static std::vector<GhostVec3> focus;
    cells.clear();
    GatherStreamFocus(focus);
    const bool editing = g_playState == GhostPlayState::Editing;

    // ---- 1. every Tile Map is a cell: measure it ----
    for (GhostObject& o : g_scene.objects)
    {
        if (!o.tileMap.enabled) continue;
        GhostTileMap_Clamp(o.tileMap);
        GhostTileMap_EnsureTiles(o.tileMap);
        if (o.tileMap.autoCollider && o.collider) GhostObject_FitTileCollider(o);
        TileInst& ti = g_tileInst[o.id];
        ti.seen = true;
        GhostVec3 wmin, wmax;
        TileWorldBox(o, wmin, wmax);
        float d = 1e30f;
        for (const GhostVec3& f : focus) d = fminf(d, TileBoxDistance(o, wmin, wmax, f));
        ti.dist = d;
        cells.push_back({ &o, &ti, editing && IsSelected(o.id), true });     // what you are editing is never streamed out
    }

    // ---- 2. who should be resident (load radius in, unload radius out: no flicker at the boundary) ----
    const float loadD = g_world.loadDist > 0.0f ? g_world.loadDist : g_world.farPlane + 10.0f;
    const float unloadD = loadD + g_world.unloadPad;
    for (Cell& c : cells)
    {
        TileInst& ti = *c.ti;
        if (!g_world.streaming || c.forced) c.want = true;
        else c.want = (ti.slot >= 0) ? (ti.dist <= unloadD) : (ti.dist <= loadD);
        if (!c.want && ti.slot >= 0) UnloadTileCell(ti);
    }

    // ---- 3. load the nearest missing cells, within the frame budget ----
    static std::vector<Cell*> need;
    need.clear();
    for (Cell& c : cells) if (c.want && c.ti->slot < 0) need.push_back(&c);
    std::sort(need.begin(), need.end(), [](const Cell* a, const Cell* b)
        { return a->forced != b->forced ? a->forced : a->ti->dist < b->ti->dist; });

    const bool burst = g_tileBurst && !cells.empty();
    if (burst) g_tileBurst = false;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    auto elapsedMs = [&]() { LARGE_INTEGER t1; QueryPerformanceCounter(&t1); return (float)((double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)g_freq.QuadPart); };

    int loads = 0;
    for (Cell* c : need)
    {
        const bool must = c->forced || burst;
        if (!must && loads > 0 && (loads >= g_world.maxLoads || elapsedMs() >= g_world.msBudget)) break;

        int slot = FindFreeTileSlot();
        if (slot < 0)                                                        // pool full: evict the farthest cell that is farther than this one
        {
            Cell* victim = nullptr;
            for (Cell& r : cells)
                if (r.ti->slot >= 0 && !r.forced && r.ti->dist > c->ti->dist && (!victim || r.ti->dist > victim->ti->dist)) victim = &r;
            if (!victim) { st.starved++; continue; }
            slot = victim->ti->slot;
            UnloadTileCell(*victim->ti);
        }
        if (LoadTileCell(*c->o, *c->ti, slot)) loads++;
    }
    st.loadsLast = loads;
    if (loads > 0) st.lastLoadMs = elapsedMs();

    // ---- 4. resident cells: rebuild changed meshes, refresh weight maps, note which layers have paint ----
    for (Cell& c : cells)
    {
        TileInst& ti = *c.ti;
        st.cells++;
        if (ti.slot < 0) { ti.state = c.want ? 2 : 0; continue; }
        st.resident++;
        ti.state = 1;

        GhostObject& o = *c.o;
        const GhostTileMap& t = o.tileMap;
        const int sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
        const unsigned int nsig = g_painting ? ti.neighSig : TileNeighborSig(o);         // neighbours wait until the stroke ends
        if (ti.hStamp != t.hStamp || ti.tileSize != t.tileSize || ti.gx != t.gridX || ti.gy != t.gridY ||
            ti.dir != t.direction || ti.sub != sub || ti.neighSig != nsig || ti.skirts != t.skirts)
        {
            if (!LoadTileCell(o, ti, ti.slot)) continue;
        }
        UpdateSplat(o, ti);

        if (ti.maskStamp != t.sStamp || ti.maskLayers != t.layerCount)
        {
            ti.maskStamp = t.sStamp; ti.maskLayers = t.layerCount;
            const size_t texels = (size_t)t.splatW * (size_t)t.splatH;
            if (t.layerCount < 2 || t.layerCount > 8 || texels == 0 || t.splat.size() != texels * 8) ti.usedMask = 0xFFFFFFFFu;
            else
            {
                const unsigned int all = (1u << t.layerCount) - 1u;
                unsigned int m = 1u;
                for (size_t i = 0; i < texels && m != all; i++)
                    for (int l = 1; l < t.layerCount; l++)
                        if (t.splat[i * 8 + (size_t)l] > 1) m |= (1u << l);
                ti.usedMask = m;
            }
        }
    }

    // ---- 5. deleted objects give their slot back ----
    std::vector<int> dead;
    for (auto& kv : g_tileInst) if (!kv.second.seen) dead.push_back(kv.first);
    for (int id : dead) { UnloadTileCell(g_tileInst[id]); g_tileInst.erase(id); }
}

// ---------------- paint mode ----------------
// Ray against the sculpted terrain. lu, lv = object-space coordinates of the hit along the grid's axes.
static bool TerrainHit(const GhostObject& o, const GhostVec3& ro, const GhostVec3& rd, float& lu, float& lv, GhostVec3& world)
{
    const GhostTileMap& t = o.tileMap;
    const GhostMat4 inv = ObjectInverse(o);
    const GhostVec3 lo = XformPoint(inv, ro, 1.0f), ld = XformPoint(inv, rd, 0.0f);      // t keeps its world meaning
    GhostVec3 mn, mx;
    GhostTileMap_Box(t, mn, mx);

    const float oo[3] = { lo.x, lo.y, lo.z }, dd[3] = { ld.x, ld.y, ld.z };
    const float a[3] = { mn.x, mn.y, mn.z }, b[3] = { mx.x, mx.y, mx.z };
    float t0 = 0.0f, t1 = 1e30f;
    for (int i = 0; i < 3; i++)
    {
        if (fabsf(dd[i]) < 1e-8f) { if (oo[i] < a[i] || oo[i] > b[i]) return false; continue; }
        const float k = 1.0f / dd[i];
        float s = (a[i] - oo[i]) * k, e = (b[i] - oo[i]) * k;
        if (s > e) { const float tmp = s; s = e; e = tmp; }
        if (s > t0) t0 = s;
        if (e < t1) t1 = e;
        if (t0 > t1) return false;
    }
    const float ldLen = sqrtf(ld.x * ld.x + ld.y * ld.y + ld.z * ld.z);
    if (ldLen < 1e-8f) return false;

    GhostVec3 U, V, N;
    TileAxes(t.direction, U, V, N);
    const float cell = t.tileSize / (float)GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    int steps = (int)ceilf((t1 - t0) * ldLen / (cell * 0.5f));
    steps = steps < 1 ? 1 : (steps > 4000 ? 4000 : steps);
    const float dt = (t1 - t0) / (float)steps;

    auto F = [&](float tt, float& u, float& v) -> float          // height of the ray above the terrain
        {
            const GhostVec3 P = lo + ld * tt;
            u = P.Dot(U); v = P.Dot(V);
            return P.Dot(N) - GhostTileMap_Height(t, u, v);
        };

    float u, v;
    float tp = t0, fp = F(t0, u, v);
    for (int i = 1; i <= steps; i++)
    {
        const float tc = t0 + dt * (float)i;
        const float fc = F(tc, u, v);
        if ((fp >= 0.0f) != (fc >= 0.0f))                         // crossed the surface: bisect
        {
            float ta = tp, tb = tc, fa = fp;
            for (int k = 0; k < 14; k++)
            {
                const float tm = 0.5f * (ta + tb);
                const float fm = F(tm, u, v);
                if ((fa >= 0.0f) == (fm >= 0.0f)) { ta = tm; fa = fm; }
                else tb = tm;
            }
            const float th = 0.5f * (ta + tb);
            F(th, lu, lv);
            world = ro + rd * th;
            return true;
        }
        tp = tc; fp = fc;
    }
    return false;
}

static float BrushFalloff(float d, float fade)      // d = distance / radius. 1 inside the core, smooth to 0 at the edge
{
    const float inner = 1.0f - fade;
    if (d <= inner) return 1.0f;
    if (d >= 1.0f) return 0.0f;
    const float x = (d - inner) / (1.0f - inner);
    return 1.0f - x * x * (3.0f - 2.0f * x);
}

static float Clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// amt = seconds of brush time this dab stands for
static void DabHeight(GhostObject& o, float lu, float lv, float amt)
{
    GhostTileMap& t = o.tileMap;
    const int   sub = GhostTileMap_EffSub(t.gridX, t.gridY, t.subdiv);
    const int   vx = t.gridX * sub + 1, vy = t.gridY * sub + 1;
    if (t.height.size() != (size_t)vx * (size_t)vy) return;
    const float cell = t.tileSize / (float)sub;
    const float u0 = -0.5f * (float)t.gridX * t.tileSize, v0 = -0.5f * (float)t.gridY * t.tileSize;
    float sU, sV, sN;
    TileScales(o, sU, sV, sN);

    const float R = g_brush.radius;
    const int pr = (int)ceilf(R / (sU * cell)) + 1, qr = (int)ceilf(R / (sV * cell)) + 1;
    const int pc = (int)floorf((lu - u0) / cell + 0.5f), qc = (int)floorf((lv - v0) / cell + 0.5f);
    const int p0 = pc - pr < 0 ? 0 : pc - pr, p1 = pc + pr > vx - 1 ? vx - 1 : pc + pr;
    const int q0 = qc - qr < 0 ? 0 : qc - qr, q1 = qc + qr > vy - 1 ? vy - 1 : qc + qr;

    const int tool = g_brush.tool;
    static std::vector<float> src;
    if (tool == 2) src = t.height;                                // smoothing reads the untouched copy

    for (int q = q0; q <= q1; q++)
        for (int p = p0; p <= p1; p++)
        {
            const float du = (u0 + (float)p * cell - lu) * sU, dv = (v0 + (float)q * cell - lv) * sV;
            const float d = sqrtf(du * du + dv * dv) / R;
            if (d >= 1.0f) continue;
            const float w = BrushFalloff(d, g_brush.fade);
            float& h = t.height[(size_t)q * vx + p];
            if (tool == 0)      h += g_brush.strength * 4.0f * w * amt / sN;            // up to 4 units per second
            else if (tool == 1) h -= g_brush.strength * 4.0f * w * amt / sN;
            else if (tool == 2)
            {
                float s = 0.0f; int c = 0;
                for (int dq = -1; dq <= 1; dq++)
                    for (int dp = -1; dp <= 1; dp++)
                    {
                        const int pp = p + dp, qq = q + dq;
                        if (pp < 0 || qq < 0 || pp >= vx || qq >= vy) continue;
                        s += src[(size_t)qq * vx + pp]; c++;
                    }
                h += (s / (float)c - h) * Clamp01f(g_brush.strength * w * amt * 12.0f);
            }
            else if (tool == 3) h += (g_flattenH - h) * Clamp01f(g_brush.strength * w * amt * 12.0f);
            else                h += (g_brush.setHeight / sN - h) * Clamp01f(g_brush.strength * w * amt * 12.0f);
            if (h > 1000.0f) h = 1000.0f;
            if (h < -1000.0f) h = -1000.0f;
        }
}

static void DabSplat(GhostObject& o, float lu, float lv, float amt, int layer)
{
    GhostTileMap& t = o.tileMap;
    if (t.layerCount < 2 || layer < 0 || layer >= t.layerCount) return;
    const int W = t.splatW, H = t.splatH;
    if (W <= 0 || H <= 0 || t.splat.size() != (size_t)W * (size_t)H * 8) return;
    const float gw = (float)t.gridX * t.tileSize, gh = (float)t.gridY * t.tileSize;
    const float tw = gw / (float)W, th = gh / (float)H;
    const float u0 = -0.5f * gw, v0 = -0.5f * gh;
    float sU, sV, sN;
    TileScales(o, sU, sV, sN);

    const float R = g_brush.radius;
    const int rc = (int)ceilf(R / (sU * tw)) + 1, rr = (int)ceilf(R / (sV * th)) + 1;
    const int cc = (int)floorf((lu - u0) / tw), cr = (int)floorf((lv - v0) / th);
    const int c0 = cc - rc < 0 ? 0 : cc - rc, c1 = cc + rc > W - 1 ? W - 1 : cc + rc;
    const int r0 = cr - rr < 0 ? 0 : cr - rr, r1 = cr + rr > H - 1 ? H - 1 : cr + rr;
    const bool paint = (g_brush.tool == 5);
    if (!paint && layer == 0) return;                                 // nothing to erase from the base layer

    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
        {
            const float du = (u0 + ((float)c + 0.5f) * tw - lu) * sU, dv = (v0 + ((float)r + 0.5f) * th - lv) * sV;
            const float d = sqrtf(du * du + dv * dv) / R;
            if (d >= 1.0f) continue;
            const float k = Clamp01f(g_brush.strength * BrushFalloff(d, g_brush.fade) * amt * 8.0f);
            if (k <= 0.0f) continue;

            unsigned char* b = &t.splat[((size_t)r * W + c) * 8];
            float f[8];
            for (int i = 0; i < 8; i++) f[i] = (float)b[i] / 255.0f;
            int fix;
            if (paint)                                                // this layer grows, the others shrink in proportion
            {
                const float cur = f[layer], nw = cur + (1.0f - cur) * k, others = 1.0f - cur;
                if (others > 1e-5f) { const float s = (1.0f - nw) / others; for (int i = 0; i < 8; i++) if (i != layer) f[i] *= s; }
                f[layer] = nw;
                fix = layer;
            }
            else                                                      // erase: the weight goes back to layer 0
            {
                const float cur = f[layer], nw = cur * (1.0f - k);
                f[0] += cur - nw;
                f[layer] = nw;
                fix = 0;
            }
            int q[8], rest = 0, big = -1;
            for (int i = 0; i < 8; i++)
            {
                int v = (int)(f[i] * 255.0f + 0.5f);
                q[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
                if (i != fix) { rest += q[i]; if (big < 0 || q[i] > q[big]) big = i; }
            }
            if (rest > 255 && big >= 0) { q[big] -= rest - 255; rest = 255; }
            q[fix] = 255 - rest;
            for (int i = 0; i < 8; i++) b[i] = (unsigned char)q[i];
        }
}

static bool PaintBegin(int x, int y)
{
    GhostObject* o = FindObject(g_scene.selectedId);
    if (!o || !o->tileMap.enabled) return false;                      // not a Tile Map: normal clicking
    g_painting = true;
    g_strokeHasPrev = false;
    g_paintMX = x; g_paintMY = y;
    GhostVec3 ro, rd, wp;
    float lu, lv;
    if (ScreenRay(x, y, ro, rd) && TerrainHit(*o, ro, rd, lu, lv, wp))
        g_flattenH = GhostTileMap_Height(o->tileMap, lu, lv);         // Flatten levels to the height where the stroke starts
    return true;
}

static void PaintTick(float dt)
{
    if (g_paintMode && g_playState != GhostPlayState::Editing) { g_paintMode = false; g_painting = false; }
    if (!g_painting) return;
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) { g_painting = false; return; }
    GhostObject* o = FindObject(g_scene.selectedId);
    if (!o || !o->tileMap.enabled) { g_painting = false; return; }
    GhostTileMap& t = o->tileMap;
    const int tool = g_brush.tool;
    if (tool >= 5 && t.layerCount < 2) return;                        // painting layers needs at least two
    if (dt > 0.05f) dt = 0.05f;

    GhostVec3 ro, rd, wp;
    float lu, lv;
    if (!ScreenRay(g_paintMX, g_paintMY, ro, rd) || !TerrainHit(*o, ro, rd, lu, lv, wp)) { g_strokeHasPrev = false; return; }

    float sU, sV, sN;
    TileScales(*o, sU, sV, sN);
    int n = 1;
    if (g_strokeHasPrev)                                              // fast strokes: dabs along the path, so there are no gaps
    {
        const float dx = (lu - g_prevU) * sU, dy = (lv - g_prevV) * sV;
        const float spacing = fmaxf(g_brush.radius * 0.3f, 0.05f);
        n = (int)ceilf(sqrtf(dx * dx + dy * dy) / spacing);
        n = n < 1 ? 1 : (n > 24 ? 24 : n);
    }
    int layer = CurrentFace();
    if (layer < 0) layer = 0;
    for (int i = 1; i <= n; i++)
    {
        const float f = (float)i / (float)n;
        const float u = g_strokeHasPrev ? g_prevU + (lu - g_prevU) * f : lu;
        const float v = g_strokeHasPrev ? g_prevV + (lv - g_prevV) * f : lv;
        if (tool <= 4) DabHeight(*o, u, v, dt / (float)n);
        else           DabSplat(*o, u, v, dt / (float)n, layer);
    }
    g_prevU = lu; g_prevV = lv; g_strokeHasPrev = true;
    if (tool <= 4) { GhostTileMap_UpdateRange(t); t.hStamp = GhostTile_NextStamp(); }
    else t.sStamp = GhostTile_NextStamp();
}

// The brush ring on the terrain and the mode line
static void OverlayPaintBrush(int rw, int rh)
{
    static const char* kTool[7] = { "Raise", "Lower", "Smooth", "Flatten", "Set Height", "Paint Layer", "Erase Layer" };
    const float hot[4] = { 1.0f, 0.82f, 0.30f, 1.0f }, cool[4] = { 0.40f, 0.80f, 1.0f, 1.0f };
    GhostObject* o = FindObject(g_scene.selectedId);
    if (!o || !o->tileMap.enabled)
    {
        g_ov.TextPx(14.0f, (float)rh - 106.0f, 20.0f, "PAINT MODE: click a Tile Map to select it   (P to leave)", hot);
        return;
    }
    const GhostTileMap& t = o->tileMap;
    const int tool = g_brush.tool < 0 ? 0 : (g_brush.tool > 6 ? 6 : g_brush.tool);
    int L = CurrentFace();
    if (L < 0) L = 0;
    char info[220];
    if (tool >= 5 && t.layerCount < 2)
        snprintf(info, sizeof info, "PAINT MODE  %s needs 2+ material layers (Details > Material Layers)   (P to leave)", kTool[tool]);
    else if (tool >= 5)
        snprintf(info, sizeof info, "PAINT MODE  %s: Layer %d   size %.2f   strength %d%%   fade %d%%   ([ ] size, P leave)",
            kTool[tool], L, g_brush.radius, (int)(g_brush.strength * 100.0f + 0.5f), (int)(g_brush.fade * 100.0f + 0.5f));
    else if (tool == 4)
        snprintf(info, sizeof info, "PAINT MODE  Set Height %.2f   size %.2f   strength %d%%   fade %d%%   ([ ] size, P leave)",
            g_brush.setHeight, g_brush.radius, (int)(g_brush.strength * 100.0f + 0.5f), (int)(g_brush.fade * 100.0f + 0.5f));
    else
        snprintf(info, sizeof info, "PAINT MODE  %s   size %.2f   strength %d%%   fade %d%%   ([ ] size, P leave)",
            kTool[tool], g_brush.radius, (int)(g_brush.strength * 100.0f + 0.5f), (int)(g_brush.fade * 100.0f + 0.5f));
    const float* col = tool >= 5 ? cool : hot;
    g_ov.TextPx(14.0f, (float)rh - 106.0f, 20.0f, info, col);

    if (!g_paintMouseIn || g_looking) return;
    GhostVec3 ro, rd, wp;
    float lu, lv;
    if (!ScreenRay(g_paintMX, g_paintMY, ro, rd) || !TerrainHit(*o, ro, rd, lu, lv, wp)) return;

    GhostVec3 U, V, N;
    TileAxes(t.direction, U, V, N);
    float sU, sV, sN;
    TileScales(*o, sU, sV, sN);
    const GhostMat4 M = GhostObjectModel(*o);
    auto at = [&](float u, float v) -> GhostVec3
        {
            const float h = GhostTileMap_Height(t, u, v) + 0.03f / sN;           // a hair above the surface
            return XformPoint(M, U * u + V * v + N * h, 1.0f);
        };
    auto ring = [&](float rad, float width)
        {
            GhostVec3 prev = at(lu + rad / sU, lv);
            for (int i = 1; i <= 48; i++)
            {
                const float a = (float)i / 48.0f * 2.0f * GHOST_PI;
                const GhostVec3 cur = at(lu + cosf(a) * rad / sU, lv + sinf(a) * rad / sV);
                g_ov.Line(prev, cur, col, width, false);
                prev = cur;
            }
        };
    ring(g_brush.radius, 2.2f);
    const float inner = g_brush.radius * (1.0f - g_brush.fade);               // where the full-strength core ends
    if (g_brush.fade > 0.05f && inner > 0.02f) ring(inner, 1.2f);
    g_ov.Point(at(lu, lv), 8.0f, col);
}

// Directional Lights set to follow the Sky Sphere copy its sun or moon direction and fade with the time of day
static void UpdateCelestialLights()
{
    const GhostModEntity* sky = nullptr;
    for (const GhostModEntity& m : g_scene.mods) if (m.typeId == GHOST_MOD_SKY) { sky = &m; break; }

    const float r2d = 180.0f / GHOST_PI;
    for (GhostLight& l : g_scene.lights)
    {
        l.celestialFade = 1.0f;
        if (l.celestial == 0 || l.type != GhostLightType::Directional || !sky) continue;   // no Sky Sphere: the light keeps its manual aim

        GhostVec3 travel;
        float w = 1.0f;
        if (!GhostSky_CelestialLight(*sky, l.celestial == 2, travel, w)) continue;

        const float yaw = atan2f(travel.x, travel.z) * r2d;
        const float py = travel.y < -1.0f ? -1.0f : (travel.y > 1.0f ? 1.0f : travel.y);
        const float pitch = asinf(py) * r2d;
        if (fabsf(l.yawDeg - yaw) > 0.0005f) l.yawDeg = yaw;
        if (fabsf(l.pitchDeg - pitch) > 0.0005f) l.pitchDeg = pitch;
        l.celestialFade = w;
    }
}


#include "../include/GhostEngineDataCore.h"