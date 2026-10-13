// GhostViewports.cs — camera windows: one dockable panel + one engine viewport per camera
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;

namespace GhostShell
{
    internal sealed class CameraWindow
    {
        public int CameraId;
        public int ViewportId = -1;
        public IntPtr BoundHandle = IntPtr.Zero;
        public ViewportPanel Panel = null!;
        public PanelDock Window = null!;
    }

    internal sealed class GhostViewportManager
    {
        public static GhostViewportManager? Instance { get; private set; }

        private const DockAreas Areas = DockAreas.Float | DockAreas.Document | DockAreas.DockLeft |
                                        DockAreas.DockRight | DockAreas.DockTop | DockAreas.DockBottom;

        private readonly DockPanel _dock;
        private readonly Control _main;
        private readonly Dictionary<int, CameraWindow> _windows = new();
        private int _spawnCount;

        public GhostViewportManager(DockPanel dock, Control mainViewport)
        {
            _dock = dock;
            _main = mainViewport;
            Instance = this;
        }

        // -------------------------------------------------------
        public bool IsShown(int cameraId) =>
            _windows.TryGetValue(cameraId, out var w) &&
            w.Window.DockPanel != null && !w.Window.IsHidden && w.Panel.Visible;

        public void Open(int cameraId) => SetShown(cameraId, true);

        public void SetShown(int cameraId, bool shown)
        {
            if (!_windows.TryGetValue(cameraId, out var w))
            {
                if (shown) Create(cameraId);
                return;
            }
            if (!shown) { w.Window.IsHidden = true; return; }

            if (w.Window.DockState == DockState.Unknown) w.Window.Show(_dock, DockState.Float);
            else w.Window.Show(_dock);
        }

        // Closes the window of every camera that no longer exists
        public void SyncCameras(HashSet<int> liveCameraIds)
        {
            foreach (int id in _windows.Keys.Where(k => !liveCameraIds.Contains(k)).ToList())
            {
                CameraWindow w = _windows[id];
                _windows.Remove(id);
                if (w.ViewportId > 0)
                {
                    GhostEngine.GhostEngine_DestroyViewport(w.ViewportId);   // before the window goes away
                    w.ViewportId = -1;
                }
                w.Window.HideOnClose = false;
                w.Window.Close();
            }
        }

        // Which viewport has keyboard focus with the cursor over it (-1 = none). Drives camera input.
        public int HotViewport()
        {
            if (IsHot(_main)) return 0;
            foreach (CameraWindow w in _windows.Values)
                if (w.ViewportId > 0 && IsHot(w.Panel)) return w.ViewportId;
            return -1;
        }

        private static bool IsHot(Control c) =>
      c.IsHandleCreated && c.Focused &&
      (GhostEngine.GhostEngine_GetPlayState() == 1 || c.ClientRectangle.Contains(c.PointToClient(Cursor.Position)));

        // -------------------------------------------------------
        private void Create(int cameraId)
        {
            var w = new CameraWindow { CameraId = cameraId };
            w.Panel = new ViewportPanel { BackColor = Color.Black };

            w.Panel.HandleCreated += (s, e) => EnsureSurface(w);
            w.Panel.HandleDestroyed += (s, e) =>
            {
                if (w.ViewportId > 0) GhostEngine.GhostEngine_SetViewportVisible(w.ViewportId, 0);
            };
            w.Panel.VisibleChanged += (s, e) => PushVisible(w);
            w.Panel.Resize += (s, e) => PushSize(w);

            w.Panel.MouseEnter += (s, e) =>
            {
                if (Form.ActiveForm != null && Form.ActiveForm == w.Panel.TopLevelControl && !GhostEditorUI.IsTyping())
                    w.Panel.Select();
            };
            w.Panel.MouseDown += (s, e) =>
            {
                w.Panel.Select();
                if (e.Button == MouseButtons.Left) GhostEngine.GhostEngine_SelectEntity(w.CameraId);
            };

            w.Window = new PanelDock("Camera:" + cameraId, GhostNames.Of(cameraId), w.Panel, Areas);
            _windows[cameraId] = w;

            // First placement: floating, stacked up from the bottom-right corner of the main viewport
            Rectangle vb = _main.RectangleToScreen(_main.ClientRectangle);
            int n = _spawnCount++ % 4;
            var bounds = new Rectangle(
                Math.Max(vb.Left, vb.Right - 480 - 16 - n * 28),
                Math.Max(vb.Top, vb.Bottom - 300 - 16 - n * 28), 480, 300);
            w.Window.Show(_dock, bounds);

            EnsureSurface(w);
        }

        // Creates the engine viewport once the panel has a window handle,
        // or hands the swap chain the new handle if WinForms recreated it
        private void EnsureSurface(CameraWindow w)
        {
            if (!w.Panel.IsHandleCreated) return;
            IntPtr handle = w.Panel.Handle;
            if (w.ViewportId > 0 && w.BoundHandle == handle) return;

            uint cw = (uint)Math.Max(w.Panel.ClientSize.Width, 1);
            uint ch = (uint)Math.Max(w.Panel.ClientSize.Height, 1);

            if (w.ViewportId <= 0)
            {
                int id = GhostEngine.GhostEngine_CreateViewport(handle, cw, ch, w.CameraId);
                if (id <= 0) { System.Media.SystemSounds.Beep.Play(); return; }
                w.ViewportId = id;
            }
            else
            {
                GhostEngine.GhostEngine_RebindViewport(w.ViewportId, handle, cw, ch);
            }
            w.BoundHandle = handle;
            PushVisible(w);
        }

        private static void PushVisible(CameraWindow w)
        {
            if (w.ViewportId > 0)
                GhostEngine.GhostEngine_SetViewportVisible(w.ViewportId, w.Panel.IsHandleCreated && w.Panel.Visible ? 1 : 0);
        }

        private static void PushSize(CameraWindow w)
        {
            if (w.ViewportId > 0 && w.Panel.ClientSize.Width > 0 && w.Panel.ClientSize.Height > 0)
                GhostEngine.GhostEngine_ResizeViewport(w.ViewportId,
                    (uint)w.Panel.ClientSize.Width, (uint)w.Panel.ClientSize.Height);
        }
    }
}
