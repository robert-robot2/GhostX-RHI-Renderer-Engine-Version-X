// GhostEngine.h
#pragma once
#include <Windows.h>

#ifdef GHOSTENGINE_EXPORTS
#define GHOST_API __declspec(dllexport)
#else
#define GHOST_API __declspec(dllimport)
#endif

struct GhostObjectInfo
{
    int   id, meshId;
    float px, py, pz;
    float rx, ry, rz;       // rotation, degrees
    float sx, sy, sz;
    float r, g, b;
};

struct GhostLightInfo
{
    int   id, type;         // 0 directional, 1 point, 2 rect
    float px, py, pz;
    float yawDeg, pitchDeg;
    float r, g, b;
    float intensity, range;
    float width, height;
};

struct GhostEntityInfo { int id; int kind; int subtype; };   // kind: 0 object, 1 light, 2 camera.
struct GhostMaterialInfo
{
    int   textureId, mapMode, wrap, filter;
    float tilingU, tilingV, offsetU, offsetV;
    float rotationDeg;
    float r, g, b;                     // tint
};
struct GhostCameraInfo
{
    int   id;
    float px, py, pz;
    float yawDeg, pitchDeg;
    float fovDeg;
    int   isActive;
};

extern "C"
{
    GHOST_API int  GhostEngine_Init(HWND hwnd, unsigned int width, unsigned int height);
    GHOST_API void GhostEngine_SetClearColor(float r, float g, float b);
    GHOST_API void GhostEngine_Tick();
    GHOST_API void GhostEngine_Resize(unsigned int width, unsigned int height);
    GHOST_API void GhostEngine_Shutdown();
    // ---- Input (client coordinates of the render panel) ----
    GHOST_API void GhostEngine_SetInputEnabled(int enabled);
    GHOST_API void GhostEngine_MouseDown(int x, int y, int button);   // 0 = left
    GHOST_API void GhostEngine_MouseMove(int x, int y);
    GHOST_API void GhostEngine_MouseUp(int x, int y, int button);
    GHOST_API void GhostEngine_MouseLeave();

    // ---- Scene ----
    // Spawn at a screen position (ground-plane hit); pass -1, -1 for screen center. Returns id, 0 on failure.
    GHOST_API int  GhostEngine_AddObject(int meshId, int screenX, int screenY);
    GHOST_API int  GhostEngine_AddLight(int lightType, int screenX, int screenY);
    GHOST_API int  GhostEngine_RemoveEntity(int id);      // returns 1 if removed (0 = refused, e.g. the last camera)

    // ---- Cameras ----
    GHOST_API int  GhostEngine_AddCamera(int screenX, int screenY);   // id, or 0 if at the limit of 4
    GHOST_API int  GhostEngine_GetCamera(int id, GhostCameraInfo* out);
    GHOST_API void GhostEngine_SetCamera(const GhostCameraInfo* in);  // isActive is ignored
    GHOST_API void GhostEngine_SetActiveCamera(int id);
    GHOST_API int  GhostEngine_GetActiveCamera();
    GHOST_API void GhostEngine_SelectEntity(int id);
    GHOST_API int  GhostEngine_GetSelected();
    GHOST_API unsigned int GhostEngine_GetSceneVersion();
    GHOST_API int  GhostEngine_GetEntities(GhostEntityInfo* out, int maxCount);   // returns count written
    GHOST_API int  GhostEngine_GetEntityName(int id, char* buf, int bufSize);
    GHOST_API int  GhostEngine_GetObject(int id, GhostObjectInfo* out);
    GHOST_API void GhostEngine_SetObject(const GhostObjectInfo* in);
    GHOST_API int  GhostEngine_GetLight(int id, GhostLightInfo* out);
    GHOST_API void GhostEngine_SetLight(const GhostLightInfo* in);
    GHOST_API void GhostEngine_GetAmbient(float* r, float* g, float* b, float* intensity);
    GHOST_API void GhostEngine_SetAmbient(float r, float g, float b, float intensity);
    // ---- Textures ----
// rgba = tightly packed 8-bit RGBA, top row first, max 4096x4096.
// Returns the texture id (built in: 0 = white, 1 = checkerboard), or -1 on failure.
    GHOST_API int GhostEngine_CreateTexture(const char* name, const unsigned char* rgba,
        unsigned int width, unsigned int height);
    // textureId 0 clears the texture. Returns 1 on success.
    GHOST_API int GhostEngine_SetObjectTexture(int objectId, int textureId);
    GHOST_API int GhostEngine_GetTextureCount();
    // Returns 1 and fills name/size if the id exists
    GHOST_API int GhostEngine_GetTextureInfo(int id, char* nameBuf, int nameSize,
        unsigned int* width, unsigned int* height);
    // Texture id currently on an object (0 = none)
    GHOST_API int GhostEngine_GetObjectTexture(int objectId);
    // ---- Faces / materials ----
    GHOST_API int  GhostEngine_GetFaceCount(int objectId);
    GHOST_API int  GhostEngine_GetFaceName(int objectId, int face, char* buf, int bufSize);
    GHOST_API int  GhostEngine_GetMaterial(int objectId, int face, GhostMaterialInfo* out);     // face -1 = face 0
    GHOST_API void GhostEngine_SetMaterial(int objectId, int face, const GhostMaterialInfo* in); // face -1 = every face
    GHOST_API int  GhostEngine_GetTargetFace();                  // -1 = all faces
    GHOST_API void GhostEngine_SetTargetFace(int face);
    // Re-uploads pixels into an imported texture (id >= 3), keeping its id. Returns 1 on success.
    GHOST_API int GhostEngine_ReplaceTexture(int id, const char* name, const unsigned char* rgba,
        unsigned int width, unsigned int height);
    // Nearest object under a client position (light icons ignored). Returns 0 if none.
    // outFace = face under the cursor, or -1.
    GHOST_API int GhostEngine_PickObjectAt(int x, int y, int* outFace);
    // ---- Viewports (0 = main view, created by Init; 1..4 = camera windows) ----
    GHOST_API int  GhostEngine_CreateViewport(HWND hwnd, unsigned int width, unsigned int height, int cameraId);  // vp id, 0 = failed
    GHOST_API void GhostEngine_DestroyViewport(int vp);
    GHOST_API int  GhostEngine_RebindViewport(int vp, HWND hwnd, unsigned int width, unsigned int height);
    GHOST_API void GhostEngine_ResizeViewport(int vp, unsigned int width, unsigned int height);
    GHOST_API void GhostEngine_SetViewportVisible(int vp, int visible);
    GHOST_API void GhostEngine_SetInputViewport(int vp);     // viewport that has keyboard focus + cursor, -1 = none
}