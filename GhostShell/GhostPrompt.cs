using System;
using System.Drawing;
using System.Windows.Forms;

namespace GhostShell
{
    internal static class GhostPrompt
    {
        // Returns the typed text, or null if cancelled
        public static string? Ask(IWin32Window owner, string title, string label, string initial)
        {
            Color bg = Color.FromArgb(0x25, 0x25, 0x28), bg2 = Color.FromArgb(0x1E, 0x1E, 0x1E), fg = Color.FromArgb(0xF2, 0xF2, 0xF2);
            using var f = new Form
            {
                Text = title,
                FormBorderStyle = FormBorderStyle.FixedDialog,
                StartPosition = FormStartPosition.CenterParent,
                MaximizeBox = false,
                MinimizeBox = false,
                ShowInTaskbar = false,
                ClientSize = new Size(360, 112),
                BackColor = bg,
                ForeColor = fg,
            };
            var l = new Label { Text = label, Location = new Point(14, 12), AutoSize = true };
            var t = new TextBox { Text = initial, Location = new Point(14, 36), Width = 332, BackColor = bg2, ForeColor = fg, BorderStyle = BorderStyle.FixedSingle };
            var ok = new Button { Text = "OK", DialogResult = DialogResult.OK, Location = new Point(166, 72), Size = new Size(84, 28), FlatStyle = FlatStyle.Flat, BackColor = bg2, ForeColor = fg };
            var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Location = new Point(262, 72), Size = new Size(84, 28), FlatStyle = FlatStyle.Flat, BackColor = bg2, ForeColor = fg };
            f.Controls.AddRange(new Control[] { l, t, ok, cancel });
            f.AcceptButton = ok; f.CancelButton = cancel;
            f.Shown += (s, e) => { t.Focus(); t.SelectAll(); };
            return f.ShowDialog(owner) == DialogResult.OK ? t.Text.Trim() : null;
        }
    }
}