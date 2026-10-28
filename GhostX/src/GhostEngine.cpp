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
#include "../include/GhostOverlay.h"
#include <string.h>
#include <filesystem>
#include <map>
#include <set> 
#include <fstream>

// ---- Engine state ----
static IGhostRHI* g_rhi = nullptr;
static GhostPipeline* g_pipeline = nullptr;

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
static bool       g_dragging = false;
static int        g_dragId = 0;
static GhostVec3  g_dragOffset;
static GhostVec3  g_dragPlanePoint;
static GhostObject* FindObject(int id);
static int          CurrentFace();
static void UpdateSprites(float dt);
static void SpriteDrawInfo(const GhostObject& o, int& tex, int& cols, int& rows, int& frame);
static int PickObjectAt(int sx, int sy);
struct EngineViewport { bool alive = false; bool visible = true; int cameraId = 0; };
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
static float                    g_uiRefW = 1920.0f;       // reference width of the main view (height is 1080)
static std::string g_projectRoot;
static float g_skyScroll[2] = { 0.0f, 0.0f };      // day / night layer scroll in degrees (play only)
static std::vector<GhostAnimAsset> g_anims;                    // id = index, never removed
static std::string g_pendingScene;       // set by LoadScene(): the switch happens after the current step
static std::string g_scenePath;          // the scene file that is open (or being played)
static std::string g_playBackupPath;
static bool        g_quitRequested = false;

// Main view render size and where it sits inside the window (for mapping the mouse)
static int   g_mainRW = 1280, g_mainRH = 720;
static float g_fitX = 0.0f, g_fitY = 0.0f, g_fitW = 1280.0f, g_fitH = 720.0f;


// Forward Declarations? are interaction state
static int LoadModelFile(const std::string& absFile, std::string& err);
static std::string g_modelWarn;                     // warnings from model files loaded during a scene load
static void PerformSceneSwitch();


// ---- Gizmo drag state (13.B) ----
struct GizmoState
{
    bool active = false, local = false, snapping = false;
    int  id = 0, axis = -1, tool = 0;                    // axis: 0..2 = X Y Z, 3 = uniform scale
    GhostVec3 startPos, startRot, startScale, dir, u, v;
    float t0 = 0.0f, d0 = 0.0f, ox = 0.0f, oy = 0.0f, lastAngle = 0.0f, accum = 0.0f, value = 0.0f;
};
static GizmoState g_gizmo;
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
static const unsigned int MAX_SCENE_OBJECTS = GHOST_MAX_OBJECTS - 2 * GHOST_MAX_LIGHTS - 2 * GHOST_MAX_CAMERAS - GHOST_MAX_MODS - 4;


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
static int RegisterTexture(const char* name, const unsigned char* rgba,
    unsigned int w, unsigned int h)
{
    if (!g_rhi || !g_pipeline) return -1;
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

int GhostEngine_Init(HWND hwnd, unsigned int width, unsigned int height)
{
    g_width = width; g_height = height;
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

    }
    // ---- The default camera: always present, gets id 1 ----
    GhostScene_AddCamera(g_scene);
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

    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_prev);

    return 0;
}
static void BuildSceneCB(const GhostScene& s, GhostSceneCB& out)
{
    const float d2r = GHOST_PI / 180.0f;
    out = {};

    for (int i = 0; i < 3; i++)
        out.ambient[i] = s.ambientColor[i] * s.ambientIntensity;

    for (const GhostLight& l : s.lights)
    {
        if (out.lightCount >= GHOST_MAX_LIGHTS) break;

        float yaw = l.yawDeg * d2r, pitch = l.pitchDeg * d2r;
        GhostVec3 fwd = { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
        GhostVec3 right = { cosf(yaw), 0.0f, -sinf(yaw) };    // yaw only, so it never degenerates when facing straight down
        GhostVec3 up = fwd.Cross(right);

        GhostGPULight& g = out.lights[out.lightCount++];
        g.posRange[0] = l.position.x; g.posRange[1] = l.position.y; g.posRange[2] = l.position.z;
        g.posRange[3] = l.range;
        g.dirType[0] = fwd.x; g.dirType[1] = fwd.y; g.dirType[2] = fwd.z;
        g.dirType[3] = (float)(int)l.type;
        g.colorIntensity[0] = l.color[0]; g.colorIntensity[1] = l.color[1];
        g.colorIntensity[2] = l.color[2]; g.colorIntensity[3] = l.intensity;
        g.rectRight[0] = right.x; g.rectRight[1] = right.y; g.rectRight[2] = right.z;
        g.rectRight[3] = l.width * 0.5f;
        g.rectUp[0] = up.x; g.rectUp[1] = up.y; g.rectUp[2] = up.z;
        g.rectUp[3] = l.height * 0.5f;
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
    return nullptr;
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
    if (bestId) return bestId;

    GhostVec3 o, d;
    if (!ScreenRay(sx, sy, o, d)) return 0;
    float bestT = 1e30f;
    for (const GhostObject& ob : g_scene.objects)
    {
        float byw; GhostMat4 inv = ObjectInverse(ob, BillYaw(ob, g_mainEye, byw));
        float t;
        if (RayAABB(XformPoint(inv, o, 1.0f), XformPoint(inv, d, 0.0f),
            GhostMesh_Min(ob.meshId), GhostMesh_Max(ob.meshId), t) && t < bestT)
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
    if (LabelClick(x, y)) return;                      // clicked a dimension label or an edge: the editor asks for a value
    if (GizmoBegin(x, y)) return;                      // grabbed a gizmo handle: no picking, no free drag
    int id = PickAt(x, y);

    // Shift+click on an object: select it and pick the face under the cursor. No drag.
    // Shift+click on the face that is already targeted goes back to "All Faces".
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) && id != 0)
    {
        if (GhostObject* ob = FindObject(id))
        {
            g_scene.selectedId = id;
            CurrentFace();                         // re-syncs the owner, so a previous object's face never leaks over
            int face = PickFace(*ob, x, y);
            if (face >= 0)
                g_scene.selectedFace = (g_scene.selectedFace == face) ? -1 : face;
            g_dragging = false;
            return;
        }
    }
    g_scene.selectedId = id;
    g_dragging = false;
    if (id == 0) return;
    if (GhostObject* lo = FindObject(id)) if (lo->locked) return;     // locked: selectable, but it never starts a drag
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
    if (g_looking || !g_haveCam || g_playState != GhostPlayState::Editing) { g_hoverId = 0; return; }

    int x, y;
    if (g_gizmo.active)
    {
        PanelToRender(px, py, x, y, true);              // clamped, so a drag can run over the bars
        GizmoUpdate(x, y);
        return;
    }
    if (g_dragging)
    {
        PanelToRender(px, py, x, y, true);                    // clamped, so a drag can run over the bars
        GhostVec3* pos = EntityPosition(g_dragId);
        GhostVec3 o, d, hit;
        if (pos && ScreenRay(x, y, o, d) && RayPlane(o, d, g_dragPlanePoint, hit))
            *pos = hit + g_dragOffset;
        return;
    }
    if (!PanelToRender(px, py, x, y, false)) { g_hoverId = 0; g_gizmoHover = -1; return; }
    g_gizmoHover = GizmoHit(x, y);
    g_hoverId = PickAt(x, y);
}

void GhostEngine_MouseUp(int x, int y, int button)
{
    if (button == 0) UiMouseEvent(2, x, y);
    (void)x; (void)y;
    if (button != 0) return;
    g_gizmo.active = false;
    g_dragging = false;
    g_dragId = 0;
}

void GhostEngine_MouseLeave() { g_hoverId = 0; UiMouseEvent(3, 0, 0); }

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
    if (lightType < 0 || lightType > 2) return 0;
    GhostLight* l = GhostScene_AddLight(g_scene, (GhostLightType)lightType);
    if (!l) return 0;
    SpawnCoords(sx, sy);
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += 1.5f;
    l->position = p;
    g_scene.selectedId = l->id;
    return l->id;
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
                     l.intensity, l.range, l.width, l.height };
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
    return o.shadowMode == 1 || (o.shadowMode == 2 && o.shadowLightId == l.id);
}

static void RenderShadowPass(GhostSceneCB& cb, const GhostVec3& eye, unsigned int rw, unsigned int rh)
{
    for (unsigned int i = 0; i < GHOST_MAX_LIGHTS; i++) cb.lights[i].shadow[0] = -1.0f;   // "no shadow" by default
    cb.shadowGlobal[0] = 0.0f;
    cb.shadowGlobal[1] = (float)GHOST_SHADOW_SIZE;
    if (!g_scene.shadowsEnabled || g_scene.shadowStrength <= 0.0f || cb.lightCount == 0) return;

    bool anyLight = false;
    for (const GhostLight& l : g_scene.lights) if (l.castShadows) anyLight = true;
    if (!anyLight) return;

    const bool playing = g_playState != GhostPlayState::Editing;

    // ---- casters: written once into the shadow constant buffer, reused by every slice ----
    struct Caster { const GhostObject* o; unsigned int slot; unsigned int tex; };
    static std::vector<Caster> casters;
    casters.clear();

    GhostVec3 bmin = { 1e30f, 1e30f, 1e30f }, bmax = { -1e30f, -1e30f, -1e30f };
    unsigned int slot = 0;
    for (const GhostObject& o : g_scene.objects)
    {
        if (!o.visible && playing) continue;

        const GhostVec3 mn = GhostMesh_Min(o.meshId), mx = GhostMesh_Max(o.meshId);      // scene bounds from every visible object
        const float ex = fmaxf(fabsf(mn.x), fabsf(mx.x)) * fabsf(o.scale.x);
        const float ey = fmaxf(fabsf(mn.y), fabsf(mx.y)) * fabsf(o.scale.y);
        const float ez = fmaxf(fabsf(mn.z), fabsf(mx.z)) * fabsf(o.scale.z);
        const float r = sqrtf(ex * ex + ey * ey + ez * ez);
        bmin.x = fminf(bmin.x, o.position.x - r); bmax.x = fmaxf(bmax.x, o.position.x + r);
        bmin.y = fminf(bmin.y, o.position.y - r); bmax.y = fmaxf(bmax.y, o.position.y + r);
        bmin.z = fminf(bmin.z, o.position.z - r); bmax.z = fmaxf(bmax.z, o.position.z + r);

        if (o.shadowMode == 0 || slot >= GHOST_MAX_SHADOW_CASTERS) continue;

        GhostObjectCB c = {};
        float byw;
        c.model = GhostObjectModel(o, BillYaw(o, eye, byw));
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
        casters.push_back({ &o, slot, texIdx });
        slot++;
    }
    if (casters.empty()) return;

    const GhostVec3 center = (bmin + bmax) * 0.5f;
    float R = (bmax - bmin).Length() * 0.5f;
    R = R < 1.0f ? 1.0f : (R > 1000.0f ? 1000.0f : R);

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
        if (!l.castShadows) continue;

        bool has = false;
        for (const Caster& c : casters) if (ShadowEligible(*c.o, l)) { has = true; break; }
        if (!has) continue;

        const bool dirL = (l.type == GhostLightType::Directional);
        const int need = dirL ? 1 : 6;
        if (next + need > (int)GHOST_SHADOW_SLICES) continue;       // out of maps: this light casts no shadows

        GhostGPULight& g = cb.lights[li];
        g.shadow[0] = (float)next;

        if (dirL)
        {
            const GhostVec3 dir = LightForward(l).Normalize();
            const GhostVec3 up = fabsf(dir.y) > 0.99f ? GhostVec3(0, 0, 1) : GhostVec3(0, 1, 0);
            const GhostVec3 from = center - dir * (R * 1.1f);
            const float farW = R * 2.2f;
            const GhostMat4 vpm = GhostMat4::OrthoLH(2.0f * R, 2.0f * R, 0.0f, farW) * GhostMat4::LookAtLH(from, center, up);
            g.shadow[1] = farW;
            g.shadow[2] = (2.0f * R) / (float)GHOST_SHADOW_SIZE;
            push(l, next, vpm, center, farW, 0.0f);
        }
        else
        {
            const float farW = fmaxf(l.range, 0.5f);
            const GhostMat4 proj = GhostMat4::PerspectiveFovLH(2.0f * atanf(kTanHalf), 1.0f, 0.05f, farW);
            g.shadow[1] = farW;
            g.shadow[2] = (2.0f * kTanHalf) / (float)GHOST_SHADOW_SIZE;
            for (int f = 0; f < 6; f++)
                push(l, next + f, proj * GhostMat4::LookAtLH(l.position, l.position + kFaceDir[f], kFaceUp[f]),
                    l.position, farW, 1.0f);
        }
        next += need;
    }
    if (jobs.empty()) return;

    cb.shadowGlobal[0] = g_scene.shadowStrength;

    GhostShadowBegin(g_rhi, g_pipeline);
    for (const Job& j : jobs)
    {
        GhostShadowSlice(g_rhi, g_pipeline, j.slice, j.vp, j.posFar, j.mode);
        for (const Caster& c : casters)
            if (ShadowEligible(*c.o, *j.light)) GhostShadowDraw(g_rhi, g_pipeline, c.o->meshId, c.slot, c.tex);
    }
    GhostShadowEnd(g_rhi, g_pipeline, rw, rh);
}



// ============================================================
//  Viewport overlay: grid, selection gizmo, dimensions, FPS.
//  Main view only. Everything except the FPS counter is editor-only.
// ============================================================
static GhostViewSettings g_viewCfg = { 1, 0, 1, 1,   0, 7, 1,   0, 0,   1.0f,   20,   0, 0, 2, 2 };
static GhostOverlay      g_ov;
static float             g_fps = 0.0f;

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
    const GhostVec3 mn = GhostMesh_Min(o.meshId), mx = GhostMesh_Max(o.meshId);
    GhostVec3 c[8];
    for (int i = 0; i < 8; i++)
        c[i] = XformPoint(M, GhostVec3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z), 1.0f);
    static const int E[12][2] = { { 0,1 },{ 2,3 },{ 4,5 },{ 6,7 },{ 0,2 },{ 1,3 },{ 4,6 },{ 5,7 },{ 0,4 },{ 1,5 },{ 2,6 },{ 3,7 } };
    const float col[4] = { 1.0f, 0.62f, 0.12f, 0.85f };
    for (int e = 0; e < 12; e++) g_ov.Line(c[E[e][0]], c[E[e][1]], col, 1.5f, false);
}

// ---------------- Gizmo: shared frame, hit testing, dragging, drawing ----------------
struct GizmoFrame
{
    GhostVec3 pos;
    GhostVec3 ax[3];
    float     L = 1.0f;          // world length of the handles (constant on screen)
    int       tool = 0;          // the tool that actually applies to this entity
    bool      object = false;
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

    int tool = g_viewCfg.tool;
    if (tool == 4) tool = 0;                                   // the Dimension tool has no handles
    if (!o && (tool == 2 || tool == 3)) tool = 0;              // only objects can be rotated and scaled
    if (o && o->locked) tool = 0;                              // a locked object shows plain axes and nothing can be grabbed

