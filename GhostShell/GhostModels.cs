// GhostModels.cs — temporary model import (11.A): file dialog, options, engine call
using System;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class ModelImportDialog : Form
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);

        private readonly NumericUpDown _scale = new(), _fit = new();
        private readonly ComboBox _up = new();
        private readonly CheckBox _flip = new();

        public float Scale => (float)_scale.Value;
        public float FitSize => (float)_fit.Value;
        public int UpAxis => Math.Max(_up.SelectedIndex, 0);
        public bool FlipWinding => _flip.Checked;

        public ModelImportDialog(string fileName)
        {
            Text = "Import Model";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false; MinimizeBox = false; ShowInTaskbar = false;
            ClientSize = new Size(470, 250);
            BackColor = Bg; ForeColor = Fg;

            Controls.Add(new Label { Text = fileName, Location = new Point(16, 14), Size = new Size(438, 22), AutoEllipsis = true });

            Controls.Add(new Label { Text = "Scale (multiplier)", Location = new Point(16, 52), AutoSize = true });
            Style(_scale, 250, 48, 4, 0.0001m, 10000m, 1m, 0.1m);

            Controls.Add(new Label { Text = "Fit largest side to (units, 0 = off)", Location = new Point(16, 88), AutoSize = true });
            Style(_fit, 250, 84, 2, 0m, 10000m, 0m, 0.5m);

            Controls.Add(new Label { Text = "Up axis", Location = new Point(16, 124), AutoSize = true });
            _up.Location = new Point(250, 120); _up.Width = 200;
            _up.DropDownStyle = ComboBoxStyle.DropDownList; _up.FlatStyle = FlatStyle.Flat;
            _up.BackColor = Bg2; _up.ForeColor = Fg;
            _up.Items.AddRange(new object[] { "Auto (from the file)", "Y up", "Z up" });
            _up.SelectedIndex = 0;
            Controls.Add(_up);

            _flip.Text = "Flip face winding (use if the model looks inside-out)";
            _flip.Location = new Point(16, 158); _flip.AutoSize = true; _flip.ForeColor = Fg;
            Controls.Add(_flip);

            var hint = new Label
            {
                Location = new Point(16, 186),
                Size = new Size(438, 20),
                ForeColor = Color.Gray,
                Text = "Fit is applied first, then Scale. Tip: Fit = 2 gives a predictable size.",
            };
            Controls.Add(hint);

            var ok = new Button
            {
                Text = "Import",
                Location = new Point(280, 210),
                Size = new Size(84, 28),
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg,
                DialogResult = DialogResult.OK
            };
            var cancel = new Button
            {
                Text = "Cancel",
                Location = new Point(372, 210),
                Size = new Size(84, 28),
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg,
                DialogResult = DialogResult.Cancel
            };
            Controls.Add(ok); Controls.Add(cancel);
            AcceptButton = ok; CancelButton = cancel;
        }

        private void Style(NumericUpDown n, int x, int y, int places, decimal min, decimal max, decimal value, decimal step)
        {
            n.Location = new Point(x, y); n.Width = 200;
            n.DecimalPlaces = places; n.Minimum = min; n.Maximum = max; n.Increment = step; n.Value = value;
            n.BackColor = Bg2; n.ForeColor = Fg; n.BorderStyle = BorderStyle.FixedSingle;
            Controls.Add(n);
        }
    }

    internal static class GhostModels
    {
        public const string Filter =
            "3D models (*.fbx;*.obj;*.gltf;*.glb;*.dae;*.3ds;*.stl;*.ply)|*.fbx;*.obj;*.gltf;*.glb;*.dae;*.3ds;*.stl;*.ply|All files (*.*)|*.*";

        private static string Utf8(byte[] b)
        {
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        public static void ImportInteractive(IWin32Window owner)
        {
            if (GhostPlay.State != 0)
            {
                MessageBox.Show(owner, "Stop playing before importing a model.", "Import Model",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            using var open = new OpenFileDialog { Title = "Import Model", Filter = Filter };
            if (open.ShowDialog(owner) != DialogResult.OK) return;
            using var dlg = new ModelImportDialog(Path.GetFileName(open.FileName));
            if (dlg.ShowDialog(owner) != DialogResult.OK) return;

            var buf = new byte[4096];
            int id;
            Cursor.Current = Cursors.WaitCursor;
            try
            {
                id = GhostEngine.GhostEngine_ImportModel(open.FileName, dlg.Scale, dlg.FitSize, dlg.UpAxis,
                    dlg.FlipWinding ? 1 : 0, buf, buf.Length);
            }
            finally { Cursor.Current = Cursors.Default; }

            string msg = Utf8(buf);
            if (id < 0)
            {
                MessageBox.Show(owner, msg, "Import failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            int obj = GhostEngine.GhostEngine_AddObject(id, -1, -1);      // dropped at the screen center, resting on the ground
            if (obj == 0) msg += "\n\nThe scene is full, so the model was imported but not placed.";
            MessageBox.Show(owner, msg, "Model imported", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }
}
