// GhostAssets.cs — C#-side texture library: source paths, thumbnails, resize bookkeeping
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;

namespace GhostShell
{
    internal sealed class TextureAsset
    {
        public int Id;
        public string Name = "";
        public string? Path;          // null = built-in
        public int OrigW, OrigH;      // the file's real size
        public int MaxSize;           // 0 = Original
        public Bitmap Thumb = null!;
    }

    internal static class GhostAssets
    {
        public const int ThumbSize = 96;

        private static readonly Dictionary<int, TextureAsset> _byId = new();
        public static event Action? Changed;

        // Built-ins mirror the ones GhostEngine_Init registers: 0 white, 1 checkerboard, 2 cube cross
        static GhostAssets()
        {
            _byId[0] = new TextureAsset { Id = 0, Name = "None", Thumb = MakeNone() };
            _byId[1] = new TextureAsset { Id = 1, Name = "Checkerboard", Thumb = MakeChecker() };
            _byId[2] = new TextureAsset { Id = 2, Name = "Cube Cross Test", Thumb = MakeCross() };
        }

        public static IReadOnlyList<TextureAsset> All => _byId.Values.OrderBy(a => a.Id).ToList();
        public static TextureAsset? Get(int id) => _byId.TryGetValue(id, out var a) ? a : null;

        public static bool IsImage(string path)
        {
            string e = Path.GetExtension(path).ToLowerInvariant();
            return e is ".png" or ".jpg" or ".jpeg" or ".bmp";
        }

        public static string ResizeText(int id)
        {
            var a = Get(id);
            if (a == null || a.Path == null) return "-";
            return a.MaxSize <= 0 ? "Original" : a.MaxSize.ToString();
        }

        // "Original" -> 0, or a number 1..4096
        public static bool TryParseSize(string text, out int max)
        {
            max = 0;
            text = text.Trim();
            if (text.Equals("Original", StringComparison.OrdinalIgnoreCase)) return true;
            return int.TryParse(text, out max) && max >= 1 && max <= GhostImageLoader.MaxSize;
        }

        // Decodes, uploads and registers a file. Returns the texture id, or -1 if the engine refused it.
        // Throws if the file can't be read.
        public static int Import(string path, int maxSize = 0)
        {
            int cap = maxSize <= 0 ? GhostImageLoader.MaxSize : maxSize;
            byte[] px = GhostImageLoader.Load(path, cap, out int w, out int h, out int ow, out int oh);
            int id = GhostEngine.GhostEngine_CreateTexture(Path.GetFileName(path), px, (uint)w, (uint)h);
            if (id < 0) return -1;

            _byId[id] = new TextureAsset
            {
                Id = id,
                Name = Path.GetFileName(path),
                Path = path,
                OrigW = ow,
                OrigH = oh,
                MaxSize = maxSize,
                Thumb = MakeThumb(path),
            };
            GhostEngine.GhostEngine_SetTexturePath(id, Path.GetFullPath(path));   // so scenes can save and re-load it
            Changed?.Invoke();
            return id;
        }

        // Re-reads the source file at the new size and re-uploads it under the same id.
        // Every object and face using this texture updates. Throws if the file can't be read.
        public static bool Resize(int id, int maxSize)
        {
            var a = Get(id);
            if (a == null || a.Path == null) return false;

            int cap = maxSize <= 0 ? GhostImageLoader.MaxSize : maxSize;
            byte[] px = GhostImageLoader.Load(a.Path, cap, out int w, out int h, out int ow, out int oh);
            if (GhostEngine.GhostEngine_ReplaceTexture(id, a.Name, px, (uint)w, (uint)h) == 0) return false;

            a.MaxSize = maxSize;
            a.OrigW = ow; a.OrigH = oh;
            return true;
        }

        // ---------------- thumbnails ----------------
        private static Bitmap MakeThumb(string path)
        {
            var thumb = new Bitmap(ThumbSize, ThumbSize);
            using var src = new Bitmap(path);
            using var g = Graphics.FromImage(thumb);
            g.Clear(Color.FromArgb(24, 24, 24));
            g.InterpolationMode = InterpolationMode.HighQualityBilinear;
            float s = Math.Min((float)ThumbSize / src.Width, (float)ThumbSize / src.Height);
            float w = src.Width * s, h = src.Height * s;
            g.DrawImage(src, (ThumbSize - w) / 2, (ThumbSize - h) / 2, w, h);
            return thumb;
        }

