// GhostEngine.cpp  — updated for IGhostRHI
#include "../include/GhostEngine.h"
#include "../include/IGhostRHI.h"
#include "../include/Ghost3D1.h"
#include "../include/GhostMath.h"
#include "../include/GhostPipeline.h"
#include "../include/GhostScene.h"
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

static const float ICON_HALF_PX = 18.0f;
static const float ICON_PICK_PX = ICON_HALF_PX + 4.0f;
// the last 2 * GHOST_MAX_LIGHTS constant-buffer slots are reserved for light icons + arrows
static const unsigned int MAX_SCENE_OBJECTS = GHOST_MAX_OBJECTS - 2 * GHOST_MAX_LIGHTS;

// local-space bounds of each mesh (picking only)
static const GhostVec3 kMeshMin[GHOST_MESH_COUNT] = { { -0.5f, -0.5f, -0.5f }, { -0.5f, 0.0f, -0.05f }, { -3.2f, -1.0f, -3.0f } };
static const GhostVec3 kMeshMax[GHOST_MESH_COUNT] = { {  0.5f,  0.5f,  0.5f }, {  0.5f, 1.0f,  0.05f }, {  3.2f,  3.0f, 11.0f } };
static GhostColor g_clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };

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
    if (!g_haveCam || g_width == 0 || g_height == 0) return false;
    float xn = 2.0f * (float)sx / (float)g_width - 1.0f;
    float yn = 1.0f - 2.0f * (float)sy / (float)g_height;
    dir = (g_camR * (xn / g_proj.m[0][0]) + g_camU * (yn / g_proj.m[1][1]) + g_camF).Normalize();
    origin = g_eye;
    return true;
}

