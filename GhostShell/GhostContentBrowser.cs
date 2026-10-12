// GhostContentBrowser.cs — bottom panel: texture thumbnails, Explorer drop, drag-onto-object
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace GhostShell
{
    internal class GhostContentBrowser : Panel
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
        private static readonly Color SelColor = Color.FromArgb(0x5A, 0x42, 0x1E);

        private readonly FlowLayoutPanel _flow;
        private readonly ToolTip _tip = new();
        private readonly Dictionary<string, Panel> _tiles = new();
        private string _selectedKey = "";
        private string _pressKey = "";

        private string? _folder;                  // current folder in project mode (null = no project)
        private Label _hint = null!;
        private Button _up = null!;
        private FileSystemWatcher? _watch;
        private bool _rebuildQueued, _inDrag, _pending;

        private static readonly Dictionary<string, Bitmap> ThumbCache = new(StringComparer.OrdinalIgnoreCase);
        private static Bitmap? _folderIcon, _sceneIcon, _blankIcon;

        // Double-click on a scene tile
        public event Action<string>? OpenSceneRequested;
        private Point _pressPoint;

        // Double-click on a tile: apply that texture to the selected object
        public event Action<int>? ApplyRequested;

        public GhostContentBrowser()
        {
            BackColor = Bg;

            var bar = new Panel { Dock = DockStyle.Top, Height = 30, BackColor = Bg };
            var _hint = new Label
            {
                Dock = DockStyle.Fill,
                ForeColor = Color.Gray,
                TextAlign = ContentAlignment.MiddleLeft,
                AutoEllipsis = true,
                Padding = new Padding(8, 0, 0, 0),
                Text = "Drop image files here (PNG, JPG, BMP)  -  drag a tile onto an object to texture it (Shift = one face)  -  double-click applies to the selection",
            };
            var import = new Button
            {
                Dock = DockStyle.Left,
                Width = 100,
                Text = "Import...",
                FlatStyle = FlatStyle.Flat,
                ForeColor = Fg,
                BackColor = Bg2,
            };
            import.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
            import.Click += (s, e) => ImportDialog();
            bar.Controls.Add(_hint);       // Fill first, then Left
            bar.Controls.Add(import);
            _up = new Button
            {
                Dock = DockStyle.Left,
                Width = 56,
                Text = "Up",
                FlatStyle = FlatStyle.Flat,
                ForeColor = Fg,
                BackColor = Bg2,
                Visible = false,
            };
            _up.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
            _up.Click += (s, e) => GoUp();
            bar.Controls.Add(_up);
            _flow = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                AutoScroll = true,
                BackColor = Bg2,
                Padding = new Padding(6),
            };

            Controls.Add(_flow);          // Fill first, then Top
            Controls.Add(bar);

            foreach (Control c in new Control[] { this, bar, _hint, import, _flow })
                WireFileDrop(c);

            GhostAssets.Changed += QueueRebuild;
            GhostProject.Changed += OnProjectChanged;
            Disposed += (s, e) =>
            {
                GhostAssets.Changed -= QueueRebuild;
                GhostProject.Changed -= OnProjectChanged;
                _watch?.Dispose();
            };
            OnProjectChanged();
        }

        // -------------------------------------------------------
        //  Import
        // -------------------------------------------------------
        private void ImportDialog()
        {
            using var dlg = new OpenFileDialog
            {
                Title = "Import Textures",
                Filter = "Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp|All files (*.*)|*.*",
                Multiselect = true,
            };
            if (dlg.ShowDialog(FindForm()) == DialogResult.OK) ImportFiles(dlg.FileNames);
        }

        public void ImportFiles(IEnumerable<string> files)
        {
            var failed = new List<string>();
            foreach (string f in files)
            {
                if (!GhostAssets.IsImage(f)) continue;
                try
                {
                    string local = GhostProject.EnsureInContent(f, _folder);   // copied into Content/ when a project is open
                    if (GhostAssets.ImportOrGet(local) < 0)
                        failed.Add($"{Path.GetFileName(f)}: the engine refused it (texture limit reached?)");
                }
                catch (Exception ex) { failed.Add($"{Path.GetFileName(f)}: {ex.Message}"); }
            }
            if (failed.Count > 0)
                MessageBox.Show(string.Join("\n", failed), "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        // -------------------------------------------------------
        //  Explorer drop (files)
        // -------------------------------------------------------
        private void WireFileDrop(Control c)
        {
            c.AllowDrop = true;
            c.DragEnter += OnFileDrag;
            c.DragOver += OnFileDrag;
            c.DragDrop += OnFileDrop;
        }

        private static bool HasImageFiles(DragEventArgs e)
            => e.Data?.GetData(DataFormats.FileDrop) is string[] f && f.Any(GhostAssets.IsImage);

        private void OnFileDrag(object? sender, DragEventArgs e)
            => e.Effect = HasImageFiles(e) ? DragDropEffects.Copy : DragDropEffects.None;

        private void OnFileDrop(object? sender, DragEventArgs e)
        {
            if (e.Data?.GetData(DataFormats.FileDrop) is string[] files) ImportFiles(files);
        }

        // -------------------------------------------------------
        //  Tiles
        // -------------------------------------------------------


        // -------------------------------------------------------
        //  Tiles
        // -------------------------------------------------------
        private sealed class Entry
        {
            public string Key = "", Label = "", Tip = "";
            public Bitmap Thumb = null!;
            public int TexId = -1;          // built-in or already-loaded texture
            public string? Path;            // file or folder on disk
            public bool IsFolder, IsScene;
        }

        // Rebuilding while a tile is being clicked or dragged would dispose that tile, so it's deferred
        private void QueueRebuild()
        {
            if (!IsHandleCreated) { Rebuild(); return; }
            if (_rebuildQueued) return;
            _rebuildQueued = true;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    _rebuildQueued = false;
                    if (_inDrag) { _pending = true; return; }
                    Rebuild();
                }));
            }
            catch { _rebuildQueued = false; }
        }

        private void OnProjectChanged()
        {
            _watch?.Dispose();
            _watch = null;
            _folder = GhostProject.ContentDir;

            if (_folder != null && Directory.Exists(_folder))
            {
                _watch = new FileSystemWatcher(_folder)
                {
                    IncludeSubdirectories = true,
                    NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName,
                    EnableRaisingEvents = true,
                };
                FileSystemEventHandler h = (s, e) => QueueRebuild();
                _watch.Created += h;
                _watch.Deleted += h;
                _watch.Renamed += (s, e) => QueueRebuild();
            }
            Rebuild();
        }

        private bool AtRoot()
        {
            string? root = GhostProject.ContentDir;
            return _folder == null || root == null || string.Equals(
                Path.TrimEndingDirectorySeparator(_folder), Path.TrimEndingDirectorySeparator(root),
                StringComparison.OrdinalIgnoreCase);
        }

        private void GoUp()
        {
            if (_folder == null) return;
            string? parent = Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(_folder));
            string? root = GhostProject.ContentDir;
            _folder = (parent != null && root != null &&
                       parent.Length >= Path.TrimEndingDirectorySeparator(root).Length) ? parent : root;
            Rebuild();
        }

        private static Entry FromAsset(TextureAsset a) => new Entry
        {
            Key = "t:" + a.Id,
            Label = a.Name,
            Tip = a.Path ?? $"{a.Name} (built-in)",
            Thumb = a.Thumb,
            TexId = a.Id,
        };

        private void Rebuild()
        {
            string? root = GhostProject.ContentDir;
            if (_folder != null && !Directory.Exists(_folder)) _folder = root;

            var entries = new List<Entry>();
            bool atRoot = AtRoot();

            if (_folder == null)
            {
                foreach (TextureAsset a in GhostAssets.All) entries.Add(FromAsset(a));       // no project: every loaded texture
            }
            else
            {
                if (atRoot)
                    foreach (TextureAsset a in GhostAssets.All.Where(x => x.Id <= 2)) entries.Add(FromAsset(a));   // built-ins

                try
                {
                    foreach (string d in Directory.GetDirectories(_folder).OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
                        entries.Add(new Entry
                        {
                            Key = "d:" + d,
                            Label = Path.GetFileName(d),
                            Tip = d,
                            Thumb = FolderIcon(),
                            Path = d,
                            IsFolder = true
                        });

                    foreach (string f in Directory.GetFiles(_folder).OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
                    {
                        if (GhostAssets.IsImage(f))
                            entries.Add(new Entry
                            {
                                Key = "f:" + f,
                                Label = Path.GetFileName(f),
                                Tip = f,
                                Thumb = ThumbFor(f),
                                Path = f
                            });
                        else if (f.EndsWith(".ghostscene", StringComparison.OrdinalIgnoreCase))
                            entries.Add(new Entry
                            {
                                Key = "f:" + f,
                                Label = Path.GetFileNameWithoutExtension(f),
                                Tip = f,
                                Thumb = SceneIcon(),
                                Path = f,
                                IsScene = true
                            });
                    }
                }
                catch { /* folder vanished or is unreadable: show what we have */ }

                string rel = Path.GetRelativePath(root ?? _folder, _folder);
                rel = rel == "." ? "" : "/" + rel.Replace('\\', '/');
                _hint.Text = "Content" + rel + "   -   drop images here to import   -   double-click a scene to open it   -   drag a tile onto an object (Shift = one face)";
            }
            _up.Visible = _folder != null && !atRoot;

            _flow.SuspendLayout();
            var old = _flow.Controls.Cast<Control>().ToArray();
            _flow.Controls.Clear();
            foreach (Control c in old) c.Dispose();
            _tiles.Clear();

            foreach (Entry e in entries)
            {
                Panel tile = MakeTile(e);
                _flow.Controls.Add(tile);
                _tiles[e.Key] = tile;
            }
            _flow.ResumeLayout();
            Highlight();
        }

        private void Highlight()
        {
            foreach (var kv in _tiles)
                kv.Value.BackColor = (kv.Key == _selectedKey) ? SelColor : Bg;
        }

        // Built-in / loaded textures already have an id; a file on disk is imported the first time it's used
        private static int ResolveTexture(Entry e)
        {
            if (e.TexId >= 0) return e.TexId;
            try
            {
                int id = GhostAssets.ImportOrGet(e.Path!);
                if (id < 0)
                    MessageBox.Show("The engine could not create the texture (too large, or the texture limit was reached).",
                        "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return id;
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return -1;
            }
        }

        private Panel MakeTile(Entry e)
        {
            var tile = new Panel { Width = 108, Height = 130, Margin = new Padding(4), BackColor = Bg, Cursor = Cursors.Hand };
            var pic = new PictureBox
            {
                Image = e.Thumb,
                SizeMode = PictureBoxSizeMode.Zoom,
                Location = new Point(6, 6),
                Size = new Size(96, 96),
                BackColor = Color.Black,
            };
            var lbl = new Label
            {
                Text = e.Label,
                AutoEllipsis = true,
                Location = new Point(2, 106),
                Size = new Size(104, 20),
                TextAlign = ContentAlignment.MiddleCenter,
                ForeColor = Fg,
            };
            tile.Controls.Add(pic);
            tile.Controls.Add(lbl);

            foreach (Control c in new Control[] { tile, pic, lbl })
            {
                _tip.SetToolTip(c, e.Tip);
                WireFileDrop(c);                                   // dropping files on a tile still imports them

                c.MouseDown += (s, ev) =>
                {
                    if (ev.Button != MouseButtons.Left) return;
                    _pressKey = e.Key;
                    _pressPoint = Control.MousePosition;
                    _selectedKey = e.Key;
                    Highlight();
                };
                c.MouseMove += (s, ev) =>
                {
                    if (ev.Button != MouseButtons.Left || _pressKey != e.Key || e.IsFolder || e.IsScene) return;
                    Size d = SystemInformation.DragSize;
                    Point p = Control.MousePosition;
                    if (Math.Abs(p.X - _pressPoint.X) < d.Width && Math.Abs(p.Y - _pressPoint.Y) < d.Height) return;
                    _pressKey = "";

                    int id = ResolveTexture(e);
                    if (id < 0) return;
                    _inDrag = true;
                    try { DoDragDrop($"ghost:tex:{id}", DragDropEffects.Copy); }
                    finally
                    {
                        _inDrag = false;
                        if (_pending) { _pending = false; QueueRebuild(); }
                    }
                };
                c.DoubleClick += (s, ev) =>
                {
                    if (e.IsFolder)
                        BeginInvoke(new Action(() => { _folder = e.Path; Rebuild(); }));
                    else if (e.IsScene)
                        BeginInvoke(new Action(() => OpenSceneRequested?.Invoke(e.Path!)));
                    else
                    {
                        int id = ResolveTexture(e);
                        if (id >= 0) ApplyRequested?.Invoke(id);
                    }
                };
            }
            return tile;
        }

        // ---------------- thumbnails and icons ----------------
        private static Bitmap ThumbFor(string path)
        {
            if (ThumbCache.TryGetValue(path, out Bitmap? b)) return b;
            Bitmap? t = GhostAssets.TryMakeThumb(path);       // null while the file is still being copied: not cached
            if (t == null) return BlankIcon();
            ThumbCache[path] = t;
            return t;
        }

        private static Bitmap FolderIcon() => _folderIcon ??= DrawIcon(0);
        private static Bitmap SceneIcon() => _sceneIcon ??= DrawIcon(1);
        private static Bitmap BlankIcon() => _blankIcon ??= DrawIcon(2);

        private static Bitmap DrawIcon(int kind)
        {
            var b = new Bitmap(96, 96);
            using var g = Graphics.FromImage(b);
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            g.Clear(Color.FromArgb(0x2A, 0x2A, 0x30));
            if (kind == 0)                                    // folder
            {
                using var br = new SolidBrush(Color.FromArgb(0xE0, 0xB0, 0x40));
                g.FillRectangle(br, 14, 26, 32, 14);
                g.FillRectangle(br, 14, 36, 68, 40);
            }
            else if (kind == 1)                               // scene
            {
                using var br = new SolidBrush(Color.FromArgb(0x5A, 0x9B, 0xE6));
                g.FillRectangle(br, 18, 22, 60, 52);
                using var f = new Font("Segoe UI", 10f, FontStyle.Bold);
                g.DrawString("SCENE", f, Brushes.White, 22, 40);
            }
            else                                              // unreadable image
            {
                using var br = new SolidBrush(Color.FromArgb(0x55, 0x55, 0x5A));
                g.FillRectangle(br, 22, 22, 52, 52);
            }
            return b;
        }







    }
}