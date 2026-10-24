// GhostSpriteUI.cs — Details grid for Sprite Actors, and the test sprite sheet generator
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class SpriteSourceConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Single Image", "Sprite Sheet" };
        protected override string[] Items => Names;
    }

    // .ghostanim files in the project's Content folder, plus Browse...
    internal sealed class AnimFileConverter : StringConverter
    {
        public const string None = "(none)", Browse = "Browse...";
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => false;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { None };
            try
            {
                string? content = GhostProject.ContentDir, root = GhostProject.Root;
                if (content != null && root != null && Directory.Exists(content))
                    foreach (string f in Directory.EnumerateFiles(content, "*.ghostanim", SearchOption.AllDirectories))
                    {
                        list.Add(Path.GetRelativePath(root, f).Replace('\\', '/'));
                        if (list.Count > 200) break;
                    }
            }
            catch { }
            list.Add(Browse);
            return new StandardValuesCollection(list);
        }
    }

    // The clips of whichever sprite the grid is showing
    internal sealed class ClipChoiceConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context) =>
            new StandardValuesCollection((context?.Instance as SpriteProxy)?.ClipNames() ?? new List<string>());
    }

    internal sealed class SpriteProxy
    {
        private readonly int _id;

        public SpriteProxy(int id)
        {
            _id = id;
            Position = new Vec3Edit(
                () => { var i = Obj(); return (i.px, i.py, i.pz); },
                (x, y, z) => { var i = Obj(); i.px = x; i.py = y; i.pz = z; SetObj(i); });
            Scale = new Vec3Edit(
                () => { var i = Obj(); return (i.sx, i.sy, i.sz); },
                (x, y, z) => { var i = Obj(); i.sx = x; i.sy = y; i.sz = z; SetObj(i); });
            ColliderSize = new Vec3Edit(
                () => { var g = Gp(); return (g.csx, g.csy, g.csz); },
                (x, y, z) => { var g = Gp(); g.csx = x; g.csy = y; g.csz = z; SetGp(g); });
            ColliderOffset = new Vec3Edit(
    () => { var g = Gp(); return (g.cox, g.coy, g.coz); },
    (x, y, z) => { var g = Gp(); g.cox = x; g.coy = y; g.coz = z; SetGp(g); });
        }

        private GhostObjectInfo Obj() { var i = new GhostObjectInfo(); GhostEngine.GhostEngine_GetObject(_id, ref i); return i; }
        private void SetObj(GhostObjectInfo i) => GhostEngine.GhostEngine_SetObject(ref i);
        private GhostSpriteInfo Spr() { var s = new GhostSpriteInfo(); GhostEngine.GhostEngine_GetSprite(_id, ref s); return s; }
        private void SetSpr(GhostSpriteInfo s) => GhostEngine.GhostEngine_SetSprite(ref s);
        private GhostGameplayInfo Gp() { var g = new GhostGameplayInfo(); GhostEngine.GhostEngine_GetGameplay(_id, ref g); return g; }
        private void SetGp(GhostGameplayInfo g) => GhostEngine.GhostEngine_SetGameplay(ref g);

        private string Text(int which)
        {
            var b = new byte[2048];
            if (GhostEngine.GhostEngine_GetSpriteText(_id, which, b, b.Length) == 0) return "";
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        internal List<string> ClipNames()
        {
            string t = Text(2);
            return t.Length == 0 ? new List<string>() : new List<string>(t.Split('|'));
        }

        [Category("Details")] public string Name => GhostNames.Of(_id);

        // ---- Sprite ----
        [Category("Sprite"), DisplayName("Texture (sheet)")]
        [TypeConverter(typeof(TextureChoiceConverter))]
        public string Texture
        {
            get => GhostTextures.DisplayName(Spr().textureId);
            set
            {
                int tex = value == GhostTextures.ImportLabel ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                var s = Spr(); s.textureId = tex; SetSpr(s);
            }
        }

        [Category("Sprite")]
        [TypeConverter(typeof(SpriteSourceConverter))]
        public string Source
        {
            get => SpriteSourceConverter.Names[Math.Clamp(Spr().source, 0, 1)];
            set { int i = Array.IndexOf(SpriteSourceConverter.Names, value); if (i >= 0) { var s = Spr(); s.source = i; SetSpr(s); } }
        }

        [Category("Sprite")] public int Columns { get => Spr().cols; set { var s = Spr(); s.cols = value; SetSpr(s); } }
        [Category("Sprite")] public int Rows { get => Spr().rows; set { var s = Spr(); s.rows = value; SetSpr(s); } }

        [Category("Sprite"), DisplayName("Frame (when no animation)")]
        public int Frame { get => Spr().frame; set { var s = Spr(); s.frame = value; SetSpr(s); } }

        [Category("Sprite"), DisplayName("Frame Size (px)")]
        public string FrameSize
        {
            get
            {
                var s = Spr();
                if (s.effTexture <= 0) return "-";
                var sb = new StringBuilder(8);
                if (GhostEngine.GhostEngine_GetTextureInfo(s.effTexture, sb, 8, out uint w, out uint h) == 0) return "-";
                return $"{w / (uint)Math.Max(s.effCols, 1)} x {h / (uint)Math.Max(s.effRows, 1)}   ({s.effCols} x {s.effRows} frames)";
            }
        }

        [Category("Sprite"), DisplayName("Current Frame")] public int CurrentFrame => Spr().curFrame;

        [Category("Sprite"), DisplayName("Flip X (face left)")]
        public bool FlipX { get => Spr().flipX != 0; set { var s = Spr(); s.flipX = value ? 1 : 0; SetSpr(s); } }

        // ---- Rendering ----
        [Category("Rendering")] public bool Billboard { get => Spr().billboard != 0; set { var s = Spr(); s.billboard = value ? 1 : 0; SetSpr(s); } }
        [Category("Rendering")] public bool Unlit { get => Spr().unlit != 0; set { var s = Spr(); s.unlit = value ? 1 : 0; SetSpr(s); } }

        [Category("Rendering"), DisplayName("Alpha Clip")]
        public bool AlphaClip { get => Spr().alphaClip != 0; set { var s = Spr(); s.alphaClip = value ? 1 : 0; SetSpr(s); } }

        [Category("Rendering"), DisplayName("Pixel Art (point filter)")]
        public bool PixelArt { get => Spr().pixelArt != 0; set { var s = Spr(); s.pixelArt = value ? 1 : 0; SetSpr(s); } }

        [Category("Rendering")]
        public Color Tint
        {
            get { var s = Spr(); return GhostColorUtil.ToColor(s.tintR, s.tintG, s.tintB); }
            set { var s = Spr(); s.tintR = value.R / 255f; s.tintG = value.G / 255f; s.tintB = value.B / 255f; SetSpr(s); }
        }

        // ---- Animation ----
        [Category("Animation"), DisplayName("Animation File")]
        [TypeConverter(typeof(AnimFileConverter))]
        public string AnimationFile
        {
            get { string t = Text(0); return t.Length == 0 ? AnimFileConverter.None : t; }
            set
            {
                string path = value == AnimFileConverter.None ? "" : value == AnimFileConverter.Browse ? Browse() ?? Text(0) : value;
                GhostEngine.GhostEngine_SetSpriteText(_id, 0, path);
            }
        }

        [Category("Animation")]
        [TypeConverter(typeof(ClipChoiceConverter))]
        public string Clip
        {
            get => Text(1);
            set => GhostEngine.GhostEngine_SetSpriteText(_id, 1, value ?? "");
        }

        [Category("Animation")] public bool Playing { get => Spr().playing != 0; set { var s = Spr(); s.playing = value ? 1 : 0; SetSpr(s); } }
        [Category("Animation")] public float Speed { get => Spr().animSpeed; set { var s = Spr(); s.animSpeed = value; SetSpr(s); } }

        private static string? Browse()
        {
            using var dlg = new OpenFileDialog { Title = "Choose an animation", Filter = "GhostX Animation (*.ghostanim)|*.ghostanim" };
            string? content = GhostProject.ContentDir;
            if (content != null && Directory.Exists(content)) dlg.InitialDirectory = content;
            if (dlg.ShowDialog() != DialogResult.OK) return null;
            string? root = GhostProject.Root;
            if (root != null)
            {
                string rel = Path.GetRelativePath(root, dlg.FileName);
                if (!rel.StartsWith("..") && !Path.IsPathRooted(rel)) return rel.Replace('\\', '/');
            }
            return dlg.FileName;
        }
        // ---- Shadows ----
        [Category("Shadows"), DisplayName("Cast Shadows")]
        [TypeConverter(typeof(ShadowCasterConverter))]
        public string CastShadows
        {
            get => GhostShadowUI.GetCast(_id);
            set => GhostShadowUI.SetCast(_id, value);
        }

        [Category("Shadows"), DisplayName("Receive Shadows")]
        public bool ReceiveShadows
        {
            get => GhostShadowUI.GetReceive(_id);
            set => GhostShadowUI.SetReceive(_id, value);
        }
        // ---- Transform ----
        [Category("Transform")] public Vec3Edit Position { get; }
        [Category("Transform"), DisplayName("Scale (width, height)")] public Vec3Edit Scale { get; }

        // ---- Gameplay ----
        [Category("Gameplay")]
        [TypeConverter(typeof(RoleConverter))]
        public string Role
        {
            get => RoleConverter.Names[Math.Clamp(Gp().role, 0, RoleConverter.Names.Length - 1)];
            set { int i = Array.IndexOf(RoleConverter.Names, value); if (i >= 0) { var g = Gp(); g.role = i; SetGp(g); } }
        }

        [Category("Gameplay")] public bool Collider { get => Gp().collider != 0; set { var g = Gp(); g.collider = value ? 1 : 0; SetGp(g); } }
        [Category("Gameplay"), DisplayName("Collider Size")] public Vec3Edit ColliderSize { get; }
        [Category("Gameplay"), DisplayName("Collider Offset")] public Vec3Edit ColliderOffset { get; }

        [Category("Gameplay"), DisplayName("Visible In Game")]
        public bool VisibleInGame { get => Gp().visible != 0; set { var g = Gp(); g.visible = value ? 1 : 0; SetGp(g); } }
    }

    // Play > Create Test Sprite Sheet: a 4 x 2 sheet and a matching .ghostanim, so there is something to test with
    internal static class GhostTestSprite
    {
        public static void Create(IWin32Window owner)
        {
            string? content = GhostProject.ContentDir;
            if (content == null)
            {
                MessageBox.Show(owner, "Open or create a project first.", "Test sprite sheet", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            try
            {
                string sprites = Path.Combine(content, "Sprites"), anims = Path.Combine(content, "Animations");
                Directory.CreateDirectory(sprites);
                Directory.CreateDirectory(anims);

                const int cell = 64, cols = 4, rows = 2;
                using (var bmp = new Bitmap(cell * cols, cell * rows, PixelFormat.Format32bppArgb))
                {
                    using (var g = Graphics.FromImage(bmp))
                    {
                        g.SmoothingMode = SmoothingMode.None;
                        g.Clear(Color.Transparent);
                        for (int f = 0; f < cols * rows; f++)
                        {
                            int cx = (f % cols) * cell, cy = (f / cols) * cell;
                            bool walk = f < 4, jump = f == 6, hurt = f == 7, squash = f == 5;
                            Color body = hurt ? Color.FromArgb(220, 60, 60) : jump ? Color.FromArgb(80, 200, 120) : Color.FromArgb(70, 130, 230);
                            int lift = jump ? 10 : 0, h = squash ? 33 : 36;
                            int top = cy + cell - 4 - 12 - h - lift;

                            using (var bb = new SolidBrush(body)) g.FillRectangle(bb, cx + 16, top, 32, h);
                            using (var lb = new SolidBrush(Color.FromArgb(40, 70, 150)))
                            {
                                int l1 = walk && f % 2 == 0 ? 4 : 0, l2 = walk && f % 2 == 1 ? 4 : 0;
                                g.FillRectangle(lb, cx + 18, cy + cell - 16 - lift - l1, 10, 12);
                                g.FillRectangle(lb, cx + 36, cy + cell - 16 - lift - l2, 10, 12);
                            }
                            g.FillRectangle(Brushes.White, cx + 34, top + 8, 11, 11);     // the eye is on the right: it faces right
                            g.FillRectangle(Brushes.Black, cx + 39, top + 11, 5, 5);
                        }
                    }
                    bmp.Save(Path.Combine(sprites, "TestSheet.png"), ImageFormat.Png);
                }

                File.WriteAllText(Path.Combine(anims, "TestSheet.ghostanim"), """
                {
                  "format": "ghostanim",
                  "version": 1,
                  "name": "TestSheet",
                  "texture": "Content/Sprites/TestSheet.png",
                  "source": "sheet",
                  "columns": 4,
                  "rows": 2,
                  "clips": [
                    { "name": "Idle", "frames": [4, 5],       "fps": 2, "loop": true },
                    { "name": "Walk", "frames": [0, 1, 2, 3], "fps": 8, "loop": true },
                    { "name": "Jump", "frames": [6],          "fps": 1, "loop": false },
                    { "name": "Hurt", "frames": [7],          "fps": 1, "loop": false }
                  ]
                }
                """);

                MessageBox.Show(owner, "Created Content/Sprites/TestSheet.png and Content/Animations/TestSheet.ghostanim.",
                    "Test sprite sheet", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show(owner, ex.Message, "Test sprite sheet", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }
    }
}
