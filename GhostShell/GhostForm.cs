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

            Controls.Add(_renderPanel);
            // ---- In BuildUI(), after Controls.Add(_renderPanel) ----
        
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
            _menuBuilder.OnToggleEditorUI += (s, e) => _editor.ToggleVisible();

            // Key 4 toggles the panels, Delete removes the selection (ignored while typing in the grid)
            KeyPreview = true;
            KeyDown += (s, e) =>
            {
                if (_editor.HandleKey(e.KeyCode)) { e.Handled = true; e.SuppressKeyPress = true; }
            };

            // The side panels change the viewport size, so re-sync the swap chain once layout is final
            Shown += (s, e) =>
            {
                _renderPanel.Select();
                if (_renderPanel.ClientSize.Width > 0 && _renderPanel.ClientSize.Height > 0)
                    GhostEngine.GhostEngine_Resize(
                        (uint)_renderPanel.ClientSize.Width,
                        (uint)_renderPanel.ClientSize.Height);
            };


        }

        // -------------------------------------------------------
        // Engine lifecycle hooks
        // -------------------------------------------------------
        private void HookEngineEvents()
        {
            // Init engine once the handle exists
            Load += (s, e) =>
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
                if (e.Button == MouseButtons.Left)
                    GhostEngine.GhostEngine_MouseDown(e.X, e.Y, 0);
            };
            _renderPanel.MouseMove += (s, e) => GhostEngine.GhostEngine_MouseMove(e.X, e.Y);
            _renderPanel.MouseUp += (s, e) =>
            {
                if (e.Button == MouseButtons.Left)
                    GhostEngine.GhostEngine_MouseUp(e.X, e.Y, 0);
            };
            _renderPanel.MouseLeave += (s, e) => GhostEngine.GhostEngine_MouseLeave();
            // Clean shutdown
            FormClosing += (s, e) =>
            {
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
                GhostEngine.GhostEngine_SetInputEnabled(IsViewportHot() ? 1 : 0);
                GhostEngine.GhostEngine_Tick();
            }
        }

        // Camera keys and right-mouse look only work while this window is active
        // and the cursor is over the viewport, so typing elsewhere never moves the camera.
        private bool IsViewportHot()
        {
            if (ActiveForm != this) return false;
            if (_ghostUI.Visible) return false;
            if (ActiveControl != _renderPanel) return false;     // typing in a panel never moves the camera
            return _renderPanel.ClientRectangle.Contains(_renderPanel.PointToClient(Cursor.Position));
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
                "GhostX Engine\nA lightweight Direct3D 12 engine\nNow with a WinForms shell.",
                "About GhostX", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private void OnVersion(object sender, EventArgs e)
        {
            MessageBox.Show(
                "GhostX v0.0.6\nWinForms shell migration complete.",
                "Version", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        // -------------------------------------------------------
        // Helpers
        // -------------------------------------------------------

        // DWM expects BGR (COLORREF) not RGB
        private static uint ToDwmColor(Color c)
            => (uint)(c.R | (c.G << 8) | (c.B << 16));
    }
}