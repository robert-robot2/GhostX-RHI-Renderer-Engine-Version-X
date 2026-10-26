// GhostViewportUI.cs — the two dropdowns floating over the viewport: tool (left) and view options (right)
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Windows.Forms;

namespace GhostShell
{
    // A button that never takes keyboard focus, so WASD keeps working after a click
    internal sealed class OverlayButton : Button
    {
        public OverlayButton()
        {
            SetStyle(ControlStyles.Selectable, false);
            TabStop = false;
            FlatStyle = FlatStyle.Flat;
            BackColor = Color.FromArgb(0x2A, 0x2A, 0x30);
            ForeColor = Color.FromArgb(0xF2, 0xF2, 0xF2);
            Font = new Font("Segoe UI", 9f);
            Size = new Size(118, 26);
            Cursor = Cursors.Hand;
            FlatAppearance.BorderColor = Color.FromArgb(0x55, 0x55, 0x5E);
        }
    }

    internal sealed class GhostViewportBar
    {
        private delegate void Change(ref GhostViewSettings s);

        private static readonly string[] Tools = { "Select", "Translate", "Rotate", "Scale", "Dimension" };

        private readonly Control _host;
        private readonly OverlayButton _toolBtn = new(), _viewBtn = new();
        private readonly ContextMenuStrip _toolMenu = new(), _viewMenu = new();
        private readonly List<(ToolStripMenuItem item, Func<GhostViewSettings, bool> on)> _marks = new();

        private delegate void ChangeG(ref GhostGizmoSettings g);
        private readonly List<(ToolStripMenuItem item, Func<GhostGizmoSettings, bool> on)> _marksG = new();


        public GhostViewportBar(Control host)
        {
            _host = host;
            Color bg = Color.FromArgb(0x26, 0x26, 0x2A), fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
            foreach (ContextMenuStrip m in new[] { _toolMenu, _viewMenu })
            {
                m.Renderer = new GhostMenuRenderer(bg, fg);
                m.ShowImageMargin = false;
                m.ShowCheckMargin = true;
                m.Font = new Font("Segoe UI", 9.5f);
                m.Opening += (s, e) => Mark();
                m.Closed += (s, e) => _host.Select();               // keyboard focus goes back to the viewport
            }

            BuildToolMenu();
            BuildViewMenu();

            _toolBtn.Click += (s, e) => _toolMenu.Show(_toolBtn, new Point(0, _toolBtn.Height));
            _viewBtn.Click += (s, e) => _viewMenu.Show(_viewBtn, new Point(0, _viewBtn.Height));

            host.Controls.Add(_toolBtn);
            host.Controls.Add(_viewBtn);
            _toolBtn.BringToFront();
            _viewBtn.BringToFront();
            host.Resize += (s, e) => Place();
            Place();
            Sync();
        }

        private void Place()
        {
            _toolBtn.Location = new Point(10, 10);
            _viewBtn.Location = new Point(Math.Max(_host.ClientSize.Width - _viewBtn.Width - 10, 140), 10);
        }

        // ---------------- menus ----------------
        private ToolStripMenuItem Add(ToolStripItemCollection into, string text, Func<GhostViewSettings, bool> on, Change apply)
        {
            var it = new ToolStripMenuItem(text);
            it.Click += (s, e) => Edit(apply);
            into.Add(it);
            _marks.Add((it, on));
            return it;
        }

        private static ToolStripMenuItem Sub(ToolStripItemCollection into, string text)
        {
            var it = new ToolStripMenuItem(text);
            into.Add(it);
            return it;
        }

        private ToolStripMenuItem AddG(ToolStripItemCollection into, string text, Func<GhostGizmoSettings, bool> on, ChangeG apply)
        {
            var it = new ToolStripMenuItem(text);
            it.Click += (s, e) => EditG(apply);
            into.Add(it);
            _marksG.Add((it, on));
            return it;
        }

