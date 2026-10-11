using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace GhostShell
{
    internal static class GhostImageLoader
    {
        public const int MaxSize = 4096;

        // Returns tightly packed RGBA bytes (top row first). Images larger than 4096 are downscaled.
        // Returns tightly packed RGBA bytes (top row first). The longest side is capped at maxSize
        // (1..4096); smaller images are left alone. sourceWidth/Height = the file's real size.
        public static byte[] Load(string path, int maxSize, out int width, out int height,
            out int sourceWidth, out int sourceHeight)
        {
            maxSize = Math.Clamp(maxSize, 1, MaxSize);
            using var src = new Bitmap(path);
            int w = src.Width, h = src.Height;
            sourceWidth = w; sourceHeight = h;
            Bitmap bmp = src;
            Bitmap? scaled = null;

            if (w > maxSize || h > maxSize)
            {
                double s = (double)maxSize / Math.Max(w, h);
                w = Math.Max(1, (int)(w * s));
                h = Math.Max(1, (int)(h * s));
                scaled = new Bitmap(w, h, PixelFormat.Format32bppArgb);
                using (var g = Graphics.FromImage(scaled))
                {
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.DrawImage(src, 0, 0, w, h);
                }
                bmp = scaled;
            }

            var px = new byte[w * h * 4];
            var data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                for (int y = 0; y < h; y++)
                    Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride), px, y * w * 4, w * 4);
            }
            finally { bmp.UnlockBits(data); scaled?.Dispose(); }

            for (int i = 0; i < px.Length; i += 4)      // BGRA -> RGBA
                (px[i], px[i + 2]) = (px[i + 2], px[i]);

            width = w; height = h;
            return px;
        }
    }
}