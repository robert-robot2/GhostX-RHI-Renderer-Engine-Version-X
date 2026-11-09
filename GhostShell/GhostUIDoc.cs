// GhostUIDoc.cs — the UI Designer's document: widget tree, layout, and .ghostui load/save (C# side)
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace GhostShell
{
    // Mirror of GhostUIView / GhostUI_ComputeView in GhostUIScale.h
    internal readonly struct UIView
    {
        public readonly float Lw, Lh, Sx, Sy, Ox, Oy;
        public UIView(float lw, float lh, float sx, float sy, float ox, float oy) { Lw = lw; Lh = lh; Sx = sx; Sy = sy; Ox = ox; Oy = oy; }

        public static readonly string[] ModeNames =
            { "Fit Height (classic)", "Expand (recommended)", "Fit Width", "Letterbox", "Stretch" };

        public static UIView Compute(int mode, float refW, float screenW, float screenH)
        {
            const float H = 1080f;
            if (!(refW >= 1f)) refW = 1920f;
            if (!(screenW >= 1f)) screenW = 1f;
            if (!(screenH >= 1f)) screenH = 1f;
            float fw = screenW / refW, fh = screenH / H, fit = MathF.Min(fw, fh);
            switch (mode)
            {
                case 1: return new UIView(screenW / fit, screenH / fit, fit, fit, 0f, 0f);
                case 2: return new UIView(refW, screenH / fw, fw, fw, 0f, 0f);
                case 3: return new UIView(refW, H, fit, fit, (screenW - refW * fit) * 0.5f, (screenH - H * fit) * 0.5f);
                case 4: return new UIView(refW, H, fw, fh, 0f, 0f);
                default: return new UIView(screenW / fh, H, fh, fh, 0f, 0f);
            }
        }
    }

    internal sealed class DWidget
    {
        public string Name = "Widget";
        public string Type = "Canvas";
        public DWidget? Parent;
        public List<DWidget> Children = new();

        public float[] AnchorMin = { 0, 0 }, AnchorMax = { 0, 0 };
        public float[] Offset = { 0, 0 }, Size = { 100, 40 }, Pivot = { 0, 0 };
        public float[] Color = { 1, 1, 1, 1 }, TextColor = { 1, 1, 1, 1 }, BgColor = { 0, 0, 0, 0.5f };
        public float[]? HoverColor, PressColor;          // null = derived from Color (what the engine does)
        public bool Visible = true;
        public string Text = "";
        public float FontSize = 32f;
        public int Align;
        public bool Shadow = true;
        public string Texture = "";
        public string Font = "";          // "" = default UI font, otherwise a .ttf/.otf path relative to the project
        public float Value = 1f;

        // layout result (reference units)
        public float X, Y, W, H;
        public bool Eff = true;

        // The engine's loader defaults (not the designer's drop defaults)
        public static DWidget Blank(string type, DWidget? parent)
        {
            var w = new DWidget { Type = type, Name = type, Parent = parent };
            if (type == "Canvas") w.Color[3] = 0f;
            if (type == "Button") { w.Color[0] = 0.25f; w.Color[1] = 0.25f; w.Color[2] = 0.30f; }
            if (type == "ProgressBar") { w.Color[0] = 0.30f; w.Color[1] = 0.85f; w.Color[2] = 0.40f; }
            return w;
        }

        public DWidget CloneTree(DWidget? parent)
        {
            var c = (DWidget)MemberwiseClone();
            c.Parent = parent;
            c.AnchorMin = (float[])AnchorMin.Clone(); c.AnchorMax = (float[])AnchorMax.Clone();
            c.Offset = (float[])Offset.Clone(); c.Size = (float[])Size.Clone(); c.Pivot = (float[])Pivot.Clone();
            c.Color = (float[])Color.Clone(); c.TextColor = (float[])TextColor.Clone(); c.BgColor = (float[])BgColor.Clone();
            c.HoverColor = (float[]?)HoverColor?.Clone();
            c.PressColor = (float[]?)PressColor?.Clone();
            c.Children = new List<DWidget>();
            foreach (DWidget k in Children) c.Children.Add(k.CloneTree(c));
            return c;
        }
    }

    internal sealed class DesignDoc
    {
        public const int MaxWidgets = 512;
        public static readonly string[] Types = { "Canvas", "Image", "Text", "Button", "ProgressBar" };

        public static readonly (string Name, float[] Min, float[] Max)[] Presets =
        {
            ("TopLeft",       new[] { 0f, 0f },    new[] { 0f, 0f }),
            ("TopCenter",     new[] { 0.5f, 0f },  new[] { 0.5f, 0f }),
            ("TopRight",      new[] { 1f, 0f },    new[] { 1f, 0f }),
            ("CenterLeft",    new[] { 0f, 0.5f },  new[] { 0f, 0.5f }),
            ("Center",        new[] { 0.5f, 0.5f }, new[] { 0.5f, 0.5f }),
            ("CenterRight",   new[] { 1f, 0.5f },  new[] { 1f, 0.5f }),
            ("BottomLeft",    new[] { 0f, 1f },    new[] { 0f, 1f }),
            ("BottomCenter",  new[] { 0.5f, 1f },  new[] { 0.5f, 1f }),
            ("BottomRight",   new[] { 1f, 1f },    new[] { 1f, 1f }),
            ("Fill",          new[] { 0f, 0f },    new[] { 1f, 1f }),
            ("StretchTop",    new[] { 0f, 0f },    new[] { 1f, 0f }),
            ("StretchBottom", new[] { 0f, 1f },    new[] { 1f, 1f }),
            ("StretchLeft",   new[] { 0f, 0f },    new[] { 0f, 1f }),
            ("StretchRight",  new[] { 1f, 0f },    new[] { 1f, 1f }),
        };

        public string Name = "NewUI";
        public float RefW = 1920f;                       // reference width: saved in the file
        public int ScaleMode;                          // 0 fit height, 1 expand, 2 fit width, 3 letterbox, 4 stretch (saved)
        public float SafeArea;                           // 0..0.2 (saved)
        public float PreviewW = 1920f;                   // editor only: width of the previewed screen (height is 1080)

        public UIView View => UIView.Compute(ScaleMode, RefW, PreviewW, 1080f);

        // The rectangle a widget's anchors resolve against, in layout space
        public (float x, float y, float w, float h) ParentRect(DWidget? p)
        {
            if (p != null) return (p.X, p.Y, p.W, p.H);
            UIView v = View;
            float ix = v.Lw * SafeArea, iy = v.Lh * SafeArea;
            return (ix, iy, v.Lw - 2f * ix, v.Lh - 2f * iy);
        }
        public readonly List<DWidget> Roots = new();

        // ---------------- tree helpers ----------------
        public IEnumerable<DWidget> All()
        {
            foreach (DWidget r in Roots)
                foreach (DWidget w in Walk(r)) yield return w;
        }

        public static IEnumerable<DWidget> Walk(DWidget w)
        {
            yield return w;
            foreach (DWidget c in w.Children)
                foreach (DWidget x in Walk(c)) yield return x;
        }

        public int WidgetCount => All().Count();
        public List<DWidget> SiblingsOf(DWidget w) => w.Parent == null ? Roots : w.Parent.Children;
        public static bool Stretched(DWidget w, int a) => MathF.Abs(w.AnchorMax[a] - w.AnchorMin[a]) > 1e-5f;

        public string UniqueName(DWidget? self, string want)
        {
            want = string.IsNullOrWhiteSpace(want) ? "Widget" : want.Trim();
            string n = want;
            int i = 2;
            while (All().Any(w => w != self && w.Name == n)) n = want + i++;
            return n;
        }

        public void Remove(DWidget w)
        {
            SiblingsOf(w).Remove(w);
            w.Parent = null;
        }

        public DWidget Duplicate(DWidget w)
        {
            DWidget c = w.CloneTree(w.Parent);
            foreach (DWidget x in Walk(c)) x.Name = UniqueName(null, x.Name);
            c.Offset[0] += 24f; c.Offset[1] += 24f;
            List<DWidget> list = SiblingsOf(w);
            list.Insert(list.IndexOf(w) + 1, c);
            return c;
        }

        public bool Move(DWidget w, int dir)
        {
            List<DWidget> l = SiblingsOf(w);
            int i = l.IndexOf(w), j = i + dir;
            if (i < 0 || j < 0 || j >= l.Count) return false;
            (l[i], l[j]) = (l[j], l[i]);
            return true;
        }

        // np = new parent (a Canvas, or null for the root list). after = insert right after this sibling (null = at the end).
        public bool Reparent(DWidget w, DWidget? np, DWidget? after)
        {
            for (DWidget? p = np; p != null; p = p.Parent) if (p == w) return false;   // not into itself or its own children
            if (after == w) return false;
            SiblingsOf(w).Remove(w);
            w.Parent = np;
            List<DWidget> list = np == null ? Roots : np.Children;
            int i = after != null ? list.IndexOf(after) + 1 : list.Count;
            list.Insert(Math.Clamp(i, 0, list.Count), w);
            return true;
        }

        // What a freshly dropped widget looks like in the designer
        public static DWidget NewForDrop(string type)
        {
            DWidget w = DWidget.Blank(type, null);
            switch (type)
            {
                case "Canvas": w.Size = new[] { 300f, 200f }; w.Color = new[] { 0f, 0f, 0f, 0.35f }; break;
                case "Image": w.Size = new[] { 128f, 128f }; break;
                case "Text": w.Size = new[] { 320f, 50f }; w.Text = "Text"; w.FontSize = 40f; break;
                case "Button": w.Size = new[] { 280f, 70f }; w.Text = "Button"; w.FontSize = 36f; break;
                case "ProgressBar": w.Size = new[] { 320f, 24f }; w.Value = 0.6f; break;
            }
            return w;
        }

        // ---------------- layout (the same maths as GhostUI.cpp) ----------------
        public void Layout()
        {
            var r = ParentRect(null);
            foreach (DWidget w in Roots) LayoutOne(w, r.x, r.y, r.w, r.h, true);
        }

        private static void LayoutOne(DWidget w, float px, float py, float pw, float ph, bool pvis)
        {
            w.Eff = pvis && w.Visible;
            Axis(px, pw, w.AnchorMin[0], w.AnchorMax[0], w.Offset[0], w.Size[0], w.Pivot[0], out w.X, out w.W);
            Axis(py, ph, w.AnchorMin[1], w.AnchorMax[1], w.Offset[1], w.Size[1], w.Pivot[1], out w.Y, out w.H);
            foreach (DWidget c in w.Children) LayoutOne(c, w.X, w.Y, w.W, w.H, w.Eff);
        }

        private static void Axis(float pp, float ps, float amin, float amax, float off, float sz, float piv,
            out float pos, out float len)
        {
            if (MathF.Abs(amax - amin) < 1e-5f)
            {
                len = sz;
                pos = pp + ps * amin + off - piv * sz;
            }
            else
            {
                float l = pp + ps * amin + off;
                float r = pp + ps * amax - sz;
                pos = l;
                len = MathF.Max(r - l, 0f);
            }
        }

        public string PresetName(DWidget w)
        {
            foreach (var p in Presets)
                if (MathF.Abs(p.Min[0] - w.AnchorMin[0]) < 1e-4f && MathF.Abs(p.Min[1] - w.AnchorMin[1]) < 1e-4f &&
                    MathF.Abs(p.Max[0] - w.AnchorMax[0]) < 1e-4f && MathF.Abs(p.Max[1] - w.AnchorMax[1]) < 1e-4f)
                    return p.Name;
            return "Custom";
        }

        // Changes the anchors and recomputes offset / size / pivot so the widget stays where it is
        public void ApplyPreset(DWidget w, string name)
        {
            foreach (var p in Presets)
            {
                if (p.Name != name) continue;
                Layout();
                var pr = ParentRect(w.Parent);
                float px = pr.x, py = pr.y, pw = pr.w, ph = pr.h;
                float[] cur = { w.X, w.Y, w.W, w.H };

                for (int a = 0; a < 2; a++)
                {
                    float pp = a == 0 ? px : py, ps = a == 0 ? pw : ph;
                    float pos = cur[a], len = cur[a + 2];
                    w.AnchorMin[a] = p.Min[a];
                    w.AnchorMax[a] = p.Max[a];
                    if (MathF.Abs(p.Max[a] - p.Min[a]) < 1e-5f)
                    {
                        w.Pivot[a] = p.Min[a];
                        w.Size[a] = MathF.Round(len, 2);
                        w.Offset[a] = MathF.Round(pos - (pp + ps * p.Min[a]) + w.Pivot[a] * len, 2);
                    }
                    else
                    {
                        w.Pivot[a] = 0f;
                        w.Offset[a] = MathF.Round(pos - (pp + ps * p.Min[a]), 2);
                        w.Size[a] = MathF.Round((pp + ps * p.Max[a]) - (pos + len), 2);
                    }
                }
                return;
            }
        }

        // ---------------- load ----------------
        private static string Str(JsonObject o, string k, string d) =>
            o[k] is JsonValue v && v.TryGetValue<string>(out var s) && s != null ? s : d;
        private static float Num(JsonObject o, string k, float d) =>
            o[k] is JsonValue v && v.TryGetValue<double>(out var x) ? (float)x : d;
        private static int Int(JsonObject o, string k, int d) =>
            o[k] is JsonValue v && v.TryGetValue<double>(out var x) ? (int)x : d;
        private static bool Bool(JsonObject o, string k, bool d) =>
            o[k] is JsonValue v && v.TryGetValue<bool>(out var b) ? b : d;

        private static bool Arr(JsonObject o, string k, float[] dst)
        {
            if (o[k] is not JsonArray a) return false;
            for (int i = 0; i < dst.Length && i < a.Count; i++)
                if (a[i] is JsonValue v && v.TryGetValue<double>(out var x)) dst[i] = (float)x;
            return true;
        }

        private static float[] DeriveHover(float[] c) =>
            new[] { c[0] + (1f - c[0]) * 0.2f, c[1] + (1f - c[1]) * 0.2f, c[2] + (1f - c[2]) * 0.2f, c[3] };
        private static float[] DerivePress(float[] c) => new[] { c[0] * 0.7f, c[1] * 0.7f, c[2] * 0.7f, c[3] };
        private static bool Same(float[] a, float[] b)
        {
            for (int i = 0; i < 4; i++) if (MathF.Abs(a[i] - b[i]) > 1e-3f) return false;
            return true;
        }

        public static DesignDoc Load(string path, out string warn) => Parse(File.ReadAllText(path), out warn);

        public static DesignDoc Parse(string text, out string warn)
        {
            warn = "";
            JsonNode? root = JsonNode.Parse(text, null,
                new JsonDocumentOptions { AllowTrailingCommas = true, CommentHandling = JsonCommentHandling.Skip });
            if (root is not JsonObject o || Str(o, "format", "") != "ghostui")
                throw new InvalidDataException("This is not a GhostX UI file (\"format\" must be \"ghostui\").");
            if (Int(o, "version", 0) > 1)
                throw new InvalidDataException("This UI file was saved by a newer version of GhostX.");

            var d = new DesignDoc
            {
                Name = Str(o, "name", "NewUI"),
                RefW = Math.Clamp(Num(o, "referenceWidth", 1920f), 640f, 7680f),
                ScaleMode = Math.Clamp(Int(o, "scaleMode", 0), 0, UIView.ModeNames.Length - 1),
                SafeArea = Math.Clamp(Num(o, "safeArea", 0f), 0f, 0.2f),
            };
            int count = 0;
            if (o["widgets"] is JsonArray list)
                foreach (JsonNode? n in list)
                    if (n is JsonObject jw) LoadWidget(jw, null, 0, d, ref count, ref warn);
            return d;
        }

        private static void LoadWidget(JsonObject j, DWidget? parent, int depth, DesignDoc d, ref int count, ref string warn)
        {
            if (depth > 12 || count >= MaxWidgets)
            { warn += "Too many widgets or nested too deep: the rest were skipped.\n"; return; }

            string tname = Str(j, "type", "Canvas");
            if (Array.IndexOf(Types, tname) < 0)
            { warn += $"Skipped a widget with the unknown type \"{tname}\".\n"; return; }

            DWidget w = DWidget.Blank(tname, parent);
            w.Name = Str(j, "name", tname);

            string preset = Str(j, "anchor", "");
            if (preset.Length > 0)
            {
                bool found = false;
                foreach (var p in Presets)
                    if (p.Name == preset)
                    {
                        w.AnchorMin = (float[])p.Min.Clone(); w.AnchorMax = (float[])p.Max.Clone();
                        found = true;
                    }
                if (!found) warn += $"Unknown anchor \"{preset}\" on {w.Name}.\n";
            }
            Arr(j, "anchorMin", w.AnchorMin);
            Arr(j, "anchorMax", w.AnchorMax);

            for (int i = 0; i < 2; i++)
            {
                bool st = Stretched(w, i);
                w.Size[i] = st ? 0f : (i == 0 ? 100f : 40f);
                w.Pivot[i] = st ? 0f : w.AnchorMin[i];
            }
            Arr(j, "offset", w.Offset);
            Arr(j, "size", w.Size);
            Arr(j, "pivot", w.Pivot);

            Arr(j, "color", w.Color);
            float[] hv = DeriveHover(w.Color), pv = DerivePress(w.Color);
            if (Arr(j, "hoverColor", hv)) w.HoverColor = hv;
            if (Arr(j, "pressColor", pv)) w.PressColor = pv;
            Arr(j, "textColor", w.TextColor);
            Arr(j, "bgColor", w.BgColor);

            w.Visible = Bool(j, "visible", true);
            w.Text = Str(j, "text", "");
            if (w.Text.Length > 255) w.Text = w.Text.Substring(0, 255);
            w.FontSize = Math.Clamp(Num(j, "fontSize", 32f), 4f, 400f);
            w.Align = Math.Clamp(Int(j, "align", 0), 0, 2);
            w.Shadow = Bool(j, "shadow", true);
            w.Value = Math.Clamp(Num(j, "value", 1f), 0f, 1f);
            w.Texture = Str(j, "texture", "");
            w.Font = Str(j, "font", "");
            count++;
            (parent == null ? d.Roots : parent.Children).Add(w);

            if (j["children"] is JsonArray kids)
                foreach (JsonNode? k in kids)
                    if (k is JsonObject kj) LoadWidget(kj, w, depth + 1, d, ref count, ref warn);
        }

        // ---------------- save ----------------
        private static JsonNode R(float v) => JsonValue.Create(Math.Round((double)v, 3));
        private static JsonArray A(float[] v)
        {
            var a = new JsonArray();
            foreach (float x in v) a.Add(R(x));
            return a;
        }

        private static JsonObject SaveWidget(DWidget w)
        {
            var j = new JsonObject
            {
                ["type"] = w.Type,
                ["name"] = w.Name,
                ["anchorMin"] = A(w.AnchorMin),
                ["anchorMax"] = A(w.AnchorMax),
                ["offset"] = A(w.Offset),
                ["size"] = A(w.Size),
                ["pivot"] = A(w.Pivot),
                ["color"] = A(w.Color),
            };
            if (!w.Visible) j["visible"] = false;

            switch (w.Type)
            {
                case "Image":
                    j["texture"] = w.Texture;
                    break;
                case "Text":
                    j["text"] = w.Text; j["fontSize"] = R(w.FontSize); j["align"] = w.Align; j["shadow"] = w.Shadow;
                    if (w.Font.Length > 0) j["font"] = w.Font;
                    break;
                case "Button":
                    j["text"] = w.Text; j["fontSize"] = R(w.FontSize); j["shadow"] = w.Shadow;
                    j["textColor"] = A(w.TextColor);
                    if (w.Font.Length > 0) j["font"] = w.Font;
                    if (w.HoverColor != null && !Same(w.HoverColor, DeriveHover(w.Color))) j["hoverColor"] = A(w.HoverColor);
                    if (w.PressColor != null && !Same(w.PressColor, DerivePress(w.Color))) j["pressColor"] = A(w.PressColor);
                    break;
                case "ProgressBar":
                    j["value"] = R(w.Value); j["bgColor"] = A(w.BgColor);
                    break;
            }

            if (w.Children.Count > 0)
            {
                var kids = new JsonArray();
                foreach (DWidget c in w.Children) kids.Add(SaveWidget(c));
                j["children"] = kids;
            }
            return j;
        }

        // withName = false is used for undo snapshots, so renaming the file never counts as an edit
        public string ToJson(bool withName)
        {
            var o = new JsonObject { ["format"] = "ghostui", ["version"] = 1 };
            if (withName) o["name"] = Name;
            o["referenceWidth"] = R(RefW); o["scaleMode"] = ScaleMode; o["safeArea"] = R(SafeArea);
            var arr = new JsonArray();
            foreach (DWidget r in Roots) arr.Add(SaveWidget(r));
            o["widgets"] = arr;
            return o.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
        }

        public void SaveTo(string path)
        {
            string? dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            string tmp = path + ".tmp";
            File.WriteAllText(tmp, ToJson(true));          // a failed save never destroys the old file
            File.Move(tmp, path, true);
        }
    }
}