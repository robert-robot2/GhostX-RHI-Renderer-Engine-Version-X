// GhostAudioMixer.cs — dockable Audio Mixer: bus sliders + mutes, and the per-sound table (enabled, bus, volume, file, test)
using System;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace GhostShell
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct GhostSoundInfo
    {
        public int bus, kind, enabled;                 // bus: 1 music, 2 sfx, 3 ambience. kind: 0 sfx, 1 music, 2 ambience
        public float volume, pitchVar;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 48)] public byte[] name;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 260)] public byte[] file;
    }

    internal sealed class GhostAudioMixer : UserControl
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
        private static readonly Color Line = Color.FromArgb(0x3A, 0x3A, 0x40);

        private static readonly string[] BusNames = { "Master", "Music", "SFX", "Ambience" };
        private static readonly string[] BusChoices = { "Music", "SFX", "Ambience" };    // bus ids 1..3
        private static readonly string[] KindNames = { "SFX", "Music", "Ambience" };
        private const int ColEnabled = 0, ColName = 1, ColKind = 2, ColBus = 3, ColVol = 4, ColPitch = 5, ColFile = 6, ColTest = 7, ColBrowse = 8;

        private readonly TrackBar[] _vol = new TrackBar[4];
        private readonly Label[] _pct = new Label[4];
        private readonly CheckBox[] _mute = new CheckBox[4];
        private readonly DataGridView _grid;
        private readonly Label _status;
        private bool _loading;

        public GhostAudioMixer()
        {
            BackColor = Bg;
            ForeColor = Fg;

            // ---- buses ----
            var buses = new TableLayoutPanel { Dock = DockStyle.Top, Height = 124, ColumnCount = 4, RowCount = 4, BackColor = Bg, Padding = new Padding(8, 6, 8, 2) };
            buses.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 80));
            buses.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            buses.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 48));
            buses.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 70));
            for (int b = 0; b < 4; b++)
            {
                int bus = b;
                buses.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
                var name = new Label { Text = BusNames[b], ForeColor = Fg, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft };
                _vol[b] = new TrackBar { Minimum = 0, Maximum = 100, TickStyle = TickStyle.None, Dock = DockStyle.Fill, Value = 100, BackColor = Bg };
                _pct[b] = new Label { Text = "100%", ForeColor = Fg, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleRight };
                _mute[b] = new CheckBox { Text = "Mute", ForeColor = Fg, Dock = DockStyle.Fill };
                _vol[b].Scroll += (s, e) => { GhostEngine.GhostEngine_SetBusVolume(bus, _vol[bus].Value / 100f); _pct[bus].Text = _vol[bus].Value + "%"; Persist(); };
                _mute[b].CheckedChanged += (s, e) => { GhostEngine.GhostEngine_SetBusMute(bus, _mute[bus].Checked ? 1 : 0); Persist(); };
                buses.Controls.Add(name, 0, b);
                buses.Controls.Add(_vol[b], 1, b);
                buses.Controls.Add(_pct[b], 2, b);
                buses.Controls.Add(_mute[b], 3, b);
            }

            // ---- buttons + status ----
            var bar = new Panel { Dock = DockStyle.Top, Height = 32, BackColor = Bg };
            Button Btn(string text, int x, EventHandler click)
            {
                var b = new Button { Text = text, Location = new Point(x, 3), Size = new Size(100, 26), FlatStyle = FlatStyle.Flat, ForeColor = Fg, BackColor = Bg2 };
                b.FlatAppearance.BorderColor = Line;
                b.TabStop = false;
                b.Click += click;
                bar.Controls.Add(b);
                return b;
            }
            Btn("Stop All", 8, (s, e) => GhostEngine.GhostEngine_StopAudio());
            Btn("Save Bank", 114, (s, e) => SaveBank());
            Btn("Reload", 220, (s, e) => Rebuild());
            _status = new Label { Location = new Point(332, 6), AutoSize = true, ForeColor = Color.Gray, Text = "" };
            bar.Controls.Add(_status);

            // ---- sounds ----
            _grid = new DataGridView
            {
                Dock = DockStyle.Fill,
                BackgroundColor = Bg2,
                GridColor = Line,
                BorderStyle = BorderStyle.None,
                AllowUserToAddRows = false,
                AllowUserToDeleteRows = false,
                AllowUserToResizeRows = false,
                RowHeadersVisible = false,
                EnableHeadersVisualStyles = false,
                SelectionMode = DataGridViewSelectionMode.FullRowSelect,
                AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.None,
            };
            _grid.ColumnHeadersDefaultCellStyle.BackColor = Bg;
            _grid.ColumnHeadersDefaultCellStyle.ForeColor = Fg;
            _grid.DefaultCellStyle.BackColor = Bg2;
            _grid.DefaultCellStyle.ForeColor = Fg;
            _grid.DefaultCellStyle.SelectionBackColor = Color.FromArgb(0x5A, 0x42, 0x1E);
            _grid.DefaultCellStyle.SelectionForeColor = Fg;

            _grid.Columns.Add(new DataGridViewCheckBoxColumn { HeaderText = "On", Width = 36 });
            _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Sound", Width = 130, ReadOnly = true });
            _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Type", Width = 70, ReadOnly = true });
            var busCol = new DataGridViewComboBoxColumn { HeaderText = "Bus", Width = 84, FlatStyle = FlatStyle.Flat };
            busCol.Items.AddRange(BusChoices);
            _grid.Columns.Add(busCol);
            _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Vol", Width = 48 });
            _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Pitch +-", Width = 62 });
            _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "File", Width = 240, ReadOnly = true });
            _grid.Columns.Add(new DataGridViewButtonColumn { HeaderText = "", Text = "Test", UseColumnTextForButtonValue = true, Width = 52, FlatStyle = FlatStyle.Flat });
            _grid.Columns.Add(new DataGridViewButtonColumn { HeaderText = "", Text = "File...", UseColumnTextForButtonValue = true, Width = 60, FlatStyle = FlatStyle.Flat });
            foreach (DataGridViewColumn c in _grid.Columns) c.SortMode = DataGridViewColumnSortMode.NotSortable;

            _grid.CurrentCellDirtyStateChanged += (s, e) => { if (_grid.IsCurrentCellDirty) _grid.CommitEdit(DataGridViewDataErrorContexts.Commit); };
            _grid.CellValueChanged += OnCellChanged;
            _grid.CellContentClick += OnCellClick;
            _grid.DataError += (s, e) => e.ThrowException = false;

            Controls.Add(_grid);       // Fill first, then the Top panels (last added = topmost)
            Controls.Add(bar);
            Controls.Add(buses);

            GhostProject.Changed += OnProjectChanged;
            VisibleChanged += (s, e) => { if (Visible) Rebuild(); };
            Disposed += (s, e) => GhostProject.Changed -= OnProjectChanged;
        }

        private void OnProjectChanged() { if (IsHandleCreated) BeginInvoke(new Action(Rebuild)); }

        // ---------------- engine <-> text ----------------
        private static string Str(byte[]? b)
        {
            if (b == null) return "";
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        private static byte[] Pack(string s, int size)
        {
            var b = new byte[size];
            byte[] u = Encoding.UTF8.GetBytes(s ?? "");
            Array.Copy(u, b, Math.Min(u.Length, size - 1));
            return b;
        }

        private static float ParseF(object? v, float fallback, float lo, float hi)
        {
            if (v != null && float.TryParse(v.ToString(), NumberStyles.Float, CultureInfo.InvariantCulture, out float f))
                return Math.Clamp(f, lo, hi);
            return fallback;
        }

        private static string F(float v) => v.ToString("0.##", CultureInfo.InvariantCulture);

        // ---------------- rebuild ----------------
        private void Rebuild()
        {
            _loading = true;
            try
            {
                for (int b = 0; b < 4; b++)
                {
                    _vol[b].Value = Math.Clamp((int)Math.Round(GhostEngine.GhostEngine_GetBusVolume(b) * 100f), 0, 100);
                    _pct[b].Text = _vol[b].Value + "%";
                    _mute[b].Checked = GhostEngine.GhostEngine_GetBusMute(b) != 0;
                }

                _grid.Rows.Clear();
                int n = GhostEngine.GhostEngine_GetSoundCount();
                for (int i = 0; i < n; i++)
                {
                    if (GhostEngine.GhostEngine_GetSoundInfo(i, out GhostSoundInfo s) == 0) continue;
                    int r = _grid.Rows.Add();
                    var c = _grid.Rows[r].Cells;
                    c[ColEnabled].Value = s.enabled != 0;
                    c[ColName].Value = Str(s.name);
                    c[ColKind].Value = KindNames[Math.Clamp(s.kind, 0, 2)];
                    c[ColBus].Value = BusChoices[Math.Clamp(s.bus, 1, 3) - 1];
                    c[ColVol].Value = F(s.volume);
                    c[ColPitch].Value = F(s.pitchVar);
                    c[ColFile].Value = Str(s.file);
                }
                _status.Text = GhostProject.IsOpen ? "Sounds are saved to Content/Audio/Sounds.ghostaudio" : "No project open: changes are not saved";
            }
            finally { _loading = false; }
        }

        // ---------------- edits ----------------
        private void OnCellChanged(object? sender, DataGridViewCellEventArgs e)
        {
            if (_loading || e.RowIndex < 0) return;
            int i = e.RowIndex;
            if (GhostEngine.GhostEngine_GetSoundInfo(i, out GhostSoundInfo s) == 0) return;
            var c = _grid.Rows[i].Cells;

            s.enabled = (c[ColEnabled].Value is bool on && on) ? 1 : 0;
            int bi = Array.IndexOf(BusChoices, c[ColBus].Value as string);
            s.bus = bi < 0 ? s.bus : bi + 1;
            s.volume = ParseF(c[ColVol].Value, s.volume, 0f, 2f);
            s.pitchVar = ParseF(c[ColPitch].Value, s.pitchVar, 0f, 0.5f);
            Apply(i, ref s);

            _loading = true;                                        // show the clamped values
            c[ColVol].Value = F(s.volume);
            c[ColPitch].Value = F(s.pitchVar);
            _loading = false;
        }

        private void Apply(int i, ref GhostSoundInfo s)
        {
            GhostEngine.GhostEngine_SetSoundInfo(i, ref s);
            SaveBank();
        }

        private void SaveBank()
        {
            if (!GhostProject.IsOpen) return;
            if (GhostEngine.GhostEngine_SaveSoundBank() == 0) _status.Text = "Could not save the sound bank.";
        }

        private void OnCellClick(object? sender, DataGridViewCellEventArgs e)
        {
            if (e.RowIndex < 0) return;
            int i = e.RowIndex;

            if (e.ColumnIndex == ColTest)
            {
                _status.Text = GhostEngine.GhostEngine_TestSound(i) != 0
                    ? "Playing " + _grid.Rows[i].Cells[ColName].Value
                    : "Could not play it: file missing or not a WAV / MP3 / FLAC (" + _grid.Rows[i].Cells[ColFile].Value + ")";
            }
            else if (e.ColumnIndex == ColBrowse)
            {
                using var dlg = new OpenFileDialog
                {
                    Title = "Choose a sound for " + _grid.Rows[i].Cells[ColName].Value,
                    Filter = "Audio (*.wav;*.mp3;*.flac)|*.wav;*.mp3;*.flac|All files (*.*)|*.*",
                };
                if (dlg.ShowDialog(FindForm()) != DialogResult.OK) return;
                try
                {
                    string? dir = GhostProject.ContentDir == null ? null : Path.Combine(GhostProject.ContentDir, "Audio");
                    if (dir != null) Directory.CreateDirectory(dir);
                    string local = GhostProject.EnsureInContent(dlg.FileName, dir);       // copied into Content/Audio
                    string stored = GhostProject.RelativeToRoot(local) ?? local;

                    if (GhostEngine.GhostEngine_GetSoundInfo(i, out GhostSoundInfo s) == 0) return;
                    s.file = Pack(stored, 260);
                    Apply(i, ref s);
                    _loading = true;
                    _grid.Rows[i].Cells[ColFile].Value = stored;
                    _loading = false;
                }
                catch (Exception ex)
                {
                    MessageBox.Show(ex.Message, "Could not use that file", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
            }
        }

        // ---------------- saved with the user's settings ----------------
        private void Persist()
        {
            if (_loading) return;
            CultureInfo I = CultureInfo.InvariantCulture;
            var p = new string[8];
            for (int b = 0; b < 4; b++)
            {
                p[b] = GhostEngine.GhostEngine_GetBusVolume(b).ToString("R", I);
                p[4 + b] = GhostEngine.GhostEngine_GetBusMute(b).ToString(I);
            }
            GhostSettings.Data.AudioMixer = string.Join(",", p);
            GhostSettings.Save();
        }

        // Call once after the engine is up (the standalone player needs it too)
        public static void ApplySaved()
        {
            string[] p = (GhostSettings.Data.AudioMixer ?? "").Split(',');
            if (p.Length != 8) return;
            CultureInfo I = CultureInfo.InvariantCulture;
            for (int b = 0; b < 4; b++)
            {
                if (float.TryParse(p[b], NumberStyles.Float, I, out float v)) GhostEngine.GhostEngine_SetBusVolume(b, v);
                if (int.TryParse(p[4 + b], NumberStyles.Integer, I, out int m)) GhostEngine.GhostEngine_SetBusMute(b, m);
            }
        }
    }
}