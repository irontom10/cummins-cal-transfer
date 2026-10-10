using System;
using System.Windows.Forms;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        EmbeddedNative.Initialize();

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new Rp1210Form());
    }
}
