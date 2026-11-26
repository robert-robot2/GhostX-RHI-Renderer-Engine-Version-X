// GhostFoliageEditor.cs - Foliage Paint panel: layer list, brush toggles, per-layer settings (PropertyGrid)
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace GhostShell
{
    internal static class FoliageMeshes
    {
        // primitives + this project's imported models (the sprite quad is excluded: sprite layers have their own kind)
        public static List<(int id, string name)> List()
        {
            var list = new List<(int, string)>();
            GhostModels.ScanProject();
            int n = GhostEngine.GhostEngine_GetMeshCount();
            for (int i = 0; i < n; i++)
            {
                if (i == 4) continue;
                if (GhostModels.IsBuiltin(i) || GhostModels.BelongsToProject(i)) list.Add((i, GhostModels.MeshName(i)));
            }
            return list;
        }
    }

    internal sealed class FoliageMeshConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var l = new List<string>();
            foreach (var (id, name) in FoliageMeshes.List()) l.Add($"{name} [{id}]");
            return new StandardValuesCollection(l);
        }
    }

    internal sealed class FoliageFaceConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
            => new StandardValuesCollection((context?.Instance as FoliageLayerProxy)?.FaceChoices() ?? new List<string> { "All Faces" });
    }

    // One map slot (normal, roughness ...) of the selected material slot of a foliage layer
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class FoliageMapEdit
    {
        private readonly int _id, _slot; private readonly Func<int> _face;
        public FoliageMapEdit(int id, int slot, Func<int> face) { _id = id; _slot = slot; _face = face; }
        private delegate void MapChange(ref GhostMapInfo m);
        private GhostMapInfo Read() { var m = new GhostMapInfo(); GhostFoliageNative.GhostEngine_GetFoliageMap(_id, Math.Max(_face(), 0), _slot, ref m); return m; }
        private void Change(MapChange c)
        {
            int face = _face();
            var m = new GhostMapInfo();
            if (GhostFoliageNative.GhostEngine_GetFoliageMap(_id, Math.Max(face, 0), _slot, ref m) == 0) return;
            c(ref m);
            GhostFoliageNative.GhostEngine_SetFoliageMap(_id, face, _slot, ref m);        // face -1 = every slot
        }

        [DisplayName("Texture"), TypeConverter(typeof(TextureChoiceConverter))]
        public string Texture
        {
            get => GhostTextures.DisplayName(Read().textureId);
            set
            {
                int tex = value == GhostTextures.ImportLabel ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex >= 0) Change((ref GhostMapInfo m) => m.textureId = tex);
            }
        }
        [DisplayName("Amount")] public float Amount { get => Read().amount; set => Change((ref GhostMapInfo m) => m.amount = value); }
        [DisplayName("Channel"), TypeConverter(typeof(MapChannelConverter))]
        public string Channel
        {
            get => MapChannelConverter.Names[Math.Clamp(Read().channel, 0, 4)];
            set { int i = Array.IndexOf(MapChannelConverter.Names, value); if (i >= 0) Change((ref GhostMapInfo m) => m.channel = i); }
        }
        [DisplayName("Invert")] public bool Invert { get => Read().invert != 0; set => Change((ref GhostMapInfo m) => m.invert = value ? 1 : 0); }
        public override string ToString() { var m = Read(); return m.textureId == 0 ? "None" : $"{GhostTextures.DisplayName(m.textureId)}  x{m.amount:0.##}"; }
    }

    // Wind + surface extras (the wind is the material's World Position Offset sway, weighted by height in the foliage shader)
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class FoliageSurfaceEdit
    {
        private readonly int _id; private readonly Func<int> _face;
        public FoliageSurfaceEdit(int id, Func<int> face) { _id = id; _face = face; }
        private delegate void SurfChange(ref GhostSurfaceInfo s);
        private GhostSurfaceInfo Read() { var s = new GhostSurfaceInfo(); GhostFoliageNative.GhostEngine_GetFoliageSurface(_id, Math.Max(_face(), 0), ref s); return s; }
        private void Change(SurfChange c)
        {
            int face = _face();
            var s = new GhostSurfaceInfo();
            if (GhostFoliageNative.GhostEngine_GetFoliageSurface(_id, Math.Max(face, 0), ref s) == 0) return;
            c(ref s);
            GhostFoliageNative.GhostEngine_SetFoliageSurface(_id, face, ref s);
        }
        [DisplayName("Wind Sway (units)")] public float WindSway { get => Read().wpoSway; set => Change((ref GhostSurfaceInfo s) => s.wpoSway = value); }
        [DisplayName("Wind Speed")] public float WindSpeed { get => Read().wpoSpeed; set => Change((ref GhostSurfaceInfo s) => s.wpoSpeed = value); }
        [DisplayName("Two Sided")] public bool TwoSided { get => Read().twoSided != 0; set => Change((ref GhostSurfaceInfo s) => s.twoSided = value ? 1 : 0); }
        [DisplayName("Subsurface Amount (0-1)")] public float Sss { get => Read().sssAmount; set => Change((ref GhostSurfaceInfo s) => s.sssAmount = value); }
        [DisplayName("Subsurface Color")]
        public Color SssColor
        {
            get { var s = Read(); return GhostColorUtil.ToColor(s.sssR, s.sssG, s.sssB); }
            set => Change((ref GhostSurfaceInfo s) => { s.sssR = value.R / 255f; s.sssG = value.G / 255f; s.sssB = value.B / 255f; });
        }
        public override string ToString() => "(wind, two sided, subsurface)";
    }

    // The PropertyGrid view of one foliage layer
    internal sealed class FoliageLayerProxy
    {
        private readonly int _id;
        private int _face = -1;
        private readonly FoliageMapEdit[] _maps = new FoliageMapEdit[13];

        public FoliageLayerProxy(int id)
        {
            _id = id;
            for (int k = 0; k < 13; k++) _maps[k] = new FoliageMapEdit(id, k, () => _face);
            Surface = new FoliageSurfaceEdit(id, () => _face);
        }

        private GhostFoliageLayerInfo G() { GhostFoliageModel.TryGet(_id, out var i); return i; }
        private void E(GhostFoliageModel.LayerEdit e) => GhostFoliageModel.Edit(_id, e);
        private bool IsSprite => G().kind == GhostFoliageModel.KindSprite;

        // ---- material helpers (same pattern as ObjectProxy) ----
        private delegate void MatEdit(ref GhostMaterialInfo m);
        private GhostMaterialInfo ReadMat() { var m = new GhostMaterialInfo(); GhostFoliageNative.GhostEngine_GetFoliageMaterial(_id, Math.Max(_face, 0), ref m); return m; }
        private void EditMat(MatEdit c)
        {
            var m = new GhostMaterialInfo();
            if (GhostFoliageNative.GhostEngine_GetFoliageMaterial(_id, Math.Max(_face, 0), ref m) == 0) return;
            c(ref m);
            GhostFoliageNative.GhostEngine_SetFoliageMaterial(_id, _face, ref m);           // face -1 = every slot
        }
        private string FaceName(int f)
        {
            var b = new byte[64];
            GhostFoliageNative.GhostEngine_GetFoliageFaceName(_id, f, b, b.Length);
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }
        internal List<string> FaceChoices()
        {
            var l = new List<string> { "All Faces" };
            int n = GhostFoliageNative.GhostEngine_GetFoliageFaceCount(_id);
            for (int f = 0; f < n; f++) l.Add(FaceName(f));
            return l;
        }

        // ---- Details ----
        [Category("Details")]
        public string Name { get => GhostFoliageModel.Name(_id); set => GhostFoliageModel.Rename(_id, value); }
        [Category("Details")]
        public bool Enabled { get => G().enabled != 0; set => E((ref GhostFoliageLayerInfo i) => i.enabled = value ? 1 : 0); }
        [Category("Details"), DisplayName("Kind")] public string Kind => IsSprite ? "Animated Sprite" : "Mesh";
        [Category("Details"), DisplayName("Instances (chunks)")] public string Instances { get { var g = G(); return $"{g.instanceCount:N0}  ({g.chunkCount} chunks)"; } }
        [Category("Details"), DisplayName("Mesh (mesh layers)"), TypeConverter(typeof(FoliageMeshConverter))]
        public string Mesh
        {
            get { var g = G(); return $"{GhostModels.MeshName(g.meshId)} [{g.meshId}]"; }
            set
            {
                if (IsSprite) return;
                int a = value.LastIndexOf('['), b = value.LastIndexOf(']');
                if (a >= 0 && b > a && int.TryParse(value.AsSpan(a + 1, b - a - 1), out int id)) GhostFoliageNative.GhostEngine_SetFoliageSource(_id, id);
            }
        }

        // ---- Paint ----
        [Category("Paint"), DisplayName("Scatter Radius (brush)")] public float ScatterRadius { get => G().scatterRadius; set => E((ref GhostFoliageLayerInfo i) => i.scatterRadius = value); }
        [Category("Paint"), DisplayName("Amount (per stamp)")] public int Amount { get => G().amount; set => E((ref GhostFoliageLayerInfo i) => i.amount = value); }
        [Category("Paint"), DisplayName("Min Spacing")] public float MinSpacing { get => G().minSpacing; set => E((ref GhostFoliageLayerInfo i) => i.minSpacing = value); }
        [Category("Paint"), DisplayName("Size Min")] public float SizeMin { get => G().sizeMin; set => E((ref GhostFoliageLayerInfo i) => i.sizeMin = value); }
        [Category("Paint"), DisplayName("Size Max")] public float SizeMax { get => G().sizeMax; set => E((ref GhostFoliageLayerInfo i) => i.sizeMax = value); }
        [Category("Paint"), DisplayName("Rotation Range (deg, 360 = any)")] public float YawRange { get => G().yawRange; set => E((ref GhostFoliageLayerInfo i) => i.yawRange = value); }
        [Category("Paint"), DisplayName("Random Tilt (deg)")] public float TiltRandom { get => G().tiltRandom; set => E((ref GhostFoliageLayerInfo i) => i.tiltRandom = value); }
        [Category("Paint"), DisplayName("Align To Surface (0-1)")] public float AlignToNormal { get => G().alignToNormal; set => E((ref GhostFoliageLayerInfo i) => i.alignToNormal = value); }
        [Category("Paint"), DisplayName("Max Slope (deg)")] public float SlopeMax { get => G().slopeMax; set => E((ref GhostFoliageLayerInfo i) => i.slopeMax = value); }
        [Category("Paint"), DisplayName("Rest On Ground (lift by mesh base)")] public bool RestOnGround { get => G().restOnGround != 0; set => E((ref GhostFoliageLayerInfo i) => i.restOnGround = value ? 1 : 0); }
        [Category("Paint"), DisplayName("Height Offset")] public float HeightOffset { get => G().heightOffset; set => E((ref GhostFoliageLayerInfo i) => i.heightOffset = value); }

        // ---- Sprite / facing ----
        [Category("Sprite / Facing"), DisplayName("Billboard (always faces the camera)")] public bool Billboard { get => G().billboard != 0; set => E((ref GhostFoliageLayerInfo i) => i.billboard = value ? 1 : 0); }
        [Category("Sprite / Facing"), DisplayName("Sheet Columns")] public int Cols { get => G().cols; set => E((ref GhostFoliageLayerInfo i) => i.cols = value); }
        [Category("Sprite / Facing"), DisplayName("Sheet Rows")] public int Rows { get => G().rows; set => E((ref GhostFoliageLayerInfo i) => i.rows = value); }
        [Category("Sprite / Facing"), DisplayName("Animated (off = fixed random frame each)")] public bool Animated { get => G().animated != 0; set => E((ref GhostFoliageLayerInfo i) => i.animated = value ? 1 : 0); }
        [Category("Sprite / Facing"), DisplayName("Animation FPS")] public float AnimFps { get => G().animFps; set => E((ref GhostFoliageLayerInfo i) => i.animFps = value); }
        [Category("Sprite / Facing"), DisplayName("Alpha Clip (sprites)")] public bool AlphaClip { get => G().alphaClip != 0; set => E((ref GhostFoliageLayerInfo i) => i.alphaClip = value ? 1 : 0); }
        [Category("Sprite / Facing"), DisplayName("Unlit (sprites)")] public bool Unlit { get => G().unlit != 0; set => E((ref GhostFoliageLayerInfo i) => i.unlit = value ? 1 : 0); }
        [Category("Sprite / Facing"), DisplayName("Pixel Art Filter (sprites)")] public bool PixelArt { get => G().pixelArt != 0; set => E((ref GhostFoliageLayerInfo i) => i.pixelArt = value ? 1 : 0); }

        // ---- Rendering / optimization ----
        [Category("Rendering"), DisplayName("Cull Distance (0 = never)")] public float CullDistance { get => G().cullDistance; set => E((ref GhostFoliageLayerInfo i) => i.cullDistance = value); }
        [Category("Rendering"), DisplayName("Fade Length (thins out before the cull)")] public float FadeLength { get => G().fadeLength; set => E((ref GhostFoliageLayerInfo i) => i.fadeLength = value); }
        [Category("Rendering"), DisplayName("Color Variation (0-1)")] public float ColorVariation { get => G().colorVariation; set => E((ref GhostFoliageLayerInfo i) => i.colorVariation = value); }
        [Category("Rendering"), DisplayName("Lighting Normal Up Blend (0-1)")] public float NormalUp { get => G().normalUp; set => E((ref GhostFoliageLayerInfo i) => i.normalUp = value); }

        // ---- Shadows ----
        [Category("Shadows"), DisplayName("Cast Shadows (light source)"), TypeConverter(typeof(ShadowCasterConverter))]
        public string CastShadows
        {
            get
            {
                var g = G();
                if (g.castShadows == 0) return GhostShadowUI.Off;
                if (g.castShadows == 2) { string n = GhostNames.Of(g.shadowLightId); if (n.Length > 0) return n; }
                return GhostShadowUI.All;
            }
            set
            {
                int mode = 1, light = 0;
                if (value == GhostShadowUI.Off) mode = 0;
                else if (value != GhostShadowUI.All)
                    foreach (var (id, name) in GhostEntityList.Of(1)) if (name == value) { mode = 2; light = id; }
                E((ref GhostFoliageLayerInfo i) => { i.castShadows = mode; i.shadowLightId = light; });
            }
        }
        [Category("Shadows"), DisplayName("Receive Shadows (objects + landscape shadow the foliage)")]
        public bool ReceiveShadows { get => G().receiveShadows != 0; set => E((ref GhostFoliageLayerInfo i) => i.receiveShadows = value ? 1 : 0); }
        [Category("Shadows"), DisplayName("Shadow Distance (casters nearer than this)")]
        public float ShadowDistance { get => G().shadowDistance; set => E((ref GhostFoliageLayerInfo i) => i.shadowDistance = value); }

        // ---- Material (PBR) ----
        [Category("Material"), DisplayName("Material Slot"), TypeConverter(typeof(FoliageFaceConverter))]
        public string MaterialSlot
        {
            get => _face < 0 ? "All Faces" : FaceName(_face);
            set
            {
                int n = GhostFoliageNative.GhostEngine_GetFoliageFaceCount(_id);
                _face = -1;
                for (int f = 0; f < n; f++) if (FaceName(f) == value) _face = f;
            }
        }
        [Category("Material"), DisplayName("Texture (sprite sheet / base color)"), TypeConverter(typeof(TextureChoiceConverter))]
        public string Texture
        {
            get => GhostTextures.DisplayName(ReadMat().textureId);
            set
            {
                int tex = value == GhostTextures.ImportLabel ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                EditMat((ref GhostMaterialInfo m) =>
                {
                    if (m.textureId == 0 && tex != 0) m.r = m.g = m.b = 1.0f;
                    else if (tex == 0) m.r = m.g = m.b = 0.6f;
                    m.textureId = tex;
                });
            }
        }
        [Category("Material")]
        public Color Tint
        {
            get { var m = ReadMat(); return GhostColorUtil.ToColor(m.r, m.g, m.b); }
            set => EditMat((ref GhostMaterialInfo m) => { m.r = value.R / 255f; m.g = value.G / 255f; m.b = value.B / 255f; });
        }
        [Category("Material"), DisplayName("Alpha Mode"), TypeConverter(typeof(AlphaModeConverter))]
        public string AlphaMode
        {
            get => AlphaModeConverter.Names[Math.Clamp(ReadMat().alphaMode, 0, 1)];
            set { int i = Array.IndexOf(AlphaModeConverter.Names, value); if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.alphaMode = i); }
        }
        [Category("Material"), DisplayName("Alpha Source"), TypeConverter(typeof(AlphaSourceConverter))]
        public string AlphaSource
        {
            get => AlphaSourceConverter.Names[Math.Clamp(ReadMat().alphaSource, 0, 2)];
            set { int i = Array.IndexOf(AlphaSourceConverter.Names, value); if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.alphaSource = i); }
        }
        [Category("Material"), DisplayName("Alpha Cutoff (0-1)")] public float AlphaCutoff { get => ReadMat().alphaCutoff; set => EditMat((ref GhostMaterialInfo m) => m.alphaCutoff = value); }
        [Category("Surface (PBR)")] public float Metallic { get => ReadMat().metallic; set => EditMat((ref GhostMaterialInfo m) => m.metallic = value); }
        [Category("Surface (PBR)")] public float Roughness { get => ReadMat().roughness; set => EditMat((ref GhostMaterialInfo m) => m.roughness = value); }
        [Category("Surface (PBR)")] public float Specular { get => ReadMat().specular; set => EditMat((ref GhostMaterialInfo m) => m.specular = value); }
        [Category("Surface (PBR)"), DisplayName("Emissive Color")]
        public Color EmissiveColor
        {
            get { var m = ReadMat(); return GhostColorUtil.ToColor(m.emR, m.emG, m.emB); }
            set => EditMat((ref GhostMaterialInfo m) => { m.emR = value.R / 255f; m.emG = value.G / 255f; m.emB = value.B / 255f; });
        }
        [Category("Surface (PBR)"), DisplayName("Emissive Strength")] public float EmissiveStrength { get => ReadMat().emissiveStrength; set => EditMat((ref GhostMaterialInfo m) => m.emissiveStrength = value); }
        [Category("Surface (PBR)"), DisplayName("Normal Map")] public FoliageMapEdit NormalMap => _maps[0];
        [Category("Surface (PBR)"), DisplayName("Specular Map")] public FoliageMapEdit SpecularMap => _maps[1];
        [Category("Surface (PBR)"), DisplayName("Metallic Map")] public FoliageMapEdit MetallicMap => _maps[2];
        [Category("Surface (PBR)"), DisplayName("Roughness Map")] public FoliageMapEdit RoughnessMap => _maps[3];
        [Category("Surface (PBR)"), DisplayName("Ambient Occlusion")] public FoliageMapEdit AoMap => _maps[4];
        [Category("Surface (PBR)"), DisplayName("Emissive Map")] public FoliageMapEdit EmissiveMap => _maps[5];
        [Category("Surface (PBR)"), DisplayName("Opacity Mask")] public FoliageMapEdit OpacityMask => _maps[7];
        [Category("Surface (PBR)"), DisplayName("Custom Data (subsurface mask)")] public FoliageMapEdit CustomData => _maps[12];
        [Category("Surface (PBR)"), DisplayName("Wind / Advanced Surface")] public FoliageSurfaceEdit Surface { get; }
    }

    // ============================================================
    //  The dockable panel
    // ============================================================
    internal sealed class GhostFoliageEditor : UserControl
    {
        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28), Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E),
            Fg = Color.FromArgb(0xF2, 0xF2, 0xF2), Accent = Color.FromArgb(0xFF, 0xC8, 0x6E);

        private readonly ListBox _list = new();
        private readonly PropertyGrid _grid = new();
        private readonly Label _stats = new();
        private readonly CheckBox _paint = new(), _erase = new(), _land = new(), _objs = new(), _all = new();
        private readonly System.Windows.Forms.Timer _timer = new() { Interval = 200 };
        private List<int> _ids = new();
        private string _namesKey = "";
        private int _shownId = -1, _lastCount = -1;
        private bool _syncing;

        public GhostFoliageEditor()
        {
            BackColor = Bg;
            _list.Dock = DockStyle.Top; _list.Height = 110; _list.BackColor = Bg2; _list.ForeColor = Fg;
            _list.BorderStyle = BorderStyle.None; _list.IntegralHeight = false; _list.Font = new Font("Segoe UI", 10f);
            _list.SelectedIndexChanged += (s, e) => PickFromList();

            _grid.Dock = DockStyle.Fill; _grid.ToolbarVisible = false; _grid.HelpVisible = false; _grid.PropertySort = PropertySort.Categorized;
            _grid.BackColor = Bg; _grid.ViewBackColor = Bg2; _grid.ViewForeColor = Fg; _grid.LineColor = Color.FromArgb(0x3A, 0x3A, 0x40);
            _grid.CategoryForeColor = Accent; _grid.CommandsBackColor = Bg; _grid.CommandsForeColor = Fg;
            _grid.PropertyValueChanged += (s, e) => _grid.Refresh();

            _stats.Dock = DockStyle.Bottom; _stats.Height = 22; _stats.ForeColor = Color.Gray; _stats.TextAlign = ContentAlignment.MiddleLeft;
            _stats.Padding = new Padding(6, 0, 0, 0);

            var tools = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 34, BackColor = Bg, Padding = new Padding(4, 4, 0, 0) };
            MakeToggle(_paint, "Paint (O)", tools); MakeToggle(_erase, "Erase", tools);
            MakeToggle(_land, "Landscape", tools); MakeToggle(_objs, "Objects", tools); MakeToggle(_all, "Erase All Layers", tools);
            _paint.CheckedChanged += (s, e) => { if (!_syncing) GhostFoliageModel.PaintMode = _paint.Checked; };
            _erase.CheckedChanged += (s, e) => PushBrush();
            _land.CheckedChanged += (s, e) => PushBrush();
            _objs.CheckedChanged += (s, e) => PushBrush();
            _all.CheckedChanged += (s, e) => PushBrush();

            var buttons = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 34, BackColor = Bg, Padding = new Padding(4, 4, 0, 0) };
            MakeButton("+ Mesh Layer", buttons, (s, e) => ShowMeshMenu((Control)s!));
            MakeButton("+ Sprite Layer", buttons, (s, e) => ShowSpriteMenu((Control)s!));
            MakeButton("Remove Layer", buttons, (s, e) => { if (_shownId > 0 && Confirm("Remove this layer and all of its instances?")) GhostFoliageModel.Remove(_shownId); });
            MakeButton("Clear Paint", buttons, (s, e) => { if (_shownId > 0 && Confirm("Delete every instance of this layer?")) GhostFoliageNative.GhostEngine_ClearFoliageLayer(_shownId); });

            Controls.Add(_grid);              // Fill first, then Bottom, then Top (the last Top added sits highest)
            Controls.Add(_stats);
            Controls.Add(_list);
            Controls.Add(buttons);
            Controls.Add(tools);

            _timer.Tick += (s, e) => Sync();
            _timer.Start();
            Disposed += (s, e) => _timer.Stop();
        }

        private static bool Confirm(string text) => MessageBox.Show(text, "Foliage", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;

        private static void MakeToggle(CheckBox c, string text, Control host)
        {
            c.Text = text; c.Appearance = Appearance.Button; c.AutoSize = true; c.FlatStyle = FlatStyle.Flat;
            c.ForeColor = Fg; c.BackColor = Color.FromArgb(0x2A, 0x2A, 0x30); c.FlatAppearance.BorderColor = Color.FromArgb(0x55, 0x55, 0x5E);
            c.FlatAppearance.CheckedBackColor = Color.FromArgb(0x5A, 0x42, 0x1E);
            host.Controls.Add(c);
        }

        private static void MakeButton(string text, Control host, EventHandler click)
        {
            var b = new Button { Text = text, AutoSize = true, FlatStyle = FlatStyle.Flat, ForeColor = Fg, BackColor = Color.FromArgb(0x2A, 0x2A, 0x30) };
            b.FlatAppearance.BorderColor = Color.FromArgb(0x55, 0x55, 0x5E);
            b.Click += click;
            host.Controls.Add(b);
        }

        private void PushBrush()
        {
            if (_syncing) return;
            var b = GhostFoliageModel.Brush;
            b.erase = _erase.Checked ? 1 : 0; b.onLandscape = _land.Checked ? 1 : 0; b.onObjects = _objs.Checked ? 1 : 0; b.eraseAll = _all.Checked ? 1 : 0;
            GhostFoliageModel.Brush = b;
        }

        private void ShowMeshMenu(Control anchor)
        {
            var m = new ContextMenuStrip { BackColor = Bg2, ForeColor = Fg };
            foreach (var (id, name) in FoliageMeshes.List())
            {
                int meshId = id;
                m.Items.Add(name, null, (s, e) => { if (GhostFoliageModel.AddMeshLayer(meshId) == 0) System.Media.SystemSounds.Beep.Play(); });
            }
            if (m.Items.Count == 0) m.Items.Add("(no meshes)").Enabled = false;
            m.Show(anchor, new Point(0, anchor.Height));
        }

        private void ShowSpriteMenu(Control anchor)                   // pick the sprite sheet / image
        {
            var m = new ContextMenuStrip { BackColor = Bg2, ForeColor = Fg };
            foreach (string text in GhostTextures.Choices())
            {
                string t = text;
                m.Items.Add(t, null, (s, e) =>
                {
                    int tex = t == GhostTextures.ImportLabel ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(t);
                    if (tex < 0) return;
                    if (GhostFoliageModel.AddSpriteLayer(tex) == 0) System.Media.SystemSounds.Beep.Play();
                });
            }
            m.Show(anchor, new Point(0, anchor.Height));
        }

        private void PickFromList()
        {
            if (_syncing || _list.SelectedIndex < 0 || _list.SelectedIndex >= _ids.Count) return;
            GhostFoliageModel.ActiveLayer = _ids[_list.SelectedIndex];
            ShowGrid(_ids[_list.SelectedIndex]);
        }

        private void ShowGrid(int id)
        {
            _shownId = id;
            _lastCount = -1;
            _grid.SelectedObject = id > 0 && GhostFoliageModel.TryGet(id, out _) ? new FoliageLayerProxy(id) : null;
        }

        public void SelectLayer(int id) { GhostFoliageModel.ActiveLayer = id; Sync(); }

        // Every 200 ms: layer list, toggle states (O key), stats, and the instance counter row
        private void Sync()
        {
            if (!IsHandleCreated) return;
            _syncing = true;
            try
            {
                var ids = GhostFoliageModel.LayerIds();
                var sb = new StringBuilder();
                foreach (int id in ids) sb.Append(id).Append(':').Append(GhostFoliageModel.Name(id)).Append(';');
                if (sb.ToString() != _namesKey)
                {
                    _namesKey = sb.ToString(); _ids = ids;
                    _list.Items.Clear();
                    foreach (int id in ids) _list.Items.Add(GhostFoliageModel.Name(id));
                }
                int active = GhostFoliageModel.ActiveLayer;
                int idx = _ids.IndexOf(active);
                if (_list.SelectedIndex != idx) _list.SelectedIndex = idx;
                if (_shownId != active) ShowGrid(active);

                var b = GhostFoliageModel.Brush;
                _paint.Checked = b.active != 0; _erase.Checked = b.erase != 0; _land.Checked = b.onLandscape != 0;
                _objs.Checked = b.onObjects != 0; _all.Checked = b.eraseAll != 0;

                GhostFoliageNative.GhostEngine_GetFoliageStats(out var st);
                _stats.Text = $"{st.layers} layers   {st.instances:N0} instances   {st.chunks} chunks   drawn {st.chunksDrawn} chunks / {st.instancesDrawn:N0}   shadow chunks {st.shadowChunks}";

                if (_shownId > 0 && GhostFoliageModel.TryGet(_shownId, out var li) && li.instanceCount != _lastCount)
                { _lastCount = li.instanceCount; _grid.Refresh(); }
            }
            finally { _syncing = false; }
        }
    }
}