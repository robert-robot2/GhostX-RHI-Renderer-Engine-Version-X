// GhostTileMapEdit.cs - Details panel for Maps > Tile Map (transform, grid, shadows, material layers)
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Design;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class TileDirectionConverter : FixedListConverter
    {
        public static readonly string[] Names =
            { "X (wall, faces +X)", "Y (floor, faces up)", "Z (wall, faces the default camera)" };
        protected override string[] Items => Names;
    }

    // "All Lights" + every directional light in the scene
    internal sealed class DirectionalLightChoiceConverter : StringConverter
    {
        public const string AllLights = "All Lights";
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
        {
            var list = new List<string> { AllLights };
            foreach (var (id, name) in GhostEntityList.Of(1))
            {
                var l = new GhostLightInfo();
                if (GhostEngine.GhostEngine_GetLight(id, ref l) != 0 && l.type == 0) list.Add(name);
            }
            return new StandardValuesCollection(list);
        }
    }

    internal sealed class TileLayerActionConverter : FixedListConverter
    {
        public const string Add = "Add Material Layer", Remove = "Remove Active Layer";
        public static readonly string[] Names = { "(choose)", Add, Remove };
        protected override string[] Items => Names;
    }

    internal sealed class TileAssignConverter : FixedListConverter
    {
        public const string AllTiles = "Active Layer -> All Tiles", RegionOnly = "Active Layer -> Region Below";
        public static readonly string[] Names = { "(choose)", AllTiles, RegionOnly };
        protected override string[] Items => Names;
    }

    // "All Layers" + Layer 0..n-1 of the Tile Map being edited
    internal sealed class TileLayerChoiceConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context)
            => new StandardValuesCollection((context?.Instance as TileMapProxy)?.LayerChoices() ?? new List<string> { "All Layers" });
    }

    // Paints a tiny thumbnail of the texture in the little box left of the value (texture rows and map rows)
    internal sealed class TextureThumbEditor : UITypeEditor
    {
        public override UITypeEditorEditStyle GetEditStyle(ITypeDescriptorContext? context) => UITypeEditorEditStyle.None;
        public override bool GetPaintValueSupported(ITypeDescriptorContext? context) => true;
        public override void PaintValue(PaintValueEventArgs e)
        {
            int id = e.Value switch { string s => GhostTextures.IdFromDisplay(s), MapEdit m => m.TextureId, _ => -1 };
            Rectangle r = e.Bounds;
            using (var bg = new SolidBrush(Color.FromArgb(0x1E, 0x1E, 0x1E))) e.Graphics.FillRectangle(bg, r);
            TextureAsset? a = id > 0 ? GhostAssets.Get(id) : null;
            if (a != null) e.Graphics.DrawImage(a.Thumb, r);
            e.Graphics.DrawRectangle(Pens.Gray, r.X, r.Y, r.Width - 1, r.Height - 1);
        }
    }
    internal sealed class TerrainDetailConverter : FixedListConverter
    {
        public static readonly string[] Names = { "1 (default)", "2", "4 (fine)", "8 (very fine)" };
        public static readonly int[] Values = { 1, 2, 4, 8 };
        protected override string[] Items => Names;
    }

    internal sealed class TerrainActionConverter : FixedListConverter
    {
        public static readonly string[] Names =
            { "(choose)", "Flatten Terrain (all heights = 0)", "Reset Paint (Layer 0 everywhere)", "Reset Both" };
        protected override string[] Items => Names;
    }

    internal sealed class TileShadingConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Smooth", "Flat" };
        protected override string[] Items => Names;
    }


    // ============================================================
    //  A Tile Map with no material layers yet
    // ============================================================
    internal class TileMapProxy
    {
        protected const int MaxLayers = 8;                 // keep in step with GHOST_TILEMAP_MAX_LAYERS
        protected readonly int _id;

        public TileMapProxy(int id)
        {
            _id = id;
            Position = new Vec3Edit(
                () => { var i = GetObj(); return (i.px, i.py, i.pz); },
                (x, y, z) => { var i = GetObj(); i.px = x; i.py = y; i.pz = z; SetObj(i); });
            Rotation = new Vec3Edit(
                () => { var i = GetObj(); return (i.rx, i.ry, i.rz); },
                (x, y, z) => { var i = GetObj(); i.rx = x; i.ry = y; i.rz = z; SetObj(i); });
            Scale = new Vec3Edit(
                () => { var i = GetObj(); return (i.sx, i.sy, i.sz); },
                (x, y, z) => { var i = GetObj(); i.sx = x; i.sy = y; i.sz = z; SetObj(i); });
            ColliderSize = new Vec3Edit(
    () => { var g = GetG(); return (g.csx, g.csy, g.csz); },
    (x, y, z) => { var g = GetG(); g.csx = x; g.csy = y; g.csz = z; SetG(g); });
            ColliderOffset = new Vec3Edit(
                () => { var g = GetG(); return (g.cox, g.coy, g.coz); },
                (x, y, z) => { var g = GetG(); g.cox = x; g.coy = y; g.coz = z; SetG(g); });
        }

        private GhostObjectInfo GetObj() { var i = new GhostObjectInfo(); GhostEngine.GhostEngine_GetObject(_id, ref i); return i; }
        private void SetObj(GhostObjectInfo i) => GhostEngine.GhostEngine_SetObject(ref i);
        protected GhostTileMapInfo Get() { var t = new GhostTileMapInfo(); GhostEngine.GhostEngine_GetTileMap(_id, ref t); return t; }
        protected void Set(GhostTileMapInfo t) => GhostEngine.GhostEngine_SetTileMap(ref t);

        protected GhostGameplayInfo GetG() { var g = new GhostGameplayInfo(); GhostEngine.GhostEngine_GetGameplay(_id, ref g); return g; }
        protected void SetG(GhostGameplayInfo g) => GhostEngine.GhostEngine_SetGameplay(ref g);

        [Browsable(false)] public virtual bool HasLayers => false;
        internal bool LayersOutOfDate() => HasLayers != (Get().layerCount > 0);   // the editor rebuilds the panel when this flips

        internal List<string> LayerChoices()
        {
            var list = new List<string> { "All Layers" };
            int n = Get().layerCount;
            for (int l = 0; l < n; l++) list.Add($"Layer {l}");
            return list;
        }

        [Category("Details")] public string Name => GhostNames.Of(_id);

        [Category("Transform")] public Vec3Edit Position { get; }
        [Category("Transform"), DisplayName("Rotation (deg)")] public Vec3Edit Rotation { get; }
        [Category("Transform")] public Vec3Edit Scale { get; }

        // ---- Tile Map ----
        [Category("Tile Map"), DisplayName("Tile Size (units)")]
        public float TileSize { get => Get().tileSize; set { var t = Get(); t.tileSize = value; Set(t); } }

        [Category("Tile Map"), DisplayName("Grid Width (tiles, 1-256)")]
        public int GridWidth { get => Get().gridX; set { var t = Get(); t.gridX = value; Set(t); } }

        [Category("Tile Map"), DisplayName("Grid Height (tiles, 1-256)")]
        public int GridHeight { get => Get().gridY; set { var t = Get(); t.gridY = value; Set(t); } }

        [Category("Tile Map"), DisplayName("Grid Direction")]
        [TypeConverter(typeof(TileDirectionConverter))]
        public string GridDirection
        {
            get => TileDirectionConverter.Names[Math.Clamp(Get().direction, 0, 2)];
            set { int i = Array.IndexOf(TileDirectionConverter.Names, value); if (i >= 0) { var t = Get(); t.direction = i; Set(t); } }
        }

        [Category("Tile Map"), DisplayName("Size (read-only)")]
        public string TotalSize
        {
            get { var t = Get(); return $"{t.gridX * t.tileSize:0.##} x {t.gridY * t.tileSize:0.##} units, {t.gridX * t.gridY:N0} tiles"; }
        }

        // ---- Terrain (sculpt + paint) ----
        [Category("Terrain"), DisplayName("Paint Mode (P): sculpt and paint with the brush")]
        public bool PaintMode
        {
            get => GhostEngine.GhostEngine_GetPaintMode() != 0;
            set => GhostEngine.GhostEngine_SetPaintMode(value ? 1 : 0);
        }

        [Category("Terrain"), DisplayName("Terrain Detail (quads per tile, lowered on big maps)")]
        [TypeConverter(typeof(TerrainDetailConverter))]
        public string TerrainDetail
        {
            get { int i = Array.IndexOf(TerrainDetailConverter.Values, Get().subdiv); return TerrainDetailConverter.Names[i < 0 ? 0 : i]; }
            set { int i = Array.IndexOf(TerrainDetailConverter.Names, value); if (i >= 0) { var t = Get(); t.subdiv = TerrainDetailConverter.Values[i]; Set(t); } }
        }

        [Category("Terrain"), DisplayName("Terrain Actions")]
        [TypeConverter(typeof(TerrainActionConverter))]
        public string TerrainActions
        {
            get => TerrainActionConverter.Names[0];
            set { int i = Array.IndexOf(TerrainActionConverter.Names, value); if (i > 0) GhostEngine.GhostEngine_ResetTerrain(_id, i); }   // 1 heights, 2 weights, 3 both
        }

        [Category("Terrain"), DisplayName("Edge Skirts (hides LOD cracks between cells)")]
        public bool EdgeSkirts
        {
            get => Get().skirts != 0;
            set { var t = Get(); t.skirts = value ? 1 : 0; Set(t); }
        }

        [Category("Terrain"), DisplayName("Shading")]
        [TypeConverter(typeof(TileShadingConverter))]
        public string Shading
        {
            get => Get().flatShade != 0 ? "Flat" : "Smooth";
            set { int i = Array.IndexOf(TileShadingConverter.Names, value); if (i >= 0) { var t = Get(); t.flatShade = i; Set(t); } }
        }

        // ---- Material layers ----
        [Category("Material Layers"), DisplayName("Layer Actions")]
        [TypeConverter(typeof(TileLayerActionConverter))]
        public string LayerActions
        {
            get => TileLayerActionConverter.Names[0];
            set
            {
                if (value == TileLayerActionConverter.Add)
                {
                    int n = GhostEngine.GhostEngine_AddTileLayer(_id);
                    if (n < 0)
                        MessageBox.Show($"A Tile Map has up to {MaxLayers} material layers for now.", "Material layers",
                            MessageBoxButtons.OK, MessageBoxIcon.Information);
                    else GhostEngine.GhostEngine_SetTargetFace(n);               // the new layer becomes the Active Layer
                }
                else if (value == TileLayerActionConverter.Remove)
                {
                    int f = GhostEngine.GhostEngine_GetTargetFace();
                    if (f < 0)
                        MessageBox.Show("Pick one layer in Active Layer first (not All Layers).", "Material layers",
                            MessageBoxButtons.OK, MessageBoxIcon.Information);
                    else GhostEngine.GhostEngine_RemoveTileLayer(_id, f);        // its tiles fall back to Layer 0
                }
            }
        }

        [Category("Material Layers"), DisplayName("Layers (tiles using each)")]
        public string LayerSummary
        {
            get
            {
                var c = new int[MaxLayers];
                int n = GhostEngine.GhostEngine_GetTileLayerCounts(_id, c, c.Length);
                if (n == 0) return "none (default checker). Layer Actions > Add Material Layer";
                var parts = new List<string>();
                for (int l = 0; l < n && l < c.Length; l++) parts.Add($"L{l}: {c[l]:N0}");
                return $"{n} of {MaxLayers}   " + string.Join("   ", parts);
            }
        }

        // ---- Landscape Shadows ----
        [Category("Landscape Shadows"), DisplayName("Landscape Shadows (objects shadow the landscape)")]
        public bool LandscapeShadows
        {
            get => Get().receiveShadows != 0;
            set { var t = Get(); t.receiveShadows = value ? 1 : 0; Set(t); }
        }

        [Category("Landscape Shadows"), DisplayName("Assigned Directional Light")]
        [TypeConverter(typeof(DirectionalLightChoiceConverter))]
        public string AssignedLight
        {
            get
            {
                int lid = Get().shadowLightId;
                if (lid == 0) return DirectionalLightChoiceConverter.AllLights;
                string n = GhostNames.Of(lid);
                return n.Length > 0 ? n : DirectionalLightChoiceConverter.AllLights;
            }
            set
            {
                int id = 0;
                if (value != DirectionalLightChoiceConverter.AllLights)
                    foreach (var (lid, name) in GhostEntityList.Of(1)) if (name == value) id = lid;
                var t = Get(); t.shadowLightId = id; Set(t);
            }
        }

        [Category("Landscape Shadows"), DisplayName("Cast Shadows (landscape onto objects)")]
        [TypeConverter(typeof(ShadowCasterConverter))]
        public string CastShadows
        {
            get => GhostShadowUI.GetCast(_id);
            set => GhostShadowUI.SetCast(_id, value);
        }

        // ---- Collision ----
        [Category("Collision")]
        public bool Collider
        {
            get => GetG().collider != 0;
            set { var g = GetG(); g.collider = value ? 1 : 0; SetG(g); }
        }

        [Category("Collision"), DisplayName("Auto-Fit To Terrain (follows sculpting)")]
        public bool AutoFitCollider
        {
            get => Get().autoCollider != 0;
            set { var t = Get(); t.autoCollider = value ? 1 : 0; Set(t); }
        }

        [Category("Collision"), DisplayName("Collider Size (Auto-Fit off)")]
        public Vec3Edit ColliderSize { get; }

        [Category("Collision"), DisplayName("Collider Offset (Auto-Fit off)")]
        public Vec3Edit ColliderOffset { get; }

    }

    // ============================================================
    //  A Tile Map with at least one layer: adds the layer editor
    // ============================================================
    internal sealed class TileMapLayerProxy : TileMapProxy
    {
        private const string CatAssign = "Material Layers: Assign To Tiles";
        private const string CatTex = "Material Layers: Texture";
        private const string CatPbr = "Material Layers: PBR Surface";

        private static readonly Dictionary<int, int[]> _regions = new();   // per Tile Map: column0, row0, column1, row1 (editor only)
        private readonly MapEdit[] _maps = new MapEdit[13];

        public TileMapLayerProxy(int id) : base(id)
        {
            if (GhostEngine.GhostEngine_GetTargetFace() < 0) GhostEngine.GhostEngine_SetTargetFace(0);   // start on Layer 0, not "All Layers"

            Tiling = new Vec2Edit(
                () => { var m = ReadMat(); return (m.tilingU, m.tilingV); },
                (u, v) => EditMat((ref GhostMaterialInfo m) => { m.tilingU = u; m.tilingV = v; }));
            Offset = new Vec2Edit(
                () => { var m = ReadMat(); return (m.offsetU, m.offsetV); },
                (u, v) => EditMat((ref GhostMaterialInfo m) => { m.offsetU = u; m.offsetV = v; }));
            for (int k = 0; k < 13; k++) _maps[k] = new MapEdit(id, k);
            SurfaceExtras = new SurfaceEdit(id);
        }

        [Browsable(false)] public override bool HasLayers => true;

        // ---- the layer being edited (the engine's "target face") ----
        private delegate void MatEdit(ref GhostMaterialInfo m);

        private GhostMaterialInfo ReadMat()
        {
            var m = new GhostMaterialInfo();
            GhostEngine.GhostEngine_GetMaterial(_id, Math.Max(GhostEngine.GhostEngine_GetTargetFace(), 0), ref m);
            return m;
        }

        private void EditMat(MatEdit edit)            // the Active Layer, or every layer when it is "All Layers"
        {
            int face = GhostEngine.GhostEngine_GetTargetFace();
            int count = GhostEngine.GhostEngine_GetFaceCount(_id);
            int from = face < 0 ? 0 : face, to = face < 0 ? count - 1 : face;
            for (int f = from; f <= to; f++)
            {
                var m = new GhostMaterialInfo();
                if (GhostEngine.GhostEngine_GetMaterial(_id, f, ref m) == 0) continue;
                edit(ref m);
                GhostEngine.GhostEngine_SetMaterial(_id, f, ref m);
            }
        }

        [Category("Material Layers"), DisplayName("Active Layer (edited below)")]
        [TypeConverter(typeof(TileLayerChoiceConverter))]
        public string ActiveLayer
        {
            get { int f = GhostEngine.GhostEngine_GetTargetFace(); return f < 0 ? "All Layers" : $"Layer {f}"; }
            set
            {
                if (value == "All Layers") { GhostEngine.GhostEngine_SetTargetFace(-1); return; }
                if (value.StartsWith("Layer ") && int.TryParse(value.AsSpan(6), out int f)) GhostEngine.GhostEngine_SetTargetFace(f);
            }
        }

        // ---- assigning the Active Layer to tiles (painting comes later) ----
        private int[] Rg()
        {
            if (!_regions.TryGetValue(_id, out int[]? r)) { r = new int[4]; _regions[_id] = r; }
            return r;
        }

        [Category(CatAssign), DisplayName("From Column (0 = first)")] public int FromColumn { get => Rg()[0]; set => Rg()[0] = Math.Max(value, 0); }
        [Category(CatAssign), DisplayName("From Row (0 = first)")] public int FromRow { get => Rg()[1]; set => Rg()[1] = Math.Max(value, 0); }
        [Category(CatAssign), DisplayName("To Column")] public int ToColumn { get => Rg()[2]; set => Rg()[2] = Math.Max(value, 0); }
        [Category(CatAssign), DisplayName("To Row")] public int ToRow { get => Rg()[3]; set => Rg()[3] = Math.Max(value, 0); }

        [Category(CatAssign), DisplayName("Assign Active Layer")]
        [TypeConverter(typeof(TileAssignConverter))]
        public string Assign
        {
            get => TileAssignConverter.Names[0];
            set
            {
                if (value != TileAssignConverter.AllTiles && value != TileAssignConverter.RegionOnly) return;
                int layer = GhostEngine.GhostEngine_GetTargetFace();
                if (layer < 0)
                {
                    MessageBox.Show("Pick one layer in Active Layer first (not All Layers).", "Assign layer",
                        MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }
                var t = Get();
                var r = Rg();
                if (value == TileAssignConverter.AllTiles) GhostEngine.GhostEngine_AssignTileLayer(_id, layer, 0, 0, t.gridX - 1, t.gridY - 1);
                else GhostEngine.GhostEngine_AssignTileLayer(_id, layer, r[0], r[1], r[2], r[3]);
            }
        }

        // ---- base texture ----
        [Category(CatTex), DisplayName("Texture (base color)")]
        [TypeConverter(typeof(TextureChoiceConverter))]
        [Editor(typeof(TextureThumbEditor), typeof(UITypeEditor))]
        public string Texture
        {
            get => GhostTextures.DisplayName(ReadMat().textureId);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                EditMat((ref GhostMaterialInfo m) =>
                {
                    if (m.textureId == 0 && tex != 0) m.r = m.g = m.b = 1.0f;   // white tint so the image shows at full strength
                    else if (tex == 0) m.r = m.g = m.b = 0.6f;                  // back to the default grey
                    m.textureId = tex;
                });
            }
        }

        [Category(CatTex), DisplayName("Image Size")] public string ImageSize => GhostTextures.SizeText(ReadMat().textureId);

        [Category(CatTex)]
        public Color Tint
        {
            get { var m = ReadMat(); return GhostColorUtil.ToColor(m.r, m.g, m.b); }
            set => EditMat((ref GhostMaterialInfo m) => { m.r = value.R / 255f; m.g = value.G / 255f; m.b = value.B / 255f; });
        }

        [Category(CatTex), DisplayName("Mapping (Mesh UV = whole texture per tile)")]
        [TypeConverter(typeof(MapModeConverter))]
        public string Mapping
        {
            get => MapModeConverter.Names[Math.Clamp(ReadMat().mapMode, 0, MapModeConverter.Names.Length - 1)];
            set { int i = Array.IndexOf(MapModeConverter.Names, value); if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.mapMode = i); }
        }

        [Category(CatTex)] public Vec2Edit Tiling { get; }
        [Category(CatTex)] public Vec2Edit Offset { get; }

        [Category(CatTex), DisplayName("UV Rotation (deg)")]
        public float UVRotation { get => ReadMat().rotationDeg; set => EditMat((ref GhostMaterialInfo m) => m.rotationDeg = value); }

        [Category(CatTex)]
        [TypeConverter(typeof(WrapConverter))]
        public string Wrap
        {
            get => WrapConverter.Names[Math.Clamp(ReadMat().wrap, 0, 1)];
            set { int i = Array.IndexOf(WrapConverter.Names, value); if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.wrap = i); }
        }

        [Category(CatTex)]
        [TypeConverter(typeof(FilterConverter))]
        public string Filter
        {
            get => FilterConverter.Names[Math.Clamp(ReadMat().filter, 0, 1)];
            set { int i = Array.IndexOf(FilterConverter.Names, value); if (i >= 0) EditMat((ref GhostMaterialInfo m) => m.filter = i); }
        }

        // ---- PBR values ----
        [Category(CatPbr)]
        [TypeConverter(typeof(MaterialPresetConverter))]
        public string Preset
        {
            get
            {
                var m = ReadMat();
                foreach (MatPreset p in MaterialPresetConverter.All) if (p.Matches(m)) return p.Name;
                return "Custom";
            }
            set
            {
                foreach (MatPreset p in MaterialPresetConverter.All)
                    if (p.Name == value) { MatPreset chosen = p; EditMat((ref GhostMaterialInfo m) => chosen.ApplyTo(ref m)); return; }
            }
        }

        [Category(CatPbr)] public float Metallic { get => ReadMat().metallic; set => EditMat((ref GhostMaterialInfo m) => m.metallic = value); }
        [Category(CatPbr)] public float Roughness { get => ReadMat().roughness; set => EditMat((ref GhostMaterialInfo m) => m.roughness = value); }
        [Category(CatPbr)] public float Specular { get => ReadMat().specular; set => EditMat((ref GhostMaterialInfo m) => m.specular = value); }

        [Category(CatPbr), DisplayName("Emissive Color")]
        public Color EmissiveColor
        {
            get { var m = ReadMat(); return GhostColorUtil.ToColor(m.emR, m.emG, m.emB); }
            set => EditMat((ref GhostMaterialInfo m) => { m.emR = value.R / 255f; m.emG = value.G / 255f; m.emB = value.B / 255f; });
        }

        [Category(CatPbr), DisplayName("Emissive Strength")]
        public float EmissiveStrength { get => ReadMat().emissiveStrength; set => EditMat((ref GhostMaterialInfo m) => m.emissiveStrength = value); }

        [Category(CatPbr), DisplayName("Normal Map")] public MapEdit NormalMap => _maps[0];
        [Category(CatPbr), DisplayName("Specular Map")] public MapEdit SpecularMap => _maps[1];
        [Category(CatPbr), DisplayName("Metallic Map")] public MapEdit MetallicMap => _maps[2];
        [Category(CatPbr), DisplayName("Roughness Map")] public MapEdit RoughnessMap => _maps[3];
        [Category(CatPbr), DisplayName("Ambient Occlusion")] public MapEdit AmbientOcclusionMap => _maps[4];
        [Category(CatPbr), DisplayName("Emissive Map")] public MapEdit EmissiveMap => _maps[5];
        [Category(CatPbr), DisplayName("Opacity Map")] public MapEdit OpacityMap => _maps[6];
        [Category(CatPbr), DisplayName("Opacity Mask")] public MapEdit OpacityMaskMap => _maps[7];
        [Category(CatPbr), DisplayName("Tangent Map")] public MapEdit TangentMap => _maps[8];
        [Category(CatPbr), DisplayName("World Position Offset")] public MapEdit WpoMap => _maps[9];
        [Category(CatPbr), DisplayName("Refraction Map")] public MapEdit RefractionMap => _maps[10];
        [Category(CatPbr), DisplayName("Reflection Map (environment)")] public MapEdit ReflectionMap => _maps[11];
        [Category(CatPbr), DisplayName("Custom Data (subsurface mask)")] public MapEdit CustomDataMap => _maps[12];
        [Category(CatPbr), DisplayName("Advanced Surface")] public SurfaceEdit SurfaceExtras { get; }
    }
}