    f.tool = tool;

    f.ax[0] = GhostVec3(1, 0, 0); f.ax[1] = GhostVec3(0, 1, 0); f.ax[2] = GhostVec3(0, 0, 1);
    if (o && (g_viewCfg.space == 1 || tool == 3))              // scale always uses the object's own axes
    {
        const GhostMat4 M = GhostObjectModel(*o);
        for (int k = 0; k < 3; k++) f.ax[k] = GhostVec3(M.m[0][k], M.m[1][k], M.m[2][k]).Normalize();
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
    g.active = true;
    g_gizmo = g;
    return true;                                               // the click was consumed by the gizmo
}

// Euler order is the engine's: model = Ry * Rx * Rz
static void ApplyRotation(GhostObject& o, const GizmoState& g, float rad)
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
    o.rotationDeg = GhostVec3(Round3(ex * r2d), Round3(ey * r2d), Round3(ez * r2d));
}

static void GizmoUpdate(int x, int y)
{
    GizmoState& g = g_gizmo;
    if (!g.active) return;
    GhostVec3* pos = EntityPosition(g.id);
    GhostObject* o = FindObject(g.id);
    if (!pos || ((g.tool == 2 || g.tool == 3) && !o)) { g.active = false; return; }

    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)                  // cancel: put everything back
    {
        *pos = g.startPos;
        if (o) { o->rotationDeg = g.startRot; o->scale = g.startScale; }
        g.active = false;
        return;
    }

    g.snapping = (g_gizSet.snap != 0) != ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);   // Shift flips snapping
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
            if (g_viewCfg.space == 0)                          // world axes: land on the grid lines themselves
            {
                const float a0 = g.startPos.Dot(g.dir);
                s = SnapTo(a0 + s, step) - a0;
            }
            else s = SnapTo(s, step);                          // local axes: snap the distance travelled
        }
        *pos = g.startPos + g.dir * s;
        g.value = s;
    }
    else if (g.tool == 2)
    {
        GhostVec3 hit;
        if (!RayPlanePN(ro, rd, g.startPos, g.dir, hit)) return;
        const GhostVec3 r = hit - g.startPos;
        const float a = atan2f(r.Dot(g.v), r.Dot(g.u));
        float d = a - g.lastAngle;
        while (d > GHOST_PI) d -= 2.0f * GHOST_PI;             // follow the cursor round past +-180 degrees
        while (d < -GHOST_PI) d += 2.0f * GHOST_PI;
        g.accum += d;
        g.lastAngle = a;

        float deg = g.accum * (180.0f / GHOST_PI);
        if (g.snapping) deg = SnapTo(deg, g_gizSet.rotStep);
        ApplyRotation(*o, g, deg * (GHOST_PI / 180.0f));
        g.value = deg;
    }
    else                                                       // scale
    {
        float f;
        if (g.axis == 3)
        {
            const float dx = (float)x - g.ox, dy = (float)y - g.oy;
            f = 1.0f + (sqrtf(dx * dx + dy * dy) - g.d0) / 100.0f;      // 100 pixels = +1.0
        }
        else
        {
            float t;
            if (!AxisParam(ro, rd, g.startPos, g.dir, t)) return;
            f = t / g.t0;
        }
        if (g.snapping) f = SnapTo(f, g_gizSet.scaleStep);
        f = f < 0.01f ? 0.01f : (f > 100.0f ? 100.0f : f);

        GhostVec3 ns = g.startScale;
        if (g.axis == 3) ns = g.startScale * f;
        else if (g.axis == 0) ns.x *= f;
        else if (g.axis == 1) ns.y *= f;
        else ns.z *= f;
        o->scale = ns;
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
    const GhostVec3 mn = GhostMesh_Min(o.meshId), mx = GhostMesh_Max(o.meshId);
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

static void DrawViewportOverlay(const GhostMat4& view, const GhostMat4& proj, const GhostVec3& eye,
    unsigned int rw, unsigned int rh, bool editing)
{
    if (!g_pipeline) return;

    g_ov.Begin(view, proj, rw, rh, &g_font);
    g_labelHits.clear();

    const GhostViewSettings& s = g_viewCfg;

    if (editing)
    {
        if (s.showGrid) OverlayGrid(eye);

        const int sel = g_scene.selectedId;
        if (sel != 0)
        {
            GhostObject* o = FindObject(sel);
            if (s.showSelection)
            {
                if (o) OverlayBounds(*o, eye);
                OverlayGizmo(sel);
            }
            if (o && s.showSelection)
            {
                if (o->locked)
                {
                    const float red[4] = { 1.0f, 0.45f, 0.35f, 1.0f };
                    g_ov.Label(o->position, "LOCKED", red, 15.0f, 0.0f, 26.0f);
                }
                if (o->role == GHOST_ROLE_MOVER || o->role == GHOST_ROLE_PROJECTILE)
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

    if (s.showFps)
    {
        char t[48];
        snprintf(t, sizeof t, "%.0f FPS   %.1f ms", g_fps, g_fps > 0.0f ? 1000.0f / g_fps : 0.0f);
        const float col[4] = { g_fps >= 55.0f ? 0.45f : 1.0f, g_fps >= 30.0f ? 0.95f : 0.35f, 0.35f, 1.0f };
        const float tw = GhostText_Measure(g_font, 18.0f, t);
        g_ov.TextPx((float)rw - tw - 14.0f, 46.0f, 18.0f, t, col);
    }

    GhostOverlayBegin(g_pipeline);
    GhostDrawOverlay(g_rhi, g_pipeline, g_ov.depth, 0, 0);
    GhostDrawOverlay(g_rhi, g_pipeline, g_ov.top, 1, 0);
    GhostDrawOverlay(g_rhi, g_pipeline, g_ov.text, 1, g_font.textureId);
}


// ============================================================
//  One viewport = one surface + one camera. The GPU is idle between viewports
//  (EndFrame waits), so the constant buffer slots are simply reused.
// ============================================================
static void RenderViewport(int vp)
{
    GhostCamera* cam = ViewportCamera(vp);
    if (!cam || !g_vp[vp].visible) return;
    if (!g_rhi->SetCurrentSurface(vp)) return;

    const unsigned int w = g_rhi->GetSurfaceWidth();
    const unsigned int h = g_rhi->GetSurfaceHeight();
    if (w == 0 || h == 0) return;

    const bool  mainView = (vp == 0);
    const bool overlays = mainView && g_playState == GhostPlayState::Editing;   // no icons or selection while playing
    const float d2r = GHOST_PI / 180.0f;
    // ---- Render resolution: the camera's setting, or the window size ("Match Window") ----
    const bool         custom = (cam->resW > 0 && cam->resH > 0);
    const unsigned int rw = custom ? (unsigned int)cam->resW : w;
    const unsigned int rh = custom ? (unsigned int)cam->resH : h;

    // Where the image sits in the window: aspect kept, centered
    float fx = 0.0f, fy = 0.0f, fw = (float)w, fh = (float)h;
    if (custom)
    {
        float s = fminf((float)w / (float)rw, (float)h / (float)rh);
        fw = fmaxf(floorf((float)rw * s), 1.0f);
        fh = fmaxf(floorf((float)rh * s), 1.0f);
        fx = floorf(((float)w - fw) * 0.5f);
        fy = floorf(((float)h - fh) * 0.5f);
    }

    // GPU targets follow the render size (between frames the GPU is idle)
    if (custom) { if (GhostEnsureOffscreen(g_rhi, g_pipeline, vp, rw, rh) != GHOST_OK) return; }
    else        GhostReleaseOffscreen(g_pipeline, vp);
    if (GhostEnsureDepth(g_rhi, g_pipeline, vp, rw, rh) != GHOST_OK) return;
    // ---- Matrices ----
    GhostVec3 eye = cam->position;
    GhostVec3 forward = AnglesToDir(cam->yawDeg, cam->pitchDeg);
    GhostVec3 worldUp = { 0.0f, 1.0f, 0.0f };
    GhostVec3 right = worldUp.Cross(forward).Normalize();
    GhostMat4 view = GhostMat4::LookAtLH(eye, eye + forward, worldUp);
    GhostMat4 proj = GhostMat4::PerspectiveFovLH(cam->fovDeg * d2r, (float)rw / (float)rh, 0.1f, 100.0f);

    if (mainView)                                   // picking and dragging use the main view only
    {
        g_view = view;
        g_proj = proj;
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
        g_rhi->BeginFrame(&bars, dsv);                                      // window (the bars) + depth
        GhostBeginOffscreen(g_rhi, g_pipeline, vp, &g_clearColor, rw, rh);  // scene goes into the offscreen image
    }
    else
    {
        g_rhi->BeginFrame(&g_clearColor, dsv);
    }

    GhostSceneCB sceneCB;
    BuildSceneCB(g_scene, sceneCB);
    RenderShadowPass(sceneCB, eye, rw, rh);          // renders the shadow maps and fills the shadow fields
    GhostUpdateSceneCB(g_pipeline, &sceneCB);

    // ---- Draw scene objects (one draw per run of faces that share a material) ----
    // ---- Sky: drawn first, everything else draws over it ----
    unsigned int slot = 0;
    for (const GhostModEntity& m : g_scene.mods)
    {
        if (m.typeId != GHOST_MOD_SKY) continue;
        GhostSkyDraw draws[3];
        const int n = GhostSky_Build(m, g_scene.textures, g_skyScroll[0], g_skyScroll[1], draws);
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
    for (const GhostObject& o : g_scene.objects)
    {
        if (!o.visible && g_playState != GhostPlayState::Editing) continue;   // hidden in game
        const int       faceCount = GhostMesh_FaceCount(o.meshId);
        const GhostMat4 model = GhostObjectModel(o);
        const bool      selected = overlays && (o.id == g_scene.selectedId);
        const int       hl = selected ? CurrentFace() : -1;
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
            const GhostFaceDef& a = GhostMesh_Face(o.meshId, f);
            const GhostFaceDef& b = GhostMesh_Face(o.meshId, g - 1);

            GhostObjectCB cb = {};
            cb.model = model;
            cb.view = view;
            cb.proj = proj;
            cb.color[0] = m.tint[0]; cb.color[1] = m.tint[1]; cb.color[2] = m.tint[2]; cb.color[3] = 1.0f;
            cb.useVertexColor = o.useVertexColor ? 1.0f : 0.0f;
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

            GhostUpdateObjectCB(g_pipeline, slot, &cb);
            GhostDrawMesh(g_rhi, g_pipeline, o.meshId, slot, (unsigned int)m.textureId,
                a.firstIndex, b.firstIndex + b.indexCount - a.firstIndex);
            slot++;
            f = g;
        }
    }

    // ---- Icons: main view only ----
    if (overlays)
    {
        for (const GhostLight& l : g_scene.lights)
        {
            int state = (l.id == g_scene.selectedId) ? 2 : (l.id == g_hoverId ? 1 : 0);
            GhostVec3 fwd = LightForward(l);
            DrawIcon(slot, (int)l.type, l.position, fwd, l.color, state, view, proj);
            if (l.type == GhostLightType::Directional)
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
            GhostUI_Build(a, g_font, frameQuads);
        }
        frameQuads.insert(frameQuads.end(), g_uiQuads.begin(), g_uiQuads.end());
        if (frameQuads.size() > GHOST_UI_MAX_QUADS) frameQuads.resize(GHOST_UI_MAX_QUADS);

        GhostDrawUI(g_rhi, g_pipeline, frameQuads, rw, rh, (float)rh / 1080.0f);
    }
    if (custom)
    {
        GhostEndOffscreen(g_rhi, g_pipeline, vp);
        GhostDrawBlit(g_rhi, g_pipeline, vp, fx, fy, fw, fh);
    }
    g_rhi->EndFrame();
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

        t.y += c.heightOffset;
        GhostVec3 want = t - AnglesToDir(c.yawDeg, c.pitchDeg) * c.distance;

        if (!smooth || c.smoothing <= 0.0f || !c.orbitValid) { c.orbitPos = want; c.orbitValid = true; }
        else
        {
            float k = 1.0f - expf(-c.smoothing * dt);
            c.orbitPos = c.orbitPos + (want - c.orbitPos) * k;
        }
        c.position = c.orbitPos;
        if (smooth && c.position.y < 0.3f) c.position.y = 0.3f;     // never below the ground while playing
    }
}


void GhostEngine_Tick()
{
    if (!g_rhi || !g_pipeline) return;

    // ---- Delta time ----
    LARGE_INTEGER curr;
    QueryPerformanceCounter(&curr);
    float dt = (float)(curr.QuadPart - g_prev.QuadPart) / (float)g_freq.QuadPart;
    g_prev = curr;
    if (dt > 1e-5f)
    {
        const float inst = 1.0f / dt;
        g_fps = (g_fps <= 0.0f) ? inst : g_fps + (inst - g_fps) * 0.05f;
    }
    // a drag never outlives the mouse button (lost focus, Play pressed mid-drag)
    if (g_gizmo.active && (g_playState != GhostPlayState::Editing ||
        !((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000)))
        g_gizmo.active = false;

    // ---- Play mode: sample input, then run the fixed-timestep simulation ----
    g_input.Sample(g_inputEnabled && g_playState == GhostPlayState::Playing);
    if (g_playState == GhostPlayState::Playing)
    {
        const int steps = g_stepper.Advance(dt);
        if (steps > 0)                               // hand the mouse movement collected since the last step to the game
        {
            g_apiMouseDX = g_mouseAccumX; g_apiMouseDY = g_mouseAccumY; g_apiWheel = g_wheelAccum;
            g_mouseAccumX = g_mouseAccumY = 0.0f; g_wheelAccum = 0.0f;
        }
        bool faulted = false;
        for (int i = 0; i < steps; i++)
        {
            g_uiQuads.clear();                       // each step redraws the whole HUD; a frame with no step keeps the last one
            if (!GhostGameModule_Update(GHOST_FIXED_DT))
            {
                g_playFault = "The game module crashed inside GhostGame_Update, so Play was stopped.\n"
                    "Fix the code, rebuild GhostGame and press Play again.";
                faulted = true;
                break;
            }
            g_playTime += GHOST_FIXED_DT;
            g_input.EndStep();
            g_apiMouseDX = g_apiMouseDY = 0.0f; g_apiWheel = 0.0f;    // only the first step of a frame sees the movement
            if (!g_pendingScene.empty()) break;              // switch scenes before running more steps
        }
        if (faulted) GhostEngine_Stop();
        else if (!g_pendingScene.empty()) PerformSceneSwitch();
    }

    if (g_playState == GhostPlayState::Playing)       // time of day and layer drift animate only while playing
        for (GhostModEntity& m : g_scene.mods)
            if (m.typeId == GHOST_MOD_SKY)
            {
                float ds, ns;
                GhostSky_Speeds(m, ds, ns);
                GhostSky_AdvanceTime(m, dt);
                g_skyScroll[0] = fmodf(g_skyScroll[0] + ds * dt, 360.0f);
                g_skyScroll[1] = fmodf(g_skyScroll[1] + ns * dt, 360.0f);
            }

    // ---- Active camera -> working state (the fly code below keeps using g_eye / g_yaw / g_pitch) ----
    GhostCamera* cam = InputCamera();
    if (cam)
    {
        g_eye = cam->position;
        g_yaw = cam->yawDeg * (GHOST_PI / 180.0f);
        g_pitch = cam->pitchDeg * (GHOST_PI / 180.0f);
    }
    const bool flyEnabled = g_inputEnabled && g_playState == GhostPlayState::Editing;   // the editor fly cam is off while playing
    // ---- Mouse look ----
    const float lookSens = 0.0025f;
    const float moveSpeed = 3.5f;

    const bool orbitCam = cam && cam->mode == 1;
    const bool editing = g_playState == GhostPlayState::Editing;
    const bool playing = g_playState == GhostPlayState::Playing;
    const bool lookEnabled = g_inputEnabled && (editing || playing);
    const bool applyLook = editing || orbitCam;      // while playing, only an orbit camera is turned by the engine
    const float toRad = GHOST_PI / 180.0f;

    if (lookEnabled && (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
    {
        if (!g_looking)
        {
            g_looking = true;
            GetCursorPos(&g_lastMouse);
            ShowCursor(FALSE);
        }
        else
        {
            POINT cur;
            GetCursorPos(&cur);
            int dx = cur.x - g_lastMouse.x;
            int dy = cur.y - g_lastMouse.y;
            if (playing) { g_mouseAccumX += (float)dx; g_mouseAccumY += (float)dy; }     // the game can read it too

            if (applyLook)
            {
                g_yaw += dx * lookSens;
                g_pitch -= dy * lookSens;
                float hi = orbitCam ? cam->maxPitchDeg * toRad : GHOST_PI * 0.49f;
                float lo = orbitCam ? cam->minPitchDeg * toRad : -GHOST_PI * 0.49f;
                if (g_pitch > hi) g_pitch = hi;
                if (g_pitch < lo) g_pitch = lo;
            }
            SetCursorPos(g_lastMouse.x, g_lastMouse.y);
        }
    }
    else if (g_looking)
    {
        g_looking = false;
        ShowCursor(TRUE);
    }


    auto down = [&](int vk) { return flyEnabled && (GetAsyncKeyState(vk) & 0x8000) != 0; };
    // ---- Movement ----
    GhostVec3 forward = {
        cosf(g_pitch) * sinf(g_yaw),
        sinf(g_pitch),
        cosf(g_pitch) * cosf(g_yaw)
    };
    forward = forward.Normalize();
    GhostVec3 worldUp = { 0.0f, 1.0f, 0.0f };
    GhostVec3 right = worldUp.Cross(forward).Normalize();   // was forward.Cross(worldUp)
    float speed = moveSpeed * dt;

    if (down('W'))        g_eye = g_eye + forward * speed;
    if (down('S'))        g_eye = g_eye - forward * speed;
    if (down('A'))        g_eye = g_eye - right * speed;
    if (down('D'))        g_eye = g_eye + right * speed;
    if (down(VK_SPACE) || down('E')) g_eye.y += speed;
    if (down('Q'))                   g_eye.y -= speed;
    // ---- Working state -> active camera ----
    if (cam)
    {
        cam->position = g_eye;
        cam->yawDeg = fmodf(g_yaw * (180.0f / GHOST_PI), 360.0f);
        cam->pitchDeg = g_pitch * (180.0f / GHOST_PI);
    }
    UpdateOrbitCameras(dt, g_playState != GhostPlayState::Editing);
    UpdateSprites(dt);                                // animation clips run in the editor and while playing
    // ---- Render: camera windows first, the main view last (it carries the vsync) ----
    for (int vp = 1; vp < GHOST_MAX_SURFACES; vp++)
        if (g_vp[vp].alive) RenderViewport(vp);
    RenderViewport(0);
    g_rhi->SetCurrentSurface(0);
}

void GhostEngine_Resize(unsigned int width, unsigned int height)
{
    if (!g_rhi || !g_pipeline) return;
    if (width == 0 || height == 0) return;
    g_width = width;
    g_height = height;
    g_rhi->SetCurrentSurface(0);
    g_rhi->Resize(width, height);
    GhostResizeDepthBuffer(g_rhi, g_pipeline, 0, width, height);
}

void GhostEngine_Shutdown()
{
    GhostGameModule_Unload();
    GhostPipelineDestroy(g_pipeline);
    g_pipeline = nullptr;

    if (g_rhi)
    {
        g_rhi->Shutdown();
        delete g_rhi;
        g_rhi = nullptr;
    }
}

// ============================================================
//  Exported: faces / materials
// ============================================================
int GhostEngine_GetFaceCount(int objectId)
{
    GhostObject* o = FindObject(objectId);
    return o ? GhostMesh_FaceCount(o->meshId) : 0;
}

int GhostEngine_GetFaceName(int objectId, int face, char* buf, int bufSize)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !buf || bufSize <= 0 || face < 0 || face >= GhostMesh_FaceCount(o->meshId)) return 0;
    strncpy_s(buf, bufSize, GhostMesh_Face(o->meshId, face).name.c_str(), _TRUNCATE);
    return 1;
}

int GhostEngine_GetMaterial(int objectId, int face, GhostMaterialInfo* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    if (face < 0) face = 0;
    if (face >= GhostMesh_FaceCount(o->meshId)) return 0;
    const GhostMaterial& m = o->faces[face];
    *out = { m.textureId, m.mapMode, m.wrap, m.filter,
             m.tiling[0], m.tiling[1], m.offset[0], m.offset[1], m.rotationDeg,
             m.tint[0], m.tint[1], m.tint[2],
             m.metallic, m.roughness, m.specular,
             m.emissive[0], m.emissive[1], m.emissive[2], m.emissiveStrength };
    return 1;
}

void GhostEngine_SetMaterial(int objectId, int face, const GhostMaterialInfo* in)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !in) return;
    const int count = GhostMesh_FaceCount(o->meshId);
    if (face >= count) return;

    auto nz = [](float v) { return fabsf(v) < 0.001f ? (v < 0.0f ? -0.001f : 0.001f) : v; };
    auto c01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };

    GhostMaterial m;
    m.textureId = (in->textureId >= 0 && in->textureId < (int)g_scene.textures.size()) ? in->textureId : 0;
    m.mapMode = in->mapMode < 0 ? 0 : (in->mapMode > 7 ? 7 : in->mapMode);
    m.wrap = in->wrap ? 1 : 0;
    m.filter = in->filter ? 1 : 0;
    m.tiling[0] = nz(in->tilingU); m.tiling[1] = nz(in->tilingV);
    m.offset[0] = in->offsetU;     m.offset[1] = in->offsetV;
    m.rotationDeg = in->rotationDeg;
    m.tint[0] = c01(in->r); m.tint[1] = c01(in->g); m.tint[2] = c01(in->b);
    m.metallic = c01(in->metallic);
    m.roughness = in->roughness < 0.02f ? 0.02f : (in->roughness > 1.0f ? 1.0f : in->roughness);
    m.specular = c01(in->specular);
    m.emissive[0] = c01(in->emR); m.emissive[1] = c01(in->emG); m.emissive[2] = c01(in->emB);
    m.emissiveStrength = in->emissiveStrength < 0.0f ? 0.0f : (in->emissiveStrength > 20.0f ? 20.0f : in->emissiveStrength);
    const int from = face < 0 ? 0 : face;
    const int to = face < 0 ? count - 1 : face;
    for (int f = from; f <= to; f++) o->faces[f] = m;
    if (o->meshId == GHOST_MESH_QUAD) o->sprite.textureId = m.textureId;
}

int GhostEngine_GetTargetFace() { return CurrentFace(); }

void GhostEngine_SetTargetFace(int face)
{
    CurrentFace();
    GhostObject* o = FindObject(g_scene.selectedId);
    g_scene.selectedFace = (o && face >= 0 && face < GhostMesh_FaceCount(o->meshId)) ? face : -1;
}


// ============================================================
//  Exported: texture replace + drop picking
// ============================================================
int GhostEngine_ReplaceTexture(int id, const char* name, const unsigned char* rgba,
    unsigned int width, unsigned int height)
{
    if (!g_rhi || !g_pipeline) return 0;
    if (id < 3 || id >= (int)g_scene.textures.size()) return 0;          // built-ins are fixed
    if (!rgba || width == 0 || height == 0 || width > 4096 || height > 4096) return 0;

    if (GhostCreateTexture(g_rhi, g_pipeline, rgba, width, height, id) != id) return 0;

    GhostTextureInfo& t = g_scene.textures[id];
    t.width = width;
    t.height = height;
    if (name) t.name = name;
    return 1;
}

static int PickObjectAt(int sx, int sy)
{
    GhostVec3 o, d;
    if (!ScreenRay(sx, sy, o, d)) return 0;

    int   bestId = 0;
    float bestT = 1e30f;
    for (const GhostObject& ob : g_scene.objects)
    {
        float byw; GhostMat4 inv = ObjectInverse(ob, BillYaw(ob, g_mainEye, byw));
        float t;
        if (RayAABB(XformPoint(inv, o, 1.0f), XformPoint(inv, d, 0.0f),
            GhostMesh_Min(ob.meshId), GhostMesh_Max(ob.meshId), t) && t < bestT)
        {
            bestT = t; bestId = ob.id;
        }
    }
    return bestId;
}

int GhostEngine_PickObjectAt(int px, int py, int* outFace)
{
    if (outFace) *outFace = -1;
    int x, y;
    if (!PanelToRender(px, py, x, y, false)) return 0;
    int id = PickObjectAt(x, y);
    if (id == 0) return 0;
    if (outFace)
        if (GhostObject* ob = FindObject(id)) *outFace = PickFace(*ob, x, y);
    return id;
}

// ============================================================
//  Exported: cameras
// ============================================================
int GhostEngine_AddCamera(int mode, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    GhostCamera* c = GhostScene_AddCamera(g_scene);
    if (!c) return 0;

    SpawnCoords(sx, sy);
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += 1.5f;
    c->position = p;
    if (const GhostCamera* a = ActiveCamera())          // face the same way as the main view
    {
        c->yawDeg = a->yawDeg; c->pitchDeg = a->pitchDeg; c->fovDeg = a->fovDeg;
    }
    if (mode == 1)
    {
        c->mode = 1;
        c->name = "Orbit Camera " + std::to_string(c->id);
        if (sx >= 0)                                    // dropped onto an object: lock to it
        {
            int hit = PickObjectAt(sx, sy);
            if (hit) c->targetId = hit;
        }
    }
    g_scene.selectedId = c->id;
    return c->id;
}

int GhostEngine_GetCamera(int id, GhostCameraInfo* out)
{
    if (!out) return 0;
    for (const GhostCamera& c : g_scene.cameras)
        if (c.id == id)
        {
            *out = { c.id, c.position.x, c.position.y, c.position.z,
                     c.yawDeg, c.pitchDeg, c.fovDeg, c.id == g_scene.activeCameraId ? 1 : 0,
                     c.resW, c.resH,
                     c.mode, c.targetId,
                     c.distance, c.heightOffset, c.minDist, c.maxDist,
                     c.minPitchDeg, c.maxPitchDeg, c.smoothing };
            return 1;
        }
    return 0;
}

void GhostEngine_SetCamera(const GhostCameraInfo* in)
{
    if (!in) return;
    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    for (GhostCamera& c : g_scene.cameras)
        if (c.id == in->id)
        {
            c.position = { in->px, in->py, in->pz };
            c.yawDeg = in->yawDeg;
            c.pitchDeg = clampf(in->pitchDeg, -88.0f, 88.0f);
            c.fovDeg = clampf(in->fovDeg, 10.0f, 120.0f);
            if (in->resW <= 0 || in->resH <= 0) { c.resW = 0; c.resH = 0; }
            else { c.resW = (int)clampf((float)in->resW, 16.0f, 4096.0f); c.resH = (int)clampf((float)in->resH, 16.0f, 4096.0f); }

            c.mode = in->mode == 1 ? 1 : 0;
            c.targetId = in->targetId > 0 ? in->targetId : 0;
            c.minDist = clampf(in->minDist, 0.3f, 100.0f);
            c.maxDist = clampf(in->maxDist, c.minDist, 100.0f);
            c.distance = clampf(in->distance, c.minDist, c.maxDist);
            c.heightOffset = clampf(in->heightOffset, -20.0f, 20.0f);
            c.minPitchDeg = clampf(in->minPitchDeg, -88.0f, 88.0f);
            c.maxPitchDeg = clampf(in->maxPitchDeg, c.minPitchDeg, 88.0f);
            c.smoothing = clampf(in->smoothing, 0.0f, 50.0f);
            return;
        }
}

void GhostEngine_SetActiveCamera(int id)
{
    for (const GhostCamera& c : g_scene.cameras)
        if (c.id == id)
        {
            if (g_scene.activeCameraId != id) { g_scene.activeCameraId = id; g_scene.version++; }
            return;
        }
}

int GhostEngine_GetActiveCamera() { return g_scene.activeCameraId; }


// ============================================================
//  Exported: viewports
// ============================================================
int GhostEngine_CreateViewport(HWND hwnd, unsigned int width, unsigned int height, int cameraId)
{
    if (!g_rhi || !g_pipeline || !FindCamera(cameraId)) return 0;
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    int id = g_rhi->CreateSurface(hwnd, width, height);
    if (id <= 0) return 0;
    if (GhostResizeDepthBuffer(g_rhi, g_pipeline, id, width, height) != GHOST_OK)
    {
        g_rhi->DestroySurface(id);
        return 0;
    }
    g_vp[id].alive = true;
    g_vp[id].visible = true;
    g_vp[id].cameraId = cameraId;
    return id;
}

void GhostEngine_DestroyViewport(int vp)
{
    if (!g_rhi || !g_pipeline || vp <= 0 || vp >= GHOST_MAX_SURFACES) return;
    g_rhi->DestroySurface(vp);
    GhostReleaseDepth(g_pipeline, vp);
    GhostReleaseOffscreen(g_pipeline, vp);
    g_vp[vp] = EngineViewport();
}

int GhostEngine_RebindViewport(int vp, HWND hwnd, unsigned int width, unsigned int height)
{
    if (!g_rhi || !g_pipeline || vp < 0 || vp >= GHOST_MAX_SURFACES || !g_vp[vp].alive) return 0;
    if (width == 0) width = 1;
    if (height == 0) height = 1;
    if (g_rhi->RebindSurface(vp, hwnd, width, height) != GHOST_OK) return 0;
    GhostResizeDepthBuffer(g_rhi, g_pipeline, vp, width, height);
    if (vp == 0) { g_width = width; g_height = height; }
    return 1;
}

void GhostEngine_ResizeViewport(int vp, unsigned int width, unsigned int height)
{
    if (vp == 0) { GhostEngine_Resize(width, height); return; }
    if (!g_rhi || !g_pipeline || vp < 0 || vp >= GHOST_MAX_SURFACES || !g_vp[vp].alive) return;
    if (width == 0 || height == 0) return;
    g_rhi->ResizeSurface(vp, width, height);
    GhostResizeDepthBuffer(g_rhi, g_pipeline, vp, width, height);
}

void GhostEngine_SetViewportVisible(int vp, int visible)
{
    if (vp <= 0 || vp >= GHOST_MAX_SURFACES) return;       // the main view is always on
    g_vp[vp].visible = (visible != 0);
}

void GhostEngine_SetInputViewport(int vp)
{
    if (vp < 0 || vp >= GHOST_MAX_SURFACES || !g_vp[vp].alive) { g_inputEnabled = false; return; }
    g_inputEnabled = true;
    if (GhostCamera* c = ViewportCamera(vp)) g_inputCameraId = c->id;
}

// ============================================================
//  Exported: scene files
// ============================================================


static void CopyMsg(const std::string& m, char* buf, int size)
{
    if (buf && size > 0) strncpy_s(buf, size, m.c_str(), _TRUNCATE);
}

// Used by the scene loader: reuse a texture that is already on the GPU, otherwise decode and upload it
static int LoadSceneTexture(const std::string& absPath, const std::string& name)
{
    for (const GhostTextureInfo& t : g_scene.textures)
        if (t.id >= GHOST_FIRST_USER_TEXTURE && !t.path.empty() && _stricmp(t.path.c_str(), absPath.c_str()) == 0)
            return t.id;

    std::vector<unsigned char> px;
    unsigned int w = 0, h = 0;
    if (!GhostImage_Load(absPath, 4096, px, w, h)) return 0;

    int id = RegisterTexture(name.c_str(), px.data(), w, h);
    if (id < 0) return 0;
    g_scene.textures[id].path = absPath;
    return id;
}

void GhostEngine_SetProjectRoot(const char* path)
{
    g_projectRoot = GhostPath_Normalize(path ? path : "");
}

int GhostEngine_SaveScene(const char* path, char* msg, int msgSize)
{
    GhostEngine_Stop();       // saving always works on the real scene, never the play copy

    std::string err;
    if (!path || !GhostScene_Save(g_scene, g_projectRoot, path, err))
    {
        CopyMsg(err.empty() ? "Save failed." : err, msg, msgSize);
        return 0;
    }
    g_scenePath = GhostPath_Normalize(path);
    return 1;
}

int GhostEngine_LoadScene(const char* path, char* msg, int msgSize)
{
    if (!g_rhi || !g_pipeline || !path) { CopyMsg("The engine is not ready.", msg, msgSize); return 0; }
    GhostEngine_Stop();
    g_modelWarn.clear();
    GhostScene backup = g_scene;
    std::string err, warn;
    bool ok = GhostScene_Load(g_scene, g_projectRoot, path, LoadSceneTexture,
        MAX_SCENE_OBJECT_COUNT, err, warn, LoadModelFile);
    if (!ok)
    {
        // Restore everything except the texture registry: textures loaded during the attempt stay on the GPU
        std::vector<GhostTextureInfo> tex = g_scene.textures;
        g_scene = backup;
        g_scene.textures = tex;
        CopyMsg(err, msg, msgSize);
        return 0;
    }
    warn += g_modelWarn;
    g_hoverId = 0; g_dragging = false; g_dragId = 0; g_inputCameraId = 0;
    g_scenePath = GhostPath_Normalize(path);
    g_scene.version++;
    CopyMsg(warn, msg, msgSize);
    return warn.empty() ? 1 : 2;
}

void GhostEngine_ClearScene()
{
    GhostEngine_Stop();
    g_scene.objects.clear(); g_scenePath.clear();
    g_scene.lights.clear();
    g_scene.cameras.clear(); g_scene.mods.clear();
    g_scene.ambientColor[0] = g_scene.ambientColor[1] = g_scene.ambientColor[2] = 1.0f;
    g_scene.ambientIntensity = 0.15f;    g_scene.shadowsEnabled = true;
    g_scene.shadowStrength = 1.0f;
    g_scene.nextId = 1;
    g_scene.selectedId = 0;
    g_scene.selectedFace = -1;
    g_scene.activeCameraId = 0;
    GhostScene_AddCamera(g_scene);            // the default camera, id 1
    g_hoverId = 0; g_dragging = false; g_dragId = 0; g_inputCameraId = 0;
    g_scene.version++;
}

unsigned int GhostEngine_GetSceneHash()
{
    return GhostScene_Hash(g_playState != GhostPlayState::Editing ? g_playBackup : g_scene);
}

void GhostEngine_SetTexturePath(int id, const char* path)
{
    if (id < GHOST_FIRST_USER_TEXTURE || id >= (int)g_scene.textures.size() || !path) return;
    g_scene.textures[id].path = GhostPath_Normalize(path);
}

int GhostEngine_GetTexturePath(int id, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    buf[0] = 0;
    if (id < 0 || id >= (int)g_scene.textures.size()) return 0;
    CopyMsg(g_scene.textures[id].path, buf, bufSize);
    return g_scene.textures[id].path.empty() ? 0 : 1;
}

// ============================================================
//  Game API: what GhostGame.dll receives (see GhostGameAPI.h)
// ============================================================
static void   Api_Log(const char* m) { OutputDebugStringA("[Game] "); OutputDebugStringA(m ? m : ""); OutputDebugStringA("\n"); }
static double Api_GetTime() { return g_playTime; }
static float  Api_GetDt() { return GHOST_FIXED_DT; }
static void   Api_SetHud(const char* t) { g_hudText = t ? t : ""; if (g_hudText.size() > 250) g_hudText.resize(250); }

static int Api_KeyDown(int vk) { return (vk > 0 && vk < 256 && g_input.down[vk]) ? 1 : 0; }
static int Api_KeyPressed(int vk) { return (vk > 0 && vk < 256 && g_input.pressed[vk]) ? 1 : 0; }
static int Api_KeyReleased(int vk) { return (vk > 0 && vk < 256 && g_input.released[vk]) ? 1 : 0; }

// is this correct palceemnts?
static void  Api_GetMouseDelta(float* v) { if (v) { v[0] = g_apiMouseDX; v[1] = g_apiMouseDY; } }
static float Api_GetMouseWheel() { return g_apiWheel; }
static int   Api_GetCameraMode(int id) { GhostCamera* c = FindCamera(id); return c ? c->mode : -1; }
static int   Api_GetCameraTarget(int id) { GhostCamera* c = FindCamera(id); return c ? c->targetId : 0; }
static int   Api_SetCameraTarget(int id, int obj)
{
    GhostCamera* c = FindCamera(id);
    if (!c || (obj != 0 && !FindObject(obj))) return 0;
    c->targetId = obj;
    c->orbitValid = false;
    return 1;
}
// end

static int Api_FindByTag(const char* tag)
{
    if (!tag) return 0;
    for (const GhostObject& o : g_scene.objects) if (GhostObject_Tag(o) == tag) return o.id;
    return 0;
}

static int Api_FindAllByTag(const char* tag, int* out, int maxCount)
{
    if (!tag || !out || maxCount <= 0) return 0;
    int n = 0;
    for (const GhostObject& o : g_scene.objects)
        if (GhostObject_Tag(o) == tag) { out[n++] = o.id; if (n >= maxCount) break; }
    return n;
}

static int Api_GetTag(int id, char* buf, int size)
{
    GhostObject* o = FindObject(id);
    if (!o || !buf || size <= 0) return 0;
    CopyMsg(GhostObject_Tag(*o), buf, size);
    return 1;
}

static int Api_SetTag(int id, const char* tag)
{
    GhostObject* o = FindObject(id);
    if (!o) return 0;
    std::string t = tag ? tag : "";
    static const char* names[GHOST_ROLE_COUNT] = { "", "Player", "Enemy", "Coin", "Block", "Goal", nullptr,
        "PortalEntry", "PortalExit", "Breakable", "Mover", "Projectile" };
    o->role = GHOST_ROLE_CUSTOM;
    for (int i = 0; i < GHOST_ROLE_COUNT; i++) if (names[i] && t == names[i]) { o->role = i; break; }
    if (o->role == GHOST_ROLE_CUSTOM) { if (t.size() > 31) t.resize(31); o->customTag = t; }
    return 1;
}

static int Api_GetPosition(int id, float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; v[0] = o->position.x; v[1] = o->position.y; v[2] = o->position.z; return 1;
}
static int Api_SetPosition(int id, const float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; o->position = { v[0], v[1], v[2] }; return 1;
}
static int Api_GetRotation(int id, float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; v[0] = o->rotationDeg.x; v[1] = o->rotationDeg.y; v[2] = o->rotationDeg.z; return 1;
}
static int Api_SetRotation(int id, const float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; o->rotationDeg = { v[0], v[1], v[2] }; return 1;
}
static int Api_GetScale(int id, float* v)
{
    GhostObject* o = FindObject(id); if (!o || !v) return 0; v[0] = o->scale.x; v[1] = o->scale.y; v[2] = o->scale.z; return 1;
}
static int Api_SetScale(int id, const float* v)
{
    GhostObject* o = FindObject(id);
    if (!o || !v) return 0;
    auto sc = [](float s) { return fabsf(s) < 0.001f ? 0.001f : s; };
    o->scale = { sc(v[0]), sc(v[1]), sc(v[2]) };
    return 1;
}

static int Api_GetColor(int id, float* v)
{
    GhostObject* o = FindObject(id);
    if (!o || !v) return 0;
    const float* c = (o->meshId == GHOST_MESH_QUAD) ? o->sprite.tint : o->faces[0].tint;
    v[0] = c[0]; v[1] = c[1]; v[2] = c[2];
    return 1;
}
static int Api_SetColor(int id, const float* v)
{
    GhostObject* o = FindObject(id);
    if (!o || !v) return 0;
    auto c01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
    for (GhostMaterial& m : o->faces) { m.tint[0] = c01(v[0]); m.tint[1] = c01(v[1]); m.tint[2] = c01(v[2]); }
    if (o->meshId == GHOST_MESH_QUAD) for (int i = 0; i < 3; i++) o->sprite.tint[i] = c01(v[i]);
    o->useVertexColor = false;
    return 1;
}
static int Api_GetVisible(int id) { GhostObject* o = FindObject(id); return (o && o->visible) ? 1 : 0; }
static int Api_SetVisible(int id, int vis) { GhostObject* o = FindObject(id); if (!o) return 0; o->visible = (vis != 0); return 1; }

static bool ColliderBounds(const GhostObject& o, GhostVec3& mn, GhostVec3& mx)
{
    if (!o.collider) return false;
    GhostVec3 h = { fabsf(o.colliderSize.x * o.scale.x) * 0.5f,
                    fabsf(o.colliderSize.y * o.scale.y) * 0.5f,
                    fabsf(o.colliderSize.z * o.scale.z) * 0.5f };
    GhostVec3 c = { o.position.x + o.colliderOffset.x * o.scale.x,
                    o.position.y + o.colliderOffset.y * o.scale.y,
                    o.position.z + o.colliderOffset.z * o.scale.z };
    mn = c - h;
    mx = c + h;
    return true;
}

static int Api_GetBounds(int id, float* mn3, float* mx3)
{
    GhostObject* o = FindObject(id);
    GhostVec3 mn, mx;
    if (!o || !mn3 || !mx3 || !ColliderBounds(*o, mn, mx)) return 0;
    mn3[0] = mn.x; mn3[1] = mn.y; mn3[2] = mn.z;
    mx3[0] = mx.x; mx3[1] = mx.y; mx3[2] = mx.z;
    return 1;
}

static int Api_QueryBox(const float* bmin, const float* bmax, int* out, int maxCount)
{
    if (!bmin || !bmax || !out || maxCount <= 0) return 0;
    int n = 0;
    for (const GhostObject& o : g_scene.objects)
    {
        if (!o.visible) continue;
        GhostVec3 mn, mx;
        if (!ColliderBounds(o, mn, mx)) continue;
        if (mn.x < bmax[0] && mx.x > bmin[0] && mn.y < bmax[1] && mx.y > bmin[1] && mn.z < bmax[2] && mx.z > bmin[2])
        {
            out[n++] = o.id;
            if (n >= maxCount) break;
        }
    }
    return n;
}

static int Api_Spawn(int meshId, const float* pos)
{
    if (!CanAddObject(meshId)) return 0;
    GhostObject* o = GhostScene_AddObject(g_scene, meshId);
    if (!o) return 0;
    if (pos) o->position = { pos[0], pos[1], pos[2] };
    return o->id;
}

static int Api_Destroy(int id)
{
    if (!FindObject(id)) return 0;                     // objects only: lights and cameras can't be destroyed from here
    return GhostScene_Remove(g_scene, id) ? 1 : 0;
}

static int Api_GetActiveCamera() { return g_scene.activeCameraId; }
static int Api_SetActiveCamera(int id)
{
    if (!FindCamera(id)) return 0;
    GhostEngine_SetActiveCamera(id);
    return 1;
}

static int Api_GetCamera(int id, float* pos, float* yp, float* fov)
{
    GhostCamera* c = FindCamera(id);
    if (!c) return 0;
    if (pos) { pos[0] = c->position.x; pos[1] = c->position.y; pos[2] = c->position.z; }
    if (yp) { yp[0] = c->yawDeg; yp[1] = c->pitchDeg; }
    if (fov) *fov = c->fovDeg;
    return 1;
}

static int Api_SetCamera(int id, const float* pos, const float* yp, const float* fov)
{
    GhostCamera* c = FindCamera(id);
    if (!c) return 0;
    if (pos) c->position = { pos[0], pos[1], pos[2] };
    if (yp) { c->yawDeg = yp[0]; c->pitchDeg = yp[1] < -88.0f ? -88.0f : (yp[1] > 88.0f ? 88.0f : yp[1]); }
    if (fov) c->fovDeg = *fov < 10.0f ? 10.0f : (*fov > 120.0f ? 120.0f : *fov);
    return 1;
}
static const float kUIWhite[4] = { 1, 1, 1, 1 };

static void PushQuad(int tex, float x, float y, float w, float h, const float* c)
{
    if (g_uiQuads.size() >= GHOST_UI_MAX_QUADS) return;
    if (!c) c = kUIWhite;
    GhostUIQuad q;
    q.x = x; q.y = y; q.w = w; q.h = h;
    q.u0 = 0.0f; q.v0 = 0.0f; q.u1 = 1.0f; q.v1 = 1.0f;
    q.color[0] = c[0]; q.color[1] = c[1]; q.color[2] = c[2]; q.color[3] = c[3];
    q.textureId = tex;
    g_uiQuads.push_back(q);
}

static void  Api_UIRect(float x, float y, float w, float h, const float* c) { PushQuad(0, x, y, w, h, c); }
static void  Api_UIImage(int tex, float x, float y, float w, float h, const float* c) { PushQuad(tex, x, y, w, h, c); }
static void  Api_UIText(float x, float y, float size, const char* t, const float* c)
{
    if (!t) return;
    GhostText_Draw(g_uiQuads, g_font, x, y, size, t, c ? c : kUIWhite);
}
static float Api_UIMeasure(const char* t, float size) { return GhostText_Measure(g_font, size, t); }
static void  Api_UIScreenSize(float* wh) { if (wh) { wh[0] = g_uiRefW; wh[1] = 1080.0f; } }

static int Api_LoadTexture(const char* rel)
{
    if (!rel || !*rel) return 0;
    std::string p = rel;
    const bool absolute = p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
    if (!absolute && !g_projectRoot.empty()) p = g_projectRoot + "\\" + p;
    p = GhostPath_Normalize(p);
    const size_t sl = p.find_last_of("\\/");
    return LoadSceneTexture(p, sl == std::string::npos ? p : p.substr(sl + 1));
}

static GhostUIAsset* UiAsset(int h)
{
    return (h >= 1 && h <= (int)g_uiAssets.size()) ? &g_uiAssets[(size_t)h - 1] : nullptr;
}

static GhostWidget* UiWidget(int h, const char* name)
{
    GhostUIAsset* a = UiAsset(h);
    if (!a || !name) return nullptr;
    for (GhostWidget& w : a->widgets) if (w.name == name) return &w;
    return nullptr;
}

static int Api_UILoad(const char* rel)
{
    if (!rel || !*rel) return 0;
    std::string p = rel;
    const bool absolute = p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
    if (!absolute && !g_projectRoot.empty()) p = g_projectRoot + "\\" + p;
    p = GhostPath_Normalize(p);

    for (size_t i = 0; i < g_uiAssets.size(); i++)                        // the same file twice = the same handle
        if (_stricmp(g_uiAssets[i].path.c_str(), p.c_str()) == 0) return (int)i + 1;
    if (g_uiAssets.size() >= 16) return 0;

    GhostUIAsset a;
    std::string err, warn;
    if (!GhostUI_Load(a, g_projectRoot, p, LoadSceneTexture, err, warn))
    {
        Api_Log(("UILoad failed: " + err).c_str());
        return 0;
    }
    if (!warn.empty()) Api_Log(("UILoad: " + warn).c_str());
    a.path = p;
    g_uiAssets.push_back(std::move(a));
    return (int)g_uiAssets.size();
}

static void Api_UIShow(int h, int vis) { if (GhostUIAsset* a = UiAsset(h)) a->shown = (vis != 0); }

static int Api_UISetText(int h, const char* name, const char* t)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w) return 0;
    std::string s = t ? t : "";
    if (s.size() > 255) s.resize(255);
    w->text = s;
    return 1;
}

