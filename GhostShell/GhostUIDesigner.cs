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
    // .ttf / .otf files in the project's Content folder, plus Browse...
    internal sealed class UIFontConverter : StringConverter
    {
        public const string Default = "(default)", Browse = "Browse...";
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => false;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { Default };
            try
            {
                string? content = GhostProject.ContentDir, root = GhostProject.Root;
                if (content != null && root != null && Directory.Exists(content))
                    foreach (string f in Directory.EnumerateFiles(content, "*.*", SearchOption.AllDirectories)
                                 .Where(p => p.EndsWith(".ttf", StringComparison.OrdinalIgnoreCase) ||
                                             p.EndsWith(".otf", StringComparison.OrdinalIgnoreCase)).Take(200))
                        list.Add(Path.GetRelativePath(root, f).Replace('\\', '/'));
            }
            catch { }
            list.Add(Browse);
            return new StandardValuesCollection(list);
        }
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
        [Category("Text"), DisplayName("Font")]
        [TypeConverter(typeof(UIFontConverter))]
        public string FontFile
        {
            get => W.Font.Length == 0 ? UIFontConverter.Default : W.Font;
            set
            {
                if (value == UIFontConverter.Browse) { string? p = BrowseForFont(); if (p != null) W.Font = p; }
                else W.Font = value == UIFontConverter.Default ? "" : value;
            }
        }

        private static string? BrowseForFont()
        {
            using var dlg = new OpenFileDialog
            {
                Title = "Choose a font",
                Filter = "Fonts (*.ttf;*.otf)|*.ttf;*.otf",
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
                MessageBox.Show(ex.Message, "Font", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return null;
            }
        }
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

        private static readonly (string Label, float W, float H)[] Previews =
{
            ("1280 x 720  (16:9)", 1280f, 720f), ("1920 x 1080  (16:9)", 1920f, 1080f), ("2560 x 1440  (16:9)", 2560f, 1440f),
            ("3440 x 1440  (21:9)", 3440f, 1440f), ("1024 x 768  (4:3)", 1024f, 768f), ("1280 x 800  (16:10)", 1280f, 800f),
            ("2400 x 1080  (20:9 phone)", 2400f, 1080f), ("1080 x 1920  (9:16 portrait)", 1080f, 1920f),
        };
        private static readonly float[] SafeSteps = { 0f, 0.025f, 0.05f, 0.075f, 0.1f };

        private DesignDoc _doc = new();
        private string? _path;
        private string _committed = "", _saved = "";
        private readonly List<string> _undo = new(), _redo = new();
        private DWidget? _sel;
        private bool _syncing;
        private int _previewIdx = 1;
        private bool _uiSync;
        private ComboBox _cRef = null!, _cMode = null!, _cPrev = null!, _cSafe = null!;
        private float PreviewW => 1080f * Previews[_previewIdx].W / Previews[_previewIdx].H;
        private static DesignDoc NewBlankDoc() => new DesignDoc { RefW = 1920f, ScaleMode = 1 };   // new UIs start in Expand

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

            bar.Controls.Add(Btn("Auto-Anchor All", AutoAnchorAll, "Re-anchor every top-left widget to its nearest edge or center"));
            _previewIdx = Math.Clamp(GhostSettings.Data.UiPreview, 0, Previews.Length - 1);
            bar.Controls.Add(Lbl("Reference"));
            bar.Controls.Add(_cRef = MakeCombo(Aspects.Select(a => a.Label), 150, "The width this UI was designed for"));
            bar.Controls.Add(Lbl("Scale"));
            bar.Controls.Add(_cMode = MakeCombo(UIView.ModeNames, 170, "How the UI scales to the screen"));
            bar.Controls.Add(Lbl("Safe"));
            bar.Controls.Add(_cSafe = MakeCombo(SafeSteps.Select(s => $"{s * 100f:0.#}%"), 70, "Safe-area margin on every side (TVs, handhelds)"));
            bar.Controls.Add(Lbl("Preview"));
            bar.Controls.Add(_cPrev = MakeCombo(Previews.Select(p => p.Label), 190, "Screen size shown in the canvas (editor only)"));
            var guides = new CheckBox { Text = "Guides", AutoSize = true, ForeColor = Fg, Margin = new Padding(6, 6, 6, 0) };
            _tip.SetToolTip(guides, "Show the design frame and 4:3, 16:10, 16:9, 21:9 and 9:16 aspect guides");
            guides.CheckedChanged += (s, e) => _canvas.ShowGuides = guides.Checked;
            bar.Controls.Add(guides);

            _uiSync = true;
            _cPrev.SelectedIndex = _previewIdx;
            _uiSync = false;
            _cRef.SelectedIndexChanged += (s, e) => { if (_uiSync) return; _doc.RefW = Aspects[Math.Max(_cRef.SelectedIndex, 0)].W; DocSettingChanged(); };
            _cMode.SelectedIndexChanged += (s, e) => { if (_uiSync) return; _doc.ScaleMode = Math.Max(_cMode.SelectedIndex, 0); DocSettingChanged(); };
            _cSafe.SelectedIndexChanged += (s, e) => { if (_uiSync) return; _doc.SafeArea = SafeSteps[Math.Max(_cSafe.SelectedIndex, 0)]; DocSettingChanged(); };
            _cPrev.SelectedIndexChanged += (s, e) =>
            {
                if (_uiSync) return;
                _previewIdx = Math.Max(_cPrev.SelectedIndex, 0);
                _doc.PreviewW = PreviewW;
                GhostSettings.Data.UiPreview = _previewIdx;      // the preview is a view setting: it never dirties the file
                GhostSettings.Save();
                _canvas.Invalidate();
            };

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
            SetDoc(NewBlankDoc(), null);
        }

        private Label Lbl(string t) => new Label { Text = t, AutoSize = true, ForeColor = Color.Gray, Margin = new Padding(8, 7, 2, 0) };

        private ComboBox MakeCombo(IEnumerable<string> items, int width, string tip)
        {
            var c = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg,
                Width = width,
                Margin = new Padding(2, 2, 6, 0),
            };
            foreach (string s in items) c.Items.Add(s);
            c.SelectedIndex = 0;
            _tip.SetToolTip(c, tip);
            return c;
        }

        private void DocSettingChanged() { _canvas.Invalidate(); Commit(); _grid.Refresh(); }

        // Shows the document's saved settings in the toolbar without firing the change handlers
        private void SyncToolbar()
        {
            _uiSync = true;
            int r = 0; float best = float.MaxValue;
            for (int i = 0; i < Aspects.Length; i++)
            {
                float d = Math.Abs(Aspects[i].W - _doc.RefW);
                if (d < best) { best = d; r = i; }
            }
            _cRef.SelectedIndex = r;
            _cMode.SelectedIndex = Math.Clamp(_doc.ScaleMode, 0, UIView.ModeNames.Length - 1);
            int sidx = 0; best = float.MaxValue;
            for (int i = 0; i < SafeSteps.Length; i++)
            {
                float d = Math.Abs(SafeSteps[i] - _doc.SafeArea);
                if (d < best) { best = d; sidx = i; }
            }
            _cSafe.SelectedIndex = sidx;
            _uiSync = false;
        }

        // Where does this widget sit inside its parent? Pick the nearest of the 9 anchors and keep it in place.
        private void AutoAnchor(DWidget w)
        {
            _doc.Layout();
            var pr = _doc.ParentRect(w.Parent);
            float fx = (w.X + w.W / 2f - pr.x) / MathF.Max(pr.w, 1f);
            float fy = (w.Y + w.H / 2f - pr.y) / MathF.Max(pr.h, 1f);
            int col = fx < 0.33f ? 0 : (fx > 0.67f ? 2 : 1), row = fy < 0.33f ? 0 : (fy > 0.67f ? 2 : 1);
            string[,] names =
            {
                { "TopLeft", "TopCenter", "TopRight" },
                { "CenterLeft", "Center", "CenterRight" },
                { "BottomLeft", "BottomCenter", "BottomRight" },
            };
            _doc.ApplyPreset(w, names[row, col]);
        }

        // Fixes an old file: every plain top-left widget gets the anchor it should have had
        private void AutoAnchorAll()
        {
            var targets = _doc.All().Where(w => !DesignDoc.Stretched(w, 0) && !DesignDoc.Stretched(w, 1)
                                                && w.AnchorMin[0] == 0f && w.AnchorMin[1] == 0f).ToList();
            if (targets.Count == 0) { System.Media.SystemSounds.Beep.Play(); return; }
            if (MessageBox.Show(FindForm(),
                    $"Re-anchor {targets.Count} top-left widget(s) to their nearest edge or center?\nThey stay exactly where they are at the reference size.",
                    "Auto-Anchor All", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;

            float keep = _doc.PreviewW;
            _doc.PreviewW = _doc.RefW;                    // judge positions against the design frame, whatever the preview is
            foreach (DWidget w in targets) AutoAnchor(w);
            _doc.PreviewW = keep;

            RebuildTree();
            SelectWidget(_sel);
            Commit();
            _canvas.Invalidate();
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
            d.PreviewW = PreviewW;
            _doc = d;
            _canvas.Doc = d;
            SyncToolbar();
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

            var pr = _doc.ParentRect(parent);
            float cx = at?.X ?? (pr.x + pr.w / 2f), cy = at?.Y ?? (pr.y + pr.h / 2f);
            w.Offset[0] = MathF.Round(cx - pr.x - w.Size[0] / 2f);
            w.Offset[1] = MathF.Round(cy - pr.y - w.Size[1] / 2f);

            w.Parent = parent;
            (parent == null ? _doc.Roots : parent.Children).Add(w);
            AutoAnchor(w);                                   // NEW: corners hug edges, middles stay centered          

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
            d.PreviewW = PreviewW;
            _doc = d;
            _path = path;
            _canvas.Doc = d;
            SyncToolbar();
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
            SetDoc(NewBlankDoc(), null);
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