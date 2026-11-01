// GhostArrayDialog.cs — Ctrl+F: duplicate the selected object into an array
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace GhostShell
{
    internal sealed class GhostArrayDialog : Form
    {
        public const int MaxCopies = 500;

        private static readonly Color Bg = Color.FromArgb(0x25, 0x25, 0x28);
        private static readonly Color Bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E);
        private static readonly Color Fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
        private static readonly Color Accent = Color.FromArgb(0xFF, 0xC8, 0x6E);
        private static readonly Color Bad = Color.FromArgb(0xFF, 0x73, 0x5A);

        private static readonly string[] Shapes = { "Line", "Grid (full square / cube)", "Staircase", "Square outline", "Circle" };
        private static readonly string[] Planes = { "Floor (X, Z)", "Wall (X, Y)", "Side wall (Y, Z)" };
        private static readonly string[] Hints =
        {
            "Each copy moves by the spacing from the one before it.",
            "Count X x Y x Z copies, spaced by the spacing values.",
            "Count X = steps, Count Z = width. Spacing X = run, Y = rise, Z = side.",
            "Only the edge of a rectangle in the chosen plane.",
            "Amount = points on the ring. Radius is measured from the center.",
        };

        // remembered between uses
        private static int _shape = 0, _amount = 5, _cx = 3, _cy = 1, _cz = 3, _plane = 0;
        private static decimal _sx = 1, _sy = 0, _sz = 0, _radius = 3;
        private static bool _onRing = false;

        private readonly ComboBox _shapeBox = new(), _planeBox = new();
        private readonly NumericUpDown _amountN, _cxN, _cyN, _czN, _sxN, _syN, _szN, _radiusN;
        private readonly CheckBox _ringChk = new();
        private readonly Label _info = new(), _hint = new();
        private readonly Button _ok = new();

        public GhostArrayDialog(string sourceName)
        {
            Text = "Array Duplicate";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false; MinimizeBox = false; ShowInTaskbar = false;
            AutoSize = true; AutoSizeMode = AutoSizeMode.GrowAndShrink;
            BackColor = Bg; ForeColor = Fg;

            _amountN = Num(_amount, 1, MaxCopies, 0, 1);
            _cxN = Num(_cx, 1, 100, 0, 1);
            _cyN = Num(_cy, 1, 100, 0, 1);
            _czN = Num(_cz, 1, 100, 0, 1);
            _sxN = Num(_sx, -1000, 1000, 2, 0.5m);
            _syN = Num(_sy, -1000, 1000, 2, 0.5m);
            _szN = Num(_sz, -1000, 1000, 2, 0.5m);
            _radiusN = Num(_radius, 0.1m, 1000, 2, 0.5m);

            foreach (ComboBox c in new[] { _shapeBox, _planeBox })
            {
                c.DropDownStyle = ComboBoxStyle.DropDownList;
                c.FlatStyle = FlatStyle.Flat;
                c.BackColor = Bg2; c.ForeColor = Fg;
                c.Width = 200;
                c.SelectedIndexChanged += (s, e) => UpdateUi();
            }
            _shapeBox.Items.AddRange(Shapes);
            _planeBox.Items.AddRange(Planes);
            _shapeBox.SelectedIndex = Math.Clamp(_shape, 0, Shapes.Length - 1);
            _planeBox.SelectedIndex = Math.Clamp(_plane, 0, Planes.Length - 1);

            _ringChk.Text = "Source is one point of the circle";
            _ringChk.AutoSize = true; _ringChk.ForeColor = Fg;
            _ringChk.Checked = _onRing;
            _ringChk.CheckedChanged += (s, e) => UpdateUi();

            _hint.AutoSize = true; _hint.MaximumSize = new Size(380, 0); _hint.ForeColor = Color.Gray;
            _info.AutoSize = true; _info.ForeColor = Accent;

            var t = new TableLayoutPanel
            {
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                ColumnCount = 2,
                Padding = new Padding(14),
                BackColor = Bg,
            };
            void Row(string text, Control c)
            {
                t.Controls.Add(new Label { Text = text, AutoSize = true, ForeColor = Fg, Anchor = AnchorStyles.Left, Margin = new Padding(0, 6, 14, 6) });
                c.Margin = new Padding(0, 3, 0, 3);
                t.Controls.Add(c);
            }
            void Wide(Control c)
            {
                c.Margin = new Padding(0, 6, 0, 6);
                t.Controls.Add(c);
                t.SetColumnSpan(c, 2);
            }

            Wide(new Label { Text = "Duplicating: " + sourceName, AutoSize = true, ForeColor = Accent });
            Row("Shape", _shapeBox);
            Row("Amount (line, circle)", _amountN);
            Row("Count X", _cxN);
            Row("Count Y", _cyN);
            Row("Count Z", _czN);
            Row("Spacing X", _sxN);
            Row("Spacing Y", _syN);
            Row("Spacing Z", _szN);
            Row("Plane (outline, circle)", _planeBox);
            Row("Radius (circle)", _radiusN);
            Wide(_ringChk);
            Wide(_hint);
            Wide(_info);

            _ok.Text = "Create";
            _ok.DialogResult = DialogResult.OK;
            var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel };
            foreach (Button b in new[] { _ok, cancel })
            {
                b.FlatStyle = FlatStyle.Flat; b.BackColor = Bg2; b.ForeColor = Fg;
                b.FlatAppearance.BorderColor = Color.FromArgb(0x3A, 0x3A, 0x40);
                b.Size = new Size(90, 28);
            }
            _ok.Click += (s, e) => Save();
            var bar = new FlowLayoutPanel { FlowDirection = FlowDirection.RightToLeft, AutoSize = true, Anchor = AnchorStyles.Right };
            bar.Controls.Add(cancel);
            bar.Controls.Add(_ok);
            Wide(bar);

            AcceptButton = _ok; CancelButton = cancel;
            Controls.Add(t);
            UpdateUi();
        }

        private NumericUpDown Num(decimal v, decimal min, decimal max, int decimals, decimal inc)
        {
            var n = new NumericUpDown
            {
                Minimum = min,
                Maximum = max,
                DecimalPlaces = decimals,
                Increment = inc,
                Width = 110,
                BackColor = Bg2,
                ForeColor = Fg,
                BorderStyle = BorderStyle.FixedSingle,
            };
            n.Value = Math.Clamp(v, min, max);
            n.ValueChanged += (s, e) => UpdateUi();
            return n;
        }

        private void Save()
        {
            _shape = _shapeBox.SelectedIndex; _plane = _planeBox.SelectedIndex;
            _amount = (int)_amountN.Value;
            _cx = (int)_cxN.Value; _cy = (int)_cyN.Value; _cz = (int)_czN.Value;
            _sx = _sxN.Value; _sy = _syN.Value; _sz = _szN.Value;
            _radius = _radiusN.Value; _onRing = _ringChk.Checked;
        }

        // the two axes of the chosen plane: 0 = X, 1 = Y, 2 = Z
        private void GetPlane(out int a, out int b)
        {
            int p = _planeBox.SelectedIndex;
            a = p == 2 ? 1 : 0;
            b = p == 1 ? 1 : 2;
        }

        private bool UsesAxis(int k) { GetPlane(out int a, out int b); return a == k || b == k; }

        private int Total()
        {
            int amount = (int)_amountN.Value;
            int cx = (int)_cxN.Value, cy = (int)_cyN.Value, cz = (int)_czN.Value;
            switch (_shapeBox.SelectedIndex)
            {
                case 0: return amount;
                case 1: return cx * cy * cz - 1;
                case 2: return cx * cz - 1;
                case 3:
                    {
                        GetPlane(out int a, out int b);
                        int[] n = { cx, cy, cz };
                        int na = n[a], nb = n[b];
                        return (na == 1 || nb == 1) ? na * nb - 1 : 2 * (na + nb) - 4 - 1;
                    }
                default: return _ringChk.Checked ? amount - 1 : amount;
            }
        }

        private void UpdateUi()
        {
            int s = _shapeBox.SelectedIndex;
            bool line = s == 0, grid = s == 1, stairs = s == 2, outline = s == 3, circle = s == 4;

            _amountN.Enabled = line || circle;
            _cxN.Enabled = grid || stairs || (outline && UsesAxis(0));
            _cyN.Enabled = grid || (outline && UsesAxis(1));
            _czN.Enabled = grid || stairs || (outline && UsesAxis(2));
            _sxN.Enabled = line || grid || stairs || (outline && UsesAxis(0));
            _syN.Enabled = line || grid || stairs || (outline && UsesAxis(1));
            _szN.Enabled = line || grid || stairs || (outline && UsesAxis(2));
            _planeBox.Enabled = outline || circle;
            _radiusN.Enabled = circle;
            _ringChk.Enabled = circle;
            _hint.Text = Hints[Math.Clamp(s, 0, Hints.Length - 1)];

            int total = Total();
            if (total > MaxCopies) { _info.ForeColor = Bad; _info.Text = $"Too many copies ({total}). The limit is {MaxCopies}."; }
            else if (total < 1) { _info.ForeColor = Bad; _info.Text = "Nothing to create with these settings."; }
            else { _info.ForeColor = Accent; _info.Text = $"Will create {total} cop{(total == 1 ? "y" : "ies")}."; }
            _ok.Enabled = total >= 1 && total <= MaxCopies;
        }

        // x, y, z per copy, relative to the source
        public float[] BuildOffsets()
        {
            int total = Total();
            if (total < 1 || total > MaxCopies) return Array.Empty<float>();

            var o = new List<float>(total * 3);
            void Add(float x, float y, float z) { o.Add(x); o.Add(y); o.Add(z); }

            int amount = (int)_amountN.Value;
            int cx = (int)_cxN.Value, cy = (int)_cyN.Value, cz = (int)_czN.Value;
            float sx = (float)_sxN.Value, sy = (float)_syN.Value, sz = (float)_szN.Value;

            switch (_shapeBox.SelectedIndex)
            {
                case 0:
                    for (int i = 1; i <= amount; i++) Add(i * sx, i * sy, i * sz);
                    break;

                case 1:
                    for (int x = 0; x < cx; x++)
                        for (int y = 0; y < cy; y++)
                            for (int z = 0; z < cz; z++)
                            {
                                if (x == 0 && y == 0 && z == 0) continue;
                                Add(x * sx, y * sy, z * sz);
                            }
                    break;

                case 2:
                    for (int i = 0; i < cx; i++)
                        for (int k = 0; k < cz; k++)
                        {
                            if (i == 0 && k == 0) continue;
                            Add(i * sx, i * sy, k * sz);
                        }
                    break;

                case 3:
                    {
                        GetPlane(out int a, out int b);
                        int[] n = { cx, cy, cz };
                        float[] sp = { sx, sy, sz };
                        for (int ia = 0; ia < n[a]; ia++)
                            for (int ib = 0; ib < n[b]; ib++)
                            {
                                if (ia != 0 && ia != n[a] - 1 && ib != 0 && ib != n[b] - 1) continue;   // inside: skip
                                if (ia == 0 && ib == 0) continue;                                        // the source itself
                                var v = new float[3];
                                v[a] = ia * sp[a]; v[b] = ib * sp[b];
                                Add(v[0], v[1], v[2]);
                            }
                        break;
                    }

                default:
                    {
                        GetPlane(out int a, out int b);
                        float r = (float)_radiusN.Value;
                        bool onRing = _ringChk.Checked;
                        for (int k = onRing ? 1 : 0; k < amount; k++)
                        {
                            double ang = 2.0 * Math.PI * k / amount;
                            var v = new float[3];
                            v[a] = MathF.Round(r * (float)Math.Cos(ang) - (onRing ? r : 0f), 4);   // source = point 0 on the ring
                            v[b] = MathF.Round(r * (float)Math.Sin(ang), 4);
                            Add(v[0], v[1], v[2]);
                        }
                        break;
                    }
            }
            return o.ToArray();
        }
    }
}