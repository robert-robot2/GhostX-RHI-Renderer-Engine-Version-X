using System.Windows.Forms;

namespace GhostShell
{
    internal static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            GhostPlayer.ParseArgs(args);
            if (!GhostPlayer.IsPlayer) GhostUIHost.Start();      // the player doesn't need the Blazor server (and its port)
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new GhostForm());
        }
    }
}