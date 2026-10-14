// GhostModUI.cs — builds the Details grid for mod entities from the engine's parameter descriptors
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Linq;
using System.Text;

namespace GhostShell
{
    internal sealed class ModParam
    {
        public int Index, Kind, Count;
        public float Min, Max;
        public string Name = "", Category = "";
        public string[] EnumNames = Array.Empty<string>();
    }

    internal static class GhostMods
    {
        private static readonly Dictionary<int, List<ModParam>> Cache = new();

        private static string Str(byte[]? b)
        {
            if (b == null) return "";
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        public static List<ModParam> Params(int typeId)
        {
            if (Cache.TryGetValue(typeId, out var list)) return list;
            list = new List<ModParam>();
            int n = GhostEngine.GhostEngine_GetModParamCount(typeId);
            for (int i = 0; i < n; i++)
            {
                if (GhostEngine.GhostEngine_GetModParamInfo(typeId, i, out GhostModParamInfo info) == 0) continue;
                string enums = Str(info.enums);
                list.Add(new ModParam
                {
                    Index = i,
                    Kind = info.kind,
                    Count = info.count,
                    Min = info.mn,
                    Max = info.mx,
                    Name = Str(info.name),
                    Category = Str(info.category),
                    EnumNames = enums.Length > 0 ? enums.Split('|') : Array.Empty<string>(),
                });
            }
            Cache[typeId] = list;
            return list;
        }

        public static int TypeOf(int id) => GhostEngine.GhostEngine_GetMod(id, new float[3]);

        // Changes whenever any value changes, so the grid knows when to refresh
        public static string Signature(int id)
        {
            int t = TypeOf(id);
            if (t < 0) return "";
            var sb = new StringBuilder();
            var pos = new float[3];
            GhostEngine.GhostEngine_GetMod(id, pos);
            sb.Append(pos[0]).Append(',').Append(pos[1]).Append(',').Append(pos[2]).Append(';');
            var v = new float[3];
            foreach (ModParam p in Params(t))
            {
                GhostEngine.GhostEngine_GetModParam(id, p.Index, v);
                sb.Append(v[0]).Append(',').Append(v[1]).Append(',').Append(v[2]).Append(';');
            }
            return sb.ToString();
        }
    }

    internal sealed class EnumListConverter : StringConverter
    {
        private readonly string[] _items;
        public EnumListConverter(string[] items) { _items = items; }
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context) => new(_items);
    }

    // One grid row for one parameter
    internal sealed class ModProp : PropertyDescriptor
    {
        private readonly int _entity;
        private readonly ModParam _p;

        public ModProp(int entity, ModParam p) : base(p.Name, new Attribute[] { new CategoryAttribute(p.Category) })
        { _entity = entity; _p = p; }

        public override Type ComponentType => typeof(GhostModProxy);
        public override bool IsReadOnly => false;
        public override Type PropertyType => _p.Kind switch { 0 => typeof(float), 1 => typeof(bool), 3 => typeof(Color), _ => typeof(string) };
        public override bool CanResetValue(object component) => false;
        public override void ResetValue(object component) { }
        public override bool ShouldSerializeValue(object component) => false;

        public override TypeConverter Converter => _p.Kind switch
        {
            2 => new EnumListConverter(_p.EnumNames),
            4 => new TextureChoiceConverter(),
            _ => base.Converter,
        };

        private float[] Read() { var v = new float[3]; GhostEngine.GhostEngine_GetModParam(_entity, _p.Index, v); return v; }
        private void Write(float a, float b = 0f, float c = 0f) =>
            GhostEngine.GhostEngine_SetModParam(_entity, _p.Index, new[] { a, b, c });
        private static int B(float x) => (int)Math.Round(Math.Clamp(x, 0f, 1f) * 255f);

