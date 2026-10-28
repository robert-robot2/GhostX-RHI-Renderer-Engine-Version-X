// GhostAnimEditor.cs — Animation Editor: per-clip texture, grid, clips, frames, live preview, files
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;
using System.ComponentModel;

namespace GhostShell
{
    // ============================================================
    //  Sheet canvas — image + grid + clip highlights
    // ============================================================
    internal sealed class AnimSheetCanvas : Control
    {
        private static readonly Color Accent = Color.FromArgb(0xFF, 0x9F, 0x1C);

        private AnimDoc? _doc;
        private Bitmap? _sheet;
        private AnimClipDoc? _clip;
        private int _playFrame = -1, _hover = -1, _dragFrom = -1;
        private readonly Font _small = new("Segoe UI", 8f);
        private readonly StringFormat _far = new()
        { Alignment = StringAlignment.Far, LineAlignment = StringAlignment.Far };

        public event Action<int, int>? FramesPicked;

        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public AnimDoc? Doc { get => _doc; set { _doc = value; Invalidate(); } }
        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public Bitmap? Sheet { get => _sheet; set { _sheet = value; Invalidate(); } }
        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public AnimClipDoc? Clip { get => _clip; set { _clip = value; Invalidate(); } }
        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public int PlayFrame { get => _playFrame; set { _playFrame = value; Invalidate(); } }