static int Api_UISetProgress(int h, const char* name, float v)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w) return 0;
    w->value = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return 1;
}

static int Api_UISetVisible(int h, const char* name, int vis)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w) return 0;
    w->visible = (vis != 0);
    return 1;
}

static int Api_UISetColor(int h, const char* name, const float* c)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w || !c) return 0;
    for (int i = 0; i < 4; i++) w->color[i] = c[i];
    return 1;
}

static int Api_UIButtonClicked(int h, const char* name)
{
    GhostWidget* w = UiWidget(h, name);
    if (!w || !w->clicked) return 0;
    w->clicked = false;
    return 1;
}

static int Api_UIButtonDown(int h, const char* name)
{
    GhostWidget* w = UiWidget(h, name);
    return (w && w->pressed) ? 1 : 0;
}

static int Api_UIGetMouse(float* xy)
{
    if (xy) { xy[0] = g_uiMouseX; xy[1] = g_uiMouseY; }
    return g_uiMouseInside ? 1 : 0;
}
static float Api_SkyGetTime()
{
    for (const GhostModEntity& m : g_scene.mods) if (m.typeId == GHOST_MOD_SKY) return GhostSky_GetTime(m);
    return 0.0f;
}
static void Api_SkySetTime(float h)
{
    for (GhostModEntity& m : g_scene.mods) if (m.typeId == GHOST_MOD_SKY) { GhostSky_SetTime(m, h); return; }
}

