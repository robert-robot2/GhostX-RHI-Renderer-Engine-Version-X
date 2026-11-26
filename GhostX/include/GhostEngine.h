// GhostEngine.h
#pragma once
#include "GhostVideoSettings.h"
#include "GhostRoleParams.h"
#include "GhostParticleMan.h"
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
    int   id, type;         // 0 directional, 1 point, 2 rect, 3 spot
    float px, py, pz;
    float yawDeg, pitchDeg;
    float r, g, b;
    float intensity, range;
    float width, height;
    float innerDeg, outerDeg;                 // spot cone
    int   shadowObjects, shadowLandscape;     // spot shadow toggles
    float spinYaw, spinPitch;                 // deg/sec while playing
    float volumetric;                         // god rays strength
    int   celestial;                          // 0 manual, 1 follows the Sky Sphere sun, 2 follows its moon (Directional only)
};
struct GhostFogInfo
{
    int   enabled;
    float density, colorR, colorG, colorB, brightness, followAmbient;
    float heightBase, heightFalloff, startDistance, maxDistance, skyFog;
    int   envMode;                            // 0 colour, 1 reflection capture, 2 texture
    float envAmount;
    int   envTexture;
    float envYaw;
    int   noiseTexture;
    float noiseAmount, noiseScale, scrollX, scrollZ;
    int   volLight;
    float volIntensity, anisotropy;
    int   steps;
};
struct GhostFlickerInfo
{
    int   enabled, pattern, smoothSteps, textureId;
    float speed, minLevel, baseInfluence, brightness;
    float r, g, b;
    float texSpeed, texTiling, texAmount;
    float diffuse, specular, roughness, metallic;
    float seed;
};

