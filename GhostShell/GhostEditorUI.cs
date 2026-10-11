// GhostEditorUI.cs — engine editor panels: palette (left), hierarchy + details (right), assets (bottom)
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

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
            for (int i = 0; i < n; i++) list.Add(DisplayName(i));
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
                int tex = GhostAssets.Import(dlg.FileName);
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
    internal class CameraProxy
    {
        private readonly int _id;

        public CameraProxy(int id)
        {
            _id = id;
            Position = new Vec3Edit(
                () => { var i = Get(); return (i.px, i.py, i.pz); },
                (x, y, z) => { var i = Get(); i.px = x; i.py = y; i.pz = z; Set(i); });
        }

        private GhostCameraInfo Get() { var i = new GhostCameraInfo(); GhostEngine.GhostEngine_GetCamera(_id, ref i); return i; }
        private void Set(GhostCameraInfo i) => GhostEngine.GhostEngine_SetCamera(ref i);

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

        [Category("Camera"), DisplayName("Yaw (deg)")]
        public float Yaw { get => Get().yawDeg; set { var i = Get(); i.yawDeg = value; Set(i); } }

        [Category("Camera"), DisplayName("Pitch (deg)")]
        public float Pitch { get => Get().pitchDeg; set { var i = Get(); i.pitchDeg = value; Set(i); } }

        [Category("Camera"), DisplayName("FOV (deg)")]
        public float Fov { get => Get().fovDeg; set { var i = Get(); i.fovDeg = value; Set(i); } }

        [Category("Transform")] public Vec3Edit Position { get; }
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

        private sealed class PaletteItem
        {
            public string Text = "";
            public string Kind = "obj";      // "light", "obj" or "cam"
            public int Type;
            public override string ToString() => Text;
        }

        private readonly Form _form;
        private readonly Panel _viewport;

        private Panel _left = null!, _right = null!, _bottom = null!;
        private ComboBox _category = null!;
        private ListBox _palette = null!;
        private TreeView _tree = null!;
        private PropertyGrid _grid = null!;
        private TreeNode _settingsNode = null!;
        private readonly Dictionary<int, TreeNode> _nodes = new();

        private bool _visible = true;
        private bool _syncing;
        private uint _lastVersion = uint.MaxValue;
        private int _lastSel = int.MinValue;
        private object? _lastSnap;

        private int _dragIndex = -1;
        private Point _dragStart;

        public GhostEditorUI(Form form, Panel viewport) { _form = form; _viewport = viewport; }

        // -------------------------------------------------------
        public void Build()
        {
           
            // ---- Bottom: content browser ----
            var browser = new GhostContentBrowser { Dock = DockStyle.Bottom, Height = 190 };
            browser.ApplyRequested += ApplyTextureToSelection;
            _bottom = browser;

            // ---- Left: palette ----
            _left = new Panel { Dock = DockStyle.Left, Width = 200, BackColor = Bg };
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
            _category.Items.AddRange(new object[] { "Lighting", "Primitives", "Cameras" });
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

            // ---- Right: hierarchy (top) + details (fill) ----
            _right = new Panel { Dock = DockStyle.Right, Width = 320, BackColor = Bg };
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

            var splitter = new Splitter { Dock = DockStyle.Top, Height = 5, BackColor = Bg2 };
            _tree = new TreeView
            {
                Dock = DockStyle.Top,
                Height = 240,
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

            // add order matters: Fill first, then the splitter, then the tree
            _right.Controls.Add(_grid);
            _right.Controls.Add(splitter);
            _right.Controls.Add(_tree);

            // ---- Dock into the form ----
            // Higher child index docks first. Added after the render panel, so these dock before it;
            // the menu strip is sent to the back so it docks first of all (full width, on top).
            _form.Controls.Add(_bottom);
            _form.Controls.Add(_left);
            _form.Controls.Add(_right);
            _form.MainMenuStrip?.SendToBack();

            // ---- Viewport: drop target + keyboard focus ----
            _viewport.AllowDrop = true;
            _viewport.DragEnter += OnViewportDrag;
            _viewport.DragOver += OnViewportDrag;
            _viewport.DragDrop += OnViewportDrop;
            _viewport.MouseDown += (s, e) => _viewport.Select();
            _viewport.MouseEnter += (s, e) =>
            {
                // Hovering the viewport takes keyboard focus (and commits any pending grid edit)
                if (Form.ActiveForm == _form && !(_form.ActiveControl is TextBoxBase))
                    _viewport.Select();
            };

            // ---- Live sync (hierarchy, selection, values changed by viewport drags) ----
            var timer = new System.Windows.Forms.Timer { Interval = 100 };
            timer.Tick += (s, e) => Sync();
            timer.Start();
            _form.FormClosing += (s, e) => timer.Stop();
        }

        // -------------------------------------------------------
        //  Visibility + keys
        // -------------------------------------------------------
        public void ToggleVisible()
        {
            _visible = !_visible;
            _left.Visible = _visible;
            _right.Visible = _visible;
            _bottom.Visible = _visible;
            _viewport.Select();
        }

        // Returns true if the key was consumed
        public bool HandleKey(Keys key)
        {
            // The property grid starts editing on a keypress, so never steal keys from it
            if (_form.ActiveControl is PropertyGrid || _form.ActiveControl is TextBoxBase || _form.ActiveControl is UpDownBase)
                return false;

            if (key == Keys.D4 || key == Keys.NumPad4) { ToggleVisible(); return true; }

            if (key == Keys.Delete)
            {
                int sel = GhostEngine.GhostEngine_GetSelected();
                if (sel == 0) return false;
                if (GhostEngine.GhostEngine_RemoveEntity(sel) == 0)
                    System.Media.SystemSounds.Beep.Play();       // refused: the last camera stays
                Sync();
                return true;
            }
            return false;
        }

        // -------------------------------------------------------
        //  Palette
        // -------------------------------------------------------
        private void FillPalette()
        {
            _palette.Items.Clear();
            if (_category.SelectedIndex == 0)
            {
                Add("Directional Light", "light", 0);
                Add("Point Light", "light", 1);
                Add("Rect Area Light", "light", 2);
            }
            else if (_category.SelectedIndex == 1)
            {
                Add("Cube", "obj", 0);
                Add("Triangle", "obj", 1);
                Add("Ship", "obj", 2);
            }
            else
            {
                Add("Free Camera", "cam", 0);
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
                   : kind == "cam" ? GhostEngine.GhostEngine_AddCamera(x, y)
                                     : GhostEngine.GhostEngine_AddObject(type, x, y);
            if (id == 0) System.Media.SystemSounds.Beep.Play();   // scene is full (or 4 cameras already)
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
                    ForeColor = buf[i].kind == 1 ? Accent : (buf[i].kind == 2 ? CamColor : Fg),
                };
                _tree.Nodes.Add(node);
                _nodes[buf[i].id] = node;
            }
            _tree.EndUpdate();
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
                    if (GhostEngine.GhostEngine_GetCamera(id, ref c) != 0) proxy = new CameraProxy(id);
                }
            }
            _grid.SelectedObject = proxy;
        }

        private static object? Snapshot(int id)
        {
            if (id == 0)
            {
                GhostEngine.GhostEngine_GetAmbient(out float r, out float g, out float b, out float k);
                return (r, g, b, k);
            }
            var o = new GhostObjectInfo();
            if (GhostEngine.GhostEngine_GetObject(id, ref o) != 0)
                return (o, GhostEngine.GhostEngine_GetTargetFace());   // face included, so the grid refreshes when it changes
            var l = new GhostLightInfo();
            if (GhostEngine.GhostEngine_GetLight(id, ref l) != 0) return l;
            var c = new GhostCameraInfo();
            if (GhostEngine.GhostEngine_GetCamera(id, ref c) != 0) return c;
            return null;
        }
    }
}