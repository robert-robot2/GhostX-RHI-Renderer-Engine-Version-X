// GhostEditorUI.cs — engine editor panels: palette (left), hierarchy + details (right), assets (bottom)
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;
using System.Windows.Forms.Design.Behavior;
using WeifenLuo.WinFormsUI.Docking;
namespace GhostShell
{
    // Render surface that can hold keyboard focus (a plain Panel can't)
    internal class ViewportPanel : Panel
    {
        public ViewportPanel()
        {
            SetStyle(ControlStyles.Selectable, true);
            TabStop = true;
        }
    }

    // ============================================================
    //  Property proxies — read/write the engine live
    // ============================================================
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal class Vec3Edit
    {
        private readonly Func<(float x, float y, float z)> _get;
        private readonly Action<float, float, float> _set;

        public Vec3Edit(Func<(float x, float y, float z)> get, Action<float, float, float> set)
        { _get = get; _set = set; }

        public float X { get => _get().x; set { var v = _get(); _set(value, v.y, v.z); } }
        public float Y { get => _get().y; set { var v = _get(); _set(v.x, value, v.z); } }
        public float Z { get => _get().z; set { var v = _get(); _set(v.x, v.y, value); } }

        public override string ToString() { var v = _get(); return $"{v.x:0.##}, {v.y:0.##}, {v.z:0.##}"; }
    }

    internal static class GhostColorUtil
    {
        private static int C(float v) => (int)Math.Round(Math.Clamp(v, 0f, 1f) * 255f);
        public static Color ToColor(float r, float g, float b) => Color.FromArgb(C(r), C(g), C(b));

    }

    internal static class GhostNames
    {
        public static string Of(int id)
        {
            var sb = new StringBuilder(128);
            GhostEngine.GhostEngine_GetEntityName(id, sb, sb.Capacity);
            return sb.ToString();
        }
    }

    // ============================================================
    //  Texture list helpers (dropdown contents + disk import)
    // ============================================================
    internal static class GhostTextures
    {
        public const string ImportLabel = "Import Texture...";

        public static string DisplayName(int id)
        {
            if (id <= 0) return "None";
            var sb = new StringBuilder(260);
            if (GhostEngine.GhostEngine_GetTextureInfo(id, sb, sb.Capacity, out _, out _) == 0) return "None";
            return id <= 2 ? sb.ToString() : $"{sb} [{id}]";      // built-ins have fixed names, imports get their id
        }

        public static string SizeText(int id)
        {
            if (id <= 0) return "-";
            var sb = new StringBuilder(260);
            if (GhostEngine.GhostEngine_GetTextureInfo(id, sb, sb.Capacity, out uint w, out uint h) == 0) return "-";
            var a = GhostAssets.Get(id);
            bool scaled = a != null && a.Path != null && (a.OrigW != w || a.OrigH != h);
            return scaled ? $"{w} x {h}  (source {a!.OrigW} x {a.OrigH})" : $"{w} x {h}";
        }

        public static List<string> Choices()
        {
            var list = new List<string>();
            int n = GhostEngine.GhostEngine_GetTextureCount();

            for (int i = 0; i < n; i++)
            {
                if (i == GhostFontAtlas.TextureId || GhostFontAtlas.ExtraTextures.Contains(i)) continue;
                string dn = DisplayName(i);
                if (dn.StartsWith("[Splat") || dn.StartsWith("[Free")) continue;   // weight maps, purged slots
                if (i >= 3 && IsStale(i)) continue;                                 // file gone, or outside this project
                list.Add(dn);
            }

            list.Add(ImportLabel);
            return list;
        }

        private static bool IsStale(int id)
        {
            var buf = new byte[1024];
            if (GhostEngine.GhostEngine_GetTexturePath(id, buf, buf.Length) == 0) return false;   // no source file (font, etc.)
            int n = Array.IndexOf(buf, (byte)0);
            string path = Encoding.UTF8.GetString(buf, 0, n < 0 ? buf.Length : n);
            if (!File.Exists(path)) return true;
            string? root = GhostProject.Root;
            if (root == null) return false;
            string prefix = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) + Path.DirectorySeparatorChar;
            return !Path.GetFullPath(path).StartsWith(prefix, StringComparison.OrdinalIgnoreCase);
        }

        public static int IdFromDisplay(string text)
        {
            int n = GhostEngine.GhostEngine_GetTextureCount();
            for (int i = 0; i < n; i++)
                if (DisplayName(i) == text) return i;
            return -1;
        }

        // Opens the file dialog, decodes, uploads. Returns the new texture id, or -1 if cancelled or failed.
        // Opens the file dialog, decodes, uploads. Returns the new texture id, or -1 if cancelled or failed.
        public static int ImportFromDisk()
        {
            using var dlg = new OpenFileDialog
            {
                Title = "Import Texture",
                Filter = "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp|All files (*.*)|*.*",
            };
            if (dlg.ShowDialog() != DialogResult.OK) return -1;

            try
            {
                int tex = GhostAssets.ImportOrGet(GhostProject.EnsureInContent(dlg.FileName, null));
                if (tex < 0)
                    MessageBox.Show("The engine could not create the texture (too large, or the texture limit was reached).",
                        "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return tex;
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return -1;
            }
        }

        // face -1 = every face. Used by the browser's double-click and by drag-to-object.
        public static void Apply(int objectId, int face, int tex)
        {
            if (face < 0) { GhostEngine.GhostEngine_SetObjectTexture(objectId, tex); return; }

            var m = new GhostMaterialInfo();
            if (GhostEngine.GhostEngine_GetMaterial(objectId, face, ref m) == 0) return;
            if (m.textureId == 0 && tex != 0) m.r = m.g = m.b = 1.0f;      // white tint so the image shows at full strength
            else if (tex == 0) m.r = m.g = m.b = 0.6f;      // back to the default grey
            m.textureId = tex;
            GhostEngine.GhostEngine_SetMaterial(objectId, face, ref m);
        }
    }

