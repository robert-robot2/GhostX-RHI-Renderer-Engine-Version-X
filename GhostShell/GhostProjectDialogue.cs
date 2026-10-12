// GhostProjectDialog.cs — New Project dialog + project actions (new / open / Visual Studio)
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class NewProjectDialog : Form
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);

        private static readonly string[] TemplateNames = { "Blank", "Starter (cube + lights)" };
        private static readonly string[] TemplateInfo =
        {
            "An empty scene with one default camera.",
            "A grey cube, a directional light and a point light, ready to look at.",
        };

        private readonly TextBox _name = new();
        private readonly TextBox _folder = new();
        private readonly ListBox _templates = new();
        private readonly Label _info = new();
        private readonly Label _preview = new();

        public string ProjectName => _name.Text.Trim();
        public string ParentFolder => _folder.Text.Trim();
        public int Template => Math.Max(_templates.SelectedIndex, 0);

        public NewProjectDialog()
        {
            Text = "New Project";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false; MinimizeBox = false; ShowInTaskbar = false;
            ClientSize = new Size(520, 350);
            BackColor = Bg; ForeColor = Fg;

            string defFolder = GhostSettings.Data.LastProjectFolder;
            if (string.IsNullOrEmpty(defFolder))
                defFolder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "GhostX Projects");

            Controls.Add(Lbl("Project name", 16, 14));
            Style(_name, 16, 36, 488); _name.Text = "MyGame";

            Controls.Add(Lbl("Location", 16, 72));
            Style(_folder, 16, 94, 392); _folder.Text = defFolder;
            var browse = new Button
            {
                Text = "Browse...",
                Location = new Point(416, 92),
                Size = new Size(88, 28),
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg
            };
            browse.Click += (s, e) =>
            {
                using var fb = new FolderBrowserDialog { SelectedPath = _folder.Text, Description = "Choose where the project folder will be created" };
                if (fb.ShowDialog(this) == DialogResult.OK) _folder.Text = fb.SelectedPath;
            };
            Controls.Add(browse);

            Controls.Add(Lbl("Template", 16, 132));
            _templates.Location = new Point(16, 154); _templates.Size = new Size(488, 60);
            _templates.BackColor = Bg2; _templates.ForeColor = Fg; _templates.BorderStyle = BorderStyle.FixedSingle;
            _templates.Items.AddRange(TemplateNames);
            _templates.SelectedIndex = 1;
            _templates.SelectedIndexChanged += (s, e) => _info.Text = TemplateInfo[Template];
            Controls.Add(_templates);

            _info.Location = new Point(16, 220); _info.Size = new Size(488, 20);
            _info.ForeColor = Color.Gray; _info.Text = TemplateInfo[Template];
            Controls.Add(_info);

            _preview.Location = new Point(16, 254); _preview.Size = new Size(488, 40);
            _preview.ForeColor = Color.Gray; _preview.AutoEllipsis = true;
            Controls.Add(_preview);
            _name.TextChanged += (s, e) => UpdatePreview();
            _folder.TextChanged += (s, e) => UpdatePreview();
            UpdatePreview();

            var ok = new Button
            {
                Text = "Create",
                Location = new Point(316, 304),
                Size = new Size(90, 30),
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg
            };
            var cancel = new Button
            {
                Text = "Cancel",
                Location = new Point(414, 304),
                Size = new Size(90, 30),
                FlatStyle = FlatStyle.Flat,
                BackColor = Bg2,
                ForeColor = Fg,
                DialogResult = DialogResult.Cancel
            };
            ok.Click += (s, e) =>
            {
                if (!GhostProject.ValidateNew(ProjectName, ParentFolder, out string err))
                { MessageBox.Show(this, err, "New Project", MessageBoxButtons.OK, MessageBoxIcon.Information); return; }
                DialogResult = DialogResult.OK;
            };
            Controls.Add(ok); Controls.Add(cancel);
            AcceptButton = ok; CancelButton = cancel;
        }

        private Label Lbl(string text, int x, int y) =>
            new Label { Text = text, Location = new Point(x, y), AutoSize = true, ForeColor = Fg };

        private void Style(TextBox t, int x, int y, int w)
        {
            t.Location = new Point(x, y); t.Size = new Size(w, 26);
            t.BackColor = Bg2; t.ForeColor = Fg; t.BorderStyle = BorderStyle.FixedSingle;
            Controls.Add(t);
        }

        private void UpdatePreview()
        {
            _preview.Text = ProjectName.Length > 0 && ParentFolder.Length > 0
                ? $"Will be created in:\n{Path.Combine(ParentFolder, ProjectName)}" : "";
        }
    }

    internal static class GhostProjectFlow
    {
        public static void NewProject(IWin32Window owner)
        {
            using var dlg = new NewProjectDialog();
            if (dlg.ShowDialog(owner) != DialogResult.OK) return;
            if (!GhostSceneFile.ConfirmDiscard(owner)) return;

            try
            {
                string manifest = GhostProject.Create(dlg.ProjectName, dlg.ParentFolder);
                GhostSettings.Data.LastProjectFolder = dlg.ParentFolder;
                GhostSettings.Save();

                if (!GhostProject.Open(manifest, out string err))
                { MessageBox.Show(owner, err, "New Project failed", MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }

                GhostSceneFile.New();
                if (dlg.Template == 1) BuildStarterScene();
                GhostSceneFile.Save(Path.Combine(GhostProject.ScenesDir!, "Main.ghostscene"));
            }
            catch (Exception ex)
            {
                MessageBox.Show(owner, ex.Message, "New Project failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        public static void OpenProject(IWin32Window owner)
        {
            using var dlg = new OpenFileDialog { Title = "Open Project", Filter = GhostProject.Filter };
            if (dlg.ShowDialog(owner) == DialogResult.OK) OpenProjectPath(owner, dlg.FileName);
        }

        public static void OpenProjectPath(IWin32Window owner, string path)
        {
            if (!File.Exists(path))
            {
                MessageBox.Show(owner, $"The project file was not found:\n{path}", "Open Project", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                GhostSettings.RemoveRecent(path);
                return;
            }
            if (!GhostSceneFile.ConfirmDiscard(owner)) return;

            if (!GhostProject.Open(path, out string err))
            { MessageBox.Show(owner, err, "Open Project failed", MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }

            string start = GhostProject.Current!.StartScene;
            string scenePath = start.Length > 0 ? Path.Combine(GhostProject.Root!, start) : "";
            if (scenePath.Length > 0 && File.Exists(scenePath)) GhostSceneFile.Open(scenePath);
            else GhostSceneFile.New();
        }

        // A grey cube, a directional light and a point light
        private static void BuildStarterScene()
        {
            int cube = GhostEngine.GhostEngine_AddObject(0, -1, -1);
            var o = new GhostObjectInfo();
            if (cube != 0 && GhostEngine.GhostEngine_GetObject(cube, ref o) != 0)
            {
                o.px = 0f; o.py = 0.5f; o.pz = 3f;
                GhostEngine.GhostEngine_SetObject(ref o);
            }

            GhostEngine.GhostEngine_AddLight(0, -1, -1);

            int pl = GhostEngine.GhostEngine_AddLight(1, -1, -1);
            var l = new GhostLightInfo();
            if (pl != 0 && GhostEngine.GhostEngine_GetLight(pl, ref l) != 0)
            {
                l.px = -2f; l.py = 2f; l.pz = 1f;
                GhostEngine.GhostEngine_SetLight(ref l);
            }
            GhostEngine.GhostEngine_SelectEntity(0);
        }

        // ---------------- Visual Studio ----------------
        public static void OpenInVisualStudio(IWin32Window owner)
        {
            string? sln = GhostSettings.Data.SolutionPath;
            if (string.IsNullOrEmpty(sln) || !File.Exists(sln))
            {
                sln = FindSolution();
                if (sln == null) sln = PickSolution(owner);
                else { GhostSettings.Data.SolutionPath = sln; GhostSettings.Save(); }
            }
            if (sln == null) return;

            try { Process.Start(new ProcessStartInfo(sln) { UseShellExecute = true }); }
            catch (Exception ex)
            {
                MessageBox.Show(owner, ex.Message, "Could not open Visual Studio", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        public static string? PickSolution(IWin32Window owner)
        {
            using var dlg = new OpenFileDialog
            {
                Title = "Choose the GhostX Visual Studio solution",
                Filter = "Visual Studio Solution (*.slnx;*.sln)|*.slnx;*.sln",
            };
            if (dlg.ShowDialog(owner) != DialogResult.OK) return null;
            GhostSettings.Data.SolutionPath = dlg.FileName;
            GhostSettings.Save();
            return dlg.FileName;
        }

        // Walks up from the exe (bin\Debug\...) to find the engine solution
        private static string? FindSolution()
        {
            try
            {
                var dir = new DirectoryInfo(AppContext.BaseDirectory);
                for (int i = 0; i < 10 && dir != null; i++, dir = dir.Parent)
                {
                    var hit = dir.GetFiles("*.slnx");
                    if (hit.Length == 0) hit = dir.GetFiles("*.sln");
                    if (hit.Length > 0) return hit[0].FullName;
                }
            }
            catch { }
            return null;
        }
    }
}