// GhostFontAtlas.cs — bakes ASCII 32..126 into an atlas with GDI+ and registers it with the engine
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Runtime.InteropServices;

namespace GhostShell
{
    internal static class GhostFontAtlas
    {
        public const int FirstChar = 32, CharCount = 95, Columns = 16;

        public static int TextureId { get; private set; } = -1;     // the engine texture holding the atlas

        public static bool BakeDefault() => Bake("Segoe UI", 48f, FontStyle.Regular);

        public static bool Bake(string family, float sizePx, FontStyle style)
        {
            const int pad = 4;
            try
            {
                using var font = new Font(family, sizePx, style, GraphicsUnit.Pixel);
                using var fmt = (StringFormat)StringFormat.GenericTypographic.Clone();
                fmt.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces | StringFormatFlags.NoWrap;

                // ---- measure ----
                var adv = new float[CharCount];
                float maxAdv = 0.0f, lineH;
                using (var probe = new Bitmap(8, 8))
                using (var g = Graphics.FromImage(probe))
                {
                    g.PageUnit = GraphicsUnit.Pixel;
                    g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                    lineH = font.GetHeight(g);
                    for (int i = 0; i < CharCount; i++)
                    {
                        adv[i] = g.MeasureString(((char)(FirstChar + i)).ToString(), font, PointF.Empty, fmt).Width;
                        maxAdv = Math.Max(maxAdv, adv[i]);
                    }
                }

                int cellW = (int)Math.Ceiling(maxAdv) + pad * 2;
                int cellH = (int)Math.Ceiling(lineH) + pad * 2;
                int rows = (CharCount + Columns - 1) / Columns;
                int W = cellW * Columns, H = cellH * rows;
                if (W > 4096 || H > 4096) return false;

                // ---- draw ----
                using var bmp = new Bitmap(W, H, PixelFormat.Format32bppArgb);
                using (var g = Graphics.FromImage(bmp))
                {
                    g.Clear(Color.Transparent);
                    g.PageUnit = GraphicsUnit.Pixel;
                    g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                    for (int i = 0; i < CharCount; i++)
                    {
                        int cx = (i % Columns) * cellW, cy = (i / Columns) * cellH;
                        g.DrawString(((char)(FirstChar + i)).ToString(), font, Brushes.White, cx + pad, cy + pad, fmt);
                    }
                }

                // ---- to RGBA: color is white everywhere, so mip levels never darken the edges; only alpha varies ----
                var rgba = new byte[W * H * 4];
                var data = bmp.LockBits(new Rectangle(0, 0, W, H), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                try
                {
                    var row = new byte[W * 4];
                    for (int y = 0; y < H; y++)
                    {
                        Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride), row, 0, W * 4);
                        for (int x = 0; x < W; x++)
                        {
                            int o = (y * W + x) * 4;
                            rgba[o] = 255; rgba[o + 1] = 255; rgba[o + 2] = 255;
                            rgba[o + 3] = row[x * 4 + 3];
                        }
                    }
                }
                finally { bmp.UnlockBits(data); }

                int tex = GhostEngine.GhostEngine_CreateTexture("UI Font", rgba, (uint)W, (uint)H);
                if (tex < 0) return false;
                if (GhostEngine.GhostEngine_SetFont(tex, W, H, cellW, cellH, pad, (float)Math.Ceiling(lineH), adv) == 0) return false;

                TextureId = tex;
                return true;
            }
            catch { return false; }
        }
    }
}