// ============================================================
//  Sprites and animations
// ============================================================
static std::string StoredToAbs(const std::string& stored)
{
    std::string p = stored;
    const bool absolute = p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
    if (!absolute && !g_projectRoot.empty()) p = g_projectRoot + "\\" + p;
    return GhostPath_Normalize(p);
}

static int LoadAnimFile(const std::string& stored)
{
    const std::string abs = StoredToAbs(stored);
    for (size_t i = 0; i < g_anims.size(); i++)
        if (_stricmp(g_anims[i].path.c_str(), abs.c_str()) == 0) return (int)i;
    if (g_anims.size() >= 64) return -1;

    GhostAnimAsset a;
    std::string err, warn;
    if (!GhostAnim_Load(a, g_projectRoot, abs, LoadSceneTexture, err, warn))
    {
        OutputDebugStringA(("[GhostX] Animation load failed: " + err + "\n").c_str());
        return -1;
    }
    if (!warn.empty()) OutputDebugStringA(("[GhostX] Animation: " + warn).c_str());
    a.path = abs;
    g_anims.push_back(std::move(a));
    return (int)g_anims.size() - 1;
}

// Re-reads every loaded .ghostanim (ids stay the same). Runs at every Play, and from Play > Reload Animations.
static void ReloadAnims()
{
    for (GhostAnimAsset& a : g_anims)
    {
        GhostAnimAsset n;
        std::string err, warn;
        if (GhostAnim_Load(n, g_projectRoot, a.path, LoadSceneTexture, err, warn)) { n.path = a.path; a = std::move(n); }
        else OutputDebugStringA(("[GhostX] Animation reload failed: " + err + "\n").c_str());
    }
}