        public AnimSheetCanvas()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw |
                     ControlStyles.Selectable, true);
            BackColor = Color.FromArgb(0x1E, 0x1E, 0x1E);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) { _small.Dispose(); _far.Dispose(); }
            base.Dispose(disposing);
        }

        private RectangleF ImageRect()
        {
            float iw = _sheet!.Width, ih = _sheet.Height;
            const float m = 24f;
            float s = Math.Min((Width - 2 * m) / iw, (Height - 2 * m) / ih);
            if (s <= 0f) s = 0.01f;
            float w = iw * s, h = ih * s;
            return new RectangleF((Width - w) / 2f, (Height - h) / 2f, w, h);
        }

        private int CellAt(Point p, bool clamp)
        {
            if (_sheet == null || _clip == null) return -1;
            RectangleF r = ImageRect();
            float u = (p.X - r.X) / r.Width, v = (p.Y - r.Y) / r.Height;
            if (!clamp && (u < 0f || u >= 1f || v < 0f || v >= 1f)) return -1;
            u = Math.Clamp(u, 0f, 0.9999f);
            v = Math.Clamp(v, 0f, 0.9999f);
            return (int)(v * _clip.Rows) * _clip.Cols + (int)(u * _clip.Cols);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(BackColor);
            if (_sheet == null || _clip == null)
            {
                using var f = new Font("Segoe UI", 11f);
                using var b = new SolidBrush(Color.FromArgb(0x99, 0x99, 0xA0));
                const string hint = "Choose an image (Browse...) to start";
                SizeF sz = g.MeasureString(hint, f);
                g.DrawString(hint, f, b, (Width - sz.Width) / 2f, (Height - sz.Height) / 2f);
                return;
            }

            RectangleF r = ImageRect();
            using (var hb = new HatchBrush(HatchStyle.LargeCheckerBoard,
                       Color.FromArgb(0x3C, 0x3C, 0x42), Color.FromArgb(0x2C, 0x2C, 0x32)))
                g.FillRectangle(hb, r);
            g.InterpolationMode = InterpolationMode.NearestNeighbor;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            g.DrawImage(_sheet, r.X, r.Y, r.Width, r.Height);
            g.PixelOffsetMode = PixelOffsetMode.Default;

            int cols = _clip.Cols, rows = _clip.Rows;
            float cw = r.Width / cols, ch = r.Height / rows;
            RectangleF Cell(int f) =>
                new(r.X + (f % cols) * cw, r.Y + (f / cols) * ch, cw, ch);

            var labels = new Dictionary<int, string>();
            for (int i = 0; i < _clip.Frames.Count; i++)
            {
                int f = _clip.Frames[i];
                if (f < 0 || f >= cols * rows) continue;
                labels[f] = labels.TryGetValue(f, out string? old)
                    ? old + "," + (i + 1) : (i + 1).ToString();
            }
            using (var tint = new SolidBrush(Color.FromArgb(70, Accent)))
                foreach (int f in labels.Keys) g.FillRectangle(tint, Cell(f));

            if (_dragFrom >= 0 && _hover >= 0)
                using (var drag = new SolidBrush(Color.FromArgb(60, 255, 255, 255)))
                    for (int f = Math.Min(_dragFrom, _hover); f <= Math.Max(_dragFrom, _hover); f++)
                        g.FillRectangle(drag, Cell(f));

            using (var grid = new Pen(Color.FromArgb(130, 255, 255, 255), 1f))
            {
                for (int c = 0; c <= cols; c++)
                    g.DrawLine(grid, r.X + c * cw, r.Y, r.X + c * cw, r.Bottom);
                for (int rr = 0; rr <= rows; rr++)
                    g.DrawLine(grid, r.X, r.Y + rr * ch, r.Right, r.Y + rr * ch);
            }

            if (cw >= 30f && ch >= 18f)
            {
                g.TextRenderingHint = TextRenderingHint.AntiAlias;
                using var gray = new SolidBrush(Color.FromArgb(210, 255, 255, 255));
                using var shadow = new SolidBrush(Color.FromArgb(160, 0, 0, 0));
                using var orange = new SolidBrush(Accent);
                for (int f = 0; f < cols * rows; f++)
                {
                    RectangleF c = Cell(f);
                    g.DrawString(f.ToString(), _small, shadow, c.X + 3f, c.Y + 2f);
                    g.DrawString(f.ToString(), _small, gray, c.X + 2f, c.Y + 1f);
                    if (labels.TryGetValue(f, out string? lab))
                        g.DrawString(lab, _small, orange,
                            new RectangleF(c.X, c.Y, c.Width - 2f, c.Height - 1f), _far);
                }
            }

            if (_playFrame >= 0 && _playFrame < cols * rows)
                using (var pen = new Pen(Color.White, 2f))
                    g.DrawRectangle(pen, Rectangle.Round(Cell(_playFrame)));
            if (_hover >= 0 && _hover < cols * rows)
                using (var pen = new Pen(Color.Yellow, 1f))
                    g.DrawRectangle(pen, Rectangle.Round(Cell(_hover)));
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e); Focus();
            if (e.Button == MouseButtons.Left) _dragFrom = CellAt(e.Location, false);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            int h = CellAt(e.Location, _dragFrom >= 0);
            if (h != _hover) { _hover = h; Invalidate(); }
            Cursor = _hover >= 0 ? Cursors.Hand : Cursors.Default;
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            if (e.Button != MouseButtons.Left || _dragFrom < 0) return;
            int from = _dragFrom, to = CellAt(e.Location, true);
            _dragFrom = -1; Invalidate();
            if (to >= 0) FramesPicked?.Invoke(from, to);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            if (_dragFrom < 0 && _hover != -1) { _hover = -1; Invalidate(); }
        }
    }

    // ============================================================
    //  Live preview
    // ============================================================
    internal sealed class AnimPreview : Control
    {
        private Bitmap? _sheet;
        private AnimClipDoc? _clip;
        private bool _flip;
        private double _time, _last;
        private readonly Stopwatch _sw = Stopwatch.StartNew();
        private readonly System.Windows.Forms.Timer _timer = new() { Interval = 33 };

        public int CurrentFrame { get; private set; } = -1;
        public event Action<int>? FrameChanged;

        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public bool Playing { get; set; } = true;
        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public Bitmap? Sheet { get => _sheet; set { _sheet = value; Invalidate(); } }
        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public AnimClipDoc? Clip { get => _clip; set { _clip = value; } }
        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public bool Flip { get => _flip; set { _flip = value; Invalidate(); } }

        public AnimPreview()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            BackColor = Color.FromArgb(0x1A, 0x1A, 0x1E);
            _last = _sw.Elapsed.TotalSeconds;
            _timer.Tick += (s, e) => Step();
            _timer.Start();
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) _timer.Dispose();
            base.Dispose(disposing);
        }

        public void Restart() { _time = 0; Step(); }

        private void Step()
        {
            double now = _sw.Elapsed.TotalSeconds, dt = now - _last;
            _last = now;
            if (Playing) _time += dt;

            int f = -1;
            if (_clip != null && _clip.Frames.Count > 0)
            {
                int n = _clip.Frames.Count;
                int i = (int)Math.Floor(_time * _clip.Fps);
                if (_clip.Loop) i %= n; else if (i >= n) i = n - 1;
                f = _clip.Frames[Math.Max(i, 0)];
            }
            if (f != CurrentFrame) { CurrentFrame = f; FrameChanged?.Invoke(f); Invalidate(); }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(BackColor);
            using (var hb = new HatchBrush(HatchStyle.LargeCheckerBoard,
                       Color.FromArgb(0x30, 0x30, 0x36), Color.FromArgb(0x26, 0x26, 0x2B)))
                g.FillRectangle(hb, 0, 0, Width, Height);

            int fc = _clip != null ? AnimDoc.FrameCount(_clip) : 0;
            if (_sheet == null || _clip == null || CurrentFrame < 0 || CurrentFrame >= fc)
            {
                using var f = new Font("Segoe UI", 9.5f);
                using var b = new SolidBrush(Color.FromArgb(0x99, 0x99, 0xA0));
                g.DrawString(_clip == null ? "No clip selected" : "Clip has no frames", f, b, 10f, 10f);
                return;
            }

            float fw = _sheet.Width / (float)_clip.Cols;
            float fh = _sheet.Height / (float)_clip.Rows;
            int col = CurrentFrame % _clip.Cols, row = CurrentFrame / _clip.Cols;
            const float m = 14f;
            float s = Math.Min((Width - 2 * m) / fw, (Height - 2 * m) / fh);
            if (s <= 0f) return;
            float w = fw * s, h = fh * s, x = (Width - w) / 2f, y = (Height - h) / 2f;

            PointF[] pts = _flip
                ? new[] { new PointF(x + w, y), new PointF(x, y), new PointF(x + w, y + h) }
                : new[] { new PointF(x, y), new PointF(x + w, y), new PointF(x, y + h) };

            using var ia = new ImageAttributes();
            ia.SetWrapMode(WrapMode.TileFlipXY);
            g.InterpolationMode = InterpolationMode.NearestNeighbor;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            g.DrawImage(_sheet, pts, new RectangleF(col * fw, row * fh, fw, fh),
                        GraphicsUnit.Pixel, ia);
            g.PixelOffsetMode = PixelOffsetMode.Default;

            using var cap = new Font("Segoe UI", 8.5f);
            using var capBrush = new SolidBrush(Color.FromArgb(0xB0, 0xB0, 0xB8));
            g.DrawString($"frame {CurrentFrame}", cap, capBrush, 6f, Height - 20f);
        }
    }

    // ============================================================
    //  The editor window
    // ============================================================
    internal sealed class GhostAnimEditor : Panel
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
        private static readonly Color Accent = Color.FromArgb(0xFF, 0xC8, 0x6E);
        private const string FileFilter = "GhostX Animation (*.ghostanim)|*.ghostanim";

        private AnimDoc _doc = new();
        private string? _path;
        private string _saved = "", _note = "";
        private Bitmap? _bmp;
        private AnimClipDoc? _clip;
        private bool _syncing;

        private readonly AnimSheetCanvas _canvas = new();
        private readonly AnimPreview _preview = new();

        // Per-clip controls (live in the right clip-properties panel)
        private readonly ComboBox _source = new();
        private readonly TextBox _image = new();
        private readonly NumericUpDown _cols = new(), _rows = new(),
                                        _fw = new(), _fh = new();
        private readonly Label _info = new();

        // Clip metadata
        private readonly TextBox _name = new();
        private readonly NumericUpDown _fps = new();
        private readonly CheckBox _loop = new(), _flip = new();

        private readonly Label _title = new(), _status = new();
        private readonly ListBox _clips = new(), _frames = new();
        private readonly ToolTip _tip = new();
        private Button _bPlay = null!;

        public bool IsDirty => _doc.ToJson() != _saved;
        private string DocName => _path == null ? "Untitled animation"
                                                : Path.GetFileName(_path);

        // ---- States dialog (unchanged) ----
        internal sealed class AnimStatesDialog : Form
        {
            public static readonly (string State, string When)[] Standard =
            {
                ("Idle",        "standing still"),
                ("Walk",        "moving left or right"),
                ("Run",         "moving while Shift is held"),
                ("Jump",        "in the air, going up (Space)"),
                ("Fall",        "in the air, coming down"),
                ("Crouch",      "Down held on the ground"),
                ("Hurt",        "hit while big"),
                ("Die",         "a stomped enemy"),
                ("Break",       "a brick hit from below"),
                ("Used",        "a ? block after it was hit"),
                ("Attack",      "placeholder: left click (Phase 17)"),
                ("SpinAttack",  "placeholder: combo (Phase 17)"),
            };
            private const string Same = "(same name)";
            private readonly DataGridView _grid = new();
            private readonly Dictionary<string, string> _extra =
                new(StringComparer.OrdinalIgnoreCase);

            public AnimStatesDialog(AnimDoc doc)
            {
                Text = "Animation states";
                FormBorderStyle = FormBorderStyle.FixedDialog;
                StartPosition = FormStartPosition.CenterParent;
                MaximizeBox = false; MinimizeBox = false; ShowInTaskbar = false;
                ClientSize = new Size(700, 490);
                Color bg = Color.FromArgb(0x25, 0x25, 0x28),
                      bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E),
                      fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
                BackColor = bg; ForeColor = fg;

                Controls.Add(new Label
                {
                    Text = "Pick the clip that plays for each state. " +
                               "\"(same name)\" uses a clip called like the state.",
                    Location = new Point(16, 12),
                    Size = new Size(668, 22)
                });

                _grid.Location = new Point(16, 40); _grid.Size = new Size(668, 396);
                _grid.BackgroundColor = bg2;
                _grid.GridColor = Color.FromArgb(0x3A, 0x3A, 0x40);
                _grid.BorderStyle = BorderStyle.FixedSingle;
                _grid.EnableHeadersVisualStyles = false;
                _grid.ColumnHeadersDefaultCellStyle.BackColor = bg;
                _grid.ColumnHeadersDefaultCellStyle.ForeColor = fg;
                _grid.DefaultCellStyle.BackColor = bg2;
                _grid.DefaultCellStyle.ForeColor = fg;
                _grid.DefaultCellStyle.SelectionBackColor =
                    Color.FromArgb(0x5A, 0x42, 0x1E);
                _grid.DefaultCellStyle.SelectionForeColor = fg;
                _grid.RowHeadersVisible = false;
                _grid.AllowUserToAddRows = false;
                _grid.AllowUserToResizeRows = false;
                _grid.EditMode = DataGridViewEditMode.EditOnEnter;
                _grid.DataError += (s, e) => e.ThrowException = false;

                var cState = new DataGridViewTextBoxColumn
                { HeaderText = "State", ReadOnly = true, Width = 120 };
                var cWhen = new DataGridViewTextBoxColumn
                { HeaderText = "Plays when", ReadOnly = true, Width = 280 };
                var cClip = new DataGridViewComboBoxColumn
                { HeaderText = "Clip", Width = 240, FlatStyle = FlatStyle.Flat };
                cClip.Items.Add(Same);
                foreach (AnimClipDoc c in doc.Clips) cClip.Items.Add(c.Name);
                foreach (var kv in doc.States)
                    if (!cClip.Items.Contains(kv.Value)) cClip.Items.Add(kv.Value);
                _grid.Columns.AddRange(cState, cWhen, cClip);

                foreach (var (state, when) in Standard)
                {
                    doc.States.TryGetValue(state, out string? cur);
                    _grid.Rows.Add(state, when, string.IsNullOrEmpty(cur) ? Same : cur);
                }
                foreach (var kv in doc.States)
                    if (Array.FindIndex(Standard,
                            s => string.Equals(s.State, kv.Key,
                                 StringComparison.OrdinalIgnoreCase)) < 0)
                        _extra[kv.Key] = kv.Value;
                Controls.Add(_grid);

                var ok = new Button
                {
                    Text = "OK",
                    Location = new Point(504, 448),
                    Size = new Size(84, 28),
                    FlatStyle = FlatStyle.Flat,
                    BackColor = bg2,
                    ForeColor = fg,
                    DialogResult = DialogResult.OK
                };
                var cancel = new Button
                {
                    Text = "Cancel",
                    Location = new Point(600, 448),
                    Size = new Size(84, 28),
                    FlatStyle = FlatStyle.Flat,
                    BackColor = bg2,
                    ForeColor = fg,
                    DialogResult = DialogResult.Cancel
                };
                Controls.Add(ok); Controls.Add(cancel);
                AcceptButton = ok; CancelButton = cancel;
            }

            public void Apply(AnimDoc doc)
            {
                doc.States.Clear();
                foreach (DataGridViewRow r in _grid.Rows)
                {
                    string state = r.Cells[0].Value?.ToString() ?? "";
                    string clip = r.Cells[2].Value?.ToString() ?? "";
                    if (state.Length > 0 && clip.Length > 0 && clip != Same)
                        doc.States[state] = clip;
                }
                foreach (var kv in _extra) doc.States[kv.Key] = kv.Value;
            }
        }

        // ============================================================
        //  Construction
        // ============================================================
        public GhostAnimEditor()
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
            bar.Controls.Add(Btn("New", NewDoc, "New animation"));
            bar.Controls.Add(Btn("Open...", OpenDialog, "Open a .ghostanim file"));
            bar.Controls.Add(Btn("Save", () => Save(), "Save (Ctrl+S)"));
            bar.Controls.Add(Btn("Save As...", () => SaveAs(), "Save under a new name"));
            bar.Controls.Add(Btn("Assign To Selected Sprite", AssignToSelected,
                "Puts this animation on the Sprite Actor selected in the scene"));
            bar.Controls.Add(Btn("Add Standard Clips", AddStandardClips,
                "Adds empty Idle, Walk, Jump, Fall, Hurt and Die clips"));
            bar.Controls.Add(Btn("States...", EditStates,
                "Choose which clip plays for Idle, Walk, Run, Jump, Fall..."));
            _title.AutoSize = true;
            _title.ForeColor = Accent;
            _title.Margin = new Padding(8, 6, 0, 0);
            bar.Controls.Add(_title);

            // ---- left: clip list ----
            var left = new Panel { Dock = DockStyle.Left, Width = 285, BackColor = Bg };

            _clips.Dock = DockStyle.Fill;
            _clips.BackColor = Bg2; _clips.ForeColor = Fg;
            _clips.BorderStyle = BorderStyle.None;
            _clips.IntegralHeight = false;
            _clips.Font = new Font("Segoe UI", 10f);

            var clipBtns = new FlowLayoutPanel
            {
                Dock = DockStyle.Bottom,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                BackColor = Bg,
                Padding = new Padding(4, 4, 0, 0),
            };
            clipBtns.Controls.Add(Btn("Add", AddClip, "Add an empty clip"));
            clipBtns.Controls.Add(Btn("Duplicate", DuplicateClip, "Copy the selected clip"));
            clipBtns.Controls.Add(Btn("Delete", DeleteClip, "Delete the selected clip"));
            clipBtns.Controls.Add(Btn("Up", () => MoveClip(-1), "Move up"));
            clipBtns.Controls.Add(Btn("Down", () => MoveClip(1), "Move down"));

            var clipLabel = new Label
            {
                Dock = DockStyle.Top,
                Height = 24,
                Text = "Clips",
                ForeColor = Accent,
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(6, 0, 0, 0),
            };
            left.Controls.Add(_clips);
            left.Controls.Add(clipBtns);
            left.Controls.Add(clipLabel);

            // ---- right: preview + clip properties (name/fps/loop + image/cols/rows) + frames ----
            var right = new Panel { Dock = DockStyle.Right, Width = 310, BackColor = Bg };
            _preview.Dock = DockStyle.Top; _preview.Height = 230;

            var pbar = new FlowLayoutPanel
            {
                Dock = DockStyle.Top,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                BackColor = Bg,
                Padding = new Padding(4, 4, 0, 0),
            };
            pbar.Controls.Add(_bPlay = Btn("Pause", TogglePlay, "Pause or resume"));
            pbar.Controls.Add(Btn("Restart", () => _preview.Restart(), "Play from start"));
            _flip.Text = "Flip X"; _flip.ForeColor = Fg;
            _flip.AutoSize = true; _flip.Margin = new Padding(6, 6, 0, 0);
            pbar.Controls.Add(_flip);

            // Clip metadata: Name / FPS / Loop
            var props = new Panel { Dock = DockStyle.Top, Height = 78, BackColor = Bg };
            props.Controls.Add(Lbl("Name", 8, 12));
            _name.Location = new Point(60, 8); _name.Width = 235;
            _name.BackColor = Bg2; _name.ForeColor = Fg;
            _name.BorderStyle = BorderStyle.FixedSingle;
            props.Controls.Add(_name);
            props.Controls.Add(Lbl("FPS", 8, 46));
            Num(_fps, 60, 42, 70, 0, 120);
            _fps.DecimalPlaces = 1; _fps.Increment = 0.5m; _fps.Minimum = 0.1m;
            props.Controls.Add(_fps);
            _loop.Text = "Loop"; _loop.ForeColor = Fg;
            _loop.AutoSize = true; _loop.Location = new Point(150, 44);
            props.Controls.Add(_loop);

            // Per-clip image / grid settings
            var imgProps = new Panel { Dock = DockStyle.Top, Height = 162, BackColor = Bg };
            var imgLabel = new Label
            {
                Text = "Clip Texture",
                ForeColor = Accent,
                Location = new Point(8, 6),
                AutoSize = true,
            };
            imgProps.Controls.Add(imgLabel);

            imgProps.Controls.Add(Lbl("Source", 8, 30));
            _source.Location = new Point(80, 26); _source.Width = 190;
            _source.DropDownStyle = ComboBoxStyle.DropDownList;
            _source.FlatStyle = FlatStyle.Flat;
            _source.BackColor = Bg2; _source.ForeColor = Fg;
            _source.Items.AddRange(SpriteSourceConverter.Names);
            imgProps.Controls.Add(_source);

            imgProps.Controls.Add(Lbl("Image", 8, 62));
            _image.Location = new Point(80, 58); _image.Width = 126;
            _image.ReadOnly = true;
            _image.BackColor = Bg2; _image.ForeColor = Fg;
            _image.BorderStyle = BorderStyle.FixedSingle;
            imgProps.Controls.Add(_image);

            var browse = new Button
            {
                Text = "Browse...",
                Location = new Point(212, 56),
                Size = new Size(58, 26),
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg,
            };
            browse.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
            browse.Click += (s, e) => BrowseImage();
            imgProps.Controls.Add(browse);

            imgProps.Controls.Add(Lbl("Columns", 8, 96)); Num(_cols, 80, 92, 62, 1, 64);
            imgProps.Controls.Add(Lbl("Rows", 150, 96)); Num(_rows, 200, 92, 62, 1, 64);
            imgProps.Controls.Add(Lbl("Frame W", 8, 126)); Num(_fw, 80, 122, 62, 1, 4096);
            imgProps.Controls.Add(Lbl("Frame H", 150, 126)); Num(_fh, 200, 122, 62, 1, 4096);
            foreach (NumericUpDown n in new[] { _cols, _rows, _fw, _fh })
                imgProps.Controls.Add(n);
            _tip.SetToolTip(_fw, "Type a frame width: Columns is worked out for you");
            _tip.SetToolTip(_fh, "Type a frame height: Rows is worked out for you");

            _info.Location = new Point(8, 154); _info.Size = new Size(278, 0);
            _info.AutoSize = true; _info.ForeColor = Color.Gray;
            imgProps.Controls.Add(_info);
            // Give imgProps enough height to hold everything (info label can grow)
            imgProps.AutoSize = true;
            imgProps.AutoSizeMode = AutoSizeMode.GrowAndShrink;

            // Frames list
            var framesLabel = new Label
            {
                Dock = DockStyle.Top,
                Height = 40,
                ForeColor = Accent,
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(6, 0, 0, 0),
                Text = "Frames: click a cell on the sheet to add it,\n" +
                       "or drag across cells to add a range",
            };
            _frames.Dock = DockStyle.Fill;
            _frames.BackColor = Bg2; _frames.ForeColor = Fg;
            _frames.BorderStyle = BorderStyle.None;
            _frames.IntegralHeight = false;
            _frames.SelectionMode = SelectionMode.MultiExtended;
            _frames.Font = new Font("Segoe UI", 10f);

            var frameBtns = new FlowLayoutPanel
            {
                Dock = DockStyle.Bottom,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                BackColor = Bg,
                Padding = new Padding(4, 4, 0, 0),
            };
            frameBtns.Controls.Add(Btn("Remove", RemoveFrames, "Remove selected frames (Delete)"));
            frameBtns.Controls.Add(Btn("Up", () => MoveFrame(-1), "Move frame earlier"));
            frameBtns.Controls.Add(Btn("Down", () => MoveFrame(1), "Move frame later"));
            frameBtns.Controls.Add(Btn("Clear", ClearFrames, "Remove all frames"));

            right.Controls.Add(_frames);
            right.Controls.Add(frameBtns);
            right.Controls.Add(framesLabel);
            right.Controls.Add(imgProps);
            right.Controls.Add(props);
            right.Controls.Add(pbar);
            right.Controls.Add(_preview);

            var splitL = new Splitter { Dock = DockStyle.Left, Width = 5, BackColor = Bg2 };
            var splitR = new Splitter { Dock = DockStyle.Right, Width = 5, BackColor = Bg2 };
            _status.Dock = DockStyle.Bottom; _status.Height = 24; _status.ForeColor = Color.Gray;
            _status.TextAlign = ContentAlignment.MiddleLeft;
            _status.Padding = new Padding(6, 0, 0, 0);
            _canvas.Dock = DockStyle.Fill;

            Controls.Add(_canvas);
            Controls.Add(_status);
            Controls.Add(splitR);
            Controls.Add(right);
            Controls.Add(splitL);
            Controls.Add(left);
            Controls.Add(bar);

            WireEvents();
            SetDoc(new AnimDoc(), null);
        }

        // ============================================================
        //  Helpers
        // ============================================================
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

        private Label Lbl(string text, int x, int y) =>
            new Label { Text = text, Location = new Point(x, y), AutoSize = true, ForeColor = Fg };

        private void Num(NumericUpDown n, int x, int y, int w, int min, int max)
        {
            n.Location = new Point(x, y); n.Width = w;
            n.Minimum = min; n.Maximum = max;
            n.BackColor = Bg2; n.ForeColor = Fg;
            n.BorderStyle = BorderStyle.FixedSingle;
        }

        public void FocusCanvas() => _canvas.Focus();

        protected override void Dispose(bool disposing)
        {
            if (disposing) { _bmp?.Dispose(); _tip.Dispose(); }
            base.Dispose(disposing);
        }

        // ============================================================
        //  Events
        // ============================================================
        private void WireEvents()
        {
            _source.SelectedIndexChanged += (s, e) =>
            {
                if (_syncing || _clip == null) return;
                _clip.Source = _source.SelectedIndex;
                AfterGridChange();
            };
            _cols.ValueChanged += (s, e) =>
            {
                if (_syncing || _clip == null) return;
                _clip.Cols = (int)_cols.Value; AfterGridChange();
            };
            _rows.ValueChanged += (s, e) =>
            {
                if (_syncing || _clip == null) return;
                _clip.Rows = (int)_rows.Value; AfterGridChange();
            };
            _fw.ValueChanged += (s, e) =>
            {
                if (_syncing || _clip == null || _bmp == null) return;
                _clip.Cols = Math.Clamp(
                    _bmp.Width / Math.Max((int)_fw.Value, 1), 1, AnimDoc.MaxGrid);
                AfterGridChange();
            };
            _fh.ValueChanged += (s, e) =>
            {
                if (_syncing || _clip == null || _bmp == null) return;
                _clip.Rows = Math.Clamp(
                    _bmp.Height / Math.Max((int)_fh.Value, 1), 1, AnimDoc.MaxGrid);
                AfterGridChange();
            };

            _clips.SelectedIndexChanged += (s, e) =>
            {
                if (!_syncing) SelectClip(_clips.SelectedItem as AnimClipDoc);
            };

            _name.TextChanged += (s, e) =>
            {
                if (_syncing || _clip == null || _name.Text.Trim().Length == 0) return;
                _clip.Name = _name.Text.Trim();
                RefreshClipLabel(); Touch();
            };
            _name.Validated += (s, e) =>
            {
                if (_clip == null) return;
                _clip.Name = _doc.UniqueClipName(_clip, _name.Text);
                _syncing = true; _name.Text = _clip.Name; _syncing = false;
                RefreshClipLabel(); Touch();
            };
            _fps.ValueChanged += (s, e) =>
            {
                if (_syncing || _clip == null) return;
                _clip.Fps = (float)_fps.Value;
                RefreshClipLabel(); Touch();
            };
            _loop.CheckedChanged += (s, e) =>
            {
                if (_syncing || _clip == null) return;
                _clip.Loop = _loop.Checked;
                RefreshClipLabel(); _preview.Restart(); Touch();
            };
            _flip.CheckedChanged += (s, e) => _preview.Flip = _flip.Checked;

            _canvas.FramesPicked += AddFrames;
            _preview.FrameChanged += f => _canvas.PlayFrame = f;

            _frames.KeyDown += (s, e) =>
            {
                if (e.KeyCode != Keys.Delete) return;
                RemoveFrames();
                e.Handled = true; e.SuppressKeyPress = true;
            };
        }

        // ============================================================
        //  Display refresh
        // ============================================================
        private void SyncSettings()
        {
            _syncing = true;
            bool hasClip = _clip != null;

            _source.Enabled = hasClip;
            _image.Enabled = hasClip;
            _cols.Enabled = hasClip && _clip!.Source == 1;
            _rows.Enabled = hasClip && _clip!.Source == 1;
            _fw.Enabled = hasClip && _clip!.Source == 1 && _bmp != null;
            _fh.Enabled = hasClip && _clip!.Source == 1 && _bmp != null;

            if (hasClip)
            {
                _source.SelectedIndex = Math.Clamp(_clip!.Source, 0, 1);
                _image.Text = _clip.Texture;

                _cols.Value = Math.Clamp(_clip.Cols, 1, 64);
                _rows.Value = Math.Clamp(_clip.Rows, 1, 64);

                if (_bmp != null)
                {
                    _fw.Value = Math.Clamp(_bmp.Width / _clip.Cols, 1, 4096);
                    _fh.Value = Math.Clamp(_bmp.Height / _clip.Rows, 1, 4096);
                    int fc = AnimDoc.FrameCount(_clip);
                    _info.Text =
                        $"Image {_bmp.Width} x {_bmp.Height} px\n" +
                        $"Frame {_bmp.Width / _clip.Cols} x {_bmp.Height / _clip.Rows} px," +
                        $"  {fc} frame{(fc == 1 ? "" : "s")}";
                    if (_bmp.Width % _clip.Cols != 0 || _bmp.Height % _clip.Rows != 0)
                        _info.Text += "\nThe image doesn't divide evenly by this grid.";
                }
                else
                {
                    _info.Text = _clip.Texture.Length > 0
                        ? "The image file was not found."
                        : "No image yet. Click Browse...";
                }
            }
            else
            {
                _source.SelectedIndex = 1;
                _image.Text = "";
                _info.Text = "";
            }
            _syncing = false;
        }

        private void AfterGridChange()
        {
            if (_clip != null) _doc.NormalizeClipGrid(_clip);
            SyncSettings();
            _canvas.Invalidate();
            _preview.Invalidate();
            RefreshFrames();
            Touch();
        }

        private void RefreshClips(AnimClipDoc? select)
        {
            _syncing = true;
            _clips.BeginUpdate();
            _clips.Items.Clear();
            foreach (AnimClipDoc c in _doc.Clips) _clips.Items.Add(c);
            _clips.EndUpdate();
            _syncing = false;

            if (select != null) _clips.SelectedItem = select;
            else if (_clips.Items.Count > 0) _clips.SelectedIndex = 0;
            else SelectClip(null);
        }

        private void RefreshClipLabel()
        {
            int i = _clips.SelectedIndex;
            if (_clip == null || i < 0) return;
            _syncing = true;
            _clips.Items[i] = _clip;
            _clips.SelectedIndex = i;
            _syncing = false;
        }

        private void SelectClip(AnimClipDoc? c)
        {
            _clip = c;
            _syncing = true;
            bool hasClip = c != null;
            _name.Enabled = _fps.Enabled = _loop.Enabled = hasClip;
            _name.Text = c?.Name ?? "";
            _fps.Value = (decimal)Math.Clamp(c?.Fps ?? 8f, 0.1f, 120f);
            _loop.Checked = c?.Loop ?? true;
            _syncing = false;

            // Reload the bitmap for this clip's texture
            ReloadBitmap();
            SyncSettings();

            _canvas.Clip = c;
            _preview.Clip = c;
            _preview.Restart();
            RefreshFrames();
        }

        private void RefreshFrames(int select = -1)
        {
            _frames.BeginUpdate();
            _frames.Items.Clear();
            if (_clip != null)
            {
                int cols = Math.Max(_clip.Cols, 1);
                int fc = AnimDoc.FrameCount(_clip);
                for (int i = 0; i < _clip.Frames.Count; i++)
                {
                    int f = _clip.Frames[i];
                    string bad = (f < 0 || f >= fc) ? "   OUT OF RANGE" : "";
                    _frames.Items.Add(
                        $"{i + 1}.   Frame {f}   (col {f % cols}, row {f / cols}){bad}");
                }
            }
            _frames.EndUpdate();
            if (select >= 0 && select < _frames.Items.Count)
                _frames.SelectedIndex = select;
            _canvas.Invalidate();
        }

        private void SetNote(string s) { _note = s; UpdateStatus(); }

        private void UpdateStatus()
        {
            int empty = _doc.Clips.Count(c => c.Frames.Count == 0);
            string warn = empty > 0
                ? $"{empty} clip{(empty == 1 ? "" : "s")} with no frames will be ignored by the game."
                : "";
            _status.Text = _note.Length > 0 && warn.Length > 0
                ? _note + "     " + warn : _note + warn;
        }

        private void Touch()
        {
            _title.Text = DocName + (IsDirty ? "*" : "");
            if (Parent is DockContent dc)
                dc.TabText = "Animation Editor - " + _title.Text;
            UpdateStatus();
        }

        private void TogglePlay()
        {
            _preview.Playing = !_preview.Playing;
            _bPlay.Text = _preview.Playing ? "Pause" : "Play";
        }

        private void EditStates()
        {
            using var dlg = new AnimStatesDialog(_doc);
            if (dlg.ShowDialog(FindForm()) != DialogResult.OK) return;
            dlg.Apply(_doc);
            Touch();
        }

        // ============================================================
        //  Clips
        // ============================================================
        private void AddClip()
        {
            if (_doc.Clips.Count >= AnimDoc.MaxClips)
            { System.Media.SystemSounds.Beep.Play(); return; }
            var c = new AnimClipDoc { Name = _doc.UniqueClipName(null, "Clip") };
            _doc.Clips.Add(c);
            RefreshClips(c);
            Touch();
        }

        private void DuplicateClip()
        {
            if (_clip == null || _doc.Clips.Count >= AnimDoc.MaxClips)
            { System.Media.SystemSounds.Beep.Play(); return; }
            var c = new AnimClipDoc
            {
                Name = _doc.UniqueClipName(null, _clip.Name + " copy"),
                Frames = new List<int>(_clip.Frames),
                Fps = _clip.Fps,
                Loop = _clip.Loop,
                Texture = _clip.Texture,
                Source = _clip.Source,
                Cols = _clip.Cols,
                Rows = _clip.Rows,
            };
            _doc.Clips.Insert(_doc.Clips.IndexOf(_clip) + 1, c);
            RefreshClips(c);
            Touch();
        }

        private void DeleteClip()
        {
            if (_clip == null) return;
            int i = _doc.Clips.IndexOf(_clip);
            _doc.Clips.Remove(_clip);
            RefreshClips(_doc.Clips.Count == 0 ? null
                : _doc.Clips[Math.Min(i, _doc.Clips.Count - 1)]);
            Touch();
        }

        private void MoveClip(int dir)
        {
            if (_clip == null) return;
            int i = _doc.Clips.IndexOf(_clip), j = i + dir;
            if (j < 0 || j >= _doc.Clips.Count) return;
            (_doc.Clips[i], _doc.Clips[j]) = (_doc.Clips[j], _doc.Clips[i]);
            RefreshClips(_clip);
            Touch();
        }

        private void AddStandardClips()
        {
            AnimClipDoc? first = null;
            int added = 0;
            foreach (var (name, fps, loop) in AnimDoc.Standard)
            {
                if (_doc.Clips.Count >= AnimDoc.MaxClips) break;
                if (_doc.Clips.Any(c => string.Equals(c.Name, name,
                        StringComparison.OrdinalIgnoreCase))) continue;
                var c = new AnimClipDoc { Name = name, Fps = fps, Loop = loop };
                _doc.Clips.Add(c);
                first ??= c;
                added++;
            }
            RefreshClips(first ?? _clip);
            Touch();
            SetNote(added > 0
                ? $"Added {added} clip{(added == 1 ? "" : "s")}. Select one, then click frames."
                : "Those clips already exist.");
        }

        // ============================================================
        //  Frames
        // ============================================================
        private void AddFrames(int from, int to)
        {
            if (_clip == null)
            {
                System.Media.SystemSounds.Beep.Play();
                SetNote("Select or add a clip first.");
                return;
            }
            int step = from <= to ? 1 : -1;
            for (int f = from; ; f += step)
            {
                if (_clip.Frames.Count >= AnimDoc.MaxFrames) break;
                _clip.Frames.Add(f);
                if (f == to) break;
            }
            RefreshClipLabel();
            RefreshFrames(_clip.Frames.Count - 1);
            _preview.Restart();
            SetNote("");
            Touch();
        }

        private void RemoveFrames()
        {
            if (_clip == null || _frames.SelectedIndices.Count == 0) return;
            int first = _frames.SelectedIndices.Cast<int>().Min();
            foreach (int i in _frames.SelectedIndices.Cast<int>().OrderByDescending(x => x))
                _clip.Frames.RemoveAt(i);
            RefreshClipLabel();
            RefreshFrames(Math.Min(first, _clip.Frames.Count - 1));
            _preview.Restart(); Touch();
        }

        private void MoveFrame(int dir)
        {
            if (_clip == null || _frames.SelectedIndices.Count != 1) return;
            int i = _frames.SelectedIndex, j = i + dir;
            if (j < 0 || j >= _clip.Frames.Count) return;
            (_clip.Frames[i], _clip.Frames[j]) = (_clip.Frames[j], _clip.Frames[i]);
            RefreshFrames(j);
            _preview.Restart(); Touch();
        }

        private void ClearFrames()
        {
            if (_clip == null || _clip.Frames.Count == 0) return;
            _clip.Frames.Clear();
            RefreshClipLabel(); RefreshFrames();
            _preview.Restart(); Touch();
        }

        // ============================================================
        //  Image — per clip
        // ============================================================
        private string? ResolveTexture()
        {
            if (_clip == null || _clip.Texture.Length == 0) return null;
            return Path.IsPathRooted(_clip.Texture)
                ? _clip.Texture
                : Path.Combine(GhostProject.Root ?? "", _clip.Texture);
        }

        private void ReloadBitmap()
        {
            Bitmap? old = _bmp;
            _bmp = null;
            string? full = ResolveTexture();
            try
            {
                if (full != null && File.Exists(full))
                {
                    using var ms = new MemoryStream(File.ReadAllBytes(full));
                    using Image img = Image.FromStream(ms);
                    _bmp = new Bitmap(img);
                }
            }
            catch { _bmp = null; }
            old?.Dispose();

            _canvas.Sheet = _bmp;
            _preview.Sheet = _bmp;
        }

        private static string StoredPath(string file)
        {
            string full = Path.GetFullPath(file);
            string? root = GhostProject.Root;
            if (root != null)
            {
                string rel = Path.GetRelativePath(root, full);
                if (!rel.StartsWith("..") && !Path.IsPathRooted(rel))
                    return rel.Replace('\\', '/');
            }
            return full;
        }

        private void BrowseImage()
        {
            if (_clip == null) return;
            using var dlg = new OpenFileDialog
            {
                Title = "Choose the sprite image",
                Filter = "Images (*.png;*.jpg;*.jpeg;*.bmp)" +
                         "|*.png;*.jpg;*.jpeg;*.bmp|All files (*.*)|*.*",
            };
            string? content = GhostProject.ContentDir;
            if (content != null && Directory.Exists(Path.Combine(content, "Sprites")))
                dlg.InitialDirectory = Path.Combine(content, "Sprites");
            if (dlg.ShowDialog(FindForm()) != DialogResult.OK) return;

            try
            {
                string? sprites = content == null ? null : Path.Combine(content, "Sprites");
                if (sprites != null) Directory.CreateDirectory(sprites);
                string local = GhostProject.EnsureInContent(dlg.FileName, sprites);
                _clip.Texture = StoredPath(local);
                ReloadBitmap();
                SyncSettings();
                _canvas.Invalidate();
                Touch();
            }
            catch (Exception ex)
            {
                MessageBox.Show(FindForm(), ex.Message, "Image",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        // ============================================================
        //  Files
        // ============================================================
        private void SetDoc(AnimDoc d, string? path)
        {
            _doc = d;
            _path = path;
            _clip = null;
            _bmp?.Dispose();
            _bmp = null;
            _canvas.Sheet = null;
            _preview.Sheet = null;
            _canvas.Doc = d;
          //  _preview.Doc = d;
            _saved = d.ToJson();
            RefreshClips(null);
            Touch();
            SetNote("");
        }

        private void NewDoc()
        {
            if (!ConfirmDiscard(FindForm())) return;
            SetDoc(new AnimDoc(), null);
        }

        private string? AnimFolder()
        {
            string? content = GhostProject.ContentDir;
            if (content == null) return null;
            string dir = Path.Combine(content, "Animations");
            try { Directory.CreateDirectory(dir); } catch { return null; }
            return dir;
        }

        private void OpenDialog()
        {
            using var dlg = new OpenFileDialog
            { Title = "Open animation", Filter = FileFilter };
            string? dir = AnimFolder();
            if (dir != null) dlg.InitialDirectory = dir;
            if (dlg.ShowDialog(FindForm()) == DialogResult.OK) OpenFile(dlg.FileName);
        }

        public void OpenFile(string path)
        {
            if (!ConfirmDiscard(FindForm())) return;
            try { SetDoc(AnimDoc.Load(path), path); }
            catch (Exception ex)
            {
                MessageBox.Show(FindForm(), ex.Message, "Could not open animation",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        public bool Save() => _path == null ? SaveAs() : WriteTo(_path);

        public bool SaveAs()
        {
            using var dlg = new SaveFileDialog
            {
                Title = "Save animation as",
                Filter = FileFilter,
                DefaultExt = "ghostanim",
                AddExtension = true,
                FileName = _path != null
                    ? Path.GetFileNameWithoutExtension(_path) : "NewAnimation",
            };
            string? dir = AnimFolder();
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
                MessageBox.Show(FindForm(), ex.Message, "Save failed",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            _path = path;
            _saved = _doc.ToJson();
            GhostEngine.GhostEngine_ReloadAnimations();
            GhostAssets.SyncFromEngine();
            Touch();
            SetNote("Saved " + Path.GetFileName(path));
            return true;
        }

        public bool ConfirmDiscard(IWin32Window? owner)
        {
            if (!IsDirty) return true;
            DialogResult r = MessageBox.Show(owner,
                $"Save changes to {DocName}?", "Animation Editor",
                MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
            if (r == DialogResult.Cancel) return false;
            return r == DialogResult.No || Save();
        }

        // ============================================================
        //  Assign to selected sprite
        // ============================================================
        private void AssignToSelected()
        {
            int sel = GhostEngine.GhostEngine_GetSelected();
            var o = new GhostObjectInfo();
            if (sel == 0 ||
                GhostEngine.GhostEngine_GetObject(sel, ref o) == 0 ||
                o.meshId != 4)
            {
                MessageBox.Show(FindForm(),
                    "Select a Sprite Actor in the scene first " +
                    "(Palette > Sprites > Sprite Actor).",
                    "Assign animation",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            if (_path == null || IsDirty)
                if (!Save()) return;

            GhostEngine.GhostEngine_SetSpriteText(sel, 0, StoredPath(_path!));
            GhostAssets.SyncFromEngine();
            SetNote($"Assigned {Path.GetFileName(_path)} to {GhostNames.Of(sel)}.");
        }
    }
}