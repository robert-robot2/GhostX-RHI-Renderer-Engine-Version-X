// GhostModelUI.cs - Details grid for animated models (the normal object grid plus an Animation section)
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace GhostShell
{
    // The clips of whichever model the grid is showing
    internal sealed class ModelClipConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext? context) => true;
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext? context) => true;
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext? context) =>
            new StandardValuesCollection((context?.Instance as ModelProxy)?.ClipNames() ?? new List<string>());
    }

    internal sealed class ModelProxy : ObjectProxy
    {
        private readonly int _mid;
        public ModelProxy(int id) : base(id) { _mid = id; }

        private string Text(int which)
        {
            var b = new byte[2048];
            if (GhostEngine.GhostEngine_GetAnimatorText(_mid, which, b, b.Length) == 0) return "";
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        internal List<string> ClipNames()
        {
            string t = Text(2);
            return t.Length == 0 ? new List<string>() : new List<string>(t.Split('|'));
        }

        [Category("Animation"), DisplayName("Animation File")]
        [TypeConverter(typeof(AnimFileConverter))]
        public string AnimationFile
        {
            get { string t = Text(0); return t.Length == 0 ? AnimFileConverter.None : t; }
            set
            {
                string path = value == AnimFileConverter.None ? "" : value == AnimFileConverter.Browse ? Browse() ?? Text(0) : value;
                GhostEngine.GhostEngine_SetAnimatorText(_mid, 0, path);
            }
        }

        [Category("Animation"), DisplayName("Clip (blank = Idle)")]
        [TypeConverter(typeof(ModelClipConverter))]
        public string Clip
        {
            get => Text(1);
            set => GhostEngine.GhostEngine_SetAnimatorText(_mid, 1, value ?? "");
        }

        [Category("Animation")]
        public bool Playing
        {
            get { GhostEngine.GhostEngine_GetAnimator(_mid, out int p, out _, out _); return p != 0; }
            set { GhostEngine.GhostEngine_GetAnimator(_mid, out _, out float sp, out _); GhostEngine.GhostEngine_SetAnimator(_mid, value ? 1 : 0, sp); }
        }

        [Category("Animation")]
        public float Speed
        {
            get { GhostEngine.GhostEngine_GetAnimator(_mid, out _, out float sp, out _); return sp; }
            set { GhostEngine.GhostEngine_GetAnimator(_mid, out int p, out _, out _); GhostEngine.GhostEngine_SetAnimator(_mid, p, value); }
        }

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
    }
}