// GhostProject.cs — the .ghostproject manifest and the open project's folders
using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace GhostShell
{
    internal sealed class ProjectManifest
    {
        [JsonPropertyName("format")] public string Format { get; set; } = "ghostproject";
        [JsonPropertyName("version")] public int Version { get; set; } = 1;
        [JsonPropertyName("name")] public string Name { get; set; } = "Untitled Project";
        [JsonPropertyName("startScene")] public string StartScene { get; set; } = "";
        [JsonPropertyName("gameModule")] public string GameModule { get; set; } = "Binaries/GhostGame.dll";
    }

    internal static class GhostProject
    {
        public const string Extension = ".ghostproject";
        public const string Filter = "GhostX Project (*.ghostproject)|*.ghostproject";
        public const string StartSceneFile = "Scenes/Main.ghostscene";

        public static event Action? Changed;

        public static ProjectManifest? Current { get; private set; }
        public static string? ManifestPath { get; private set; }

        public static bool IsOpen => Current != null;
        public static string DisplayName => Current?.Name ?? "";
        public static string? Root => ManifestPath == null ? null : Path.GetDirectoryName(ManifestPath);
        public static string? ContentDir => Root == null ? null : Path.Combine(Root, "Content");
        public static string? ScenesDir => Root == null ? null : Path.Combine(Root, "Scenes");

        private static readonly JsonSerializerOptions Pretty = new() { WriteIndented = true };

        public static bool ValidateNew(string name, string parent, out string error)
        {
            error = "";
            if (name.Length == 0) { error = "Enter a project name."; return false; }
            if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
            { error = "The project name contains characters that can't be used in a folder name."; return false; }
            if (parent.Length == 0 || !Path.IsPathRooted(parent))
            { error = "Choose a full folder path for the location."; return false; }

            string dir = Path.Combine(parent, name);
            if (Directory.Exists(dir) && Directory.EnumerateFileSystemEntries(dir).Any())
            { error = $"\"{dir}\" already exists and is not empty."; return false; }
            return true;
        }

        // Creates the folders and the manifest. Returns the manifest path. Throws on I/O errors.
        public static string Create(string name, string parent)
        {
            string dir = Path.Combine(parent, name);
            Directory.CreateDirectory(Path.Combine(dir, "Content"));
            Directory.CreateDirectory(Path.Combine(dir, "Scenes"));
            Directory.CreateDirectory(Path.Combine(dir, "Binaries"));

            string file = Path.Combine(dir, name + Extension);
            var m = new ProjectManifest { Name = name, StartScene = StartSceneFile };
            File.WriteAllText(file, JsonSerializer.Serialize(m, Pretty));
            return file;
        }

        // Makes the project current. Loading its scene is the caller's job (GhostProjectFlow).
        public static bool Open(string manifestPath, out string error, bool remember = true)
        {
            error = "";
            try
            {
                var m = JsonSerializer.Deserialize<ProjectManifest>(File.ReadAllText(manifestPath));
                if (m == null || m.Format != "ghostproject") { error = "This is not a GhostX project file."; return false; }
                if (m.Version > 1) { error = "This project was saved by a newer version of GhostX."; return false; }

                Current = m;
                ManifestPath = Path.GetFullPath(manifestPath);
                Directory.CreateDirectory(ContentDir!);
                Directory.CreateDirectory(ScenesDir!);

                GhostSceneFile.ProjectRoot = Root;           // scene files store texture paths relative to this
                GhostEngine.GhostEngine_SetGameModulePath(Path.GetFullPath(Path.Combine(Root!, m.GameModule)));
                if (remember) GhostSettings.AddRecent(ManifestPath);
                Changed?.Invoke();
                return true;
            }
            catch (Exception ex) { error = ex.Message; return false; }
        }

        // Imported files live in Content/: copies the file in (unless it's already there)
        // and returns the path to use. With no project open the file is used where it is.
        public static string EnsureInContent(string src, string? targetFolder)
        {
            string? content = ContentDir;
            if (content == null) return src;

            string full = Path.GetFullPath(src);
            string prefix = Path.TrimEndingDirectorySeparator(Path.GetFullPath(content)) + Path.DirectorySeparatorChar;
            if (full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return full;

            string dir = (targetFolder != null && Directory.Exists(targetFolder)) ? targetFolder : content;
            string name = Path.GetFileNameWithoutExtension(full), ext = Path.GetExtension(full);
            string dest = Path.Combine(dir, name + ext);
            for (int i = 2; File.Exists(dest); i++) dest = Path.Combine(dir, $"{name} ({i}){ext}");
            File.Copy(full, dest);
            return dest;
        }
    }
}