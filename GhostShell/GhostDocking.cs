// GhostDocking.cs — dockable window wrapper + global key filter
using System;
using System.Drawing;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;

namespace GhostShell
{
    // One dockable window around any control. Closing hides it (the Window menu brings it back).
    internal class PanelDock : DockContent
    {
        public string Key { get; }

        public PanelDock(string key, string title, Control content, DockAreas areas, bool closable = true)
        {
            Key = key;
            Text = title;
            TabText = title;
            DockAreas = areas;
            HideOnClose = true;
            CloseButton = closable;
            CloseButtonVisible = closable;
            BackColor = Color.FromArgb(0x25, 0x25, 0x28);
            ForeColor = Color.FromArgb(0xF2, 0xF2, 0xF2);

            content.Dock = DockStyle.Fill;
            Controls.Add(content);
        }

        // Saved into the layout file, so a layout maps back to the same windows
        protected override string GetPersistString() => Key;
    }

    // Key 4 and Delete must work even when the focused panel is floating in its own window,
    // so this watches key messages for the whole app instead of one form's KeyDown.
    internal sealed class EditorKeyFilter : IMessageFilter
    {
        private const int WM_KEYDOWN = 0x0100;
        private readonly GhostEditorUI _ui;

        public EditorKeyFilter(GhostEditorUI ui) { _ui = ui; }

        public bool PreFilterMessage(ref Message m)
        {
            if (m.Msg != WM_KEYDOWN) return false;
            bool repeat = (m.LParam.ToInt64() & 0x40000000) != 0;
            return _ui.HandleKey((Keys)m.WParam.ToInt32(), repeat);
        }
    }
}
