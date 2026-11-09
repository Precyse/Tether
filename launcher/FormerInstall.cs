using System.Diagnostics;

namespace Tether;

/// <summary>
/// An install made before the rename starts <c>coop-launcher.exe</c> after an update. The release ships that file as a copy of the
/// current exe (the only name those launchers know), and when it is the one that runs it hands over to <c>tether.exe</c> beside it.
/// </summary>
public static class FormerInstall
{
    public const string FormerExeName = "coop-launcher.exe";
    public const string ExeName = "tether.exe";

    /// <summary>True when the exe at <paramref name="path"/> carries the former name.</summary>
    public static bool IsFormerExe(string? path) =>
        path != null && string.Equals(Path.GetFileName(path), FormerExeName, StringComparison.OrdinalIgnoreCase);

    /// <summary>Starts <c>tether.exe</c> from the folder of the running former-named exe with the same arguments. True when it started and this process must exit.</summary>
    public static bool HandOver(string[] args)
    {
        var path = Environment.ProcessPath;
        if (!IsFormerExe(path)) return false;
        var current = Path.Combine(Path.GetDirectoryName(path)!, ExeName);
        if (!File.Exists(current)) return false;
        try
        {
            var start = new ProcessStartInfo(current) { UseShellExecute = false };
            start.Environment[SingleInstance.PredecessorVariable] = Environment.ProcessId.ToString();
            foreach (var arg in args) start.ArgumentList.Add(arg);
            Process.Start(start)?.Dispose();
            return true;
        }
        catch (Exception e) when (e is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            Log.Info($"Could not hand over to {ExeName}: {e.Message}");
            return false;
        }
    }
}
