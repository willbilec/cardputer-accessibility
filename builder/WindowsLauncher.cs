using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

internal static class WindowsLauncher
{
    [STAThread]
    private static void Main()
    {
        string root = AppDomain.CurrentDomain.BaseDirectory;
        try
        {
            string runtime = Path.Combine(root, "runtime", "pythonw.exe");
            string script = Path.Combine(root, "builder", "bootstrap.py");
            var start = new ProcessStartInfo(runtime, "-I \"" + script + "\"");
            start.WorkingDirectory = root;
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            if (!File.Exists(script))
                throw new FileNotFoundException("Extract the complete builder ZIP before running.", script);
            using (Process child = Process.Start(start))
                child.WaitForExit();
        }
        catch (Exception error)
        {
            MessageBox.Show(error.Message, "Cardputer ADV Firmware Builder",
                            MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