        private static Bitmap MakeNone()
        {
            var b = new Bitmap(ThumbSize, ThumbSize);
            using var g = Graphics.FromImage(b);
            g.Clear(Color.FromArgb(40, 40, 44));
            using var white = new SolidBrush(Color.White);
            g.FillRectangle(white, 20, 20, 56, 56);
            return b;
        }

        private static Bitmap MakeChecker()
        {
            var b = new Bitmap(ThumbSize, ThumbSize);
            using var g = Graphics.FromImage(b);
            int cell = ThumbSize / 8;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                {
                    Color c = ((x + y) % 2 == 0) ? Color.FromArgb(230, 230, 230) : Color.FromArgb(70, 70, 70);
                    if (x == 0 && y == 0) c = Color.FromArgb(255, 140, 40);
                    using var br = new SolidBrush(c);
                    g.FillRectangle(br, x * cell, y * cell, cell, cell);
                }
            return b;
        }

        private static Bitmap MakeCross()
        {
            var b = new Bitmap(ThumbSize, ThumbSize);
            using var g = Graphics.FromImage(b);
            g.Clear(Color.FromArgb(24, 24, 24));
            const int cs = ThumbSize / 4;
            int oy = (ThumbSize - cs * 3) / 2;
            (int cx, int cy, Color c)[] cells =
            {
                (1, 0, Color.FromArgb(60, 180, 75)),  (0, 1, Color.FromArgb(60, 100, 220)),
                (1, 1, Color.FromArgb(220, 60, 60)),  (2, 1, Color.FromArgb(230, 200, 50)),
                (3, 1, Color.FromArgb(150, 70, 200)), (1, 2, Color.FromArgb(50, 200, 210)),
            };
            using var mark = new SolidBrush(Color.FromArgb(255, 140, 40));
            foreach (var (cx, cy, c) in cells)
            {
                using var br = new SolidBrush(c);
                g.FillRectangle(br, cx * cs, oy + cy * cs, cs, cs);
                g.FillRectangle(mark, cx * cs, oy + cy * cs, 6, 6);
            }
            return b;
        }

        // After a scene load the engine may have loaded textures C# doesn't know yet: register them
        public static void SyncFromEngine()
        {
            int n = GhostEngine.GhostEngine_GetTextureCount();
            bool changed = false;
            for (int id = 3; id < n; id++)
            {
                if (_byId.ContainsKey(id)) continue;

                var buf = new byte[1024];
                if (GhostEngine.GhostEngine_GetTexturePath(id, buf, buf.Length) == 0) continue;
                int len = Array.IndexOf(buf, (byte)0);
                string path = System.Text.Encoding.UTF8.GetString(buf, 0, len < 0 ? buf.Length : len);

                GhostEngine.GhostEngine_GetTextureInfo(id, new System.Text.StringBuilder(8), 8, out uint w, out uint h);
                try
                {
                    _byId[id] = new TextureAsset
                    {
                        Id = id,
                        Name = Path.GetFileName(path),
                        Path = path,
                        OrigW = (int)w,
                        OrigH = (int)h,
                        MaxSize = 0,
                        Thumb = MakeThumb(path),
                    };
                    changed = true;
                }
                catch { /* unreadable thumbnail: the texture still works, it just has no tile */ }
            }
            if (changed) Changed?.Invoke();
        }

        public static int FindByPath(string path)
        {
            string full = Path.GetFullPath(path);
            foreach (TextureAsset a in _byId.Values)
                if (a.Path != null && string.Equals(Path.GetFullPath(a.Path), full, StringComparison.OrdinalIgnoreCase))
                    return a.Id;
            return -1;
        }

        // Reuses the texture if this file is already loaded, otherwise imports it
        public static int ImportOrGet(string path)
        {
            int id = FindByPath(path);
            return id >= 0 ? id : Import(path);
        }

        public static Bitmap? TryMakeThumb(string path)
        {
            try { return MakeThumb(path); } catch { return null; }
        }


    }
}