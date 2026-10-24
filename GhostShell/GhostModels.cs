// GhostModels.cs — model import (dialog, files), .ghostmesh loading, and mesh info for the UI
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
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
        private readonly CheckBox _flip = new(), _place = new();
        private readonly CheckBox _mats = new();
        public float Scale => (float)_scale.Value;
        public float FitSize => (float)_fit.Value;
        public int UpAxis => Math.Max(_up.SelectedIndex, 0);
        public bool FlipWinding => _flip.Checked;
        public bool PlaceInScene => _place.Visible && _place.Checked;

        public int Flags => (_flip.Checked ? 1 : 0) | (_mats.Checked ? 0 : 2);

        // Fills the controls with the settings a model was imported with (used by Re-import with settings)
        public void Preset(float scale, float fit, int up, int flags)
        {
            _scale.Value = Math.Clamp((decimal)scale, _scale.Minimum, _scale.Maximum);
            _fit.Value = Math.Clamp((decimal)fit, _fit.Minimum, _fit.Maximum);
            _up.SelectedIndex = Math.Clamp(up, 0, 2);
            _flip.Checked = (flags & 1) != 0;
            _mats.Checked = (flags & 2) == 0;
        }

        public ModelImportDialog(string[] files, bool offerPlace)
        {
            Text = "Import Model";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false; MinimizeBox = false; ShowInTaskbar = false;
            ClientSize = new Size(470, 316);
            BackColor = Bg; ForeColor = Fg;

            string title = files.Length == 1 ? Path.GetFileName(files[0]) : $"{files.Length} files (the same settings apply to each)";
            Controls.Add(new Label { Text = title, Location = new Point(16, 14), Size = new Size(438, 22), AutoEllipsis = true });

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

            _place.Text = "Place in the scene after importing";
            _place.Location = new Point(16, 186); _place.AutoSize = true; _place.ForeColor = Fg;
            _place.Checked = true; _place.Visible = offerPlace;
            Controls.Add(_place);

            _mats.Text = "Import materials and textures from the file";
            _mats.Location = new Point(16, 214); _mats.AutoSize = true; _mats.ForeColor = Fg; _mats.Checked = true;
            Controls.Add(_mats);

            Controls.Add(new Label
            {
                Location = new Point(16, 244),
                Size = new Size(438, 20),
                ForeColor = Color.Gray,
                Text = "Fit is applied first, then Scale. Tip: Fit = 2 gives a predictable size.",
            });

            var ok = new Button
            {
                Text = "Import",
                Location = new Point(244, 278),
                Size = new Size(84, 28),
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg,
                DialogResult = DialogResult.OK
            };
            var cancel = new Button
            {
                Text = "Cancel",
                Location = new Point(372, 244),
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
        public const string MeshExt = ".ghostmesh";
        public const string Filter =
            "3D models (*.fbx;*.obj;*.gltf;*.glb;*.dae;*.3ds;*.stl;*.ply)|*.fbx;*.obj;*.gltf;*.glb;*.dae;*.3ds;*.stl;*.ply|All files (*.*)|*.*";

        private static readonly string[] Exts = { ".fbx", ".obj", ".gltf", ".glb", ".dae", ".3ds", ".stl", ".ply" };
        private static readonly HashSet<string> Reported = new(StringComparer.OrdinalIgnoreCase);

        public static event Action? Changed;            // a model was imported: the palette refreshes

        public static bool IsModelFile(string path) =>
            Array.IndexOf(Exts, Path.GetExtension(path).ToLowerInvariant()) >= 0;

        private static string Utf8(byte[] b)
        {
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        // ---------------- mesh info ----------------
        public static string MeshText(int meshId, int which)
        {
            var b = new byte[2048];
            return GhostEngine.GhostEngine_GetMeshText(meshId, which, b, b.Length) == 0 ? "" : Utf8(b);
        }

        public static string MeshName(int meshId) => MeshText(meshId, 0);

        public static bool IsBuiltin(int meshId) =>
            GhostEngine.GhostEngine_GetMeshInfo(meshId, out GhostMeshStats s) == 0 || s.builtin != 0;

        public static string MissingMesh(int objectId)
        {
            var b = new byte[2048];
            return GhostEngine.GhostEngine_GetObjectMissingMesh(objectId, b, b.Length) == 0 ? "" : Utf8(b);
        }

        // Models of the open project, plus any imported this session without a project
        public static bool BelongsToProject(int meshId)
        {
            string file = MeshText(meshId, 1);
            string? root = GhostProject.Root;
            if (file.Length == 0 || root == null) return true;
            return file.StartsWith(root, StringComparison.OrdinalIgnoreCase);
        }

        // ---------------- loading converted files ----------------
        public static int LoadFile(string path)
        {
            var buf = new byte[4096];
            int id = GhostEngine.GhostEngine_LoadModelFile(path, buf, buf.Length);
            string text = Utf8(buf);
            if (id < 0 && Reported.Add(path))                // one message per file, not one per palette refresh
                MessageBox.Show($"{Path.GetFileName(path)}:\n{text}", "Model", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            else if (id >= 0 && text.Length > 0 && Reported.Add(path))
                MessageBox.Show(text, "Model loaded with warnings", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return id;
        }

        // Loads every .ghostmesh in the project's Content/Models folder
        public static void ScanProject()
        {
            string? content = GhostProject.ContentDir;
            if (content == null) return;
            string dir = Path.Combine(content, "Models");
            if (!Directory.Exists(dir)) return;
            try
            {
                foreach (string f in Directory.EnumerateFiles(dir, "*" + MeshExt, SearchOption.AllDirectories)) LoadFile(f);
            }
            catch { /* folder vanished: nothing to list */ }
        }

        // ---------------- importing ----------------
        public static void ImportInteractive(IWin32Window owner)
        {
            using var open = new OpenFileDialog { Title = "Import Model", Filter = Filter, Multiselect = true };
            if (open.ShowDialog(owner) != DialogResult.OK) return;
            ImportFiles(owner, open.FileNames, true);
        }

        // offerPlace: show "Place in the scene" (single files only)
        public static void ImportFiles(IWin32Window owner, string[] files, bool offerPlace)
        {
            string[] models = files.Where(IsModelFile).ToArray();
            if (models.Length == 0) return;

            if (GhostPlay.State != 0)
            {
                MessageBox.Show(owner, "Stop playing before importing a model.", "Import Model",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            using var dlg = new ModelImportDialog(models, offerPlace && models.Length == 1);
            if (dlg.ShowDialog(owner) != DialogResult.OK) return;

            var report = new StringBuilder();
            bool anyFailed = false;
            Cursor.Current = Cursors.WaitCursor;
            try
            {
                foreach (string f in models)
                {
                    var buf = new byte[4096];
                    int id = GhostEngine.GhostEngine_ImportModel(f, dlg.Scale, dlg.FitSize, dlg.UpAxis,
                        dlg.Flags, buf, buf.Length);
                    string msg = Utf8(buf);

                    if (id < 0)
                    {
                        anyFailed = true;
                        report.AppendLine($"{Path.GetFileName(f)}: {msg}").AppendLine();
                        continue;
                    }
                    // one model: the full summary; several: the first two lines of each
                    report.AppendLine(models.Length == 1 ? msg : string.Join(" - ", msg.Split('\n').Take(2))).AppendLine();
                    if (dlg.PlaceInScene && GhostEngine.GhostEngine_AddObject(id, -1, -1) == 0)
                        report.AppendLine("The scene is full, so the model was imported but not placed.").AppendLine();
                }
            }
            finally { Cursor.Current = Cursors.Default; }

            Changed?.Invoke();
            MessageBox.Show(owner, report.ToString().TrimEnd(), anyFailed ? "Import finished with problems" : "Import Model",
                MessageBoxButtons.OK, anyFailed ? MessageBoxIcon.Warning : MessageBoxIcon.Information);
        }

        // ---------------- re-import ----------------
        public static void ReimportSelected(IWin32Window owner, bool askSettings)
        {
            int sel = GhostEngine.GhostEngine_GetSelected();
            var o = new GhostObjectInfo();
            if (sel == 0 || GhostEngine.GhostEngine_GetObject(sel, ref o) == 0 || IsBuiltin(o.meshId))
            {
                MessageBox.Show(owner, "Select a placed imported model first.", "Re-import",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            Reimport(owner, o.meshId, askSettings);
        }

        public static void Reimport(IWin32Window owner, int meshId, bool askSettings)
        {
            if (GhostPlay.State != 0)
            {
                MessageBox.Show(owner, "Stop playing before re-importing a model.", "Re-import",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            string src = MeshText(meshId, 2);
            if (IsBuiltin(meshId) || src.Length == 0 || !File.Exists(src))
            {
                MessageBox.Show(owner, "The original file was not found:\n" + (src.Length == 0 ? "(unknown)" : src) +
                    "\n\nRe-import needs the source file where it was when you imported it.",
                    "Re-import", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            int useStored = 1, up = 0, flags = 0;
            float scale = 1f, fit = 0f;
            if (askSettings)
            {
                GhostEngine.GhostEngine_GetMeshInfo(meshId, out GhostMeshStats st);
                using var dlg = new ModelImportDialog(new[] { src }, false);
                dlg.Preset(st.importScale, st.importFit, st.importUp, st.importFlags);
                if (dlg.ShowDialog(owner) != DialogResult.OK) return;
                useStored = 0; scale = dlg.Scale; fit = dlg.FitSize; up = dlg.UpAxis; flags = dlg.Flags;
            }

            var buf = new byte[4096];
            int id;
            Cursor.Current = Cursors.WaitCursor;
            try { id = GhostEngine.GhostEngine_ReimportModel(meshId, useStored, scale, fit, up, flags, buf, buf.Length); }
            finally { Cursor.Current = Cursors.Default; }

            MessageBox.Show(owner, Utf8(buf), id < 0 ? "Re-import failed" : "Model re-imported",
                MessageBoxButtons.OK, id < 0 ? MessageBoxIcon.Warning : MessageBoxIcon.Information);
        }

    }
}