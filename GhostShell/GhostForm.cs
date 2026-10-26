using System;
using System.Drawing;
using System.Reflection.Metadata;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using static System.Net.Mime.MediaTypeNames;
using Application = System.Windows.Forms.Application;

namespace GhostShell
{
    public class GhostForm : Form
    {
        // ---- Render surface ----
        private Panel _renderPanel;

        // ---- Theme state ----
        private Color _menuBgColor = Color.FromArgb(0x26, 0x26, 0x2A);
        private Color _menuTextColor = Color.FromArgb(0xF2, 0xF2, 0xF2);
        private Color _clearColor = Color.Black;

        // ---- Menu builder ----
        private GhostMenuBuilder _menuBuilder;
        private Microsoft.Web.WebView2.WinForms.WebView2 _ghostUI;
        private GhostEditorUI _editor = null!;
        private EditorKeyFilter _keyFilter = null!;
        private bool _engineReady;
        private GhostViewportBar? _viewBar;

        public GhostForm()
        {
            InitializeForm();
            ApplyDarkMode();
            BuildUI();
            HookEngineEvents();
        }

        // -------------------------------------------------------
        // Form setup
        // -------------------------------------------------------
        private void InitializeForm()
        {
            Text = "GhostX Shell";
            Size = new Size(1280, 720);
            MinimumSize = new Size(640, 480);
            BackColor = Color.FromArgb(0x1E, 0x1E, 0x1E);
            StartPosition = FormStartPosition.CenterScreen;
        }

        private void ApplyDarkMode()
        {
            uint dark = 1;
            Win32.DwmSetWindowAttribute(Handle, 20, ref dark, sizeof(uint));
        }

