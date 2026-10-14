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

struct GhostEntityInfo { int id; int kind; int subtype; };   // kind: 0 object, 1 light, 2 camera, 3 mod
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
    int   resW, resH;
    int   mode, targetId;                 // mode: 0 free, 1 orbit
    float distance, heightOffset, minDist, maxDist;
    float minPitchDeg, maxPitchDeg, smoothing;
};
struct GhostGameplayInfo
{
    int   id;
    int   role;                 // 0 static, 1 player, 2 enemy, 3 coin, 4 block, 5 goal, 6 custom
    int   collider;
    float csx, csy, csz;        // collider size (multiplied by the object's scale)
    int   visible;
};
struct GhostModParamInfo
{
    int   kind;                 // 0 float, 1 bool, 2 enum, 3 color, 4 texture
    float mn, mx;
    float def[3];
    int   count;                // floats used: 3 for colors, else 1
    char  name[48];
    char  category[32];
    char  enums[192];           // "A|B|C"
};
struct GhostSpriteInfo
{
    int   id;
    int   textureId, source, cols, rows, frame;
    int   flipX, billboard, unlit, alphaClip, pixelArt, playing;
    float tintR, tintG, tintB;
    float animSpeed;
    int   effTexture, effCols, effRows, curFrame;      // read-only: what is actually drawn
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
    GHOST_API int  GhostEngine_AddCamera(int mode, int screenX, int screenY);   // mode 0 free, 1 orbit (locks to an object under the drop point)
    GHOST_API int  GhostEngine_GetCameraTargetTag(int cameraId, char* buf, int bufSize);
    GHOST_API void GhostEngine_SetCameraTargetTag(int cameraId, const char* utf8Tag);
    GHOST_API int  GhostEngine_GetGameStartCamera();                 // 0 = keep the current main view camera
    GHOST_API void GhostEngine_SetGameStartCamera(int cameraId);
    GHOST_API void GhostEngine_MouseWheel(int delta);                // WinForms e.Delta (120 per notch)
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
    // ---- Scene files (all paths are UTF-8) ----
    GHOST_API void GhostEngine_SetProjectRoot(const char* path);
    // Return 0 = failed (msg = reason), 1 = ok, 2 = ok with warnings (msg = warnings, e.g. missing textures)
    GHOST_API int  GhostEngine_SaveScene(const char* path, char* msg, int msgSize);
    GHOST_API int  GhostEngine_LoadScene(const char* path, char* msg, int msgSize);
    GHOST_API void GhostEngine_ClearScene();                       // empty scene + one default camera
    GHOST_API unsigned int GhostEngine_GetSceneHash();
    GHOST_API void GhostEngine_SetTexturePath(int id, const char* path);   // remember where an imported texture came from
    GHOST_API int  GhostEngine_GetTexturePath(int id, char* buf, int bufSize);
    // ---- Play mode ----
    GHOST_API int  GhostEngine_Play();     // 0 = failed (see GetPlayMessage), 1 = playing with a game module, 2 = playing without one
    GHOST_API void GhostEngine_Stop();                 // restores the snapshot
    GHOST_API void GhostEngine_SetPaused(int paused);
    GHOST_API int  GhostEngine_GetPlayState();         // 0 editing, 1 playing, 2 paused
    // ---- Gameplay data ----
    GHOST_API int  GhostEngine_GetGameplay(int objectId, GhostGameplayInfo* out);
    GHOST_API void GhostEngine_SetGameplay(const GhostGameplayInfo* in);
    GHOST_API int  GhostEngine_GetCustomTag(int objectId, char* buf, int bufSize);
    GHOST_API void GhostEngine_SetCustomTag(int objectId, const char* utf8Tag);
    GHOST_API void GhostEngine_SetGameModulePath(const char* utf8Path);
    GHOST_API int  GhostEngine_GetPlayMessage(char* buf, int bufSize);   // last module message (path, or why it failed)
    GHOST_API int  GhostEngine_GetHudText(char* buf, int bufSize);       // what the game set with SetHudText
    GHOST_API int  GhostEngine_TakePlayFault(char* buf, int bufSize);    // 1 once if the module crashed (message in buf)
    GHOST_API int GhostEngine_DuplicateEntity(int id);     // objects only. New id, 0 = failed (or playing)
    // Registers the baked font atlas (texture must already exist). advances = 95 floats, ASCII 32..126. Returns 1 on success.
    GHOST_API int GhostEngine_SetFont(int textureId, int atlasW, int atlasH, float cellW, float cellH,
        float pad, float lineHeight, const float* advances);
    // ---- Mods ----
    GHOST_API int  GhostEngine_GetModTypeCount();
    GHOST_API int  GhostEngine_GetModTypeName(int typeId, char* buf, int bufSize);
    GHOST_API int  GhostEngine_GetModParamCount(int typeId);
    GHOST_API int  GhostEngine_GetModParamInfo(int typeId, int index, GhostModParamInfo* out);
    GHOST_API int  GhostEngine_AddMod(int typeId, int screenX, int screenY);   // 0 = failed; at the per-type limit it selects the existing one
    GHOST_API int  GhostEngine_GetMod(int id, float* outPos3);                 // returns the type id, -1 if not a mod
    GHOST_API void GhostEngine_SetModPosition(int id, const float* pos3);
    GHOST_API int  GhostEngine_GetModParam(int id, int index, float* out3);
    GHOST_API void GhostEngine_SetModParam(int id, int index, const float* in3);
    // ---- Sprites ----
    GHOST_API int  GhostEngine_GetSprite(int objectId, GhostSpriteInfo* out);
    GHOST_API void GhostEngine_SetSprite(const GhostSpriteInfo* in);
    // which: 0 animation path, 1 clip, 2 (get only) clip names joined with '|'
    GHOST_API int  GhostEngine_GetSpriteText(int objectId, int which, char* buf, int bufSize);
    GHOST_API void GhostEngine_SetSpriteText(int objectId, int which, const char* utf8);
    GHOST_API void GhostEngine_ReloadAnimations();
}