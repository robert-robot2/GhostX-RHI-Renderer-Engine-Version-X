using System.Runtime.InteropServices;

namespace GhostShell
{
    internal static class Win32
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct MSG
        {
            public IntPtr hwnd;
            public uint message;
            public IntPtr wParam;
            public IntPtr lParam;
            public uint time;
            public int ptX, ptY;
        }

        [DllImport("user32.dll")]
        private static extern bool PeekMessage(
            out MSG lpMsg, IntPtr hWnd,
            uint wMsgFilterMin, uint wMsgFilterMax, uint wRemoveMsg);

        // Used by GhostForm.OnIdle to check if the queue is empty
        public static bool PeekMessageExists()
            => PeekMessage(out _, IntPtr.Zero, 0, 0, 0); // PM_NOREMOVE

        [DllImport("dwmapi.dll")]
        public static extern int DwmSetWindowAttribute(
            IntPtr hwnd, uint dwAttribute, ref uint pvAttribute, uint cbAttribute);
        [DllImport("user32.dll")]
        public static extern IntPtr GetFocus();
    }
}