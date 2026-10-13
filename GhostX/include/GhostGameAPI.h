#pragma once
// GhostGameAPI.h: the interface between the GhostX engine and a game module (GhostGame.dll).
// Plain C, so the DLL doesn't depend on how the engine was compiled.
// The struct only ever grows at the END. `version` changes only if an existing field changes meaning.
//
// World: left-handed, Y up, +Z forward, +X right. Rotation is in degrees. Colors are 0..1.
// Hidden objects (SetVisible 0) are skipped by QueryBox. Colliders ignore rotation (axis-aligned boxes).

#ifdef __cplusplus
extern "C" {
#endif

#define GHOST_GAME_API_VERSION 1

#ifdef __cplusplus
#define GHOST_GAME_EXPORT extern "C" __declspec(dllexport)
#else
#define GHOST_GAME_EXPORT __declspec(dllexport)
#endif

    // Mesh ids for Spawn
#define GHOST_API_MESH_CUBE     0
#define GHOST_API_MESH_TRIANGLE 1
#define GHOST_API_MESH_SHIP     2
#define GHOST_API_MESH_SQUARE   3

    typedef struct GhostGameAPI
    {
        int version;                                       // GHOST_GAME_API_VERSION
        int size;                                          // sizeof(GhostGameAPI) as the engine built it

        // ---- core ----
        void   (*Log)(const char* utf8);                   // shows in the Visual Studio Output window
        double (*GetTime)(void);                           // seconds since Play started
        float  (*GetFixedDeltaTime)(void);                 // 1/60
        void   (*SetHudText)(const char* utf8);            // shown in the title bar until Phase 5 adds real text

        // ---- input (Windows virtual-key codes; letters are uppercase ASCII, e.g. 'W') ----
        int (*KeyDown)(int vk);
        int (*KeyPressed)(int vk);                         // true for the first step after the key goes down
        int (*KeyReleased)(int vk);

        // ---- finding objects (ids are valid until the object is destroyed) ----
        int (*FindByTag)(const char* tag);                 // first match, 0 = none. Tag = the Role name, or the Custom tag
        int (*FindAllByTag)(const char* tag, int* outIds, int maxCount);   // returns how many were written
        int (*GetTag)(int id, char* buf, int bufSize);
        int (*SetTag)(int id, const char* tag);

        // ---- object properties (return 1 on success, 0 if the id doesn't exist) ----
        int (*GetPosition)(int id, float* xyz);
        int (*SetPosition)(int id, const float* xyz);
        int (*GetRotation)(int id, float* xyzDeg);
        int (*SetRotation)(int id, const float* xyzDeg);
        int (*GetScale)(int id, float* xyz);
        int (*SetScale)(int id, const float* xyz);
        int (*GetColor)(int id, float* rgb);               // tint of the object's faces
        int (*SetColor)(int id, const float* rgb);         // sets every face's tint
        int (*GetVisible)(int id);
        int (*SetVisible)(int id, int visible);

        // ---- collision ----
        int (*GetBounds)(int id, float* outMin3, float* outMax3);   // world box of the collider; 0 if the object has no collider
        // Colliders overlapping the box. Touching faces don't count, so grow the box a little to detect contact.
        int (*QueryBox)(const float* boxMin3, const float* boxMax3, int* outIds, int maxCount);

        // ---- spawning ----
        int (*Spawn)(int meshId, const float* position3);  // new id, 0 = failed (object limit)
        int (*Destroy)(int id);

        // ---- cameras (a null pointer means "leave unchanged") ----
        int (*GetActiveCamera)(void);                      // the main view camera
        int (*SetActiveCamera)(int cameraId);
        int (*GetCamera)(int cameraId, float* outPos3, float* outYawPitchDeg2, float* outFovDeg);
        int (*SetCamera)(int cameraId, const float* pos3, const float* yawPitchDeg2, const float* fovDeg);
        // ---- mouse (while the right button is held during Play, the cursor is captured and these report its movement) ----
        void  (*GetMouseDelta)(float* outDxDy2);          // pixels moved since the previous step
        float (*GetMouseWheel)(void);                     // wheel notches since the previous step (+ = away from you)

        // ---- orbit cameras (the engine moves them: right-drag orbits, wheel zooms) ----
        int (*GetCameraMode)(int cameraId);               // 0 = free, 1 = orbit, -1 = no such camera
        int (*GetCameraTarget)(int cameraId);             // orbit target object id, 0 = none
        int (*SetCameraTarget)(int cameraId, int objectId);   // 0 clears it
        // ---- 2D drawing (reference space: 1080 units tall, origin top-left, y down) ----
// Call these from every GhostGame_Update. The list is rebuilt each step and drawn over the main view while playing.
// rgba4 = four floats 0..1 (null = white). Drawing order = call order.
        void  (*UIRect)(float x, float y, float w, float h, const float* rgba4);
        void  (*UIImage)(int textureId, float x, float y, float w, float h, const float* rgba4);
        void  (*UIText)(float x, float y, float size, const char* utf8, const float* rgba4);   // size = line height; ASCII only, '\n' = new line
        float (*UIMeasureText)(const char* utf8, float size);                                   // width of the widest line
        void  (*UIGetScreenSize)(float* outWH2);          // width follows the window's aspect ratio, height is always 1080
        int   (*LoadTexture)(const char* projectRelativePath);   // PNG / JPG / BMP, returns a texture id, 0 = failed. Call it in Init
        // ---- retained UI (.ghostui files, see Content/UI) ----
// Widgets are found by name (first match). Loaded UIs start shown, draw under the UIRect / UIText calls,
// and are reloaded from disk every time Play starts.
        int   (*UILoad)(const char* projectRelativePath);      // handle > 0, 0 = failed (see the Output window). Call in Init
        void  (*UIShow)(int ui, int visible);
        int   (*UISetText)(int ui, const char* widget, const char* utf8);
        int   (*UISetProgress)(int ui, const char* widget, float value01);
        int   (*UISetVisible)(int ui, const char* widget, int visible);
        int   (*UISetColor)(int ui, const char* widget, const float* rgba4);
        int   (*UIButtonClicked)(int ui, const char* widget); // 1 once per click, then 0 until the next one
        int   (*UIButtonDown)(int ui, const char* widget);    // 1 while the mouse is held on it
        int   (*UIGetMouse)(float* outXY2);                   // cursor in UIRect units, returns 1 if it is over the view
    } GhostGameAPI;

    // A game module must export:
    //   int  GhostGame_Init(const GhostGameAPI* api);   // return 1 = ok, 0 = failed (Play is cancelled)
    //   void GhostGame_Update(float dt);                // fixed 1/60 s step, only while playing (not paused)
    //   void GhostGame_Shutdown(void);                  // optional

#ifdef __cplusplus
}
#endif