        public override object? GetValue(object? component)
        {
            float[] v = Read();
            switch (_p.Kind)
            {
                case 0: return v[0];
                case 1: return v[0] > 0.5f;
                case 2:
                    if (_p.EnumNames.Length == 0) return "";
                    return _p.EnumNames[Math.Clamp((int)Math.Round(v[0]), 0, _p.EnumNames.Length - 1)];
                case 3: return Color.FromArgb(B(v[0]), B(v[1]), B(v[2]));
                default: return GhostTextures.DisplayName((int)Math.Round(v[0]));
            }
        }

        public override void SetValue(object? component, object? value)
        {
            switch (_p.Kind)
            {
                case 0: Write(Convert.ToSingle(value)); break;
                case 1: Write(Convert.ToBoolean(value) ? 1f : 0f); break;
                case 2:
                    {
                        int i = Array.IndexOf(_p.EnumNames, value as string);
                        if (i >= 0) Write(i);
                        break;
                    }
                case 3:
                    if (value is Color c) Write(c.R / 255f, c.G / 255f, c.B / 255f);
                    break;
                default:
                    {
                        string s = value as string ?? "";
                        int tex = s == GhostTextures.ImportLabel ? GhostTextures.ImportFromDisk() : GhostTextures.IdFromDisplay(s);
                        if (tex >= 0) Write(tex);
                        break;
                    }
            }
            OnValueChanged(component, EventArgs.Empty);
        }
    }

    // A read-only or expandable row that isn't a mod parameter (Name, Position)
    internal sealed class FixedProp : PropertyDescriptor
    {
        private readonly Type _type;
        private readonly Func<object?> _get;
        public FixedProp(string name, string category, Type type, Func<object?> get)
            : base(name, new Attribute[] { new CategoryAttribute(category) }) { _type = type; _get = get; }

        public override Type ComponentType => typeof(GhostModProxy);
        public override bool IsReadOnly => true;
        public override Type PropertyType => _type;
        public override bool CanResetValue(object component) => false;
        public override void ResetValue(object component) { }
        public override bool ShouldSerializeValue(object component) => false;
        public override object? GetValue(object? component) => _get();
        public override void SetValue(object? component, object? value) { }
    }

    // What the grid shows for a selected mod entity
    internal sealed class GhostModProxy : ICustomTypeDescriptor
    {
        private readonly int _id;
        private readonly Vec3Edit _position;

        public GhostModProxy(int id)
        {
            _id = id;
            _position = new Vec3Edit(
                () => { var p = new float[3]; GhostEngine.GhostEngine_GetMod(_id, p); return (p[0], p[1], p[2]); },
                (x, y, z) => GhostEngine.GhostEngine_SetModPosition(_id, new[] { x, y, z }));
        }

        public static bool Exists(int id) => GhostMods.TypeOf(id) >= 0;

        public PropertyDescriptorCollection GetProperties()
        {
            var props = new List<PropertyDescriptor>
            {
                new FixedProp("Name", "Details", typeof(string), () => GhostNames.Of(_id)),
                new FixedProp("Position (icon only)", "Transform", typeof(Vec3Edit), () => _position),
            };
            int t = GhostMods.TypeOf(_id);
            if (t >= 0) props.AddRange(GhostMods.Params(t).Select(p => (PropertyDescriptor)new ModProp(_id, p)));
            return new PropertyDescriptorCollection(props.ToArray());
        }

        public PropertyDescriptorCollection GetProperties(Attribute[]? attributes) => GetProperties();
        public AttributeCollection GetAttributes() => AttributeCollection.Empty;
        public string? GetClassName() => null;
        public string? GetComponentName() => null;
        public TypeConverter GetConverter() => new TypeConverter();
        public EventDescriptor? GetDefaultEvent() => null;
        public PropertyDescriptor? GetDefaultProperty() => null;
        public object? GetEditor(Type editorBaseType) => null;
        public EventDescriptorCollection GetEvents() => EventDescriptorCollection.Empty;
        public EventDescriptorCollection GetEvents(Attribute[]? attributes) => EventDescriptorCollection.Empty;
        public object GetPropertyOwner(PropertyDescriptor? pd) => this;
    }
}
