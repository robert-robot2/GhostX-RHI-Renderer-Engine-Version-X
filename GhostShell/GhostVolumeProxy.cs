// GhostVolumeProxy.cs — Details panel for Utilities volumes
using System.ComponentModel;
using System.Drawing;

namespace GhostShell
{
    internal class VolumeShapeConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Box", "Sphere" };
        protected override string[] Items => Names;
    }

    internal abstract class VolumeProxy
    {
        protected readonly int Id;
        protected delegate void VolChange(ref GhostVolumeInfo v);

        public static VolumeProxy Create(int id, int type) => type switch
        {
            0 => new ReflectionCaptureProxy(id),
            2 => new PostProcessVolumeProxy(id),
            _ => new FogVolumeProxy(id),
        };

        protected VolumeProxy(int id)
        {
            Id = id;
            Position = new Vec3Edit(() => { var v = Get(); return (v.px, v.py, v.pz); },
                (x, y, z) => Change((ref GhostVolumeInfo v) => { v.px = x; v.py = y; v.pz = z; }));
            Rotation = new Vec3Edit(() => { var v = Get(); return (v.rx, v.ry, v.rz); },
                (x, y, z) => Change((ref GhostVolumeInfo v) => { v.rx = x; v.ry = y; v.rz = z; }));
            BoxSize = new Vec3Edit(() => { var v = Get(); return (v.sx, v.sy, v.sz); },
                (x, y, z) => Change((ref GhostVolumeInfo v) => { v.sx = x; v.sy = y; v.sz = z; }));
        }

        protected GhostVolumeInfo Get() { GhostEngine.GhostEngine_GetVolume(Id, out GhostVolumeInfo v); return v; }
        protected void Change(VolChange c)
        {
            GhostEngine.GhostEngine_GetVolume(Id, out GhostVolumeInfo v);
            c(ref v);
            GhostEngine.GhostEngine_SetVolume(ref v);
        }

        [Category("Details")] public string Name => GhostNames.Of(Id);
        [Category("Details")] public string Type => Get().type switch { 0 => "Reflection Capture", 2 => "Post Process Volume", _ => "Fog Volume" };

        [Category("Volume")]
        public bool Enabled
        {
            get => Get().enabled != 0;
            set => Change((ref GhostVolumeInfo v) => v.enabled = value ? 1 : 0);
        }

        [Category("Transform")] public Vec3Edit Position { get; }
        [Category("Transform"), DisplayName("Rotation (deg)")] public Vec3Edit Rotation { get; }

        [Category("Volume"), TypeConverter(typeof(VolumeShapeConverter))]
        public string Shape
        {
            get => VolumeShapeConverter.Names[Get().shape == 0 ? 0 : 1];
            set { int i = System.Array.IndexOf(VolumeShapeConverter.Names, value); if (i >= 0) Change((ref GhostVolumeInfo v) => v.shape = i); }
        }

        [Category("Volume"), DisplayName("Box Size (Shape = Box)")] public Vec3Edit BoxSize { get; }

        [Category("Volume"), DisplayName("Radius (Shape = Sphere)")]
        public float Radius { get => Get().radius; set => Change((ref GhostVolumeInfo v) => v.radius = value); }

        [Category("Volume"), DisplayName("Blend Distance")]
        public float BlendDistance { get => Get().blend; set => Change((ref GhostVolumeInfo v) => v.blend = value); }

        [Category("Volume"), DisplayName("Priority (higher wins)")]
        public int Priority { get => Get().priority; set => Change((ref GhostVolumeInfo v) => v.priority = value); }
    }

    // Phase 4 adds capture parameters here (resolution, source: sky / sun / environment, box projection ...)
    internal sealed class ReflectionCaptureProxy : VolumeProxy
    {
        public ReflectionCaptureProxy(int id) : base(id) { }

        private float P(int i) => Get().p[i];
        private void SetP(int i, float x) => Change((ref GhostVolumeInfo v) => v.p[i] = x);

        [Category("Capture"), DisplayName("Status")]
        public string Status => GhostEngine.GhostEngine_GetProbeStatus(Id) switch
        {
            1 => "Captured",
            0 => "Waiting to capture...",
            2 => "No free probe slot (max 8 enabled probes)",
            3 => "Unavailable (probe system failed to start)",
            4 => "Disabled",
            _ => "-"
        };

        [Category("Capture"), DisplayName("Capture Action")]
        [TypeConverter(typeof(ProbeActionConverter))]
        public string CaptureAction
        {
            get => ProbeActionConverter.Names[0];
            set { if (value == "Capture Now") GhostEngine.GhostEngine_CaptureProbe(Id); }
        }

        [Category("Capture"), DisplayName("Update Mode")]
        [TypeConverter(typeof(ProbeModeConverter))]
        public string UpdateMode
        {
            get => ProbeModeConverter.Names[P(0) > 0.5f ? 1 : 0];
            set { int i = System.Array.IndexOf(ProbeModeConverter.Names, value); if (i >= 0) SetP(0, i); }
        }

        [Category("Capture"), DisplayName("Realtime Interval (s, 0.05+)")]
        public float RealtimeInterval { get => P(2); set => SetP(2, value); }

        [Category("Capture"), DisplayName("Resolution")]
        [TypeConverter(typeof(ProbeResConverter))]
        public string Resolution
        {
            get => ProbeResConverter.Names[System.Math.Clamp((int)(P(1) + 0.5f), 0, 2)];
            set { int i = System.Array.IndexOf(ProbeResConverter.Names, value); if (i >= 0) SetP(1, i); }
        }

        [Category("Capture"), DisplayName("Source")]
        [TypeConverter(typeof(ProbeSourceConverter))]
        public string Source
        {
            get => ProbeSourceConverter.Names[System.Math.Clamp((int)(P(7) + 0.5f), 0, 2)];
            set { int i = System.Array.IndexOf(ProbeSourceConverter.Names, value); if (i >= 0) SetP(7, i); }
        }

        [Category("Capture"), DisplayName("Custom Texture (Source = Custom texture)")]
        [TypeConverter(typeof(TextureChoiceConverter))]
        public string EnvTexture
        {
            get => GhostTextures.DisplayName(Get().tex[0]);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                Change((ref GhostVolumeInfo v) => v.tex[0] = tex);
            }
        }

        [Category("Capture"), DisplayName("Texture Rotation (deg)")]
        public float TextureRotation { get => P(8); set => SetP(8, value); }

        [Category("Capture"), DisplayName("Near Clip")]
        public float NearClip { get => P(4); set => SetP(4, value); }

        [Category("Capture"), DisplayName("Far Clip")]
        public float FarClip { get => P(5); set => SetP(5, value); }

        [Category("Lighting"), DisplayName("Intensity (0-10)")]
        public float Intensity { get => P(3); set => SetP(3, value); }

        [Category("Lighting"), DisplayName("Parallax Correction (uses the volume as the room)")]
        public bool Parallax { get => P(6) > 0.5f; set => SetP(6, value ? 1f : 0f); }

        [Category("Lighting"), DisplayName("Ambient Diffuse (0-1)")]
        public float AmbientDiffuse { get => P(9); set => SetP(9, value); }
    }

    internal sealed class FogVolumeProxy : VolumeProxy
    {
        public FogVolumeProxy(int id) : base(id) { }

        private float P(int i) => Get().p[i];
        private void SetP(int i, float x) => Change((ref GhostVolumeInfo v) => v.p[i] = x);

        [Category("Fog"), DisplayName("Density (0-5, try 0.15)")]
        public float Density { get => P(0); set => SetP(0, value); }

        [Category("Fog"), DisplayName("Color")]
        public Color FogColor
        {
            get { var v = Get(); return GhostColorUtil.ToColor(v.p[1], v.p[2], v.p[3]); }
            set => Change((ref GhostVolumeInfo v) => { v.p[1] = value.R / 255f; v.p[2] = value.G / 255f; v.p[3] = value.B / 255f; });
        }

        [Category("Fog"), DisplayName("Gradient (0 = uniform, 1 = fades out toward the top)")]
        public float Gradient { get => P(4); set => SetP(4, value); }

        [Category("Fog"), DisplayName("God Rays Scatter (0-2)")]
        public float LightScatter { get => P(5); set => SetP(5, value); }

        [Category("Fog Noise"), DisplayName("Noise Texture")]
        [TypeConverter(typeof(TextureChoiceConverter))]
        public string NoiseTexture
        {
            get => GhostTextures.DisplayName(Get().tex[0]);
            set
            {
                int tex = (value == GhostTextures.ImportLabel) ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(value);
                if (tex < 0) return;
                Change((ref GhostVolumeInfo v) => v.tex[0] = tex);
            }
        }

        [Category("Fog Noise"), DisplayName("Noise Amount (0-1)")]
        public float NoiseAmount { get => P(6); set => SetP(6, value); }

        [Category("Fog Noise"), DisplayName("Noise Scale")]
        public float NoiseScale { get => P(7); set => SetP(7, value); }

        [Category("Fog Noise"), DisplayName("Wind X (units/s)")]
        public float WindX { get => P(8); set => SetP(8, value); }

        [Category("Fog Noise"), DisplayName("Wind Z (units/s)")]
        public float WindZ { get => P(9); set => SetP(9, value); }

        [Category("Details")] public string Info => "Adds to the global fog. Use Blend Distance for soft edges.";
    }

    internal sealed class PostProcessVolumeProxy : VolumeProxy
    {
        private const int O_EXPOSURE = 1, O_BLOOM = 2, O_SSAO = 4, O_SSGI = 8, O_SSR = 16, O_SHAFTS = 32, O_TONE = 64;

        public PostProcessVolumeProxy(int id) : base(id) { }

        private float P(int i) => Get().p[i];
        private void SetP(int i, float x) => Change((ref GhostVolumeInfo v) => v.p[i] = x);
        private bool Bit(int b) => (((int)(P(1) + 0.5f)) & b) != 0;
        private void SetBit(int b, bool on) => Change((ref GhostVolumeInfo v) =>
        {
            int m = (int)(v.p[1] + 0.5f);
            m = on ? (m | b) : (m & ~b);
            v.p[1] = m;
        });

        [Category("Post Process"), DisplayName("Blend Weight (0-1)")]
        public float Weight { get => P(0); set => SetP(0, value); }

        [Category("Post Process"), DisplayName("Unbound (whole scene, ignores shape and blend distance)")]
        public bool Unbound { get => P(12) > 0.5f; set => SetP(12, value ? 1f : 0f); }

        [Category("Exposure"), DisplayName("Override Exposure")]
        public bool OverrideExposure { get => Bit(O_EXPOSURE); set => SetBit(O_EXPOSURE, value); }
        [Category("Exposure"), DisplayName("Exposure (EV stops, -5 to 5)")]
        public float Exposure { get => P(2); set => SetP(2, value); }

        [Category("Bloom"), DisplayName("Override Bloom")]
        public bool OverrideBloom { get => Bit(O_BLOOM); set => SetBit(O_BLOOM, value); }
        [Category("Bloom"), DisplayName("Intensity (0 = off, 0-10)")]
        public float BloomIntensity { get => P(3); set => SetP(3, value); }
        [Category("Bloom"), DisplayName("Threshold (linear, after exposure)")]
        public float BloomThreshold { get => P(4); set => SetP(4, value); }

        [Category("Ambient Occlusion (SSAO)"), DisplayName("Override SSAO")]
        public bool OverrideSsao { get => Bit(O_SSAO); set => SetBit(O_SSAO, value); }
        [Category("Ambient Occlusion (SSAO)"), DisplayName("Strength (0 = off, 0-1)")]
        public float SsaoStrength { get => P(5); set => SetP(5, value); }
        [Category("Ambient Occlusion (SSAO)"), DisplayName("Radius (world units)")]
        public float SsaoRadius { get => P(6); set => SetP(6, value); }

        [Category("Global Illumination (SSGI)"), DisplayName("Override SSGI")]
        public bool OverrideSsgi { get => Bit(O_SSGI); set => SetBit(O_SSGI, value); }
        [Category("Global Illumination (SSGI)"), DisplayName("Intensity (0 = off, 0-5)")]
        public float SsgiIntensity { get => P(7); set => SetP(7, value); }

        [Category("Reflections (SSR)"), DisplayName("Override SSR")]
        public bool OverrideSsr { get => Bit(O_SSR); set => SetBit(O_SSR, value); }
        [Category("Reflections (SSR)"), DisplayName("Intensity (0 = off, 0-2)")]
        public float SsrIntensity { get => P(8); set => SetP(8, value); }

        [Category("Light Shafts"), DisplayName("Override Light Shafts")]
        public bool OverrideShafts { get => Bit(O_SHAFTS); set => SetBit(O_SHAFTS, value); }
        [Category("Light Shafts"), DisplayName("Intensity (0 = off, 0-10)")]
        public float ShaftIntensity { get => P(9); set => SetP(9, value); }

        [Category("Tone Mapping"), DisplayName("Override Tone Mapping (curve switches at weight 0.5)")]
        public bool OverrideTone { get => Bit(O_TONE); set => SetBit(O_TONE, value); }
        [Category("Tone Mapping"), DisplayName("Tone Mapping"), TypeConverter(typeof(ToneMapConverter))]
        public string ToneMapping
        {
            get => ToneMapConverter.Names[System.Math.Clamp((int)(P(10) + 0.5f), 0, ToneMapConverter.Names.Length - 1)];
            set { int i = System.Array.IndexOf(ToneMapConverter.Names, value); if (i >= 0) SetP(10, i); }
        }
        [Category("Tone Mapping"), DisplayName("White Point (Reinhard, 1-16)")]
        public float WhitePoint { get => P(11); set => SetP(11, value); }

        [Category("Details")] public string Info => "Tick an Override box to use that group. Applies while the camera is inside the volume (soft edge = Blend Distance).";
    }

    internal class ProbeModeConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Baked (on demand)", "Realtime (every N seconds)" };
        protected override string[] Items => Names;
    }
    internal class ProbeResConverter : FixedListConverter
    {
        public static readonly string[] Names = { "64 (fast)", "128 (default)", "256 (sharp)" };
        protected override string[] Items => Names;
    }
    internal class ProbeSourceConverter : FixedListConverter
    {
        public static readonly string[] Names = { "Scene (sky, sun, lights, objects)", "Sky only", "Custom texture" };
        protected override string[] Items => Names;
    }
    internal class ProbeActionConverter : FixedListConverter
    {
        public static readonly string[] Names = { "(choose)", "Capture Now" };
        protected override string[] Items => Names;
    }
}