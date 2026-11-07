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
        public string AudioMixer { get; set; } = "";   // "vol0,vol1,vol2,vol3,mute0,mute1,mute2,mute3"
        public int UiPreview { get; set; } = 1;        // UI Designer preview size (index into its list)
        public string Video { get; set; } = "";     // "resW,resH,displayMode,scaleFilter,fit" (editor)

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

    internal static class GhostVideo
    {
        private static string PlayerFile => Path.Combine(AppContext.BaseDirectory, "video.cfg");
        private static string FallbackFile => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "GhostX", "player-video.cfg");

        public static string Format(GhostVideoSettings v) =>
            string.Join(",", v.resW, v.resH, v.displayMode, v.scaleFilter, v.fit);

        public static bool TryParse(string? text, out GhostVideoSettings v)
        {
            v = default;
            string[] p = (text ?? "").Trim().Split(',');
            if (p.Length != 5) return false;
            var n = new int[5];
            for (int i = 0; i < 5; i++)
                if (!int.TryParse(p[i].Trim(), System.Globalization.NumberStyles.Integer,
                        System.Globalization.CultureInfo.InvariantCulture, out n[i])) return false;
            v = new GhostVideoSettings { resW = n[0], resH = n[1], displayMode = n[2], scaleFilter = n[3], fit = n[4] };
            return true;
        }

        public static bool Same(GhostVideoSettings a, GhostVideoSettings b) =>
            a.resW == b.resW && a.resH == b.resH && a.displayMode == b.displayMode &&
            a.scaleFilter == b.scaleFilter && a.fit == b.fit;

        private static bool LoadPlayer(out GhostVideoSettings v)
        {
            v = default;
            try
            {
                if (File.Exists(PlayerFile)) return TryParse(File.ReadAllText(PlayerFile), out v);
                if (File.Exists(FallbackFile)) return TryParse(File.ReadAllText(FallbackFile), out v);
            }
            catch { }
            return false;
        }

        public static void Save(GhostVideoSettings v)
        {
            if (!GhostPlayer.IsPlayer)
            {
                GhostSettings.Data.Video = Format(v);
                GhostSettings.Save();
            }
            else if (GhostPlayer.SaveVideo)                          // a standalone launched by the editor with --video doesn't overwrite the game's file
            {
                try { File.WriteAllText(PlayerFile, Format(v)); return; } catch { }
                try { Directory.CreateDirectory(Path.GetDirectoryName(FallbackFile)!); File.WriteAllText(FallbackFile, Format(v)); } catch { }
            }
        }

        // Once the engine is running, before the player launches its scene
        public static void ApplyStartup()
        {
            GhostVideoSettings v;
            bool ok;
            if (GhostPlayer.IsPlayer)
                ok = GhostPlayer.VideoOverride != null ? TryParse(GhostPlayer.VideoOverride, out v) : LoadPlayer(out v);
            else
            {
                ok = TryParse(GhostSettings.Data.Video, out v);
                v.displayMode = 0;                                   // the editor always starts windowed
            }
            if (ok) GhostEngine.GhostEngine_SetVideoSettings(ref v);
        }
    }

}