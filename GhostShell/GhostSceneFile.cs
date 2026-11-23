// GhostSceneFile.cs — open / save / new scene, plus unsaved-changes tracking
using System;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace GhostShell
{
    internal static class GhostSceneFile
    {
        public const string Filter = "GhostX Scene (*.ghostscene)|*.ghostscene";

        public static string? CurrentPath { get; private set; }

        // Texture paths are saved relative to this folder. 4A-2 sets it to the project folder.
        // null = the folder of the scene file.
        public static string? ProjectRoot { get; set; }

        private static uint _savedHash;

        public static bool IsDirty => GhostEngine.GhostEngine_GetSceneHash() != _savedHash;
        public static string DisplayName => CurrentPath == null ? "Untitled" : Path.GetFileName(CurrentPath);

        public static void MarkClean() => _savedHash = GhostEngine.GhostEngine_GetSceneHash();

        private static string Msg(byte[] b)
        {
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        private static void ApplyRoot(string scenePath) =>
            GhostEngine.GhostEngine_SetProjectRoot(
                ProjectRoot ?? Path.GetDirectoryName(Path.GetFullPath(scenePath)) ?? "");

        public static bool Save(string path)
        {
            ApplyRoot(path);
            var buf = new byte[1024];
            if (GhostEngine.GhostEngine_SaveScene(path, buf, buf.Length) == 0)
            {
                MessageBox.Show(Msg(buf), "Save failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            CurrentPath = path;
            MarkClean();
            return true;
        }

        // Loads a scene and registers its textures. No message boxes, so it can run inside a loading bar.
        // 0 = failed (msg = reason), 1 = ok, 2 = ok with warnings (msg = warnings). Caller calls MarkClean().
        public static int OpenQuiet(string path, out string msg)
        {
            ApplyRoot(path);
            var buf = new byte[4096];
            int r = GhostEngine.GhostEngine_LoadScene(path, buf, buf.Length);
            msg = Msg(buf);
            if (r != 0)
            {
                GhostLoad.Stage("Building texture thumbnails...");
                GhostAssets.SyncFromEngine();
                CurrentPath = path;
            }
            return r;
        }

        public static void ShowOpenResult(int r, string msg)
        {
            if (r == 0) MessageBox.Show(msg, "Could not open scene", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            else if (r == 2) MessageBox.Show(msg, "Scene opened with warnings", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        public static bool Open(string path)
        {
            int r = 0; string msg = "";
            GhostLoad.Run(null, "Loading scene: " + Path.GetFileName(path), GhostLoad.CountScene(path), () =>
            {
                r = OpenQuiet(path, out string m);
                msg = m;
            });
            if (r != 0) MarkClean();              // after the bar's final engine tick, so terrain collider fitting isn't counted as an edit
            ShowOpenResult(r, msg);
            return r != 0;                        // on failure the current scene is untouched
        }

        public static void New()
        {
            GhostEngine.GhostEngine_ClearScene();
            CurrentPath = null;
            MarkClean();
        }

        public static bool SaveInteractive(IWin32Window owner) =>
            CurrentPath != null ? Save(CurrentPath) : SaveAs(owner);

        public static bool SaveAs(IWin32Window owner)
        {
            using var dlg = new SaveFileDialog
            {
                Title = "Save Scene As",
                Filter = Filter,
                DefaultExt = "ghostscene",
                AddExtension = true,
                FileName = CurrentPath != null ? Path.GetFileNameWithoutExtension(CurrentPath) : "Untitled",
                InitialDirectory = GhostProject.ScenesDir ?? "",
            };
            return dlg.ShowDialog(owner) == DialogResult.OK && Save(dlg.FileName);
        }

        public static void OpenInteractive(IWin32Window owner)
        {
            if (!ConfirmDiscard(owner)) return;
            using var dlg = new OpenFileDialog { Title = "Open Scene", Filter = Filter, InitialDirectory = GhostProject.ScenesDir ?? "" };
            if (dlg.ShowDialog(owner) == DialogResult.OK) Open(dlg.FileName);
        }

        // true = safe to continue (saved, discarded, or nothing to save)
        public static bool ConfirmDiscard(IWin32Window owner)
        {
            if (!IsDirty) return true;
            var r = MessageBox.Show(owner, $"Save changes to {DisplayName}?", "GhostX",
                MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
            if (r == DialogResult.Cancel) return false;
            return r == DialogResult.No || SaveInteractive(owner);
        }

        // Frees every file-backed texture, reloads the open scene (only what it uses gets loaded), then re-saves it
        public static bool CleanReload(IWin32Window owner)
        {
            if (GhostPlay.State != 0)
            { MessageBox.Show(owner, "Stop playing first.", "Clean Stale Textures", MessageBoxButtons.OK, MessageBoxIcon.Information); return false; }
            if (CurrentPath == null)
            { MessageBox.Show(owner, "Save the scene first.", "Clean Stale Textures", MessageBoxButtons.OK, MessageBoxIcon.Information); return false; }
            if (MessageBox.Show(owner,
                    "This frees every texture loaded from a file, reloads the scene so only the textures it really uses are loaded, and saves it again.\n\nUnsaved changes are saved first. Continue?",
                    "Clean Stale Textures", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return false;

            string path = CurrentPath;
            if (IsDirty && !Save(path)) return false;
            GhostEngine.GhostEngine_PurgeProjectAssets();
            GhostAssets.ResetProjectTextures();
            return Open(path) && Save(path);
        }

    }
}