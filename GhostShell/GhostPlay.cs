// GhostPlay.cs — Play / Pause / Stop, the standalone launcher, and the --play Player mode
using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;
using System.Text;
using System.Linq;


namespace GhostShell
{
    internal static class GhostPlay
    {
        public static int State => GhostEngine.GhostEngine_GetPlayState();     // 0 editing, 1 playing, 2 paused

        public static bool HasModule { get; set; }

        public static bool TakeQuit() => GhostEngine.GhostEngine_TakeQuitRequest() != 0;

        public static void Play()
        {
            int st = State;
            if (st == 0)
            {
                int r = GhostEngine.GhostEngine_Play();
                if (r == 0)
                {
                    MessageBox.Show(PlayMessage(), "Game module", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                HasModule = (r == 1);
            }
            else if (st == 2) GhostEngine.GhostEngine_SetPaused(0);            // Play while paused resumes
        }

        private static string Text(Func<byte[], int> read)
        {
            var b = new byte[2048];
            if (read(b) == 0) return "";
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        public static string PlayMessage() => Text(b => GhostEngine.GhostEngine_GetPlayMessage(b, b.Length));
        public static string HudText() => Text(b => GhostEngine.GhostEngine_GetHudText(b, b.Length));
        public static string TakeFault() => Text(b => GhostEngine.GhostEngine_TakePlayFault(b, b.Length));

        public static void ShowModuleInfo(IWin32Window owner)
        {
            string m = PlayMessage();
            MessageBox.Show(owner, m.Length > 0 ? m : "Press Play first. The message shows where the game module was (or wasn't) found.",
                "Game module", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        public static void TogglePause()
        {
            int st = State;
            if (st == 1) GhostEngine.GhostEngine_SetPaused(1);
            else if (st == 2) GhostEngine.GhostEngine_SetPaused(0);
        }

        public static void Stop() => GhostEngine.GhostEngine_Stop();

        // Starts a second GhostShell in Player mode on this project's start scene (the saved version on disk)
        public static void LaunchStandalone(IWin32Window owner)
        {
            if (!GhostProject.IsOpen)
            {
                MessageBox.Show(owner, "Open or create a project first.", "Play Standalone",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            if (!GhostSceneFile.ConfirmDiscard(owner)) return;      // offers to save, so the player sees your latest edits

            string? exe = Environment.ProcessPath;
            if (exe == null) return;
            try
            {
                var psi = new ProcessStartInfo(exe) { UseShellExecute = false };
                psi.ArgumentList.Add("--play");
                psi.ArgumentList.Add(GhostProject.ManifestPath!);
                Process.Start(psi);
            }
            catch (Exception ex)
            {
                MessageBox.Show(owner, ex.Message, "Could not start the player", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }
    }

    // GhostShell.exe --play <project.ghostproject>: no menu, no panels, starts playing, Esc quits
    internal static class GhostPlayer
    {
        public static bool IsPlayer { get; private set; }
        public static string? ProjectPath { get; private set; }

        public static void ParseArgs(string[] args)
        {
            for (int i = 0; i + 1 < args.Length; i++)
                if (args[i].Equals("--play", StringComparison.OrdinalIgnoreCase))
                {
                    IsPlayer = true;
                    ProjectPath = Path.GetFullPath(args[i + 1]);
                    return;
                }

            // A packaged game: no arguments, and Data\<name>.ghostproject sits next to the exe
            try
            {
                string data = Path.Combine(AppContext.BaseDirectory, "Data");
                string? proj = Directory.Exists(data) ? Directory.EnumerateFiles(data, "*.ghostproject").FirstOrDefault() : null;
                if (proj != null) { IsPlayer = true; ProjectPath = Path.GetFullPath(proj); }
            }
            catch { }
        }

        // Opens the project, loads its start scene and starts playing. false = failed (the window is closing).
        public static bool Launch(Form form)
        {
            string path = ProjectPath ?? "";
            string? error = null;

            if (!File.Exists(path))
            {
                error = $"The project file was not found:\n{path}";
            }
            else
            {
                bool opened = GhostProject.Open(path, out string openError, false);
                if (!opened)
                {
                    error = openError;
                }
                else
                {
                    string start = GhostProject.Current!.StartScene;
                    string scene = start.Length > 0 ? Path.Combine(GhostProject.Root!, start) : "";
                    if (!File.Exists(scene)) error = $"The start scene was not found:\n{scene}";
                    else if (!GhostSceneFile.Open(scene)) { form.Close(); return false; }   // Open already explained why
                }
            }

            if (error != null)
            {
                MessageBox.Show(form, error, "GhostX Player", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                form.Close();
                return false;
            }

            form.Text = GhostProject.DisplayName;
            int r = GhostEngine.GhostEngine_Play();
            if (r == 0)
            {
                MessageBox.Show(form, GhostPlay.PlayMessage(), "GhostX Player", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                form.Close();
                return false;
            }
            GhostPlay.HasModule = (r == 1);
            return true;
        }
    }
}