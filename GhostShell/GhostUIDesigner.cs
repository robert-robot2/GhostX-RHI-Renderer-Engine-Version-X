// GhostUIDesigner.cs — the UI Designer panel: palette, hierarchy, canvas, properties, files, undo
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;

namespace GhostShell
{
    // ============================================================
    //  Property grid helpers
    // ============================================================
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class PairEdit
    {
        private readonly Func<(float x, float y)> _get;
        private readonly Action<float, float> _set;
        public PairEdit(Func<(float x, float y)> get, Action<float, float> set) { _get = get; _set = set; }

        public float X { get => _get().x; set => _set(value, _get().y); }
        public float Y { get => _get().y; set => _set(_get().x, value); }
        public override string ToString() { var v = _get(); return $"{v.x:0.##}, {v.y:0.##}"; }
    }

    internal sealed class AnchorConverter : FixedListConverter
    {
        public static readonly string[] Names = DesignDoc.Presets.Select(p => p.Name).Append("Custom").ToArray();
        protected override string[] Items => Names;
    }

    internal sealed class AlignConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Left", "Center", "Right" };
        protected override string[] Items => Names;
    }

    // Image files in the project's Content folder, plus Browse...; typing any path also works
    internal sealed class UITextureConverter : StringConverter
    {
        public const string None = "(none)", Browse = "Browse...";
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => false;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { None };
            try
            {
                string? content = GhostProject.ContentDir, root = GhostProject.Root;
                if (content != null && root != null && Directory.Exists(content))
                    foreach (string f in Directory.EnumerateFiles(content, "*.*", SearchOption.AllDirectories)
                                                  .Where(GhostAssets.IsImage).Take(200))
                        list.Add(Path.GetRelativePath(root, f).Replace('\\', '/'));
            }
            catch { }
            list.Add(Browse);
            return new StandardValuesCollection(list);
        }
    }

    // ============================================================
    //  One proxy class per widget type, so the grid only shows what applies
    // ============================================================
    internal abstract class WidgetProxy
    {
        protected readonly DesignDoc Doc;
        protected readonly DWidget W;

        protected WidgetProxy(DesignDoc doc, DWidget w)
        {
            Doc = doc; W = w;
            Offset = new PairEdit(() => (W.Offset[0], W.Offset[1]),
                (x, y) => { W.Offset[0] = MathF.Round(x, 2); W.Offset[1] = MathF.Round(y, 2); });
            Size = new PairEdit(() => (W.Size[0], W.Size[1]), (x, y) =>
            {
                W.Size[0] = DesignDoc.Stretched(W, 0) ? MathF.Round(x, 2) : MathF.Max(0f, MathF.Round(x, 2));
                W.Size[1] = DesignDoc.Stretched(W, 1) ? MathF.Round(y, 2) : MathF.Max(0f, MathF.Round(y, 2));
            });
            Pivot = new PairEdit(() => (W.Pivot[0], W.Pivot[1]), (x, y) => { W.Pivot[0] = x; W.Pivot[1] = y; });
        }

        protected static Color Rgb(float[] c) =>
            Color.FromArgb((int)Math.Round(Math.Clamp(c[0], 0f, 1f) * 255f),
                           (int)Math.Round(Math.Clamp(c[1], 0f, 1f) * 255f),
                           (int)Math.Round(Math.Clamp(c[2], 0f, 1f) * 255f));
        protected static void SetRgb(float[] c, Color v) { c[0] = v.R / 255f; c[1] = v.G / 255f; c[2] = v.B / 255f; }

        [Category("Details")]
        public string Name { get => W.Name; set => W.Name = Doc.UniqueName(W, value); }

        [Category("Details")]
        public string Type => W.Type;

        [Category("Layout")]
        [TypeConverter(typeof(AnchorConverter))]
        public string Anchor
        {
            get => Doc.PresetName(W);
            set { if (value != "Custom") Doc.ApplyPreset(W, value); }
        }

        [Category("Layout"), DisplayName("Offset (or left/top margin)")]
        public PairEdit Offset { get; }

        [Category("Layout"), DisplayName("Size (or right/bottom margin)")]
        public PairEdit Size { get; }

        [Category("Layout")]
        public PairEdit Pivot { get; }

        [Category("Layout")]
        public bool Visible { get => W.Visible; set => W.Visible = value; }
    }

    internal abstract class ColoredProxy : WidgetProxy
    {
        protected ColoredProxy(DesignDoc d, DWidget w) : base(d, w) { }

        [Category("Appearance"), DisplayName("Color")]
        public Color ColorRgb { get => Rgb(W.Color); set => SetRgb(W.Color, value); }

        [Category("Appearance")]
        public float Opacity { get => W.Color[3]; set => W.Color[3] = Math.Clamp(value, 0f, 1f); }
    }

    internal sealed class CanvasProxy : ColoredProxy
    {
        public CanvasProxy(DesignDoc d, DWidget w) : base(d, w) { }
    }

    internal sealed class ImageProxy : ColoredProxy
    {
        public ImageProxy(DesignDoc d, DWidget w) : base(d, w) { }

        [Category("Image")]
        [TypeConverter(typeof(UITextureConverter))]
        public string Texture
        {
            get => W.Texture.Length == 0 ? UITextureConverter.None : W.Texture;
            set
            {
                if (value == UITextureConverter.Browse) { string? p = BrowseForImage(); if (p != null) W.Texture = p; }
                else W.Texture = value == UITextureConverter.None ? "" : value;
            }
        }

        // Copies the image into Content/ (when a project is open) and returns the path to store
        private static string? BrowseForImage()
        {
            using var dlg = new OpenFileDialog
            {
                Title = "Choose an image",
                Filter = "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp|All files (*.*)|*.*",
            };
            if (dlg.ShowDialog() != DialogResult.OK) return null;
            try
            {
                string local = GhostProject.EnsureInContent(dlg.FileName, null);
                string? root = GhostProject.Root;
                if (root != null)
                {
                    string rel = Path.GetRelativePath(root, local);
                    if (!rel.StartsWith("..") && !Path.IsPathRooted(rel)) return rel.Replace('\\', '/');
                }
                return local;
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Image", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return null;
            }
        }
    }

    internal abstract class LabelProxy : ColoredProxy
    {
        protected LabelProxy(DesignDoc d, DWidget w) : base(d, w) { }

        // type \n for a line break
        [Category("Text")]
        public string Text
        {
            get => W.Text.Replace("\n", "\\n");
            set { string t = value.Replace("\\n", "\n"); W.Text = t.Length > 255 ? t.Substring(0, 255) : t; }
        }

        [Category("Text"), DisplayName("Font Size")]
        public float FontSize { get => W.FontSize; set => W.FontSize = Math.Clamp(value, 4f, 400f); }

        [Category("Text")]
        public bool Shadow { get => W.Shadow; set => W.Shadow = value; }
    }

    internal sealed class TextWidgetProxy : LabelProxy
    {
        public TextWidgetProxy(DesignDoc d, DWidget w) : base(d, w) { }

        [Category("Text")]
        [TypeConverter(typeof(AlignConverter))]
        public string Align
        {
            get => AlignConverter.Names[Math.Clamp(W.Align, 0, 2)];
            set { int i = Array.IndexOf(AlignConverter.Names, value); if (i >= 0) W.Align = i; }
        }
    }

    internal sealed class ButtonProxy : LabelProxy
    {
        public ButtonProxy(DesignDoc d, DWidget w) : base(d, w) { }

        [Category("Text"), DisplayName("Label Color")]
        public Color TextColorRgb { get => Rgb(W.TextColor); set => SetRgb(W.TextColor, value); }
    }

    internal sealed class ProgressProxy : ColoredProxy
    {
        public ProgressProxy(DesignDoc d, DWidget w) : base(d, w) { }

        [Category("Progress")]
        public float Value { get => W.Value; set => W.Value = Math.Clamp(value, 0f, 1f); }

        [Category("Progress"), DisplayName("Background Color")]
        public Color BackgroundColor { get => Rgb(W.BgColor); set => SetRgb(W.BgColor, value); }

        [Category("Progress"), DisplayName("Background Opacity")]
        public float BackgroundOpacity { get => W.BgColor[3]; set => W.BgColor[3] = Math.Clamp(value, 0f, 1f); }
    }

    // ============================================================
    //  The designer panel
    // ============================================================
    internal sealed class GhostUIDesigner : Panel
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
        private static readonly Color Accent = Color.FromArgb(0xFF, 0xC8, 0x6E);
        private const string FileFilter = "GhostX UI (*.ghostui)|*.ghostui";

        private sealed class PalItem
        {
            public string Text = "", Type = "";
            public override string ToString() => Text;
        }

        private static readonly (string Label, float W)[] Aspects =
        {
            ("16:9  (1920 x 1080)", 1920f), ("16:10  (1728 x 1080)", 1728f), ("4:3  (1440 x 1080)", 1440f),
            ("21:9  (2520 x 1080)", 2520f), ("3:2  (1620 x 1080)", 1620f),
        };

        private DesignDoc _doc = new();
        private string? _path;
        private string _committed = "", _saved = "";
        private readonly List<string> _undo = new(), _redo = new();
        private DWidget? _sel;
        private bool _syncing;
        private float _refW = 1920f;

        private readonly UICanvas _canvas = new();
        private readonly TreeView _tree = new();
        private readonly PropertyGrid _grid = new();
        private readonly ListBox _palette = new();
        private readonly Label _title = new();
        private readonly ToolTip _tip = new();
        private readonly Dictionary<DWidget, TreeNode> _nodes = new();
        private Button _bUndo = null!, _bRedo = null!, _bDup = null!, _bDel = null!, _bBack = null!, _bFwd = null!;

        private int _palIndex = -1;
        private Point _palStart;

        public bool IsDirty => _committed != _saved;
        private string DocName => _path == null ? "Untitled UI" : Path.GetFileName(_path);

        public GhostUIDesigner()
        {
            BackColor = Bg;

            // ---- toolbar ----
            var bar = new FlowLayoutPanel
            {
                Dock = DockStyle.Top,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                WrapContents = true,
                BackColor = Bg,
                Padding = new Padding(6, 6, 6, 4),
            };
            bar.Controls.Add(Btn("New", NewDoc, "New UI"));
            bar.Controls.Add(Btn("Open...", OpenDialog, "Open a .ghostui file"));
            bar.Controls.Add(Btn("Save", () => Save(), "Save (Ctrl+S while this window has focus)"));
            bar.Controls.Add(Btn("Save As...", () => SaveAs(), "Save under a new name"));
            bar.Controls.Add(_bUndo = Btn("Undo", Undo, "Ctrl+Z"));
            bar.Controls.Add(_bRedo = Btn("Redo", Redo, "Ctrl+Y"));
            bar.Controls.Add(_bDup = Btn("Duplicate", DuplicateSelected, "Ctrl+D"));
            bar.Controls.Add(_bDel = Btn("Delete", DeleteSelected, "Delete key"));
            bar.Controls.Add(_bBack = Btn("Send Back", () => MoveSelected(-1), "Draw earlier (underneath its siblings)"));
            bar.Controls.Add(_bFwd = Btn("Bring Forward", () => MoveSelected(1), "Draw later (on top of its siblings)"));

            var aspect = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg,
                Width = 170,
                Margin = new Padding(8, 2, 6, 0),
            };
            foreach (var a in Aspects) aspect.Items.Add(a.Label);
            aspect.SelectedIndex = 0;
            aspect.SelectedIndexChanged += (s, e) =>
            {
                _refW = Aspects[Math.Max(aspect.SelectedIndex, 0)].W;
                _doc.RefW = _refW;
                _canvas.Invalidate();
            };
            bar.Controls.Add(aspect);

            _title.AutoSize = true;
            _title.ForeColor = Accent;
            _title.Margin = new Padding(8, 6, 0, 0);
            bar.Controls.Add(_title);

            // ---- left: palette + hierarchy ----
            var left = new Panel { Dock = DockStyle.Left, Width = 230, BackColor = Bg };
            _tree.Dock = DockStyle.Fill;
            _tree.BackColor = Bg2; _tree.ForeColor = Fg; _tree.BorderStyle = BorderStyle.None;
            _tree.HideSelection = false; _tree.FullRowSelect = true; _tree.ShowLines = false;
            _tree.Font = new Font("Segoe UI", 10f);
            _tree.AllowDrop = true;

            var vsplit = new Splitter { Dock = DockStyle.Top, Height = 5, BackColor = Bg2 };

            _palette.Dock = DockStyle.Top; _palette.Height = 128;
            _palette.BackColor = Bg2; _palette.ForeColor = Fg; _palette.BorderStyle = BorderStyle.None;
            _palette.IntegralHeight = false; _palette.Font = new Font("Segoe UI", 10.5f);
            _palette.Items.Add(new PalItem { Text = "Canvas", Type = "Canvas" });
            _palette.Items.Add(new PalItem { Text = "Image", Type = "Image" });
            _palette.Items.Add(new PalItem { Text = "Text", Type = "Text" });
            _palette.Items.Add(new PalItem { Text = "Button", Type = "Button" });
            _palette.Items.Add(new PalItem { Text = "Progress Bar", Type = "ProgressBar" });

            var palHeader = new Label
            {
                Dock = DockStyle.Top,
                Height = 40,
                ForeColor = Color.Gray,
                TextAlign = ContentAlignment.MiddleLeft,
                Text = "Widgets: drag onto the canvas,\nor double-click to add",
            };
            // dock order: Fill first, then the Top controls, last-added ends up topmost
            left.Controls.Add(_tree);
            left.Controls.Add(vsplit);
            left.Controls.Add(_palette);
            left.Controls.Add(palHeader);

            // ---- right: properties ----
            var right = new Panel { Dock = DockStyle.Right, Width = 310, BackColor = Bg };
            _grid.Dock = DockStyle.Fill;
            _grid.ToolbarVisible = false; _grid.HelpVisible = false; _grid.PropertySort = PropertySort.Categorized;
            _grid.BackColor = Bg; _grid.ViewBackColor = Bg2; _grid.ViewForeColor = Fg;
            _grid.LineColor = Color.FromArgb(0x3A, 0x3A, 0x40); _grid.CategoryForeColor = Accent;
            _grid.DisabledItemForeColor = Color.Gray; _grid.HelpBackColor = Bg; _grid.HelpForeColor = Fg;
            _grid.CommandsBackColor = Bg; _grid.CommandsForeColor = Fg;
            right.Controls.Add(_grid);

            var splitL = new Splitter { Dock = DockStyle.Left, Width = 5, BackColor = Bg2 };
            var splitR = new Splitter { Dock = DockStyle.Right, Width = 5, BackColor = Bg2 };

            _canvas.Dock = DockStyle.Fill;
            // dock order: Fill first, then Right/Left edges (last-added is outermost), toolbar last so it spans the top
            Controls.Add(_canvas);
            Controls.Add(splitR);
            Controls.Add(right);
            Controls.Add(splitL);
            Controls.Add(left);
            Controls.Add(bar);

            WireEvents();
            SetDoc(new DesignDoc { RefW = _refW }, null);
        }

        private Button Btn(string text, Action click, string tip)
        {
            var b = new Button
            {
                Text = text,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                FlatStyle = FlatStyle.Flat,
                ForeColor = Fg,
                BackColor = Bg2,
                Margin = new Padding(0, 0, 6, 4),
                Padding = new Padding(6, 0, 6, 0),
                MinimumSize = new Size(0, 28),
            };
            b.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
            b.Click += (s, e) => click();
            _tip.SetToolTip(b, tip);
            return b;
        }

        public void FocusCanvas() => _canvas.Focus();

        // -------------------------------------------------------
        //  Events
        // -------------------------------------------------------
        private void WireEvents()
        {
            _canvas.SelectionChanged += w => SelectWidget(w);
            _canvas.LiveChanged += () => _grid.Refresh();
            _canvas.Committed += () => { Commit(); _grid.Refresh(); };
            _canvas.WidgetDropped += (type, at, parent) => AddWidget(type, at, parent);
            _canvas.KeyDown += OnKey;

            _tree.KeyDown += OnKey;
            _tree.AfterSelect += (s, e) => { if (!_syncing) SelectWidget(e.Node?.Tag as DWidget); };
            _tree.ItemDrag += (s, e) => { if (e.Item is TreeNode) _tree.DoDragDrop(e.Item, DragDropEffects.Move); };
            _tree.DragEnter += TreeDrag;
            _tree.DragOver += TreeDrag;
            _tree.DragDrop += TreeDrop;

            _grid.PropertyValueChanged += (s, e) => AfterPropertyEdit();

            _palette.MouseDown += (s, e) => { _palIndex = _palette.IndexFromPoint(e.Location); _palStart = e.Location; };
            _palette.MouseMove += (s, e) =>
            {
                if (e.Button != MouseButtons.Left || _palIndex < 0) return;
                Size d = SystemInformation.DragSize;
                if (Math.Abs(e.X - _palStart.X) < d.Width && Math.Abs(e.Y - _palStart.Y) < d.Height) return;
                var item = _palette.Items[_palIndex] as PalItem;
                _palIndex = -1;
                if (item != null) _palette.DoDragDrop("ghostui:" + item.Type, DragDropEffects.Copy);
            };
            _palette.DoubleClick += (s, e) =>
            {
                if (_palette.SelectedItem is PalItem it)
                    AddWidget(it.Type, null, _sel == null ? null : (_sel.Type == "Canvas" ? _sel : _sel.Parent));
            };
        }

        private void OnKey(object? sender, KeyEventArgs e)
        {
            bool handled = true;
            if (e.Control && e.KeyCode == Keys.Z) Undo();
            else if (e.Control && e.KeyCode == Keys.Y) Redo();
            else if (e.Control && e.KeyCode == Keys.D) DuplicateSelected();
            else if (e.KeyCode == Keys.Delete) DeleteSelected();
            else if (sender == _canvas && _sel != null &&
                     (e.KeyCode == Keys.Left || e.KeyCode == Keys.Right || e.KeyCode == Keys.Up || e.KeyCode == Keys.Down))
            {
                int step = e.Shift ? 10 : 1;
                _canvas.Nudge(e.KeyCode == Keys.Left ? -step : e.KeyCode == Keys.Right ? step : 0,
                              e.KeyCode == Keys.Up ? -step : e.KeyCode == Keys.Down ? step : 0);
                _grid.Refresh();
                Commit();
            }
            else handled = false;

            if (handled) { e.Handled = true; e.SuppressKeyPress = true; }
        }

        private void TreeDrag(object? s, DragEventArgs e) =>
            e.Effect = e.Data?.GetDataPresent(typeof(TreeNode)) == true ? DragDropEffects.Move : DragDropEffects.None;

        private void TreeDrop(object? s, DragEventArgs e)
        {
            if (e.Data?.GetData(typeof(TreeNode)) is not TreeNode src || src.Tag is not DWidget w) return;
            TreeNode? target = _tree.GetNodeAt(_tree.PointToClient(new Point(e.X, e.Y)));

            DWidget? np = null, after = null;
            if (target?.Tag is DWidget t)
            {
                if (t.Type == "Canvas") np = t;              // drop onto a Canvas: becomes its last child
                else { np = t.Parent; after = t; }           // drop onto anything else: becomes the next sibling
            }
            if (!_doc.Reparent(w, np, after)) { System.Media.SystemSounds.Beep.Play(); return; }

            RebuildTree();
            SelectWidget(w);
            Commit();
            _canvas.Invalidate();
        }

        // -------------------------------------------------------
        //  Selection, tree, properties
        // -------------------------------------------------------
        private static string NodeText(DWidget w) => $"{w.Name}   [{w.Type}]";

        private void RebuildTree()
        {
            _syncing = true;
            _tree.BeginUpdate();
            _tree.Nodes.Clear();
            _nodes.Clear();

            void Add(DWidget w, TreeNodeCollection into)
            {
                var n = new TreeNode(NodeText(w)) { Tag = w, ForeColor = w.Visible ? Fg : Color.Gray };
                into.Add(n);
                _nodes[w] = n;
                foreach (DWidget c in w.Children) Add(c, n.Nodes);
            }
            foreach (DWidget r in _doc.Roots) Add(r, _tree.Nodes);

            _tree.ExpandAll();
            _tree.EndUpdate();
            if (_sel != null && _nodes.TryGetValue(_sel, out TreeNode? sn)) _tree.SelectedNode = sn;
            _syncing = false;
        }

        private WidgetProxy MakeProxy(DWidget w) => w.Type switch
        {
            "Image" => new ImageProxy(_doc, w),
            "Text" => new TextWidgetProxy(_doc, w),
            "Button" => new ButtonProxy(_doc, w),
            "ProgressBar" => new ProgressProxy(_doc, w),
            _ => new CanvasProxy(_doc, w),
        };

        private void SelectWidget(DWidget? w)
        {
            _sel = w;
            _canvas.Selected = w;
            _syncing = true;
            _tree.SelectedNode = (w != null && _nodes.TryGetValue(w, out TreeNode? n)) ? n : null;
            _syncing = false;
            _grid.SelectedObject = w == null ? null : MakeProxy(w);
            UpdateButtons();
        }

        private void AfterPropertyEdit()
        {
            if (_sel != null && _nodes.TryGetValue(_sel, out TreeNode? n))
            {
                n.Text = NodeText(_sel);
                n.ForeColor = _sel.Visible ? Fg : Color.Gray;
            }
            _canvas.Invalidate();
            Commit();
            _grid.Refresh();
        }

        private void UpdateButtons()
        {
            bool has = _sel != null;
            _bDup.Enabled = _bDel.Enabled = _bBack.Enabled = _bFwd.Enabled = has;
            _bUndo.Enabled = _undo.Count > 0;
            _bRedo.Enabled = _redo.Count > 0;
        }

        private void UpdateTitle()
        {
            _title.Text = DocName + (IsDirty ? "*" : "");
            if (Parent is DockContent dc) dc.TabText = "UI Designer - " + _title.Text;
        }

        // -------------------------------------------------------
        //  Undo: every committed state is a JSON snapshot
        // -------------------------------------------------------
        private void Commit()
        {
            string now = _doc.ToJson(false);
            if (now != _committed)
            {
                _undo.Add(_committed);
                if (_undo.Count > 60) _undo.RemoveAt(0);
                _redo.Clear();
                _committed = now;
            }
            UpdateTitle();
            UpdateButtons();
        }

        private void RestoreSnapshot(string json)
        {
            int index = _sel == null ? -1 : _doc.All().ToList().IndexOf(_sel);
            DesignDoc d = DesignDoc.Parse(json, out _);
            d.Name = _doc.Name;
            d.RefW = _refW;
            _doc = d;
            _canvas.Doc = d;
            _committed = json;

            List<DWidget> all = d.All().ToList();
            _sel = (index >= 0 && index < all.Count) ? all[index] : null;
            RebuildTree();
            SelectWidget(_sel);
            UpdateTitle();
        }

        private void Undo()
        {
            if (_undo.Count == 0) return;
            _redo.Add(_committed);
            string prev = _undo[^1];
            _undo.RemoveAt(_undo.Count - 1);
            RestoreSnapshot(prev);
        }

        private void Redo()
        {
            if (_redo.Count == 0) return;
            _undo.Add(_committed);
            string next = _redo[^1];
            _redo.RemoveAt(_redo.Count - 1);
            RestoreSnapshot(next);
        }

        // -------------------------------------------------------
        //  Editing
        // -------------------------------------------------------
        private static int Depth(DWidget? w) { int d = 0; for (DWidget? p = w; p != null; p = p.Parent) d++; return d; }

        private void AddWidget(string type, PointF? at, DWidget? parent)
        {
            if (_doc.WidgetCount >= DesignDoc.MaxWidgets || Depth(parent) >= 11)
            { System.Media.SystemSounds.Beep.Play(); return; }

            DWidget w = DesignDoc.NewForDrop(type);
            w.Name = _doc.UniqueName(null, type);
            _doc.Layout();

            float px = parent?.X ?? 0f, py = parent?.Y ?? 0f;
            float pw = parent?.W ?? _doc.RefW, ph = parent?.H ?? 1080f;
            float cx = at?.X ?? (px + pw / 2f), cy = at?.Y ?? (py + ph / 2f);
            w.Offset[0] = MathF.Round(cx - px - w.Size[0] / 2f);
            w.Offset[1] = MathF.Round(cy - py - w.Size[1] / 2f);

            w.Parent = parent;
            (parent == null ? _doc.Roots : parent.Children).Add(w);

            RebuildTree();
            SelectWidget(w);
            Commit();
            _canvas.Invalidate();
        }

        private void DeleteSelected()
        {
            if (_sel == null) return;
            _doc.Remove(_sel);
            RebuildTree();
            SelectWidget(null);
            Commit();
            _canvas.Invalidate();
        }

        private void DuplicateSelected()
        {
            if (_sel == null || _doc.WidgetCount + DesignDoc.Walk(_sel).Count() > DesignDoc.MaxWidgets)
            { System.Media.SystemSounds.Beep.Play(); return; }
            DWidget c = _doc.Duplicate(_sel);
            RebuildTree();
            SelectWidget(c);
            Commit();
            _canvas.Invalidate();
        }

        private void MoveSelected(int dir)
        {
            if (_sel == null || !_doc.Move(_sel, dir)) return;
            RebuildTree();
            Commit();
            _canvas.Invalidate();
        }

        // -------------------------------------------------------
        //  Files
        // -------------------------------------------------------
        private void SetDoc(DesignDoc d, string? path)
        {
            d.RefW = _refW;
            _doc = d;
            _path = path;
            _canvas.Doc = d;
            _undo.Clear();
            _redo.Clear();
            _committed = _saved = d.ToJson(false);
            _sel = null;
            RebuildTree();
            SelectWidget(null);
            UpdateTitle();
        }

        private void NewDoc()
        {
            if (!ConfirmDiscard(FindForm())) return;
            SetDoc(new DesignDoc(), null);
        }

        private string? UiFolder()
        {
            string? content = GhostProject.ContentDir;
            if (content == null) return null;
            string dir = Path.Combine(content, "UI");
            try { Directory.CreateDirectory(dir); } catch { return null; }
            return dir;
        }

        private void OpenDialog()
        {
            using var dlg = new OpenFileDialog { Title = "Open UI", Filter = FileFilter };
            string? dir = UiFolder();
            if (dir != null) dlg.InitialDirectory = dir;
            if (dlg.ShowDialog(FindForm()) == DialogResult.OK) OpenFile(dlg.FileName);
        }

        public void OpenFile(string path)
        {
            if (!ConfirmDiscard(FindForm())) return;
            try
            {
                DesignDoc d = DesignDoc.Load(path, out string warn);
                SetDoc(d, path);
                if (warn.Length > 0)
                    MessageBox.Show(FindForm(), warn, "UI opened with warnings", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show(FindForm(), ex.Message, "Could not open UI", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        public bool Save() => _path == null ? SaveAs() : WriteTo(_path);

        public bool SaveAs()
        {
            using var dlg = new SaveFileDialog
            {
                Title = "Save UI As",
                Filter = FileFilter,
                DefaultExt = "ghostui",
                AddExtension = true,
                FileName = _path != null ? Path.GetFileNameWithoutExtension(_path) : "NewUI",
            };
            string? dir = UiFolder();
            if (dir != null) dlg.InitialDirectory = dir;
            return dlg.ShowDialog(FindForm()) == DialogResult.OK && WriteTo(dlg.FileName);
        }

        private bool WriteTo(string path)
        {
            try
            {
                _doc.Name = Path.GetFileNameWithoutExtension(path);
                _doc.SaveTo(path);
            }
            catch (Exception ex)
            {
                MessageBox.Show(FindForm(), ex.Message, "Save failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            _path = path;
            Commit();
            _saved = _committed;
            UpdateTitle();
            return true;
        }

        // true = safe to continue (saved, discarded, or nothing to save)
        public bool ConfirmDiscard(IWin32Window? owner)
        {
            if (!IsDirty) return true;
            DialogResult r = MessageBox.Show(owner, $"Save changes to {DocName}?", "UI Designer",
                MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
            if (r == DialogResult.Cancel) return false;
            return r == DialogResult.No || Save();
        }
    }
}