        private void EditG(ChangeG c)
        {
            GhostEngine.GhostEngine_GetGizmoSettings(out GhostGizmoSettings g);
            c(ref g);
            GhostEngine.GhostEngine_SetGizmoSettings(ref g);
            PersistG();
        }

        private static void PersistG()
        {
            GhostEngine.GhostEngine_GetGizmoSettings(out GhostGizmoSettings g);        // read back the clamped values
            CultureInfo I = CultureInfo.InvariantCulture;
            GhostSettings.Data.GizmoSettings = string.Join(",",
                g.snap.ToString(I), g.moveStep.ToString("R", I), g.rotStep.ToString("R", I), g.scaleStep.ToString("R", I));
            GhostSettings.Save();
        }

        private void BuildToolMenu()
        {
            string[] names = { "Select", "Translate (move)", "Rotate", "Scale", "Dimension" };
            for (int i = 0; i < names.Length; i++)
            {
                int t = i;
                Add(_toolMenu.Items, names[i], s => s.tool == t, (ref GhostViewSettings s) => s.tool = t);
            }
            _toolMenu.Items.Add(new ToolStripSeparator());
            Add(_toolMenu.Items, "Gizmo space: World", s => s.space == 0, (ref GhostViewSettings s) => s.space = 0);
            Add(_toolMenu.Items, "Gizmo space: Local", s => s.space == 1, (ref GhostViewSettings s) => s.space = 1);
            _toolMenu.Items.Add(new ToolStripSeparator());
            AddG(_toolMenu.Items, "Snapping  (hold Shift while dragging to flip it)", g => g.snap != 0, (ref GhostGizmoSettings g) => g.snap ^= 1);

            ToolStripMenuItem mv = Sub(_toolMenu.Items, "Move snap");
            AddG(mv.DropDownItems, "Grid size", g => g.moveStep <= 0f, (ref GhostGizmoSettings g) => g.moveStep = 0f);
            foreach (float sz in new[] { 0.01f, 0.1f, 0.25f, 0.5f, 1f, 5f })
            {
                float v = sz;
                AddG(mv.DropDownItems, v.ToString("0.##", CultureInfo.InvariantCulture),
                    g => Math.Abs(g.moveStep - v) < 1e-4f, (ref GhostGizmoSettings g) => g.moveStep = v);
            }

            ToolStripMenuItem rt = Sub(_toolMenu.Items, "Rotate snap (degrees)");
            foreach (float deg in new[] { 1f, 5f, 15f, 45f, 90f })
            {
                float v = deg;
                AddG(rt.DropDownItems, v.ToString("0.##", CultureInfo.InvariantCulture),
                    g => Math.Abs(g.rotStep - v) < 1e-4f, (ref GhostGizmoSettings g) => g.rotStep = v);
            }

            ToolStripMenuItem sc = Sub(_toolMenu.Items, "Scale snap");
            foreach (float st in new[] { 0.01f, 0.05f, 0.1f, 0.25f, 0.5f })
            {
                float v = st;
                AddG(sc.DropDownItems, v.ToString("0.##", CultureInfo.InvariantCulture),
                    g => Math.Abs(g.scaleStep - v) < 1e-4f, (ref GhostGizmoSettings g) => g.scaleStep = v);
            }
        }