// Replace the existing SpriteDrawInfo function in GhostEngine.cpp with this:

static void SpriteDrawInfo(const GhostObject& o, int& tex, int& cols, int& rows, int& frame)
{
    const GhostSprite& s = o.sprite;
    tex = s.textureId;
    cols = s.source == 0 ? 1 : s.cols;
    rows = s.source == 0 ? 1 : s.rows;

    if (s.animId >= 0 && s.animId < (int)g_anims.size())
    {
        const GhostAnimAsset& a = g_anims[(size_t)s.animId];

        // Find the active clip — same logic as UpdateSprites
        int ci = GhostAnim_FindClip(a, s.clip.c_str());
        if (ci < 0) ci = 0;

        if (ci < (int)a.clips.size())
        {
            const GhostClip& c = a.clips[(size_t)ci];
            if (c.textureId > 0) tex = c.textureId;
            cols = c.source == 0 ? 1 : c.cols;
            rows = c.source == 0 ? 1 : c.rows;
        }
    }

    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    frame = s.curFrame < 0 ? 0 : (s.curFrame > cols * rows - 1 ? cols * rows - 1 : s.curFrame);
}

static void UpdateSprites(float dt)
{
    const bool paused = g_playState == GhostPlayState::Paused;
    for (GhostObject& o : g_scene.objects)
    {
        if (o.meshId != GHOST_MESH_QUAD) continue;
        GhostSprite& s = o.sprite;
        if (!s.animResolved)
        {
            s.animResolved = true;
            s.animId = s.anim.empty() ? -1 : LoadAnimFile(s.anim);
        }

        const GhostAnimAsset* a = (s.animId >= 0 && s.animId < (int)g_anims.size()) ? &g_anims[(size_t)s.animId] : nullptr;
        if (a && !a->clips.empty())
        {
            int ci = GhostAnim_FindClip(*a, s.clip.c_str());
            if (ci < 0) ci = 0;                                    // unknown or empty clip name: the first clip
            const GhostClip& c = a->clips[(size_t)ci];
            if (!paused && s.playing) s.animTime += dt * s.animSpeed;
            const float dur = (float)c.frames.size() / c.fps;
            if (c.loop && dur > 0.0f && s.animTime > dur) s.animTime = fmodf(s.animTime, dur);
            s.curFrame = GhostAnim_FrameAt(c, s.animTime);
        }
        else s.curFrame = s.frame;
    }
}

static GhostObject* FindSprite(int id)
{
    GhostObject* o = FindObject(id);
    return (o && o->meshId == GHOST_MESH_QUAD) ? o : nullptr;
}

static int SetSpriteAnimImpl(GhostSprite& s, const std::string& path)
{
    s.anim = path;
    s.animTime = 0.0f;
    s.animResolved = true;
    s.animId = path.empty() ? -1 : LoadAnimFile(path);
    return (path.empty() || s.animId >= 0) ? 1 : 0;
}

static int Api_SpriteSetClip(int id, const char* name)
{
    GhostObject* o = FindSprite(id);
    if (!o || !name) return 0;
    GhostSprite& s = o->sprite;

    std::string want = name;
    if (s.animId >= 0 && s.animId < (int)g_anims.size())
    {
        const GhostAnimAsset& a = g_anims[(size_t)s.animId];
        const int ci = want.empty() ? -1 : GhostAnim_FindClip(a, name);
        if (!want.empty() && ci < 0) return 0;
        if (ci >= 0) want = a.clips[(size_t)ci].name;             // canonical spelling, so repeated calls don't restart
    }
    if (s.clip == want) return 1;
    s.clip = want;
    s.animTime = 0.0f;
    return 1;
}

static int Api_SpriteSetFlip(int id, int flip) { GhostObject* o = FindSprite(id); if (!o) return 0; o->sprite.flipX = (flip != 0); return 1; }
static int Api_SpriteSetFrame(int id, int f) { GhostObject* o = FindSprite(id); if (!o) return 0; o->sprite.frame = f < 0 ? 0 : (f > 4095 ? 4095 : f); return 1; }
static int Api_SpriteSetAnim(int id, const char* p) { GhostObject* o = FindSprite(id); return o ? SetSpriteAnimImpl(o->sprite, p ? p : "") : 0; }
static int Api_SpriteSetSpeed(int id, float v) { GhostObject* o = FindSprite(id); if (!o) return 0; o->sprite.animSpeed = v < 0.0f ? 0.0f : (v > 10.0f ? 10.0f : v); return 1; }
static int Api_SpriteSetState(int id, const char* state)
{
    GhostObject* o = FindSprite(id);
    if (!o || !state) return 0;
    std::string clip = state;
    const GhostSprite& s = o->sprite;
    if (s.animId >= 0 && s.animId < (int)g_anims.size())
        for (const auto& kv : g_anims[(size_t)s.animId].states)
            if (_stricmp(kv.first.c_str(), state) == 0) { if (!kv.second.empty()) clip = kv.second; break; }
    return Api_SpriteSetClip(id, clip.c_str());
}
static int Api_LoadScene(const char* rel)
{
    if (!rel || !*rel || g_playState == GhostPlayState::Editing) return 0;
    const std::string abs = StoredToAbs(rel);
    if (!std::filesystem::exists(std::filesystem::path(GhostUtf8ToWide(abs)))) return 0;
    g_pendingScene = rel;
    return 1;
}

static int Api_GetSceneName(char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    if (g_scenePath.empty()) return 0;
    const size_t s = g_scenePath.find_last_of("\\/");
    CopyMsg(s == std::string::npos ? g_scenePath : g_scenePath.substr(s + 1), buf, size);
    return 1;
}

static void Api_Quit() { g_quitRequested = true; }
static int Api_GetObjectProps(int id, GhostObjectProps* out)
{
    GhostObject* o = FindObject(id);
    if (!o || !out) return 0;
    memset(out, 0, sizeof * out);
    strncpy_s(out->link, sizeof out->link, o->link.c_str(), _TRUNCATE);
    out->pathOffset[0] = o->pathOffset.x; out->pathOffset[1] = o->pathOffset.y; out->pathOffset[2] = o->pathOffset.z;
    out->moveSpeed = o->moveSpeed;  out->movePause = o->movePause;
    out->pathMode = o->pathMode;    out->breakEffect = o->breakEffect;
    out->contents = o->contents;    out->portalTrigger = o->portalTrigger;
    out->locked = o->locked ? 1 : 0;
    return 1;
}

static void BuildGameApi()
{
    GhostGameAPI& a = g_gameApi;
    memset(&a, 0, sizeof a);
    a.version = GHOST_GAME_API_VERSION;
    a.size = (int)sizeof(GhostGameAPI);
    a.Log = Api_Log;  a.GetTime = Api_GetTime;  a.GetFixedDeltaTime = Api_GetDt;  a.SetHudText = Api_SetHud;
    a.KeyDown = Api_KeyDown;  a.KeyPressed = Api_KeyPressed;  a.KeyReleased = Api_KeyReleased;
    a.FindByTag = Api_FindByTag;  a.FindAllByTag = Api_FindAllByTag;  a.GetTag = Api_GetTag;  a.SetTag = Api_SetTag;
    a.GetPosition = Api_GetPosition;  a.SetPosition = Api_SetPosition;
    a.GetRotation = Api_GetRotation;  a.SetRotation = Api_SetRotation;
    a.GetScale = Api_GetScale;  a.SetScale = Api_SetScale;
    a.GetColor = Api_GetColor;  a.SetColor = Api_SetColor;
    a.GetVisible = Api_GetVisible;  a.SetVisible = Api_SetVisible;
    a.GetBounds = Api_GetBounds;  a.QueryBox = Api_QueryBox;
    a.Spawn = Api_Spawn;  a.Destroy = Api_Destroy;
    a.GetActiveCamera = Api_GetActiveCamera;  a.SetActiveCamera = Api_SetActiveCamera;
    a.GetCamera = Api_GetCamera;  a.SetCamera = Api_SetCamera;
    a.GetMouseDelta = Api_GetMouseDelta;  a.GetMouseWheel = Api_GetMouseWheel;
    a.GetCameraMode = Api_GetCameraMode;  a.GetCameraTarget = Api_GetCameraTarget;  a.SetCameraTarget = Api_SetCameraTarget;
    a.UIRect = Api_UIRect;  a.UIImage = Api_UIImage;  a.UIText = Api_UIText;
    a.UIMeasureText = Api_UIMeasure;  a.UIGetScreenSize = Api_UIScreenSize;  a.LoadTexture = Api_LoadTexture;
    a.UILoad = Api_UILoad;  a.UIShow = Api_UIShow;  a.UISetText = Api_UISetText;  a.UISetProgress = Api_UISetProgress;
    a.UISetVisible = Api_UISetVisible;  a.UISetColor = Api_UISetColor;
    a.UIButtonClicked = Api_UIButtonClicked;  a.UIButtonDown = Api_UIButtonDown;  a.UIGetMouse = Api_UIGetMouse;
    a.SkyGetTime = Api_SkyGetTime;  a.SkySetTime = Api_SkySetTime;
    a.SpriteSetClip = Api_SpriteSetClip;  a.SpriteSetFlip = Api_SpriteSetFlip;  a.SpriteSetFrame = Api_SpriteSetFrame;
    a.SpriteSetAnim = Api_SpriteSetAnim;  a.SpriteSetSpeed = Api_SpriteSetSpeed;
    a.LoadScene = Api_LoadScene;  a.GetSceneName = Api_GetSceneName;  a.Quit = Api_Quit;
    a.GetObjectProps = Api_GetObjectProps;
    a.SpriteSetState = Api_SpriteSetState;
}

