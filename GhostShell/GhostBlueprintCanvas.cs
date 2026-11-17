// GhostBlueprintCanvas.cs — node graph editor (pan, zoom, drag nodes, wire pins, drag & drop)
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class GhostBlueprintCanvas : Control
    {
        private const float HeadH = 24f, NodeW = 172f, BodyH = 36f;
        private GraphDoc _g = new();
        private PointF _pan = new(80, 80);
        private float _zoom = 1f;
        private GraphNodeDoc? _dragNode, _wireFrom;
        private PointF _dragOff, _wireTo;
        private bool _panning, _rmbMoved;
        private Point _panLast;
        private readonly Font _title = new("Segoe UI", 9f, FontStyle.Bold), _small = new("Segoe UI", 8f);

        [Browsable(false)] public GraphNodeDoc? Selected { get; private set; }
        [Browsable(false)] public GraphLinkDoc? SelectedLink { get; private set; }
        public event Action? GraphChanged;
        public event Action<GraphNodeDoc?>? SelectionChanged;
        public Func<IEnumerable<string>>? ClipNames;

        public GhostBlueprintCanvas()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
            BackColor = Color.FromArgb(0x1B, 0x1B, 0x1E);
            AllowDrop = true;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) { _title.Dispose(); _small.Dispose(); }
            base.Dispose(disposing);
        }

        public void Bind(GraphDoc g) { _g = g; Selected = null; SelectedLink = null; _wireFrom = null; FrameAll(); SelectionChanged?.Invoke(null); }

        public void FrameAll()
        {
            if (_g.Nodes.Count == 0) return;
            _pan = new PointF(60 - _g.Nodes.Min(n => n.X) * _zoom, 60 - _g.Nodes.Min(n => n.Y) * _zoom);
            Invalidate();
        }

        // ---------------- geometry ----------------
        private static SizeF SizeOf(GraphNodeDoc n) =>
            n.Type is GNodeType.Entry or GNodeType.Any ? new SizeF(110, HeadH + 6) : new SizeF(NodeW, HeadH + BodyH);
        private static PointF InPin(GraphNodeDoc n) => new(n.X, n.Y + HeadH / 2);
        private static PointF OutPin(GraphNodeDoc n) => new(n.X + SizeOf(n).Width, n.Y + HeadH / 2);
        private static float Dist(PointF a, PointF b) => MathF.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));
        private static float Snap(float v) => MathF.Round(v / 8f) * 8f;
        private PointF ToWorld(Point p) => new((p.X - _pan.X) / _zoom, (p.Y - _pan.Y) / _zoom);

        private static PointF Bez(PointF a, PointF b, PointF c, PointF d, float t)
        {
            float u = 1 - t;
            return new PointF(u * u * u * a.X + 3 * u * u * t * b.X + 3 * u * t * t * c.X + t * t * t * d.X,
                              u * u * u * a.Y + 3 * u * u * t * b.Y + 3 * u * t * t * c.Y + t * t * t * d.Y);
        }
        private static void WirePoints(PointF p0, PointF p1, out PointF c0, out PointF c1)
        {
            float dx = Math.Max(40f, Math.Abs(p1.X - p0.X) * 0.5f);
            c0 = new PointF(p0.X + dx, p0.Y); c1 = new PointF(p1.X - dx, p1.Y);
        }

        private GraphNodeDoc? NodeAt(PointF w)
        {
            for (int i = _g.Nodes.Count - 1; i >= 0; i--)
            {
                var n = _g.Nodes[i]; var s = SizeOf(n);
                if (new RectangleF(n.X, n.Y, s.Width, s.Height).Contains(w)) return n;
            }
            return null;
        }
        private GraphNodeDoc? OutPinAt(PointF w) => _g.Nodes.FirstOrDefault(n => n.HasOut && Dist(OutPin(n), w) <= 10f);
        private GraphLinkDoc? LinkAt(PointF w)
        {
            foreach (var l in _g.Links)
            {
                var a = _g.Find(l.From); var b = _g.Find(l.To);
                if (a == null || b == null) continue;
                PointF p0 = OutPin(a), p1 = InPin(b);
                WirePoints(p0, p1, out var c0, out var c1);
                for (int i = 0; i <= 24; i++) if (Dist(Bez(p0, c0, c1, p1, i / 24f), w) < 7f / _zoom) return l;
            }
            return null;
        }

        // ---------------- appearance ----------------
        private static Color HeaderColor(GNodeType t) => t switch
        {
            GNodeType.Entry => Color.FromArgb(0x2E, 0x8B, 0x57),
            GNodeType.State => Color.FromArgb(0x3A, 0x6E, 0xB5),
            GNodeType.Any => Color.FromArgb(0x8E, 0x44, 0xAD),
            GNodeType.Blend => Color.FromArgb(0x2A, 0x9D, 0x8F),
            GNodeType.Squash => Color.FromArgb(0xE0, 0x8E, 0x2B),
            GNodeType.Speed => Color.FromArgb(0xC0, 0x5A, 0x2E),
            GNodeType.Sound => Color.FromArgb(0xB0, 0x3A, 0x6E),
            GNodeType.Rumble => Color.FromArgb(0x7A, 0x7A, 0x2E),
            _ => Color.FromArgb(0x6A, 0x4C, 0xB0),                      // Turn, Lean: global
        };
        public static string TitleOf(GraphNodeDoc n) => n.Type switch
        {
            GNodeType.Entry => "Entry",
            GNodeType.Any => "Any State",
            GNodeType.State => n.Name,
            GNodeType.Blend => "Blend",
            GNodeType.Turn => "Turn Smoothing",
            GNodeType.Lean => "Lean",
            GNodeType.Squash => "Squash & Stretch",
            GNodeType.Speed => "Speed Ramp",
            GNodeType.Sound => "Play Sound",
            _ => "Rumble",
        };
        private static string Summary(GraphNodeDoc n) => n.Type switch
        {
            GNodeType.State => n.Clip.Length > 0 ? "clip: " + n.Clip : "clip: (same as name)",
            GNodeType.Blend => $"{n.P[0]:0.##}s  {new[] { "linear", "ease in", "ease out", "smooth" }[Math.Clamp(n.Curve, 0, 3)]}",
            GNodeType.Squash => $"{(n.P[2] > 0.5f ? "squash" : "stretch")} {n.P[0] * 100:0}%  {n.P[1]:0.##}s",
            GNodeType.Speed => $"x{n.P[0]:0.##} to x{n.P[1]:0.##} in {n.P[2]:0.##}s",
            GNodeType.Sound => n.Name.Length > 0 ? n.Name : "(pick a sound name)",
            GNodeType.Rumble => $"{n.P[0]:0.#} / {n.P[1]:0.#}  for {n.P[2]:0.##}s",
            GNodeType.Turn => $"settle {n.P[0]:0.##}s  (global)",
            GNodeType.Lean => $"max {n.P[0]:0} deg  x{n.P[1]:0.##}  (global)",
            _ => "",
        };

        private static GraphicsPath Round(RectangleF r, float rad)
        {
            var p = new GraphicsPath(); float d = rad * 2;
            p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure(); return p;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(BackColor);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TranslateTransform(_pan.X, _pan.Y);
            g.ScaleTransform(_zoom, _zoom);

            // grid
            float x0 = -_pan.X / _zoom, y0 = -_pan.Y / _zoom, x1 = x0 + Width / _zoom, y1 = y0 + Height / _zoom;
            using (var minor = new Pen(Color.FromArgb(0x26, 0x26, 0x2A))) using (var major = new Pen(Color.FromArgb(0x34, 0x34, 0x3A)))
            {
                for (float x = MathF.Floor(x0 / 32) * 32; x < x1; x += 32) g.DrawLine(((long)(x / 32) % 4 == 0) ? major : minor, x, y0, x, y1);
                for (float y = MathF.Floor(y0 / 32) * 32; y < y1; y += 32) g.DrawLine(((long)(y / 32) % 4 == 0) ? major : minor, x0, y, x1, y);
            }

            // wires
            foreach (var l in _g.Links)
            {
                var a = _g.Find(l.From); var b = _g.Find(l.To);
                if (a == null || b == null) continue;
                PointF p0 = OutPin(a), p1 = InPin(b);
                WirePoints(p0, p1, out var c0, out var c1);
                bool sel = l == SelectedLink;
                using var pen = new Pen(sel ? Color.FromArgb(0xFF, 0xC8, 0x6E) : Color.FromArgb(0xE8, 0xE8, 0xEE), sel ? 3.2f : 2f);
                g.DrawBezier(pen, p0, c0, c1, p1);
            }
            if (_wireFrom != null)
            {
                PointF p0 = OutPin(_wireFrom);
                WirePoints(p0, _wireTo, out var c0, out var c1);
                using var pen = new Pen(Color.FromArgb(0xFF, 0xC8, 0x6E), 2f) { DashStyle = DashStyle.Dash };
                g.DrawBezier(pen, p0, c0, c1, _wireTo);
            }

            // nodes
            foreach (var n in _g.Nodes)
            {
                var s = SizeOf(n);
                var body = new RectangleF(n.X, n.Y, s.Width, s.Height);
                using (var path = Round(body, 6))
                {
                    using (var shadow = new SolidBrush(Color.FromArgb(70, 0, 0, 0))) g.FillRectangle(shadow, body.X + 3, body.Y + 3, body.Width, body.Height);
                    using (var fill = new SolidBrush(Color.FromArgb(0x2B, 0x2B, 0x30))) g.FillPath(fill, path);
                    var head = new RectangleF(n.X, n.Y, s.Width, HeadH);
                    var oldClip = g.Clip; g.SetClip(path, CombineMode.Intersect); g.SetClip(head, CombineMode.Intersect);
                    using (var hb = new SolidBrush(HeaderColor(n.Type))) g.FillRectangle(hb, head);
                    g.Clip = oldClip;
                    using var border = new Pen(n == Selected ? Color.FromArgb(0xFF, 0xC8, 0x6E) : Color.FromArgb(0x50, 0x50, 0x58), n == Selected ? 2.4f : 1f);
                    g.DrawPath(border, path);
                }
                g.DrawString(TitleOf(n), _title, Brushes.White, n.X + 18, n.Y + 4);
                if (s.Height > HeadH + 10)
                    g.DrawString(Summary(n), _small, Brushes.Silver, new RectangleF(n.X + 8, n.Y + HeadH + 8, s.Width - 14, BodyH - 8));
                if (n.HasIn) DrawPin(g, InPin(n), false);
                if (n.HasOut) DrawPin(g, OutPin(n), n == _wireFrom);
            }
        }

        private static void DrawPin(Graphics g, PointF p, bool hot)
        {
            using var b = new SolidBrush(hot ? Color.FromArgb(0xFF, 0xC8, 0x6E) : Color.White);
            g.FillEllipse(b, p.X - 6, p.Y - 6, 12, 12);
            using var pen = new Pen(Color.FromArgb(0x20, 0x20, 0x24), 2f);
            g.DrawEllipse(pen, p.X - 6, p.Y - 6, 12, 12);
        }

        // ---------------- selection / editing ----------------
        private void Select(GraphNodeDoc? n, GraphLinkDoc? l = null)
        {
            bool nodeChanged = n != Selected;
            Selected = n; SelectedLink = l;
            if (nodeChanged) SelectionChanged?.Invoke(n);
            Invalidate();
        }

        public GraphNodeDoc AddNode(GNodeType t, PointF world, string? clip = null)
        {
            var n = _g.Add(t, Snap(world.X), Snap(world.Y));
            if (clip != null) { n.Name = clip; n.Clip = clip; }
            Select(n); GraphChanged?.Invoke(); Invalidate();
            return n;
        }

        public void DeleteSelection()
        {
            if (SelectedLink != null) { _g.Links.Remove(SelectedLink); SelectedLink = null; }
            else if (Selected != null && Selected.Type != GNodeType.Entry) { _g.Remove(Selected); Selected = null; SelectionChanged?.Invoke(null); }
            else return;
            GraphChanged?.Invoke(); Invalidate();
        }

        // ---------------- mouse ----------------
        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e); Focus();
            if (e.Button == MouseButtons.Middle || e.Button == MouseButtons.Right)
            { _panning = true; _rmbMoved = false; _panLast = e.Location; return; }
            if (e.Button != MouseButtons.Left) return;

            PointF w = ToWorld(e.Location);
            var pin = OutPinAt(w);
            if (pin != null) { _wireFrom = pin; _wireTo = w; Select(pin); return; }
            var n = NodeAt(w);
            if (n != null) { Select(n); _dragNode = n; _dragOff = new PointF(w.X - n.X, w.Y - n.Y); return; }
            var l = LinkAt(w);
            Select(null, l);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (_panning)
            {
                _pan = new PointF(_pan.X + e.X - _panLast.X, _pan.Y + e.Y - _panLast.Y);
                if (Math.Abs(e.X - _panLast.X) + Math.Abs(e.Y - _panLast.Y) > 0) _rmbMoved = true;
                _panLast = e.Location; Invalidate(); return;
            }
            PointF w = ToWorld(e.Location);
            if (_dragNode != null)
            {
                _dragNode.X = Snap(w.X - _dragOff.X); _dragNode.Y = Snap(w.Y - _dragOff.Y);
                Invalidate();
            }
            else if (_wireFrom != null) { _wireTo = w; Invalidate(); }
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            if (_panning && (e.Button == MouseButtons.Middle || e.Button == MouseButtons.Right))
            {
                _panning = false;
                if (e.Button == MouseButtons.Right && !_rmbMoved) ShowMenu(e.Location);
                return;
            }
            if (e.Button != MouseButtons.Left) return;
            if (_wireFrom != null)
            {
                var target = NodeAt(ToWorld(e.Location));
                if (target != null && _g.TryLink(_wireFrom, target)) GraphChanged?.Invoke();
                _wireFrom = null; Invalidate();
            }
            if (_dragNode != null) { _dragNode = null; GraphChanged?.Invoke(); }
        }

        protected override void OnMouseWheel(MouseEventArgs e)
        {
            base.OnMouseWheel(e);
            PointF w = ToWorld(e.Location);
            _zoom = Math.Clamp(_zoom * (e.Delta > 0 ? 1.1f : 0.9f), 0.35f, 2.2f);
            _pan = new PointF(e.X - w.X * _zoom, e.Y - w.Y * _zoom);
            Invalidate();
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Delete) { DeleteSelection(); e.Handled = true; }
            if (e.KeyCode == Keys.F) { FrameAll(); e.Handled = true; }
        }

        // ---------------- context menu + drag & drop ----------------
        private void ShowMenu(Point screenPt)
        {
            PointF w = ToWorld(screenPt);
            var m = new ContextMenuStrip();
            var states = (ToolStripMenuItem)m.Items.Add("State from clip");
            foreach (string c in ClipNames?.Invoke() ?? Enumerable.Empty<string>())
            { string cn = c; states.DropDownItems.Add(cn, null, (s, ev) => AddNode(GNodeType.State, w, cn)); }
            states.DropDownItems.Add("Empty state", null, (s, ev) => AddNode(GNodeType.State, w));
            m.Items.Add(new ToolStripSeparator());
            void Add(string text, GNodeType t) => m.Items.Add(text, null, (s, ev) => AddNode(t, w));
            Add("Any State", GNodeType.Any);
            Add("Blend (crossfade)", GNodeType.Blend);
            Add("Squash && Stretch", GNodeType.Squash);
            Add("Speed Ramp", GNodeType.Speed);
            Add("Play Sound", GNodeType.Sound);
            Add("Rumble", GNodeType.Rumble);
            m.Items.Add(new ToolStripSeparator());
            Add("Turn Smoothing (global)", GNodeType.Turn);
            Add("Lean (global)", GNodeType.Lean);
            m.Show(this, screenPt);
        }

        protected override void OnDragEnter(DragEventArgs e)
        {
            base.OnDragEnter(e);
            e.Effect = e.Data?.GetData(DataFormats.StringFormat) is string s && (s.StartsWith("ghostclip:") || s.StartsWith("ghostnode:"))
                ? DragDropEffects.Copy : DragDropEffects.None;
        }
        protected override void OnDragOver(DragEventArgs e) { base.OnDragOver(e); OnDragEnter(e); }
        protected override void OnDragDrop(DragEventArgs e)
        {
            base.OnDragDrop(e);
            if (e.Data?.GetData(DataFormats.StringFormat) is not string s) return;
            PointF w = ToWorld(PointToClient(new Point(e.X, e.Y)));
            if (s.StartsWith("ghostclip:")) AddNode(GNodeType.State, w, s.Substring("ghostclip:".Length));
            else if (s.StartsWith("ghostnode:") && int.TryParse(s.Substring("ghostnode:".Length), out int t)) AddNode((GNodeType)t, w);
        }
    }
}