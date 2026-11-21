// GhostLoadingBar.cs - the "Loading project..." window: percentage bar, current asset, per-type counts, scrolling list
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows.Forms;

namespace GhostShell
{
    // The engine calls this once per asset it loads: kind = "Texture", "Model", "Scene", "Animation"
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void GhostLoadProgressFn(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string kind,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    internal sealed class GhostLoadingBar : Form
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28), Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E),
                                      Fg = Color.FromArgb(0xF2, 0xF2, 0xF2), Accent = Color.FromArgb(0xFF, 0xC8, 0x6E);

        private sealed class Bar : Control
        {
            public float Value;
            public Bar() { SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true); }
            protected override void OnPaint(PaintEventArgs e)
            {
                Graphics g = e.Graphics;
                g.Clear(Bg2);
                int w = (int)(Width * Math.Clamp(Value, 0f, 1f));
                using (var fill = new SolidBrush(Accent)) g.FillRectangle(fill, 0, 0, w, Height);
                using (var pen = new Pen(Color.FromArgb(0x3A, 0x3A, 0x40))) g.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
                string t = $"{(int)(Value * 100f)}%";
                using var f = new Font("Segoe UI", 9.5f, FontStyle.Bold);
                SizeF sz = g.MeasureString(t, f);
                float x = (Width - sz.Width) / 2f, y = (Height - sz.Height) / 2f;
                using (var sh = new SolidBrush(Color.FromArgb(160, 0, 0, 0))) g.DrawString(t, f, sh, x + 1, y + 1);
                g.DrawString(t, f, Brushes.White, x, y);
            }
        }

        private readonly Label _title = new(), _status = new(), _counts = new();
        private readonly ListBox _log = new();
        private readonly Bar _bar = new();
        private readonly Dictionary<string, int> _tally = new();
        private readonly int _planned;
        private int _done;
        private Control? _owner;

        private GhostLoadingBar(string title, int planned)
        {
            _planned = planned;
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.CenterParent;
            ShowInTaskbar = false;
            BackColor = Bg; ForeColor = Fg;
            ClientSize = new Size(520, 250);
            DoubleBuffered = true;

            _title.Text = title; _title.Location = new Point(16, 12); _title.AutoSize = true;
            _title.Font = new Font("Segoe UI", 11f, FontStyle.Bold); _title.ForeColor = Accent;

            _status.Location = new Point(16, 42); _status.Size = new Size(488, 20);
            _status.AutoEllipsis = true; _status.Text = "Starting...";

            _bar.Location = new Point(16, 68); _bar.Size = new Size(488, 24);

            _counts.Location = new Point(16, 98); _counts.Size = new Size(488, 18);
            _counts.ForeColor = Color.Gray; _counts.AutoEllipsis = true;

            _log.Location = new Point(16, 124); _log.Size = new Size(488, 112);
            _log.BackColor = Bg2; _log.ForeColor = Color.FromArgb(0xB8, 0xB8, 0xC0);
            _log.BorderStyle = BorderStyle.None; _log.IntegralHeight = false;
            _log.Font = new Font("Segoe UI", 9f); _log.TabStop = false;

            Controls.AddRange(new Control[] { _title, _status, _bar, _counts, _log });
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            using var pen = new Pen(Color.FromArgb(0x3A, 0x3A, 0x40));
            e.Graphics.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
        }

        protected override bool ShowWithoutActivation => false;

        public static GhostLoadingBar Open(IWin32Window? owner, string title, int planned)
        {
            var bar = new GhostLoadingBar(title, planned);
            bar.Show(owner);
            if (owner is Control c) { bar._owner = c; c.Enabled = false; }      // no clicks while loading
            bar.Refresh();
            return bar;
        }

        // One asset loaded
        public void Note(string kind, string name)
        {
            _done++;
            _tally[kind] = _tally.TryGetValue(kind, out int c) ? c + 1 : 1;
            _status.Text = $"Loading {kind.ToLowerInvariant()}: {name}";
            _log.Items.Add($"[{kind}]  {name}");
            if (_log.Items.Count > 300) _log.Items.RemoveAt(0);
            _log.TopIndex = Math.Max(_log.Items.Count - 7, 0);
            _counts.Text = string.Join("      ", _tally.Select(kv => $"{kv.Key}s {kv.Value}"));

            // planned can be a little off (animation textures, etc.), so never show 100% until Finish()
            float pct = _planned > 0 ? _done / (float)Math.Max(_planned, _done + 1) : _done / (_done + 10f);
            _bar.Value = Math.Min(pct, 0.99f);
            Refresh();
        }

        // A status line that isn't an asset (no count)
        public void Stage(string text) { _status.Text = text; Refresh(); }

        public void Finish()
        {
            _bar.Value = 1f;
            _status.Text = _done == 1 ? "Done: 1 asset loaded" : $"Done: {_done} assets loaded";
            Refresh();
            if (_owner != null) { _owner.Enabled = true; _owner = null; }
            Close();
        }
    }

    internal static class GhostLoad
    {
        private static GhostLoadingBar? _bar;
        private static GhostLoadProgressFn? _cb;          // static, so the GC can't free it while native code holds it

        // Shows the bar, runs `work` (the real loading code), then closes it. Nested calls share the outer bar.
        public static void Run(IWin32Window? owner, string title, int planned, Action work)
        {
            if (_bar != null) { work(); return; }
            GhostLoadingBar bar = GhostLoadingBar.Open(owner, title, planned);
            _bar = bar;
            _cb = (kind, name) => { try { bar.Note(kind, name); } catch { } };    // never let an exception cross into native code
            GhostEngine.GhostEngine_SetLoadProgress(_cb);
            try { work(); }
            finally
            {
                GhostEngine.GhostEngine_SetLoadProgress(null);
                _cb = null; _bar = null;
                bar.Finish();
            }
        }

        // Call at the end of the work: one engine tick loads animations, builds terrain meshes, etc. inside the bar
        public static void Prime()
        {
            _bar?.Stage("Preparing animations, terrain and shadows...");
            GhostEngine.GhostEngine_Tick();
        }

        public static void Stage(string text) => _bar?.Stage(text);

        // How many assets opening this scene will touch: itself, its textures, its model files, its animations
        public static int CountScene(string scenePath)
        {
            try
            {
                using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(scenePath));
                JsonElement r = doc.RootElement;
                int n = 1;
                if (r.TryGetProperty("textures", out JsonElement t) && t.ValueKind == JsonValueKind.Array) n += t.GetArrayLength();
                var meshes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                var anims = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                if (r.TryGetProperty("objects", out JsonElement objs) && objs.ValueKind == JsonValueKind.Array)
                    foreach (JsonElement o in objs.EnumerateArray())
                    {
                        if (o.TryGetProperty("meshFile", out JsonElement mf) && mf.ValueKind == JsonValueKind.String) meshes.Add(mf.GetString()!);
                        foreach (string key in new[] { "sprite", "animator" })
                            if (o.TryGetProperty(key, out JsonElement s) && s.ValueKind == JsonValueKind.Object &&
                                s.TryGetProperty("anim", out JsonElement a) && a.ValueKind == JsonValueKind.String && a.GetString()!.Length > 0)
                                anims.Add(a.GetString()!);
                    }
                return n + meshes.Count + anims.Count;
            }
            catch { return 0; }
        }

        // Opening a project: every .ghostmesh in Content/Models (the palette scan) plus the first scene
        public static int CountProject(string? scenePath)
        {
            int n = 0;
            try
            {
                string? content = GhostProject.ContentDir;
                if (content != null && Directory.Exists(Path.Combine(content, "Models")))
                    n += Directory.GetFiles(Path.Combine(content, "Models"), "*.ghostmesh", SearchOption.AllDirectories).Length;
            }
            catch { }
            if (!string.IsNullOrEmpty(scenePath) && File.Exists(scenePath)) n += CountScene(scenePath);
            return n;
        }
    }
}