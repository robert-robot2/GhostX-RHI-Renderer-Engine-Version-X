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

        public static bool Open(string path)
        {
            ApplyRoot(path);
            var buf = new byte[4096];
            int r = GhostEngine.GhostEngine_LoadScene(path, buf, buf.Length);
            if (r == 0)
            {
                MessageBox.Show(Msg(buf), "Could not open scene", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;      // the current scene is untouched
            }
            GhostAssets.SyncFromEngine();
            CurrentPath = path;
            MarkClean();
            if (r == 2)
                MessageBox.Show(Msg(buf), "Scene opened with warnings", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return true;
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
    }
}