// GhostParticleEditor.cs — particle editor window + the property proxy shown in Details.
// Edits go straight to the engine (GhostParticleMan), so the main viewport shows every change as you make it.
// A particle system is a normal scene entity (same id space as objects and lights), so the hierarchy,
// the viewport selection and the gizmo drive the same emitter this window edits.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace GhostShell
{
    // ============================================================
    //  Dropdown converters
    // ============================================================
    internal sealed class ParticleSpawnModeConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Anywhere in volume", "Top face (snow)", "Bottom face" };
        protected override string[] Items => Names;
    }

    internal sealed class ParticleSourceConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Single Image", "Sprite Sheet" };
        protected override string[] Items => Names;
    }

    internal sealed class ParticleBlendConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Additive (glow)", "Cutout (alpha test)" };
        protected override string[] Items => Names;
    }

    internal sealed class ParticlePresetConverter : FixedListConverter
    {
        public static readonly string[] Names = { "(choose)", "Fire", "Snow", "Custom" };
        protected override string[] Items => Names;
    }

    // Sprite quad, or any mesh the engine knows (flakes / embers / debris)
    internal sealed class ParticleMeshConverter : StringConverter
    {
        public const string QuadLabel = "Sprite (quad)";
        public const int QuadId = 4;                         // GHOST_MESH_QUAD

        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { QuadLabel };
            int n = GhostEngine.GhostEngine_GetMeshCount();
            for (int i = 0; i < n; i++)
            {
                if (i == QuadId) continue;
                string name = NameOf(i);
                if (name.Length > 0) list.Add(name);
            }
            return new StandardValuesCollection(list);
        }

        public static string NameOf(int meshId)
        {
            var b = new byte[128];
            GhostEngine.GhostEngine_GetMeshText(meshId, 0, b, b.Length);
            return GhostSceneAudio.Text(b);
        }
    }

    // .ghostanim files under the project's Content folder (stored project-relative with '/', like everywhere else)
    internal sealed class ParticleAnimFileConverter : StringConverter
    {
        public const string NoneLabel = "(none)";

        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => false;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { NoneLabel };
            try
            {
                string? content = GhostProject.ContentDir;
                string? root = content == null ? null : Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(content));
                if (content != null && root != null && Directory.Exists(content))
                    foreach (string f in Directory.EnumerateFiles(content, "*.ghostanim", SearchOption.AllDirectories)
                                                  .OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
                        list.Add(Path.GetRelativePath(root, f).Replace('\\', '/'));
            }
            catch { /* unreadable folder: just offer (none) */ }
            return new StandardValuesCollection(list);
        }
    }

    internal sealed class ParticleClipConverter : StringConverter
    {
        public const string FirstLabel = "(first clip)";

        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => false;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
            => new StandardValuesCollection((context?.Instance as ParticleProxy)?.ClipNames() ?? new List<string> { FirstLabel });
    }

    // ============================================================
    //  Property proxy for one emitter (the Details panel and the Particle Editor window both use it)
    // ============================================================
    internal sealed class ParticleProxy
    {
        private readonly int _id;
        private delegate void PChange(ref GhostParticleParams p);
        private static readonly string[] TypeNames = { "Fire", "Snow", "Custom" };

        public ParticleProxy(int id)
        {
            _id = id;
            Position = new Vec3Edit(() => { var p = Get(); return (p.px, p.py, p.pz); },
                (x, y, z) => Edit((ref GhostParticleParams p) => { p.px = x; p.py = y; p.pz = z; }));
            Rotation = new Vec3Edit(() => { var p = Get(); return (p.rotX, p.rotY, p.rotZ); },
                (x, y, z) => Edit((ref GhostParticleParams p) => { p.rotX = x; p.rotY = y; p.rotZ = z; }));
            Scale = new Vec3Edit(() => { var p = Get(); return (p.scaleX, p.scaleY, p.scaleZ); },
                (x, y, z) => Edit((ref GhostParticleParams p) => { p.scaleX = x; p.scaleY = y; p.scaleZ = z; }));
            VolumeSize = new Vec3Edit(() => { var p = Get(); return (p.boxX, p.boxY, p.boxZ); },
                (x, y, z) => Edit((ref GhostParticleParams p) => { p.boxX = x; p.boxY = y; p.boxZ = z; }));
            VelocityMin = new Vec3Edit(() => { var p = Get(); return (p.velMinX, p.velMinY, p.velMinZ); },
                (x, y, z) => Edit((ref GhostParticleParams p) => { p.velMinX = x; p.velMinY = y; p.velMinZ = z; }));
            VelocityMax = new Vec3Edit(() => { var p = Get(); return (p.velMaxX, p.velMaxY, p.velMaxZ); },
                (x, y, z) => Edit((ref GhostParticleParams p) => { p.velMaxX = x; p.velMaxY = y; p.velMaxZ = z; }));
            Acceleration = new Vec3Edit(() => { var p = Get(); return (p.accelX, p.accelY, p.accelZ); },
                (x, y, z) => Edit((ref GhostParticleParams p) => { p.accelX = x; p.accelY = y; p.accelZ = z; }));
        }

        private GhostParticleParams Get() { var p = new GhostParticleParams(); GhostEngine.GhostEngine_GetParticle(_id, ref p); return p; }
        private void Edit(PChange c) { var p = Get(); c(ref p); GhostEngine.GhostEngine_SetParticle(ref p); }

        private string Text(int which)
        {
            var b = new byte[512];
            GhostEngine.GhostEngine_GetParticleText(_id, which, b, b.Length);
            return GhostSceneAudio.Text(b);
        }

        internal List<string> ClipNames()
        {
            var list = new List<string> { ParticleClipConverter.FirstLabel };
            var b = new byte[2048];
            if (GhostEngine.GhostEngine_GetParticleText(_id, 2, b, b.Length) != 0)
                foreach (string s in GhostSceneAudio.Text(b).Split('|', StringSplitOptions.RemoveEmptyEntries)) list.Add(s);
            return list;
        }

        // ---- Details / Transform ----
        [Category("Details")] public string Name => GhostNames.Of(_id);
        [Category("Details")] public string Type => TypeNames[Math.Clamp(Get().type, 0, TypeNames.Length - 1)];
        [Category("Transform")] public Vec3Edit Position { get; }
        [Category("Transform"), DisplayName("Rotation (deg)")] public Vec3Edit Rotation { get; }
        [Category("Transform"), DisplayName("Scale (volume, speed, size)")] public Vec3Edit Scale { get; }

        // ---- Emitter ----
        [Category("Emitter"), DisplayName("Live Particles")] public int LiveParticles => GhostEngine.GhostEngine_GetParticleLive(_id);

        [Category("Emitter"), DisplayName("Reset To Preset")]
        [TypeConverter(typeof(ParticlePresetConverter))]
        public string Preset
        {
            get => ParticlePresetConverter.Names[0];
            set
            {
                int i = Array.IndexOf(ParticlePresetConverter.Names, value);      // 1 fire, 2 snow, 3 custom
                if (i > 0) GhostEngine.GhostEngine_ApplyParticlePreset(_id, i - 1);
            }
        }

        [Category("Emitter")] public bool Enabled { get => Get().enabled != 0; set => Edit((ref GhostParticleParams p) => p.enabled = value ? 1 : 0); }

        // ---- Emission ----
        [Category("Emission"), DisplayName("Rate (particles/s)")] public float Rate { get => Get().rate; set => Edit((ref GhostParticleParams p) => p.rate = value); }
        [Category("Emission"), DisplayName("Max Particles")] public int MaxParticles { get => Get().maxParticles; set => Edit((ref GhostParticleParams p) => p.maxParticles = value); }
        [Category("Emission"), DisplayName("Lifetime Min (s)")] public float LifeMin { get => Get().lifeMin; set => Edit((ref GhostParticleParams p) => p.lifeMin = value); }
        [Category("Emission"), DisplayName("Lifetime Max (s)")] public float LifeMax { get => Get().lifeMax; set => Edit((ref GhostParticleParams p) => p.lifeMax = value); }
        [Category("Emission"), DisplayName("Start Full (prewarm)")] public bool Prewarm { get => Get().prewarm != 0; set => Edit((ref GhostParticleParams p) => p.prewarm = value ? 1 : 0); }

        [Category("Emission"), DisplayName("Spawn Mode")]
        [TypeConverter(typeof(ParticleSpawnModeConverter))]
        public string SpawnMode
        {
            get => ParticleSpawnModeConverter.Names[Math.Clamp(Get().spawnMode, 0, 2)];
            set { int i = Array.IndexOf(ParticleSpawnModeConverter.Names, value); if (i >= 0) Edit((ref GhostParticleParams p) => p.spawnMode = i); }
        }

        [Category("Emission"), DisplayName("Volume Size (box, centered)")] public Vec3Edit VolumeSize { get; }
        [Category("Emission"), DisplayName("Die Below Box")] public bool KillOutside { get => Get().killOutside != 0; set => Edit((ref GhostParticleParams p) => p.killOutside = value ? 1 : 0); }

        // ---- Motion ----
        [Category("Motion"), DisplayName("Velocity Min")] public Vec3Edit VelocityMin { get; }
        [Category("Motion"), DisplayName("Velocity Max")] public Vec3Edit VelocityMax { get; }
        [Category("Motion"), DisplayName("Acceleration")] public Vec3Edit Acceleration { get; }
        [Category("Motion")] public float Drag { get => Get().drag; set => Edit((ref GhostParticleParams p) => p.drag = value); }
        [Category("Motion"), DisplayName("Sway Amount")] public float SwayAmp { get => Get().swayAmp; set => Edit((ref GhostParticleParams p) => p.swayAmp = value); }
        [Category("Motion"), DisplayName("Sway Speed")] public float SwayFreq { get => Get().swayFreq; set => Edit((ref GhostParticleParams p) => p.swayFreq = value); }

        // ---- Look ----
        [Category("Look"), DisplayName("Size Min")] public float SizeMin { get => Get().sizeMin; set => Edit((ref GhostParticleParams p) => p.sizeMin = value); }
        [Category("Look"), DisplayName("Size Max")] public float SizeMax { get => Get().sizeMax; set => Edit((ref GhostParticleParams p) => p.sizeMax = value); }
        [Category("Look"), DisplayName("Size At Death (x)")] public float SizeEnd { get => Get().sizeEnd; set => Edit((ref GhostParticleParams p) => p.sizeEnd = value); }

        [Category("Look"), DisplayName("Start Color")]
        public Color StartColor
        {
            get { var p = Get(); return GhostColorUtil.ToColor(p.r0, p.g0, p.b0); }
            set => Edit((ref GhostParticleParams p) => { p.r0 = value.R / 255f; p.g0 = value.G / 255f; p.b0 = value.B / 255f; });
        }
        [Category("Look"), DisplayName("End Color")]
        public Color EndColor
        {
            get { var p = Get(); return GhostColorUtil.ToColor(p.r1, p.g1, p.b1); }
            set => Edit((ref GhostParticleParams p) => { p.r1 = value.R / 255f; p.g1 = value.G / 255f; p.b1 = value.B / 255f; });
        }
        [Category("Look"), DisplayName("Fade Start (0-1, additive)")] public float Alpha0 { get => Get().alpha0; set => Edit((ref GhostParticleParams p) => p.alpha0 = value); }
        [Category("Look"), DisplayName("Fade End (0-1, additive)")] public float Alpha1 { get => Get().alpha1; set => Edit((ref GhostParticleParams p) => p.alpha1 = value); }

        [Category("Look"), DisplayName("Particle Rotation Min (deg)")] public float RotMin { get => Get().rotMin; set => Edit((ref GhostParticleParams p) => p.rotMin = value); }
        [Category("Look"), DisplayName("Particle Rotation Max (deg)")] public float RotMax { get => Get().rotMax; set => Edit((ref GhostParticleParams p) => p.rotMax = value); }
        [Category("Look"), DisplayName("Spin Min (deg/s)")] public float SpinMin { get => Get().spinMin; set => Edit((ref GhostParticleParams p) => p.spinMin = value); }
        [Category("Look"), DisplayName("Spin Max (deg/s)")] public float SpinMax { get => Get().spinMax; set => Edit((ref GhostParticleParams p) => p.spinMax = value); }

        [Category("Look"), DisplayName("Billboard (faces camera)")] public bool Billboard { get => Get().billboard != 0; set => Edit((ref GhostParticleParams p) => p.billboard = value ? 1 : 0); }
        [Category("Look")] public bool Unlit { get => Get().unlit != 0; set => Edit((ref GhostParticleParams p) => p.unlit = value ? 1 : 0); }

        [Category("Look"), DisplayName("Blend")]
        [TypeConverter(typeof(ParticleBlendConverter))]
        public string Blend
        {
            get => ParticleBlendConverter.Names[Get().additive != 0 ? 0 : 1];
            set
            {
                int i = Array.IndexOf(ParticleBlendConverter.Names, value);
                if (i >= 0) Edit((ref GhostParticleParams p) => { p.additive = i == 0 ? 1 : 0; p.alphaClip = i == 0 ? 0 : 1; });
            }
        }
        [Category("Look"), DisplayName("Pixel Art (point filter)")] public bool PixelArt { get => Get().pixelArt != 0; set => Edit((ref GhostParticleParams p) => p.pixelArt = value ? 1 : 0); }
        [Category("Look"), DisplayName("Draw Distance")] public float DrawDistance { get => Get().drawDistance; set => Edit((ref GhostParticleParams p) => p.drawDistance = value); }

        // ---- Sprite / Mesh ----
        [Category("Sprite"), DisplayName("Mesh (Sprite = quad)")]
        [TypeConverter(typeof(ParticleMeshConverter))]
        public string Mesh
        {
            get { int m = Get().meshId; return m < 0 ? ParticleMeshConverter.QuadLabel : ParticleMeshConverter.NameOf(m); }
            set
            {
                int id = -1;
                if (value != ParticleMeshConverter.QuadLabel)
                {
                    int n = GhostEngine.GhostEngine_GetMeshCount();
                    for (int i = 0; i < n && id < 0; i++)
                        if (i != ParticleMeshConverter.QuadId && ParticleMeshConverter.NameOf(i) == value) id = i;
                }
                Edit((ref GhostParticleParams p) => p.meshId = id);
            }
        }

        [Category("Sprite"), DisplayName("Texture")]
        [TypeConverter(typeof(TextureChoiceConverter))]
        public string Texture
        {
            get => GhostTextures.DisplayName(Get().textureId);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                Edit((ref GhostParticleParams p) => p.textureId = tex);
            }
        }

        [Category("Sprite"), DisplayName("Animation File (.ghostanim)")]
        [TypeConverter(typeof(ParticleAnimFileConverter))]
        public string AnimationFile
        {
            get { string t = Text(0); return t.Length == 0 ? ParticleAnimFileConverter.NoneLabel : t; }
            set => GhostEngine.GhostEngine_SetParticleText(_id, 0, value == ParticleAnimFileConverter.NoneLabel ? "" : (value ?? ""));
        }

        [Category("Sprite"), DisplayName("Animation Clip")]
        [TypeConverter(typeof(ParticleClipConverter))]
        public string Clip
        {
            get { string t = Text(1); return t.Length == 0 ? ParticleClipConverter.FirstLabel : t; }
            set => GhostEngine.GhostEngine_SetParticleText(_id, 1, value == ParticleClipConverter.FirstLabel ? "" : (value ?? ""));
        }

        [Category("Sprite"), DisplayName("Animation Plays Once Over Life")] public bool AnimOverLife { get => Get().animOverLife != 0; set => Edit((ref GhostParticleParams p) => p.animOverLife = value ? 1 : 0); }
        [Category("Sprite"), DisplayName("Random Start Frame")] public bool RandomStart { get => Get().randomStartFrame != 0; set => Edit((ref GhostParticleParams p) => p.randomStartFrame = value ? 1 : 0); }
        [Category("Sprite"), DisplayName("Animation Speed (x)")] public float AnimSpeed { get => Get().animSpeed; set => Edit((ref GhostParticleParams p) => p.animSpeed = value); }

        [Category("Sprite Sheet (no animation file)"), DisplayName("Source")]
        [TypeConverter(typeof(ParticleSourceConverter))]
        public string Source
        {
            get => ParticleSourceConverter.Names[Math.Clamp(Get().source, 0, 1)];
            set { int i = Array.IndexOf(ParticleSourceConverter.Names, value); if (i >= 0) Edit((ref GhostParticleParams p) => p.source = i); }
        }
        [Category("Sprite Sheet (no animation file)")] public int Columns { get => Get().cols; set => Edit((ref GhostParticleParams p) => p.cols = value); }
        [Category("Sprite Sheet (no animation file)")] public int Rows { get => Get().rows; set => Edit((ref GhostParticleParams p) => p.rows = value); }
        [Category("Sprite Sheet (no animation file)"), DisplayName("Frames Per Second")] public float AnimFps { get => Get().animFps; set => Edit((ref GhostParticleParams p) => p.animFps = value); }
    }

    // ============================================================
    //  The window: emitter list on the left, properties on the right
    // ============================================================
    internal sealed class GhostParticleEditor : Panel
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
        private static readonly Color Accent = Color.FromArgb(0xFF, 0xC8, 0x6E);

        private sealed class Item
        {
            public int Id;
            public string Text = "";
            public override string ToString() => Text;
        }

        private readonly ListBox _list;
        private readonly PropertyGrid _grid;
        private readonly System.Windows.Forms.Timer _timer;
        private string _lastKey = "";
        private int _lastEngineSel = int.MinValue;
        private bool _syncing;

        public GhostParticleEditor()
        {
            BackColor = Bg;

            _list = new ListBox
            {
                Dock = DockStyle.Fill,
                BackColor = Bg2,
                ForeColor = Fg,
                BorderStyle = BorderStyle.None,
                IntegralHeight = false,
                Font = new Font("Segoe UI", 10.5f),
            };
            _list.SelectedIndexChanged += (s, e) =>
            {
                if (_syncing) return;
                if (_list.SelectedItem is Item it)
                {
                    GhostEngine.GhostEngine_SelectEntity(it.Id);          // the viewport gizmo and the hierarchy follow
                    _lastEngineSel = it.Id;
                }
                ShowSelected();
            };
            _list.KeyDown += (s, e) => { if (e.KeyCode == Keys.Delete) { RemoveSelected(); e.Handled = true; } };

            _grid = new PropertyGrid
            {
                Dock = DockStyle.Fill,
                ToolbarVisible = false,
                HelpVisible = false,
                PropertySort = PropertySort.Categorized,
                BackColor = Bg,
                ViewBackColor = Bg2,
                ViewForeColor = Fg,
                LineColor = Color.FromArgb(0x3A, 0x3A, 0x40),
                CategoryForeColor = Accent,
                DisabledItemForeColor = Color.Gray,
                HelpBackColor = Bg,
                HelpForeColor = Fg,
                CommandsBackColor = Bg,
                CommandsForeColor = Fg,
            };
            _grid.PropertyValueChanged += (s, e) => _grid.Refresh();       // a preset or a texture change moves other rows

            var split = new SplitContainer { Size = new Size(760, 400), BackColor = Bg, FixedPanel = FixedPanel.Panel1 };
            split.SplitterDistance = 190;
            split.Dock = DockStyle.Fill;
            split.Panel1.Controls.Add(_list);
            split.Panel2.Controls.Add(_grid);

            var bar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 34, BackColor = Bg, Padding = new Padding(4, 4, 4, 0), WrapContents = false };
            bar.Controls.Add(MakeButton("+ Fire", 70, () => AddNew(0)));
            bar.Controls.Add(MakeButton("+ Snow", 70, () => AddNew(1)));
            bar.Controls.Add(MakeButton("+ Custom", 80, () => AddNew(2)));
            bar.Controls.Add(MakeButton("Restart", 70, RestartSelected));
            bar.Controls.Add(MakeButton("Remove", 70, RemoveSelected));

            Controls.Add(split);               // Fill first, then Top
            Controls.Add(bar);

            _timer = new System.Windows.Forms.Timer { Interval = 250 };
            _timer.Tick += (s, e) => { SyncList(); FollowEngineSelection(); };
            _timer.Start();
            Disposed += (s, e) => _timer.Dispose();
            SyncList();
        }

        private static Button MakeButton(string text, int width, Action click)
        {
            var b = new Button
            {
                Text = text,
                Width = width,
                Height = 26,
                FlatStyle = FlatStyle.Flat,
                ForeColor = Fg,
                BackColor = Bg2,
                Margin = new Padding(2, 0, 2, 0),
            };
            b.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
            b.Click += (s, e) => click();
            return b;
        }

        // ---- called by the palette after a drop / double-click ----
        public void SelectEmitter(int id)
        {
            SyncList();
            SelectInList(id);
            GhostEngine.GhostEngine_SelectEntity(id);
            _lastEngineSel = id;
        }

        public void FocusCanvas() => _list.Focus();

        private void AddNew(int type)
        {
            int id = GhostEngine.GhostEngine_AddParticleSystem(type, -1, -1);      // ground point under the screen center
            if (id == 0) { System.Media.SystemSounds.Beep.Play(); return; }        // playing, or 32 emitters already
            SelectEmitter(id);
        }

        private void RemoveSelected()
        {
            if (_list.SelectedItem is not Item it) return;
            if (GhostEngine.GhostEngine_RemoveEntity(it.Id) == 0) System.Media.SystemSounds.Beep.Play();   // (also refreshes the hierarchy)
            SyncList();
        }

        private void RestartSelected()
        {
            if (_list.SelectedItem is Item it) GhostEngine.GhostEngine_RestartParticle(it.Id);
        }

        private void ShowSelected()
        {
            _grid.SelectedObject = _list.SelectedItem is Item it ? new ParticleProxy(it.Id) : null;
        }

        private bool SelectInList(int id)
        {
            for (int i = 0; i < _list.Items.Count; i++)
                if (_list.Items[i] is Item it && it.Id == id)
                {
                    _syncing = true;
                    _list.SelectedIndex = i;
                    _syncing = false;
                    ShowSelected();
                    return true;
                }
            return false;
        }

        // Clicking an emitter in the viewport or the hierarchy selects it here too
        private void FollowEngineSelection()
        {
            int sel = GhostEngine.GhostEngine_GetSelected();
            if (sel == _lastEngineSel) return;
            _lastEngineSel = sel;
            SelectInList(sel);
        }

        // Rebuilds the list only when the emitters (or their names) changed
        private void SyncList()
        {
            int n = GhostEngine.GhostEngine_GetParticleCount();
            var ids = new int[Math.Max(n, 1)];
            n = GhostEngine.GhostEngine_GetParticleIds(ids, ids.Length);
            int[] cur = ids.Take(n).ToArray();
            string key = string.Join("|", cur.Select(id => id + ":" + GhostNames.Of(id)));
            if (key == _lastKey) return;
            _lastKey = key;

            int keep = (_list.SelectedItem as Item)?.Id ?? 0;
            _syncing = true;
            _list.BeginUpdate();
            _list.Items.Clear();
            int select = -1;
            foreach (int id in cur)
            {
                _list.Items.Add(new Item { Id = id, Text = GhostNames.Of(id) });
                if (id == keep) select = _list.Items.Count - 1;
            }
            if (select < 0 && _list.Items.Count > 0) select = 0;
            if (select >= 0) _list.SelectedIndex = select;
            _list.EndUpdate();
            _syncing = false;
            ShowSelected();
        }
    }
}