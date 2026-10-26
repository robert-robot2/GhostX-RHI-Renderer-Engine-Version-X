// GhostUnits.cs — length parsing and formatting (1 engine unit = 1 metre), the size dialog, and the Details "Size" row
using System;
using System.ComponentModel;
using System.Drawing;
using System.Globalization;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace GhostShell
{
    internal static class GhostUnits
    {
        private static readonly string[] Names = { "mm", "cm", "m", "in", "ft" };
        private static readonly float[] PerMetre = { 1000f, 100f, 1f, 39.3701f, 3.28084f };
        private static readonly Regex Tok = new(@"([-+]?\d+(?:\.\d+)?|[-+]?\.\d+)\s*(mm|cm|m|in|ft|""|')?",
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

        public static int CurrentUnit()
        {
            GhostEngine.GhostEngine_GetViewSettings(out GhostViewSettings s);
            return Math.Clamp(s.unit, 0, 4);
        }

        public static string Format(float metres)
        {
            GhostEngine.GhostEngine_GetViewSettings(out GhostViewSettings s);
            int u = Math.Clamp(s.unit, 0, 4), d = Math.Clamp(s.decimals, 0, 3);
            return (metres * PerMetre[u]).ToString("F" + d, CultureInfo.InvariantCulture) + Names[u];
        }

        // "10mm", "2.5 cm", "1ft 6in", "4\"", "3'", or a bare number in the default unit. Returns metres.
        public static bool TryParse(string? text, int defaultUnit, out float metres)
        {
            metres = 0f;
            string t = (text ?? "").Trim().Replace(',', '.');
            if (t.Length == 0) return false;
            MatchCollection ms = Tok.Matches(t);
            if (ms.Count == 0 || Tok.Replace(t, "").Trim().Length > 0) return false;   // anything left over is a typo

            double sum = 0;
            foreach (Match m in ms)
            {
                if (!double.TryParse(m.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out double v)) return false;
                double mult = m.Groups[2].Value.ToLowerInvariant() switch
                {
                    "mm" => 0.001,
                    "cm" => 0.01,
                    "m" => 1.0,
                    "in" or "\"" => 0.0254,
                    "ft" or "'" => 0.3048,
                    _ => 1.0 / PerMetre[Math.Clamp(defaultUnit, 0, 4)],
                };
                sum += v * mult;
            }
            metres = (float)sum;
            return metres == metres && Math.Abs(metres) < 1e6f;
        }

        // Called after a click on a dimension label (kind 0) or a polygon edge (kind 1)
        public static void EditFromClick(Control host, int kind, int index, int objectId, float worldLen)
        {
            string prompt = kind == 0
                ? $"New {"XYZ"[Math.Clamp(index, 0, 2)]} size of {GhostNames.Of(objectId)}"
                : $"New length of edge {(char)('A' + index)}  (the whole object scales uniformly)";
            using var dlg = new UnitInputDialog(kind == 0 ? "Set size" : "Set edge length", prompt, Format(worldLen),
                "Examples: 10mm   2.5cm   1.2m   4in   3ft   1ft 6in. No unit = " + new[] { "mm", "cm", "m", "in", "ft" }[CurrentUnit()] + ".");
            if (dlg.ShowDialog(host.FindForm()) == DialogResult.OK)
            {
                if (!TryParse(dlg.Value, CurrentUnit(), out float m) || m < 0.0001f)
                    MessageBox.Show(host.FindForm(), "I couldn't read that length. Try something like 10mm or 1.5m.", "Set size",
                        MessageBoxButtons.OK, MessageBoxIcon.Information);
                else if (kind == 0) GhostEngine.GhostEngine_SetObjectSize(objectId, Math.Clamp(index, 0, 2), m);
                else if (worldLen > 1e-6f) GhostEngine.GhostEngine_ScaleObject(objectId, m / worldLen);
            }
            host.Select();
        }
    }

    internal sealed class UnitInputDialog : Form
    {
        private readonly TextBox _box = new();
        public string Value => _box.Text;

        public UnitInputDialog(string title, string prompt, string initial, string hint)
        {
            Text = title;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false; MinimizeBox = false; ShowInTaskbar = false;
            ClientSize = new Size(440, 140);
            BackColor = Color.FromArgb(0x25, 0x25, 0x28); ForeColor = Color.FromArgb(0xF2, 0xF2, 0xF2);
            Color bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);

            Controls.Add(new Label { Text = prompt, Location = new Point(16, 14), Size = new Size(408, 22), AutoEllipsis = true });
            _box.Location = new Point(16, 40); _box.Width = 408; _box.Text = initial;
            _box.BackColor = bg2; _box.ForeColor = ForeColor; _box.BorderStyle = BorderStyle.FixedSingle;
            Controls.Add(_box);
            Controls.Add(new Label { Text = hint, Location = new Point(16, 70), Size = new Size(408, 20), ForeColor = Color.Gray });

            var ok = new Button
            {
                Text = "OK",
                Location = new Point(244, 100),
                Size = new Size(84, 28),
                FlatStyle = FlatStyle.Flat,
                BackColor = bg2,
                ForeColor = ForeColor,
                DialogResult = DialogResult.OK
            };
            var cancel = new Button
            {
                Text = "Cancel",
                Location = new Point(340, 100),
                Size = new Size(84, 28),
                FlatStyle = FlatStyle.Flat,
                BackColor = bg2,
                ForeColor = ForeColor,
                DialogResult = DialogResult.Cancel
            };
            Controls.Add(ok); Controls.Add(cancel);
            AcceptButton = ok; CancelButton = cancel;
            Shown += (s, e) => { _box.Focus(); _box.SelectAll(); };
        }
    }

    // The expandable "Size" row in Details: X, Y and Z as text with units
    [TypeConverter(typeof(ExpandableObjectConverter))]
    internal sealed class DimEdit
    {
        private readonly int _id;
        public DimEdit(int id) { _id = id; }

        private string Get(int a)
        {
            var v = new float[3];
            return GhostEngine.GhostEngine_GetObjectSize(_id, v) == 0 ? "-" : GhostUnits.Format(v[a]);
        }

        private void Set(int a, string text)
        {
            if (!GhostUnits.TryParse(text, GhostUnits.CurrentUnit(), out float m) || m < 0.0001f)
            { System.Media.SystemSounds.Beep.Play(); return; }
            GhostEngine.GhostEngine_SetObjectSize(_id, a, m);
        }

        public string X { get => Get(0); set => Set(0, value); }
        public string Y { get => Get(1); set => Set(1, value); }
        public string Z { get => Get(2); set => Set(2, value); }
        public override string ToString() => $"{Get(0)} x {Get(1)} x {Get(2)}";
    }
}