// Runs between steps (never inside game code). The editor's own scene stays safe in g_playBackup.
static void PerformSceneSwitch()
{
    const std::string rel = g_pendingScene;
    g_pendingScene.clear();
    if (g_playState == GhostPlayState::Editing || rel.empty()) return;

    const std::string abs = StoredToAbs(rel);
    GhostGameModule_Unload();                           // Shutdown: the old level's code lets go of its object ids

    GhostScene backup = g_scene;
    std::string err, warn;
    g_modelWarn.clear();
    if (!GhostScene_Load(g_scene, g_projectRoot, abs, LoadSceneTexture, MAX_SCENE_OBJECT_COUNT, err, warn, LoadModelFile))
    {
        std::vector<GhostTextureInfo> tex = g_scene.textures;
        g_scene = backup;
        g_scene.textures = tex;
        g_playFault = "Could not load the scene \"" + rel + "\":\n" + err;
        GhostEngine_Stop();
        return;
    }
    if (!warn.empty() || !g_modelWarn.empty())
        OutputDebugStringA(("[GhostX] Scene \"" + rel + "\":\n" + warn + g_modelWarn).c_str());

    g_scenePath = abs;
    g_scene.version++;
    if (g_scene.gameStartCameraId && FindCamera(g_scene.gameStartCameraId))
        g_scene.activeCameraId = g_scene.gameStartCameraId;
    g_hoverId = 0; g_dragging = false; g_dragId = 0; g_inputCameraId = 0;
    g_uiAssets.clear(); g_uiQuads.clear();
    g_uiMouseX = g_uiMouseY = -1.0f; g_uiMouseInside = false;
    g_skyScroll[0] = g_skyScroll[1] = 0.0f;
    g_mouseAccumX = g_mouseAccumY = g_apiMouseDX = g_apiMouseDY = 0.0f;
    g_wheelAccum = g_apiWheel = 0.0f;
    g_hudText.clear();
    g_stepper.Reset();

    BuildGameApi();
    if (GhostGameModule_Load(g_gameModulePath, &g_gameApi, g_playMessage) == GhostModuleResult::Error)
    {
        g_playFault = "The game module failed to start in the new scene:\n" + g_playMessage;
        GhostEngine_Stop();
    }
}


// ============================================================
//  Exported: play mode
// ============================================================
int GhostEngine_Play()
{
    if (!g_rhi || !g_pipeline) { g_playMessage = "The engine is not ready."; return 0; }
    if (g_playState != GhostPlayState::Editing) return 1;

    g_playBackup = g_scene;
    g_playBackupPath = g_scenePath;
    g_pendingScene.clear();
    g_quitRequested = false;
    ReloadAnims();
    if (g_scene.gameStartCameraId && FindCamera(g_scene.gameStartCameraId))
        g_scene.activeCameraId = g_scene.gameStartCameraId;      // Stop restores the backup, so your editor camera comes back
    for (GhostCamera& c : g_scene.cameras) c.orbitValid = false; // orbit cameras snap to their target on the first frame
    g_mouseAccumX = g_mouseAccumY = g_apiMouseDX = g_apiMouseDY = 0.0f;
    g_wheelAccum = g_apiWheel = 0.0f;
    g_scene.version++;                                           // the hierarchy shows the new (Main View)
    g_stepper.Reset();
    g_input.Reset();
    g_playTime = 0.0; g_skyScroll[0] = g_skyScroll[1] = 0.0f;
    g_hudText.clear(); g_uiQuads.clear(); g_playMessage.clear(); g_playFault.clear();
    g_uiAssets.clear();
    g_uiMouseX = g_uiMouseY = -1.0f; g_uiMouseInside = false;
    g_hoverId = 0; g_dragging = false; g_dragId = 0;
    g_playState = GhostPlayState::Playing;

    BuildGameApi();
    GhostModuleResult r = GhostGameModule_Load(g_gameModulePath, &g_gameApi, g_playMessage);
    OutputDebugStringA(("[GhostX] " + g_playMessage + "\n").c_str());

    if (r == GhostModuleResult::Error)
    {
        GhostEngine_Stop();                          // restores the scene: Init may already have changed it
        return 0;
    }
    return r == GhostModuleResult::Loaded ? 1 : 2;
}

void GhostEngine_Stop()
{
    if (g_playState == GhostPlayState::Editing) return;
    g_scenePath = g_playBackupPath;                  // the editor goes back to the scene you were editing
    g_pendingScene.clear();
    GhostGameModule_Unload();                        // Shutdown, then free the temp copy
    g_hudText.clear(); g_uiQuads.clear();
    g_uiAssets.clear();
    g_uiMouseX = g_uiMouseY = -1.0f; g_uiMouseInside = false;
    // Textures loaded during play stay on the GPU, so keep the registry. Everything else goes back.
    std::vector<GhostTextureInfo> tex = g_scene.textures;
    unsigned int version = g_scene.version;
    g_scene = std::move(g_playBackup);
    g_scene.textures = tex;
    g_scene.version = version + 1;                   // the hierarchy rebuilds
    g_playBackup = GhostScene();

    g_playState = GhostPlayState::Editing;
    g_hoverId = 0; g_dragging = false; g_dragId = 0;
}

void GhostEngine_SetPaused(int paused)
{
    if (paused && g_playState == GhostPlayState::Playing) g_playState = GhostPlayState::Paused;
    else if (!paused && g_playState == GhostPlayState::Paused) g_playState = GhostPlayState::Playing;
}

int GhostEngine_GetPlayState() { return (int)g_playState; }

// ============================================================
//  Exported: gameplay data
// ============================================================
int GhostEngine_GetGameplay(int objectId, GhostGameplayInfo* out)
{
    GhostObject* o = FindObject(objectId);
    if (!o || !out) return 0;
    *out = { o->id, o->role, o->collider ? 1 : 0,
             o->colliderSize.x, o->colliderSize.y, o->colliderSize.z,
             o->colliderOffset.x, o->colliderOffset.y, o->colliderOffset.z,
             o->visible ? 1 : 0,
             o->locked ? 1 : 0, o->pathMode, o->breakEffect, o->contents, o->portalTrigger,
             o->pathOffset.x, o->pathOffset.y, o->pathOffset.z, o->moveSpeed, o->movePause };
    return 1;
}

void GhostEngine_SetGameplay(const GhostGameplayInfo* in)
{
    if (!in) return;
    GhostObject* o = FindObject(in->id);
    if (!o) return;
    auto size = [](float v) { v = fabsf(v); return v < 0.01f ? 0.01f : v; };
    o->role = in->role < 0 ? 0 : (in->role >= GHOST_ROLE_COUNT ? GHOST_ROLE_COUNT - 1 : in->role);
    o->collider = (in->collider != 0);
    o->colliderSize = { size(in->csx), size(in->csy), size(in->csz) };
    o->colliderOffset = { in->cox, in->coy, in->coz };
    o->visible = (in->visible != 0);
    auto ci = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
    o->locked = (in->locked != 0);
    o->pathMode = ci(in->pathMode, 0, 2);
    o->breakEffect = ci(in->breakEffect, 0, 2);
    o->contents = ci(in->contents, 0, 3);
    o->portalTrigger = ci(in->portalTrigger, 0, 2);
    o->pathOffset = { cf(in->moveX, -10000.0f, 10000.0f), cf(in->moveY, -10000.0f, 10000.0f), cf(in->moveZ, -10000.0f, 10000.0f) };
    o->moveSpeed = cf(in->moveSpeed, 0.01f, 100.0f);
    o->movePause = cf(in->movePause, 0.0f, 60.0f);
}

int GhostEngine_GetCustomTag(int objectId, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    buf[0] = 0;
    GhostObject* o = FindObject(objectId);
    if (!o) return 0;
    CopyMsg(o->customTag, buf, bufSize);
    return 1;
}

void GhostEngine_SetCustomTag(int objectId, const char* utf8Tag)
{
    GhostObject* o = FindObject(objectId);
    if (!o) return;
    std::string t = utf8Tag ? utf8Tag : "";
    if (t.size() > 31) t.resize(31);
    o->customTag = t;
}

void GhostEngine_SetGameModulePath(const char* path) { g_gameModulePath = path ? path : ""; }

int GhostEngine_GetPlayMessage(char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    CopyMsg(g_playMessage, buf, size);
    return 1;
}

int GhostEngine_GetHudText(char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    CopyMsg(g_hudText, buf, size);
    return 1;
}

int GhostEngine_TakePlayFault(char* buf, int size)
{
    if (g_playFault.empty() || !buf || size <= 0) return 0;
    CopyMsg(g_playFault, buf, size);
    g_playFault.clear();
    return 1;
}
int GhostEngine_DuplicateEntity(int id)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    GhostObject* src = FindObject(id);
    if (!src || !CanAddObject(src->meshId)) return 0;

    GhostObject copy = *src;                         // copy first: push_back can move the vector
    copy.id = g_scene.nextId++;

    std::string base = copy.name;                    // "Cube 4" -> "Cube", then add the new id
    size_t sp = base.find_last_of(' ');
    if (sp != std::string::npos && sp + 1 < base.size() &&
        base.find_first_not_of("0123456789", sp + 1) == std::string::npos)
        base.resize(sp);
    copy.name = base + " " + std::to_string(copy.id);
    copy.position.x += fabsf(copy.scale.x) + 0.25f;  // lands beside the original

    g_scene.objects.push_back(copy);
    g_scene.selectedId = copy.id;
    g_scene.selectedFace = -1;
    g_scene.version++;
    return copy.id;
}

int GhostEngine_GetCameraTargetTag(int id, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    GhostCamera* c = FindCamera(id);
    if (!c) return 0;
    CopyMsg(c->targetTag, buf, size);
    return 1;
}

void GhostEngine_SetCameraTargetTag(int id, const char* tag)
{
    GhostCamera* c = FindCamera(id);
    if (!c) return;
    std::string t = tag ? tag : "";
    if (t.size() > 31) t.resize(31);
    c->targetTag = t;
    c->orbitValid = false;
}

int GhostEngine_GetGameStartCamera() { return g_scene.gameStartCameraId; }

void GhostEngine_SetGameStartCamera(int id)
{
    g_scene.gameStartCameraId = (id != 0 && FindCamera(id)) ? id : 0;
}

void GhostEngine_MouseWheel(int delta)
{
    if (g_playState != GhostPlayState::Playing || !g_inputEnabled) return;
    const float notches = (float)delta / 120.0f;
    g_wheelAccum += notches;                       // the game can read it too

    GhostCamera* c = InputCamera();
    if (c && c->mode == 1)
    {
        float d = c->distance - notches;
        c->distance = d < c->minDist ? c->minDist : (d > c->maxDist ? c->maxDist : d);
    }
}

int GhostEngine_SetFont(int tex, int aw, int ah, float cw, float ch, float pad, float lh, const float* adv)
{
    if (!adv || tex < 0 || tex >= (int)g_scene.textures.size() || aw <= 0 || ah <= 0 ||
        cw <= 0.0f || ch <= 0.0f || lh <= 0.0f) return 0;

    GhostFont f;
    f.textureId = tex;
    f.atlasW = aw; f.atlasH = ah;
    f.cellW = cw;  f.cellH = ch;
    f.pad = pad;   f.lineHeight = lh;
    memcpy(f.advance, adv, 95 * sizeof(float));
    f.advance[95] = 0.0f;
    f.ready = true;
    g_font = f;
    return 1;
}

// ============================================================
//  Exported: mods
// ============================================================
static GhostModEntity* FindMod(int id)
{
    for (GhostModEntity& m : g_scene.mods) if (m.id == id) return &m;
    return nullptr;
}

int GhostEngine_GetModTypeCount() { return GHOST_MOD_COUNT; }

int GhostEngine_GetModTypeName(int t, char* buf, int size)
{
    const GhostModType* ty = GhostMods_Type(t);
    if (!ty || !buf || size <= 0) return 0;
    CopyMsg(ty->name, buf, size);
    return 1;
}

int GhostEngine_GetModParamCount(int t)
{
    const GhostModType* ty = GhostMods_Type(t);
    return ty ? ty->paramCount : 0;
}

int GhostEngine_GetModParamInfo(int t, int i, GhostModParamInfo* out)
{
    const GhostModType* ty = GhostMods_Type(t);
    if (!ty || !out || i < 0 || i >= ty->paramCount) return 0;
    const GhostParamDesc& d = ty->params[i];
    memset(out, 0, sizeof * out);
    out->kind = d.kind;
    out->mn = d.mn; out->mx = d.mx;
    for (int k = 0; k < 3; k++) out->def[k] = d.def[k];
    out->count = GhostParam_Count(d);
    strncpy_s(out->name, sizeof out->name, d.name, _TRUNCATE);
    strncpy_s(out->category, sizeof out->category, d.category, _TRUNCATE);
    if (d.enums) strncpy_s(out->enums, sizeof out->enums, d.enums, _TRUNCATE);
    return 1;
}

int GhostEngine_AddMod(int typeId, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    const GhostModType* ty = GhostMods_Type(typeId);
    if (!ty) return 0;

    int have = 0;
    GhostModEntity* first = nullptr;
    for (GhostModEntity& m : g_scene.mods) if (m.typeId == typeId) { if (!first) first = &m; have++; }
    if (first && ty->maxPerScene > 0 && have >= ty->maxPerScene)
    {
        g_scene.selectedId = first->id;              // already there: select it instead
        return 0;
    }

    SpawnCoords(sx, sy);
    GhostModEntity* m = GhostScene_AddMod(g_scene, typeId);
    if (!m) return 0;
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += 1.5f;
    m->position = p;
    g_scene.selectedId = m->id;
    return m->id;
}

int GhostEngine_GetMod(int id, float* pos)
{
    GhostModEntity* m = FindMod(id);
    if (!m) return -1;
    if (pos) { pos[0] = m->position.x; pos[1] = m->position.y; pos[2] = m->position.z; }
    return m->typeId;
}

void GhostEngine_SetModPosition(int id, const float* pos)
{
    GhostModEntity* m = FindMod(id);
    if (m && pos) m->position = { pos[0], pos[1], pos[2] };
}

