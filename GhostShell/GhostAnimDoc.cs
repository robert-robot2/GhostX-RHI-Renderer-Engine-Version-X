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

        public int Kind = 0;          // 0 = sprite sheet, 1 = 3D model animation (FBX)
        public string Model = "";     // Kind 1: stored path of the FBX

        public override string ToString() => Kind == 1
            ? $"{Name}   (3D: {(Model.Length == 0 ? "no FBX yet" : Path.GetFileName(Model))}{(Loop ? "" : ", plays once")})"
            : $"{Name}   ({Frames.Count} frames, {Fps:0.#} fps{(Loop ? "" : ", plays once")})";
        public float Speed = 1f;
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
        public GraphDoc Graph = new();
        public AnimDoc() { Graph.EnsureEntry(); }
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
                        Kind = Str(jc, "kind", "sprite") == "skeletal" ? 1 : 0,
                        Model = Str(jc, "model", ""),
                        Speed = Math.Clamp(Num(jc, "speed", 1f), 0.05f, 8f),
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
            d.Graph = GraphDoc.From(o["graph"] as JsonObject);
            d.Graph.EnsureEntry();
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
                    ["kind"] = c.Kind == 1 ? "skeletal" : "sprite",
                    ["model"] = c.Model,
                    ["speed"] = Math.Round((double)c.Speed, 3),
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
            o["graph"] = Graph.ToJson();
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

    // Node numbers match GhostGraphNodeType in GhostAnim.h
    internal enum GNodeType { Entry = 0, State = 1, Any = 2, Blend = 3, Turn = 4, Lean = 5, Squash = 6, Speed = 7, Sound = 8, Rumble = 9 }

    internal sealed class GraphNodeDoc
    {
        public int Id; public GNodeType Type; public float X, Y;
        public string Name = "", Clip = "";
        public float[] P = new float[4];
        public int Curve = 3;
        public bool HasIn => Type is GNodeType.State or GNodeType.Blend or GNodeType.Squash or GNodeType.Speed or GNodeType.Sound or GNodeType.Rumble;
        public bool HasOut => Type is not (GNodeType.Turn or GNodeType.Lean);
    }
    internal sealed class GraphLinkDoc { public int From, To; }

    internal sealed class GraphDoc
    {
        public readonly List<GraphNodeDoc> Nodes = new();
        public readonly List<GraphLinkDoc> Links = new();
        public int NextId = 1;

        public GraphNodeDoc? Find(int id) => Nodes.FirstOrDefault(n => n.Id == id);

        public GraphNodeDoc Add(GNodeType t, float x, float y)
        {
            var n = new GraphNodeDoc { Id = NextId++, Type = t, X = x, Y = y };
            switch (t)
            {
                case GNodeType.Entry: n.Name = "Entry"; break;
                case GNodeType.State: n.Name = "State"; break;
                case GNodeType.Blend: n.P[0] = 0.15f; n.Curve = 3; break;
                case GNodeType.Squash: n.P[0] = 0.3f; n.P[1] = 0.25f; break;            // amount, seconds, mode (0 stretch, 1 squash)
                case GNodeType.Speed: n.P[0] = 0.5f; n.P[1] = 1f; n.P[2] = 0.3f; break;
                case GNodeType.Rumble: n.P[0] = 0.5f; n.P[1] = 0.2f; n.P[2] = 0.15f; break;
                case GNodeType.Turn: n.P[0] = 0.2f; break;
                case GNodeType.Lean: n.P[0] = 15f; n.P[1] = 0.3f; break;
            }
            Nodes.Add(n);
            return n;
        }

        public void EnsureEntry() { if (!Nodes.Any(n => n.Type == GNodeType.Entry)) Add(GNodeType.Entry, 0, 0); }

        public bool TryLink(GraphNodeDoc a, GraphNodeDoc b)
        {
            if (a == b || !a.HasOut || !b.HasIn) return false;
            if (Links.Any(l => l.From == a.Id && l.To == b.Id)) return false;
            Links.Add(new GraphLinkDoc { From = a.Id, To = b.Id });
            return true;
        }

        public void Remove(GraphNodeDoc n)
        {
            if (n.Type == GNodeType.Entry) return;                      // there is always one entry
            Links.RemoveAll(l => l.From == n.Id || l.To == n.Id);
            Nodes.Remove(n);
        }

        public IEnumerable<string> StateNames() => Nodes.Where(n => n.Type == GNodeType.State).Select(n => n.Name);

        public JsonObject ToJson()
        {
            var nodes = new JsonArray();
            foreach (var n in Nodes)
                nodes.Add(new JsonObject
                {
                    ["id"] = n.Id,
                    ["type"] = (int)n.Type,
                    ["x"] = Math.Round((double)n.X, 1),
                    ["y"] = Math.Round((double)n.Y, 1),
                    ["name"] = n.Name,
                    ["clip"] = n.Clip,
                    ["curve"] = n.Curve,
                    ["p"] = new JsonArray(n.P.Select(v => (JsonNode)Math.Round((double)v, 4)).ToArray()),
                });
            var links = new JsonArray();
            foreach (var l in Links) links.Add(new JsonObject { ["from"] = l.From, ["to"] = l.To });
            return new JsonObject { ["nodes"] = nodes, ["links"] = links };
        }

        public static GraphDoc From(JsonObject? o)
        {
            var g = new GraphDoc();
            if (o == null) return g;
            static double D(JsonNode? n, double d) => n is JsonValue v && v.TryGetValue<double>(out var x) ? x : d;
            static string S(JsonNode? n) => n is JsonValue v && v.TryGetValue<string>(out var s) && s != null ? s : "";
            if (o["nodes"] is JsonArray na)
                foreach (JsonNode? jn in na)
                {
                    if (jn is not JsonObject j) continue;
                    int id = (int)D(j["id"], 0);
                    if (id <= 0 || g.Nodes.Count >= 128) continue;
                    var n = new GraphNodeDoc
                    {
                        Id = id,
                        Type = (GNodeType)Math.Clamp((int)D(j["type"], 1), 0, 9),
                        X = (float)D(j["x"], 0),
                        Y = (float)D(j["y"], 0),
                        Name = S(j["name"]),
                        Clip = S(j["clip"]),
                        Curve = Math.Clamp((int)D(j["curve"], 3), 0, 3),
                    };
                    if (j["p"] is JsonArray pa) for (int k = 0; k < 4 && k < pa.Count; k++) n.P[k] = (float)D(pa[k], 0);
                    g.Nodes.Add(n);
                    g.NextId = Math.Max(g.NextId, id + 1);
                }
            if (o["links"] is JsonArray la)
                foreach (JsonNode? jl in la)
                    if (jl is JsonObject j && g.Find((int)D(j["from"], 0)) != null && g.Find((int)D(j["to"], 0)) != null)
                        g.Links.Add(new GraphLinkDoc { From = (int)D(j["from"], 0), To = (int)D(j["to"], 0) });
            return g;
        }
    }
}