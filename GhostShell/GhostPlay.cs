// GhostPlay.cs — Play / Pause / Stop, the standalone launcher, and the --play Player mode
using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

namespace GhostShell
{
    internal static class GhostPlay
    {
        public static int State => GhostEngine.GhostEngine_GetPlayState();     // 0 editing, 1 playing, 2 paused

        public static void Play()
        {
            int st = State;
            if (st == 0) GhostEngine.GhostEngine_Play();
            else if (st == 2) GhostEngine.GhostEngine_SetPaused(0);            // Play while paused resumes
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
            GhostEngine.GhostEngine_Play();
            return true;
        }
    }
}