        private void BuildViewMenu()
        {
            ToolStripItemCollection it = _viewMenu.Items;
            Add(it, "Selection (origin, axes, bounds)", s => s.showSelection != 0, (ref GhostViewSettings s) => s.showSelection ^= 1);
            Add(it, "FPS", s => s.showFps != 0, (ref GhostViewSettings s) => s.showFps ^= 1);
            Add(it, "Grid", s => s.showGrid != 0, (ref GhostViewSettings s) => s.showGrid ^= 1);
            Add(it, "World axes", s => s.showWorldAxes != 0, (ref GhostViewSettings s) => s.showWorldAxes ^= 1);
            it.Add(new ToolStripSeparator());

            ToolStripMenuItem dims = Sub(it, "Dimensions");
            Add(dims.DropDownItems, "Show dimensions", s => s.showDims != 0, (ref GhostViewSettings s) => s.showDims ^= 1);
            Add(dims.DropDownItems, "X axis", s => (s.dimAxes & 1) != 0, (ref GhostViewSettings s) => s.dimAxes ^= 1);
            Add(dims.DropDownItems, "Y axis", s => (s.dimAxes & 2) != 0, (ref GhostViewSettings s) => s.dimAxes ^= 2);
            Add(dims.DropDownItems, "Z axis", s => (s.dimAxes & 4) != 0, (ref GhostViewSettings s) => s.dimAxes ^= 4);
            Add(dims.DropDownItems, "Polygon edge labels (A, B, C...)", s => s.edgeLabels != 0, (ref GhostViewSettings s) => s.edgeLabels ^= 1);

            ToolStripMenuItem size = Sub(it, "Grid size");
            foreach (float sz in new[] { 0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1f, 2f, 5f, 10f, 25f, 50f, 100f })
            {
                float v = sz;
                Add(size.DropDownItems, v.ToString("0.##", CultureInfo.InvariantCulture),
                    s => Math.Abs(s.gridSize - v) < 1e-4f, (ref GhostViewSettings s) => s.gridSize = v);
            }

            ToolStripMenuItem plane = Sub(it, "Grid plane (axis)");
            Add(plane.DropDownItems, "XZ  (floor)", s => s.gridPlane == 0, (ref GhostViewSettings s) => s.gridPlane = 0);
            Add(plane.DropDownItems, "XY  (front wall)", s => s.gridPlane == 1, (ref GhostViewSettings s) => s.gridPlane = 1);
            Add(plane.DropDownItems, "YZ  (side wall)", s => s.gridPlane == 2, (ref GhostViewSettings s) => s.gridPlane = 2);

            ToolStripMenuItem ext = Sub(it, "Grid extent (lines each side)");
            foreach (int n in new[] { 10, 20, 50, 100 })
            {
                int v = n;
                Add(ext.DropDownItems, v.ToString(CultureInfo.InvariantCulture), s => s.gridExtent == v, (ref GhostViewSettings s) => s.gridExtent = v);
            }

            ToolStripMenuItem style = Sub(it, "Grid style");
            Add(style.DropDownItems, "Standard", s => s.gridStyle == 0, (ref GhostViewSettings s) => s.gridStyle = 0);
            Add(style.DropDownItems, "Blueprint (CAD)", s => s.gridStyle == 1, (ref GhostViewSettings s) => s.gridStyle = 1);

            ToolStripMenuItem units = Sub(it, "Units  (1 engine unit = 1 metre)");
            string[] un = { "Millimetres (mm)", "Centimetres (cm)", "Metres (m)", "Inches (in)", "Feet (ft)" };
            for (int i = 0; i < un.Length; i++)
            {
                int v = i;
                Add(units.DropDownItems, un[i], s => s.unit == v, (ref GhostViewSettings s) => s.unit = v);
            }

            ToolStripMenuItem dec = Sub(it, "Decimals");
            for (int i = 0; i <= 3; i++)
            {
                int v = i;
                Add(dec.DropDownItems, v.ToString(CultureInfo.InvariantCulture), s => s.decimals == v, (ref GhostViewSettings s) => s.decimals = v);
            }

            it.Add(new ToolStripSeparator());
            var reset = new ToolStripMenuItem("Reset view settings");
            reset.Click += (s, e) =>
            {
                var d = new GhostViewSettings
                {
                    showSelection = 1,
                    showGrid = 1,
                    showWorldAxes = 1,
                    dimAxes = 7,
                    edgeLabels = 1,
                    gridSize = 1f,
                    gridExtent = 20,
                    unit = 2,
                    decimals = 2,
                };
                GhostEngine.GhostEngine_SetViewSettings(ref d);
                Persist();
                Sync();
            };
            it.Add(reset);
        }

