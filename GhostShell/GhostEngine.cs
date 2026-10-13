
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
        public static extern int GhostEngine_AddCamera(int screenX, int screenY);
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

    }
}