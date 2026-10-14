// GhostUICanvas.cs — the designer's canvas: draws the widgets (GDI+), selection, move / resize handles, drops
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using System.ComponentModel;
namespace GhostShell
{
    internal sealed class UICanvas : Control
    {
        private static readonly Color Accent = Color.FromArgb(0xFF, 0x9F, 0x1C);

        // (static fields initialize in order: Family before FontRatio)
        private static readonly FontFamily Family = MakeFamily();
        private static readonly float FontRatio = ComputeRatio();

        private DesignDoc _doc = new();
        private DWidget? _sel;
        private float _s = 1f, _ox, _oy;

        private enum Mode { None, Move, Resize }
        private Mode _mode = Mode.None;
        private int _mask;                                   // resize handle: 1 left, 2 right, 4 top, 8 bottom
        private Point _start;
        private bool _moved;
        private float[] _so = new float[2], _ss = new float[2];

        private readonly Dictionary<int, Font> _fonts = new();
        private readonly StringFormat _fmt;
        private readonly Bitmap _mbmp = new(1, 1);
        private readonly Graphics _mg;
        private readonly Dictionary<string, (DateTime stamp, Bitmap? bmp)> _images = new(StringComparer.OrdinalIgnoreCase);

        public event Action<DWidget?>? SelectionChanged;     // the user clicked something (or empty space)
        public event Action? LiveChanged;                    // a drag is changing values
        public event Action? Committed;                      // a drag finished
        public event Action<string, PointF, DWidget?>? WidgetDropped;   // type, reference point, Canvas under it

        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public DesignDoc Doc { get => _doc; set { _doc = value; Invalidate(); } }

        [Browsable(false), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public DWidget? Selected { get => _sel; set { _sel = value; Invalidate(); } }
        /*
        public DesignDoc Doc { get => _doc; set { _doc = value; Invalidate(); } }
        public DWidget? Selected { get => _sel; set { _sel = value; Invalidate(); } }
        */

        public UICanvas()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.Selectable | ControlStyles.ResizeRedraw, true);
            TabStop = true;
            AllowDrop = true;
            BackColor = Color.FromArgb(0x1E, 0x1E, 0x1E);

