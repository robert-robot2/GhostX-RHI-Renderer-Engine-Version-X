
using System.Runtime.InteropServices;
using System.Text;
namespace GhostShell
{

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostObjectInfo
    {
        public int id, meshId;
        public float px, py, pz;
        public float rx, ry, rz;
        public float sx, sy, sz;
        public float r, g, b;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostLightInfo
    {
        public int id, type;
        public float px, py, pz;
        public float yawDeg, pitchDeg;
        public float r, g, b;
        public float intensity, range;
        public float width, height;
        public float innerDeg, outerDeg;
        public int shadowObjects, shadowLandscape;
        public float spinYaw, spinPitch;
        public float volumetric;
        public int celestial;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostFogInfo
    {
        public int enabled;
        public float density, colorR, colorG, colorB, brightness, followAmbient;
        public float heightBase, heightFalloff, startDistance, maxDistance, skyFog;
        public int envMode;
        public float envAmount;
        public int envTexture;
        public float envYaw;
        public int noiseTexture;
        public float noiseAmount, noiseScale, scrollX, scrollZ;
        public int volLight;
        public float volIntensity, anisotropy;
        public int steps;
    }


    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostFlickerInfo
    {
        public int enabled, pattern, smoothSteps, textureId;
        public float speed, minLevel, baseInfluence, brightness;
        public float r, g, b;
        public float texSpeed, texTiling, texAmount;
        public float diffuse, specular, roughness, metallic;
        public float seed;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostEntityInfo { public int id, kind, subtype; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostMaterialInfo
    {
        public int textureId, mapMode, wrap, filter;
        public float tilingU, tilingV, offsetU, offsetV;
        public float rotationDeg;
        public float r, g, b;
        public float metallic, roughness, specular;
        public float emR, emG, emB, emissiveStrength;
        public int alphaMode, alphaSource;
        public float alphaCutoff;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostCameraInfo
    {
        public int id;
        public float px, py, pz;
        public float yawDeg, pitchDeg;
        public float fovDeg;
        public int isActive;
        public int resW, resH;
        public int mode, targetId;
        public float distance, heightOffset, minDist, maxDist;
        public float minPitchDeg, maxPitchDeg, smoothing;
        public float moveSpeed;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostGameplayInfo
    {
        public int id, role, collider;
        public float csx, csy, csz;
        public float cox, coy, coz;
        public int visible;
        public int locked, pathMode, breakEffect, contents, portalTrigger;
        public float moveX, moveY, moveZ, moveSpeed, movePause;
        public int powerType, itemMoves, coinCount;
        public int enemyPathMode;
        public int flying;
        public int enemyDeathEffect;
        public float crx, cry, crz;
        public float timeOffset;     // NEW: last
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostModParamInfo
    {
        public int kind;
        public float mn, mx;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)] public float[] def;
        public int count;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 48)] public byte[] name;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] category;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 192)] public byte[] enums;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostSpriteInfo
    {
        public int id;
        public int textureId, source, cols, rows, frame;
        public int flipX, billboard, unlit, alphaClip, pixelArt, playing;
        public float tintR, tintG, tintB;
        public float animSpeed;
        public int effTexture, effCols, effRows, curFrame;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostMeshStats
    {
        public int builtin, faceCount;
        public uint vertexCount, triangleCount;
        public float minX, minY, minZ, maxX, maxY, maxZ;
        public float importScale, importFit;
        public int importUp, importFlags;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostViewSettings
    {
        public int showSelection, showFps, showGrid, showWorldAxes;
        public int showDims, dimAxes, edgeLabels;
        public int gridPlane, gridStyle;
        public float gridSize;
        public int gridExtent;
        public int tool, space, unit, decimals;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostGizmoSettings
    {
        public int snap;
        public float moveStep, rotStep, scaleStep;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostPadInfo
    {
        public int connected, slot, buttons;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 6)] public float[] axes;
        public int raw;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostVideoSettings
    {
        public int resW, resH;        // 0, 0 = Match Window
        public int displayMode;       // 0 windowed, 1 borderless window, 2 fullscreen
        public int scaleFilter;       // 0 linear, 1 point
        public int fit;               // 0 letterbox, 1 integer, 2 stretch
    }
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int GhostFontProviderFn([MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostShadowTuning
    {
        public int filter;
        public float softness, bias, normalBias, distance;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostMapInfo { public int textureId; public float amount; public int channel; public int invert; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostSurfaceInfo
    {
        public int twoSided;
        public float opacity, refractionIOR, reflectionRotDeg, anisotropy;
        public float sssR, sssG, sssB, sssAmount;
        public float mapTilingU, mapTilingV, mapOffsetU, mapOffsetV;
        public float wpoSway, wpoSpeed, wpoMid;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostTileMapInfo
    {
        public int id;
        public float tileSize;
        public int gridX, gridY, gridZ;
        public int direction;
        public int receiveShadows;
        public int shadowLightId;
        public int layerCount;
        public int subdiv;
        public int autoCollider;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostPaintInfo
    {
        public int active, tool;
        public float radius, strength, fade, setHeight;
        public int layer, layerCount, hasTileMap;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostPlayerParams
    {
        public float walk, run, runAnimSpeed, groundAccel, airAccel, gravity, maxFall, fallLimit;
        public int sideScroller;
        public float jump, runJump, jumpCut, coyoteTime, jumpBuffer;
        public float stompBounce, stompBounceHeld, stompTolerance, invulnerable, hurtTime, hurtHop;
        public int smallBreaksBricks;
        public float bigScaleX, bigScaleY, fireScaleX, fireScaleY, modelBigScale, modelFireScale,
                     growPop, transformTime, shrinkTime, starTime;
        public float skidMinSpeed, skidExitSpeed, skidFriction, skidOpposeCos;
        public float attackCooldown, fireballSpeed, fireballLife, fireballBounce;
        public float deathTime, deathHop, deathPop;
        public float modelYawOffset, modelTurnSpeed, modelStrideSpeed, modelAttackTime, modelAttackHitAt, modelAttackReach;
        public int modelMeleeDamage;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostEnemyParams
    {
        public float gravity, maxFall, fallLimit, patrolDefault, ledgeLead, ledgeDepth;
        public float attackRange, attackHeight, attackTime, attackCooldown;
        public float modelDieTime, spriteDieTime, flattenTime, flattenScaleY, flattenDrop, deathBounce, shellGrace;
        public float modelYawOffset, modelTurnSpeed;
        public int followPathway, pathGroup, pathLoop;
        public float pathSprintMul, pathJumpHeight, pathSprintJumpHeight;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostBossParams
    {
        public int autoJumpY;
        public float hopInterval, gravity, maxFall;
        public int followPathway, pathGroup, pathLoop;
        public float pathSprintMul, pathJumpHeight, pathSprintJumpHeight;
        public float modelYawOffset, modelTurnSpeed;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostPathParams
    {
        public int pathGroup, action;
        public float holdTime, jumpHeight;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostVolumeInfo
    {
        public int id, type, shape, enabled, priority;
        public float px, py, pz;
        public float rx, ry, rz;
        public float sx, sy, sz;
        public float radius, blend;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)] public float[] p;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)] public int[] tex;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostPostInfo
    {
        public int toneMode;
        public float exposureEV, whitePoint;
        public int bloomEnabled;
        public float bloomThreshold, bloomKnee, bloomIntensity, bloomScatter;
        public int bloomMips;
        public float bloomTintR, bloomTintG, bloomTintB;
        public int ssaoEnabled;
        public float ssaoRadius, ssaoIntensity, ssaoBias, ssaoStrength;
        public int ssaoSamples;
        public float ssaoDirectFade;
        public int ssaoHalfRes, ssaoShowOnly;
        public float ssaoFadeDist;
        public int ssrEnabled;
        public float ssrIntensity, ssrMaxDistance, ssrThickness;
        public int ssrSteps;
        public float ssrMaxRoughness, ssrEdgeFade;
        public int ssgiEnabled;
        public float ssgiIntensity, ssgiRadius;
        public int ssgiSamples;
        public float ssgiTemporal, ssgiFadeDist;
        public int ssgiShowOnly;
        public int shaftEnabled;
        public float shaftIntensity, shaftThreshold, shaftMaxBright, shaftLength, shaftDecay, shaftDarkness, shaftDepthRange;
        public int shaftSteps, shaftOcclusion;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostParticleParams
    {
        public int id, type;
        public float px, py, pz;
        public int enabled;
        public float rate;
        public int maxParticles;
        public float lifeMin, lifeMax;
        public int prewarm, spawnMode;
        public float boxX, boxY, boxZ;
        public int killOutside;
        public float velMinX, velMinY, velMinZ;
        public float velMaxX, velMaxY, velMaxZ;
        public float accelX, accelY, accelZ;
        public float drag, swayAmp, swayFreq;
        public float sizeMin, sizeMax, sizeEnd;
        public float r0, g0, b0, r1, g1, b1;
        public float alpha0, alpha1;
        public float rotMin, rotMax, spinMin, spinMax;
        public int billboard, unlit, additive, alphaClip, pixelArt;
        public int textureId, source, cols, rows;
        public float animFps, animSpeed;
        public int animOverLife, randomStartFrame, meshId;
        public float drawDistance;
        public float rotX, rotY, rotZ;
        public float scaleX, scaleY, scaleZ;
    
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostWorldInfo
    {
        public int streaming, lod, cull, showCells, maxLoads;
        public float loadDist, unloadPad, msBudget, lodDist1, lodDist2, farPlane, nearPlane;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostWorldStatsInfo
    {
        public int cells, resident, slotsTotal, starved, loadsLast, cellsDrawn, bandsDrawn, bandsCulled, trisDrawn, objectsCulled;
        public float lastLoadMs;
    }
    internal static class GhostEngine
    {
        private const string DllName = "GhostX.dll";

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_Init(IntPtr hwnd, uint width, uint height);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_Tick();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_Resize(uint width, uint height);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_Shutdown();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetClearColor(float r, float g, float b);

        // ---- Input ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetInputEnabled(int enabled);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_MouseDown(int x, int y, int button);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_MouseMove(int x, int y);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_MouseUp(int x, int y, int button);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_MouseLeave();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_PurgeProjectAssets();

        // ---- Scene ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddObject(int meshId, int screenX, int screenY);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddLight(int lightType, int screenX, int screenY);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddTileMap(int screenX, int screenY);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetTileMap(int objectId, ref GhostTileMapInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetTileMap(ref GhostTileMapInfo info);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddVolume(int type, int screenX, int screenY);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetVolume(int id, out GhostVolumeInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetVolume(ref GhostVolumeInfo info);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_CaptureProbe(int volumeId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetProbeStatus(int volumeId);


        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddTileLayer(int objectId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_RemoveTileLayer(int objectId, int layer);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AssignTileLayer(int objectId, int layer, int col0, int row0, int col1, int row1);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetTileLayerCounts(int objectId, [Out] int[] outCounts, int maxCount);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetPaintMode(int on);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetPaintMode();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_GetPaintSettings(out GhostPaintInfo p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetPaintSettings(ref GhostPaintInfo p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_ResetTerrain(int objectId, int what);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_RemoveEntity(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SelectEntity(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetSelected();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint GhostEngine_GetSceneVersion();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetEntities([Out] GhostEntityInfo[] buffer, int maxCount);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int GhostEngine_GetEntityName(int id, StringBuilder buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetObject(int id, ref GhostObjectInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetObject(ref GhostObjectInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetLight(int id, ref GhostLightInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetLight(ref GhostLightInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetAmbient(out float r, out float g, out float b, out float intensity);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetAmbient(float r, float g, float b, float intensity);
        // ---- Textures ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int GhostEngine_CreateTexture(string name, byte[] rgba, uint width, uint height);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_SetObjectTexture(int objectId, int textureId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetTextureCount();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int GhostEngine_GetTextureInfo(int id, StringBuilder name, int nameSize, out uint width, out uint height);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetObjectTexture(int objectId);

        // ---- Faces / materials ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetFaceCount(int objectId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int GhostEngine_GetFaceName(int objectId, int face, StringBuilder buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetMaterial(int objectId, int face, ref GhostMaterialInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetMaterial(int objectId, int face, ref GhostMaterialInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetTargetFace();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetTargetFace(int face);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int GhostEngine_ReplaceTexture(int id, string name, byte[] rgba, uint width, uint height);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_PickObjectAt(int x, int y, out int face);

        // ---- Cameras ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddCamera(int mode, int screenX, int screenY);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetCameraTargetTag(int id, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetCameraTargetTag(int id, [MarshalAs(UnmanagedType.LPUTF8Str)] string tag);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetGameStartCamera();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetGameStartCamera(int cameraId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_MouseWheel(int delta);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetCamera(int id, ref GhostCameraInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetCamera(ref GhostCameraInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetActiveCamera(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetActiveCamera();

        // ---- Viewports ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_CreateViewport(IntPtr hwnd, uint width, uint height, int cameraId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_DestroyViewport(int vp);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_RebindViewport(int vp, IntPtr hwnd, uint width, uint height);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_ResizeViewport(int vp, uint width, uint height);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetViewportVisible(int vp, int visible);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetInputViewport(int vp);


        // ---- Scene files ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetProjectRoot([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_SaveScene([MarshalAs(UnmanagedType.LPUTF8Str)] string path, [Out] byte[] msg, int msgSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_LoadScene([MarshalAs(UnmanagedType.LPUTF8Str)] string path, [Out] byte[] msg, int msgSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_ClearScene();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern uint GhostEngine_GetSceneHash();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetTexturePath(int id, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetTexturePath(int id, [Out] byte[] buf, int bufSize);
        // ---- Play mode ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_Play();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_Stop();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetPaused(int paused);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetPlayState();
        // ---- Gameplay data ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetGameplay(int objectId, ref GhostGameplayInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetGameplay(ref GhostGameplayInfo info);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetPlayerParams(int objectId, ref GhostPlayerParams p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetPlayerParams(int objectId, ref GhostPlayerParams p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetEnemyParams(int objectId, ref GhostEnemyParams p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetEnemyParams(int objectId, ref GhostEnemyParams p);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetBossParams(int objectId, ref GhostBossParams p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetBossParams(int objectId, ref GhostBossParams p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetPathParams(int objectId, ref GhostPathParams p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetPathParams(int objectId, ref GhostPathParams p);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetCustomTag(int objectId, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetCustomTag(int objectId, [MarshalAs(UnmanagedType.LPUTF8Str)] string tag);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetGameModulePath([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetPlayMessage([Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetHudText([Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_TakePlayFault([Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_DuplicateEntity(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_DuplicateArray(int id, [In] float[] offsets, int count, [Out] int[] outIds);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_Undo();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_Redo();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_UndoCount();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_RedoCount();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_CopySelected();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_Paste();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_FocusSelected();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetEntityName(int id, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFaceMode(int on);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFaceMode();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_SetFont(int textureId, int atlasW, int atlasH, float cellW, float cellH,
                                             float pad, float lineHeight, [In] float[] advances);
        // ---- Mods ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetModTypeCount();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int GhostEngine_GetModTypeName(int typeId, StringBuilder buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetModParamCount(int typeId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetModParamInfo(int typeId, int index, out GhostModParamInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddMod(int typeId, int screenX, int screenY);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetMod(int id, [Out] float[] pos3);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetModPosition(int id, [In] float[] pos3);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetModParam(int id, int index, [Out] float[] out3);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetModParam(int id, int index, [In] float[] in3);

        // ---- Sprites ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetSprite(int objectId, ref GhostSpriteInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetSprite(ref GhostSpriteInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetSpriteText(int objectId, int which, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetSpriteText(int objectId, int which, [MarshalAs(UnmanagedType.LPUTF8Str)] string text);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_ReloadAnimations();

        // ---- Shadows ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetObjectShadow(int objectId, out int mode, out int lightId, out int receive);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetObjectShadow(int objectId, int mode, int lightId, int receive);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetLightShadows(int lightId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetLightShadows(int lightId, int on);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetShadowSettings(out int enabled, out float strength);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetShadowSettings(int enabled, float strength);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetShadowResolution();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetShadowResolution(int size);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetShadowTuning(out GhostShadowTuning t);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetShadowTuning(ref GhostShadowTuning t);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetMap(int objectId, int face, int slot, ref GhostMapInfo m);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetMap(int objectId, int face, int slot, ref GhostMapInfo m);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetSurface(int objectId, int face, ref GhostSurfaceInfo s);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetSurface(int objectId, int face, ref GhostSurfaceInfo s);


        // ---- Models ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_ImportModel([MarshalAs(UnmanagedType.LPUTF8Str)] string path,
            float scale, float fitSize, int upAxis, int flipWinding, [Out] byte[] msg, int msgSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_LoadModelFile([MarshalAs(UnmanagedType.LPUTF8Str)] string path, [Out] byte[] msg, int msgSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetMeshCount();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetMeshInfo(int meshId, out GhostMeshStats info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetMeshText(int meshId, int which, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetObjectMissingMesh(int objectId, [Out] byte[] buf, int bufSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_ReimportModel(int meshId, int useStored, float scale, float fitSize, int upAxis,
    int flags, [Out] byte[] msg, int msgSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_ApplyMeshDefaults(int objectId, int what);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_TakeQuitRequest();

        // ---- Viewport overlay ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetViewSettings(out GhostViewSettings s);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetViewSettings(ref GhostViewSettings s);


        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetGizmoSettings(out GhostGizmoSettings s);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetGizmoSettings(ref GhostGizmoSettings s);

        // ---- Dimension editing ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_TakeDimClick(out int kind, out int index, out int objectId, out float worldLen);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetObjectSize(int objectId, [Out] float[] xyz);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetObjectSize(int objectId, int axis, float worldSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_ScaleObject(int objectId, float factor);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetLinkId(int objectId, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetLinkId(int objectId, [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetSelectedCount();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetSelectedIds([Out] int[] outIds, int maxCount);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetSelection([In] int[] ids, int count);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetColliderMode(int on);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetColliderMode();
        // ---- Antialiasing ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetMSAA(int samples);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetMSAA();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetMSAAMax();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetTAA(int on);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetTAA();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetTAAJitterSpeed(float speed);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern float GhostEngine_GetTAAJitterSpeed();

        // ---- Gamepad ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetPadSettings(float deadzone, int rumble, int slot);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetPadInfo(out GhostPadInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_PadTestRumble(int on);

        // ---- Audio ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetSoundCount();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetSoundInfo(int index, out GhostSoundInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetSoundInfo(int index, ref GhostSoundInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_SaveSoundBank();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_TestSound(int index);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_StopAudio();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetBusVolume(int bus, float v);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern float GhostEngine_GetBusVolume(int bus);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetBusMute(int bus, int mute);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetBusMute(int bus);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_PreviewFile([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetSceneAudio(int which, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetSceneAudio(int which, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        // ---- Video ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetVideoSettings(out GhostVideoSettings s);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetVideoSettings(ref GhostVideoSettings s);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetResolution(out int w, out int h);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetResolutionList([Out] int[] outWH, int maxPairs);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_RegisterFont(int textureId, int atlasW, int atlasH, float cellW, float cellH,
                                                  float pad, float lineHeight, [In] float[] advances);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetFontProvider(GhostFontProviderFn fn);

        // ---- Particle systems ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_AddParticleSystem(int type, int screenX, int screenY);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_AddParticleAt(int type, float x, float y, float z);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_RemoveParticle(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetParticleCount();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetParticleIds([Out] int[] outIds, int maxCount);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetParticle(int id, ref GhostParticleParams p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetParticle(ref GhostParticleParams p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetParticleText(int id, int which, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetParticleText(int id, int which, [MarshalAs(UnmanagedType.LPUTF8Str)] string text);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_ApplyParticlePreset(int id, int type);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_RestartParticle(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetParticleLive(int id);

        // ---- Animated models ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_IsMeshSkinned(int meshId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetAnimatorText(int objectId, int which, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetAnimatorText(int objectId, int which, [MarshalAs(UnmanagedType.LPUTF8Str)] string text);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetAnimator(int objectId, out int playing, out float speed, out float time);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetAnimator(int objectId, int playing, float speed);


        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] 
        public static extern int GhostEngine_CreatePreviewViewport(IntPtr hwnd, uint w, uint h);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_PreviewSetTarget(int vp, int objectId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_PreviewOrbit(int vp, float dYaw, float dPitch, float zoomMul);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_PreviewResetCamera(int vp);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern void GhostEngine_AnimPreview_Set(int objectId, [MarshalAs(UnmanagedType.LPUTF8Str)] string fbx, int loop, float speed, int playing);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_AnimPreview_Stop();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_AnimPreview_Seek(float seconds);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_AnimPreview_Get(out float time, out float duration, out int matched, out int total);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AnimRequestState(int objectId, [MarshalAs(UnmanagedType.LPUTF8Str)] string state);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_AnimResetRuntime(int objectId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetLoadProgress(GhostLoadProgressFn? fn);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetPostSettings(out GhostPostInfo p);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetPostSettings(ref GhostPostInfo p);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetFlicker(int lightId, int layer, ref GhostFlickerInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetFlicker(int lightId, int layer, ref GhostFlickerInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_GetFlickerText(int lightId, int layer, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetFlickerText(int lightId, int layer, [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_GetFogSettings(out GhostFogInfo f);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void GhostEngine_SetFogSettings(ref GhostFogInfo f);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_ExpandLandscape(int objectId, int radius);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_StitchLandscape();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_GetWorldSettings(out GhostWorldInfo w);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetWorldSettings(ref GhostWorldInfo w);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_GetWorldStats(out GhostWorldStatsInfo s);


    }
}