int GhostEngine_GetModParam(int id, int index, float* out3)
{
    GhostModEntity* m = FindMod(id);
    const GhostModType* ty = m ? GhostMods_Type(m->typeId) : nullptr;
    if (!ty || !out3 || index < 0 || index >= ty->paramCount) return 0;
    const int off = GhostMods_ParamOffset(m->typeId, index), n = GhostParam_Count(ty->params[index]);
    if (off < 0 || (size_t)(off + n) > m->values.size()) return 0;
    for (int i = 0; i < 3; i++) out3[i] = i < n ? m->values[(size_t)(off + i)] : 0.0f;
    return 1;
}

void GhostEngine_SetModParam(int id, int index, const float* in3)
{
    GhostModEntity* m = FindMod(id);
    const GhostModType* ty = m ? GhostMods_Type(m->typeId) : nullptr;
    if (!ty || !in3 || index < 0 || index >= ty->paramCount) return;
    const GhostParamDesc& d = ty->params[index];
    const int off = GhostMods_ParamOffset(m->typeId, index), n = GhostParam_Count(d);
    if (off < 0 || (size_t)(off + n) > m->values.size()) return;

    float v[3] = { in3[0], in3[1], in3[2] };
    GhostMods_Clamp(m->typeId, index, v);
    if (d.kind == GHOST_PARAM_TEXTURE && (int)v[0] >= (int)g_scene.textures.size()) v[0] = 0.0f;
    for (int i = 0; i < n; i++) m->values[(size_t)(off + i)] = v[i];
}


// ============================================================
//  Exported: sprites
// ============================================================
int GhostEngine_GetSprite(int id, GhostSpriteInfo* out)
{
    GhostObject* o = FindSprite(id);
    if (!o || !out) return 0;
    const GhostSprite& s = o->sprite;
    int tex, cols, rows, frame;
    SpriteDrawInfo(*o, tex, cols, rows, frame);
    *out = { id, s.textureId, s.source, s.cols, s.rows, s.frame,
             s.flipX, s.billboard, s.unlit, s.alphaClip, s.pixelArt, s.playing,
             s.tint[0], s.tint[1], s.tint[2], s.animSpeed,
             tex, cols, rows, frame };
    return 1;
}

void GhostEngine_SetSprite(const GhostSpriteInfo* in)
{
    GhostObject* o = in ? FindSprite(in->id) : nullptr;
    if (!o) return;
    auto ci = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto cf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    GhostSprite& s = o->sprite;
    s.textureId = (in->textureId >= 0 && in->textureId < (int)g_scene.textures.size()) ? in->textureId : 0;
    s.source = in->source ? 1 : 0;
    s.cols = ci(in->cols, 1, 64);
    s.rows = ci(in->rows, 1, 64);
    s.frame = ci(in->frame, 0, 4095);
    s.flipX = in->flipX != 0;
    s.billboard = in->billboard != 0;
    s.unlit = in->unlit != 0;
    s.alphaClip = in->alphaClip != 0;
    s.pixelArt = in->pixelArt != 0;
    s.playing = in->playing != 0;
    s.tint[0] = cf(in->tintR, 0.0f, 1.0f); s.tint[1] = cf(in->tintG, 0.0f, 1.0f); s.tint[2] = cf(in->tintB, 0.0f, 1.0f);
    s.animSpeed = cf(in->animSpeed, 0.0f, 10.0f);
}

int GhostEngine_GetSpriteText(int id, int which, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    GhostObject* o = FindSprite(id);
    if (!o) return 0;
    std::string t;
    if (which == 0) t = o->sprite.anim;
    else if (which == 1) t = o->sprite.clip;
    else if (o->sprite.animId >= 0 && o->sprite.animId < (int)g_anims.size())
        for (const GhostClip& c : g_anims[(size_t)o->sprite.animId].clips) { if (!t.empty()) t += "|"; t += c.name; }
    CopyMsg(t, buf, size);
    return 1;
}

void GhostEngine_SetSpriteText(int id, int which, const char* text)
{
    GhostObject* o = FindSprite(id);
    if (!o) return;
    if (which == 0) SetSpriteAnimImpl(o->sprite, text ? text : "");
    else if (which == 1) Api_SpriteSetClip(id, text ? text : "");
}

void GhostEngine_ReloadAnimations() { ReloadAnims(); }

// ============================================================
//  Exported: shadows
// ============================================================
int GhostEngine_GetObjectShadow(int id, int* mode, int* lightId, int* receive)
{
    GhostObject* o = FindObject(id);
    if (!o) return 0;
    if (mode) *mode = o->shadowMode;
    if (lightId) *lightId = o->shadowLightId;
    if (receive) *receive = o->receiveShadows ? 1 : 0;
    return 1;
}

void GhostEngine_SetObjectShadow(int id, int mode, int lightId, int receive)
{
    GhostObject* o = FindObject(id);
    if (!o) return;
    if (mode < 0) mode = 0;
    if (mode > 2) mode = 2;
    if (mode == 2)
    {
        bool ok = false;
        for (const GhostLight& l : g_scene.lights) if (l.id == lightId) ok = true;
        if (!ok) { mode = 1; lightId = 0; }
    }
    o->shadowMode = mode;
    o->shadowLightId = (mode == 2) ? lightId : 0;
    o->receiveShadows = (receive != 0);
}

int GhostEngine_GetLightShadows(int id)
{
    for (const GhostLight& l : g_scene.lights) if (l.id == id) return l.castShadows ? 1 : 0;
    return -1;
}

void GhostEngine_SetLightShadows(int id, int on)
{
    for (GhostLight& l : g_scene.lights) if (l.id == id) { l.castShadows = (on != 0); return; }
}

void GhostEngine_GetShadowSettings(int* enabled, float* strength)
{
    if (enabled) *enabled = g_scene.shadowsEnabled ? 1 : 0;
    if (strength) *strength = g_scene.shadowStrength;
}

void GhostEngine_SetShadowSettings(int enabled, float strength)
{
    g_scene.shadowsEnabled = (enabled != 0);
    g_scene.shadowStrength = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
}





// ============================================================
//  Exported: model import, materials, re-import and .ghostmesh loading
// ============================================================
static std::string StemOf(const std::string& path)
{
    return GhostWideToUtf8(std::filesystem::path(GhostUtf8ToWide(path)).stem().wstring());
}

static std::string FileNameOf(const std::string& path)
{
    return GhostWideToUtf8(std::filesystem::path(GhostUtf8ToWide(path)).filename().wstring());
}

static std::string ModelFilePath(const std::string& name)
{
    return GhostPath_Normalize(g_projectRoot + "\\Content\\Models\\" + name + ".ghostmesh");
}

// A name nothing else is using: not in the registry, and (when saving to the project) no existing file
static std::string UniqueModelName(std::string base, bool checkFile)
{
    for (char& c : base) if (strchr("<>:\"/\\|?*", c)) c = '_';
    if (base.empty()) base = "Model";
    std::string n = base;
    for (int k = 2; ; k++)
    {
        bool taken = GhostMesh_FindByName(n) >= 0;
        if (!taken && checkFile && !g_projectRoot.empty())
            taken = std::filesystem::exists(std::filesystem::path(GhostUtf8ToWide(ModelFilePath(n))));
        if (!taken) return n;
        n = base + " (" + std::to_string(k) + ")";
    }
}

// Project-relative with '/', or absolute when the file is outside the project
static std::string StoredRel(const std::string& absUtf8)
{
    namespace fs = std::filesystem;
    fs::path p = fs::path(GhostUtf8ToWide(absUtf8)).lexically_normal();
    if (!g_projectRoot.empty())
    {
        fs::path rel = p.lexically_relative(fs::path(GhostUtf8ToWide(g_projectRoot)).lexically_normal());
        if (!rel.empty() && *rel.begin() != fs::path(L"..")) return GhostWideToUtf8(rel.generic_wstring());
    }
    return GhostWideToUtf8(p.generic_wstring());
}

static std::string ModelTexDir(const std::string& name)
{
    if (!g_projectRoot.empty()) return GhostPath_Normalize(g_projectRoot + "\\Content\\Textures\\" + name);
    wchar_t t[MAX_PATH + 2];
    const DWORD n = GetTempPathW(MAX_PATH, t);                     // no project: textures go to a temp folder for this session
    return GhostPath_Normalize(GhostWideToUtf8(std::wstring(t, n)) + "GhostX\\ModelTextures\\" + name);
}

// Copies (or extracts) every texture the importer found into the project, and points the slot at the copy
static void PlaceModelTextures(GhostModelResult& r, const std::string& name, std::string& warn)
{
    namespace fs = std::filesystem;
    const fs::path dir(GhostUtf8ToWide(ModelTexDir(name)));
    std::map<std::string, fs::path> placed;                        // source file -> where it went
    std::set<std::wstring> usedNames;
    int embedded = 0;

    for (GhostModelSub& s : r.subs)
    {
        GhostModelMat& m = s.mat;
        if (m.texSource.empty() && m.texData.empty()) continue;

        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path dest;
        bool ok = false;

        if (!m.texSource.empty())
        {
            auto it = placed.find(m.texSource);
            if (it != placed.end()) { dest = it->second; ok = true; }
            else
            {
                const fs::path src(GhostUtf8ToWide(m.texSource));
                const std::wstring stem = src.stem().wstring(), ext = src.extension().wstring();
                dest = dir / src.filename();
                for (int k = 2; usedNames.count(dest.filename().wstring()); k++)
                    dest = dir / (stem + L"_" + std::to_wstring(k) + ext);
                usedNames.insert(dest.filename().wstring());

                if (src.lexically_normal() == dest.lexically_normal()) ok = true;
                else { fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec); ok = !ec; }
                if (ok) placed[m.texSource] = dest;
            }
        }
        else
        {
            dest = dir / (GhostUtf8ToWide(name) + L"_embedded_" + std::to_wstring(++embedded) + L"." + GhostUtf8ToWide(m.texExt));
            std::ofstream f(dest, std::ios::binary);
            f.write((const char*)m.texData.data(), (std::streamsize)m.texData.size());
            ok = (bool)f;
        }

        if (ok)
        {
            m.texPath = StoredRel(GhostWideToUtf8(dest.wstring()));
            m.tint[0] = m.tint[1] = m.tint[2] = 1.0f;              // a textured slot shows the image at full strength
        }
        else warn += "Could not copy the texture for \"" + s.name + "\" into the project.\n";
        m.texSource.clear();
        m.texData.clear();
        m.texData.shrink_to_fit();
    }
}

// One material slot of the registry: the stored defaults, with the texture loaded onto the GPU
static GhostFaceDef MakeFace(const GhostModelSub& s, std::string& warn)
{
    GhostFaceDef fd;
    fd.name = s.name;
    fd.firstIndex = s.firstIndex;
    fd.indexCount = s.indexCount;
    const GhostModelMat& m = s.mat;
    if (!m.hasMat) return fd;

    GhostMaterial g;
    for (int k = 0; k < 3; k++) { g.tint[k] = m.tint[k]; g.emissive[k] = m.emissive[k]; }
    g.metallic = m.metallic;
    g.roughness = m.roughness;
    g.specular = m.specular;
    g.emissiveStrength = m.emissiveStrength;
    if (!m.texPath.empty())
    {
        const std::string abs = StoredToAbs(m.texPath);
        const int id = LoadSceneTexture(abs, FileNameOf(abs));
        if (id > 0) g.textureId = id;
        else warn += "Missing texture: " + m.texPath + "\n";
    }
    fd.mat = g;
    fd.hasMat = true;
    return fd;
}

static GhostMeshInfo BuildMeshInfo(const GhostModelResult& r, const std::string& name, const std::string& source,
    const std::string& file, std::string& warn)
{
    GhostMeshInfo mi;
    mi.name = name;
    mi.source = source;
    mi.file = file;
    mi.builtin = false;
    mi.index32 = true;
    mi.bmin = { r.bmin[0], r.bmin[1], r.bmin[2] };
    mi.bmax = { r.bmax[0], r.bmax[1], r.bmax[2] };
    mi.defaultScale = { 1, 1, 1 };
    mi.vertexCount = (unsigned int)r.vertices.size();
    mi.triangleCount = r.triangleCount;
    mi.importScale = r.opts.scale;
    mi.importFit = r.opts.fitSize;
    mi.importUp = r.opts.upAxis;
    mi.importFlags = (r.opts.flipWinding ? 1 : 0) | (r.opts.importMaterials ? 0 : 2);
    for (const GhostModelSub& s : r.subs) mi.faces.push_back(MakeFace(s, warn));
    return mi;
}

// Uploads a converted model and adds it to the registry
static int RegisterModel(const GhostModelResult& r, const std::string& name, const std::string& source,
    const std::string& file, std::string& err, std::string& warn)
{
    static_assert(GHOST_MODEL_MAX_SLOTS == GHOST_MAX_FACES, "importer slot limit must match GHOST_MAX_FACES");
    static_assert(sizeof(GhostMeshVertex) == sizeof(GhostVertex), "GhostMeshVertex must match GhostVertex");

    if (GhostMesh_Count() >= GHOST_MAX_MESHES)
    {
        err = "The mesh limit (64) has been reached. Restart the editor to clear models you no longer use."; return -1;
    }

    const int id = GhostMesh_Count();
    if (GhostUploadMeshData(g_rhi, g_pipeline, id, r.vertices.data(), (unsigned int)r.vertices.size(),
        r.indices.data(), (unsigned int)r.indices.size()) != GHOST_OK)
    {
        err = "The GPU upload failed (out of memory?)."; return -1;
    }

    if (GhostMesh_Register(BuildMeshInfo(r, name, source, file, warn)) != id)
    {
        err = "The mesh registry is out of sync."; return -1;
    }
    return id;
}

// Finds a loaded mesh by its .ghostmesh path, or loads the file
static int LoadModelFile(const std::string& absFile, std::string& err)
{
    const std::string abs = GhostPath_Normalize(absFile);
    for (int i = GHOST_MESH_COUNT; i < GhostMesh_Count(); i++)
    {
        const GhostMeshInfo* m = GhostMesh_Info(i);
        if (m && !m->file.empty() && _stricmp(m->file.c_str(), abs.c_str()) == 0) return i;
    }
    if (!g_rhi || !g_pipeline) { err = "The engine is not ready."; return -1; }

    GhostModelResult r;
    std::string source;
    if (!GhostMeshFile_Load(abs, r, source, err)) return -1;

    const std::string name = UniqueModelName(StemOf(abs), false);
    r.name = name;
    std::string warn;
    const int id = RegisterModel(r, name, source, abs, err, warn);
    if (id >= 0 && !warn.empty()) g_modelWarn += "Model \"" + name + "\":\n" + warn;
    return id;
}