        // -------------------------------------------------------
        // UI construction
        // -------------------------------------------------------
        private void BuildUI()
        {
            // ---- Render panel (DX12 draws here) ----
            _renderPanel = new ViewportPanel      // was: new Panel
            {
                Dock = DockStyle.Fill,
                BackColor = Color.Black,
            };

            // Prevent WinForms from painting over DX12 output
            typeof(Panel).InvokeMember(
                "DoubleBuffered",
                System.Reflection.BindingFlags.SetProperty |
                System.Reflection.BindingFlags.Instance |
                System.Reflection.BindingFlags.NonPublic,
                null, _renderPanel, new object[] { false });

          
            // ---- Menu strip ----
            _menuBuilder = new GhostMenuBuilder(_menuBgColor, _menuTextColor);
            _menuBuilder.Build(this);

            // ---- Back in GhostForm.cs, wire it up in BuildUI()
            //      after the other _menuBuilder.On... lines ----

            _ghostUI = new Microsoft.Web.WebView2.WinForms.WebView2
            {
                Bounds = _renderPanel.ClientRectangle,
                Anchor = AnchorStyles.Top | AnchorStyles.Bottom |

         AnchorStyles.Left | AnchorStyles.Right,

                Visible = false,
            };
            _renderPanel.Controls.Add(_ghostUI);
            _ghostUI.BringToFront();
            // Player mode: no menu, no panels. The render panel fills the window.
            if (GhostPlayer.IsPlayer)
            {
                if (MainMenuStrip != null) MainMenuStrip.Visible = false;
                Controls.Add(_renderPanel);
                _renderPanel.BringToFront();

                // this may be incorrect above player layout return block?
                var hudTimer = new System.Windows.Forms.Timer { Interval = 200 };
                hudTimer.Tick += (s, e) =>
                {
                    if (!_engineReady) return;
                    if (GhostPlay.TakeQuit()) { Close(); return; }
                    string hud = GhostPlay.HudText();
                    string t = GhostProject.DisplayName + (hud.Length > 0 ? "  |  " + hud : "");
                    if (Text != t) Text = t;

                    string fault = GhostPlay.TakeFault();
                    if (fault.Length > 0)
                    {
                        MessageBox.Show(this, fault, "Game module crashed", MessageBoxButtons.OK, MessageBoxIcon.Error);
                        Close();
                    }
                };
                hudTimer.Start();
                return;
            }
            // Wire menu callbacks
            _menuBuilder.OnExit += (s, e) => Close();
            _menuBuilder.OnBackgroundColor += OnPickBackgroundColor;
            _menuBuilder.OnWindowColor += OnPickWindowColor;
            _menuBuilder.OnWindowTextColor += OnPickWindowTextColor;
            _menuBuilder.OnMenuBgColor += OnPickMenuBgColor;
            _menuBuilder.OnMenuTextColor += OnPickMenuTextColor;
            _menuBuilder.OnToggleGhostUI += async (s, e) =>
            {
                if (!_ghostUI.Visible)
                {
                    await _ghostUI.EnsureCoreWebView2Async(null);
                    _ghostUI.CoreWebView2.Settings.AreBrowserAcceleratorKeysEnabled = false;
                    await _ghostUI.CoreWebView2.CallDevToolsProtocolMethodAsync(
                        "Security.setIgnoreCertificateErrors",
                        "{\"ignore\":true}"
                    );
                    await _ghostUI.CoreWebView2.CallDevToolsProtocolMethodAsync(
    "Page.setBypassCSP", "{\"enabled\":true}"
);
                    await _ghostUI.CoreWebView2.CallDevToolsProtocolMethodAsync(
    "Network.setExtraHTTPHeaders",
    "{\"headers\":{\"Cross-Origin-Embedder-Policy\":\"unsafe-none\"}}"
);
                    _ghostUI.CoreWebView2.Navigate("http://localhost:5050");
                    // Wire up Blazor → DX12 color bridge
                    _ghostUI.CoreWebView2.WebMessageReceived += (sender, args) =>
                    {
                        var msg = args.TryGetWebMessageAsString();
                        if (msg.StartsWith("SetColor:"))
                        {
                            var parts = msg.Replace("SetColor:", "").Split(',');
                            float r = float.Parse(parts[0]);
                            float g = float.Parse(parts[1]);
                            float b = float.Parse(parts[2]);
                            GhostEngine.GhostEngine_SetClearColor(r, g, b);
                        }
                    };
                    _ghostUI.Visible = true;
                }
                else
                {
                    _ghostUI.Visible = false;
                }
            };

            _menuBuilder.OnAbout += OnAbout;
            _menuBuilder.OnVersion += OnVersion;

            // ---- Engine editor panels (must come after the menu strip exists) ----
            _editor = new GhostEditorUI(this, _renderPanel);
            _editor.Build();
            _viewBar = new GhostViewportBar(_renderPanel);       // the two dropdowns over the viewport
            _menuBuilder.OnToggleEditorUI += (s, e) => _editor.ToggleVisible();
            _menuBuilder.OnImportModel += (s, e) => GhostModels.ImportInteractive(this);
            _menuBuilder.OnReimportModel += (s, e) => GhostModels.ReimportSelected(this, false);
            _menuBuilder.OnReimportModelSettings += (s, e) => GhostModels.ReimportSelected(this, true);
            _menuBuilder.OnNewScene += (s, e) => { if (GhostSceneFile.ConfirmDiscard(this)) GhostSceneFile.New(); };
            _menuBuilder.OnOpenScene += (s, e) => GhostSceneFile.OpenInteractive(this);
            _menuBuilder.OnSaveScene += (s, e) => { if (!_editor.SaveDesignerIfFocused()) GhostSceneFile.SaveInteractive(this); };
            _menuBuilder.OnSaveSceneAs += (s, e) => GhostSceneFile.SaveAs(this);
            _menuBuilder.OnNewProject += (s, e) => GhostProjectFlow.NewProject(this);
            _menuBuilder.OnOpenProject += (s, e) => GhostProjectFlow.OpenProject(this);
            _menuBuilder.OnOpenRecent += p => GhostProjectFlow.OpenProjectPath(this, p);
            _menuBuilder.OnOpenInVS += (s, e) => GhostProjectFlow.OpenInVisualStudio(this);
            _menuBuilder.OnPackageProject += (s, e) => GhostPackager.Show(this);
            _menuBuilder.OnSetSolution += (s, e) => GhostProjectFlow.PickSolution(this);
            _menuBuilder.OnPlay += (s, e) => { GhostPlay.Play(); _renderPanel.Select(); };
            _menuBuilder.OnPause += (s, e) => GhostPlay.TogglePause();
            _menuBuilder.OnStop += (s, e) => GhostPlay.Stop();
            _menuBuilder.OnPlayStandalone += (s, e) => GhostPlay.LaunchStandalone(this);
            _menuBuilder.OnGameModuleInfo += (s, e) => GhostPlay.ShowModuleInfo(this);
            _menuBuilder.OnReloadAnimations += (s, e) => GhostEngine.GhostEngine_ReloadAnimations();
            _menuBuilder.OnCreateTestSprite += (s, e) => GhostTestSprite.Create(this);
            _menuBuilder.OnSaveProject += (s, e) => GhostProjectFlow.SaveProject(this);
            _menuBuilder.OnSaveProjectAs += (s, e) => GhostProjectFlow.SaveProjectAs(this);
            _menuBuilder.OnSetStartScene += (s, e) => GhostProjectFlow.SetStartScene(this);
            // Title bar: scene name, with * when there are unsaved changes

            var titleTimer = new System.Windows.Forms.Timer { Interval = 200 };
            titleTimer.Tick += (s, e) =>
            {
                if (_engineReady)
                {
                    if (GhostPlay.State != 0 && GhostPlay.TakeQuit()) GhostPlay.Stop();
                    string star = GhostSceneFile.IsDirty ? "*" : "";
                    string state = GhostPlay.State switch
                    {
                        1 => GhostPlay.HasModule ? "  [Playing]" : "  [Playing - no game module]",
                        2 => "  [Paused]",
                        _ => ""
                    };
                    string hud = GhostPlay.State != 0 ? GhostPlay.HudText() : "";
                    string t = (GhostProject.IsOpen
                        ? $"GhostX Shell - {GhostProject.DisplayName} - {GhostSceneFile.DisplayName}{star}"
                        : $"GhostX Shell - {GhostSceneFile.DisplayName}{star}")
                        + state + (hud.Length > 0 ? "  |  " + hud : "");
                    if (Text != t) Text = t;

                    string fault = GhostPlay.TakeFault();
                    if (fault.Length > 0)
                        MessageBox.Show(this, fault, "Game module crashed", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            };
            titleTimer.Start();


            // Restore the saved layout once the form handle exists
            Load += (s, e) => _editor.RestoreLayout();

            // Key 4 / Delete: a message filter, so it works from floating panels too
            _keyFilter = new EditorKeyFilter(_editor);
            Application.AddMessageFilter(_keyFilter);
            FormClosing += (s, e) =>
            {
                _editor.SaveLayout();
                Application.RemoveMessageFilter(_keyFilter);
            };


        }
        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (_engineReady && (!GhostSceneFile.ConfirmDiscard(this) ||
                      (_editor != null && !_editor.ConfirmDiscardDesigner(this)))) { e.Cancel = true; return; }
            base.OnFormClosing(e);
        }
        // -------------------------------------------------------
        // Engine lifecycle hooks
        // -------------------------------------------------------
        private void HookEngineEvents()
        {
            // If WinForms ever recreates the render panel's window (docking can), give the swap chain the new one
            _renderPanel.HandleCreated += (s, e) =>
            {
                if (!_engineReady) return;
                GhostEngine.GhostEngine_RebindViewport(0, _renderPanel.Handle,
                    (uint)Math.Max(_renderPanel.ClientSize.Width, 1),
                    (uint)Math.Max(_renderPanel.ClientSize.Height, 1));
            };
            // Init engine once the handle exists
            Shown += (s, e) =>
            {
                int result = GhostEngine.GhostEngine_Init(
                    _renderPanel.Handle,
                    (uint)_renderPanel.ClientSize.Width,
                    (uint)_renderPanel.ClientSize.Height);

                if (result != 0)
                {
                    MessageBox.Show("GhostEngine failed to initialize.",
                        "Fatal Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    Close();
                    return;
                }
                _engineReady = true;
                GhostFontAtlas.BakeDefault();     // the on-screen text font (the player needs it too)
                _viewBar?.ApplyStartup();                        // the overlay text needs the font, and the settings need the engine
                GhostSceneFile.MarkClean();       // the starting scene counts as saved
                if (GhostPlayer.IsPlayer && !GhostPlayer.Launch(this)) return;
                // Drive the render loop from the message pump idle time
                Application.Idle += OnIdle;
            };

            // Resize the swap chain when the render panel changes size
            _renderPanel.Resize += (s, e) =>
            {
                if (_renderPanel.ClientSize.Width > 0 &&
                    _renderPanel.ClientSize.Height > 0)
                {
                    GhostEngine.GhostEngine_Resize(
                        (uint)_renderPanel.ClientSize.Width,
                        (uint)_renderPanel.ClientSize.Height);
                    // ---- In HookEngineEvents(), inside the _renderPanel.Resize handler,
                    //      after the GhostEngine_Resize call ----
                    _ghostUI.Bounds = _renderPanel.ClientRectangle;
                }
            };
            // Forward left-button mouse events (client coords) for picking and dragging
            _renderPanel.MouseDown += (s, e) =>
            {
                if (e.Button != MouseButtons.Left) return;
                GhostEngine.GhostEngine_MouseDown(e.X, e.Y, 0);
                if (GhostEngine.GhostEngine_TakeDimClick(out int kind, out int index, out int objId, out float len) != 0)
                    BeginInvoke(new Action(() => GhostUnits.EditFromClick(_renderPanel, kind, index, objId, len)));   // after the click finishes
            };
            _renderPanel.MouseMove += (s, e) => GhostEngine.GhostEngine_MouseMove(e.X, e.Y);
            _renderPanel.MouseWheel += (s, e) => GhostEngine.GhostEngine_MouseWheel(e.Delta);
            _renderPanel.MouseUp += (s, e) =>
            {
                if (e.Button == MouseButtons.Left)
                    GhostEngine.GhostEngine_MouseUp(e.X, e.Y, 0);
            };
            _renderPanel.MouseLeave += (s, e) => GhostEngine.GhostEngine_MouseLeave();
            // Clean shutdown
            FormClosing += (s, e) =>
            {
                _renderPanel.Select();
                Application.Idle -= OnIdle;
                GhostEngine.GhostEngine_Shutdown();
            };
        }

        // -------------------------------------------------------
        // Render loop
        // -------------------------------------------------------
        private void OnIdle(object sender, EventArgs e)
        {
            // Drain all pending Windows messages first, then tick
            while (!Win32.PeekMessageExists())
            {
                int hot = _ghostUI.Visible ? -1
         : (_editor == null ? (_renderPanel.Focused ? 0 : -1) : _editor.HotViewport());
                GhostEngine.GhostEngine_SetInputViewport(hot);
                GhostEngine.GhostEngine_Tick();
            }
        }

        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            if (GhostPlayer.IsPlayer && keyData == Keys.Escape) { Close(); return true; }
            return base.ProcessCmdKey(ref msg, keyData);
        }

        // -------------------------------------------------------
        // Menu callbacks
        // -------------------------------------------------------
        private void OnPickBackgroundColor(object sender, EventArgs e)
        {
            GhostColorDialog.Pick(this, _clearColor, color =>
            {
                _clearColor = color;
                GhostEngine.GhostEngine_SetClearColor(
                    color.R / 255f,
                    color.G / 255f,
                    color.B / 255f);
            });
        }

        private void OnPickWindowColor(object sender, EventArgs e)
        {
            GhostColorDialog.Pick(this, BackColor, color =>
            {
                BackColor = color;
                uint raw = ToDwmColor(color);
                Win32.DwmSetWindowAttribute(Handle, 35, ref raw, sizeof(uint));
            });
        }

        private void OnPickWindowTextColor(object sender, EventArgs e)
        {
            // Start from white as a sensible default for title bar text
            GhostColorDialog.Pick(this, Color.White, color =>
            {
                uint raw = ToDwmColor(color);
                Win32.DwmSetWindowAttribute(Handle, 36, ref raw, sizeof(uint));
            });
        }

        private void OnPickMenuBgColor(object sender, EventArgs e)
        {
            GhostColorDialog.Pick(this, _menuBgColor, color =>
            {
                _menuBgColor = color;
                _menuBuilder.UpdateColors(_menuBgColor, _menuTextColor);
            });
        }

        private void OnPickMenuTextColor(object sender, EventArgs e)
        {
            GhostColorDialog.Pick(this, _menuTextColor, color =>
            {
                _menuTextColor = color;
                _menuBuilder.UpdateColors(_menuBgColor, _menuTextColor);
            });
        }

        private void OnAbout(object sender, EventArgs e)
        {
            MessageBox.Show(
                "GhostX Engine\nRobert Chilewski\nrobertchilewskir83@gmail.com",
                "About GhostX", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private void OnVersion(object sender, EventArgs e)
        {
            MessageBox.Show(
                "GhostX RHI Renderer Engine Version X.\nDirectX 12 Support.",
                "Version", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        // Selected object if there is one, otherwise the first cube in the scene (0 = none)
        private int TargetObjectId()
        {
            int sel = GhostEngine.GhostEngine_GetSelected();
            var o = new GhostObjectInfo();
            if (sel != 0 && GhostEngine.GhostEngine_GetObject(sel, ref o) != 0) return sel;

            var buf = new GhostEntityInfo[512];
            int n = GhostEngine.GhostEngine_GetEntities(buf, buf.Length);
            for (int i = 0; i < n; i++)
                if (buf[i].kind == 0 && buf[i].subtype == 0) return buf[i].id;
            return 0;
        }

        private void ApplyTexture(int textureId)
        {
            int id = TargetObjectId();
            if (id == 0)
            {
                MessageBox.Show("Drop a cube into the scene first.", "No object", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            GhostEngine.GhostEngine_SetObjectTexture(id, textureId);
        }

        private void OnImportTexture(object? sender, EventArgs e)
        {
            int target = TargetObjectId();
            if (target == 0)
            {
                MessageBox.Show("Drop a cube into the scene first.", "No object", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            using var dlg = new OpenFileDialog
            {
                Title = "Import Texture",
                Filter = "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp|All files (*.*)|*.*",
            };
            if (dlg.ShowDialog(this) != DialogResult.OK) return;

            try
            {
                byte[] px = GhostImageLoader.Load(dlg.FileName,4096, out int w, out int h, out int sw,out int sh);
                int tex = GhostEngine.GhostEngine_CreateTexture(Path.GetFileName(dlg.FileName), px, (uint)w, (uint)h);
                if (tex < 0)
                {
                    MessageBox.Show("The engine could not create the texture (too large, or the texture limit was reached).",
                        "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                GhostEngine.GhostEngine_SetObjectTexture(target, tex);
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }
        // -------------------------------------------------------
        // Helpers
        // -------------------------------------------------------

        // DWM expects BGR (COLORREF) not RGB
        private static uint ToDwmColor(Color c)
            => (uint)(c.R | (c.G << 8) | (c.B << 16));
    }
}