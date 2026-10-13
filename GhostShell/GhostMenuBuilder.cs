using System;
using System.Drawing;
using System.Windows.Forms;

namespace GhostShell
{
    public class GhostMenuBuilder
    {
        // ---- Theme state ----
        private Color _menuBgColor;
        private Color _menuTextColor;

        // ---- The strip ----
        private MenuStrip _menuStrip;

        // ---- Events the form subscribes to ----
        public event EventHandler? OnExit;
        public event EventHandler? OnBackgroundColor;
        public event EventHandler? OnWindowColor;
        public event EventHandler? OnWindowTextColor;
        public event EventHandler? OnMenuBgColor;
        public event EventHandler? OnMenuTextColor;
        public event EventHandler? OnAbout;
        public event EventHandler? OnVersion;
        public event EventHandler? OnToggleGhostUI;
        public event EventHandler? OnToggleEditorUI;
        public event EventHandler? OnNewScene;
        public event EventHandler? OnOpenScene;
        public event EventHandler? OnSaveScene;
        public event EventHandler? OnSaveSceneAs;
        public event EventHandler? OnNewProject;
        public event EventHandler? OnOpenProject;
        public event EventHandler? OnOpenInVS;
        public event EventHandler? OnSetSolution;
        public event Action<string>? OnOpenRecent;
        public event EventHandler? OnPlay;
        public event EventHandler? OnPause;
        public event EventHandler? OnStop;
        public event EventHandler? OnPlayStandalone;
        public event EventHandler? OnGameModuleInfo;

        private ToolStripMenuItem _recentMenu = null!;

        public GhostMenuBuilder(Color menuBgColor, Color menuTextColor)
        {
            _menuBgColor = menuBgColor;
            _menuTextColor = menuTextColor;
        }

