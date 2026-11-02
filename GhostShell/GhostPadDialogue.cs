// GhostPadDialog.cs — Settings > Gamepad: deadzone, rumble, controller slot, live test view
using System;
using System.Drawing;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class PadView : Control
    {
        private static readonly string[] Names = { "A", "B", "X", "Y", "LB", "RB", "Bk", "St", "L3", "R3", "Up", "Dn", "Lt", "Rt" };
        public float[] Axes = new float[6];
        public int Buttons;
        public bool Connected;

        public PadView()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            BackColor = Color.FromArgb(0x1E, 0x1E, 0x1E);
        }

        private static void Stick(Graphics g, Pen ring, Brush dot, float cx, float cy, float x, float y)
        {
            const float r = 50f;
            g.DrawEllipse(ring, cx - r, cy - r, r * 2, r * 2);
            g.FillEllipse(dot, cx + x * r - 6, cy - y * r - 6, 12, 12);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(BackColor);
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            using var ring = new Pen(Color.FromArgb(0x55, 0x55, 0x5C), 2f);
            using var dot = new SolidBrush(Color.FromArgb(0xFF, 0xC8, 0x6E));
            using var dim = new SolidBrush(Color.FromArgb(0x77, 0x77, 0x7E));
            using var lit = new SolidBrush(Color.FromArgb(0xFF, 0xC8, 0x6E));
            using var f = new Font("Segoe UI", 9f);

            float[] a = Connected ? Axes : new float[6];
            Stick(g, ring, dot, 85, 70, a[0], a[1]);
            Stick(g, ring, dot, 307, 70, a[2], a[3]);
            g.DrawString("Left stick", f, dim, 52, 126);
            g.DrawString("Right stick", f, dim, 268, 126);

            for (int t = 0; t < 2; t++)                          // triggers: LT, RT
            {
                float x = 172 + t * 40, v = Math.Clamp(a[4 + t], 0f, 1f);
                g.DrawRectangle(ring, x, 20, 16, 90);
                g.FillRectangle(dot, x + 1, 20 + 90 - v * 90, 15, v * 90);
                g.DrawString(t == 0 ? "LT" : "RT", f, dim, x - 1, 112);
            }
            for (int i = 0; i < Names.Length; i++)
                g.DrawString(Names[i], f, (Buttons & (1 << i)) != 0 ? lit : dim, 8 + i * 27, 148);
        }
    }

    internal sealed class GhostPadDialog : Form
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28), Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E),
                                      Fg = Color.FromArgb(0xF2, 0xF2, 0xF2), Accent = Color.FromArgb(0xFF, 0xC8, 0x6E);

        private readonly PadView _view = new();
        private readonly Label _status = new(), _dzLabel = new();
        private readonly TrackBar _dz = new();
        private readonly CheckBox _rumble = new();
        private readonly ComboBox _slot = new();
        private readonly System.Windows.Forms.Timer _timer = new() { Interval = 40 };
        private readonly System.Windows.Forms.Timer _rumbleOff = new() { Interval = 450 };
        private bool _loading = true;

        // Called at startup so the saved settings reach the engine
        public static void ApplySaved()
        {
            GhostSettingsData d = GhostSettings.Data;
            GhostEngine.GhostEngine_SetPadSettings(d.PadDeadzone, d.PadRumble ? 1 : 0, d.PadSlot);
        }

        public static void Open(IWin32Window owner)
        {
            using var f = new GhostPadDialog();
            f.ShowDialog(owner);
        }

        private GhostPadDialog()
        {
            Text = "Gamepad";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false; MinimizeBox = false; ShowInTaskbar = false;
            ClientSize = new Size(420, 400);
            BackColor = Bg; ForeColor = Fg;

            _status.Location = new Point(14, 12); _status.AutoSize = true; _status.ForeColor = Accent;
            _view.Location = new Point(14, 40); _view.Size = new Size(392, 175);

            _dzLabel.Location = new Point(14, 228); _dzLabel.AutoSize = true;
            _dz.Location = new Point(10, 248); _dz.Width = 400; _dz.Minimum = 5; _dz.Maximum = 50; _dz.TickFrequency = 5;

            _rumble.Text = "Rumble"; _rumble.Location = new Point(14, 296); _rumble.AutoSize = true;

            var slotLabel = new Label { Text = "Controller", Location = new Point(14, 330), AutoSize = true };
            _slot.Location = new Point(100, 326); _slot.Width = 150; _slot.DropDownStyle = ComboBoxStyle.DropDownList;
            _slot.FlatStyle = FlatStyle.Flat; _slot.BackColor = Bg2; _slot.ForeColor = Fg;
            _slot.Items.AddRange(new object[] { "Auto", "Pad 1", "Pad 2", "Pad 3", "Pad 4" });

            var test = new Button { Text = "Test rumble", Location = new Point(270, 322), Size = new Size(136, 30), FlatStyle = FlatStyle.Flat, BackColor = Bg2, ForeColor = Fg };
            var close = new Button { Text = "Close", Location = new Point(306, 360), Size = new Size(100, 30), FlatStyle = FlatStyle.Flat, BackColor = Bg2, ForeColor = Fg, DialogResult = DialogResult.OK };
            AcceptButton = close;

            Controls.AddRange(new Control[] { _status, _view, _dzLabel, _dz, _rumble, slotLabel, _slot, test, close });

            GhostSettingsData d = GhostSettings.Data;
            _dz.Value = Math.Clamp((int)Math.Round(d.PadDeadzone * 100f), 5, 50);
            _rumble.Checked = d.PadRumble;
            _slot.SelectedIndex = Math.Clamp(d.PadSlot + 1, 0, 4);
            _loading = false;
            Apply();

            _dz.ValueChanged += (s, e) => Apply();
            _rumble.CheckedChanged += (s, e) => Apply();
            _slot.SelectedIndexChanged += (s, e) => Apply();
            test.Click += (s, e) => { GhostEngine.GhostEngine_PadTestRumble(1); _rumbleOff.Start(); };
            _rumbleOff.Tick += (s, e) => { _rumbleOff.Stop(); GhostEngine.GhostEngine_PadTestRumble(0); };
            _timer.Tick += (s, e) => Refresh_();
            FormClosed += (s, e) =>
            {
                _timer.Stop(); _rumbleOff.Stop();
                GhostEngine.GhostEngine_PadTestRumble(0);              // never leave the motors running
                _timer.Dispose(); _rumbleOff.Dispose();
            };
            _timer.Start();
        }

        private void Apply()
        {
            _dzLabel.Text = $"Stick deadzone: {_dz.Value}%";
            if (_loading) return;
            GhostSettingsData d = GhostSettings.Data;
            d.PadDeadzone = _dz.Value / 100f;
            d.PadRumble = _rumble.Checked;
            d.PadSlot = _slot.SelectedIndex - 1;                       // Auto = -1
            GhostSettings.Save();
            ApplySaved();
        }

        private void Refresh_()
        {
            if (GhostEngine.GhostEngine_GetPadInfo(out GhostPadInfo info) != 0)
            {
                _view.Connected = true;
                _view.Axes = info.axes ?? new float[6];
                _view.Buttons = info.buttons;
                _status.Text = $"Controller {info.slot + 1} connected";
            }
            else
            {
                _view.Connected = false; _view.Buttons = 0;
                _status.Text = "No controller found. Plug one in or press a button.";
            }
            _view.Invalidate();
        }
    }
}