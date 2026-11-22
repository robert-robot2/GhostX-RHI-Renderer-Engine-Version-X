// GhostVolumeProxy.cs — Details panel for Utilities volumes
using System.ComponentModel;

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

        public static VolumeProxy Create(int id, int type) =>
            type == 0 ? (VolumeProxy)new ReflectionCaptureProxy(id) : new FogVolumeProxy(id);

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
        [Category("Details")] public string Type => Get().type == 0 ? "Reflection Capture" : "Fog Volume";

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

    // Phase 5 adds fog parameters here (density, color, height falloff, cubemap / texture, light scattering ...)
    internal sealed class FogVolumeProxy : VolumeProxy
    {
        public FogVolumeProxy(int id) : base(id) { }
        [Category("Details")] public string Status => "Placeholder: fog arrives in Phase 5";
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