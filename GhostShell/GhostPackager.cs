// GhostPackager.cs — File > Package Project: builds Release x64 (or takes an existing build) and gathers a runnable game folder
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class PackageOptions
    {
        public string OutRoot = "";
        public bool UseExisting;
        public string ExistingFolder = "";
        public bool SelfContained;
        public bool Zip;
    }

    internal static class GhostPackager
    {
        private static readonly string[] RuntimeDlls =
            { "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "concrt140.dll" };
        private static readonly string[] JunkExt = { ".pdb", ".lib", ".exp", ".ilk", ".iobj", ".ipdb" };

        public static string SafeName(string s)
        {
            var sb = new StringBuilder();
            foreach (char c in s.Trim()) sb.Append(Array.IndexOf(Path.GetInvalidFileNameChars(), c) >= 0 ? '_' : c);
            return sb.Length == 0 ? "Game" : sb.ToString();
        }

        public static void Show(IWin32Window owner)
        {
            if (!GhostProject.IsOpen)
            {
                MessageBox.Show(owner, "Open or create a project first.", "Package Project", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            if (GhostPlay.State != 0)
            {
                MessageBox.Show(owner, "Stop playing first.", "Package Project", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            if (!GhostSceneFile.ConfirmDiscard(owner)) return;      // the package uses the saved files on disk
            using var dlg = new PackageDialog();
            dlg.ShowDialog(owner);
        }

        // ---------------- helpers ----------------
        private static int RunProcess(string exe, IEnumerable<string> args, Action<string> onLine)
        {
            var psi = new ProcessStartInfo(exe)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            foreach (string a in args) psi.ArgumentList.Add(a);          // quoting and trailing backslashes are handled
            using Process p = Process.Start(psi)!;
            p.OutputDataReceived += (s, e) => { if (e.Data != null) onLine(e.Data); };
            p.ErrorDataReceived += (s, e) => { if (e.Data != null) onLine(e.Data); };
            p.BeginOutputReadLine();
            p.BeginErrorReadLine();
            p.WaitForExit();
            return p.ExitCode;
        }

        private static string FindMsBuild()
        {
            string vswhere = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                "Microsoft Visual Studio", "Installer", "vswhere.exe");
            if (File.Exists(vswhere))
            {
                var lines = new List<string>();
                RunProcess(vswhere, new[] { "-latest", "-prerelease", "-requires", "Microsoft.Component.MSBuild",
                    "-find", @"MSBuild\**\Bin\MSBuild.exe" }, l => lines.Add(l.Trim()));
                foreach (string l in lines)
                    if (l.EndsWith("MSBuild.exe", StringComparison.OrdinalIgnoreCase) && File.Exists(l)) return l;
            }
            throw new InvalidOperationException("MSBuild was not found (Visual Studio's installer tool vswhere didn't list it).\n" +
                "Build GhostX, GhostGame and GhostShell as Release | x64 in Visual Studio, then choose \"Use an existing Release folder\".");
        }

        private static string? FindSolution()
        {
            string? sln = GhostSettings.Data.SolutionPath;
            if (!string.IsNullOrEmpty(sln) && File.Exists(sln)) return sln;
            return GhostProjectFlow.FindSolution();
        }

        private static string? FindFile(string dir, string fileName)
        {
            var opt = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true };
            foreach (string f in Directory.EnumerateFiles(dir, fileName, opt))
            {
                string p = f.Replace('/', '\\');
                if (p.Contains("\\obj\\") || p.Contains("\\bin\\") || p.Contains("\\.vs\\") || p.Contains("\\x64\\")) continue;
                return f;
            }
            return null;
        }

        private static int CopyTree(string src, string dst)
        {
            int n = 0;
            Directory.CreateDirectory(dst);
            foreach (string d in Directory.EnumerateDirectories(src, "*", SearchOption.AllDirectories))
                Directory.CreateDirectory(Path.Combine(dst, Path.GetRelativePath(src, d)));
            foreach (string f in Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories))
            {
                if (Path.GetExtension(f).Equals(".tmp", StringComparison.OrdinalIgnoreCase)) continue;
                File.Copy(f, Path.Combine(dst, Path.GetRelativePath(src, f)), true);
                n++;
            }
            return n;
        }

        // A Debug DLL imports the debug runtime (…140D.dll), which doesn't exist on other PCs
        private static bool IsDebugBinary(string path)
        {
            try
            {
                string text = Encoding.Latin1.GetString(File.ReadAllBytes(path));
                return text.IndexOf("VCRUNTIME140D", StringComparison.OrdinalIgnoreCase) >= 0 ||
                       text.IndexOf("MSVCP140D", StringComparison.OrdinalIgnoreCase) >= 0 ||
                       text.IndexOf("UCRTBASED", StringComparison.OrdinalIgnoreCase) >= 0;
            }
            catch { return false; }
        }

        private static void Build(string msbuild, string proj, string label, string outDir, string slnDir,
            bool selfContained, Action<string> log)
        {
            log($"=== Building {label} (Release x64) ===");
            var a = new List<string>
            {
                proj, "-nologo", "-v:minimal", "-restore",
                "-p:Configuration=Release", "-p:Platform=x64",
                "-p:OutDir=" + outDir, "-p:SolutionDir=" + slnDir + "\\",
            };
            if (selfContained && proj.EndsWith(".csproj", StringComparison.OrdinalIgnoreCase))
            { a.Add("-p:RuntimeIdentifier=win-x64"); a.Add("-p:SelfContained=true"); }
            int code = RunProcess(msbuild, a, log);
            if (code != 0)
                throw new InvalidOperationException($"MSBuild failed on {label} (exit code {code}). The log shows why. " +
                    "Fix it in Visual Studio (Release | x64) and try again.");
        }

        // ---------------- the job ----------------
        // Returns the package folder. Throws with a readable message on failure.
        public static string Run(PackageOptions o, Action<string> log, out string summary)
        {
            if (!GhostProject.IsOpen) throw new InvalidOperationException("Open or create a project first.");
            string root = GhostProject.Root!;
            ProjectManifest m = GhostProject.Current!;
            string manifest = GhostProject.ManifestPath!;
            string name = SafeName(m.Name);

            string startRel = m.StartScene ?? "";
            if (startRel.Length == 0 || !File.Exists(Path.Combine(root, startRel)))
                throw new InvalidOperationException($"The start scene was not found: \"{startRel}\".\n" +
                    $"Set \"startScene\" in {Path.GetFileName(manifest)} to a scene that exists, for example \"Scenes/Level1.ghostscene\".");

            string stage = Path.GetFullPath(Path.Combine(o.OutRoot, name));
            var warn = new List<string>();
            string? sln = FindSolution();
            string? slnDir = sln == null ? null : Path.GetDirectoryName(sln);

            // ---- prepare the output folder ----
            if (Directory.Exists(stage))
            {
                bool ours = File.Exists(Path.Combine(stage, ".ghostpackage"));
                bool empty = !Directory.EnumerateFileSystemEntries(stage).Any();
                if (!ours && !empty)
                    throw new InvalidOperationException($"\"{stage}\" already exists and wasn't made by Package Project.\n" +
                        "Choose another output folder, or empty that one.");
                if (ours)
                {
                    log("Removing the previous package...");
                    try { Directory.Delete(stage, true); }
                    catch (IOException) { throw new InvalidOperationException("The old package is in use. Close the packaged game and try again."); }
                }
            }
            Directory.CreateDirectory(stage);

            // ---- the binaries ----
            if (o.UseExisting)
            {
                string src = Path.GetFullPath(o.ExistingFolder).TrimEnd('\\');
                if (!File.Exists(Path.Combine(src, "GhostShell.exe")) || !File.Exists(Path.Combine(src, "GhostX.dll")))
                    throw new InvalidOperationException("That folder must contain GhostShell.exe and GhostX.dll (a Release | x64 build).");
                if ((stage + "\\").StartsWith(src + "\\", StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("The output folder can't be inside the build folder you're copying.");
                log($"Copying {src} ...");
                log($"{CopyTree(src, stage)} files copied.");
            }
            else
            {
                if (slnDir == null)
                    throw new InvalidOperationException("The Visual Studio solution wasn't found.\nUse Settings > Set Visual Studio Solution... first.");
                string msbuild = FindMsBuild();
                log("MSBuild: " + msbuild);

                string x = FindFile(slnDir, "GhostX.vcxproj") ?? throw new InvalidOperationException("GhostX.vcxproj was not found under " + slnDir);
                string g = FindFile(slnDir, "GhostGame.vcxproj") ?? throw new InvalidOperationException("GhostGame.vcxproj was not found under " + slnDir);
                string s = FindFile(slnDir, "GhostShell.csproj") ?? throw new InvalidOperationException("GhostShell.csproj was not found under " + slnDir);

                string outDir = stage + "\\";
                Build(msbuild, x, "GhostX", outDir, slnDir, false, log);
                Build(msbuild, g, "GhostGame", outDir, slnDir, false, log);
                Build(msbuild, s, "GhostShell", outDir, slnDir, o.SelfContained, log);
            }

            // ---- checks ----
            string exe = Path.Combine(stage, "GhostShell.exe");
            string xdll = Path.Combine(stage, "GhostX.dll");
            string gdll = Path.Combine(stage, "GhostGame.dll");
            if (!File.Exists(exe) || !File.Exists(xdll))
                throw new InvalidOperationException("The package folder is missing GhostShell.exe or GhostX.dll.");
            if (IsDebugBinary(xdll))
                throw new InvalidOperationException("GhostX.dll is a Debug build. Package needs Release | x64.");
            if (!File.Exists(gdll)) warn.Add("GhostGame.dll is missing: the game will run with no game code.");
            else if (IsDebugBinary(gdll)) throw new InvalidOperationException("GhostGame.dll is a Debug build. Package needs Release | x64.");

            // ---- shaders ----
            string shaderDst = Path.Combine(stage, "shaders");
            if (!File.Exists(Path.Combine(shaderDst, "GhostVS.hlsl")))
            {
                string? vs = slnDir == null ? null : FindFile(slnDir, "GhostVS.hlsl");
                if (vs == null) throw new InvalidOperationException("The shaders were not found. They must be in a \"shaders\" folder next to GhostX.dll.");
                Directory.CreateDirectory(shaderDst);
                foreach (string f in Directory.GetFiles(Path.GetDirectoryName(vs)!, "*.hlsl"))
                    File.Copy(f, Path.Combine(shaderDst, Path.GetFileName(f)), true);
                log("Shaders copied from " + Path.GetDirectoryName(vs));
            }

            // ---- tidy: debug symbols and build leftovers ----
            foreach (string f in Directory.EnumerateFiles(stage, "*", SearchOption.AllDirectories).ToList())
                if (Array.IndexOf(JunkExt, Path.GetExtension(f).ToLowerInvariant()) >= 0)
                    try { File.Delete(f); } catch { }

            // ---- C++ runtime next to the exe, so the game needs no redistributable install ----
            int rt = 0;
            foreach (string d in RuntimeDlls)
            {
                string from = Path.Combine(Environment.SystemDirectory, d), to = Path.Combine(stage, d);
                if (File.Exists(from) && !File.Exists(to)) { File.Copy(from, to); rt++; }
            }
            log($"{rt} C++ runtime files copied.");

            // ---- the game's own exe ----
            string newExe = Path.Combine(stage, name + ".exe");
            if (!name.Equals("GhostShell", StringComparison.OrdinalIgnoreCase))
            {
                if (File.Exists(newExe)) File.Delete(newExe);
                File.Move(exe, newExe);
            }

            // ---- the project data ----
            string data = Path.Combine(stage, "Data");
            Directory.CreateDirectory(Path.Combine(data, "Content"));
            Directory.CreateDirectory(Path.Combine(data, "Scenes"));
            File.Copy(manifest, Path.Combine(data, Path.GetFileName(manifest)), true);
            int content = CopyTree(Path.Combine(root, "Content"), Path.Combine(data, "Content"));
            int scenes = CopyTree(Path.Combine(root, "Scenes"), Path.Combine(data, "Scenes"));
            log($"Project data: {scenes} scene files, {content} content files.");

            // ---- full paths outside the project would break on another PC ----
            var abs = new Regex("\"[A-Za-z]:[\\\\/]");
            foreach (string f in Directory.EnumerateFiles(data, "*", SearchOption.AllDirectories))
            {
                string ext = Path.GetExtension(f).ToLowerInvariant();
                if (ext != ".ghostscene" && ext != ".ghostanim" && ext != ".ghostui") continue;
                foreach (string line in File.ReadLines(f))
                {
                    if (line.Contains("meshSource")) continue;            // the original FBX path is only informational
                    if (abs.IsMatch(line))
                    {
                        warn.Add($"{Path.GetRelativePath(data, f)} refers to a file outside the project by its full path " +
                                 "(import that file through the Content Browser so it is copied into Content/).");
                        break;
                    }
                }
            }

            File.WriteAllText(Path.Combine(stage, ".ghostpackage"), "Made by GhostX Package Project");

            // ---- optional zip ----
            string zipNote = "";
            if (o.Zip)
            {
                string zip = stage + ".zip";
                if (File.Exists(zip)) File.Delete(zip);
                log("Creating " + zip + " ...");
                ZipFile.CreateFromDirectory(stage, zip, CompressionLevel.Optimal, true);
                zipNote = "\nZip: " + zip;
            }

            var all = Directory.EnumerateFiles(stage, "*", SearchOption.AllDirectories).ToList();
            double mb = all.Sum(f => new FileInfo(f).Length) / (1024.0 * 1024.0);
            summary = $"Packaged \"{name}\": {all.Count} files, {mb:0.#} MB\n{stage}\nRun: {name}.exe{zipNote}";
            if (warn.Count > 0) summary += "\n\nWarnings:\n- " + string.Join("\n- ", warn);
            return stage;
        }
    }

    // ============================================================
    //  The dialog
    // ============================================================
    internal sealed class PackageDialog : Form
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);

        private readonly TextBox _out = new(), _existing = new(), _log = new();
        private readonly ComboBox _source = new();
        private readonly CheckBox _self = new(), _zip = new();
        private readonly Button _go, _open, _run, _browseOut, _browseExisting, _close;
        private bool _busy;
        private string _stage = "";

        public PackageDialog()
        {
            Text = "Package Project";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false; MinimizeBox = false; ShowInTaskbar = false;
            ClientSize = new Size(740, 600);
            BackColor = Bg; ForeColor = Fg;

            Controls.Add(Lbl($"Project: {GhostProject.DisplayName}     Start scene: {GhostProject.Current!.StartScene}", 16, 14));

            Controls.Add(Lbl("Output folder (the game goes in a subfolder named after the project)", 16, 46));
            Style(_out, 16, 68, 600);
            _out.Text = Path.Combine(GhostProject.Root!, "Build");
            _browseOut = Btn("Browse...", 626, 66, 98, () =>
            {
                using var fb = new FolderBrowserDialog { SelectedPath = _out.Text, Description = "Where should the packaged game go?" };
                if (fb.ShowDialog(this) == DialogResult.OK) _out.Text = fb.SelectedPath;
            });

            Controls.Add(Lbl("Binaries", 16, 108));
            _source.Location = new Point(16, 130); _source.Width = 600;
            _source.DropDownStyle = ComboBoxStyle.DropDownList; _source.FlatStyle = FlatStyle.Flat;
            _source.BackColor = Bg2; _source.ForeColor = Fg;
            _source.Items.AddRange(new object[]
                { "Build Release x64 with MSBuild (recommended)", "Use an existing Release x64 folder (no build)" });
            _source.SelectedIndex = 0;
            Controls.Add(_source);

            Style(_existing, 16, 162, 600);
            _existing.Enabled = false;
            _browseExisting = Btn("Browse...", 626, 160, 98, () =>
            {
                using var fb = new FolderBrowserDialog { Description = "Choose the folder that holds GhostShell.exe and GhostX.dll (Release | x64)" };
                if (fb.ShowDialog(this) == DialogResult.OK) _existing.Text = fb.SelectedPath;
            });
            _browseExisting.Enabled = false;
            _source.SelectedIndexChanged += (s, e) =>
            {
                bool ex = _source.SelectedIndex == 1;
                _existing.Enabled = ex; _browseExisting.Enabled = ex; _self.Enabled = !ex;
            };

            _self.Text = "Include the .NET runtime (self-contained: about 150 MB bigger, but needs no .NET install)";
            _self.Location = new Point(16, 198); _self.AutoSize = true; _self.ForeColor = Fg;
            Controls.Add(_self);
            _zip.Text = "Also make a .zip";
            _zip.Location = new Point(16, 224); _zip.AutoSize = true; _zip.ForeColor = Fg;
            Controls.Add(_zip);

            _go = Btn("Package", 16, 256, 120, () => { _ = StartAsync(); });
            _open = Btn("Open Folder", 146, 256, 120, () => { if (_stage.Length > 0) Process.Start("explorer.exe", _stage); });
            _run = Btn("Run Game", 276, 256, 120, RunGame);
            _close = Btn("Close", 604, 256, 120, Close);
            _open.Enabled = false; _run.Enabled = false;

            _log.Location = new Point(16, 296); _log.Size = new Size(708, 288);
            _log.Multiline = true; _log.ReadOnly = true; _log.ScrollBars = ScrollBars.Vertical;
            _log.BackColor = Bg2; _log.ForeColor = Fg; _log.BorderStyle = BorderStyle.FixedSingle;
            _log.Font = new Font("Consolas", 9f);
            Controls.Add(_log);

            FormClosing += (s, e) => { if (_busy) e.Cancel = true; };
        }

        private Label Lbl(string t, int x, int y) => new Label { Text = t, Location = new Point(x, y), AutoSize = true, ForeColor = Fg };

        private void Style(TextBox t, int x, int y, int w)
        {
            t.Location = new Point(x, y); t.Width = w;
            t.BackColor = Bg2; t.ForeColor = Fg; t.BorderStyle = BorderStyle.FixedSingle;
            Controls.Add(t);
        }

        private Button Btn(string text, int x, int y, int w, Action click)
        {
            var b = new Button { Text = text, Location = new Point(x, y), Size = new Size(w, 30), FlatStyle = FlatStyle.Flat, BackColor = Bg2, ForeColor = Fg };
            b.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
            b.Click += (s, e) => click();
            Controls.Add(b);
            return b;
        }

        private void Log(string line)
        {
            if (IsDisposed) return;
            if (InvokeRequired) { try { BeginInvoke(new Action<string>(Log), line); } catch { } return; }
            _log.AppendText(line + "\r\n");
        }

        private void SetBusy(bool busy)
        {
            _busy = busy;
            _go.Enabled = !busy; _out.Enabled = !busy; _source.Enabled = !busy; _browseOut.Enabled = !busy;
            _zip.Enabled = !busy; _close.Enabled = !busy;
            _self.Enabled = !busy && _source.SelectedIndex == 0;
            _existing.Enabled = !busy && _source.SelectedIndex == 1;
            _browseExisting.Enabled = _existing.Enabled;
            if (busy) { _open.Enabled = false; _run.Enabled = false; }
            Cursor = busy ? Cursors.WaitCursor : Cursors.Default;
        }

        private async Task StartAsync()
        {
            if (_out.Text.Trim().Length == 0 || !Path.IsPathRooted(_out.Text.Trim()))
            { MessageBox.Show(this, "Choose a full output folder path.", "Package Project", MessageBoxButtons.OK, MessageBoxIcon.Information); return; }
            bool useExisting = _source.SelectedIndex == 1;
            if (useExisting && !Directory.Exists(_existing.Text.Trim()))
            { MessageBox.Show(this, "Choose the Release build folder first.", "Package Project", MessageBoxButtons.OK, MessageBoxIcon.Information); return; }

            var opts = new PackageOptions
            {
                OutRoot = _out.Text.Trim(),
                UseExisting = useExisting,
                ExistingFolder = _existing.Text.Trim(),
                SelfContained = _self.Checked,
                Zip = _zip.Checked,
            };

            _log.Clear();
            SetBusy(true);
            string stage = "", summary = "", error = "";
            await Task.Run(() =>
            {
                try { stage = GhostPackager.Run(opts, Log, out summary); }
                catch (Exception ex) { error = ex.Message; }
            });
            SetBusy(false);

            if (error.Length > 0)
            {
                Log("");
                Log("FAILED: " + error);
                MessageBox.Show(this, error, "Packaging failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            _stage = stage;
            _open.Enabled = true; _run.Enabled = true;
            Log("");
            Log(summary);
            MessageBox.Show(this, summary, "Package finished", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private void RunGame()
        {
            string exe = Path.Combine(_stage, GhostPackager.SafeName(GhostProject.Current!.Name) + ".exe");
            try { Process.Start(new ProcessStartInfo(exe) { UseShellExecute = true, WorkingDirectory = _stage }); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "Run Game", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        }
    }
}