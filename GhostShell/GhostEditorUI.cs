// GhostEditorUI.cs — engine editor panels: palette (left), hierarchy + details (right), assets (bottom)
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Text;
using System.Windows.Forms;
using System.IO;
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
                if (i == GhostFontAtlas.TextureId) continue; // the UI font isn't a material texture

                list.Add(DisplayName(i));
            }

            list.Add(ImportLabel);
            return list;
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
            var buf = new GhostEntityInfo[512];
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
        public static readonly string[] Names = { "Static", "Player", "Enemy", "Coin", "Block", "Goal", "Custom" };
        protected override string[] Items => Names;
    }
    internal class ObjectProxy
    {
        private readonly int _id;

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
        }

        private GhostObjectInfo Get() { var i = new GhostObjectInfo(); GhostEngine.GhostEngine_GetObject(_id, ref i); return i; }
        private void Set(GhostObjectInfo i) => GhostEngine.GhostEngine_SetObject(ref i);

        [Category("Details")] public string Name => GhostNames.Of(_id);

        [Category("Transform")] public Vec3Edit Position { get; }
        [Category("Transform"), DisplayName("Rotation (deg)")] public Vec3Edit Rotation { get; }
        [Category("Transform")] public Vec3Edit Scale { get; }

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

        [Category("Material"), DisplayName("Target Face")]
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

        [Category("Gameplay")]
        public bool Collider
        {
            get => GetG().collider != 0;
            set { var g = GetG(); g.collider = value ? 1 : 0; SetG(g); }
        }

        [Category("Gameplay"), DisplayName("Collider Size")]
        public Vec3Edit ColliderSize { get; }

        [Category("Gameplay"), DisplayName("Visible In Game")]
        public bool VisibleInGame
        {
            get => GetG().visible != 0;
            set { var g = GetG(); g.visible = value ? 1 : 0; SetG(g); }
        }
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
        [Category("Details")] public string Type => Get().type switch { 0 => "Directional", 1 => "Point", _ => "Rect Area" };

        [Category("Transform")] public Vec3Edit Position { get; }

        [Category("Light"), DisplayName("Color")]
        public Color LightColor
        {
            get { var i = Get(); return GhostColorUtil.ToColor(i.r, i.g, i.b); }
            set { var i = Get(); i.r = value.R / 255f; i.g = value.G / 255f; i.b = value.B / 255f; Set(i); }
        }

        [Category("Light")]
        public float Intensity { get => Get().intensity; set { var i = Get(); i.intensity = value; Set(i); } }
    }

    internal class DirectionalLightProxy : LightProxy
    {
        public DirectionalLightProxy(int id) : base(id) { }

        [Category("Light"), DisplayName("Yaw (deg)")]
        public float Yaw { get => Get().yawDeg; set { var i = Get(); i.yawDeg = value; Set(i); } }

        [Category("Light"), DisplayName("Pitch (deg)")]
        public float Pitch { get => Get().pitchDeg; set { var i = Get(); i.pitchDeg = value; Set(i); } }
    }

    internal class PointLightProxy : LightProxy
    {
        public PointLightProxy(int id) : base(id) { }

        [Category("Light")]
        public float Range { get => Get().range; set { var i = Get(); i.range = value; Set(i); } }
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


        private bool _visible = true;
        private bool _syncing;
        private uint _lastVersion = uint.MaxValue;
        private int _lastSel = int.MinValue;
        private object? _lastSnap;

        private int _dragIndex = -1;
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
            _category.Items.AddRange(new object[] { "Lighting", "Primitives", "Cameras", "Mods" });
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
                if (_palette.SelectedItem is PaletteItem it) Spawn(it.Kind, it.Type, -1, -1);
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
                GhostEngine.GhostEngine_SelectEntity(id);
                ShowDetails(id);
                _lastSel = id;
                _lastSnap = Snapshot(id);
            };
            // Double-click a camera in the hierarchy to make it the main view
            _tree.NodeMouseDoubleClick += (s, e) =>
            {
                if (e.Node?.Tag is int id && id != 0)
                {
                    var c = new GhostCameraInfo();
                    if (GhostEngine.GhostEngine_GetCamera(id, ref c) != 0)
                        GhostEngine.GhostEngine_SetActiveCamera(id);
                }
            };

            BuildDocking();

            // ---- Viewport: drop target + keyboard focus ----
            _viewport.AllowDrop = true;
            _viewport.DragEnter += OnViewportDrag;
            _viewport.DragOver += OnViewportDrag;
            _viewport.DragDrop += OnViewportDrop;
            _viewport.MouseDown += (s, e) => _viewport.Select();
            _viewport.MouseEnter += (s, e) =>
            {
                // Hovering the viewport takes keyboard focus, unless you're typing somewhere
                if (Form.ActiveForm != null && Form.ActiveForm == _viewport.TopLevelControl && !IsTyping())
                    _viewport.Select();
            };

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
        public bool HandleKey(Keys key, bool repeat)
        {
            bool ctrlD = key == Keys.D && Control.ModifierKeys == Keys.Control;
            if (key != Keys.D4 && key != Keys.NumPad4 && key != Keys.Delete && !ctrlD) return false;

            Control? focus = Control.FromHandle(GhostShell.Win32.GetFocus());
            if (focus == null) return false;          // a native dialog (file / color picker) has focus
            if (IsInsideDesigner(focus)) return false;
            if (IsTypingIn(focus)) return false;      // never steal keys from a text box or the property grid
            if (repeat) return true;

            if (key == Keys.Delete)
            {
                int sel = GhostEngine.GhostEngine_GetSelected();
                if (sel == 0) return false;
                if (GhostEngine.GhostEngine_RemoveEntity(sel) == 0)
                    System.Media.SystemSounds.Beep.Play();       // refused: the last camera stays
                Sync();
                return true;
            }
            if (ctrlD)
            {
                int sel = GhostEngine.GhostEngine_GetSelected();
                if (sel == 0) return false;
                if (GhostEngine.GhostEngine_DuplicateEntity(sel) == 0)
                    System.Media.SystemSounds.Beep.Play();         // not an object, scene full, or playing
                Sync();
                return true;
            }
            ToggleVisible();
            return true;
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
            _form.Controls.Add(_dock);
            _dock.BringToFront();                    // the dock fills whatever the menu strip leaves
            _form.MainMenuStrip?.SendToBack();

            BuildWindowMenu();
        }

        private IDockContent GetContentFromPersistString(string key) => key switch
        {
            "Viewport" => _dockViewport,
            "Palette" => _dockPalette,
            "Hierarchy" => _dockHierarchy,
            "Details" => _dockDetails,
            "Content" => _dockContent,
            "UIDesigner" => _dockDesigner,
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
            _hiddenByToggle.Clear();
            foreach (PanelDock d in _tools) d.DockPanel = null!;
            if (_dockDesigner.DockPanel != null) _dockDesigner.DockPanel = null!;
            ApplyDefaultLayout();
        }
        private bool IsInsideDesigner(Control? c)
        {
            for (; c != null; c = c.Parent) if (c == _designer) return true;
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

        public bool ConfirmDiscardDesigner(IWin32Window owner) => _designer == null || _designer.ConfirmDiscard(owner);

        // Ctrl+S is a menu shortcut for Save Scene; while the designer has focus it saves the UI instead
        public bool SaveDesignerIfFocused()
        {
            if (_designer == null || !IsInsideDesigner(Control.FromHandle(GhostShell.Win32.GetFocus()))) return false;
            _designer.Save();
            return true;
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
            menu.DropDownItems.Add(new ToolStripSeparator());
            var reset = new ToolStripMenuItem("Reset Layout");
            reset.Click += (s, e) => ResetLayout();
            menu.DropDownItems.Add(reset);

            menu.DropDownOpening += (s, e) =>
            {
                for (int i = 0; i < _tools.Count; i++)
                    ((ToolStripMenuItem)menu.DropDownItems[i]).Checked = IsShown(_tools[i]);
                // this may be wrong order
                uiItem.Checked = IsShown(_dockDesigner);
            };

            int at = strip.Items.Count;                                   // before Help
            for (int i = 0; i < strip.Items.Count; i++)
                if (strip.Items[i].Text == "Help") { at = i; break; }
            strip.Items.Insert(at, menu);
        }

        // -------------------------------------------------------
        //  Palette
        // -------------------------------------------------------
        private void FillPalette()
        {
            _palette.Items.Clear();
            switch (_category.SelectedIndex)
            {
                case 0:
                    Add("Directional Light", "light", 0);
                    Add("Point Light", "light", 1);
                    Add("Rect Area Light", "light", 2);
                    break;
                case 1:
                    Add("Cube", "obj", 0);
                    Add("Triangle", "obj", 1);
                    Add("Ship", "obj", 2);
                    Add("Square", "obj", 3);
                    break;
                case 2:
                    Add("Free Camera", "cam", 0);
                    Add("Orbit Camera", "cam", 1);
                    break;
                default:
                    {
                        int n = GhostEngine.GhostEngine_GetModTypeCount();       // every registered mod type
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
            if (item != null)
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
                           : GhostEngine.GhostEngine_AddObject(type, x, y);
            if (id == 0) System.Media.SystemSounds.Beep.Play();   // scene is full (or 4 cameras already)
            else if (kind == "cam") _vp.Open(id);
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
                else
                    _grid.Refresh();
            }
        }

        private void RebuildTree()
        {
            _syncing = true;
            _tree.BeginUpdate();
            _tree.Nodes.Clear();
            _nodes.Clear();

            _settingsNode = new TreeNode("Scene Settings") { Tag = 0, ForeColor = Fg };
            _tree.Nodes.Add(_settingsNode);

            var buf = new GhostEntityInfo[512];
            int n = GhostEngine.GhostEngine_GetEntities(buf, buf.Length);
            int activeCam = GhostEngine.GhostEngine_GetActiveCamera();
            for (int i = 0; i < n; i++)
            {
                string text = GhostNames.Of(buf[i].id);
                if (buf[i].kind == 2 && buf[i].id == activeCam) text += "   (Main View)";
                var node = new TreeNode(text)
                {
                    Tag = buf[i].id,
                    ForeColor = buf[i].kind == 1 ? Accent : (buf[i].kind == 2 ? CamColor : (buf[i].kind == 3 ? ModColor : Fg)),
                };
                _tree.Nodes.Add(node);
                _nodes[buf[i].id] = node;
            }
            _tree.EndUpdate();
            _syncing = false;
            var camIds = new HashSet<int>();
            for (int i = 0; i < n; i++) if (buf[i].kind == 2) camIds.Add(buf[i].id);
            _vp.SyncCameras(camIds);                       // a deleted camera takes its window with it
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
                    proxy = new ObjectProxy(id);
                }
                else if (GhostEngine.GhostEngine_GetLight(id, ref l) != 0)
                {
                    if (l.type == 0) proxy = new DirectionalLightProxy(id);
                    else if (l.type == 1) proxy = new PointLightProxy(id);
                    else proxy = new RectLightProxy(id);
                }
                else
                {
                    var c = new GhostCameraInfo();
                    if (GhostEngine.GhostEngine_GetCamera(id, ref c) != 0)
                        proxy = c.mode == 1 ? new OrbitCameraProxy(id) : new CameraProxy(id);
                    else if (GhostModProxy.Exists(id))
                        proxy = new GhostModProxy(id);
                }
            }
            _grid.SelectedObject = proxy;
        }

        private static object? Snapshot(int id)
        {
            if (id == 0)
            {
                GhostEngine.GhostEngine_GetAmbient(out float r, out float g, out float b, out float k);
                return (r, g, b, k, GhostEngine.GhostEngine_GetGameStartCamera());
            }
            var o = new GhostObjectInfo();
            if (GhostEngine.GhostEngine_GetObject(id, ref o) != 0)
                return (o, GhostEngine.GhostEngine_GetTargetFace());   // face included, so the grid refreshes when it changes
            var l = new GhostLightInfo();
            if (GhostEngine.GhostEngine_GetLight(id, ref l) != 0) return l;
            var c = new GhostCameraInfo();
            if (GhostEngine.GhostEngine_GetCamera(id, ref c) != 0) return c;
            if (GhostModProxy.Exists(id)) return GhostMods.Signature(id);
            return null;
        }
    }
}