            _fmt = (StringFormat)StringFormat.GenericTypographic.Clone();
            _fmt.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces | StringFormatFlags.NoWrap;
            _mg = Graphics.FromImage(_mbmp);
            _mg.PageUnit = GraphicsUnit.Pixel;
            _mg.TextRenderingHint = TextRenderingHint.AntiAlias;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                foreach (Font f in _fonts.Values) f.Dispose();
                foreach (var i in _images.Values) i.bmp?.Dispose();
                _fmt.Dispose(); _mg.Dispose(); _mbmp.Dispose();
            }
            base.Dispose(disposing);
        }

        private static FontFamily MakeFamily() { try { return new FontFamily("Segoe UI"); } catch { return FontFamily.GenericSansSerif; } }
        private static float ComputeRatio()
        {
            try { return Family.GetLineSpacing(FontStyle.Regular) / (float)Family.GetEmHeight(FontStyle.Regular); }
            catch { return 1.33f; }
        }

        // The engine's text size is the LINE height; GDI+ wants the em size
        private Font FontFor(float size)
        {
            float em = Math.Max(size / FontRatio, 1f);
            int key = (int)Math.Round(em * 10f);
            if (!_fonts.TryGetValue(key, out Font? f))
            {
                if (_fonts.Count > 64) { foreach (Font o in _fonts.Values) o.Dispose(); _fonts.Clear(); }
                f = new Font(Family, em, FontStyle.Regular, GraphicsUnit.Pixel);
                _fonts[key] = f;
            }
            return f;
        }

        // ---------------- view ----------------
        private void ComputeView()
        {
            float W = _doc.RefW;
            const float m = 28f;
            float aw = Math.Max(Width - 2 * m, 40f), ah = Math.Max(Height - 2 * m, 40f);
            _s = Math.Max(0.02f, Math.Min(aw / W, ah / 1080f));
            _ox = (Width - W * _s) / 2f;
            _oy = (Height - 1080f * _s) / 2f;
        }

        private float RefX(float px) => (px - _ox) / _s;
        private float RefY(float py) => (py - _oy) / _s;

        // ---------------- painting ----------------
        private static int B(float v) => (int)Math.Round(Math.Clamp(v, 0f, 1f) * 255f);
        private static Color Col(float[] c, float alphaMul = 1f) => Color.FromArgb(B(c[3] * alphaMul), B(c[0]), B(c[1]), B(c[2]));

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(BackColor);
            ComputeView();
            _doc.Layout();
            float W = _doc.RefW;

            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.AntiAlias;
            g.InterpolationMode = InterpolationMode.HighQualityBilinear;

            g.TranslateTransform(_ox, _oy);
            g.ScaleTransform(_s, _s);
            g.SetClip(new RectangleF(0f, 0f, W, 1080f));
            using (var bg = new SolidBrush(Color.FromArgb(0x12, 0x14, 0x18))) g.FillRectangle(bg, 0f, 0f, W, 1080f);
            using (var grid = new Pen(Color.FromArgb(22, 255, 255, 255), 1f / _s))
            {
                for (float x = 120f; x < W; x += 120f) g.DrawLine(grid, x, 0f, x, 1080f);
                for (float y = 120f; y < 1080f; y += 120f) g.DrawLine(grid, 0f, y, W, y);
            }
            foreach (DWidget r in _doc.Roots) DrawTree(g, r);
            g.ResetClip();
            g.ResetTransform();

            using (var frame = new Pen(Color.FromArgb(0x66, 0x66, 0x6E), 1f))
                g.DrawRectangle(frame, _ox, _oy, W * _s, 1080f * _s);

            using (var cap = new SolidBrush(Color.FromArgb(0x88, 0x88, 0x90)))
            using (var small = new Font("Segoe UI", 8.5f))
                g.DrawString($"{W:0} x 1080 reference", small, cap, _ox, Math.Max(_oy - 18f, 2f));

            if (_doc.Roots.Count == 0)
            {
                using var f = new Font("Segoe UI", 11f);
                using var b = new SolidBrush(Color.FromArgb(0x99, 0x99, 0xA0));
                const string hint = "Drag widgets from the palette onto the canvas (or double-click one)";
                SizeF sz = g.MeasureString(hint, f);
                g.DrawString(hint, f, b, (Width - sz.Width) / 2f, Height / 2f - sz.Height / 2f);
            }

            if (_sel != null && _sel.Eff)
            {
                float sx = _ox + _sel.X * _s, sy = _oy + _sel.Y * _s, sw = _sel.W * _s, sh = _sel.H * _s;
                using var pen = new Pen(Accent, 2f);
                g.DrawRectangle(pen, sx, sy, sw, sh);
                using var hb = new SolidBrush(Color.White);
                using var hp = new Pen(Accent, 1f);
                foreach (var (hx, hy, _) in Handles(sx, sy, sw, sh))
                {
                    g.FillRectangle(hb, hx - 4f, hy - 4f, 8f, 8f);
                    g.DrawRectangle(hp, hx - 4f, hy - 4f, 8f, 8f);
                }
            }
        }

        private void DrawTree(Graphics g, DWidget w)
        {
            if (!w.Eff) return;                 // children inherit invisibility
            DrawOne(g, w);
            foreach (DWidget c in w.Children) DrawTree(g, c);
        }

        private void DrawOne(Graphics g, DWidget w)
        {
            switch (w.Type)
            {
                case "Canvas":
                    if (w.Color[3] > 0.001f && w.W > 0f && w.H > 0f)
                        using (var b = new SolidBrush(Col(w.Color))) g.FillRectangle(b, w.X, w.Y, w.W, w.H);
                    using (var dash = new Pen(Color.FromArgb(70, 200, 200, 210), 1f / _s) { DashStyle = DashStyle.Dash })
                        g.DrawRectangle(dash, w.X, w.Y, w.W, w.H);      // editor-only outline
                    break;

                case "Image":
                    DrawImageWidget(g, w);
                    break;

                case "Text":
                    DrawText(g, w, w.Align, false, w.Color);
                    break;

                case "Button":
                    using (var b = new SolidBrush(Col(w.Color))) g.FillRectangle(b, w.X, w.Y, w.W, w.H);
                    DrawText(g, w, 1, true, w.TextColor);               // button labels are always centered
                    break;

                case "ProgressBar":
                    using (var bg = new SolidBrush(Col(w.BgColor))) g.FillRectangle(bg, w.X, w.Y, w.W, w.H);
                    using (var fill = new SolidBrush(Col(w.Color)))
                        g.FillRectangle(fill, w.X, w.Y, w.W * Math.Clamp(w.Value, 0f, 1f), w.H);
                    break;
            }
        }

        private void DrawText(Graphics g, DWidget w, int align, bool centerV, float[] col)
        {
            if (w.Text.Length == 0 || col[3] <= 0.001f) return;
            Font f = FontFor(w.FontSize);
            float tw = _mg.MeasureString(w.Text, f, PointF.Empty, _fmt).Width;     // widest line, in reference units

            float tx = w.X;
            if (align == 1) tx += (w.W - tw) * 0.5f;
            else if (align == 2) tx += w.W - tw;
            float ty = centerV ? w.Y + (w.H - w.FontSize) * 0.5f : w.Y;

            if (w.Shadow)
                using (var sb = new SolidBrush(Color.FromArgb(B(col[3] * 0.65f), 0, 0, 0)))
                    g.DrawString(w.Text, f, sb, tx + w.FontSize * 0.05f, ty + w.FontSize * 0.05f, _fmt);
            using (var br = new SolidBrush(Col(col)))
                g.DrawString(w.Text, f, br, tx, ty, _fmt);
        }

        private void DrawImageWidget(Graphics g, DWidget w)
        {
            if (w.W < 1f || w.H < 1f) return;
            Bitmap? bmp = w.Texture.Length > 0 ? GetImage(w.Texture) : null;
            if (bmp == null)
            {
                using var fill = new SolidBrush(Color.FromArgb(B(0.7f * w.Color[3]), 0x46, 0x46, 0x50));
                g.FillRectangle(fill, w.X, w.Y, w.W, w.H);
                using var pen = new Pen(Color.FromArgb(B(0.7f * w.Color[3]), 0xA0, 0xA0, 0xB0), 2f / _s);
                g.DrawLine(pen, w.X, w.Y, w.X + w.W, w.Y + w.H);
                g.DrawLine(pen, w.X + w.W, w.Y, w.X, w.Y + w.H);
                return;
            }

            var cm = new ColorMatrix(new[]
            {
                new[] { w.Color[0], 0f, 0f, 0f, 0f },
                new[] { 0f, w.Color[1], 0f, 0f, 0f },
                new[] { 0f, 0f, w.Color[2], 0f, 0f },
                new[] { 0f, 0f, 0f, w.Color[3], 0f },
                new[] { 0f, 0f, 0f, 0f, 1f },
            });
            using var ia = new ImageAttributes();
            ia.SetColorMatrix(cm);
            ia.SetWrapMode(WrapMode.TileFlipXY);
            g.DrawImage(bmp, Rectangle.Round(new RectangleF(w.X, w.Y, w.W, w.H)),
                0, 0, bmp.Width, bmp.Height, GraphicsUnit.Pixel, ia);
        }

        private Bitmap? GetImage(string rel)
        {
            try
            {
                string path = Path.IsPathRooted(rel) ? rel : Path.Combine(GhostProject.Root ?? "", rel);
                if (!File.Exists(path)) return null;
                DateTime stamp = File.GetLastWriteTimeUtc(path);
                if (_images.TryGetValue(path, out var cached))
                {
                    if (cached.stamp == stamp) return cached.bmp;
                    cached.bmp?.Dispose();
                }
                using FileStream fs = File.OpenRead(path);
                using Image img = Image.FromStream(fs);
                var bmp = new Bitmap(img);
                _images[path] = (stamp, bmp);
                return bmp;
            }
            catch { return null; }
        }

        // ---------------- handles and hit testing ----------------
        // Corners first, so they win over edges on very small widgets
        private static (float x, float y, int mask)[] Handles(float x, float y, float w, float h) => new[]
        {
            (x, y, 1 | 4), (x + w, y, 2 | 4), (x, y + h, 1 | 8), (x + w, y + h, 2 | 8),
            (x + w / 2f, y, 4), (x + w / 2f, y + h, 8), (x, y + h / 2f, 1), (x + w, y + h / 2f, 2),
        };

        private int HandleAt(Point p)
        {
            if (_sel == null || !_sel.Eff) return 0;
            float sx = _ox + _sel.X * _s, sy = _oy + _sel.Y * _s, sw = _sel.W * _s, sh = _sel.H * _s;
            foreach (var (hx, hy, mask) in Handles(sx, sy, sw, sh))
                if (Math.Abs(p.X - hx) <= 7f && Math.Abs(p.Y - hy) <= 7f) return mask;
            return 0;
        }

        private DWidget? HitTest(float rx, float ry)
        {
            float band = 6f / _s;
            foreach (DWidget w in _doc.All().Reverse())             // topmost first
            {
                if (!w.Eff) continue;
                bool inside = rx >= w.X && rx < w.X + w.W && ry >= w.Y && ry < w.Y + w.H;
                if (w.Type == "Canvas" && w.Color[3] <= 0.01f)
                {
                    // An invisible container is only grabbed by its edge, so it never blocks clicks on what is inside it
                    bool outer = rx >= w.X - band && rx < w.X + w.W + band && ry >= w.Y - band && ry < w.Y + w.H + band;
                    bool core = rx >= w.X + band && rx < w.X + w.W - band && ry >= w.Y + band && ry < w.Y + w.H - band;
                    if (outer && !core) return w;
                    continue;
                }
                if (inside) return w;
            }
            return null;
        }

        private DWidget? CanvasAt(float rx, float ry)
        {
            foreach (DWidget w in _doc.All().Reverse())
                if (w.Type == "Canvas" && w.Eff && rx >= w.X && rx < w.X + w.W && ry >= w.Y && ry < w.Y + w.H) return w;
            return null;
        }

        // ---------------- mouse ----------------
        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            Focus();
            if (e.Button != MouseButtons.Left) return;
            ComputeView();
            _doc.Layout();

            int mask = HandleAt(e.Location);
            if (mask != 0) { BeginDrag(e.Location, mask); return; }

            DWidget? hit = HitTest(RefX(e.X), RefY(e.Y));
            if (hit != _sel) { _sel = hit; SelectionChanged?.Invoke(hit); Invalidate(); }
            if (hit != null) BeginDrag(e.Location, 0);
        }

        private void BeginDrag(Point p, int mask)
        {
            if (_sel == null) return;
            _mode = mask == 0 ? Mode.Move : Mode.Resize;
            _mask = mask;
            _start = p;
            _moved = false;
            _so = new[] { _sel.Offset[0], _sel.Offset[1] };
            _ss = new[] { _sel.Size[0], _sel.Size[1] };
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (_mode != Mode.None && _sel != null)
            {
                if (!_moved && Math.Abs(e.X - _start.X) < 3 && Math.Abs(e.Y - _start.Y) < 3) return;
                _moved = true;
                float dx = (e.X - _start.X) / _s, dy = (e.Y - _start.Y) / _s;
                if (_mode == Mode.Move) ApplyMove(dx, dy); else ApplyResize(dx, dy);
                LiveChanged?.Invoke();
                Invalidate();
                return;
            }
            Cursor = CursorFor(HandleAt(e.Location));
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            if (e.Button != MouseButtons.Left) return;
            bool changed = _mode != Mode.None && _moved;
            _mode = Mode.None;
            _moved = false;
            if (changed) Committed?.Invoke();
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            base.OnMouseLeave(e);
            if (_mode == Mode.None) Cursor = Cursors.Default;
        }

        private static Cursor CursorFor(int mask) => mask switch
        {
            1 or 2 => Cursors.SizeWE,
            4 or 8 => Cursors.SizeNS,
            5 or 10 => Cursors.SizeNWSE,
            6 or 9 => Cursors.SizeNESW,
            _ => Cursors.Default,
        };

        private void ApplyMove(float dx, float dy)
        {
            DWidget w = _sel!;
            for (int a = 0; a < 2; a++)
            {
                float d = a == 0 ? dx : dy;
                w.Offset[a] = MathF.Round(_so[a] + d);
                if (DesignDoc.Stretched(w, a)) w.Size[a] = MathF.Round(_ss[a] - d);     // margins move together
            }
        }

        private void ApplyResize(float dx, float dy)
        {
            DWidget w = _sel!;
            for (int a = 0; a < 2; a++)
            {
                float d = a == 0 ? dx : dy;
                bool low = (_mask & (a == 0 ? 1 : 4)) != 0, high = (_mask & (a == 0 ? 2 : 8)) != 0;
                if (!low && !high) continue;
                float so = _so[a], ss = _ss[a], pv = w.Pivot[a];

                if (DesignDoc.Stretched(w, a))
                {
                    if (low) w.Offset[a] = MathF.Round(so + d);          // left / top margin
                    else w.Size[a] = MathF.Round(ss - d);                // right / bottom margin
                }
                else if (low)
                {
                    float e = MathF.Min(d, ss - 4f);                     // never smaller than 4 units
                    w.Size[a] = MathF.Round(ss - e);
                    w.Offset[a] = MathF.Round(so + e * (1f - pv));
                }
                else
                {
                    float e = MathF.Max(d, -(ss - 4f));
                    w.Size[a] = MathF.Round(ss + e);
                    w.Offset[a] = MathF.Round(so + e * pv);
                }
            }
        }

        // Arrow keys: moves the selection by whole reference units. The designer commits afterwards.
        public void Nudge(int dx, int dy)
        {
            if (_sel == null) return;
            for (int a = 0; a < 2; a++)
            {
                float d = a == 0 ? dx : dy;
                _sel.Offset[a] += d;
                if (DesignDoc.Stretched(_sel, a)) _sel.Size[a] -= d;
            }
            Invalidate();
        }

        // ---------------- keyboard (arrows must be declared as input keys) ----------------
        protected override bool IsInputKey(Keys keyData)
        {
            switch (keyData & Keys.KeyCode)
            {
                case Keys.Left: case Keys.Right: case Keys.Up: case Keys.Down: return true;
            }
            return base.IsInputKey(keyData);
        }

        // ---------------- drops from the palette ----------------
        private static string? Payload(DragEventArgs e)
        {
            string? s = e.Data?.GetData(DataFormats.StringFormat) as string;
            return (s != null && s.StartsWith("ghostui:")) ? s.Substring(8) : null;
        }

        protected override void OnDragEnter(DragEventArgs e) =>
            e.Effect = Payload(e) != null ? DragDropEffects.Copy : DragDropEffects.None;

        protected override void OnDragOver(DragEventArgs e) =>
            e.Effect = Payload(e) != null ? DragDropEffects.Copy : DragDropEffects.None;

        protected override void OnDragDrop(DragEventArgs e)
        {
            string? type = Payload(e);
            if (type == null) return;
            Point pt = PointToClient(new Point(e.X, e.Y));
            ComputeView();
            _doc.Layout();
            float rx = Math.Clamp(RefX(pt.X), 0f, _doc.RefW), ry = Math.Clamp(RefY(pt.Y), 0f, 1080f);
            WidgetDropped?.Invoke(type, new PointF(rx, ry), CanvasAt(rx, ry));
            Focus();
        }
    }
}