// GhostAnimDoc.cs — Animation Editor document: per-clip texture + grid, clips, load/save
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace GhostShell
{
    internal sealed class AnimClipDoc
    {
        public string Name = "Clip";
        public List<int> Frames = new();
        public float Fps = 8f;
        public bool Loop = true;

        // Per-clip texture and grid (moved down from AnimDoc)
        public string Texture = "";   // stored path: relative to project or absolute
        public int Source = 1;    // 0 = single image, 1 = sprite sheet
        public int Cols = 1;
        public int Rows = 1;

        public override string ToString() =>
            $"{Name}   ({Frames.Count} frames, {Fps:0.#} fps{(Loop ? "" : ", plays once")})";
    }

    internal sealed class AnimDoc
    {
        public const int MaxClips = 64, MaxFrames = 256, MaxGrid = 64;

        public static readonly (string Name, float Fps, bool Loop)[] Standard =
        {
            ("Idle",  4f,  true),  ("Walk", 10f,  true), ("Run",  14f, true),
            ("Jump",  1f,  false), ("Fall",  1f,  false), ("Hurt",  1f, false),
            ("Die",   6f,  false), ("Break", 12f, false), ("Used",  1f, false),
        };

        public string Name = "NewAnimation";
        public readonly List<AnimClipDoc> Clips = new();
        public readonly Dictionary<string, string> States =
            new(StringComparer.OrdinalIgnoreCase);

        // FrameCount is now per-clip; this helper is kept for the canvas/preview
        // and returns the count for the currently selected clip (caller passes the clip).
        public static int FrameCount(AnimClipDoc c) => c.Cols * c.Rows;

        public void NormalizeClipGrid(AnimClipDoc c)
        {
            if (c.Source == 0) { c.Cols = 1; c.Rows = 1; }
            c.Cols = Math.Clamp(c.Cols, 1, MaxGrid);
            c.Rows = Math.Clamp(c.Rows, 1, MaxGrid);
        }

        public string UniqueClipName(AnimClipDoc? self, string want)
        {
            want = string.IsNullOrWhiteSpace(want) ? "Clip" : want.Trim();
            string n = want;
            int i = 2;
            while (Clips.Any(c => c != self &&
                   string.Equals(c.Name, n, StringComparison.OrdinalIgnoreCase)))
                n = want + i++;
            return n;
        }

        // ---------------- helpers ----------------
        private static string Str(JsonObject o, string k, string d) =>
            o[k] is JsonValue v && v.TryGetValue<string>(out var s) && s != null ? s : d;
        private static float Num(JsonObject o, string k, float d) =>
            o[k] is JsonValue v && v.TryGetValue<double>(out var x) ? (float)x : d;
        private static int Int(JsonObject o, string k, int d) =>
            o[k] is JsonValue v && v.TryGetValue<double>(out var x) ? (int)x : d;
        private static bool Bool(JsonObject o, string k, bool d) =>
            o[k] is JsonValue v && v.TryGetValue<bool>(out var b) ? b : d;
        private static bool TryInt(JsonNode? n, out int value)
        {
            value = 0;
            if (n is JsonValue v && v.TryGetValue<double>(out var x)) { value = (int)x; return true; }
            return false;
        }

        // ---------------- load ----------------
        public static AnimDoc Load(string path) => Parse(File.ReadAllText(path));

        public static AnimDoc Parse(string text)
        {
            JsonNode? root = JsonNode.Parse(text, null,
                new JsonDocumentOptions
                {
                    AllowTrailingCommas = true,
                    CommentHandling = JsonCommentHandling.Skip
                });
            if (root is not JsonObject o || Str(o, "format", "") != "ghostanim")
                throw new InvalidDataException(
                    "This is not a GhostX animation file (\"format\" must be \"ghostanim\").");
            if (Int(o, "version", 0) > 1)
                throw new InvalidDataException(
                    "This animation file was saved by a newer version of GhostX.");

            var d = new AnimDoc { Name = Str(o, "name", "NewAnimation") };

            if (o["clips"] is JsonArray arr)
            {
                foreach (JsonNode? n in arr)
                {
                    if (n is not JsonObject jc || d.Clips.Count >= MaxClips) continue;
                    string name = Str(jc, "name", "");
                    if (name.Length == 0) continue;

                    var c = new AnimClipDoc
                    {
                        Name = name,
                        Fps = Math.Clamp(Num(jc, "fps", 8f), 0.1f, 120f),
                        Loop = Bool(jc, "loop", true),
                        Texture = Str(jc, "texture", ""),
                        Source = Str(jc, "source", "sheet") == "single" ? 0 : 1,
                        Cols = Math.Clamp(Int(jc, "columns", 1), 1, MaxGrid),
                        Rows = Math.Clamp(Int(jc, "rows", 1), 1, MaxGrid),
                    };
                    if (c.Source == 0) { c.Cols = 1; c.Rows = 1; }

                    if (jc["frames"] is JsonArray fa)
                    {
                        foreach (JsonNode? f in fa)
                            if (TryInt(f, out int fi) && c.Frames.Count < MaxFrames)
                                c.Frames.Add(fi);
                    }
                    else if (jc["range"] is JsonArray ra && ra.Count >= 2 &&
                             TryInt(ra[0], out int from) && TryInt(ra[1], out int to))
                    {
                        int step = from <= to ? 1 : -1;
                        for (int f = from; c.Frames.Count < MaxFrames; f += step)
                        {
                            c.Frames.Add(f);
                            if (f == to) break;
                        }
                    }
                    d.Clips.Add(c);
                }
            }

            if (o["states"] is JsonObject so)
                foreach (var kv in so)
                    if (kv.Value is JsonValue sv && sv.TryGetValue<string>(out var sc) &&
                        !string.IsNullOrWhiteSpace(sc))
                        d.States[kv.Key] = sc;

            return d;
        }

        // ---------------- save ----------------
        public string ToJson()
        {
            var clips = new JsonArray();
            foreach (AnimClipDoc c in Clips)
            {
                var frames = new JsonArray();
                foreach (int f in c.Frames) frames.Add(f);
                clips.Add(new JsonObject
                {
                    ["name"] = c.Name,
                    ["texture"] = c.Texture,
                    ["source"] = c.Source == 0 ? "single" : "sheet",
                    ["columns"] = c.Cols,
                    ["rows"] = c.Rows,
                    ["frames"] = frames,
                    ["fps"] = Math.Round((double)c.Fps, 3),
                    ["loop"] = c.Loop,
                });
            }
            var o = new JsonObject
            {
                ["format"] = "ghostanim",
                ["version"] = 1,
                ["name"] = Name,
                ["clips"] = clips,
            };
            var st = new JsonObject();
            foreach (var kv in States) st[kv.Key] = kv.Value;
            o["states"] = st;
            return o.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
        }

        public void SaveTo(string path)
        {
            string? dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            string tmp = path + ".tmp";
            File.WriteAllText(tmp, ToJson());
            File.Move(tmp, path, true);
        }
    }
}