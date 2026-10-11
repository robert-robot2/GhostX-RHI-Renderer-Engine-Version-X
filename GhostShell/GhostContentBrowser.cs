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
        private readonly Dictionary<int, Panel> _tiles = new();
        private int _selectedId = -1;
        private int _pressId = -1;
        private Point _pressPoint;

        // Double-click on a tile: apply that texture to the selected object
        public event Action<int>? ApplyRequested;

        public GhostContentBrowser()
        {
            BackColor = Bg;

            var bar = new Panel { Dock = DockStyle.Top, Height = 30, BackColor = Bg };
            var hint = new Label
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
            bar.Controls.Add(hint);       // Fill first, then Left
            bar.Controls.Add(import);

            _flow = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                AutoScroll = true,
                BackColor = Bg2,
                Padding = new Padding(6),
            };

            Controls.Add(_flow);          // Fill first, then Top
            Controls.Add(bar);

            foreach (Control c in new Control[] { this, bar, hint, import, _flow })
                WireFileDrop(c);

            GhostAssets.Changed += Rebuild;
            Disposed += (s, e) => GhostAssets.Changed -= Rebuild;
            Rebuild();
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
                    if (GhostAssets.Import(f) < 0)
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
        private void Rebuild()
        {
            _flow.SuspendLayout();
            var old = _flow.Controls.Cast<Control>().ToArray();
            _flow.Controls.Clear();
            foreach (Control c in old) c.Dispose();
            _tiles.Clear();

            foreach (TextureAsset a in GhostAssets.All)
            {
                Panel tile = MakeTile(a);
                _flow.Controls.Add(tile);
                _tiles[a.Id] = tile;
            }
            _flow.ResumeLayout();
            Highlight();
        }

        private void Highlight()
        {
            foreach (var kv in _tiles)
                kv.Value.BackColor = (kv.Key == _selectedId) ? SelColor : Bg;
        }

        private Panel MakeTile(TextureAsset a)
        {
            var tile = new Panel { Width = 108, Height = 130, Margin = new Padding(4), BackColor = Bg, Cursor = Cursors.Hand };
            var pic = new PictureBox
            {
                Image = a.Thumb,
                SizeMode = PictureBoxSizeMode.Zoom,
                Location = new Point(6, 6),
                Size = new Size(96, 96),
                BackColor = Color.Black,
            };
            var lbl = new Label
            {
                Text = a.Name,
                AutoEllipsis = true,
                Location = new Point(2, 106),
                Size = new Size(104, 20),
                TextAlign = ContentAlignment.MiddleCenter,
                ForeColor = Fg,
            };
            tile.Controls.Add(pic);
            tile.Controls.Add(lbl);

            string tip = a.Path ?? $"{a.Name} (built-in)";
            foreach (Control c in new Control[] { tile, pic, lbl })
            {
                _tip.SetToolTip(c, tip);
                WireFileDrop(c);                                   // dropping files on a tile still imports them

                c.MouseDown += (s, e) =>
                {
                    if (e.Button != MouseButtons.Left) return;
                    _pressId = a.Id;
                    _pressPoint = Control.MousePosition;
                    _selectedId = a.Id;
                    Highlight();
                };
                c.MouseMove += (s, e) =>
                {
                    if (e.Button != MouseButtons.Left || _pressId != a.Id) return;
                    Size d = SystemInformation.DragSize;
                    Point p = Control.MousePosition;
                    if (Math.Abs(p.X - _pressPoint.X) < d.Width && Math.Abs(p.Y - _pressPoint.Y) < d.Height) return;
                    _pressId = -1;
                    DoDragDrop($"ghost:tex:{a.Id}", DragDropEffects.Copy);
                };
                c.DoubleClick += (s, e) => ApplyRequested?.Invoke(a.Id);
            }
            return tile;
        }
    }
}