static bool WorldToScreen(const GhostVec3& p, float& sx, float& sy)
{
    GhostVec3 v = XformPoint(g_view, p, 1.0f);
    if (v.z < 0.1f) return false;
    float nx = g_proj.m[0][0] * v.x / v.z;
    float ny = g_proj.m[1][1] * v.y / v.z;
    sx = (nx * 0.5f + 0.5f) * (float)g_width;
    sy = (1.0f - (ny * 0.5f + 0.5f)) * (float)g_height;
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

// Ground-plane (y = 0) hit under a screen position, or a point 5 units ahead if the ray misses
static GhostVec3 SpawnPoint(int sx, int sy)
{
    if (sx < 0 || sy < 0) { sx = (int)g_width / 2; sy = (int)g_height / 2; }
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
    cb.model.m[1][0] = (float)g_width; cb.model.m[1][1] = (float)g_height;
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

void GhostEngine_MouseDown(int x, int y, int button)
{
    if (button != 0 || g_looking || !g_haveCam) return;

    int id = PickAt(x, y);
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

void GhostEngine_MouseMove(int x, int y)
{
    if (g_looking || !g_haveCam) { g_hoverId = 0; return; }

    if (g_dragging)
    {
        GhostVec3* pos = EntityPosition(g_dragId);
        GhostVec3 o, d, hit;
        if (pos && ScreenRay(x, y, o, d) && RayPlane(o, d, g_dragPlanePoint, hit))
            *pos = hit + g_dragOffset;
        return;
    }
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
    if (g_scene.objects.size() >= MAX_SCENE_OBJECTS) return 0;
    GhostObject* o = GhostScene_AddObject(g_scene, meshId);
    if (!o) return 0;

    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += -kMeshMin[meshId].y * o->scale.y;       // rest the mesh on the ground
    o->position = p;
    g_scene.selectedId = o->id;
    return o->id;
}

int GhostEngine_AddLight(int lightType, int sx, int sy)
{
    if (lightType < 0 || lightType > 2) return 0;
    GhostLight* l = GhostScene_AddLight(g_scene, (GhostLightType)lightType);
    if (!l) return 0;

    GhostVec3 p = SpawnPoint(sx, sy);
    p.y += 1.5f;
    l->position = p;
    g_scene.selectedId = l->id;
    return l->id;
}

void GhostEngine_RemoveEntity(int id)
{
    if (!GhostScene_Remove(g_scene, id)) return;
    if (g_hoverId == id) g_hoverId = 0;
    if (g_dragId == id) { g_dragging = false; g_dragId = 0; }
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
    return n;
}

int GhostEngine_GetEntityName(int id, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return 0;
    const std::string* name = nullptr;
    for (const GhostObject& o : g_scene.objects) if (o.id == id) name = &o.name;
    for (const GhostLight& l : g_scene.lights)  if (l.id == id) name = &l.name;
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
                     o.color[0], o.color[1], o.color[2] };
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
            if (o.color[0] != in->r || o.color[1] != in->g || o.color[2] != in->b)
                o.useVertexColor = false;              // editing the color switches off vertex colors
            o.color[0] = in->r; o.color[1] = in->g; o.color[2] = in->b;
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

void GhostEngine_Tick()
{
    if (!g_rhi || !g_pipeline) return;

    // ---- Delta time ----
    LARGE_INTEGER curr;
    QueryPerformanceCounter(&curr);
    float dt = (float)(curr.QuadPart - g_prev.QuadPart) / (float)g_freq.QuadPart;
    g_prev = curr;

    // ---- Mouse look ----
    const float lookSens = 0.0025f;
    const float moveSpeed = 3.5f;

    if (g_inputEnabled && (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
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
    auto down = [&](int vk) { return g_inputEnabled && (GetAsyncKeyState(vk) & 0x8000) != 0; };
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

    // ---- Matrices ----
    GhostVec3 target = g_eye + forward;
    GhostMat4 view = GhostMat4::LookAtLH(g_eye, target, worldUp);
    float aspect = (g_height > 0) ? (float)g_width / (float)g_height : 1.0f;
    GhostMat4 proj = GhostMat4::PerspectiveFovLH(
        3.1415926535f / 4.0f, aspect, 0.1f, 100.0f);
    g_view = view;
    g_proj = proj;
    g_camR = right;
    g_camU = forward.Cross(right);
    g_camF = forward;
    g_haveCam = true;

    // ---- Begin frame ----
    D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        g_pipeline->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    g_rhi->BeginFrame(&g_clearColor, dsv);
    GhostSceneCB sceneCB;
    BuildSceneCB(g_scene, sceneCB);
    GhostUpdateSceneCB(g_pipeline, &sceneCB);
    // ---- Draw scene objects ----
    unsigned int slot = 0;
    for (const GhostObject& o : g_scene.objects)
    {
        if (slot >= MAX_SCENE_OBJECTS) break;

        GhostObjectCB cb = {};
        cb.model = GhostObjectModel(o);
        cb.view = view;
        cb.proj = proj;
        cb.color[0] = o.color[0]; cb.color[1] = o.color[1];
        cb.color[2] = o.color[2]; cb.color[3] = 1.0f;
        cb.useVertexColor = o.useVertexColor ? 1.0f : 0.0f;
        cb.pad[0] = (o.id == g_scene.selectedId) ? 1.0f : 0.0f;    // selection tint (VS)
        GhostUpdateObjectCB(g_pipeline, slot, &cb);
        GhostDrawMesh(g_rhi, g_pipeline, o.meshId, slot);
        slot++;
    }
    // ---- Light icons (unlit, always on top) ----
    for (const GhostLight& l : g_scene.lights)
    {
        int state = (l.id == g_scene.selectedId) ? 2 : (l.id == g_hoverId ? 1 : 0);
        GhostVec3 fwd = LightForward(l);
        DrawIcon(slot, (int)l.type, l.position, fwd, l.color, state, view, proj);
        if (l.type == GhostLightType::Directional)
            DrawIcon(slot, 3, l.position, fwd, l.color, state, view, proj);   // direction arrow
    }
    // ---- End frame ----
    g_rhi->EndFrame();
}

void GhostEngine_Resize(unsigned int width, unsigned int height)
{
    if (!g_rhi || !g_pipeline) return;
    if (width == 0 || height == 0) return;
    g_width = width;
    g_height = height;
    g_rhi->Resize(width, height);
    GhostResizeDepthBuffer(g_rhi, g_pipeline, width, height);
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