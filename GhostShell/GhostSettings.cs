// GhostSettings.cs — editor settings saved in %LocalAppData%\GhostX\settings.json
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace GhostShell
{
    internal sealed class GhostSettingsData
    {
        public List<string> RecentProjects { get; set; } = new();
        public string SolutionPath { get; set; } = "";
        public string LastProjectFolder { get; set; } = "";
        public string ViewSettings { get; set; } = "";
        public string GizmoSettings { get; set; } = "";
        public int MsaaSamples { get; set; } = 1;      // 1 = off, 2, 4 or 8
        public int TaaEnabled { get; set; } = 0;
        public float TaaJitterSpeed { get; set; } = 1.0f;     // 1 = normal
        public float PadDeadzone { get; set; } = 0.22f;
        public bool PadRumble { get; set; } = true;
        public int PadSlot { get; set; } = -1;        // -1 = auto, 0..3 = that pad

    }

    internal static class GhostSettings
    {
        // static fields initialize in order: the path must come before Data
        private static readonly string FilePath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "GhostX", "settings.json");

        public static GhostSettingsData Data { get; } = Load();

        private static GhostSettingsData Load()
        {
            try
            {
                if (File.Exists(FilePath))
                    return JsonSerializer.Deserialize<GhostSettingsData>(File.ReadAllText(FilePath)) ?? new();
            }
            catch { /* damaged settings file: start fresh */ }
            return new();
        }

        public static void Save()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
                File.WriteAllText(FilePath, JsonSerializer.Serialize(Data, new JsonSerializerOptions { WriteIndented = true }));
            }
            catch { }
        }

        public static void AddRecent(string path)
        {
            Data.RecentProjects.RemoveAll(p => string.Equals(p, path, StringComparison.OrdinalIgnoreCase));
            Data.RecentProjects.Insert(0, path);
            if (Data.RecentProjects.Count > 8) Data.RecentProjects.RemoveRange(8, Data.RecentProjects.Count - 8);
            Save();
        }

        public static void RemoveRecent(string path)
        {
            Data.RecentProjects.RemoveAll(p => string.Equals(p, path, StringComparison.OrdinalIgnoreCase));
            Save();
        }
    }
}