int GhostEngine_ImportModel(const char* path, float scale, float fitSize, int upAxis, int flags,
    char* msg, int msgSize)
{
    if (!g_rhi || !g_pipeline || !path) { CopyMsg("The engine is not ready.", msg, msgSize); return -1; }
    if (g_playState != GhostPlayState::Editing) { CopyMsg("Stop playing before importing a model.", msg, msgSize); return -1; }
    if (GhostMesh_Count() >= GHOST_MAX_MESHES) { CopyMsg("The mesh limit (64) has been reached.", msg, msgSize); return -1; }

    GhostModelOptions opt;
    opt.scale = (scale > 0.0001f && scale <= 10000.0f) ? scale : 1.0f;
    opt.fitSize = fitSize > 0.0f ? (fitSize > 10000.0f ? 10000.0f : fitSize) : 0.0f;
    opt.upAxis = upAxis < 0 ? 0 : (upAxis > 2 ? 2 : upAxis);
    opt.flipWinding = (flags & 1) != 0;
    opt.importMaterials = (flags & 2) == 0;

    GhostModelResult r;
    std::string err;
    if (!GhostModel_Load(path, opt, r, err)) { CopyMsg(err, msg, msgSize); return -1; }

    const std::string src = GhostPath_Normalize(path);
    const bool toProject = !g_projectRoot.empty();
    const std::string name = UniqueModelName(r.name, toProject);
    r.name = name;

    PlaceModelTextures(r, name, r.warn);

    std::string file, saveWarn;
    if (toProject)                                       // convert once; scenes load the .ghostmesh from now on
    {
        const std::string target = ModelFilePath(name);
        std::string serr;
        if (GhostMeshFile_Save(target, r, src, serr)) file = target;
        else saveWarn = "Could not save the converted mesh: " + serr + "\nThe model works in this session only.";
    }

    std::string regWarn;
    const int id = RegisterModel(r, name, src, file, err, regWarn);
    if (id < 0)
    {
        if (!file.empty())
        {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(GhostUtf8ToWide(file)), ec);
        }
        CopyMsg(err, msg, msgSize);
        return -1;
    }

    std::string text = "Imported \"" + name + "\":\n" + r.info;
    if (!file.empty()) text += "\nSaved as Content/Models/" + name + ".ghostmesh";
    const std::string allWarn = r.warn + regWarn;
    if (!allWarn.empty()) text += "\n\n" + allWarn;
    if (!saveWarn.empty()) text += "\n\n" + saveWarn;
    if (!toProject) text += "\n\nNo project is open, so this model lives only in this session. Open or create a project to keep it.";
    CopyMsg(text, msg, msgSize);
    return id;
}

// ---- re-import: keep a placed object's own edits, refresh everything it hadn't touched ----
static bool NearV(const GhostVec3& a, const GhostVec3& b)
{
    return fabsf(a.x - b.x) < 0.001f && fabsf(a.y - b.y) < 0.001f && fabsf(a.z - b.z) < 0.001f;
}

static void RemapObjectToMesh(GhostObject& o, const GhostMeshInfo& oldM, const GhostMeshInfo& newM)
{
    GhostMaterial prev[GHOST_MAX_FACES];
    for (int f = 0; f < GHOST_MAX_FACES; f++) prev[f] = o.faces[f];
    const int oc = (int)oldM.faces.size() > GHOST_MAX_FACES ? GHOST_MAX_FACES : (int)oldM.faces.size();
    const int nc = (int)newM.faces.size() > GHOST_MAX_FACES ? GHOST_MAX_FACES : (int)newM.faces.size();

    for (int f = 0; f < GHOST_MAX_FACES; f++) o.faces[f] = GhostMaterial();
    for (int f = 0; f < nc; f++)
    {
        const GhostFaceDef& nf = newM.faces[(size_t)f];
        o.faces[f] = nf.hasMat ? nf.mat : GhostMaterial();
        for (int k = 0; k < oc; k++)
        {
            const GhostFaceDef& of = oldM.faces[(size_t)k];
            if (_stricmp(of.name.c_str(), nf.name.c_str()) != 0) continue;
            const GhostMaterial oldDefault = of.hasMat ? of.mat : GhostMaterial();
            if (!GhostMaterial_Equal(prev[k], oldDefault)) o.faces[f] = prev[k];   // the user changed this slot: keep it
            break;
        }
    }

    GhostVec3 os, oo, ns, no;
    GhostMesh_FitCollider(oldM, os, oo);
    GhostMesh_FitCollider(newM, ns, no);
    if (NearV(o.colliderSize, os) && NearV(o.colliderOffset, oo)) { o.colliderSize = ns; o.colliderOffset = no; }
}

int GhostEngine_ReimportModel(int meshId, int useStored, float scale, float fitSize, int upAxis, int flags,
    char* msg, int msgSize)
{
    if (!g_rhi || !g_pipeline) { CopyMsg("The engine is not ready.", msg, msgSize); return -1; }
    if (g_playState != GhostPlayState::Editing) { CopyMsg("Stop playing before re-importing a model.", msg, msgSize); return -1; }
    const GhostMeshInfo* probe = GhostMesh_Info(meshId);
    if (!probe || probe->builtin) { CopyMsg("Only imported models can be re-imported.", msg, msgSize); return -1; }

    GhostMeshInfo& cur = GhostMeshes()[(size_t)meshId];
    if (cur.source.empty() || !std::filesystem::exists(std::filesystem::path(GhostUtf8ToWide(cur.source))))
    {
        CopyMsg("The original file was not found:\n" + (cur.source.empty() ? std::string("(unknown)") : cur.source), msg, msgSize);
        return -1;
    }

    GhostModelOptions opt;
    if (useStored)
    {
        opt.scale = cur.importScale > 0.0001f ? cur.importScale : 1.0f;
        opt.fitSize = cur.importFit > 0.0f ? cur.importFit : 0.0f;
        opt.upAxis = cur.importUp;
        opt.flipWinding = (cur.importFlags & 1) != 0;
        opt.importMaterials = (cur.importFlags & 2) == 0;
    }
    else
    {
        opt.scale = (scale > 0.0001f && scale <= 10000.0f) ? scale : 1.0f;
        opt.fitSize = fitSize > 0.0f ? (fitSize > 10000.0f ? 10000.0f : fitSize) : 0.0f;
        opt.upAxis = upAxis < 0 ? 0 : (upAxis > 2 ? 2 : upAxis);
        opt.flipWinding = (flags & 1) != 0;
        opt.importMaterials = (flags & 2) == 0;
    }

    GhostModelResult r;
    std::string err;
    if (!GhostModel_Load(cur.source, opt, r, err)) { CopyMsg(err, msg, msgSize); return -1; }   // nothing has changed yet
    const std::string name = cur.name;
    r.name = name;
    PlaceModelTextures(r, name, r.warn);

    if (GhostUploadMeshData(g_rhi, g_pipeline, meshId, r.vertices.data(), (unsigned int)r.vertices.size(),
        r.indices.data(), (unsigned int)r.indices.size()) != GHOST_OK)
    {
        CopyMsg("The GPU upload failed (out of memory?).", msg, msgSize); return -1;
    }

    std::string file = cur.file, saveWarn;
    if (file.empty() && !g_projectRoot.empty()) file = ModelFilePath(name);
    if (!file.empty())
    {
        std::string serr;
        if (!GhostMeshFile_Save(file, r, cur.source, serr))
        {
            saveWarn = "Could not save the converted mesh: " + serr; file = cur.file;
        }
    }

    const GhostMeshInfo old = cur;
    std::string regWarn;
    cur = BuildMeshInfo(r, name, old.source, file, regWarn);
    for (GhostObject& o : g_scene.objects)
        if (o.meshId == meshId && o.missingMesh.empty() && o.missingMeshFile.empty()) RemapObjectToMesh(o, old, cur);
    g_scene.selectedFace = -1;
    g_scene.version++;                                           // the hierarchy and Details refresh

    std::string text = "Re-imported \"" + name + "\":\n" + r.info;
    const std::string allWarn = r.warn + regWarn;
    if (!allWarn.empty()) text += "\n\n" + allWarn;
    if (!saveWarn.empty()) text += "\n\n" + saveWarn;
    CopyMsg(text, msg, msgSize);
    return meshId;
}

void GhostEngine_ApplyMeshDefaults(int objectId, int what)
{
    if (g_playState != GhostPlayState::Editing) return;
    GhostObject* o = FindObject(objectId);
    const GhostMeshInfo* mi = o ? GhostMesh_Info(o->meshId) : nullptr;
    if (!o || !mi || !o->missingMesh.empty() || !o->missingMeshFile.empty()) return;

    if (what & 1)                                                // materials
    {
        for (int f = 0; f < GHOST_MAX_FACES; f++) o->faces[f] = GhostMaterial();
        const int n = GhostMesh_FaceCount(o->meshId);
        for (int f = 0; f < n; f++) if (mi->faces[(size_t)f].hasMat) o->faces[f] = mi->faces[(size_t)f].mat;
        o->useVertexColor = false;
    }
    if (what & 2) GhostMesh_FitCollider(*mi, o->colliderSize, o->colliderOffset);
}

int GhostEngine_LoadModelFile(const char* path, char* msg, int msgSize)
{
    if (!path) { CopyMsg("No file given.", msg, msgSize); return -1; }
    g_modelWarn.clear();
    std::string err;
    const int id = LoadModelFile(path, err);
    CopyMsg(id < 0 ? err : g_modelWarn, msg, msgSize);
    return id;
}

int GhostEngine_GetMeshCount() { return GhostMesh_Count(); }

int GhostEngine_GetMeshInfo(int id, GhostMeshStats* out)
{
    const GhostMeshInfo* m = GhostMesh_Info(id);
    if (!m || !out) return 0;
    *out = { m->builtin ? 1 : 0, GhostMesh_FaceCount(id), m->vertexCount, m->triangleCount,
             m->bmin.x, m->bmin.y, m->bmin.z, m->bmax.x, m->bmax.y, m->bmax.z,
             m->importScale, m->importFit, m->importUp, m->importFlags };
    return 1;
}

int GhostEngine_GetMeshText(int id, int which, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    const GhostMeshInfo* m = GhostMesh_Info(id);
    if (!m) return 0;
    CopyMsg(which == 0 ? m->name : (which == 1 ? m->file : m->source), buf, size);
    return 1;
}

int GhostEngine_GetObjectMissingMesh(int objectId, char* buf, int size)
{
    if (buf && size > 0) buf[0] = 0;
    GhostObject* o = FindObject(objectId);
    if (!o || (o->missingMesh.empty() && o->missingMeshFile.empty())) return 0;
    if (buf && size > 0) CopyMsg(o->missingMesh.empty() ? o->missingMeshFile : o->missingMesh, buf, size);
    return 1;
}


int GhostEngine_TakeQuitRequest()
{
    const bool q = g_quitRequested;
    g_quitRequested = false;
    return q ? 1 : 0;
}

void GhostEngine_GetViewSettings(GhostViewSettings* out) { if (out) *out = g_viewCfg; }

void GhostEngine_SetViewSettings(const GhostViewSettings* in)
{
    if (!in) return;
    auto b = [](int v) { return v ? 1 : 0; };
    auto ci = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };

    GhostViewSettings s = *in;
    s.showSelection = b(s.showSelection); s.showFps = b(s.showFps);
    s.showGrid = b(s.showGrid);           s.showWorldAxes = b(s.showWorldAxes);
    s.showDims = b(s.showDims);           s.edgeLabels = b(s.edgeLabels);
    s.dimAxes = ci(s.dimAxes, 0, 7);
    s.gridPlane = ci(s.gridPlane, 0, 2);  s.gridStyle = ci(s.gridStyle, 0, 1);
    s.gridSize = (s.gridSize != s.gridSize) ? 1.0f : (s.gridSize < 0.01f ? 0.01f : (s.gridSize > 100.0f ? 100.0f : s.gridSize));
    s.gridExtent = ci(s.gridExtent, 4, 100);
    s.tool = ci(s.tool, 0, 4);            s.space = ci(s.space, 0, 1);
    s.unit = ci(s.unit, 0, 4);            s.decimals = ci(s.decimals, 0, 3);
    g_viewCfg = s;
}



void GhostEngine_GetGizmoSettings(GhostGizmoSettings* out) { if (out) *out = g_gizSet; }

void GhostEngine_SetGizmoSettings(const GhostGizmoSettings* in)
{
    if (!in) return;
    auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
    g_gizSet.snap = in->snap ? 1 : 0;
    g_gizSet.moveStep = cf(in->moveStep, 0.0f, 1000.0f);
    g_gizSet.rotStep = cf(in->rotStep, 0.0f, 180.0f);
    g_gizSet.scaleStep = cf(in->scaleStep, 0.0f, 10.0f);
}


int GhostEngine_TakeDimClick(int* kind, int* index, int* objectId, float* worldLen)
{
    if (!g_dimClick.valid) return 0;
    if (kind) *kind = g_dimClick.kind;
    if (index) *index = g_dimClick.index;
    if (objectId) *objectId = g_dimClick.objectId;
    if (worldLen) *worldLen = g_dimClick.len;
    g_dimClick.valid = false;
    return 1;
}

int GhostEngine_GetObjectSize(int id, float* xyz)
{
    GhostObject* o = FindObject(id);
    if (!o || !xyz) return 0;
    const GhostVec3 mn = GhostMesh_Min(o->meshId), mx = GhostMesh_Max(o->meshId);
    xyz[0] = fabsf((mx.x - mn.x) * o->scale.x);
    xyz[1] = fabsf((mx.y - mn.y) * o->scale.y);
    xyz[2] = fabsf((mx.z - mn.z) * o->scale.z);
    return 1;
}

void GhostEngine_SetObjectSize(int id, int axis, float world)
{
    GhostObject* o = FindObject(id);
    if (!o || g_playState != GhostPlayState::Editing || axis < 0 || axis > 2 || world != world) return;
    const GhostVec3 mn = GhostMesh_Min(o->meshId), mx = GhostMesh_Max(o->meshId);
    const float meshExt[3] = { fabsf(mx.x - mn.x), fabsf(mx.y - mn.y), fabsf(mx.z - mn.z) };
    if (meshExt[axis] < 1e-6f) return;

    world = world < 0.0001f ? 0.0001f : (world > 100000.0f ? 100000.0f : world);
    float* s = (axis == 0) ? &o->scale.x : (axis == 1 ? &o->scale.y : &o->scale.z);
    *s = (*s < 0.0f ? -1.0f : 1.0f) * (world / meshExt[axis]);        // the mirror sign stays
}

void GhostEngine_ScaleObject(int id, float f)
{
    GhostObject* o = FindObject(id);
    if (!o || g_playState != GhostPlayState::Editing || f != f) return;
    f = f < 0.001f ? 0.001f : (f > 1000.0f ? 1000.0f : f);
    o->scale = o->scale * f;
}

int GhostEngine_GetLinkId(int id, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = 0;
    GhostObject* o = FindObject(id);
    if (!o) return 0;
    CopyMsg(o->link, buf, size);
    return 1;
}

void GhostEngine_SetLinkId(int id, const char* t)
{
    GhostObject* o = FindObject(id);
    if (!o) return;
    std::string s = t ? t : "";
    if (s.size() > 31) s.resize(31);
    o->link = s;
}