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
        }

        private GhostObjectInfo Get() { var i = new GhostObjectInfo(); GhostEngine.GhostEngine_GetObject(_id, ref i); return i; }
        private void Set(GhostObjectInfo i) => GhostEngine.GhostEngine_SetObject(ref i);

        [Category("Details")] public string Name => GhostNames.Of(_id);

        [Category("Transform")] public Vec3Edit Position { get; }
        [Category("Transform"), DisplayName("Rotation (deg)")] public Vec3Edit Rotation { get; }
        [Category("Transform")] public Vec3Edit Scale { get; }

        [Category("Material"), DisplayName("Color")]
        public Color BaseColor
        {
            get { var i = Get(); return GhostColorUtil.ToColor(i.r, i.g, i.b); }
            set { var i = Get(); i.r = value.R / 255f; i.g = value.G / 255f; i.b = value.B / 255f; Set(i); }
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

        private sealed class PaletteItem
        {
            public string Text = "";
            public bool IsLight;
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
            // ---- Bottom: asset browser placeholder ----
            _bottom = new Panel { Dock = DockStyle.Bottom, Height = 140, BackColor = Bg };
            _bottom.Controls.Add(new Label
            {
                Dock = DockStyle.Fill,
                ForeColor = Color.Gray,
                TextAlign = ContentAlignment.MiddleCenter,
                Text = "Content Browser — mesh & asset imports (drag and drop) coming soon",
            });

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
            _category.Items.AddRange(new object[] { "Lighting", "Primitives" });
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
                if (_palette.SelectedItem is PaletteItem it) Spawn(it.IsLight, it.Type, -1, -1);
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
            _viewport.DragEnter += (s, e) => e.Effect = DropPayload(e) != null ? DragDropEffects.Copy : DragDropEffects.None;
            _viewport.DragOver += (s, e) => e.Effect = DropPayload(e) != null ? DragDropEffects.Copy : DragDropEffects.None;
            _viewport.DragDrop += (s, e) =>
            {
                string? payload = DropPayload(e);
                if (payload == null) return;
                string[] p = payload.Split(':');
                if (p.Length != 3 || !int.TryParse(p[2], out int type)) return;
                Point pt = _viewport.PointToClient(new Point(e.X, e.Y));
                Spawn(p[1] == "light", type, pt.X, pt.Y);
                _viewport.Select();
            };
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
                GhostEngine.GhostEngine_RemoveEntity(sel);
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
                Add("Directional Light", true, 0);
                Add("Point Light", true, 1);
                Add("Rect Area Light", true, 2);
            }
            else
            {
                Add("Cube", false, 0);
                Add("Triangle", false, 1);
                Add("Ship", false, 2);
            }
            void Add(string text, bool light, int type) =>
                _palette.Items.Add(new PaletteItem { Text = text, IsLight = light, Type = type });
        }

        private void OnPaletteMouseMove(object? sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Left || _dragIndex < 0) return;
            Size d = SystemInformation.DragSize;
            if (Math.Abs(e.X - _dragStart.X) < d.Width && Math.Abs(e.Y - _dragStart.Y) < d.Height) return;

            var item = _palette.Items[_dragIndex] as PaletteItem;
            _dragIndex = -1;
            if (item != null)
                _palette.DoDragDrop($"ghost:{(item.IsLight ? "light" : "obj")}:{item.Type}", DragDropEffects.Copy);
        }

        private static string? DropPayload(DragEventArgs e)
        {
            if (e.Data == null || !e.Data.GetDataPresent(DataFormats.StringFormat)) return null;
            string? s = e.Data.GetData(DataFormats.StringFormat) as string;
            return (s != null && s.StartsWith("ghost:")) ? s : null;
        }

        private void Spawn(bool isLight, int type, int x, int y)
        {
            int id = isLight ? GhostEngine.GhostEngine_AddLight(type, x, y)
                             : GhostEngine.GhostEngine_AddObject(type, x, y);
            if (id == 0) System.Media.SystemSounds.Beep.Play();   // scene is full
            Sync();
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
            for (int i = 0; i < n; i++)
            {
                var node = new TreeNode(GhostNames.Of(buf[i].id))
                {
                    Tag = buf[i].id,
                    ForeColor = buf[i].kind == 1 ? Accent : Fg,   // lights are highlighted
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
            if (GhostEngine.GhostEngine_GetObject(id, ref o) != 0) return o;
            var l = new GhostLightInfo();
            if (GhostEngine.GhostEngine_GetLight(id, ref l) != 0) return l;
            return null;
        }
    }
}