        // -------------------------------------------------------
        // Build and attach the MenuStrip to the form
        // -------------------------------------------------------
        public void Build(Form form)
        {
            _menuStrip = new MenuStrip
            {
                Dock = DockStyle.Top,
            };

            _menuStrip.Renderer = new GhostMenuRenderer(_menuBgColor, _menuTextColor);

            // ---- File ----
            var fileMenu = new ToolStripMenuItem("File");
            var newScene = MakeItem("New Scene", (s, e) => OnNewScene?.Invoke(s, e));
            newScene.ShortcutKeys = Keys.Control | Keys.N;
            var openScene = MakeItem("Open Scene...", (s, e) => OnOpenScene?.Invoke(s, e));
            openScene.ShortcutKeys = Keys.Control | Keys.O;
            var saveScene = MakeItem("Save Scene", (s, e) => OnSaveScene?.Invoke(s, e));
            saveScene.ShortcutKeys = Keys.Control | Keys.S;
            var saveSceneAs = MakeItem("Save Scene As...", (s, e) => OnSaveSceneAs?.Invoke(s, e));
            saveSceneAs.ShortcutKeys = Keys.Control | Keys.Shift | Keys.S;
            var newProject = MakeItem("New Project...", (s, e) => OnNewProject?.Invoke(s, e));
            newProject.ShortcutKeys = Keys.Control | Keys.Shift | Keys.N;
            var openProject = MakeItem("Open Project...", (s, e) => OnOpenProject?.Invoke(s, e));
            openProject.ShortcutKeys = Keys.Control | Keys.Shift | Keys.O;

            _recentMenu = new ToolStripMenuItem("Recent Projects");
            _recentMenu.DropDownItems.Add(new ToolStripMenuItem("(none)") { Enabled = false });
            _recentMenu.DropDownOpening += (s, e) => FillRecent();

            fileMenu.DropDownItems.Add(newProject);
            fileMenu.DropDownItems.Add(openProject);
            fileMenu.DropDownItems.Add(_recentMenu);
            fileMenu.DropDownItems.Add(new ToolStripSeparator());
            fileMenu.DropDownItems.Add(newScene);
            fileMenu.DropDownItems.Add(openScene);
            fileMenu.DropDownItems.Add(new ToolStripSeparator());
            fileMenu.DropDownItems.Add(saveScene);
            fileMenu.DropDownItems.Add(saveSceneAs);
            fileMenu.DropDownItems.Add(new ToolStripSeparator());
            fileMenu.DropDownItems.Add(MakeItem("Open in Visual Studio", (s, e) => OnOpenInVS?.Invoke(s, e)));
            fileMenu.DropDownItems.Add(new ToolStripSeparator());
            fileMenu.DropDownItems.Add(MakeItem("Exit", (s, e) => OnExit?.Invoke(s, e)));
            // ---- Settings ----
            var settingsMenu = new ToolStripMenuItem("Settings");
            settingsMenu.DropDownItems.Add(MakeItem("Background Color...", (s, e) => OnBackgroundColor?.Invoke(s, e)));
            settingsMenu.DropDownItems.Add(MakeItem("Window Color...", (s, e) => OnWindowColor?.Invoke(s, e)));
            settingsMenu.DropDownItems.Add(MakeItem("Window Text Color...", (s, e) => OnWindowTextColor?.Invoke(s, e)));
            settingsMenu.DropDownItems.Add(MakeItem("Menu Background Color...", (s, e) => OnMenuBgColor?.Invoke(s, e)));
            settingsMenu.DropDownItems.Add(MakeItem("Menu Text Color...", (s, e) => OnMenuTextColor?.Invoke(s, e)));

            // ---- In Build(), inside settingsMenu.DropDownItems ----
            settingsMenu.DropDownItems.Add(new ToolStripSeparator());

            settingsMenu.DropDownItems.Add(MakeItem("Toggle Ghost UI", (s, e) => OnToggleGhostUI?.Invoke(s, e)));
            settingsMenu.DropDownItems.Add(MakeItem("Set Visual Studio Solution...", (s, e) => OnSetSolution?.Invoke(s, e)));
            // ---- View ----
            var viewMenu = new ToolStripMenuItem("View");
            viewMenu.DropDownItems.Add(MakeItem("Toggle Engine UI  (4)", (s, e) => OnToggleEditorUI?.Invoke(s, e)));
            // ---- Play ----
            var playMenu = new ToolStripMenuItem("Play");
            var play = MakeItem("Play", (s, e) => OnPlay?.Invoke(s, e));
            play.ShortcutKeys = Keys.Control | Keys.P;
            var pause = MakeItem("Pause / Resume", (s, e) => OnPause?.Invoke(s, e));
            pause.ShortcutKeys = Keys.Control | Keys.Shift | Keys.P;
            var stop = MakeItem("Stop", (s, e) => OnStop?.Invoke(s, e));
            stop.ShortcutKeys = Keys.Shift | Keys.F5;
            playMenu.DropDownItems.Add(play);
            playMenu.DropDownItems.Add(pause);
            playMenu.DropDownItems.Add(stop);
            playMenu.DropDownItems.Add(new ToolStripSeparator());
            playMenu.DropDownItems.Add(MakeItem("Play Standalone (new window)", (s, e) => OnPlayStandalone?.Invoke(s, e)));
            playMenu.DropDownItems.Add(MakeItem("Game Module Info...", (s, e) => OnGameModuleInfo?.Invoke(s, e)));
            // ---- Help ----
            var helpMenu = new ToolStripMenuItem("Help");
            helpMenu.DropDownItems.Add(MakeItem("About", (s, e) => OnAbout?.Invoke(s, e)));
            helpMenu.DropDownItems.Add(MakeItem("Version", (s, e) => OnVersion?.Invoke(s, e)));

            _menuStrip.Items.Add(fileMenu);
            _menuStrip.Items.Add(settingsMenu);
            _menuStrip.Items.Add(viewMenu);
            _menuStrip.Items.Add(playMenu);
            _menuStrip.Items.Add(helpMenu);

            form.Controls.Add(_menuStrip);
            form.MainMenuStrip = _menuStrip;
        }

        // -------------------------------------------------------
        // Live color update (called when user picks new colors)
        // -------------------------------------------------------
        public void UpdateColors(Color menuBgColor, Color menuTextColor)
        {
            _menuBgColor = menuBgColor;
            _menuTextColor = menuTextColor;
            _menuStrip.Renderer = new GhostMenuRenderer(_menuBgColor, _menuTextColor);
            _menuStrip.Invalidate(true);
        }
        private void FillRecent()
        {
            _recentMenu.DropDownItems.Clear();
            var list = GhostSettings.Data.RecentProjects;
            if (list.Count == 0)
            {
                _recentMenu.DropDownItems.Add(new ToolStripMenuItem("(none)") { Enabled = false });
                return;
            }
            foreach (string p in list.ToArray())
            {
                string path = p;
                _recentMenu.DropDownItems.Add(
                    MakeItem($"{Path.GetFileNameWithoutExtension(path)}    -    {path}", (s, e) => OnOpenRecent?.Invoke(path)));
            }
        }
        // -------------------------------------------------------
        // Helper
        // -------------------------------------------------------
        private static ToolStripMenuItem MakeItem(string text, EventHandler handler)
        {
            var item = new ToolStripMenuItem(text);
            item.Click += handler;
            return item;
        }
    }

