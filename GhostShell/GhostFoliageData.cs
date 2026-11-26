// GhostFoliageData.cs - foliage interop (structs + P/Invoke) and small helpers for the editor UI.
// Add to the GhostShell project. The struct field order must match GhostEngine.h exactly.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace GhostShell
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostFoliageLayerInfo
    {
        public int id, kind, meshId, enabled;
        public int billboard, alphaClip, unlit, pixelArt;
        public int cols, rows, animated;
        public float animFps;
        public float sizeMin, sizeMax, yawRange, tiltRandom, alignToNormal, slopeMax;
        public float scatterRadius;
        public int amount;
        public float minSpacing, heightOffset;
        public int restOnGround;
        public float cullDistance, fadeLength, colorVariation, normalUp;
        public int castShadows, shadowLightId, receiveShadows;
        public float shadowDistance;
        public int instanceCount, chunkCount;        // read-only
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostFoliageBrushInfo
    {
        public int active, erase, onLandscape, onObjects, eraseAll, activeLayerId;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostFoliageStatsInfo
    {
        public int layers, chunks, instances, chunksDrawn, instancesDrawn, shadowChunks;
    }

    internal static class GhostFoliageNative
    {
        private const string DllName = "GhostX.dll";

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_AddFoliageLayer(int kind, int source);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_RemoveFoliageLayer(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageLayerIds([Out] int[] outIds, int maxCount);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageLayer(int id, out GhostFoliageLayerInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFoliageLayer(ref GhostFoliageLayerInfo info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_SetFoliageSource(int id, int meshId);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageLayerName(int id, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFoliageLayerName(int id, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_ClearFoliageLayer(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageFaceCount(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageFaceName(int id, int face, [Out] byte[] buf, int bufSize);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageActiveLayer();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFoliageActiveLayer(int id);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_GetFoliageBrush(out GhostFoliageBrushInfo b);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFoliageBrush(ref GhostFoliageBrushInfo b);

        // materials of a foliage layer (same structs as the object versions)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageMaterial(int id, int face, ref GhostMaterialInfo m);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFoliageMaterial(int id, int face, ref GhostMaterialInfo m);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageMap(int id, int face, int slot, ref GhostMapInfo m);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFoliageMap(int id, int face, int slot, ref GhostMapInfo m);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliageSurface(int id, int face, ref GhostSurfaceInfo s);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFoliageSurface(int id, int face, ref GhostSurfaceInfo s);

        // Phase 2 (engine glue)
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_SetFoliagePaintMode(int on);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern int GhostEngine_GetFoliagePaintMode();
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)] public static extern void GhostEngine_GetFoliageStats(out GhostFoliageStatsInfo s);
    }

    // Managed helpers. Phase 3 (the panel) is built on these.
    internal static class GhostFoliageModel
    {
        public const int KindMesh = 0, KindSprite = 1;
        public static readonly string[] CastModes = { "Off", "All Lights", "One Light" };

        public delegate void LayerEdit(ref GhostFoliageLayerInfo i);

        public static List<int> LayerIds()
        {
            var buf = new int[32];
            int n = GhostFoliageNative.GhostEngine_GetFoliageLayerIds(buf, buf.Length);
            n = Math.Min(n, buf.Length);
            return new List<int>(new ArraySegment<int>(buf, 0, n));
        }

        public static bool TryGet(int id, out GhostFoliageLayerInfo info) => GhostFoliageNative.GhostEngine_GetFoliageLayer(id, out info) != 0;

        public static void Edit(int id, LayerEdit edit)
        {
            if (!TryGet(id, out var i)) return;
            edit(ref i);
            GhostFoliageNative.GhostEngine_SetFoliageLayer(ref i);
        }

        public static string Name(int id)
        {
            var b = new byte[128];
            GhostFoliageNative.GhostEngine_GetFoliageLayerName(id, b, b.Length);
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        public static void Rename(int id, string name) => GhostFoliageNative.GhostEngine_SetFoliageLayerName(id, name);

        public static int AddMeshLayer(int meshId) => GhostFoliageNative.GhostEngine_AddFoliageLayer(KindMesh, meshId);
        public static int AddSpriteLayer(int textureId) => GhostFoliageNative.GhostEngine_AddFoliageLayer(KindSprite, textureId);
        public static bool Remove(int id) => GhostFoliageNative.GhostEngine_RemoveFoliageLayer(id) != 0;

        public static int ActiveLayer
        {
            get => GhostFoliageNative.GhostEngine_GetFoliageActiveLayer();
            set => GhostFoliageNative.GhostEngine_SetFoliageActiveLayer(value);
        }

        public static bool PaintMode
        {
            get => GhostFoliageNative.GhostEngine_GetFoliagePaintMode() != 0;
            set => GhostFoliageNative.GhostEngine_SetFoliagePaintMode(value ? 1 : 0);
        }

        public static GhostFoliageBrushInfo Brush
        {
            get { GhostFoliageNative.GhostEngine_GetFoliageBrush(out var b); return b; }
            set => GhostFoliageNative.GhostEngine_SetFoliageBrush(ref value);
        }

        // [ and ] while foliage paint mode is on: scales the active layer's scatter radius. false = foliage paint is off.
        public static bool AdjustRadius(float mul)
        {
            if (!PaintMode) return false;
            if (TryGet(ActiveLayer, out var li))
            {
                li.scatterRadius = Math.Clamp(li.scatterRadius * mul, 0.25f, 100f);
                GhostFoliageNative.GhostEngine_SetFoliageLayer(ref li);
            }
            return true;
        }

        // Test hook used by the palette until the panel exists (Phase 3): uses the selected object's mesh / sprite texture.
        public static int AddFromSelection(int type)
        {
            int sel = GhostEngine.GhostEngine_GetSelected();
            var o = new GhostObjectInfo();
            bool isObj = sel != 0 && GhostEngine.GhostEngine_GetObject(sel, ref o) != 0;
            int id;
            if (type == KindMesh)
            {
                if (!isObj || o.meshId == 4) return 0;                       // needs a selected mesh object (not a sprite)
                id = AddMeshLayer(o.meshId);
            }
            else
            {
                int tex = 1;                                                 // checkerboard when nothing better is selected
                var sp = new GhostSpriteInfo();
                if (isObj && o.meshId == 4 && GhostEngine.GhostEngine_GetSprite(sel, ref sp) != 0) tex = sp.textureId;
                id = AddSpriteLayer(tex);
            }
            if (id != 0) PaintMode = true;
            return id;
        }
    }
}