        // ---------------- state ----------------
        private void Edit(Change c)
        {
            GhostEngine.GhostEngine_GetViewSettings(out GhostViewSettings s);
            c(ref s);
            GhostEngine.GhostEngine_SetViewSettings(ref s);
            Persist();
            Sync();
        }

        private void Mark()
        {
            GhostEngine.GhostEngine_GetViewSettings(out GhostViewSettings s);
            foreach (var (item, on) in _marks) item.Checked = on(s);
            GhostEngine.GhostEngine_GetGizmoSettings(out GhostGizmoSettings gs);
            foreach (var (item, on) in _marksG) item.Checked = on(gs);
        }

        private void Sync()
        {
            GhostEngine.GhostEngine_GetViewSettings(out GhostViewSettings s);
            _toolBtn.Text = Tools[Math.Clamp(s.tool, 0, Tools.Length - 1)] + "  \u25BE";
            _viewBtn.Text = "View  \u25BE";
        }

        // ---------------- saved with the user's settings ----------------
        private static void Persist()
        {
            GhostEngine.GhostEngine_GetViewSettings(out GhostViewSettings s);          // read back, so the saved values are the clamped ones
            CultureInfo I = CultureInfo.InvariantCulture;
            GhostSettings.Data.ViewSettings = string.Join(",",
                s.showSelection.ToString(I), s.showFps.ToString(I), s.showGrid.ToString(I), s.showWorldAxes.ToString(I),
                s.showDims.ToString(I), s.dimAxes.ToString(I), s.edgeLabels.ToString(I),
                s.gridPlane.ToString(I), s.gridStyle.ToString(I), s.gridSize.ToString("R", I), s.gridExtent.ToString(I),
                s.tool.ToString(I), s.space.ToString(I), s.unit.ToString(I), s.decimals.ToString(I));
            GhostSettings.Save();
        }

        private static bool TryParse(string text, out GhostViewSettings s)
        {
            s = default;
            string[] p = (text ?? "").Split(',');
            if (p.Length != 15) return false;
            CultureInfo I = CultureInfo.InvariantCulture;
            var v = new int[15];
            for (int i = 0; i < 15; i++)
            {
                if (i == 9) continue;
                if (!int.TryParse(p[i], NumberStyles.Integer, I, out v[i])) return false;
            }
            if (!float.TryParse(p[9], NumberStyles.Float, I, out float size)) return false;
            s = new GhostViewSettings
            {
                showSelection = v[0],
                showFps = v[1],
                showGrid = v[2],
                showWorldAxes = v[3],
                showDims = v[4],
                dimAxes = v[5],
                edgeLabels = v[6],
                gridPlane = v[7],
                gridStyle = v[8],
                gridSize = size,
                gridExtent = v[10],
                tool = v[11],
                space = v[12],
                unit = v[13],
                decimals = v[14],
            };
            return true;
        }

        // Called once the engine is running
        public void ApplyStartup()
        {
            if (TryParse(GhostSettings.Data.ViewSettings, out GhostViewSettings s))
                GhostEngine.GhostEngine_SetViewSettings(ref s);
            string[] gp = (GhostSettings.Data.GizmoSettings ?? "").Split(',');
            CultureInfo ci = CultureInfo.InvariantCulture;
            if (gp.Length == 4 &&
                int.TryParse(gp[0], NumberStyles.Integer, ci, out int gSnap) &&
                float.TryParse(gp[1], NumberStyles.Float, ci, out float gMove) &&
                float.TryParse(gp[2], NumberStyles.Float, ci, out float gRot) &&
                float.TryParse(gp[3], NumberStyles.Float, ci, out float gScale))
            {
                var g = new GhostGizmoSettings { snap = gSnap, moveStep = gMove, rotStep = gRot, scaleStep = gScale };
                GhostEngine.GhostEngine_SetGizmoSettings(ref g);
            }
            Sync();
        }
    }
}