    // -------------------------------------------------------
    // Custom renderer — replaces all the WM_DRAWITEM logic
    // -------------------------------------------------------
    internal class GhostMenuRenderer : ToolStripProfessionalRenderer
    {
        private readonly Color _bg;
        private readonly Color _fg;
        private readonly Color _highlight;

        public GhostMenuRenderer(Color bg, Color fg)
            : base(new GhostColorTable(bg, fg))
        {
            _bg = bg;
            _fg = fg;
            _highlight = Blend(bg, Color.White, 0.15f);
        }

        // ---- Menu bar item text ----
        protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
        {
            e.TextColor = _fg;
            base.OnRenderItemText(e);
        }

        // ---- Dropdown item background ----
        protected override void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs e)
        {
            var item = e.Item;
            var g = e.Graphics;
            var rect = new Rectangle(Point.Empty, item.Size);

            Color fill = item.Selected ? _highlight : _bg;
            using var brush = new SolidBrush(fill);
            g.FillRectangle(brush, rect);
        }

        // ---- Menu bar background ----
        protected override void OnRenderToolStripBackground(ToolStripRenderEventArgs e)
        {
            using var brush = new SolidBrush(_bg);
            e.Graphics.FillRectangle(brush, e.AffectedBounds);
        }

        // ---- Remove the default border around dropdowns ----
        protected override void OnRenderToolStripBorder(ToolStripRenderEventArgs e)
        {
            // intentionally empty — no border
        }

        // ---- Blend helper (mirrors old Win32.BlendColor) ----
        private static Color Blend(Color base_col, Color overlay, float t)
        {
            int r = (int)(base_col.R + (overlay.R - base_col.R) * t);
            int g = (int)(base_col.G + (overlay.G - base_col.G) * t);
            int b = (int)(base_col.B + (overlay.B - base_col.B) * t);
            return Color.FromArgb(
                Math.Clamp(r, 0, 255),
                Math.Clamp(g, 0, 255),
                Math.Clamp(b, 0, 255));
        }
    }

    // -------------------------------------------------------
    // Color table — feeds the professional renderer
    // -------------------------------------------------------
    internal class GhostColorTable : ProfessionalColorTable
    {
        private readonly Color _bg;
        private readonly Color _fg;

        public GhostColorTable(Color bg, Color fg)
        {
            _bg = bg;
            _fg = fg;
        }

        public override Color MenuItemSelected => Blend(_bg, Color.White, 0.15f);
        public override Color MenuItemBorder => _bg;
        public override Color MenuBorder => _bg;
        public override Color MenuStripGradientBegin => _bg;
        public override Color MenuStripGradientEnd => _bg;
        public override Color ToolStripDropDownBackground => _bg;
        public override Color ImageMarginGradientBegin => _bg;
        public override Color ImageMarginGradientMiddle => _bg;
        public override Color ImageMarginGradientEnd => _bg;
        public override Color MenuItemPressedGradientBegin => _bg;
        public override Color MenuItemPressedGradientEnd => _bg;
        public override Color MenuItemSelectedGradientBegin => Blend(_bg, Color.White, 0.15f);
        public override Color MenuItemSelectedGradientEnd => Blend(_bg, Color.White, 0.15f);

        private static Color Blend(Color base_col, Color overlay, float t)
        {
            int r = (int)(base_col.R + (overlay.R - base_col.R) * t);
            int g = (int)(base_col.G + (overlay.G - base_col.G) * t);
            int b = (int)(base_col.B + (overlay.B - base_col.B) * t);
            return Color.FromArgb(
                Math.Clamp(r, 0, 255),
                Math.Clamp(g, 0, 255),
                Math.Clamp(b, 0, 255));
        }
    }
}