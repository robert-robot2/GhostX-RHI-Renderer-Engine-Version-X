
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

        // ---- Scene ----
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddObject(int meshId, int screenX, int screenY);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GhostEngine_AddLight(int lightType, int screenX, int screenY);
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



    }
}