    // Turns a string property into a dropdown fed from the engine's texture list
    internal class TextureChoiceConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
            => new StandardValuesCollection(GhostTextures.Choices());
    }

    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal class Vec2Edit
    {
        private readonly Func<(float u, float v)> _get;
        private readonly Action<float, float> _set;

        public Vec2Edit(Func<(float u, float v)> get, Action<float, float> set) { _get = get; _set = set; }

        public float U { get => _get().u; set { var t = _get(); _set(value, t.v); } }
        public float V { get => _get().v; set { var t = _get(); _set(t.u, value); } }

        public override string ToString() { var t = _get(); return $"{t.u:0.##}, {t.v:0.##}"; }
    }

    internal abstract class FixedListConverter : StringConverter
    {
        protected abstract string[] Items { get; }
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
            => new StandardValuesCollection(Items);
    }

    internal class ToneMapConverter : FixedListConverter
    {
        public static readonly string[] Names = { "None (clamp)", "Reinhard", "ACES (filmic)", "Neutral (keeps colors)" };
        protected override string[] Items => Names;
    }

    internal class FogEnvConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Fog Color", "Reflection Capture (cubemap)", "Texture (lat-long image)" };
        protected override string[] Items => Names;
    }
    internal class MapModeConverter : FixedListConverter
    {
        public static readonly string[] Names =
            { "Mesh UV", "Box", "Planar XY", "Planar XZ", "Planar YZ", "Cylindrical", "Spherical", "Cube Cross" };
        protected override string[] Items => Names;
    }

    internal class WrapConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Repeat", "Clamp" };
        protected override string[] Items => Names;
    }
    internal class ShadowResConverter : FixedListConverter
    {
        public static readonly string[] Names =
            { "512 (low)", "1024 (default)", "2048 (high)", "4096 (very high, ~1 GB video memory)" };
        public static readonly int[] Sizes = { 512, 1024, 2048, 4096 };
        protected override string[] Items => Names;
    }

    internal class ShadowFilterConverter : FixedListConverter
    {
        public static readonly string[] Names =
        {
            "Sharp (1 bilinear tap)",
            "PCF 3x3 (classic)",
            "PCF Smooth (bilinear 3x3)",
            "Soft (16 rotated taps)"
        };
        protected override string[] Items => Names;
    }

    internal class AlphaModeConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Opaque", "Masked (alpha test)", "Translucent (blended)" };
        protected override string[] Items => Names;
    }
    internal class MapChannelConverter : FixedListConverter
    {
        public static readonly string[] Names = { "R", "G", "B", "A", "Luminance" };
        protected override string[] Items => Names;
    }

    // One texture map of the selected material slot: texture + amount + channel + invert
    [TypeConverter(typeof(ExpandableObjectConverter))]
    [System.ComponentModel.Editor(typeof(TextureThumbEditor), typeof(System.Drawing.Design.UITypeEditor))]
    internal sealed class MapEdit
    {
        private readonly int _id, _slot;
        public MapEdit(int id, int slot) { _id = id; _slot = slot; }

        [Browsable(false)] public int TextureId => Read().textureId;

        private delegate void MapChange(ref GhostMapInfo m);

        private GhostMapInfo Read()
        {
            var m = new GhostMapInfo();
            GhostEngine.GhostEngine_GetMap(_id, Math.Max(GhostEngine.GhostEngine_GetTargetFace(), 0), _slot, ref m);
            return m;
        }

        private void Change(MapChange c)               // edits the Target Face, or every slot when it is "All Faces"
        {
            int face = GhostEngine.GhostEngine_GetTargetFace();
            int count = GhostEngine.GhostEngine_GetFaceCount(_id);
            int from = face < 0 ? 0 : face, to = face < 0 ? count - 1 : face;
            for (int f = from; f <= to; f++)
            {
                var m = new GhostMapInfo();
                if (GhostEngine.GhostEngine_GetMap(_id, f, _slot, ref m) == 0) continue;
                c(ref m);
                GhostEngine.GhostEngine_SetMap(_id, f, _slot, ref m);
            }
        }

        [DisplayName("Texture"), TypeConverter(typeof(TextureChoiceConverter))]
        public string Texture
        {
            get => GhostTextures.DisplayName(Read().textureId);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                Change((ref GhostMapInfo m) => m.textureId = tex);
            }
        }

        [DisplayName("Amount (strength / blend)")]
        public float Amount { get => Read().amount; set => Change((ref GhostMapInfo m) => m.amount = value); }

        [DisplayName("Channel"), TypeConverter(typeof(MapChannelConverter))]
        public string Channel
        {
            get => MapChannelConverter.Names[Math.Clamp(Read().channel, 0, 4)];
            set { int i = Array.IndexOf(MapChannelConverter.Names, value); if (i >= 0) Change((ref GhostMapInfo m) => m.channel = i); }
        }

        [DisplayName("Invert (Normal map: flip green)")]
        public bool Invert { get => Read().invert != 0; set => Change((ref GhostMapInfo m) => m.invert = value ? 1 : 0); }

        public override string ToString() { var m = Read(); return m.textureId == 0 ? "None" : $"{GhostTextures.DisplayName(m.textureId)}  x{m.amount:0.##}"; }
    }

    // Surface values that aren't a texture
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class SurfaceEdit
    {
        private readonly int _id;
        private delegate void SurfChange(ref GhostSurfaceInfo s);

        public SurfaceEdit(int id)
        {
            _id = id;
            MapTiling = new Vec2Edit(() => { var s = Read(); return (s.mapTilingU, s.mapTilingV); },
                (u, v) => Change((ref GhostSurfaceInfo s) => { s.mapTilingU = u; s.mapTilingV = v; }));
            MapOffset = new Vec2Edit(() => { var s = Read(); return (s.mapOffsetU, s.mapOffsetV); },
                (u, v) => Change((ref GhostSurfaceInfo s) => { s.mapOffsetU = u; s.mapOffsetV = v; }));
        }

        private GhostSurfaceInfo Read()
        {
            var s = new GhostSurfaceInfo();
            GhostEngine.GhostEngine_GetSurface(_id, Math.Max(GhostEngine.GhostEngine_GetTargetFace(), 0), ref s);
            return s;
        }

        private void Change(SurfChange c)
        {
            int face = GhostEngine.GhostEngine_GetTargetFace();
            int count = GhostEngine.GhostEngine_GetFaceCount(_id);
            int from = face < 0 ? 0 : face, to = face < 0 ? count - 1 : face;
            for (int f = from; f <= to; f++)
            {
                var s = new GhostSurfaceInfo();
                if (GhostEngine.GhostEngine_GetSurface(_id, f, ref s) == 0) continue;
                c(ref s);
                GhostEngine.GhostEngine_SetSurface(_id, f, ref s);
            }
        }

        [DisplayName("Two Sided")]
        public bool TwoSided { get => Read().twoSided != 0; set => Change((ref GhostSurfaceInfo s) => s.twoSided = value ? 1 : 0); }
        [DisplayName("Opacity (Translucent mode)")]
        public float Opacity { get => Read().opacity; set => Change((ref GhostSurfaceInfo s) => s.opacity = value); }
        [DisplayName("Refraction IOR")]
        public float RefractionIOR { get => Read().refractionIOR; set => Change((ref GhostSurfaceInfo s) => s.refractionIOR = value); }
        [DisplayName("Reflection Rotation (deg)")]
        public float ReflectionRotation { get => Read().reflectionRotDeg; set => Change((ref GhostSurfaceInfo s) => s.reflectionRotDeg = value); }
        [DisplayName("Anisotropy (-0.95 to 0.95)")]
        public float Anisotropy { get => Read().anisotropy; set => Change((ref GhostSurfaceInfo s) => s.anisotropy = value); }
        [DisplayName("Subsurface Color")]
        public Color SubsurfaceColor
        {
            get { var s = Read(); return GhostColorUtil.ToColor(s.sssR, s.sssG, s.sssB); }
            set => Change((ref GhostSurfaceInfo s) => { s.sssR = value.R / 255f; s.sssG = value.G / 255f; s.sssB = value.B / 255f; });
        }
        [DisplayName("Subsurface Amount (skin, 0-1)")]
        public float SubsurfaceAmount { get => Read().sssAmount; set => Change((ref GhostSurfaceInfo s) => s.sssAmount = value); }
        [DisplayName("Map Tiling (all extra maps)")] public Vec2Edit MapTiling { get; }
        [DisplayName("Map Offset (all extra maps)")] public Vec2Edit MapOffset { get; }
        [DisplayName("WPO Sway (wind, units)")]
        public float WpoSway { get => Read().wpoSway; set => Change((ref GhostSurfaceInfo s) => s.wpoSway = value); }
        [DisplayName("WPO Sway Speed")]
        public float WpoSpeed { get => Read().wpoSpeed; set => Change((ref GhostSurfaceInfo s) => s.wpoSpeed = value); }
        [DisplayName("WPO Mid Level (0-1)")]
        public float WpoMid { get => Read().wpoMid; set => Change((ref GhostSurfaceInfo s) => s.wpoMid = value); }

        public override string ToString() => "(opacity, IOR, subsurface, WPO ...)";
    }




    internal class AlphaSourceConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Texture Alpha", "Brightness (dark = clear)", "Inverted Brightness (light = clear)" };
        protected override string[] Items => Names;
    }

    internal class FilterConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Linear", "Point" };
        protected override string[] Items => Names;
    }

    // Lists "All Faces" plus the faces of whichever object the grid is showing
    internal class FaceChoiceConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
            => new StandardValuesCollection((context?.Instance as ObjectProxy)?.FaceChoices()
                                            ?? new List<string> { "All Faces" });
    }
    // Presets in a dropdown, but any number can be typed too
    internal class ResizeConverter : StringConverter
    {
        public static readonly string[] Names = { "Original", "4096", "2048", "1024", "512", "256", "128", "64" };
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => false;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
            => new StandardValuesCollection(Names);
    }

    internal class ResolutionConverter : FixedListConverter
    {
        public static readonly string[] Names =
            { "Match Window", "640 x 360", "1280 x 720", "1920 x 1080", "2560 x 1440", "3840 x 2160", "1024 x 768", "800 x 600" };
        public static readonly (int w, int h)[] Sizes =
            { (0, 0), (640, 360), (1280, 720), (1920, 1080), (2560, 1440), (3840, 2160), (1024, 768), (800, 600) };
        protected override string[] Items => Names;
    }
    internal static class GhostEntityList
    {
        // kind: 0 object, 1 light, 2 camera
        public static List<(int id, string name)> Of(int kind)
        {
            var list = new List<(int, string)>();
            var buf = new GhostEntityInfo[1024];
            int n = GhostEngine.GhostEngine_GetEntities(buf, buf.Length);
            for (int i = 0; i < n; i++)
                if (buf[i].kind == kind) list.Add((buf[i].id, GhostNames.Of(buf[i].id)));
            return list;
        }
    }

    internal class CameraModeConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Free", "Orbit" };
        protected override string[] Items => Names;
    }

    internal class OrbitTargetConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { "None" };
            foreach (var (_, name) in GhostEntityList.Of(0)) list.Add(name);
            return new StandardValuesCollection(list);
        }
    }

    internal class StartCameraConverter : StringConverter
    {
        public const string CurrentLabel = "Current Main View";
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { CurrentLabel };
            foreach (var (_, name) in GhostEntityList.Of(2)) list.Add(name);
            return new StandardValuesCollection(list);
        }
    }
    internal class CameraProxy
    {
        protected readonly int _id;

        public CameraProxy(int id)
        {
            _id = id;
            Position = new Vec3Edit(
                () => { var i = Get(); return (i.px, i.py, i.pz); },
                (x, y, z) => { var i = Get(); i.px = x; i.py = y; i.pz = z; Set(i); });
        }

        protected GhostCameraInfo Get() { var i = new GhostCameraInfo(); GhostEngine.GhostEngine_GetCamera(_id, ref i); return i; }
        protected void Set(GhostCameraInfo i) => GhostEngine.GhostEngine_SetCamera(ref i);

        [Category("Details")] public string Name => GhostNames.Of(_id);

        [Category("Camera"), DisplayName("Main View Camera")]
        public bool MainView
        {
            get => GhostEngine.GhostEngine_GetActiveCamera() == _id;
            set
            {
                if (value) GhostEngine.GhostEngine_SetActiveCamera(_id);
                else System.Media.SystemSounds.Beep.Play();     // there is always one main view camera
            }
        }
        [Category("Camera")]
        [TypeConverter(typeof(CameraModeConverter))]
        public string Mode
        {
            get => Get().mode == 1 ? "Orbit" : "Free";
            set { var i = Get(); i.mode = value == "Orbit" ? 1 : 0; Set(i); }
        }

        [Browsable(false)] public virtual bool IsOrbit => false;
        [Category("Camera"), DisplayName("Show Viewport")]
        public bool ShowViewport
        {
            get => GhostViewportManager.Instance?.IsShown(_id) ?? false;
            set => GhostViewportManager.Instance?.SetShown(_id, value);
        }
        [Category("Camera"), DisplayName("Yaw (deg)")]
        public float Yaw { get => Get().yawDeg; set { var i = Get(); i.yawDeg = value; Set(i); } }

        [Category("Camera"), DisplayName("Pitch (deg)")]
        public float Pitch { get => Get().pitchDeg; set { var i = Get(); i.pitchDeg = value; Set(i); } }

        [Category("Camera"), DisplayName("FOV (deg)")]
        public float Fov { get => Get().fovDeg; set { var i = Get(); i.fovDeg = value; Set(i); } }

        [Category("Camera"), DisplayName("Fly Speed (units/s)")]
        public float FlySpeed { get => Get().moveSpeed; set { var i = Get(); i.moveSpeed = value; Set(i); } }

        [Category("Transform")] public Vec3Edit Position { get; }

        [Category("Camera"), DisplayName("Resolution")]
        [TypeConverter(typeof(ResolutionConverter))]
        public string Resolution
        {
            get
            {
                var i = Get();
                for (int k = 0; k < ResolutionConverter.Sizes.Length; k++)
                    if (ResolutionConverter.Sizes[k] == (i.resW, i.resH)) return ResolutionConverter.Names[k];
                return $"{i.resW} x {i.resH}";
            }
            set
            {
                int k = Array.IndexOf(ResolutionConverter.Names, value);
                if (k < 0) return;
                var i = Get();
                i.resW = ResolutionConverter.Sizes[k].w;
                i.resH = ResolutionConverter.Sizes[k].h;
                Set(i);
            }
        }

    }
    // Shown instead of CameraProxy while the camera's Mode is Orbit
    internal class OrbitCameraProxy : CameraProxy
    {
        public OrbitCameraProxy(int id) : base(id) { }

        [Browsable(false)] public override bool IsOrbit => true;

        [Category("Orbit")]
        [TypeConverter(typeof(OrbitTargetConverter))]
        public string Target
        {
            get
            {
                int t = Get().targetId;
                string n = t == 0 ? "" : GhostNames.Of(t);
                return n.Length > 0 ? n : "None";
            }
            set
            {
                int id = 0;
                foreach (var (oid, name) in GhostEntityList.Of(0)) if (name == value) id = oid;
                var i = Get(); i.targetId = id; Set(i);
            }
        }

        [Category("Orbit"), DisplayName("Target Tag (when Target = None)")]
        public string TargetTag
        {
            get
            {
                var b = new byte[64];
                GhostEngine.GhostEngine_GetCameraTargetTag(_id, b, b.Length);
                int n = Array.IndexOf(b, (byte)0);
                return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
            }
            set => GhostEngine.GhostEngine_SetCameraTargetTag(_id, value ?? "");
        }

        [Category("Orbit")] public float Distance { get => Get().distance; set { var i = Get(); i.distance = value; Set(i); } }

        [Category("Orbit"), DisplayName("Height Offset")]
        public float HeightOffset { get => Get().heightOffset; set { var i = Get(); i.heightOffset = value; Set(i); } }

        [Category("Orbit"), DisplayName("Min Distance")]
        public float MinDistance { get => Get().minDist; set { var i = Get(); i.minDist = value; Set(i); } }

        [Category("Orbit"), DisplayName("Max Distance")]
        public float MaxDistance { get => Get().maxDist; set { var i = Get(); i.maxDist = value; Set(i); } }

        [Category("Orbit"), DisplayName("Min Pitch (deg)")]
        public float MinPitch { get => Get().minPitchDeg; set { var i = Get(); i.minPitchDeg = value; Set(i); } }

        [Category("Orbit"), DisplayName("Max Pitch (deg)")]
        public float MaxPitch { get => Get().maxPitchDeg; set { var i = Get(); i.maxPitchDeg = value; Set(i); } }

        [Category("Orbit"), DisplayName("Smoothing (0 = snap)")]
        public float Smoothing { get => Get().smoothing; set { var i = Get(); i.smoothing = value; Set(i); } }
    }
    internal class RoleConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Static", "Player", "Enemy", "Coin", "Block", "Goal", "Custom",
            "Portal Entry", "Portal Exit", "Breakable", "Moving Object", "Projectile", "Boss", "Hidden Block"};
        protected override string[] Items => Names;
    }

    internal sealed class MatPreset
    {
        public readonly string Name;
        public readonly float[] Base;          // tint (display colors)
        public readonly float Metal, Rough, Spec, EmStrength;
        public readonly float[] Emissive;

        public MatPreset(string name, float[] baseColor, float metal, float rough, float spec,
            float[]? emissive = null, float emStrength = 1f)
        {
            Name = name; Base = baseColor; Metal = metal; Rough = rough; Spec = spec;
            Emissive = emissive ?? new float[] { 0f, 0f, 0f };
            EmStrength = emStrength;
        }

        private static bool Near(float a, float b) => Math.Abs(a - b) < 0.01f;

        public bool Matches(GhostMaterialInfo m) =>
            Near(m.metallic, Metal) && Near(m.roughness, Rough) && Near(m.specular, Spec) &&
            Near(m.emR * m.emissiveStrength, Emissive[0] * EmStrength) &&
            Near(m.emG * m.emissiveStrength, Emissive[1] * EmStrength) &&
            Near(m.emB * m.emissiveStrength, Emissive[2] * EmStrength) &&
            (m.textureId != 0 || (Near(m.r, Base[0]) && Near(m.g, Base[1]) && Near(m.b, Base[2])));

        public void ApplyTo(ref GhostMaterialInfo m)
        {
            if (m.textureId == 0) { m.r = Base[0]; m.g = Base[1]; m.b = Base[2]; }   // a texture keeps its own color
            m.metallic = Metal; m.roughness = Rough; m.specular = Spec;
            m.emR = Emissive[0]; m.emG = Emissive[1]; m.emB = Emissive[2]; m.emissiveStrength = EmStrength;
        }
    }

    internal class MaterialPresetConverter : FixedListConverter
    {
        public static readonly MatPreset[] All =
        {
            new("Default Grey",    new[] { 0.60f, 0.60f, 0.60f }, 0.0f, 0.60f, 0.5f),
            new("Rubber",          new[] { 0.12f, 0.12f, 0.12f }, 0.0f, 0.85f, 0.4f),
            new("Plastic",         new[] { 0.75f, 0.20f, 0.20f }, 0.0f, 0.30f, 0.5f),
            new("Ceramic",         new[] { 0.92f, 0.92f, 0.90f }, 0.0f, 0.15f, 0.6f),
            new("Wood",            new[] { 0.55f, 0.36f, 0.20f }, 0.0f, 0.70f, 0.35f),
            new("Stone",           new[] { 0.50f, 0.50f, 0.48f }, 0.0f, 0.90f, 0.4f),
            new("Brushed Steel",   new[] { 0.77f, 0.78f, 0.78f }, 1.0f, 0.40f, 0.5f),
            new("Chrome",          new[] { 0.95f, 0.95f, 0.95f }, 1.0f, 0.08f, 0.5f),
            new("Gold",            new[] { 1.00f, 0.86f, 0.57f }, 1.0f, 0.25f, 0.5f),
            new("Copper",          new[] { 0.98f, 0.82f, 0.76f }, 1.0f, 0.30f, 0.5f),
            new("Glow",            new[] { 0.10f, 0.10f, 0.10f }, 0.0f, 0.60f, 0.5f, new[] { 1.0f, 0.85f, 0.3f }, 2.0f),
        };

        public static readonly string[] Names = BuildNames();

        private static string[] BuildNames()
        {
            var n = new string[All.Length + 1];
            n[0] = "Custom";
            for (int i = 0; i < All.Length; i++) n[i + 1] = All[i].Name;
            return n;
        }

        protected override string[] Items => Names;
    }

    internal static class GhostShadowUI
    {
        public const string Off = "Off", All = "All Lights";

        public static List<string> Choices()
        {
            var list = new List<string> { All, Off };
            foreach (var (_, name) in GhostEntityList.Of(1)) list.Add(name);      // one entry per light
            return list;
        }

        public static string GetCast(int objectId)
        {
            if (GhostEngine.GhostEngine_GetObjectShadow(objectId, out int mode, out int light, out _) == 0) return All;
            if (mode == 0) return Off;
            if (mode == 2)
            {
                string n = GhostNames.Of(light);
                if (n.Length > 0) return n;
            }
            return All;
        }

        public static void SetCast(int objectId, string value)
        {
            GhostEngine.GhostEngine_GetObjectShadow(objectId, out _, out _, out int receive);
            int mode = 1, light = 0;
            if (value == Off) mode = 0;
            else if (value != All)
                foreach (var (id, name) in GhostEntityList.Of(1))
                    if (name == value) { mode = 2; light = id; }
            GhostEngine.GhostEngine_SetObjectShadow(objectId, mode, light, receive);
        }

        public static bool GetReceive(int objectId)
        {
            GhostEngine.GhostEngine_GetObjectShadow(objectId, out _, out _, out int receive);
            return receive != 0;
        }

        public static void SetReceive(int objectId, bool on)
        {
            if (GhostEngine.GhostEngine_GetObjectShadow(objectId, out int mode, out int light, out _) == 0) return;
            GhostEngine.GhostEngine_SetObjectShadow(objectId, mode, light, on ? 1 : 0);
        }
    }

    internal sealed class ShadowCasterConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
            => new StandardValuesCollection(GhostShadowUI.Choices());
    }
    internal class ModelDefaultsConverter : FixedListConverter
    {
        public static readonly string[] Names = { "(choose)", "Materials", "Collider", "Materials and Collider" };
        protected override string[] Items => Names;
    }

    internal sealed class PathModeConverter : FixedListConverter
    { public static readonly string[] Names = { "Ping-Pong (A to B and back)", "Restart (jump back to A)", "Once (stop at B)", "Timed Elevator (synced loop, uses Timing Offset)" }; protected override string[] Items => Names; }
    internal sealed class BreakEffectConverter : FixedListConverter
    { public static readonly string[] Names = { "Vanish", "Play Break clip, then vanish", "Play Break clip, leave rubble" }; protected override string[] Items => Names; }
    internal sealed class ContentsConverter : FixedListConverter
    { public static readonly string[] Names = { "Role default", "Nothing", "Coin", "Power-up" }; protected override string[] Items => Names; }
    internal sealed class PortalTriggerConverter : FixedListConverter
    { public static readonly string[] Names = { "Touch (walk through)", "Press Down", "Press F" }; protected override string[] Items => Names; }

     internal sealed class PowerTypeConverter : FixedListConverter
    { public static readonly string[] Names = { "Auto (mushroom, then flower)", "Mushroom", "Fire Flower", "Star", "1-Up", "Spinning Coin" }; protected override string[] Items => Names; }
    internal sealed class ItemMoveConverter : FixedListConverter
    { public static readonly string[] Names = { "By type", "Moves", "Stays put" }; protected override string[] Items => Names; }

    internal sealed class EnemyPathConverter : FixedListConverter
    { public static readonly string[] Names = { "Ping-Pong (A to B and back)", "Ping-Pong, turn at ledges", "Once (stop at B)", "Stand still" }; protected override string[] Items => Names; }

    internal sealed class EnemyDeathEffectConverter : FixedListConverter
    { public static readonly string[] Names = { "Vanish", "Bounce off screen", "Flatten", "Drop Shell" }; protected override string[] Items => Names; }

    internal sealed class FlyingEnemyConverter : FixedListConverter
    { public static readonly string[] Names = { "No", "Yes (no gravity)", "Winged (first stomp drops it)" }; protected override string[] Items => Names; }


    // Filters the Behavior group down to the selected role's own properties
    internal sealed class RoleBehaviorConverter : ExpandableObjectConverter
    {
        public override bool GetPropertiesSupported(ITypeDescriptorContext? context) => true;

        public override PropertyDescriptorCollection? GetProperties(ITypeDescriptorContext? context, object value, Attribute[]? attributes)
        {
            var all = base.GetProperties(context, value, attributes);
            if (all == null || value is not BehaviorEdit b) return all;

            var keep = new List<PropertyDescriptor>();
            foreach (string name in BehaviorEdit.PropsFor(b.CurrentRole))
            {
                PropertyDescriptor? pd = all[name];
                if (pd != null) keep.Add(pd);
            }
            return new PropertyDescriptorCollection(keep.ToArray(), true);
        }
    }


    // Details > Gameplay > Behavior: the settings the new roles use
    [TypeConverter(typeof(RoleBehaviorConverter))]
    internal sealed class BehaviorEdit
    {
        private readonly int _id;
        private readonly int _shownRole;

        public BehaviorEdit(int id)
        {
            _id = id;
            _shownRole = Get().role;
            MoveOffset = new Vec3Edit(
                () => { var g = Get(); return (g.moveX, g.moveY, g.moveZ); },
                (x, y, z) => { var g = Get(); g.moveX = x; g.moveY = y; g.moveZ = z; Set(g); });
            PlayerMovement = new PlayerMoveGroup(id); PlayerJump = new PlayerJumpGroup(id);
            PlayerDamage = new PlayerDamageGroup(id); PlayerPower = new PlayerPowerGroup(id);
            PlayerSkid = new PlayerSkidGroup(id); PlayerAttack = new PlayerAttackGroup(id);
            PlayerDeath = new PlayerDeathGroup(id); PlayerModel = new PlayerModelGroup(id);
            EnemyPhysics = new EnemyPhysicsGroup(id); EnemyAttack = new EnemyAttackGroup(id);
            EnemyDeath = new EnemyDeathGroup(id); EnemyModel = new EnemyModelGroup(id);
            EnemyPathway = new EnemyPathGroup(id);
            BossMotion = new BossMotionGroup(id); BossPathway = new BossPathGroup(id); BossModel = new BossModelGroup(id);
            PathPoint = new PathPointGroup(id);
        }

        private GhostGameplayInfo Get() { var g = new GhostGameplayInfo(); GhostEngine.GhostEngine_GetGameplay(_id, ref g); return g; }
        private void Set(GhostGameplayInfo g) => GhostEngine.GhostEngine_SetGameplay(ref g);

        [Browsable(false)] public int CurrentRole => Get().role;
        [Browsable(false)] public int ShownRole => _shownRole;

        // Role index (same order as RoleConverter.Names) -> Behavior properties that role shows.
        // 0 Static, 1 Player, 2 Enemy, 3 Coin, 4 Block, 5 Goal, 6 Custom, 7 Portal Entry,
        // 8 Portal Exit, 9 Breakable, 10 Moving Object, 11 Projectile, 12 Boss, 13 Hidden Block
        private static readonly string[] NoProps = Array.Empty<string>();
        private static readonly string[] PlayerProps = { nameof(PlayerMovement), nameof(PlayerJump), nameof(PlayerDamage),
        nameof(PlayerPower), nameof(PlayerSkid), nameof(PlayerAttack), nameof(PlayerDeath), nameof(PlayerModel) };
        private static readonly string[] EnemyProps = { nameof(MoveOffset), nameof(MoveSpeed), nameof(MovePause),
        nameof(EnemyPathMode), nameof(FlyingEnemy), nameof(EnemyDeathEffect),
        nameof(EnemyPhysics), nameof(EnemyAttack), nameof(EnemyDeath), nameof(EnemyModel), nameof(EnemyPathway) };
        private static readonly string[] BossProps = { nameof(MoveOffset), nameof(MoveSpeed), nameof(MovePause), nameof(PathMode),
        nameof(BossMotion), nameof(BossPathway), nameof(BossModel) };
        private static readonly string[] CustomProps = { nameof(PathPoint) };
        private static readonly string[] BlockProps = { nameof(Contents), nameof(PowerType), nameof(PickupMovement), nameof(CoinCount) };
        private static readonly string[] BreakableProps = { nameof(Contents), nameof(PowerType), nameof(PickupMovement), nameof(CoinCount), nameof(BreakEffect) };
        private static readonly string[] PortalProps = { nameof(PortalTrigger) };
        private static readonly string[] MovingProps = { nameof(MoveOffset), nameof(MoveSpeed), nameof(MovePause), nameof(PathMode), nameof(TimingOffset) };

        internal static string[] PropsFor(int role) => role switch
        {
            1 => PlayerProps,
            2 => EnemyProps,
            4 => BlockProps,
            7 => PortalProps,
            6 => CustomProps,         
            9 => BreakableProps,
            10 => MovingProps,
            12 => BossProps,
            13 => BlockProps,
            _ => NoProps,        // Static, Player, Coin, Goal, Custom, Portal Exit, Projectile, Boss: empty for now
        };



     

        [DisplayName("Portal Trigger"), TypeConverter(typeof(PortalTriggerConverter))]
        public string PortalTrigger
        {
            get => PortalTriggerConverter.Names[Math.Clamp(Get().portalTrigger, 0, 2)];
            set { int i = Array.IndexOf(PortalTriggerConverter.Names, value); if (i >= 0) { var g = Get(); g.portalTrigger = i; Set(g); } }
        }

        [DisplayName("Move Offset (A to B)")] public Vec3Edit MoveOffset { get; }

        static BehaviorEdit() { GhostRoleParamCheck.Verify(); }

        [DisplayName("Movement")] public PlayerMoveGroup PlayerMovement { get; }
        [DisplayName("Jump")] public PlayerJumpGroup PlayerJump { get; }
        [DisplayName("Stomp && Damage")] public PlayerDamageGroup PlayerDamage { get; }
        [DisplayName("Power-Ups")] public PlayerPowerGroup PlayerPower { get; }
        [DisplayName("Skid")] public PlayerSkidGroup PlayerSkid { get; }
        [DisplayName("Attack && Fireball")] public PlayerAttackGroup PlayerAttack { get; }
        [DisplayName("Death")] public PlayerDeathGroup PlayerDeath { get; }
        [DisplayName("3D Model")] public PlayerModelGroup PlayerModel { get; }

        [DisplayName("Physics && Patrol")] public EnemyPhysicsGroup EnemyPhysics { get; }
        [DisplayName("Attack (3D)")] public EnemyAttackGroup EnemyAttack { get; }
        [DisplayName("Death")] public EnemyDeathGroup EnemyDeath { get; }
        [DisplayName("3D Model")] public EnemyModelGroup EnemyModel { get; }


        [DisplayName("Move Speed (units/s)")] public float MoveSpeed { get => Get().moveSpeed; set { var g = Get(); g.moveSpeed = value; Set(g); } }
        [DisplayName("Move Pause at ends (s)")] public float MovePause { get => Get().movePause; set { var g = Get(); g.movePause = value; Set(g); } }

        [DisplayName("Path Mode"), TypeConverter(typeof(PathModeConverter))]
        public string PathMode
        {
            get => PathModeConverter.Names[Math.Clamp(Get().pathMode, 0, PathModeConverter.Names.Length - 1)];
            set { int i = Array.IndexOf(PathModeConverter.Names, value); if (i >= 0) { var g = Get(); g.pathMode = i; Set(g); } }
        }
        [DisplayName("Timing Offset (s) (Timed Elevator)")]
        public float TimingOffset { get => Get().timeOffset; set { var g = Get(); g.timeOffset = value; Set(g); } }

        [DisplayName("Enemy Path Mode (Role = Enemy)"), TypeConverter(typeof(EnemyPathConverter))]
        public string EnemyPathMode
        {
            get => EnemyPathConverter.Names[Math.Clamp(Get().enemyPathMode, 0, 3)];
            set { int i = Array.IndexOf(EnemyPathConverter.Names, value); if (i >= 0) { var g = Get(); g.enemyPathMode = i; Set(g); } }
        }
        [DisplayName("Flying Enemy (no gravity)"), TypeConverter(typeof(FlyingEnemyConverter))]
        public string FlyingEnemy
        {
            get => FlyingEnemyConverter.Names[Math.Clamp(Get().flying, 0, 2)];
            set { int i = Array.IndexOf(FlyingEnemyConverter.Names, value); if (i >= 0) { var g = Get(); g.flying = i; Set(g); } }
        }
        [DisplayName("Enemy Death Effect"), TypeConverter(typeof(EnemyDeathEffectConverter))]
        public string EnemyDeathEffect
        {
            get => EnemyDeathEffectConverter.Names[Math.Clamp(Get().enemyDeathEffect, 0, 3)];
            set { int i = Array.IndexOf(EnemyDeathEffectConverter.Names, value); if (i >= 0) { var g = Get(); g.enemyDeathEffect = i; Set(g); } }
        }
        [DisplayName("Break Effect"), TypeConverter(typeof(BreakEffectConverter))]
        public string BreakEffect
        {
            get => BreakEffectConverter.Names[Math.Clamp(Get().breakEffect, 0, 2)];
            set { int i = Array.IndexOf(BreakEffectConverter.Names, value); if (i >= 0) { var g = Get(); g.breakEffect = i; Set(g); } }
        }

        [DisplayName("Contents (blocks, breakables)"), TypeConverter(typeof(ContentsConverter))]
        public string Contents
        {
            get => ContentsConverter.Names[Math.Clamp(Get().contents, 0, 3)];
            set { int i = Array.IndexOf(ContentsConverter.Names, value); if (i >= 0) { var g = Get(); g.contents = i; Set(g); } }
        }

        public override string ToString() => PropsFor(CurrentRole).Length == 0 ? "(none for this role)" : "(role settings)";

        [DisplayName("Power-Up Type (contents = Power-up)"), TypeConverter(typeof(PowerTypeConverter))]
        public string PowerType
        {
            get => PowerTypeConverter.Names[Math.Clamp(Get().powerType, 0, 5)];
            set { int i = Array.IndexOf(PowerTypeConverter.Names, value); if (i >= 0) { var g = Get(); g.powerType = i; Set(g); } }
        }
        [DisplayName("Pickup Movement"), TypeConverter(typeof(ItemMoveConverter))]
        public string PickupMovement
        {
            get => ItemMoveConverter.Names[Math.Clamp(Get().itemMoves, 0, 2)];
            set { int i = Array.IndexOf(ItemMoveConverter.Names, value); if (i >= 0) { var g = Get(); g.itemMoves = i; Set(g); } }
        }
        [DisplayName("Coin Count (contents = Coin)")]
        public int CoinCount { get => Get().coinCount; set { var g = Get(); g.coinCount = Math.Clamp(value, 1, 999); Set(g); } }


        [DisplayName("Pathway Following")] public EnemyPathGroup EnemyPathway { get; }
        [DisplayName("Motion")] public BossMotionGroup BossMotion { get; }
        [DisplayName("Pathway Following")] public BossPathGroup BossPathway { get; }
        [DisplayName("3D Model")] public BossModelGroup BossModel { get; }
        [DisplayName("Pathway Point (Custom Tag = Pathway)")] public PathPointGroup PathPoint { get; }
    }


    internal class ObjectProxy
    {
        private readonly int _id;
        private readonly MapEdit[] _maps = new MapEdit[13];
        public ObjectProxy(int id)
        {
            _id = id;
            Position = new Vec3Edit(
                () => { var i = Get(); return (i.px, i.py, i.pz); },
                (x, y, z) => { var i = Get(); i.px = x; i.py = y; i.pz = z; Set(i); });
            Rotation = new Vec3Edit(
                () => { var i = Get(); return (i.rx, i.ry, i.rz); },
                (x, y, z) => { var i = Get(); i.rx = x; i.ry = y; i.rz = z; Set(i); });
            Scale = new Vec3Edit(
                () => { var i = Get(); return (i.sx, i.sy, i.sz); },
                (x, y, z) => { var i = Get(); i.sx = x; i.sy = y; i.sz = z; Set(i); });
            Tiling = new Vec2Edit(
    () => { var m = ReadMat(); return (m.tilingU, m.tilingV); },
    (u, v) => EditMat((ref GhostMaterialInfo m) => { m.tilingU = u; m.tilingV = v; }));
            Offset = new Vec2Edit(
                () => { var m = ReadMat(); return (m.offsetU, m.offsetV); },
                (u, v) => EditMat((ref GhostMaterialInfo m) => { m.offsetU = u; m.offsetV = v; }));
            ColliderSize = new Vec3Edit(
    () => { var g = GetG(); return (g.csx, g.csy, g.csz); },
    (x, y, z) => { var g = GetG(); g.csx = x; g.csy = y; g.csz = z; SetG(g); });
            ColliderOffset = new Vec3Edit(
    () => { var g = GetG(); return (g.cox, g.coy, g.coz); },
    (x, y, z) => { var g = GetG(); g.cox = x; g.coy = y; g.coz = z; SetG(g); });
            ColliderRotation = new Vec3Edit(
() => { var g = GetG(); return (g.crx, g.cry, g.crz); },
(x, y, z) => { var g = GetG(); g.crx = x; g.cry = y; g.crz = z; SetG(g); });
            Dimensions = new DimEdit(id);
            Behavior = new BehaviorEdit(id);
            for (int k = 0; k < 13; k++) _maps[k] = new MapEdit(id, k);
            SurfaceExtras = new SurfaceEdit(id);
        }

        private GhostObjectInfo Get() { var i = new GhostObjectInfo(); GhostEngine.GhostEngine_GetObject(_id, ref i); return i; }
        private void Set(GhostObjectInfo i) => GhostEngine.GhostEngine_SetObject(ref i);

        [Category("Details")] public string Name => GhostNames.Of(_id);

        [Category("Transform")] public Vec3Edit Position { get; }
        [Category("Transform"), DisplayName("Rotation (deg)")] public Vec3Edit Rotation { get; }
        [Category("Transform")] public Vec3Edit Scale { get; }

        [Category("Transform"), DisplayName("Size (type 10mm, 2in ...)")]
        public DimEdit Dimensions { get; }

        // ---- Material: edits go to the Target Face, or to every face when it is "All Faces" ----
        private delegate void MatEdit(ref GhostMaterialInfo m);

        private GhostMaterialInfo ReadMat()          // "All Faces" shows face 0's values
        {
            var m = new GhostMaterialInfo();
            GhostEngine.GhostEngine_GetMaterial(_id, Math.Max(GhostEngine.GhostEngine_GetTargetFace(), 0), ref m);
            return m;
        }

        private void EditMat(MatEdit edit)            // changes one field and leaves each face's other settings alone
        {
            int face = GhostEngine.GhostEngine_GetTargetFace();
            int count = GhostEngine.GhostEngine_GetFaceCount(_id);
            int from = face < 0 ? 0 : face;
            int to = face < 0 ? count - 1 : face;
            for (int f = from; f <= to; f++)
            {
                var m = new GhostMaterialInfo();
                if (GhostEngine.GhostEngine_GetMaterial(_id, f, ref m) == 0) continue;
                edit(ref m);
                GhostEngine.GhostEngine_SetMaterial(_id, f, ref m);
            }
        }

        private string FaceName(int f)
        {
            var sb = new StringBuilder(64);
            return GhostEngine.GhostEngine_GetFaceName(_id, f, sb, sb.Capacity) != 0 ? sb.ToString() : "All Faces";
        }

        internal List<string> FaceChoices()
        {
            var list = new List<string> { "All Faces" };
            int n = GhostEngine.GhostEngine_GetFaceCount(_id);
            for (int f = 0; f < n; f++) list.Add(FaceName(f));
            return list;
        }

        [Category("Material"), DisplayName("Material Slot")]
        [TypeConverter(typeof(FaceChoiceConverter))]
        public string TargetFace
        {
            get { int f = GhostEngine.GhostEngine_GetTargetFace(); return f < 0 ? "All Faces" : FaceName(f); }
            set
            {
                int n = GhostEngine.GhostEngine_GetFaceCount(_id);
                int face = -1;
                for (int f = 0; f < n; f++) if (FaceName(f) == value) face = f;
                GhostEngine.GhostEngine_SetTargetFace(face);
            }
        }

        [Category("Material"), DisplayName("Texture")]
        [TypeConverter(typeof(TextureChoiceConverter))]
        public string Texture
        {
            get => GhostTextures.DisplayName(ReadMat().textureId);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk()
                                                               : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                EditMat((ref GhostMaterialInfo m) =>
                {
                    if (m.textureId == 0 && tex != 0) m.r = m.g = m.b = 1.0f;   // white tint so the image shows at full strength
                    else if (tex == 0) m.r = m.g = m.b = 0.6f;   // back to the default grey
                    m.textureId = tex;
                });
            }
        }

        [Category("Material"), DisplayName("Image Size")]
        public string ImageSize => GhostTextures.SizeText(ReadMat().textureId);

        [Category("Material"), DisplayName("Resize (max px)")]
        [TypeConverter(typeof(ResizeConverter))]
        public string Resize
        {
            get => GhostAssets.ResizeText(ReadMat().textureId);
            set
            {
                int tex = ReadMat().textureId;
                var a = GhostAssets.Get(tex);
                if (a == null || a.Path == null) { System.Media.SystemSounds.Beep.Play(); return; }   // built-ins have no source file

                if (!GhostAssets.TryParseSize(value, out int max))
                {
                    MessageBox.Show("Enter Original, or the longest side in pixels (1 - 4096).",
                        "Resize", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }
                try { GhostAssets.Resize(tex, max); }
                catch (Exception ex)
                {
                    MessageBox.Show($"Could not re-read the source image:\n{ex.Message}",
                        "Resize failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
            }
        }

        [Category("Material"), DisplayName("Mapping")]
        [TypeConverter(typeof(MapModeConverter))]
        public string Mapping
        {
            get => MapModeConverter.Names[Math.Clamp(ReadMat().mapMode, 0, MapModeConverter.Names.Length - 1)];
            set
            {
                int i = Array.IndexOf(MapModeConverter.Names, value);
                if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.mapMode = i);
            }
        }

        [Category("Material")] public Vec2Edit Tiling { get; }
        [Category("Material")] public Vec2Edit Offset { get; }

        [Category("Material"), DisplayName("UV Rotation (deg)")]
        public float UVRotation
        {
            get => ReadMat().rotationDeg;
            set => EditMat((ref GhostMaterialInfo m) => m.rotationDeg = value);
        }

        [Category("Material")]
        [TypeConverter(typeof(WrapConverter))]
        public string Wrap
        {
            get => WrapConverter.Names[Math.Clamp(ReadMat().wrap, 0, 1)];
            set
            {
                int i = Array.IndexOf(WrapConverter.Names, value);
                if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.wrap = i);
            }
        }

        [Category("Material")]
        [TypeConverter(typeof(FilterConverter))]
        public string Filter
        {
            get => FilterConverter.Names[Math.Clamp(ReadMat().filter, 0, 1)];
            set
            {
                int i = Array.IndexOf(FilterConverter.Names, value);
                if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.filter = i);
            }
        }

        [Category("Material")]
        public Color Tint
        {
            get { var m = ReadMat(); return GhostColorUtil.ToColor(m.r, m.g, m.b); }
            set => EditMat((ref GhostMaterialInfo m) => { m.r = value.R / 255f; m.g = value.G / 255f; m.b = value.B / 255f; });
        }

        [Category("Transparency"), DisplayName("Alpha Mode")]
        [TypeConverter(typeof(AlphaModeConverter))]
        public string AlphaMode
        {
            get => AlphaModeConverter.Names[Math.Clamp(ReadMat().alphaMode, 0, 1)];
            set { int i = Array.IndexOf(AlphaModeConverter.Names, value); if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.alphaMode = i); }
        }

        [Category("Transparency"), DisplayName("Alpha Source")]
        [TypeConverter(typeof(AlphaSourceConverter))]
        public string AlphaSource
        {
            get => AlphaSourceConverter.Names[Math.Clamp(ReadMat().alphaSource, 0, 2)];
            set { int i = Array.IndexOf(AlphaSourceConverter.Names, value); if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.alphaSource = i); }
        }

        [Category("Transparency"), DisplayName("Alpha Cutoff (0-1)")]
        public float AlphaCutoff
        {
            get => ReadMat().alphaCutoff;
            set => EditMat((ref GhostMaterialInfo m) => m.alphaCutoff = value);
        }

        // ---- Surface (PBR) ----
        [Category("Surface (PBR)")]
        [TypeConverter(typeof(MaterialPresetConverter))]
        public string Preset
        {
            get
            {
                var m = ReadMat();
                foreach (MatPreset p in MaterialPresetConverter.All)
                    if (p.Matches(m)) return p.Name;
                return "Custom";
            }
            set
            {
                foreach (MatPreset p in MaterialPresetConverter.All)
                    if (p.Name == value)
                    {
                        MatPreset chosen = p;
                        EditMat((ref GhostMaterialInfo m) => chosen.ApplyTo(ref m));
                        return;
                    }
            }
        }

        [Category("Surface (PBR)")]
        public float Metallic
        {
            get => ReadMat().metallic;
            set => EditMat((ref GhostMaterialInfo m) => m.metallic = value);
        }

        [Category("Surface (PBR)")]
        public float Roughness
        {
            get => ReadMat().roughness;
            set => EditMat((ref GhostMaterialInfo m) => m.roughness = value);
        }

        [Category("Surface (PBR)")]
        public float Specular
        {
            get => ReadMat().specular;
            set => EditMat((ref GhostMaterialInfo m) => m.specular = value);
        }

        [Category("Surface (PBR)"), DisplayName("Emissive Color")]
        public Color EmissiveColor
        {
            get { var m = ReadMat(); return GhostColorUtil.ToColor(m.emR, m.emG, m.emB); }
            set => EditMat((ref GhostMaterialInfo m) => { m.emR = value.R / 255f; m.emG = value.G / 255f; m.emB = value.B / 255f; });
        }

        [Category("Surface (PBR)"), DisplayName("Emissive Strength")]
        public float EmissiveStrength
        {
            get => ReadMat().emissiveStrength;
            set => EditMat((ref GhostMaterialInfo m) => m.emissiveStrength = value);
        }

        [Category("Surface (PBR)"), DisplayName("Normal Map")] public MapEdit NormalMap => _maps[0];
        [Category("Surface (PBR)"), DisplayName("Specular Map")] public MapEdit SpecularMap => _maps[1];
        [Category("Surface (PBR)"), DisplayName("Metallic Map")] public MapEdit MetallicMap => _maps[2];
        [Category("Surface (PBR)"), DisplayName("Roughness Map")] public MapEdit RoughnessMap => _maps[3];
        [Category("Surface (PBR)"), DisplayName("Ambient Occlusion")] public MapEdit AmbientOcclusionMap => _maps[4];
        [Category("Surface (PBR)"), DisplayName("Emissive Map")] public MapEdit EmissiveMap => _maps[5];
        [Category("Surface (PBR)"), DisplayName("Opacity Map")] public MapEdit OpacityMap => _maps[6];
        [Category("Surface (PBR)"), DisplayName("Opacity Mask")] public MapEdit OpacityMaskMap => _maps[7];
        [Category("Surface (PBR)"), DisplayName("Tangent Map")] public MapEdit TangentMap => _maps[8];
        [Category("Surface (PBR)"), DisplayName("World Position Offset")] public MapEdit WpoMap => _maps[9];
        [Category("Surface (PBR)"), DisplayName("Refraction Map")] public MapEdit RefractionMap => _maps[10];
        [Category("Surface (PBR)"), DisplayName("Reflection Map (environment)")] public MapEdit ReflectionMap => _maps[11];
        [Category("Surface (PBR)"), DisplayName("Custom Data (subsurface mask)")] public MapEdit CustomDataMap => _maps[12];
        [Category("Surface (PBR)"), DisplayName("Advanced Surface")] public SurfaceEdit SurfaceExtras { get; }


        // ---- Shadows ----
        [Category("Shadows"), DisplayName("Cast Shadows")]
        [TypeConverter(typeof(ShadowCasterConverter))]
        public string CastShadows
        {
            get => GhostShadowUI.GetCast(_id);
            set => GhostShadowUI.SetCast(_id, value);
        }

        [Category("Shadows"), DisplayName("Receive Shadows")]
        public bool ReceiveShadows
        {
            get => GhostShadowUI.GetReceive(_id);
            set => GhostShadowUI.SetReceive(_id, value);
        }

        // ---- Mesh (read-only) ----
        private int MeshId() => Get().meshId;

        private GhostMeshStats MeshStats()
        {
            GhostEngine.GhostEngine_GetMeshInfo(MeshId(), out GhostMeshStats s);
            return s;
        }

        [Category("Mesh")]
        public string Model
        {
            get
            {
                string missing = GhostModels.MissingMesh(_id);
                return missing.Length > 0 ? $"MISSING: {missing} (placeholder)" : GhostModels.MeshName(MeshId());
            }
        }

        [Category("Mesh")] public string Triangles => MeshStats().triangleCount.ToString("N0");
        [Category("Mesh")] public string Vertices => MeshStats().vertexCount.ToString("N0");

        [Category("Mesh"), DisplayName("Material Slots")]
        public string MaterialSlots
        {
            get
            {
                var names = FaceChoices();
                names.RemoveAt(0);                                          // "All Faces"
                return $"{names.Count}: " + string.Join(", ", names);
            }
        }

        [Category("Mesh"), DisplayName("Model Size (units, before Scale)")]
        public string ModelSize
        {
            get
            {
                var s = MeshStats();
                return $"{s.maxX - s.minX:0.##} x {s.maxY - s.minY:0.##} x {s.maxZ - s.minZ:0.##}";
            }
        }

        [Category("Mesh"), DisplayName("Mesh File")]
        public string MeshFile { get { string t = GhostModels.MeshText(MeshId(), 1); return t.Length == 0 ? "(built-in or this session only)" : t; } }

        [Category("Mesh"), DisplayName("Source File")]
        public string SourceFile { get { string t = GhostModels.MeshText(MeshId(), 2); return t.Length == 0 ? "(built-in)" : t; } }
        [Category("Mesh"), DisplayName("Reset To Model Defaults")]
        [TypeConverter(typeof(ModelDefaultsConverter))]
        public string ResetDefaults
        {
            get => ModelDefaultsConverter.Names[0];
            set
            {
                int i = Array.IndexOf(ModelDefaultsConverter.Names, value);       // 1 materials, 2 collider, 3 both
                if (i > 0) GhostEngine.GhostEngine_ApplyMeshDefaults(_id, i);
            }
        }

        // ---- Gameplay ----
        private GhostGameplayInfo GetG() { var g = new GhostGameplayInfo(); GhostEngine.GhostEngine_GetGameplay(_id, ref g); return g; }
        private void SetG(GhostGameplayInfo g) => GhostEngine.GhostEngine_SetGameplay(ref g);

        [Category("Gameplay")]
        [TypeConverter(typeof(RoleConverter))]
        public string Role
        {
            get => RoleConverter.Names[Math.Clamp(GetG().role, 0, RoleConverter.Names.Length - 1)];
            set
            {
                int i = Array.IndexOf(RoleConverter.Names, value);
                if (i >= 0) { var g = GetG(); g.role = i; SetG(g); }
            }
        }

        [Category("Gameplay"), DisplayName("Custom Tag (when Role = Custom)")]
        public string CustomTag
        {
            get
            {
                var b = new byte[64];
                GhostEngine.GhostEngine_GetCustomTag(_id, b, b.Length);
                int n = Array.IndexOf(b, (byte)0);
                return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
            }
            set => GhostEngine.GhostEngine_SetCustomTag(_id, value ?? "");
        }

        [Category("Gameplay"), DisplayName("Link ID (portals, or Scenes/Other.ghostscene)")]
        public string LinkId
        {
            get
            {
                var b = new byte[128];
                GhostEngine.GhostEngine_GetLinkId(_id, b, b.Length);
                int n = Array.IndexOf(b, (byte)0);
                return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
            }
            set => GhostEngine.GhostEngine_SetLinkId(_id, value ?? "");
        }

        [Category("Gameplay")]
        public bool Collider
        {
            get => GetG().collider != 0;
            set { var g = GetG(); g.collider = value ? 1 : 0; SetG(g); }
        }

        [Category("Gameplay"), DisplayName("Collider Size")]
        public Vec3Edit ColliderSize { get; }

        [Category("Gameplay"), DisplayName("Collider Offset")]
        public Vec3Edit ColliderOffset { get; }

        [Category("Gameplay"), DisplayName("Collider Rotation (deg)")]
        public Vec3Edit ColliderRotation { get; }

        [Category("Gameplay"), DisplayName("Visible In Game")]
        public bool VisibleInGame
        {
            get => GetG().visible != 0;
            set { var g = GetG(); g.visible = value ? 1 : 0; SetG(g); }
        }

        [Category("Gameplay"), DisplayName("Locked in place")]
        public bool Locked { get => GetG().locked != 0; set { var g = GetG(); g.locked = value ? 1 : 0; SetG(g); } }

        [Category("Gameplay")] public BehaviorEdit Behavior { get; }
        internal bool BehaviorOutOfDate() => Behavior.CurrentRole != Behavior.ShownRole;


    }

    internal class FlickerPatternConverter : FixedListConverter
    {
        public static readonly string[] Names =
            { "Random", "Smooth Noise", "Sine Pulse", "Telephone Ring (double burst)", "Broken Bulb", "Candle", "Custom String" };
        protected override string[] Items => Names;
    }

    // One flicker layer of a Point light
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class FlickerEdit
    {
        private readonly int _id, _layer;
        private delegate void FlChange(ref GhostFlickerInfo f);
        public FlickerEdit(int id, int layer) { _id = id; _layer = layer; }

        private GhostFlickerInfo Read() { var f = new GhostFlickerInfo(); GhostEngine.GhostEngine_GetFlicker(_id, _layer, ref f); return f; }
        private void Change(FlChange c) { var f = Read(); c(ref f); GhostEngine.GhostEngine_SetFlicker(_id, _layer, ref f); }

        [DisplayName("Enabled")]
        public bool Enabled { get => Read().enabled != 0; set => Change((ref GhostFlickerInfo f) => f.enabled = value ? 1 : 0); }

        [DisplayName("Pattern"), TypeConverter(typeof(FlickerPatternConverter))]
        public string Pattern
        {
            get => FlickerPatternConverter.Names[Math.Clamp(Read().pattern, 0, FlickerPatternConverter.Names.Length - 1)];
            set { int i = Array.IndexOf(FlickerPatternConverter.Names, value); if (i >= 0) Change((ref GhostFlickerInfo f) => f.pattern = i); }
        }

        [DisplayName("Custom Pattern (letters a-z: a dark, z bright)")]
        public string CustomPattern
        {
            get
            {
                var b = new byte[64];
                GhostEngine.GhostEngine_GetFlickerText(_id, _layer, b, b.Length);
                int n = Array.IndexOf(b, (byte)0);
                return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
            }
            set => GhostEngine.GhostEngine_SetFlickerText(_id, _layer, value ?? "");
        }

        [DisplayName("Speed (steps or cycles / sec)")]
        public float Speed { get => Read().speed; set => Change((ref GhostFlickerInfo f) => f.speed = value); }
        [DisplayName("Dark Level (0 = fully off)")]
        public float MinLevel { get => Read().minLevel; set => Change((ref GhostFlickerInfo f) => f.minLevel = value); }
        [DisplayName("Smooth Steps")]
        public bool SmoothSteps { get => Read().smoothSteps != 0; set => Change((ref GhostFlickerInfo f) => f.smoothSteps = value ? 1 : 0); }
        [DisplayName("Seed")]
        public float Seed { get => Read().seed; set => Change((ref GhostFlickerInfo f) => f.seed = value); }

        [DisplayName("Flicker Brightness (added light)")]
        public float Brightness { get => Read().brightness; set => Change((ref GhostFlickerInfo f) => f.brightness = value); }
        [DisplayName("Affect Main Light (0-1)")]
        public float BaseInfluence { get => Read().baseInfluence; set => Change((ref GhostFlickerInfo f) => f.baseInfluence = value); }

        [DisplayName("Flicker Color")]
        public Color FlickerColor
        {
            get { var f = Read(); return GhostColorUtil.ToColor(f.r, f.g, f.b); }
            set => Change((ref GhostFlickerInfo f) => { f.r = value.R / 255f; f.g = value.G / 255f; f.b = value.B / 255f; });
        }

        [DisplayName("Flicker Texture"), TypeConverter(typeof(TextureChoiceConverter))]
        public string Texture
        {
            get => GhostTextures.DisplayName(Read().textureId);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                Change((ref GhostFlickerInfo f) => f.textureId = tex);
            }
        }
        [DisplayName("Texture Scroll Speed")]
        public float TexSpeed { get => Read().texSpeed; set => Change((ref GhostFlickerInfo f) => f.texSpeed = value); }
        [DisplayName("Texture Tiling")]
        public float TexTiling { get => Read().texTiling; set => Change((ref GhostFlickerInfo f) => f.texTiling = value); }
        [DisplayName("Texture Amount (0-1)")]
        public float TexAmount { get => Read().texAmount; set => Change((ref GhostFlickerInfo f) => f.texAmount = value); }

        [DisplayName("PBR Diffuse Response")]
        public float Diffuse { get => Read().diffuse; set => Change((ref GhostFlickerInfo f) => f.diffuse = value); }
        [DisplayName("PBR Specular Response")]
        public float Specular { get => Read().specular; set => Change((ref GhostFlickerInfo f) => f.specular = value); }
        [DisplayName("PBR Roughness (-1 = material)")]
        public float Roughness { get => Read().roughness; set => Change((ref GhostFlickerInfo f) => f.roughness = value); }
        [DisplayName("PBR Metallic Boost (0-1)")]
        public float Metallic { get => Read().metallic; set => Change((ref GhostFlickerInfo f) => f.metallic = value); }

        public override string ToString() { var f = Read(); return f.enabled == 0 ? "Off" : "On  -  " + FlickerPatternConverter.Names[Math.Clamp(f.pattern, 0, 6)]; }
    }


    internal abstract class LightProxy
    {
        protected readonly int Id;

        protected LightProxy(int id)
        {
            Id = id;
            Position = new Vec3Edit(
                () => { var i = Get(); return (i.px, i.py, i.pz); },
                (x, y, z) => { var i = Get(); i.px = x; i.py = y; i.pz = z; Set(i); });
        }

        protected GhostLightInfo Get() { var i = new GhostLightInfo(); GhostEngine.GhostEngine_GetLight(Id, ref i); return i; }
        protected void Set(GhostLightInfo i) => GhostEngine.GhostEngine_SetLight(ref i);

        [Category("Details")] public string Name => GhostNames.Of(Id);
        [Category("Details")] public string Type => Get().type switch { 0 => "Directional", 1 => "Point", 3 => "Spot", _ => "Rect Area" };

        [Category("Transform")] public Vec3Edit Position { get; }

        [Category("Light"), DisplayName("Color")]
        public Color LightColor
        {
            get { var i = Get(); return GhostColorUtil.ToColor(i.r, i.g, i.b); }
            set { var i = Get(); i.r = value.R / 255f; i.g = value.G / 255f; i.b = value.B / 255f; Set(i); }
        }

        [Category("Light")]
        public float Intensity { get => Get().intensity; set { var i = Get(); i.intensity = value; Set(i); } }
        [Category("Light"), DisplayName("Cast Shadows")]
        public bool CastShadows
        {
            get => GhostEngine.GhostEngine_GetLightShadows(Id) > 0;
            set => GhostEngine.GhostEngine_SetLightShadows(Id, value ? 1 : 0);
        }

        [Category("Volumetric Lighting"), DisplayName("God Rays Strength (0-5, needs Scene Settings)")]
        public float Volumetric { get => Get().volumetric; set { var i = Get(); i.volumetric = value; Set(i); } }
    }

    internal class DirectionalLightProxy : LightProxy
    {
        public DirectionalLightProxy(int id) : base(id) { }

        [Category("Light"), DisplayName("Yaw (deg)")]
        public float Yaw { get => Get().yawDeg; set { var i = Get(); i.yawDeg = value; Set(i); } }

        [Category("Light"), DisplayName("Pitch (deg)")]
        public float Pitch { get => Get().pitchDeg; set { var i = Get(); i.pitchDeg = value; Set(i); } }

        [Category("Rotation"), DisplayName("Spin Yaw (deg/sec, Play)")]
        public float SpinYaw { get => Get().spinYaw; set { var i = Get(); i.spinYaw = value; Set(i); } }

        [Category("Rotation"), DisplayName("Spin Pitch (deg/sec, Play)")]
        public float SpinPitch { get => Get().spinPitch; set { var i = Get(); i.spinPitch = value; Set(i); } }
    }

    internal class PointLightProxy : LightProxy
    {
        public PointLightProxy(int id) : base(id)
        {
            Flicker1 = new FlickerEdit(id, 0);
            Flicker2 = new FlickerEdit(id, 1);
        }

        [Category("Light")]
        public float Range { get => Get().range; set { var i = Get(); i.range = value; Set(i); } }

        [Category("Flicker"), DisplayName("Flicker Layer 1 (modulates main light)")]
        public FlickerEdit Flicker1 { get; }

        [Category("Flicker"), DisplayName("Flicker Layer 2 (extra light layer)")]
        public FlickerEdit Flicker2 { get; }
    }

    // Rect light = facing (yaw/pitch) + range + size
    internal class RectLightProxy : DirectionalLightProxy
    {
        public RectLightProxy(int id) : base(id) { }

        [Category("Light")]
        public float Range { get => Get().range; set { var i = Get(); i.range = value; Set(i); } }

        [Category("Light")]
        public float Width { get => Get().width; set { var i = Get(); i.width = value; Set(i); } }

        [Category("Light")]
        public float Height { get => Get().height; set { var i = Get(); i.height = value; Set(i); } }
    }

    internal class SpotLightProxy : DirectionalLightProxy
    {
        public SpotLightProxy(int id) : base(id) { }

        [Category("Light")]
        public float Range { get => Get().range; set { var i = Get(); i.range = value; Set(i); } }

        [Category("Spot"), DisplayName("Inner Cone (deg)")]
        public float InnerCone { get => Get().innerDeg; set { var i = Get(); i.innerDeg = value; Set(i); } }

        [Category("Spot"), DisplayName("Outer Cone (deg)")]
        public float OuterCone { get => Get().outerDeg; set { var i = Get(); i.outerDeg = value; Set(i); } }

        [Category("Spot Shadows"), DisplayName("Objects Cast Shadows")]
        public bool ShadowObjects
        {
            get => Get().shadowObjects != 0;
            set { var i = Get(); i.shadowObjects = value ? 1 : 0; Set(i); }
        }

        [Category("Spot Shadows"), DisplayName("Landscape Casts Shadows")]
        public bool ShadowLandscape
        {
            get => Get().shadowLandscape != 0;
            set { var i = Get(); i.shadowLandscape = value ? 1 : 0; Set(i); }
        }
    }

    internal static class GhostSceneAudio
    {
        public static string Text(byte[]? b)
        {
            if (b == null) return "";
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }
        public static string Get(int which)
        {
            var b = new byte[64];
            GhostEngine.GhostEngine_GetSceneAudio(which, b, b.Length);
            string t = Text(b);
            return t.Length == 0 ? "None" : t;
        }
        public static void Set(int which, string v) => GhostEngine.GhostEngine_SetSceneAudio(which, v == "None" ? "" : v);
    }

    internal abstract class SoundChoiceConverter : StringConverter
    {
        protected abstract int Kind { get; }
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { "None" };
            int n = GhostEngine.GhostEngine_GetSoundCount();
            for (int i = 0; i < n; i++)
                if (GhostEngine.GhostEngine_GetSoundInfo(i, out GhostSoundInfo s) != 0 && s.kind == Kind)
                    list.Add(GhostSceneAudio.Text(s.name));
            return new StandardValuesCollection(list);
        }
    }
    internal sealed class MusicChoiceConverter : SoundChoiceConverter { protected override int Kind => 1; }
    internal sealed class AmbienceChoiceConverter : SoundChoiceConverter { protected override int Kind => 2; }

    internal class AmbientProxy
    {
        [Category("Ambient"), DisplayName("Color")]
        public Color AmbientColor
        {
            get { GhostEngine.GhostEngine_GetAmbient(out float r, out float g, out float b, out _); return GhostColorUtil.ToColor(r, g, b); }
            set
            {
                GhostEngine.GhostEngine_GetAmbient(out _, out _, out _, out float k);
                GhostEngine.GhostEngine_SetAmbient(value.R / 255f, value.G / 255f, value.B / 255f, k);
            }
        }

        [Category("Ambient")]
        public float Intensity
        {
            get { GhostEngine.GhostEngine_GetAmbient(out _, out _, out _, out float k); return k; }
            set
            {
                GhostEngine.GhostEngine_GetAmbient(out float r, out float g, out float b, out _);
                GhostEngine.GhostEngine_SetAmbient(r, g, b, value);
            }
        }

        [Category("Game"), DisplayName("Game Start Camera")]
        [TypeConverter(typeof(StartCameraConverter))]
        public string GameStartCamera
        {
            get
            {
                int id = GhostEngine.GhostEngine_GetGameStartCamera();
                string n = id == 0 ? "" : GhostNames.Of(id);
                return n.Length > 0 ? n : StartCameraConverter.CurrentLabel;
            }
            set
            {
                int id = 0;
                foreach (var (cid, name) in GhostEntityList.Of(2)) if (name == value) id = cid;
                GhostEngine.GhostEngine_SetGameStartCamera(id);
            }
        }
        [Category("Shadows"), DisplayName("Shadows Enabled")]
        public bool ShadowsEnabled
        {
            get { GhostEngine.GhostEngine_GetShadowSettings(out int e, out _); return e != 0; }
            set { GhostEngine.GhostEngine_GetShadowSettings(out _, out float s); GhostEngine.GhostEngine_SetShadowSettings(value ? 1 : 0, s); }
        }

        [Category("Shadows"), DisplayName("Shadow Strength")]
        public float ShadowStrength
        {
            get { GhostEngine.GhostEngine_GetShadowSettings(out _, out float s); return s; }
            set { GhostEngine.GhostEngine_GetShadowSettings(out int e, out _); GhostEngine.GhostEngine_SetShadowSettings(e, value); }
        }

        [Category("Shadows"), DisplayName("Shadow Resolution")]
        [TypeConverter(typeof(ShadowResConverter))]
        public string ShadowResolution
        {
            get
            {
                int i = Array.IndexOf(ShadowResConverter.Sizes, GhostEngine.GhostEngine_GetShadowResolution());
                return ShadowResConverter.Names[i < 0 ? 1 : i];
            }
            set
            {
                int i = Array.IndexOf(ShadowResConverter.Names, value);
                if (i >= 0) GhostEngine.GhostEngine_SetShadowResolution(ShadowResConverter.Sizes[i]);
            }
        }
        private static GhostShadowTuning GetTune() { GhostEngine.GhostEngine_GetShadowTuning(out var t); return t; }
        private static void SetTune(GhostShadowTuning t) => GhostEngine.GhostEngine_SetShadowTuning(ref t);

        [Category("Shadows"), DisplayName("Shadow Filter")]
        [TypeConverter(typeof(ShadowFilterConverter))]
        public string ShadowFilter
        {
            get => ShadowFilterConverter.Names[Math.Clamp(GetTune().filter, 0, 3)];
            set { int i = Array.IndexOf(ShadowFilterConverter.Names, value); if (i >= 0) { var t = GetTune(); t.filter = i; SetTune(t); } }
        }

        [Category("Shadows"), DisplayName("Shadow Softness (texels, 0.25-4)")]
        public float ShadowSoftness { get => GetTune().softness; set { var t = GetTune(); t.softness = value; SetTune(t); } }

        [Category("Shadows"), DisplayName("Depth Bias (x, 0-8)")]
        public float ShadowDepthBias { get => GetTune().bias; set { var t = GetTune(); t.bias = value; SetTune(t); } }

        [Category("Shadows"), DisplayName("Normal Bias (x, 0-8)")]
        public float ShadowNormalBias { get => GetTune().normalBias; set { var t = GetTune(); t.normalBias = value; SetTune(t); } }

        [Category("Shadows"), DisplayName("Shadow Distance (m, 0 = whole scene)")]
        public float ShadowDistance { get => GetTune().distance; set { var t = GetTune(); t.distance = value; SetTune(t); } }

        private static GhostPostInfo GetPost() { GhostEngine.GhostEngine_GetPostSettings(out var p); return p; }
        private static void SetPost(GhostPostInfo p) => GhostEngine.GhostEngine_SetPostSettings(ref p);

        [Category("Post Processing"), DisplayName("Tone Mapping")]
        [TypeConverter(typeof(ToneMapConverter))]
        public string ToneMapping
        {
            get => ToneMapConverter.Names[Math.Clamp(GetPost().toneMode, 0, ToneMapConverter.Names.Length - 1)];
            set { int i = Array.IndexOf(ToneMapConverter.Names, value); if (i >= 0) { var p = GetPost(); p.toneMode = i; SetPost(p); } }
        }

        [Category("Post Processing"), DisplayName("Exposure (EV stops, -5 to 5)")]
        public float Exposure { get => GetPost().exposureEV; set { var p = GetPost(); p.exposureEV = value; SetPost(p); } }

        [Category("Post Processing"), DisplayName("White Point (Reinhard, 1-16)")]
        public float WhitePoint { get => GetPost().whitePoint; set { var p = GetPost(); p.whitePoint = value; SetPost(p); } }

        [Category("Bloom"), DisplayName("Bloom Enabled")]
        public bool BloomEnabled { get => GetPost().bloomEnabled != 0; set { var p = GetPost(); p.bloomEnabled = value ? 1 : 0; SetPost(p); } }

        [Category("Bloom"), DisplayName("Threshold (linear, after exposure)")]
        public float BloomThreshold { get => GetPost().bloomThreshold; set { var p = GetPost(); p.bloomThreshold = value; SetPost(p); } }

        [Category("Bloom"), DisplayName("Soft Knee (0-1)")]
        public float BloomKnee { get => GetPost().bloomKnee; set { var p = GetPost(); p.bloomKnee = value; SetPost(p); } }

        [Category("Bloom"), DisplayName("Intensity (0-10)")]
        public float BloomIntensity { get => GetPost().bloomIntensity; set { var p = GetPost(); p.bloomIntensity = value; SetPost(p); } }

        [Category("Bloom"), DisplayName("Scatter (0.1 tight - 0.95 wide)")]
        public float BloomScatter { get => GetPost().bloomScatter; set { var p = GetPost(); p.bloomScatter = value; SetPost(p); } }

        [Category("Bloom"), DisplayName("Size (blur levels, 2-6)")]
        public int BloomMips { get => GetPost().bloomMips; set { var p = GetPost(); p.bloomMips = value; SetPost(p); } }

        [Category("Bloom"), DisplayName("Tint")]
        public Color BloomTint
        {
            get { var p = GetPost(); return GhostColorUtil.ToColor(p.bloomTintR, p.bloomTintG, p.bloomTintB); }
            set { var p = GetPost(); p.bloomTintR = value.R / 255f; p.bloomTintG = value.G / 255f; p.bloomTintB = value.B / 255f; SetPost(p); }
        }
        [Category("Ambient Occlusion (SSAO)"), DisplayName("SSAO Enabled")]
        public bool SsaoEnabled { get => GetPost().ssaoEnabled != 0; set { var p = GetPost(); p.ssaoEnabled = value ? 1 : 0; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Radius (world units, 0.05-5)")]
        public float SsaoRadius { get => GetPost().ssaoRadius; set { var p = GetPost(); p.ssaoRadius = value; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Intensity (contrast, 0-4)")]
        public float SsaoIntensity { get => GetPost().ssaoIntensity; set { var p = GetPost(); p.ssaoIntensity = value; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Strength (0-1)")]
        public float SsaoStrength { get => GetPost().ssaoStrength; set { var p = GetPost(); p.ssaoStrength = value; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Direct Light Fade (0-1, lit areas get less AO)")]
        public float SsaoDirectFade { get => GetPost().ssaoDirectFade; set { var p = GetPost(); p.ssaoDirectFade = value; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Bias (world units)")]
        public float SsaoBias { get => GetPost().ssaoBias; set { var p = GetPost(); p.ssaoBias = value; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Samples (4-32)")]
        public int SsaoSamples { get => GetPost().ssaoSamples; set { var p = GetPost(); p.ssaoSamples = value; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Fade Distance (0 = no fade)")]
        public float SsaoFadeDist { get => GetPost().ssaoFadeDist; set { var p = GetPost(); p.ssaoFadeDist = value; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Half Resolution (faster)")]
        public bool SsaoHalfRes { get => GetPost().ssaoHalfRes != 0; set { var p = GetPost(); p.ssaoHalfRes = value ? 1 : 0; SetPost(p); } }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Show AO Only (debug)")]
        public bool SsaoShowOnly { get => GetPost().ssaoShowOnly != 0; set { var p = GetPost(); p.ssaoShowOnly = value ? 1 : 0; SetPost(p); } }

        [Category("Reflections (SSR)"), DisplayName("SSR Enabled")]
        public bool SsrEnabled { get => GetPost().ssrEnabled != 0; set { var p = GetPost(); p.ssrEnabled = value ? 1 : 0; SetPost(p); } }

        [Category("Reflections (SSR)"), DisplayName("Intensity (0-2)")]
        public float SsrIntensity { get => GetPost().ssrIntensity; set { var p = GetPost(); p.ssrIntensity = value; SetPost(p); } }

        [Category("Reflections (SSR)"), DisplayName("Max Distance (world units)")]
        public float SsrMaxDistance { get => GetPost().ssrMaxDistance; set { var p = GetPost(); p.ssrMaxDistance = value; SetPost(p); } }

        [Category("Reflections (SSR)"), DisplayName("Thickness (world units)")]
        public float SsrThickness { get => GetPost().ssrThickness; set { var p = GetPost(); p.ssrThickness = value; SetPost(p); } }

        [Category("Reflections (SSR)"), DisplayName("Quality (steps, 8-96)")]
        public int SsrSteps { get => GetPost().ssrSteps; set { var p = GetPost(); p.ssrSteps = value; SetPost(p); } }

        [Category("Reflections (SSR)"), DisplayName("Max Roughness (rougher surfaces use probes)")]
        public float SsrMaxRoughness { get => GetPost().ssrMaxRoughness; set { var p = GetPost(); p.ssrMaxRoughness = value; SetPost(p); } }

        [Category("Reflections (SSR)"), DisplayName("Edge Fade (0-0.5)")]
        public float SsrEdgeFade { get => GetPost().ssrEdgeFade; set { var p = GetPost(); p.ssrEdgeFade = value; SetPost(p); } }

        [Category("Global Illumination (SSGI)"), DisplayName("SSGI Enabled")]
        public bool SsgiEnabled { get => GetPost().ssgiEnabled != 0; set { var p = GetPost(); p.ssgiEnabled = value ? 1 : 0; SetPost(p); } }

        [Category("Global Illumination (SSGI)"), DisplayName("Intensity (0-5)")]
        public float SsgiIntensity { get => GetPost().ssgiIntensity; set { var p = GetPost(); p.ssgiIntensity = value; SetPost(p); } }

        [Category("Global Illumination (SSGI)"), DisplayName("Radius (world units)")]
        public float SsgiRadius { get => GetPost().ssgiRadius; set { var p = GetPost(); p.ssgiRadius = value; SetPost(p); } }

        [Category("Global Illumination (SSGI)"), DisplayName("Quality (samples, 4-32)")]
        public int SsgiSamples { get => GetPost().ssgiSamples; set { var p = GetPost(); p.ssgiSamples = value; SetPost(p); } }

        [Category("Global Illumination (SSGI)"), DisplayName("Temporal Smoothing (0-0.97)")]
        public float SsgiTemporal { get => GetPost().ssgiTemporal; set { var p = GetPost(); p.ssgiTemporal = value; SetPost(p); } }

        [Category("Global Illumination (SSGI)"), DisplayName("Fade Distance (0 = no limit)")]
        public float SsgiFadeDist { get => GetPost().ssgiFadeDist; set { var p = GetPost(); p.ssgiFadeDist = value; SetPost(p); } }

        [Category("Global Illumination (SSGI)"), DisplayName("Show GI Only (debug)")]
        public bool SsgiShowOnly { get => GetPost().ssgiShowOnly != 0; set { var p = GetPost(); p.ssgiShowOnly = value ? 1 : 0; SetPost(p); } }

        private static GhostFogInfo GetFog() { GhostEngine.GhostEngine_GetFogSettings(out var f); return f; }
        private static void SetFog(GhostFogInfo f) => GhostEngine.GhostEngine_SetFogSettings(ref f);

        [Category("Fog"), DisplayName("Fog Enabled")]
        public bool FogEnabled { get => GetFog().enabled != 0; set { var f = GetFog(); f.enabled = value ? 1 : 0; SetFog(f); } }

        [Category("Fog"), DisplayName("Density (0-1, try 0.02)")]
        public float FogDensity { get => GetFog().density; set { var f = GetFog(); f.density = value; SetFog(f); } }

        [Category("Fog"), DisplayName("Color")]
        public Color FogColor
        {
            get { var f = GetFog(); return GhostColorUtil.ToColor(f.colorR, f.colorG, f.colorB); }
            set { var f = GetFog(); f.colorR = value.R / 255f; f.colorG = value.G / 255f; f.colorB = value.B / 255f; SetFog(f); }
        }

        [Category("Fog"), DisplayName("Brightness (self-lit glow)")]
        public float FogBrightness { get => GetFog().brightness; set { var f = GetFog(); f.brightness = value; SetFog(f); } }

        [Category("Fog"), DisplayName("Follow Ambient (0-1, dims at night with the Sky Sphere)")]
        public float FogFollowAmbient { get => GetFog().followAmbient; set { var f = GetFog(); f.followAmbient = value; SetFog(f); } }

        [Category("Fog"), DisplayName("Height Base (world Y, densest here)")]
        public float FogHeightBase { get => GetFog().heightBase; set { var f = GetFog(); f.heightBase = value; SetFog(f); } }

        [Category("Fog"), DisplayName("Height Falloff (0 = uniform)")]
        public float FogHeightFalloff { get => GetFog().heightFalloff; set { var f = GetFog(); f.heightFalloff = value; SetFog(f); } }

        [Category("Fog"), DisplayName("Start Distance")]
        public float FogStart { get => GetFog().startDistance; set { var f = GetFog(); f.startDistance = value; SetFog(f); } }

        [Category("Fog"), DisplayName("Max Distance")]
        public float FogMaxDistance { get => GetFog().maxDistance; set { var f = GetFog(); f.maxDistance = value; SetFog(f); } }

        [Category("Fog"), DisplayName("Sky Fog (0-1)")]
        public float FogSky { get => GetFog().skyFog; set { var f = GetFog(); f.skyFog = value; SetFog(f); } }

        [Category("Fog Environment"), DisplayName("Color Source")]
        [TypeConverter(typeof(FogEnvConverter))]
        public string FogEnvSource
        {
            get => FogEnvConverter.Names[Math.Clamp(GetFog().envMode, 0, 2)];
            set { int i = Array.IndexOf(FogEnvConverter.Names, value); if (i >= 0) { var f = GetFog(); f.envMode = i; SetFog(f); } }
        }

        [Category("Fog Environment"), DisplayName("Source Amount (0-1)")]
        public float FogEnvAmount { get => GetFog().envAmount; set { var f = GetFog(); f.envAmount = value; SetFog(f); } }

        [Category("Fog Environment"), DisplayName("Texture (Source = Texture)")]
        [TypeConverter(typeof(TextureChoiceConverter))]
        public string FogEnvTexture
        {
            get => GhostTextures.DisplayName(GetFog().envTexture);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                var f = GetFog(); f.envTexture = tex; SetFog(f);
            }
        }

        [Category("Fog Environment"), DisplayName("Texture Rotation (deg)")]
        public float FogEnvYaw { get => GetFog().envYaw; set { var f = GetFog(); f.envYaw = value; SetFog(f); } }

        [Category("Fog Noise"), DisplayName("Noise Texture")]
        [TypeConverter(typeof(TextureChoiceConverter))]
        public string FogNoiseTexture
        {
            get => GhostTextures.DisplayName(GetFog().noiseTexture);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                var f = GetFog(); f.noiseTexture = tex; SetFog(f);
            }
        }

        [Category("Fog Noise"), DisplayName("Noise Amount (0-1)")]
        public float FogNoiseAmount { get => GetFog().noiseAmount; set { var f = GetFog(); f.noiseAmount = value; SetFog(f); } }

        [Category("Fog Noise"), DisplayName("Noise Scale")]
        public float FogNoiseScale { get => GetFog().noiseScale; set { var f = GetFog(); f.noiseScale = value; SetFog(f); } }

        [Category("Fog Noise"), DisplayName("Wind X (units/s)")]
        public float FogScrollX { get => GetFog().scrollX; set { var f = GetFog(); f.scrollX = value; SetFog(f); } }

        [Category("Fog Noise"), DisplayName("Wind Z (units/s)")]
        public float FogScrollZ { get => GetFog().scrollZ; set { var f = GetFog(); f.scrollZ = value; SetFog(f); } }

        [Category("Volumetric Lighting"), DisplayName("God Rays Enabled")]
        public bool VolLight { get => GetFog().volLight != 0; set { var f = GetFog(); f.volLight = value ? 1 : 0; SetFog(f); } }

        [Category("Volumetric Lighting"), DisplayName("Intensity (0-10)")]
        public float VolIntensity { get => GetFog().volIntensity; set { var f = GetFog(); f.volIntensity = value; SetFog(f); } }

        [Category("Volumetric Lighting"), DisplayName("Anisotropy (-0.9 to 0.95, forward = rays toward the light)")]
        public float VolAnisotropy { get => GetFog().anisotropy; set { var f = GetFog(); f.anisotropy = value; SetFog(f); } }

        [Category("Volumetric Lighting"), DisplayName("Quality (steps, 8-64)")]
        public int VolSteps { get => GetFog().steps; set { var f = GetFog(); f.steps = value; SetFog(f); } }


        [Category("Audio"), DisplayName("Scene Music (plays during Play)")]
        [TypeConverter(typeof(MusicChoiceConverter))]
        public string SceneMusic { get => GhostSceneAudio.Get(0); set => GhostSceneAudio.Set(0, value); }

        [Category("Audio"), DisplayName("Scene Ambience (plays during Play)")]
        [TypeConverter(typeof(AmbienceChoiceConverter))]
        public string SceneAmbience { get => GhostSceneAudio.Get(1); set => GhostSceneAudio.Set(1, value); }
    }

    // ============================================================
    //  The editor UI
    // ============================================================
    internal class GhostEditorUI
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
        private static readonly Color Accent = Color.FromArgb(0xFF, 0xC8, 0x6E);
        private static readonly Color CamColor = Color.FromArgb(0x7F, 0xD4, 0xFF);
        private static readonly Color ModColor = Color.FromArgb(0xC8, 0xA0, 0xFF);
        private static readonly Color VolColor = Color.FromArgb(0x7F, 0xE0, 0xB0);
        private static readonly Color FxColor = Color.FromArgb(0xFF, 0x9A, 0x4A);
        private sealed class PaletteItem
        {
            public string Text = "";
            public string Kind = "obj";      // "light", "obj" or "cam"
            public int Type;
            public override string ToString() => Text;
        }

        private readonly Form _form;
        private readonly Panel _viewport;

        private Panel _left = null!, _bottom = null!;

        // ---- Docking ----
        private const DockAreas ToolAreas = DockAreas.DockLeft | DockAreas.DockRight |
                                            DockAreas.DockTop | DockAreas.DockBottom | DockAreas.Float;
        private DockPanel _dock = null!;
        private PanelDock _dockViewport = null!, _dockPalette = null!, _dockHierarchy = null!,
                          _dockDetails = null!, _dockContent = null!;
        private readonly List<PanelDock> _tools = new();
        private readonly List<PanelDock> _hiddenByToggle = new();
        private static readonly string LayoutPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "GhostX", "layout.xml");
        private ComboBox _category = null!;
        private ListBox _palette = null!;
        private TreeView _tree = null!;
        private PropertyGrid _grid = null!;
        private TreeNode _settingsNode = null!;
        private readonly Dictionary<int, TreeNode> _nodes = new();

        private GhostUIDesigner _designer = null!;
        private PanelDock _dockDesigner = null!;
        private GhostAnimEditor _animEditor = null!;
        private PanelDock _dockAnim = null!;
        private GhostAudioMixer _mixer = null!;
        private PanelDock _dockMixer = null!;
        private GhostParticleEditor _particles = null!;
        private PanelDock _dockParticles = null!;

        private bool _visible = true;
        private bool _syncing;
        private uint _lastVersion = uint.MaxValue;
        private int _lastSel = int.MinValue;
        private object? _lastSnap;

        private int _dragIndex = -1;
        private ContextMenuStrip _ctx = null!;
        private DateTime _rmbDown;
        private Point _dragStart;
        private GhostViewportManager _vp = null!;
        public GhostEditorUI(Form form, Panel viewport) { _form = form; _viewport = viewport; }

        // -------------------------------------------------------
        public void Build()
        {

            // ---- Bottom: content browser ----
            var browser = new GhostContentBrowser { Dock = DockStyle.Fill };
            browser.ApplyRequested += ApplyTextureToSelection;
            browser.OpenSceneRequested += path =>
            {
                if (GhostSceneFile.ConfirmDiscard(_form)) GhostSceneFile.Open(path);
            };
            _bottom = browser;
            browser.OpenUIRequested += path => OpenDesigner(path);
            browser.OpenAnimRequested += path => OpenAnimEditor(path);
            browser.AddModelRequested += id =>
            {
                if (GhostEngine.GhostEngine_AddObject(id, -1, -1) == 0) System.Media.SystemSounds.Beep.Play();
                Sync();
            };
            browser.ModelFilesDropped += files => GhostModels.ImportFiles(_form, files, false);
            GhostModels.Changed += RefreshPalette;
            GhostProject.Changed += RefreshPalette;
            // ---- Left: palette ----
            _left = new Panel { Dock = DockStyle.Fill, BackColor = Bg };
            _palette = new ListBox
            {
                Dock = DockStyle.Fill,
                BackColor = Bg2,
                ForeColor = Fg,
                BorderStyle = BorderStyle.None,
                IntegralHeight = false,
                Font = new Font("Segoe UI", 10.5f),
            };
            var hint = new Label
            {
                Dock = DockStyle.Bottom,
                Height = 40,
                ForeColor = Color.Gray,
                TextAlign = ContentAlignment.MiddleCenter,
                Text = "Drag into the viewport\n(or double-click to add)",
            };
            _category = new ComboBox
            {
                Dock = DockStyle.Top,
                DropDownStyle = ComboBoxStyle.DropDownList,
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg,
            };
            _category.Items.AddRange(new object[] { "Lighting", "Primitives", "Sprites", "Cameras", "Mods", "Models", "Maps","Utilities", "Particle Systems" });
            _category.SelectedIndexChanged += (s, e) => FillPalette();
            // add order matters for docking: Fill first, then Bottom, then Top
            _left.Controls.Add(_palette);
            _left.Controls.Add(hint);
            _left.Controls.Add(_category);
            _category.SelectedIndex = 0;

            _palette.MouseDown += (s, e) => { _dragIndex = _palette.IndexFromPoint(e.Location); _dragStart = e.Location; };
            _palette.MouseMove += OnPaletteMouseMove;
            _palette.DoubleClick += (s, e) =>
            {
                if (_palette.SelectedItem is PaletteItem it && it.Kind != "none") Spawn(it.Kind, it.Type, -1, -1);
            };

           
            _grid = new PropertyGrid
            {
                Dock = DockStyle.Fill,
                ToolbarVisible = false,
                HelpVisible = false,
                PropertySort = PropertySort.Categorized,
                BackColor = Bg,
                ViewBackColor = Bg2,
                ViewForeColor = Fg,
                LineColor = Color.FromArgb(0x3A, 0x3A, 0x40),
                CategoryForeColor = Accent,
                DisabledItemForeColor = Color.Gray,
                HelpBackColor = Bg,
                HelpForeColor = Fg,
                CommandsBackColor = Bg,
                CommandsForeColor = Fg,
            };
            // Re-read every row after an edit, so "Image Size" and "Color" follow a texture change
            _grid.PropertyValueChanged += (s, e) => _grid.Refresh();

           
            _tree = new TreeView
            {
                Dock = DockStyle.Fill,
                BackColor = Bg2,
                ForeColor = Fg,
                BorderStyle = BorderStyle.None,
                HideSelection = false,
                FullRowSelect = true,
                ShowLines = false,
                Font = new Font("Segoe UI", 10f),
            };
            _tree.AfterSelect += (s, e) =>
            {
                if (_syncing || e.Node == null) return;
                int id = (int)e.Node.Tag!;
                if (id < 0 && e.Node.Parent?.Tag is int parentId)          // a material slot row
                {
                    if (GhostEngine.GhostEngine_GetSelected() != parentId) GhostEngine.GhostEngine_SelectEntity(parentId);
                    GhostEngine.GhostEngine_SetTargetFace(-id - 1);
                    ShowDetails(parentId);
                    _lastSel = parentId;
                    _lastSnap = Snapshot(parentId);
                    return;
                }
                GhostEngine.GhostEngine_SelectEntity(id);
                ShowDetails(id);
                _lastSel = id;
                _lastSnap = Snapshot(id);
            };
            // NodeMouseDoubleClick:  if (e.Node?.Tag is int id && id > 0)
            // NodeMouseClick:        if (... || e.Node.Tag is not int id || id <= 0) return;
            // Double-click a camera in the hierarchy to make it the main view
            _tree.NodeMouseDoubleClick += (s, e) =>
            {
                if (e.Node?.Tag is int id && id > 0)
                {
                    var c = new GhostCameraInfo();
                    if (GhostEngine.GhostEngine_GetCamera(id, ref c) != 0)
                        GhostEngine.GhostEngine_SetActiveCamera(id);       // cameras: make it the main view
                    else { GhostEngine.GhostEngine_SelectEntity(id); DoFocus(); }   // everything else: jump to it
                }
            };
            _tree.NodeMouseClick += (s, e) =>
            {
                if (e.Button != MouseButtons.Right || e.Node == null || e.Node.Tag is not int id || id <= 0) return;
                _tree.SelectedNode = e.Node;
                _ctx.Show(_tree, e.Location);
            };

            BuildDocking();
            _ctx = BuildContextMenu();
            _viewport.MouseDown += (s, e) => { if (e.Button == MouseButtons.Right) _rmbDown = DateTime.UtcNow; };
            _viewport.MouseUp += (s, e) =>
            {
                if (e.Button != MouseButtons.Right || GhostEngine.GhostEngine_GetPlayState() != 0) return;
                if ((DateTime.UtcNow - _rmbDown).TotalMilliseconds > 220) return;     // held = camera look, tapped = menu
                int id = GhostEngine.GhostEngine_PickObjectAt(e.X, e.Y, out _);
                if (id != 0 && !SelectedIdsList().Contains(id)) GhostEngine.GhostEngine_SelectEntity(id);   // keep a group selected
                _ctx.Show(_viewport, e.Location);
            };

            // ---- Viewport: drop target + keyboard focus ----
            _viewport.AllowDrop = true;
            _viewport.DragEnter += OnViewportDrag;
            _viewport.DragOver += OnViewportDrag;
            _viewport.DragDrop += OnViewportDrop;
            _viewport.MouseDown += (s, e) => _viewport.Select();
            /*
            _viewport.MouseEnter += (s, e) =>
            {
                // Hovering the viewport takes keyboard focus, unless you're typing somewhere
                if (Form.ActiveForm != null && Form.ActiveForm == _viewport.TopLevelControl && !IsTyping())
                    _viewport.Select();
            };
            */
            // ---- Live sync (hierarchy, selection, values changed by viewport drags) ----
            var timer = new System.Windows.Forms.Timer { Interval = 100 };
            timer.Tick += (s, e) => Sync();
            timer.Start();
            _form.FormClosing += (s, e) => timer.Stop();
        }
        public int HotViewport() => _vp.HotViewport();
        // -------------------------------------------------------
        //  Visibility + keys
        // -------------------------------------------------------
        // Key 4: hide every tool window (remembering which were open), or bring them back
        public void ToggleVisible()
        {
            if (_hiddenByToggle.Count == 0)
            {
                foreach (PanelDock d in _tools)
                    if (d.DockPanel != null && !d.IsHidden) { d.IsHidden = true; _hiddenByToggle.Add(d); }
            }
            else
            {
                foreach (PanelDock d in _hiddenByToggle) d.IsHidden = false;
                _hiddenByToggle.Clear();
            }
            _viewport.Select();
        }

        // Returns true if the key was consumed
        // Returns true if the key was consumed
        public bool HandleKey(Keys key, bool repeat)
        {
            Keys mods = Control.ModifierKeys;
            bool ctrl = mods == Keys.Control, plain = mods == Keys.None;

            bool isFour = key == Keys.D4 || key == Keys.NumPad4, isDelete = key == Keys.Delete;
            bool ctrlD = ctrl && key == Keys.D, ctrlF = ctrl && key == Keys.F;
            bool undo = ctrl && key == Keys.Z;
            bool redo = (ctrl && key == Keys.Y) || (mods == (Keys.Control | Keys.Shift) && key == Keys.Z);
            bool copy = ctrl && key == Keys.C, paste = ctrl && key == Keys.V;
            bool faceKey = plain && key == Keys.F, gridKey = plain && key == Keys.G;
            bool colKey = plain && key == Keys.H;
            bool paintKey = plain && key == Keys.P;
            bool brushDown = plain && key == Keys.OemOpenBrackets, brushUp = plain && key == Keys.OemCloseBrackets;
            bool focusKey = plain && key == Keys.Z, renameKey = plain && key == Keys.F2;
            bool fullscreenKey = plain && key == Keys.F11;

            if (!(isFour || isDelete || ctrlD || ctrlF || undo || redo || copy || paste || faceKey || gridKey ||
                  colKey || focusKey || renameKey || paintKey || brushDown || brushUp || fullscreenKey))
                return false;

            // Never act while a modal dialog (Rename prompt, color picker, Array dialog...) is open
            if (Form.ActiveForm is { Modal: true }) return false;

            if (fullscreenKey) return ToggleFullscreenForFocus();

            Control? focus = Control.FromHandle(GhostShell.Win32.GetFocus());
            if (focus == null) return false;          // a native dialog (file / color picker) has focus
            if (IsInsideDesigner(focus)) return false;
            if (IsTypingIn(focus)) return false;      // never steal keys from a text box or the property grid

            bool inViewport = IsInViewport(focus);
            bool sceneFocus = inViewport || IsInTree(focus);

            // Single-letter viewport tools: only when the viewport itself is the active control
            bool viewportOnly = faceKey || gridKey || colKey || paintKey || brushDown || brushUp || focusKey;
            // Scene edits: viewport or hierarchy only (not the content browser, palette, etc.)
            bool sceneKey = isDelete || ctrlD || ctrlF || copy || paste || renameKey;

            if (viewportOnly && !inViewport) return false;
            if (sceneKey && !sceneFocus) return false;
            if ((viewportOnly || renameKey) && GhostEngine.GhostEngine_GetPlayState() != 0)
                return false;                                 // while playing these keys belong to the game

            if (brushDown || brushUp)                         // [ and ]: brush size while paint mode is on (hold to repeat)
            {
                if (GhostEngine.GhostEngine_GetPaintMode() == 0) return false;
                GhostEngine.GhostEngine_GetPaintSettings(out GhostPaintInfo bp);
                bp.radius *= brushUp ? 1.15f : 0.87f;
                GhostEngine.GhostEngine_SetPaintSettings(ref bp);
                return true;
            }
            if (repeat) return true;

            int sel = GhostEngine.GhostEngine_GetSelected();
            if (undo) { DoUndo(); return true; }
            if (redo) { DoRedo(); return true; }
            if (paste) { DoPaste(); return true; }
            if (gridKey) { ToggleGrid(); return true; }
            if (faceKey) { GhostEngine.GhostEngine_SetFaceMode(GhostEngine.GhostEngine_GetFaceMode() == 0 ? 1 : 0); return true; }
            if (colKey) { GhostEngine.GhostEngine_SetColliderMode(GhostEngine.GhostEngine_GetColliderMode() == 0 ? 1 : 0); return true; }
            if (paintKey) { GhostEngine.GhostEngine_SetPaintMode(GhostEngine.GhostEngine_GetPaintMode() == 0 ? 1 : 0); return true; }
            if (isFour) { ToggleVisible(); return true; }
            if (sel == 0) return false;
            if (isDelete) { DoDelete(); return true; }
            if (ctrlD) { DoDuplicate(); return true; }
            if (ctrlF) { _form.BeginInvoke(new Action(() => ArrayDuplicate(sel))); return true; }
            if (copy) { DoCopy(); return true; }
            if (focusKey) { DoFocus(); return true; }
            if (renameKey) { _form.BeginInvoke(new Action(RenameSelected)); return true; }
            return false;
        }

        private bool IsInViewport(Control? c)
        {
            for (; c != null; c = c.Parent) if (c == _viewport) return true;
            return false;
        }
        private bool IsInTree(Control? c)
        {
            for (; c != null; c = c.Parent) if (c == _tree) return true;
            return false;
        }

        // For GhostForm: true only when the main viewport is clicked/active and nothing is being typed into
        public bool ViewportActive()
        {
            Control? f = Control.FromHandle(GhostShell.Win32.GetFocus());
            return f != null && IsInViewport(f) && !IsTypingIn(f);
        }

        // -------------------------------------------------------
        //  Windowed fullscreen for editor windows (F11)
        // -------------------------------------------------------
        private sealed class FullscreenHost : Form
        {
            private readonly Action _exit;
            private readonly Action? _save;
            public bool Leaving;

            public FullscreenHost(Control content, string title, Rectangle bounds, Action exit, Action? save)
            {
                _exit = exit; _save = save;
                Text = title;
                FormBorderStyle = FormBorderStyle.None;
                StartPosition = FormStartPosition.Manual;
                Bounds = bounds;
                ShowInTaskbar = false;
                BackColor = Color.FromArgb(0x25, 0x25, 0x28);

                content.Dock = DockStyle.Fill;
                Controls.Add(content);                                   // Fill first, then the strip on top

                var strip = new Panel { Dock = DockStyle.Top, Height = 26, BackColor = Color.FromArgb(0x1E, 0x1E, 0x1E) };
                var lbl = new Label
                {
                    Dock = DockStyle.Fill,
                    Text = title + "   -   F11 to exit fullscreen",
                    ForeColor = Color.Gray,
                    TextAlign = ContentAlignment.MiddleLeft,
                    Padding = new Padding(8, 0, 0, 0),
                };
                var btn = new Button
                {
                    Dock = DockStyle.Right,
                    Width = 120,
                    Text = "Exit Fullscreen",
                    FlatStyle = FlatStyle.Flat,
                    ForeColor = Color.FromArgb(0xF2, 0xF2, 0xF2),
                    BackColor = Color.FromArgb(0x25, 0x25, 0x28),
                };
                btn.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
                btn.Click += (s, e) => _exit();
                strip.Controls.Add(lbl);
                strip.Controls.Add(btn);
                Controls.Add(strip);
            }

            protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
            {
                if (keyData == Keys.F11) { _exit(); return true; }
                // the menu's Ctrl+S belongs to the main form, so the editor saves itself here
                if (keyData == (Keys.Control | Keys.S) && _save != null) { _save(); return true; }
                return base.ProcessCmdKey(ref msg, keyData);
            }

            protected override void OnFormClosing(FormClosingEventArgs e)
            {
                if (!Leaving && e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; _exit(); return; }   // Alt+F4 leaves fullscreen
                base.OnFormClosing(e);
            }
        }

        private FullscreenHost? _fs;
        private Control? _fsContent, _fsParent;
        private int _fsIndex;
        private DockStyle _fsDockStyle;

        private void ToggleEditorFullscreen(Control content, string title, Action? save)
        {
            if (_fs != null) { ExitEditorFullscreen(); return; }
            Control? parent = content.Parent;
            if (parent == null) return;

            _fsContent = content; _fsParent = parent;
            _fsIndex = parent.Controls.GetChildIndex(content);
            _fsDockStyle = content.Dock;

            Rectangle bounds = Screen.FromControl(_form).Bounds;          // whole monitor, borderless
            parent.Controls.Remove(content);
            _fs = new FullscreenHost(content, title, bounds, ExitEditorFullscreen, save);
            _fs.Show(_form);
            content.Focus();
        }

        private void ExitEditorFullscreen()
        {
            if (_fs == null || _fsContent == null || _fsParent == null) return;
            FullscreenHost fs = _fs;
            Control content = _fsContent, parent = _fsParent;
            _fs = null; _fsContent = null; _fsParent = null;

            fs.Leaving = true;
            fs.Controls.Remove(content);
            content.Dock = _fsDockStyle;
            parent.Controls.Add(content);
            parent.Controls.SetChildIndex(content, _fsIndex);
            fs.Close();
            content.Focus();
        }

        // F11: whichever editor window has focus goes fullscreen (or leaves it)
        private bool ToggleFullscreenForFocus()
        {
            if (_fs != null) { ExitEditorFullscreen(); return true; }
            for (Control? c = Control.FromHandle(GhostShell.Win32.GetFocus()); c != null; c = c.Parent)
            {
                if (c == _designer) { ToggleEditorFullscreen(_designer, "UI Designer", () => _designer.Save()); return true; }
                if (c == _animEditor) { ToggleEditorFullscreen(_animEditor, "Animation Editor", () => _animEditor.Save()); return true; }
                if (c == _mixer) { ToggleEditorFullscreen(_mixer, "Audio Mixer", null); return true; }
            }
            return false;
        }


        private void DoUndo() { GhostEngine.GhostEngine_Undo(); Sync(); }
        private void DoRedo() { GhostEngine.GhostEngine_Redo(); Sync(); }
        private void DoFocus() => GhostEngine.GhostEngine_FocusSelected();
        private void DoCopy() { if (GhostEngine.GhostEngine_CopySelected() == 0) System.Media.SystemSounds.Beep.Play(); }
        private void DoPaste() { if (GhostEngine.GhostEngine_Paste() == 0) System.Media.SystemSounds.Beep.Play(); Sync(); }

        private void DoDelete()
        {
            foreach (int id in SelectedIdsList())
                if (GhostEngine.GhostEngine_RemoveEntity(id) == 0) System.Media.SystemSounds.Beep.Play();   // refused: the last camera stays
            Sync();
        }

        private void DoDuplicate()          // same engine call Ctrl+D always used, once per selected object
        {
            var made = new List<int>();
            foreach (int id in SelectedIdsList())
            {
                int n = GhostEngine.GhostEngine_DuplicateEntity(id);
                if (n == 0) System.Media.SystemSounds.Beep.Play(); else made.Add(n);
            }
            if (made.Count > 1) GhostEngine.GhostEngine_SetSelection(made.ToArray(), made.Count);
            Sync();
        }

        private void ToggleGrid()
        {
            GhostEngine.GhostEngine_GetViewSettings(out GhostViewSettings v);
            v.showGrid = v.showGrid != 0 ? 0 : 1;
            GhostEngine.GhostEngine_SetViewSettings(ref v);
        }

        private void RenameSelected()
        {
            int sel = GhostEngine.GhostEngine_GetSelected();
            if (sel == 0 || GhostEngine.GhostEngine_GetPlayState() != 0) return;
            string? name = GhostPrompt.Ask(_form, "Rename", "New name:", GhostNames.Of(sel));
            if (string.IsNullOrWhiteSpace(name)) return;
            GhostEngine.GhostEngine_SetEntityName(sel, name);
            Sync();
        }

        private ContextMenuStrip BuildContextMenu()
        {
            var m = new ContextMenuStrip { BackColor = Bg2, ForeColor = Fg, ShowImageMargin = false };
            m.Items.Add("Rename...    F2", null, (s, e) => RenameSelected());
            m.Items.Add("Jump camera here    Z", null, (s, e) => DoFocus());
            m.Items.Add(new ToolStripSeparator());
            m.Items.Add("Copy    Ctrl+C", null, (s, e) => DoCopy());
            m.Items.Add("Paste    Ctrl+V", null, (s, e) => DoPaste());
            m.Items.Add("Duplicate    Ctrl+D", null, (s, e) => DoDuplicate());
            m.Items.Add("Array Duplicate...    Ctrl+F", null, (s, e) => { int id = GhostEngine.GhostEngine_GetSelected(); if (id != 0) ArrayDuplicate(id); });
            m.Items.Add(new ToolStripSeparator());
            m.Items.Add("Delete    Del", null, (s, e) => DoDelete());
            return m;
        }

        private void BuildEditMenu()
        {
            ToolStrip? strip = _form.MainMenuStrip;
            if (strip == null) return;
            var menu = new ToolStripMenuItem("Edit");
            ToolStripMenuItem Add(string text, string keys, Action a)
            {
                var it = new ToolStripMenuItem(text) { ShortcutKeyDisplayString = keys };   // display only: the key handler does the work
                it.Click += (s, e) => a();
                menu.DropDownItems.Add(it);
                return it;
            }
            var undo = Add("Undo", "Ctrl+Z", DoUndo);
            var redo = Add("Redo", "Ctrl+Y", DoRedo);
            menu.DropDownItems.Add(new ToolStripSeparator());
            var copy = Add("Copy", "Ctrl+C", DoCopy);
            var paste = Add("Paste", "Ctrl+V", DoPaste);
            var dup = Add("Duplicate", "Ctrl+D", DoDuplicate);
            var arr = Add("Array Duplicate...", "Ctrl+F", () => { int id = GhostEngine.GhostEngine_GetSelected(); if (id != 0) ArrayDuplicate(id); });
            menu.DropDownItems.Add(new ToolStripSeparator());
            var ren = Add("Rename...", "F2", RenameSelected);
            var jump = Add("Jump Camera To Selection", "Z", DoFocus);
            var del = Add("Delete", "Del", DoDelete);

            menu.DropDownOpening += (s, e) =>
            {
                bool ed = GhostEngine.GhostEngine_GetPlayState() == 0;
                bool has = ed && GhostEngine.GhostEngine_GetSelected() != 0;
                undo.Enabled = ed && GhostEngine.GhostEngine_UndoCount() > 0;
                redo.Enabled = ed && GhostEngine.GhostEngine_RedoCount() > 0;
                paste.Enabled = ed;
                copy.Enabled = dup.Enabled = arr.Enabled = ren.Enabled = jump.Enabled = del.Enabled = has;
            };

            int at = 1;                                            // right after File
            for (int i = 0; i < strip.Items.Count; i++)
                if (strip.Items[i].Text == "File") { at = i + 1; break; }
            strip.Items.Insert(at, menu);
        }
        private void ArrayDuplicate(int sel)
        {
            var o = new GhostObjectInfo();
            if (GhostEngine.GhostEngine_GetPlayState() != 0 || GhostEngine.GhostEngine_GetObject(sel, ref o) == 0)
            {
                System.Media.SystemSounds.Beep.Play();            // playing, or not an object
                return;
            }

            using var dlg = new GhostArrayDialog(GhostNames.Of(sel));
            if (dlg.ShowDialog(_form) != DialogResult.OK) return;

            float[] offsets = dlg.BuildOffsets();
            int want = offsets.Length / 3;
            if (want == 0) return;

            var ids = new int[want];
            int made = GhostEngine.GhostEngine_DuplicateArray(sel, offsets, want, ids);
            if (made == 0) System.Media.SystemSounds.Beep.Play();
            else if (made < want)
                MessageBox.Show(_form, $"Created {made} of {want} copies. The scene object limit was reached.",
                    "Array Duplicate", MessageBoxButtons.OK, MessageBoxIcon.Information);
            Sync();
            _viewport.Select();
        }
        private static bool IsTypingIn(Control? c)
        {
            for (; c != null; c = c.Parent)
                if (c is PropertyGrid || c is TextBoxBase || c is UpDownBase) return true;
            return false;
        }

        internal static bool IsTyping() => IsTypingIn(Control.FromHandle(GhostShell.Win32.GetFocus()));

        // -------------------------------------------------------
        //  Docking
        // -------------------------------------------------------
        private void BuildDocking()
        {
            _dock = new DockPanel { Dock = DockStyle.Fill, BackColor = Bg2 };
            _dock.Theme = new VS2015DarkTheme();
            _vp = new GhostViewportManager(_dock, _viewport);
            // The viewport is the main document and can't be closed
            _dockViewport = new PanelDock("Viewport", "Viewport", _viewport, DockAreas.Document | DockAreas.Float, false);
            _dockPalette = new PanelDock("Palette", "Palette", _left, ToolAreas);
            _dockHierarchy = new PanelDock("Hierarchy", "Hierarchy", _tree, ToolAreas);
            _dockDetails = new PanelDock("Details", "Details", _grid, ToolAreas);
            _dockContent = new PanelDock("Content", "Content Browser", _bottom, ToolAreas);
            _tools.AddRange(new[] { _dockPalette, _dockHierarchy, _dockDetails, _dockContent });
            _designer = new GhostUIDesigner();
            _dockDesigner = new PanelDock("UIDesigner", "UI Designer", _designer, ToolAreas | DockAreas.Document);
            _animEditor = new GhostAnimEditor();
            _dockAnim = new PanelDock("AnimEditor", "Animation Editor", _animEditor, ToolAreas | DockAreas.Document);
            _mixer = new GhostAudioMixer();
            _dockMixer = new PanelDock("AudioMixer", "Audio Mixer", _mixer, ToolAreas | DockAreas.Document);
            _particles = new GhostParticleEditor();
            _dockParticles = new PanelDock("ParticleEditor", "Particle Editor", _particles, ToolAreas | DockAreas.Document);
            _form.Controls.Add(_dock);
            _dock.BringToFront();                    // the dock fills whatever the menu strip leaves
            _form.MainMenuStrip?.SendToBack();

            BuildWindowMenu();
            BuildEditMenu();
        }

        private IDockContent GetContentFromPersistString(string key) => key switch
        {
            "Viewport" => _dockViewport,
            "Palette" => _dockPalette,
            "Hierarchy" => _dockHierarchy,
            "Details" => _dockDetails,
            "Content" => _dockContent,
            "UIDesigner" => _dockDesigner,
            "AnimEditor" => _dockAnim,
            "AudioMixer" => _dockMixer,
            "ParticleEditor" => _dockParticles,
            _ => null!,
        };

        private static DockState DefaultState(PanelDock d) =>
            d.Key == "Palette" ? DockState.DockLeft :
            d.Key == "Content" ? DockState.DockBottom : DockState.DockRight;

        private void ApplyDefaultLayout()
        {
            if (_dockViewport.DockPanel == null) _dockViewport.Show(_dock, DockState.Document);

            _dockPalette.Show(_dock, DockState.DockLeft);
            _dockHierarchy.Show(_dock, DockState.DockRight);
            _dockDetails.Show(_dockHierarchy.Pane, DockAlignment.Bottom, 0.6);
            _dockContent.Show(_dock, DockState.DockBottom);

            _dock.DockLeftPortion = 210;
            _dock.DockRightPortion = 340;
            _dock.DockBottomPortion = 200;
        }

        // Called from the form's Load event
        public void RestoreLayout()
        {
            bool loaded = false;
            try
            {
                if (File.Exists(LayoutPath))
                {
                    _dock.LoadFromXml(LayoutPath, GetContentFromPersistString);
                    loaded = true;
                }
            }
            catch { /* corrupt or old layout file: use the default */ }

            if (!loaded)
            {
                foreach (PanelDock d in _tools) d.DockPanel = null!;     // a failed load may have attached some
                ApplyDefaultLayout();
            }
            else if (_dockViewport.DockPanel == null)
            {
                _dockViewport.Show(_dock, DockState.Document);
            }

            foreach (PanelDock d in _tools)                               // panels an older layout file didn't know about
                if (d.DockPanel == null) d.Show(_dock, DefaultState(d));
        }

        // Called from the form's FormClosing event
        public void SaveLayout()
        {
            ExitEditorFullscreen();
            if (_hiddenByToggle.Count > 0) ToggleVisible();               // don't save a layout with everything hidden
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(LayoutPath)!);
                _dock.SaveAsXml(LayoutPath);
            }
            catch { }
        }

        private void ResetLayout()
        {
            ExitEditorFullscreen();
            _hiddenByToggle.Clear();
            foreach (PanelDock d in _tools) d.DockPanel = null!;
            if (_dockDesigner.DockPanel != null) _dockDesigner.DockPanel = null!;
            if (_dockAnim.DockPanel != null) _dockAnim.DockPanel = null!;
            if (_dockMixer.DockPanel != null) _dockMixer.DockPanel = null!;
            if (_dockParticles.DockPanel != null) _dockParticles.DockPanel = null!;
            ApplyDefaultLayout();
        }
        private bool IsInsideDesigner(Control? c)
        {
            for (; c != null; c = c.Parent) if (c == _designer || c == _animEditor || c == _mixer || c == _particles) return true;
            return false;
        }

        public void OpenDesigner(string? path)
        {
            if (_dockDesigner.DockState == DockState.Unknown)
            {
                Rectangle r = _form.Bounds;                       // first time: a big floating window you can dock later
                var b = new Rectangle(r.X + 60, r.Y + 80,
                    Math.Max(700, Math.Min(1400, r.Width - 120)), Math.Max(500, Math.Min(820, r.Height - 140)));
                _dockDesigner.Show(_dock, b);
            }
            else _dockDesigner.Show(_dock);

            if (path != null) _designer.OpenFile(path);
            _designer.FocusCanvas();
        }

        public bool ConfirmDiscardDesigner(IWin32Window owner) =>
        (_designer == null || _designer.ConfirmDiscard(owner)) &&
        (_animEditor == null || _animEditor.ConfirmDiscard(owner));

        // Ctrl+S is a menu shortcut for Save Scene; while an editor window has focus it saves that file instead
        public bool SaveDesignerIfFocused()
        {
            for (Control? c = Control.FromHandle(GhostShell.Win32.GetFocus()); c != null; c = c.Parent)
            {
                if (c == _designer) { _designer.Save(); return true; }
                if (c == _animEditor) { _animEditor.Save(); return true; }
            }
            return false;
        }
        public void OpenMixer()
        {
            if (_dockMixer.DockState == DockState.Unknown)
            {
                Rectangle r = _form.Bounds;
                var b = new Rectangle(r.X + 100, r.Y + 120,
                    Math.Max(760, Math.Min(900, r.Width - 160)), Math.Max(480, Math.Min(640, r.Height - 180)));
                _dockMixer.Show(_dock, b);
            }
            else _dockMixer.Show(_dock);
        }
        public void OpenAnimEditor(string? path)
        {
            if (_dockAnim.DockState == DockState.Unknown)
            {
                Rectangle r = _form.Bounds;
                var b = new Rectangle(r.X + 80, r.Y + 100,
                    Math.Max(900, Math.Min(1500, r.Width - 140)), Math.Max(560, Math.Min(800, r.Height - 160)));
                _dockAnim.Show(_dock, b);
            }
            else _dockAnim.Show(_dock);

            if (path != null) _animEditor.OpenFile(path);
            _animEditor.FocusCanvas();
        }

        public void OpenParticleEditor()
        {
            if (_dockParticles.DockState == DockState.Unknown)
            {
                Rectangle r = _form.Bounds;
                var b = new Rectangle(r.X + 120, r.Y + 140,
                    Math.Max(760, Math.Min(1000, r.Width - 180)), Math.Max(520, Math.Min(760, r.Height - 200)));
                _dockParticles.Show(_dock, b);
            }
            else _dockParticles.Show(_dock);
            _particles.FocusCanvas();
        }

        private bool IsShown(PanelDock d) => d.DockPanel != null && !d.IsHidden;

        private void ShowPanel(PanelDock d)
        {
            if (d.DockState == DockState.Unknown) d.Show(_dock, DefaultState(d));
            else d.Show(_dock);
        }

        private void BuildWindowMenu()
        {
            ToolStrip? strip = _form.MainMenuStrip;
            if (strip == null) return;

            var menu = new ToolStripMenuItem("Window");

            var viewportItem = new ToolStripMenuItem("Viewport");
            viewportItem.Click += (s, e) =>
            {
                if (_dockViewport.DockState == DockState.Unknown || _dockViewport.DockPanel == null)
                    _dockViewport.Show(_dock, DockState.Document);
                else
                    _dockViewport.Show(_dock);
            };
            menu.DropDownItems.Add(viewportItem);
            menu.DropDownItems.Add(new ToolStripSeparator());

            foreach (PanelDock d in _tools)
            {
                PanelDock dock = d;
                var item = new ToolStripMenuItem(dock.Text);
                item.Click += (s, e) => ShowPanel(dock);
                menu.DropDownItems.Add(item);
            }
            var uiItem = new ToolStripMenuItem("UI Designer");
            uiItem.Click += (s, e) => OpenDesigner(null);
            menu.DropDownItems.Add(uiItem);
            var animItem = new ToolStripMenuItem("Animation Editor");
            animItem.Click += (s, e) => OpenAnimEditor(null);
            menu.DropDownItems.Add(animItem);
            var mixItem = new ToolStripMenuItem("Audio Mixer");
            mixItem.Click += (s, e) => OpenMixer();
            menu.DropDownItems.Add(mixItem);
            var fsMenu = new ToolStripMenuItem("Fullscreen Editor");
            var fsUi = new ToolStripMenuItem("UI Designer") { ShortcutKeyDisplayString = "F11" };
            fsUi.Click += (s, e) => { OpenDesigner(null); ToggleEditorFullscreen(_designer, "UI Designer", () => _designer.Save()); };
            var fsAnim = new ToolStripMenuItem("Animation Editor") { ShortcutKeyDisplayString = "F11" };
            fsAnim.Click += (s, e) => { OpenAnimEditor(null); ToggleEditorFullscreen(_animEditor, "Animation Editor", () => _animEditor.Save()); };
            var fsMix = new ToolStripMenuItem("Audio Mixer") { ShortcutKeyDisplayString = "F11" };
            fsMix.Click += (s, e) => { OpenMixer(); ToggleEditorFullscreen(_mixer, "Audio Mixer", null); };
            var fxItem = new ToolStripMenuItem("Particle Editor");
            fxItem.Click += (s, e) => OpenParticleEditor();
            menu.DropDownItems.Add(fxItem);
            fsMenu.DropDownItems.AddRange(new ToolStripItem[] { fsUi, fsAnim, fsMix, fxItem });
            menu.DropDownItems.Add(fsMenu);
            menu.DropDownItems.Add(new ToolStripSeparator());
            var reset = new ToolStripMenuItem("Reset Layout");
            reset.Click += (s, e) => ResetLayout();
            menu.DropDownItems.Add(reset);

            menu.DropDownOpening += (s, e) =>
            {
                viewportItem.Checked = _dockViewport.DockPanel != null && !_dockViewport.IsHidden;

                foreach (PanelDock d in _tools)
                {
                    // find the matching menu item by text instead of index
                    foreach (ToolStripItem ti in menu.DropDownItems)
                    {
                        if (ti is ToolStripMenuItem mi && mi.Text == d.Text)
                        {
                            mi.Checked = IsShown(d);
                            break;
                        }
                    }
                }

                uiItem.Checked = IsShown(_dockDesigner);
                animItem.Checked = IsShown(_dockAnim);
                mixItem.Checked = IsShown(_dockMixer);
                fxItem.Checked = IsShown(_dockParticles);
            };

            int at = strip.Items.Count;                                   // before Help
            for (int i = 0; i < strip.Items.Count; i++)
                if (strip.Items[i].Text == "Help") { at = i; break; }
            strip.Items.Insert(at, menu);
        }

        // -------------------------------------------------------
        //  Palette
        // -------------------------------------------------------
        public void RefreshPalette() { if (_category.SelectedIndex == 5) FillPalette(); }
        private void FillPalette()
        {
            _palette.Items.Clear();
            switch (_category.SelectedIndex)
            {
                case 0:
                    Add("Directional Light", "light", 0);
                    Add("Point Light", "light", 1);
                    Add("Rect Area Light", "light", 2);
                    Add("Spot Light", "light", 3);
                    break;
                case 1:
                    Add("Cube", "obj", 0);
                    Add("Triangle", "obj", 1);
                    Add("Ship", "obj", 2);
                    Add("Square", "obj", 3);
                    Add("Cylinder", "obj", 5);
                    Add("Cone", "obj", 6);
                    Add("Capsule", "obj", 7);
                    Add("Plane", "obj", 8);
                    Add("UV Sphere", "obj", 9);
                    Add("Ico Sphere", "obj", 10);
                    Add("Torus", "obj", 11);
                    Add("Pyramid", "obj", 12);
                    Add("Wedge", "obj", 13);
                    Add("Disc", "obj", 14);
                    break;
                case 2:
                    Add("Sprite Actor", "obj", 4);
                    break;
                case 3:
                    Add("Free Camera", "cam", 0);
                    Add("Orbit Camera", "cam", 1);
                    break;
                case 5:
                    {
                        GhostModels.ScanProject();                              // loads every .ghostmesh in Content/Models
                        int n = GhostEngine.GhostEngine_GetMeshCount();
                        for (int i = 0; i < n; i++)
                        {
                            if (GhostModels.IsBuiltin(i) || !GhostModels.BelongsToProject(i)) continue;
                            Add(GhostModels.MeshName(i), "obj", i);             // a model is dropped like a primitive, by mesh id
                        }
                        if (_palette.Items.Count == 0) Add("(none yet - File > Import Model...)", "none", 0);
                        break;
                    }
                case 6:
                    Add("Tile Map", "tilemap", 0);
                    break;
                case 7:
                    Add("Reflection Capture (Sphere)", "volume", 0);
                    Add("Fog Volume", "volume", 1);
                    break;
                case 8:
                    Add("Fire", "particle", 0);
                    Add("Snow", "particle", 1);
                    Add("Custom", "particle", 2);
                    break;
           
              
                default:
                    {
                        int n = GhostEngine.GhostEngine_GetModTypeCount();
                        for (int i = 0; i < n; i++)
                        {
                            var sb = new StringBuilder(64);
                            GhostEngine.GhostEngine_GetModTypeName(i, sb, sb.Capacity);
                            Add(sb.ToString(), "mod", i);
                        }
                        break;
                    }
            }
            void Add(string text, string kind, int type) =>
                _palette.Items.Add(new PaletteItem { Text = text, Kind = kind, Type = type });
        }
        private void OnPaletteMouseMove(object? sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Left || _dragIndex < 0) return;
            Size d = SystemInformation.DragSize;
            if (Math.Abs(e.X - _dragStart.X) < d.Width && Math.Abs(e.Y - _dragStart.Y) < d.Height) return;

            var item = _palette.Items[_dragIndex] as PaletteItem;
            _dragIndex = -1;
            if (item != null && item.Kind != "none")
                _palette.DoDragDrop($"ghost:{item.Kind}:{item.Type}", DragDropEffects.Copy);
        }

        private static string? DropPayload(DragEventArgs e)
        {
            if (e.Data == null || !e.Data.GetDataPresent(DataFormats.StringFormat)) return null;
            string? s = e.Data.GetData(DataFormats.StringFormat) as string;
            return (s != null && s.StartsWith("ghost:")) ? s : null;
        }

    
        private void Spawn(string kind, int type, int x, int y)
        {
            int id = kind == "light" ? GhostEngine.GhostEngine_AddLight(type, x, y)
: kind == "cam" ? GhostEngine.GhostEngine_AddCamera(type, x, y)
: kind == "mod" ? GhostEngine.GhostEngine_AddMod(type, x, y)
: kind == "volume" ? GhostEngine.GhostEngine_AddVolume(type, x, y)
: kind == "tilemap" ? GhostEngine.GhostEngine_AddTileMap(x, y)
: kind == "particle" ? GhostEngine.GhostEngine_AddParticleSystem(type, x, y)
                : GhostEngine.GhostEngine_AddObject(type, x, y);
            if (id == 0) System.Media.SystemSounds.Beep.Play();   // scene is full (or 4 cameras already)
            else if (kind == "cam") _vp.Open(id);
            else if (kind == "particle")
            {
                if (!IsShown(_dockParticles)) OpenParticleEditor();
                _particles.SelectEmitter(id);
            }
            Sync();
        }


        private void OnViewportDrag(object? sender, DragEventArgs e)
        {
            string? payload = DropPayload(e);
            if (payload == null) { e.Effect = DragDropEffects.None; return; }
            e.Effect = DragDropEffects.Copy;

            if (payload.StartsWith("ghost:tex:"))
            {
                // The object under the cursor gets selected, so its orange tint shows where the drop will land
                Point pt = _viewport.PointToClient(new Point(e.X, e.Y));
                int id = GhostEngine.GhostEngine_PickObjectAt(pt.X, pt.Y, out _);
                if (id == 0) e.Effect = DragDropEffects.None;
                else if (id != GhostEngine.GhostEngine_GetSelected()) GhostEngine.GhostEngine_SelectEntity(id);

                GhostEngine.GhostEngine_Tick();   // the idle render loop doesn't run during a drag, so draw a frame here
            }
        }

        private void OnViewportDrop(object? sender, DragEventArgs e)
        {
            string? payload = DropPayload(e);
            if (payload == null) return;
            string[] p = payload.Split(':');
            if (p.Length != 3 || !int.TryParse(p[2], out int value)) return;
            Point pt = _viewport.PointToClient(new Point(e.X, e.Y));

            if (p[1] == "tex")
            {
                int id = GhostEngine.GhostEngine_PickObjectAt(pt.X, pt.Y, out int face);
                if (id != 0)
                {
                    GhostEngine.GhostEngine_SelectEntity(id);
                    bool oneFace = (e.KeyState & 4) != 0 && face >= 0;          // Shift = only the face under the cursor
                    GhostEngine.GhostEngine_SetTargetFace(oneFace ? face : -1);
                    GhostTextures.Apply(id, oneFace ? face : -1, value);
                    _grid.Refresh();
                }
            }
            else
            {
                Spawn(p[1], value, pt.X, pt.Y);
            }
            _viewport.Select();
        }

        private void ApplyTextureToSelection(int tex)
        {
            int sel = GhostEngine.GhostEngine_GetSelected();
            var o = new GhostObjectInfo();
            if (sel == 0 || GhostEngine.GhostEngine_GetObject(sel, ref o) == 0)
            {
                System.Media.SystemSounds.Beep.Play();                         // nothing selected to apply to
                return;
            }
            GhostTextures.Apply(sel, GhostEngine.GhostEngine_GetTargetFace(), tex);
            _grid.Refresh();
        }
        // -------------------------------------------------------
        //  Hierarchy / details sync
        // -------------------------------------------------------
        private void Sync()
        {
            uint ver = GhostEngine.GhostEngine_GetSceneVersion();
            int sel = GhostEngine.GhostEngine_GetSelected();

            bool rebuilt = ver != _lastVersion;
            if (rebuilt) { RebuildTree(); _lastVersion = ver; }

            if (rebuilt || sel != _lastSel)
            {
                SelectNode(sel);
                ShowDetails(sel);
                HighlightSlot(sel);
                _lastSel = sel;
                _lastSnap = Snapshot(sel);
                return;
            }
                      
            // Same selection: refresh the grid only if the values changed (viewport drag)
            object? snap = Snapshot(sel);
            if (!Equals(snap, _lastSnap))
            {
                _lastSnap = snap;
                // Free <-> Orbit swaps which properties exist, so rebuild the grid instead of refreshing it
                if (snap is GhostCameraInfo ci && _grid.SelectedObject is CameraProxy cp && cp.IsOrbit != (ci.mode == 1))
                    ShowDetails(sel);
                else if (BehaviorStale(_grid.SelectedObject))     // role changed: rebuild so Behavior lists the new role's properties
                    ShowDetails(sel);
                else
                    _grid.Refresh();
                HighlightSlot(sel);
            }
        }

        private static bool BehaviorStale(object? o) => o switch
        {
            ObjectProxy p => p.BehaviorOutOfDate(),
            SpriteProxy p => p.BehaviorOutOfDate(),
            TileMapProxy p => p.LayersOutOfDate(),
            _ => false,
        };

        private void RebuildTree()
        {
            _syncing = true;
            _tree.BeginUpdate();
            var open = new HashSet<int>();
            foreach (TreeNode t in _tree.Nodes) if (t.IsExpanded && t.Tag is int tid) open.Add(tid);
            _tree.Nodes.Clear();
            _nodes.Clear();

            _settingsNode = new TreeNode("Scene Settings") { Tag = 0, ForeColor = Fg };
            _tree.Nodes.Add(_settingsNode);

            var buf = new GhostEntityInfo[1024];
            int n = GhostEngine.GhostEngine_GetEntities(buf, buf.Length);
            int activeCam = GhostEngine.GhostEngine_GetActiveCamera();
            for (int i = 0; i < n; i++)
            {
                string text = GhostNames.Of(buf[i].id);
                if (buf[i].kind == 2 && buf[i].id == activeCam) text += "   (Main View)";
                var node = new TreeNode(text)
                {
                    Tag = buf[i].id,
                    ForeColor = buf[i].kind == 1 ? Accent : (buf[i].kind == 2 ? CamColor : (buf[i].kind == 3 ? ModColor : (buf[i].kind == 4 ? VolColor : (buf[i].kind == 4 ? FxColor :  Fg)))),
                   
                };
                _tree.Nodes.Add(node);
                _nodes[buf[i].id] = node;
                if (buf[i].kind == 0 && IsImportedMesh(buf[i].subtype))
                {
                    AddSlotNodes(node, buf[i].id);
                    if (open.Contains(buf[i].id)) node.Expand();
                }
            }
            _tree.EndUpdate();
            _syncing = false;
            var camIds = new HashSet<int>();
            for (int i = 0; i < n; i++) if (buf[i].kind == 2) camIds.Add(buf[i].id);
            _vp.SyncCameras(camIds);                       // a deleted camera takes its window with it
        }
        private static bool IsImportedMesh(int meshId) =>
    GhostEngine.GhostEngine_GetMeshInfo(meshId, out GhostMeshStats s) != 0 && s.builtin == 0 && s.faceCount > 1;

        private static void AddSlotNodes(TreeNode parent, int objectId)
        {
            int n = GhostEngine.GhostEngine_GetFaceCount(objectId);
            for (int f = 0; f < n; f++)
            {
                var sb = new StringBuilder(64);
                GhostEngine.GhostEngine_GetFaceName(objectId, f, sb, sb.Capacity);
                parent.Nodes.Add(new TreeNode($"[{f}] {sb}") { Tag = -(f + 1), ForeColor = Fg });
            }
        }

        // Keeps the slot row in step with the target face (also when it was picked in the viewport with F)
        private void HighlightSlot(int objectId)
        {
            if (!_nodes.TryGetValue(objectId, out TreeNode? node) || node.Nodes.Count == 0) return;
            int face = GhostEngine.GhostEngine_GetTargetFace();
            TreeNode target = (face >= 0 && face < node.Nodes.Count) ? node.Nodes[face] : node;
            if (_tree.SelectedNode == target) return;
            _syncing = true;
            node.Expand();
            _tree.SelectedNode = target;
            _syncing = false;
        }
        private void SelectNode(int id)
        {
            _syncing = true;
            _tree.SelectedNode = (id != 0 && _nodes.TryGetValue(id, out TreeNode? node)) ? node : _settingsNode;
            _syncing = false;
        }

        private void ShowDetails(int id)
        {
            object? proxy = null;
            if (id == 0)
            {
                proxy = new AmbientProxy();
            }
            else
            {
                var o = new GhostObjectInfo();
                var l = new GhostLightInfo();
                if (GhostEngine.GhostEngine_GetObject(id, ref o) != 0)
                {
                    var tm = new GhostTileMapInfo();
                    if (GhostEngine.GhostEngine_GetTileMap(id, ref tm) != 0)
                        proxy = tm.layerCount > 0 ? new TileMapLayerProxy(id) : new TileMapProxy(id);
                    else
                        proxy = o.meshId == 4 ? new SpriteProxy(id)
                              : GhostEngine.GhostEngine_IsMeshSkinned(o.meshId) != 0 ? new ModelProxy(id)
                              : new ObjectProxy(id);
                }
                else if (GhostEngine.GhostEngine_GetLight(id, ref l) != 0)
                {
                    if (l.type == 0) proxy = new DirectionalLightProxy(id);
                    else if (l.type == 1) proxy = new PointLightProxy(id);
                    else if (l.type == 3) proxy = new SpotLightProxy(id);
                    else proxy = new RectLightProxy(id);
                }
              
                else
                {
                    var c = new GhostCameraInfo();
                    if (GhostEngine.GhostEngine_GetCamera(id, ref c) != 0)
                        proxy = c.mode == 1 ? new OrbitCameraProxy(id) : new CameraProxy(id);
                    else if (GhostEngine.GhostEngine_GetVolume(id, out GhostVolumeInfo vi) != 0)
                        proxy = VolumeProxy.Create(id, vi.type);
                    else if (GhostModProxy.Exists(id))
                        proxy = new GhostModProxy(id);
                    else
                    {
                        var pp = new GhostParticleParams();
                        if (GhostEngine.GhostEngine_GetParticle(id, ref pp) != 0) proxy = new ParticleProxy(id);
                    }
                }


            }
            _grid.SelectedObject = proxy;
        }

        private static object? Snapshot(int id)
        {
            if (id == 0)
            {
                GhostEngine.GhostEngine_GetAmbient(out float r, out float g, out float b, out float k);
                GhostEngine.GhostEngine_GetShadowSettings(out int se, out float ss);
                GhostEngine.GhostEngine_GetShadowTuning(out GhostShadowTuning st);
                GhostEngine.GhostEngine_GetPostSettings(out GhostPostInfo pi);
                GhostEngine.GhostEngine_GetFogSettings(out GhostFogInfo fogi);
                return (r, g, b, k, GhostEngine.GhostEngine_GetGameStartCamera(), se, ss, GhostEngine.GhostEngine_GetShadowResolution(), st, pi);
            }
            var o = new GhostObjectInfo();
            if (GhostEngine.GhostEngine_GetObject(id, ref o) != 0)
            {
                var gp = new GhostGameplayInfo();
                GhostEngine.GhostEngine_GetGameplay(id, ref gp);
                var tm = new GhostTileMapInfo();
                GhostEngine.GhostEngine_GetTileMap(id, ref tm);
                return (o, GhostEngine.GhostEngine_GetTargetFace(), gp, tm, GhostEngine.GhostEngine_GetPaintMode());
            }
            var l = new GhostLightInfo();
            if (GhostEngine.GhostEngine_GetLight(id, ref l) != 0) return l;
            var c = new GhostCameraInfo();
            if (GhostEngine.GhostEngine_GetCamera(id, ref c) != 0) return c;
            if (GhostEngine.GhostEngine_GetVolume(id, out GhostVolumeInfo vol) != 0)
                return (vol.shape, vol.enabled, vol.priority, vol.px, vol.py, vol.pz, vol.rx, vol.ry, vol.rz,
                        vol.sx, vol.sy, vol.sz, vol.radius, vol.blend, string.Join(",", vol.p), string.Join(",", vol.tex),
                        GhostEngine.GhostEngine_GetProbeStatus(id));
            var pp = new GhostParticleParams();
            if (GhostEngine.GhostEngine_GetParticle(id, ref pp) != 0) return pp;
            if (GhostModProxy.Exists(id)) return GhostMods.Signature(id);
            return null;
        }

        private static List<int> SelectedIdsList()
        {
            var buf = new int[512];
            int n = GhostEngine.GhostEngine_GetSelectedIds(buf, buf.Length);
            return new List<int>(new ArraySegment<int>(buf, 0, n));
        }
    }
}