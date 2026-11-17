// GhostAnimPreview3D.cs — floating 640 x 480 window: model preview, transport, blueprint test buttons
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class AnimPreview3DWindow : Form
    {
        private sealed class HostPanel : Panel
        {
            public HostPanel()
            {
                SetStyle(ControlStyles.Selectable | ControlStyles.Opaque | ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint, true);
                TabStop = true; BackColor = Color.Black;
            }
            protected override void OnPaintBackground(PaintEventArgs e) { }   // the engine draws here
        }

        private readonly GhostAnimEditor _ed;
        private readonly HostPanel _host = new();
        private readonly Button _play = new(), _restart = new(), _stepB = new(), _stepF = new(), _useSel = new(), _resetCam = new();
        private readonly CheckBox _loop = new();
        private readonly NumericUpDown _speed = new();
        private readonly TrackBar _scrub = new();
        private readonly Label _time = new(), _info = new();
        private readonly FlowLayoutPanel _states = new();
        private readonly System.Windows.Forms.Timer _timer = new() { Interval = 33 };
        private AnimClipDoc? _clip;
        private int _vp, _model;
        private bool _playing = true, _syncing;
        private float _curTime, _curDur;
        private Point _lastMouse;

        public AnimPreview3DWindow(GhostAnimEditor ed)
        {
            _ed = ed;
            Text = "3D Animation Preview  (640 x 480)";
            FormBorderStyle = FormBorderStyle.FixedToolWindow;
            StartPosition = FormStartPosition.Manual;
            ShowInTaskbar = false;
            ClientSize = new Size(640, 480 + 150);
            BackColor = Color.FromArgb(0x25, 0x25, 0x28); ForeColor = Color.FromArgb(0xF2, 0xF2, 0xF2);

            _host.SetBounds(0, 0, 640, 480);
            Controls.Add(_host);

            int y = 486;
            _scrub.SetBounds(0, y, 520, 30); _scrub.Minimum = 0; _scrub.Maximum = 1000; _scrub.TickStyle = TickStyle.None;
            _time.SetBounds(526, y + 6, 110, 20);
            Controls.Add(_scrub); Controls.Add(_time);

            y += 34;
            Place(_play, "Pause", 6, y, 64); Place(_restart, "Restart", 74, y, 64);
            Place(_stepB, "< Frame", 142, y, 64); Place(_stepF, "Frame >", 210, y, 64);
            _loop.Text = "Loop"; _loop.SetBounds(284, y + 4, 52, 22);
            var lbl = new Label { Text = "Speed", Location = new Point(340, y + 6), AutoSize = true };
            _speed.SetBounds(384, y + 2, 64, 24); _speed.DecimalPlaces = 2; _speed.Increment = 0.05m; _speed.Minimum = 0.05m; _speed.Maximum = 8; _speed.Value = 1;
            _speed.BackColor = Color.FromArgb(0x1E, 0x1E, 0x1E); _speed.ForeColor = ForeColor;
            Place(_useSel, "Use Selected Model", 456, y, 124); Place(_resetCam, "Cam", 584, y, 50);
            Controls.AddRange(new Control[] { _loop, lbl, _speed });

            y += 32;
            _info.SetBounds(6, y, 628, 18); _info.ForeColor = Color.Gray;
            Controls.Add(_info);

            y += 22;
            var sl = new Label { Text = "Test states:", Location = new Point(6, y + 6), AutoSize = true, ForeColor = Color.FromArgb(0xFF, 0xC8, 0x6E) };
            _states.SetBounds(84, y, 550, 34); _states.WrapContents = true; _states.AutoScroll = true;
            Controls.Add(sl); Controls.Add(_states);

            // events
            _host.MouseEnter += (s, e) => _host.Focus();
            _host.MouseDown += (s, e) => _lastMouse = e.Location;
            _host.MouseMove += (s, e) =>
            {
                if (_vp != 0 && e.Button == MouseButtons.Left)
                    GhostEngine.GhostEngine_PreviewOrbit(_vp, (e.X - _lastMouse.X) * 0.4f, -(e.Y - _lastMouse.Y) * 0.3f, 1f);
                _lastMouse = e.Location;
            };
            _host.MouseWheel += (s, e) => { if (_vp != 0) GhostEngine.GhostEngine_PreviewOrbit(_vp, 0, 0, e.Delta > 0 ? 0.9f : 1.1f); };

            _play.Click += (s, e) => { _playing = !_playing; _play.Text = _playing ? "Pause" : "Play"; ApplyClip(); };
            _restart.Click += (s, e) => { _playing = true; _play.Text = "Pause"; ApplyClip(); GhostEngine.GhostEngine_AnimPreview_Seek(0f); };
            _stepB.Click += (s, e) => Step(-1);
            _stepF.Click += (s, e) => Step(1);
            _scrub.Scroll += (s, e) =>
            {
                if (_syncing || _curDur <= 0f) return;
                _playing = false; _play.Text = "Play"; ApplyClip();
                GhostEngine.GhostEngine_AnimPreview_Seek(_scrub.Value / 1000f * _curDur);
            };
            _loop.CheckedChanged += (s, e) =>
            {
                if (_syncing || _clip == null) return;
                _clip.Loop = _loop.Checked; _ed.ClipEditedExternally(); ApplyClip();
            };
            _speed.ValueChanged += (s, e) =>
            {
                if (_syncing || _clip == null) return;
                _clip.Speed = (float)_speed.Value; _ed.ClipEditedExternally(); ApplyClip();
            };
            _useSel.Click += (s, e) => UseSelectedModel(true);
            _resetCam.Click += (s, e) => { if (_vp != 0) GhostEngine.GhostEngine_PreviewResetCamera(_vp); };

            _timer.Tick += (s, e) => Poll();
            Shown += (s, e) =>
            {
                _vp = GhostEngine.GhostEngine_CreatePreviewViewport(_host.Handle, 640, 480);
                if (_vp == 0) _info.Text = "Could not create the preview viewport (surface limit?).";
                _timer.Start();
                UseSelectedModel(false);
            };
            FormClosed += (s, e) =>
            {
                _timer.Stop();
                GhostEngine.GhostEngine_AnimPreview_Stop();
                if (_vp != 0) GhostEngine.GhostEngine_DestroyViewport(_vp);
                _vp = 0;
            };
        }

        private static void Place(Button b, string text, int x, int y, int w)
        {
            b.Text = text; b.SetBounds(x, y, w, 26); b.FlatStyle = FlatStyle.Flat;
            b.BackColor = Color.FromArgb(0x1E, 0x1E, 0x1E); b.ForeColor = Color.FromArgb(0xF2, 0xF2, 0xF2);
            b.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
            b.Parent = null;
        }
        protected override void OnShown(EventArgs e)
        {
            foreach (var b in new[] { _play, _restart, _stepB, _stepF, _useSel, _resetCam }) if (b.Parent == null) Controls.Add(b);
            base.OnShown(e);
        }

        public void SetClip(AnimClipDoc? c)
        {
            _clip = c;
            _syncing = true;
            _loop.Checked = c?.Loop ?? true;
            _speed.Value = (decimal)Math.Clamp(c?.Speed ?? 1f, 0.05f, 8f);
            _syncing = false;
            ApplyClip();
        }

        public void SetStates(IEnumerable<string> names)
        {
            _states.Controls.Clear();
            foreach (string n in names.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                string name = n;
                var b = new Button { Text = name, AutoSize = true, FlatStyle = FlatStyle.Flat, BackColor = Color.FromArgb(0x1E, 0x1E, 0x1E), ForeColor = ForeColor, Margin = new Padding(0, 0, 4, 2) };
                b.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
                b.Click += (s, e) =>
                {
                    if (_model == 0 || !_ed.PrepareTest(_model)) return;
                    GhostEngine.GhostEngine_AnimPreview_Stop();                 // test the blueprint, not the single clip
                    GhostEngine.GhostEngine_AnimRequestState(_model, name);
                };
                _states.Controls.Add(b);
            }
        }

        public void ApplyClip()
        {
            if (_vp == 0) return;
            bool ok = _clip != null && _clip.Kind == 1 && _clip.Model.Length > 0 && _model != 0;
            if (!ok)
            {
                GhostEngine.GhostEngine_AnimPreview_Stop();
                _info.Text = _model == 0 ? "Select an animated model in the scene, then press Use Selected Model."
                                         : "Select a 3D clip that has an FBX to preview it.";
                return;
            }
            GhostEngine.GhostEngine_AnimPreview_Set(_model, _clip!.Model, _clip.Loop ? 1 : 0, _clip.Speed, _playing ? 1 : 0);
        }

        private void UseSelectedModel(bool warn)
        {
            int sel = GhostEngine.GhostEngine_GetSelected();
            var o = new GhostObjectInfo();
            if (sel != 0 && GhostEngine.GhostEngine_GetObject(sel, ref o) != 0 && GhostEngine.GhostEngine_IsMeshSkinned(o.meshId) != 0)
            {
                _model = sel;
                if (_vp != 0) { GhostEngine.GhostEngine_PreviewSetTarget(_vp, sel); GhostEngine.GhostEngine_PreviewResetCamera(_vp); }
                ApplyClip();
            }
            else if (warn)
                MessageBox.Show(this, "Select a model imported with \"Animated character\" ticked, in the scene view first.", "3D Preview",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
            else ApplyClip();
        }

        private void Step(int dir)
        {
            _playing = false; _play.Text = "Play"; ApplyClip();
            GhostEngine.GhostEngine_AnimPreview_Seek(_curTime + dir / 30f);
        }

        private void Poll()
        {
            if (_vp == 0) return;
            if (GhostEngine.GhostEngine_AnimPreview_Get(out float t, out float d, out int matched, out int total) == 0) return;
            _curTime = t; _curDur = d;
            _syncing = true;
            _scrub.Value = d > 0f ? Math.Clamp((int)(t / d * 1000f), 0, 1000) : 0;
            _time.Text = $"{t:0.00} / {d:0.00} s";
            _syncing = false;
            _info.Text = $"Tracks matched: {matched}/{total}" + (matched < total ? "   (bone names differ from the model's skeleton)" : "")
                         + (_clip != null ? $"   Plays in {(_clip.Speed > 0 ? d / _clip.Speed : 0):0.00}s at x{_clip.Speed:0.##}" : "");
        }
    }
}