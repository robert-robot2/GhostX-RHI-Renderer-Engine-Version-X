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
#include <string.h>
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
struct EngineViewport { bool alive = false; bool visible = true; int cameraId = 0; };
static EngineViewport g_vp[GHOST_MAX_SURFACES];
static int            g_inputCameraId = 0;               // the camera WASD / right-mouse look drives
static GhostVec3      g_mainEye = { 0, 0, 0 };           // main view position, for picking rays
// ---- Play mode ----
static GhostPlayState  g_playState = GhostPlayState::Editing;
static GhostScene      g_playBackup;
static GhostStepper    g_stepper;
static GhostInput      g_input;
static GhostTestState  g_testState;
// Main view render size and where it sits inside the window (for mapping the mouse)
static int   g_mainRW = 1280, g_mainRH = 720;
static float g_fitX = 0.0f, g_fitY = 0.0f, g_fitW = 1280.0f, g_fitH = 720.0f;

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
static const unsigned int MAX_SCENE_OBJECTS = GHOST_MAX_OBJECTS - 2 * GHOST_MAX_LIGHTS - 2 * GHOST_MAX_CAMERAS;
// worst case an object needs one draw per face
static const unsigned int MAX_SCENE_OBJECT_COUNT = MAX_SCENE_OBJECTS / GHOST_MAX_FACES;
// local-space bounds of each mesh (picking only)
static const GhostVec3 kMeshMin[GHOST_MESH_COUNT] = { { -0.5f, -0.5f, -0.5f }, { -0.5f, 0.0f, -0.05f }, { -3.2f, -1.0f, -3.0f }, { -0.5f, -0.5f, -0.5f } };
static const GhostVec3 kMeshMax[GHOST_MESH_COUNT] = { {  0.5f,  0.5f,  0.5f }, {  0.5f, 1.0f,  0.05f }, {  3.2f,  3.0f, 11.0f }, {  0.5f,  0.5f,  0.5f } };
static GhostColor g_clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };

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
    // ---- TEMP seed (removed in section C) so the checkpoint has something to see ----
    {
        GhostObject* o = GhostScene_AddObject(g_scene, GHOST_MESH_CUBE);
        o->useVertexColor = true;                       // old rainbow cube

        o = GhostScene_AddObject(g_scene, GHOST_MESH_CUBE);
        o->position = { 1.1f, 0.0f, 0.0f };             // grey cube

        o = GhostScene_AddObject(g_scene, GHOST_MESH_SHIP);
        o->position = { -1.5f, 0.0f, 2.0f };
        o->useVertexColor = true;
    }
    // ---- TEMP test lights (removed in section C) ----
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
static GhostMat4 ObjectInverse(const GhostObject& o)
{
    const float d2r = GHOST_PI / 180.0f;
    auto inv = [](float s) { return fabsf(s) > 1e-6f ? 1.0f / s : 0.0f; };
    return GhostMat4::Scale(inv(o.scale.x), inv(o.scale.y), inv(o.scale.z))
        * GhostMat4::RotationZ(-o.rotationDeg.z * d2r)
        * GhostMat4::RotationX(-o.rotationDeg.x * d2r)
        * GhostMat4::RotationY(-o.rotationDeg.y * d2r)
        * GhostMat4::Translation(-o.position.x, -o.position.y, -o.position.z);
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
    if (bestId) return bestId;

    GhostVec3 o, d;
    if (!ScreenRay(sx, sy, o, d)) return 0;
    float bestT = 1e30f;
    for (const GhostObject& ob : g_scene.objects)
    {
        GhostMat4 inv = ObjectInverse(ob);
        float t;
        if (RayAABB(XformPoint(inv, o, 1.0f), XformPoint(inv, d, 0.0f),
            kMeshMin[ob.meshId], kMeshMax[ob.meshId], t) && t < bestT)
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

    GhostMat4 inv = ObjectInverse(ob);
    GhostVec3 lo = XformPoint(inv, o, 1.0f);
    GhostVec3 ld = XformPoint(inv, d, 0.0f);

    const GhostVertex* v = nullptr;
    const unsigned short* idx = nullptr;
    unsigned int count = 0;
    if (!GhostGetMeshCPU(ob.meshId, &v, &idx, &count)) return -1;

    float bestT = 1e30f;
    int   bestTri = -1;
    for (unsigned int i = 0; i + 2 < count; i += 3)
    {
        const GhostVertex& a = v[idx[i]];
        const GhostVertex& b = v[idx[i + 1]];
        const GhostVertex& c = v[idx[i + 2]];
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

// ============================================================
//  Exported: input
// ============================================================
void GhostEngine_SetInputEnabled(int enabled) { g_inputEnabled = (enabled != 0); }

void GhostEngine_MouseDown(int px, int py, int button)
{
    if (button != 0 || g_looking || !g_haveCam || g_playState != GhostPlayState::Editing) return;
    int x, y;
    if (!PanelToRender(px, py, x, y, false)) return;          // clicked the dark bars

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
    if (g_looking || !g_haveCam || g_playState != GhostPlayState::Editing) { g_hoverId = 0; return; }

    int x, y;
    if (g_dragging)
    {
        PanelToRender(px, py, x, y, true);                    // clamped, so a drag can run over the bars
        GhostVec3* pos = EntityPosition(g_dragId);
        GhostVec3 o, d, hit;
        if (pos && ScreenRay(x, y, o, d) && RayPlane(o, d, g_dragPlanePoint, hit))
            *pos = hit + g_dragOffset;
        return;
    }
    if (!PanelToRender(px, py, x, y, false)) { g_hoverId = 0; return; }
    g_hoverId = PickAt(x, y);
}

void GhostEngine_MouseUp(int x, int y, int button)
{
    (void)x; (void)y;
    if (button != 0) return;
    g_dragging = false;
    g_dragId = 0;
}

void GhostEngine_MouseLeave() { g_hoverId = 0; }

// ============================================================
//  Exported: scene
// ============================================================
int GhostEngine_AddObject(int meshId, int sx, int sy)
{
    if (g_playState != GhostPlayState::Editing) return 0;
    if (g_scene.objects.size() >= MAX_SCENE_OBJECT_COUNT) return 0;
    GhostObject* o = GhostScene_AddObject(g_scene, meshId);
    if (!o) return 0;
    SpawnCoords(sx, sy);
    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += -kMeshMin[meshId].y * o->scale.y;       // rest the mesh on the ground
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
    return n;
}

int GhostEngine_GetEntityName(int id, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    const std::string* name = nullptr;
    for (const GhostObject& o : g_scene.objects) if (o.id == id) name = &o.name;
    for (const GhostLight& l : g_scene.lights)  if (l.id == id) name = &l.name;
    for (const GhostCamera& c : g_scene.cameras) if (c.id == id) name = &c.name;
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
    GhostUpdateSceneCB(g_pipeline, &sceneCB);

    // ---- Draw scene objects (one draw per run of faces that share a material) ----
    unsigned int slot = 0;
    for (const GhostObject& o : g_scene.objects)
    {
        if (!o.visible && g_playState != GhostPlayState::Editing) continue;   // hidden in game
        const int       faceCount = GhostMesh_FaceCount(o.meshId);
        const GhostMat4 model = GhostObjectModel(o);
        const bool      selected = overlays && (o.id == g_scene.selectedId);
        const int       hl = selected ? CurrentFace() : -1;

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
            cb.uvParams[0] = m.tiling[0]; cb.uvParams[1] = m.tiling[1];
            cb.uvParams[2] = m.offset[0]; cb.uvParams[3] = m.offset[1];
            cb.mapParams[0] = (float)m.mapMode;
            cb.mapParams[1] = m.rotationDeg * d2r;
            cb.mapParams[2] = (float)m.wrap;
            cb.mapParams[3] = (float)m.filter;

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
    }
    if (custom)
    {
        GhostEndOffscreen(g_rhi, g_pipeline, vp);
        GhostDrawBlit(g_rhi, g_pipeline, vp, fx, fy, fw, fh);
    }
    g_rhi->EndFrame();
}

void GhostEngine_Tick()
{
    if (!g_rhi || !g_pipeline) return;

    // ---- Delta time ----
    LARGE_INTEGER curr;
    QueryPerformanceCounter(&curr);
    float dt = (float)(curr.QuadPart - g_prev.QuadPart) / (float)g_freq.QuadPart;
    g_prev = curr;
    // ---- Play mode: sample input, then run the fixed-timestep simulation ----
    g_input.Sample(g_inputEnabled && g_playState == GhostPlayState::Playing);
    if (g_playState == GhostPlayState::Playing)
    {
        const int steps = g_stepper.Advance(dt);
        for (int i = 0; i < steps; i++)
        {
            GhostRuntime_TestStep(g_scene, g_input, g_testState, GHOST_FIXED_DT);   // TEMP: the game module replaces this in 4C
            g_input.EndStep();
        }
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

    if (flyEnabled && (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
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
            g_yaw += dx * lookSens;     // was -=
            g_pitch -= dy * lookSens;
            const float limit = 3.1415926535f * 0.49f;
            if (g_pitch > limit) g_pitch = limit;
            if (g_pitch < -limit) g_pitch = -limit;
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
    if (down(VK_SPACE))   g_eye.y += speed;
    if (down(VK_CONTROL)) g_eye.y -= speed;
    // ---- Working state -> active camera ----
    if (cam)
    {
        cam->position = g_eye;
        cam->yawDeg = fmodf(g_yaw * (180.0f / GHOST_PI), 360.0f);
        cam->pitchDeg = g_pitch * (180.0f / GHOST_PI);
    }
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
    strncpy_s(buf, bufSize, GhostMesh_Face(o->meshId, face).name, _TRUNCATE);
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
             m.tint[0], m.tint[1], m.tint[2] };
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

    const int from = face < 0 ? 0 : face;
    const int to = face < 0 ? count - 1 : face;
    for (int f = from; f <= to; f++) o->faces[f] = m;
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
        GhostMat4 inv = ObjectInverse(ob);
        float t;
        if (RayAABB(XformPoint(inv, o, 1.0f), XformPoint(inv, d, 0.0f),
            kMeshMin[ob.meshId], kMeshMax[ob.meshId], t) && t < bestT)
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
int GhostEngine_AddCamera(int sx, int sy)
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
         c.resW, c.resH };
            return 1;
        }
    return 0;
}

void GhostEngine_SetCamera(const GhostCameraInfo* in)
{
    if (!in) return;
    for (GhostCamera& c : g_scene.cameras)
        if (c.id == in->id)
        {
            c.position = { in->px, in->py, in->pz };
            c.yawDeg = in->yawDeg;
            c.pitchDeg = in->pitchDeg < -88.0f ? -88.0f : (in->pitchDeg > 88.0f ? 88.0f : in->pitchDeg);
            c.fovDeg = in->fovDeg < 10.0f ? 10.0f : (in->fovDeg > 120.0f ? 120.0f : in->fovDeg);
            if (in->resW <= 0 || in->resH <= 0) { c.resW = 0; c.resH = 0; }
            else
            {
                c.resW = in->resW < 16 ? 16 : (in->resW > 4096 ? 4096 : in->resW);
                c.resH = in->resH < 16 ? 16 : (in->resH > 4096 ? 4096 : in->resH);
            }
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
static std::string g_projectRoot;

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
    return 1;
}

int GhostEngine_LoadScene(const char* path, char* msg, int msgSize)
{
    if (!g_rhi || !g_pipeline || !path) { CopyMsg("The engine is not ready.", msg, msgSize); return 0; }
    GhostEngine_Stop();
    GhostScene backup = g_scene;
    std::string err, warn;
    bool ok = GhostScene_Load(g_scene, g_projectRoot, path, LoadSceneTexture,
        MAX_SCENE_OBJECT_COUNT, err, warn);
    if (!ok)
    {
        // Restore everything except the texture registry: textures loaded during the attempt stay on the GPU
        std::vector<GhostTextureInfo> tex = g_scene.textures;
        g_scene = backup;
        g_scene.textures = tex;
        CopyMsg(err, msg, msgSize);
        return 0;
    }

    g_hoverId = 0; g_dragging = false; g_dragId = 0; g_inputCameraId = 0;
    g_scene.version++;
    CopyMsg(warn, msg, msgSize);
    return warn.empty() ? 1 : 2;
}

void GhostEngine_ClearScene()
{
    GhostEngine_Stop();
    g_scene.objects.clear();
    g_scene.lights.clear();
    g_scene.cameras.clear();
    g_scene.ambientColor[0] = g_scene.ambientColor[1] = g_scene.ambientColor[2] = 1.0f;
    g_scene.ambientIntensity = 0.15f;
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
//  Exported: play mode
// ============================================================
int GhostEngine_Play()
{
    if (!g_rhi || !g_pipeline) return 0;
    if (g_playState != GhostPlayState::Editing) return 1;

    g_playBackup = g_scene;                          // Stop puts this back
    GhostRuntime_TestBegin(g_scene, g_testState);
    g_stepper.Reset();
    g_input.Reset();
    g_hoverId = 0; g_dragging = false; g_dragId = 0;
    g_playState = GhostPlayState::Playing;
    return 1;
}

void GhostEngine_Stop()
{
    if (g_playState == GhostPlayState::Editing) return;

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
             o->colliderSize.x, o->colliderSize.y, o->colliderSize.z, o->visible ? 1 : 0 };
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
    o->visible = (in->visible != 0);
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