struct GhostEntityInfo { int id; int kind; int subtype; };   // kind: 0 object, 1 light, 2 camera, 3 mod
struct GhostMaterialInfo
{
    int   textureId, mapMode, wrap, filter;
    float tilingU, tilingV, offsetU, offsetV;
    float rotationDeg;
    float r, g, b;                     // tint
    float metallic, roughness, specular;
    float emR, emG, emB, emissiveStrength;
    int   alphaMode, alphaSource;
    float alphaCutoff;
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
    float moveSpeed;                      // editor fly speed (units/s)
};
struct GhostGameplayInfo
{
    int   id;
    int   role;                 // 0 static, 1 player, 2 enemy, 3 coin, 4 block, 5 goal, 6 custom, 7 portal entry, 8 portal exit, 9 breakable, 10 moving object, 11 projectile
    int   collider;
    float csx, csy, csz;        // collider size (multiplied by the object's scale)
    float cox, coy, coz;        // collider center offset (multiplied by the object's scale)
    int   visible;
    int   locked, pathMode, breakEffect, contents, portalTrigger;
    float moveX, moveY, moveZ, moveSpeed, movePause;
    int   powerType, itemMoves, coinCount;
    int   enemyPathMode;
    int   flying;
    int enemyDeathEffect;
    float crx, cry, crz;
    float timeOffset;           // NEW: last field, Timed Elevator offset in seconds
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
struct GhostMeshStats
{
    int          builtin, faceCount;
    unsigned int vertexCount, triangleCount;
    float        minX, minY, minZ, maxX, maxY, maxZ;       // local-space bounds
    float        importScale, importFit;
    int          importUp, importFlags;                    // flags: 1 flip winding, 2 no materials
};
struct GhostViewSettings
{
    int   showSelection, showFps, showGrid, showWorldAxes;
    int   showDims, dimAxes, edgeLabels;       // dimAxes: bit 0 = X, bit 1 = Y, bit 2 = Z
    int   gridPlane, gridStyle;                // plane: 0 XZ (floor), 1 XY, 2 YZ.  style: 0 standard, 1 blueprint
    float gridSize;                            // spacing between lines, in units (1 unit = 1 metre)
    int   gridExtent;                          // lines on each side of the camera
    int   tool, space, unit, decimals;         // tool: 0 select, 1 translate, 2 rotate, 3 scale, 4 dimension.  space: 0 world, 1 local.  unit: 0 mm, 1 cm, 2 m, 3 in, 4 ft
};
struct GhostGizmoSettings
{
    int   snap;                // 1 = snapping on
    float moveStep;            // 0 = use the grid size
    float rotStep;             // degrees, 0 = off
    float scaleStep;           // factor step, 0 = off
};
struct GhostPadInfo
{
    int   connected, slot;
    int   buttons;                 // bit i = GHOST_PAD_* button i
    float axes[6];                 // LX LY RX RY LT RT
    int   raw;                     // physical button bits of a generic pad (bit n = button n + 1)
};
struct GhostSoundInfo
{
    int   bus, kind, enabled;      // bus: 1 music, 2 sfx, 3 ambience. kind: 0 sfx, 1 music, 2 ambience
    float volume, pitchVar;
    char  name[48];
    char  file[260];
};

typedef int(__cdecl* GhostFontProviderFn)(const char* utf8AbsPath);   // returns a font slot, 0 = failed
typedef void(__cdecl* GhostLoadProgressFn)(const char* utf8Kind, const char* utf8Name);
struct GhostShadowTuning { int filter; float softness, bias, normalBias, distance; };
struct GhostMapInfo { int textureId; float amount; int channel; int invert; };

struct GhostPostInfo
{
    int   toneMode;                                   // 0 none, 1 Reinhard, 2 ACES, 3 Neutral
    float exposureEV, whitePoint;
    int   bloomEnabled;
    float bloomThreshold, bloomKnee, bloomIntensity, bloomScatter;
    int   bloomMips;
    float bloomTintR, bloomTintG, bloomTintB;
    int   ssaoEnabled;
    float ssaoRadius, ssaoIntensity, ssaoBias, ssaoStrength;
    int   ssaoSamples;
    float ssaoDirectFade;
    int   ssaoHalfRes, ssaoShowOnly;
    float ssaoFadeDist;
    int   ssrEnabled;
    float ssrIntensity, ssrMaxDistance, ssrThickness;
    int   ssrSteps;
    float ssrMaxRoughness, ssrEdgeFade;
    int   ssgiEnabled;
    float ssgiIntensity, ssgiRadius;
    int   ssgiSamples;
    float ssgiTemporal, ssgiFadeDist;
    int   ssgiShowOnly;
    int   shaftEnabled;
    float shaftIntensity, shaftThreshold, shaftMaxBright, shaftLength, shaftDecay, shaftDarkness, shaftDepthRange;
    int   shaftSteps, shaftOcclusion;
};

struct GhostSurfaceInfo
{
    int   twoSided;
    float opacity, refractionIOR, reflectionRotDeg, anisotropy;
    float sssR, sssG, sssB, sssAmount;
    float mapTilingU, mapTilingV, mapOffsetU, mapOffsetV;
    float wpoSway, wpoSpeed, wpoMid;
    float sssTransmission, sssDensity, sssThickness;   // NEW (keep last)
};

struct GhostTileMapInfo
{
    int   id;
    float tileSize;
    int   gridX, gridY, gridZ;
    int   direction;            // 0 X, 1 Y (floor), 2 Z
    int   receiveShadows;       // Landscape Shadows on/off
    int   shadowLightId;        // 0 = all lights, otherwise a directional light's id
    int   layerCount;           // read-only here: use Add / RemoveTileLayer
    int   subdiv;               // terrain quads per tile side
    int   autoCollider;
};
struct GhostPaintInfo
{
    int   active;               // paint mode on
    int   tool;                 // 0 raise, 1 lower, 2 smooth, 3 flatten, 4 set height, 5 paint layer, 6 erase layer
    float radius, strength, fade, setHeight;
    int   layer;                // the Active Layer (read-only here: set through SetTargetFace)
    int   layerCount;           // layers of the selected Tile Map (0 = none selected)
    int   hasTileMap;
};

struct GhostVolumeInfo
{
    int   id, type, shape, enabled, priority;   // type: 0 reflection capture, 1 fog, 2 post process.  shape: 0 box, 1 sphere
    float px, py, pz;
    float rx, ry, rz;                           // degrees
    float sx, sy, sz;                           // box size
    float radius, blend;
    float p[16];                                // per-type parameters (later phases)
    int   tex[4];                               // per-type texture ids (later phases)
};

struct GhostWorldInfo
{
    int   streaming, lod, cull, showCells, maxLoads;
    float loadDist, unloadPad, msBudget, lodDist1, lodDist2, farPlane, nearPlane;
};
struct GhostWorldStatsInfo
{
    int   cells, resident, slotsTotal, starved, loadsLast, cellsDrawn, bandsDrawn, bandsCulled, trisDrawn, objectsCulled;
    float lastLoadMs;
};

struct GhostFoliageLayerInfo
{
    int   id, kind, meshId, enabled;
    int   billboard, alphaClip, unlit, pixelArt;
    int   cols, rows, animated;
    float animFps;
    float sizeMin, sizeMax, yawRange, tiltRandom, alignToNormal, slopeMax;
    float scatterRadius;
    int   amount;
    float minSpacing, heightOffset;
    int   restOnGround;
    float cullDistance, fadeLength, colorVariation, normalUp;
    int   castShadows, shadowLightId, receiveShadows;
    float shadowDistance;
    int   instanceCount, chunkCount;     // read-only
};
struct GhostFoliageBrushInfo { int active, erase, onLandscape, onObjects, eraseAll, activeLayerId; };
struct GhostFoliageStatsInfo { int layers, chunks, instances, chunksDrawn, instancesDrawn, shadowChunks; };


extern "C"
{
    GHOST_API int  GhostEngine_Init(HWND hwnd, unsigned int width, unsigned int height);
    GHOST_API void GhostEngine_SetClearColor(float r, float g, float b);
    GHOST_API void GhostEngine_Tick();
    GHOST_API void GhostEngine_Resize(unsigned int width, unsigned int height);
    GHOST_API void GhostEngine_Shutdown();
    GHOST_API int  GhostEngine_PurgeProjectAssets();
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

    GHOST_API int  GhostEngine_AddTileMap(int screenX, int screenY);         // Maps > Tile Map. New id, 0 = failed

    GHOST_API int  GhostEngine_AddVolume(int type, int screenX, int screenY);   // Utilities palette. New id, 0 = failed
    GHOST_API int  GhostEngine_GetVolume(int id, GhostVolumeInfo* out);         // 0 = not a volume
    GHOST_API void GhostEngine_SetVolume(const GhostVolumeInfo* in);

    GHOST_API void GhostEngine_CaptureProbe(int volumeId);     // Reflection Capture: capture now (works for baked and realtime)
    GHOST_API int  GhostEngine_GetProbeStatus(int volumeId);   // -1 not a probe, 0 waiting, 1 captured, 2 no free slot (max 8), 3 unavailable, 4 disabled

    GHOST_API int  GhostEngine_GetTileMap(int objectId, GhostTileMapInfo* out);   // 0 = not a Tile Map
    GHOST_API void GhostEngine_SetTileMap(const GhostTileMapInfo* in);
    GHOST_API int  GhostEngine_AddTileLayer(int objectId);                        // new layer index, -1 = refused (limit / not a Tile Map)
    GHOST_API int  GhostEngine_RemoveTileLayer(int objectId, int layer);          // 1 = removed
    GHOST_API int  GhostEngine_AssignTileLayer(int objectId, int layer, int col0, int row0, int col1, int row1);   // returns tiles changed
    GHOST_API int  GhostEngine_GetTileLayerCounts(int objectId, int* outCounts, int maxCount);   // returns layer count, outCounts[l] = tiles using layer l
    GHOST_API void GhostEngine_SetPaintMode(int on);
    GHOST_API int  GhostEngine_GetPaintMode();
    GHOST_API void GhostEngine_GetPaintSettings(GhostPaintInfo* out);
    GHOST_API void GhostEngine_SetPaintSettings(const GhostPaintInfo* in);
    GHOST_API void GhostEngine_ResetTerrain(int objectId, int what);       // 1 heights, 2 weights, 3 both

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

    GHOST_API int  GhostEngine_GetFlicker(int lightId, int layer, GhostFlickerInfo* out);        // layer 0 or 1
    GHOST_API void GhostEngine_SetFlicker(int lightId, int layer, const GhostFlickerInfo* in);
    GHOST_API int  GhostEngine_GetFlickerText(int lightId, int layer, char* buf, int bufSize);   // custom pattern letters
    GHOST_API void GhostEngine_SetFlickerText(int lightId, int layer, const char* utf8);

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

    GHOST_API int  GhostEngine_GetPlayerParams(int objectId, GhostPlayerParams* out);
    GHOST_API void GhostEngine_SetPlayerParams(int objectId, const GhostPlayerParams* in);
    GHOST_API int  GhostEngine_GetEnemyParams(int objectId, GhostEnemyParams* out);
    GHOST_API void GhostEngine_SetEnemyParams(int objectId, const GhostEnemyParams* in);

    GHOST_API int  GhostEngine_GetBossParams(int objectId, GhostBossParams* out);
    GHOST_API void GhostEngine_SetBossParams(int objectId, const GhostBossParams* in);
    GHOST_API int  GhostEngine_GetPathParams(int objectId, GhostPathParams* out);
    GHOST_API void GhostEngine_SetPathParams(int objectId, const GhostPathParams* in);


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
    // ---- Shadows ----
    GHOST_API int  GhostEngine_GetObjectShadow(int objectId, int* mode, int* lightId, int* receive);   // mode: 0 off, 1 all lights, 2 one light
    GHOST_API void GhostEngine_SetObjectShadow(int objectId, int mode, int lightId, int receive);
    GHOST_API int  GhostEngine_GetLightShadows(int lightId);                  // 1 casts, 0 doesn't, -1 no such light
    GHOST_API void GhostEngine_SetLightShadows(int lightId, int on);
    GHOST_API void GhostEngine_GetShadowSettings(int* enabled, float* strength);
    GHOST_API void GhostEngine_SetShadowSettings(int enabled, float strength);

    GHOST_API int  GhostEngine_GetShadowResolution();            // 512, 1024, 2048 or 4096
    GHOST_API void GhostEngine_SetShadowResolution(int size);    // snapped to the nearest allowed size

    GHOST_API void GhostEngine_GetShadowTuning(GhostShadowTuning* out);
    GHOST_API void GhostEngine_SetShadowTuning(const GhostShadowTuning* in);

    GHOST_API int  GhostEngine_GetMap(int objectId, int face, int slot, GhostMapInfo* out);        // face -1 = face 0
    GHOST_API void GhostEngine_SetMap(int objectId, int face, int slot, const GhostMapInfo* in);   // face -1 = every face
    GHOST_API int  GhostEngine_GetSurface(int objectId, int face, GhostSurfaceInfo* out);
    GHOST_API void GhostEngine_SetSurface(int objectId, int face, const GhostSurfaceInfo* in);


    // ---- Models ----
// Returns the new mesh id (>= 5), or -1 on failure (msg = the reason). On success msg holds a summary.
// upAxis: 0 auto, 1 Y up, 2 Z up. fitSize 0 = off.
    GHOST_API int GhostEngine_ImportModel(const char* utf8Path, float scale, float fitSize, int upAxis,
        int flipWinding, char* msg, int msgSize);
    // Re-reads the model's source file. useStored 1 = the original import options. Returns the mesh id, or -1 (msg = reason).
    GHOST_API int  GhostEngine_ReimportModel(int meshId, int useStored, float scale, float fitSize, int upAxis, int flags,
        char* msg, int msgSize);
    GHOST_API void GhostEngine_ApplyMeshDefaults(int objectId, int what);   // 1 materials, 2 collider, 3 both

    // Loads a converted .ghostmesh (or finds it if it is already loaded). Returns the mesh id, or -1 (msg = reason).
    GHOST_API int  GhostEngine_LoadModelFile(const char* utf8Path, char* msg, int msgSize);
    GHOST_API int  GhostEngine_GetMeshCount();
    GHOST_API int  GhostEngine_GetMeshInfo(int meshId, GhostMeshStats* out);
    GHOST_API int  GhostEngine_GetMeshText(int meshId, int which, char* buf, int bufSize);   // 0 name, 1 mesh file, 2 source file
    GHOST_API int  GhostEngine_GetObjectMissingMesh(int objectId, char* buf, int bufSize);   // 1 if the object is a placeholder
    GHOST_API int GhostEngine_TakeQuitRequest();     // 1 once after the game called Quit()

    // ---- Viewport overlay settings ----
    GHOST_API void GhostEngine_GetViewSettings(GhostViewSettings* out);
    GHOST_API void GhostEngine_SetViewSettings(const GhostViewSettings* in);

    // Gizmos
    GHOST_API void GhostEngine_GetGizmoSettings(GhostGizmoSettings* out);
    GHOST_API void GhostEngine_SetGizmoSettings(const GhostGizmoSettings* in);

    // ---- Dimension editing (13.C) ----
// 1 once after a dimension label or an edge was clicked. kind: 0 = axis dimension (index 0..2 = X Y Z), 1 = polygon edge (index 0 = A, 1 = B ...).
    GHOST_API int  GhostEngine_TakeDimClick(int* kind, int* index, int* objectId, float* worldLen);
    GHOST_API int  GhostEngine_GetObjectSize(int objectId, float* xyz);             // world size per axis (mesh bounds x scale)
    GHOST_API void GhostEngine_SetObjectSize(int objectId, int axis, float worldSize);
    GHOST_API void GhostEngine_ScaleObject(int objectId, float factor);             // uniform

    GHOST_API int  GhostEngine_GetLinkId(int objectId, char* buf, int bufSize);
    GHOST_API void GhostEngine_SetLinkId(int objectId, const char* utf8);
    // Ctrl+F array: creates `count` copies of an object. offsets = x,y,z per copy, relative to the source.
// outIds (optional, size count) receives the new ids. Returns how many were created.
    GHOST_API int GhostEngine_DuplicateArray(int id, const float* offsets, int count, int* outIds);
    // ---- Editor: undo, clipboard, focus, rename, face mode ----
    GHOST_API int  GhostEngine_Undo();
    GHOST_API int  GhostEngine_Redo();
    GHOST_API int  GhostEngine_UndoCount();
    GHOST_API int  GhostEngine_RedoCount();
    GHOST_API int  GhostEngine_CopySelected();
    GHOST_API int  GhostEngine_Paste();
    GHOST_API int  GhostEngine_FocusSelected();
    GHOST_API void GhostEngine_SetEntityName(int id, const char* utf8);
    GHOST_API void GhostEngine_SetFaceMode(int on);
    GHOST_API int  GhostEngine_GetFaceMode();

    GHOST_API int  GhostEngine_GetSelectedCount();
    GHOST_API int  GhostEngine_GetSelectedIds(int* outIds, int maxCount);   // the primary is first
    GHOST_API void GhostEngine_SetSelection(const int* ids, int count);
    GHOST_API void GhostEngine_SetColliderMode(int on);
    GHOST_API int  GhostEngine_GetColliderMode();

    // ---- Antialiasing ----
    GHOST_API void GhostEngine_SetMSAA(int samples);      // 1 = off, 2 / 4 / 8. Applied before the next frame; clamped to what the GPU supports
    GHOST_API int  GhostEngine_GetMSAA();                 // the sample count currently in use
    GHOST_API int  GhostEngine_GetMSAAMax();              // highest supported: 1, 2, 4 or 8

    GHOST_API void  GhostEngine_SetTAA(int on);                    // 1 = TAA (forces MSAA off)
    GHOST_API int   GhostEngine_GetTAA();
    GHOST_API void  GhostEngine_SetTAAJitterSpeed(float speed);    // 0.05 .. 4, default 1
    GHOST_API float GhostEngine_GetTAAJitterSpeed();

    GHOST_API void GhostEngine_SetSMAA(int on);            // 1 = SMAA (forces MSAA and TAA off)
    GHOST_API int  GhostEngine_GetSMAA();
    GHOST_API void GhostEngine_SetSMAAQuality(int q);      // 0 low, 1 medium, 2 high (default), 3 ultra
    GHOST_API int  GhostEngine_GetSMAAQuality();
    GHOST_API int  GhostEngine_GetSMAAAvailable();         // 1 if the SMAA shaders compiled

    GHOST_API void GhostEngine_SetFXAA(int on);            // 1 = FXAA (forces MSAA, TAA and SMAA off)
    GHOST_API int  GhostEngine_GetFXAA();
    GHOST_API void GhostEngine_SetFXAAQuality(int q);      // 0 low, 1 default, 2 high
    GHOST_API int  GhostEngine_GetFXAAQuality();
    GHOST_API int  GhostEngine_GetFXAAAvailable();         // 1 if the FXAA shader compiled

    // ---- Particle systems ----
    GHOST_API int  GhostEngine_AddParticleSystem(int type, int screenX, int screenY);   // 0 fire, 1 snow, 2 custom
    GHOST_API int  GhostEngine_AddParticleAt(int type, float x, float y, float z);
    GHOST_API int  GhostEngine_RemoveParticle(int id);
    GHOST_API int  GhostEngine_GetParticleCount();
    GHOST_API int  GhostEngine_GetParticleIds(int* outIds, int maxCount);
    GHOST_API int  GhostEngine_GetParticle(int id, GhostParticleParams* out);
    GHOST_API void GhostEngine_SetParticle(const GhostParticleParams* in);
    GHOST_API int  GhostEngine_GetParticleText(int id, int which, char* buf, int bufSize);   // 0 anim file, 1 clip, 2 clip names '|'
    GHOST_API void GhostEngine_SetParticleText(int id, int which, const char* utf8);
    GHOST_API void GhostEngine_ApplyParticlePreset(int id, int type);
    GHOST_API void GhostEngine_RestartParticle(int id);
    GHOST_API int  GhostEngine_GetParticleLive(int id);

    // ---- Gamepad ----
    GHOST_API void GhostEngine_SetPadSettings(float deadzone, int rumble, int slot);   // slot -1 = auto
    GHOST_API int  GhostEngine_GetPadInfo(GhostPadInfo* out);                          // 1 = a pad is connected
    GHOST_API void GhostEngine_PadTestRumble(int on);

    // ---- Audio ----
    GHOST_API int  GhostEngine_GetSoundCount();
    GHOST_API int  GhostEngine_GetSoundInfo(int index, GhostSoundInfo* out);
    GHOST_API void GhostEngine_SetSoundInfo(int index, const GhostSoundInfo* in);   // enabled, bus, volume, pitchVar, file
    GHOST_API int  GhostEngine_SaveSoundBank();
    GHOST_API int  GhostEngine_TestSound(int index);
    GHOST_API void GhostEngine_StopAudio();
    GHOST_API void  GhostEngine_SetBusVolume(int bus, float v);
    GHOST_API float GhostEngine_GetBusVolume(int bus);
    GHOST_API void  GhostEngine_SetBusMute(int bus, int mute);
    GHOST_API int   GhostEngine_GetBusMute(int bus);
    GHOST_API int  GhostEngine_PreviewFile(const char* utf8Path);       // editor only, streamed on the Music bus
    GHOST_API int  GhostEngine_GetSceneAudio(int which, char* buf, int bufSize);   // 0 music, 1 ambience
    GHOST_API void GhostEngine_SetSceneAudio(int which, const char* utf8Name);

    // ---- Video ----
    GHOST_API void GhostEngine_GetVideoSettings(GhostVideoSettings* out);
    GHOST_API void GhostEngine_SetVideoSettings(const GhostVideoSettings* in);
    GHOST_API void GhostEngine_GetResolution(int* w, int* h);               // render size of the main view right now
    GHOST_API int  GhostEngine_GetResolutionList(int* outWH, int maxPairs); // presets + monitor modes, ascending. Returns pairs

    GHOST_API int  GhostEngine_RegisterFont(int textureId, int atlasW, int atlasH, float cellW, float cellH,
        float pad, float lineHeight, const float* advances);          // returns slot >= 1, 0 = failed
    GHOST_API void GhostEngine_SetFontProvider(GhostFontProviderFn fn);

    // ---- Animated models (3D skeletal animation) ----
    GHOST_API int  GhostEngine_IsMeshSkinned(int meshId);        // 1 if the model was imported with "Animated character"
    // which: 0 animation file (.ghostanim), 1 clip, 2 (get only) clip names joined with '|'
    GHOST_API int  GhostEngine_GetAnimatorText(int objectId, int which, char* buf, int bufSize);
    GHOST_API void GhostEngine_SetAnimatorText(int objectId, int which, const char* utf8);
    GHOST_API int  GhostEngine_GetAnimator(int objectId, int* playing, float* speed, float* time);
    GHOST_API void GhostEngine_SetAnimator(int objectId, int playing, float speed);

    // ---- Animation preview window + blueprint ----
    GHOST_API int  GhostEngine_CreatePreviewViewport(HWND hwnd, unsigned int width, unsigned int height);
    GHOST_API void GhostEngine_PreviewSetTarget(int vp, int objectId);
    GHOST_API void GhostEngine_PreviewOrbit(int vp, float dYaw, float dPitch, float zoomMul);
    GHOST_API void GhostEngine_PreviewResetCamera(int vp);
    GHOST_API void GhostEngine_AnimPreview_Set(int objectId, const char* storedFbxPath, int loop, float speed, int playing);
    GHOST_API void GhostEngine_AnimPreview_Stop();
    GHOST_API void GhostEngine_AnimPreview_Seek(float seconds);
    GHOST_API int  GhostEngine_AnimPreview_Get(float* time, float* duration, int* matched, int* total);
    GHOST_API int  GhostEngine_AnimRequestState(int objectId, const char* state);
    GHOST_API void GhostEngine_AnimResetRuntime(int objectId);

    GHOST_API void GhostEngine_GetPostSettings(GhostPostInfo* out);
    GHOST_API void GhostEngine_SetPostSettings(const GhostPostInfo* in);

    GHOST_API void GhostEngine_GetFogSettings(GhostFogInfo* out);
    GHOST_API void GhostEngine_SetFogSettings(const GhostFogInfo* in);

    GHOST_API void GhostEngine_SetLoadProgress(GhostLoadProgressFn fn);

    GHOST_API int  GhostEngine_ExpandLandscape(int objectId, int radius);   // adds a (2r+1)^2 block of Tile Maps around a floor map. Returns how many were created
    GHOST_API int  GhostEngine_StitchLandscape();

    GHOST_API void GhostEngine_GetWorldSettings(GhostWorldInfo* out);
    GHOST_API void GhostEngine_SetWorldSettings(const GhostWorldInfo* in);
    GHOST_API void GhostEngine_GetWorldStats(GhostWorldStatsInfo* out);

    // ---- Foliage ----
    GHOST_API int  GhostEngine_AddFoliageLayer(int kind, int source);          // kind 0 mesh (source = mesh id), 1 sprite (source = texture id). New id, 0 = failed
    GHOST_API int  GhostEngine_RemoveFoliageLayer(int id);
    GHOST_API int  GhostEngine_GetFoliageLayerIds(int* outIds, int maxCount);
    GHOST_API int  GhostEngine_GetFoliageLayer(int id, GhostFoliageLayerInfo* out);
    GHOST_API void GhostEngine_SetFoliageLayer(const GhostFoliageLayerInfo* in);
    GHOST_API int  GhostEngine_SetFoliageSource(int id, int meshId);
    GHOST_API int  GhostEngine_GetFoliageLayerName(int id, char* buf, int bufSize);
    GHOST_API void GhostEngine_SetFoliageLayerName(int id, const char* utf8);
    GHOST_API void GhostEngine_ClearFoliageLayer(int id);
    GHOST_API int  GhostEngine_GetFoliageFaceCount(int id);
    GHOST_API int  GhostEngine_GetFoliageFaceName(int id, int face, char* buf, int bufSize);
    GHOST_API int  GhostEngine_GetFoliageActiveLayer();
    GHOST_API void GhostEngine_SetFoliageActiveLayer(int id);
    GHOST_API void GhostEngine_GetFoliageBrush(GhostFoliageBrushInfo* out);
    GHOST_API void GhostEngine_SetFoliageBrush(const GhostFoliageBrushInfo* in);
    GHOST_API int  GhostEngine_GetFoliageMaterial(int id, int face, GhostMaterialInfo* out);
    GHOST_API void GhostEngine_SetFoliageMaterial(int id, int face, const GhostMaterialInfo* in);
    GHOST_API int  GhostEngine_GetFoliageMap(int id, int face, int slot, GhostMapInfo* out);
    GHOST_API void GhostEngine_SetFoliageMap(int id, int face, int slot, const GhostMapInfo* in);
    GHOST_API int  GhostEngine_GetFoliageSurface(int id, int face, GhostSurfaceInfo* out);
    GHOST_API void GhostEngine_SetFoliageSurface(int id, int face, const GhostSurfaceInfo* in);
    GHOST_API void GhostEngine_SetFoliagePaintMode(int on);       // defined in the engine glue (Phase 2)
    GHOST_API int  GhostEngine_GetFoliagePaintMode();
    GHOST_API void GhostEngine_GetFoliageStats(GhostFoliageStatsInfo* out);

}