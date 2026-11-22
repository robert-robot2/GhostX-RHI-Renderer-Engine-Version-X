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
        [Category("Details")] public string Status => "Placeholder: capture arrives in Phase 4";
    }

    // Phase 5 adds fog parameters here (density, color, height falloff, cubemap / texture, light scattering ...)
    internal sealed class FogVolumeProxy : VolumeProxy
    {
        public FogVolumeProxy(int id) : base(id) { }
        [Category("Details")] public string Status => "Placeholder: fog arrives in Phase 5";
    }
}