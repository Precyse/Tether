using System.ComponentModel;
using System.Diagnostics;

namespace CoopLauncher;

/// <summary>The running processes of a game. A Steam game is its executable's name. A game the launcher builds starts through a stub whose
/// child is the engine: the engine of that install counts (by its path, so another copy of the engine on the PC does not), and the stub,
/// which stays open to show an error box after the engine died, does not.</summary>
public static class GameProcesses
{
    /// <summary>The processes now; the caller disposes them.</summary>
    public static Process[] Of(GameProfile profile)
    {
        if (profile.Recomp is not { } recomp) return Process.GetProcessesByName(Path.GetFileNameWithoutExtension(profile.Exe));
        var engine = Path.GetFullPath(Path.Combine(RecompInstall.GameDir(recomp), recomp.Engine));
        var ours = new List<Process>();
        foreach (var process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(recomp.Engine)))
        {
            if (IsAt(process, engine)) ours.Add(process);
            else process.Dispose();
        }
        return ours.ToArray();
    }

    private static bool IsAt(Process process, string path)
    {
        try
        {
            return string.Equals(process.MainModule?.FileName, path, StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception e) when (e is Win32Exception or InvalidOperationException)
        {
            return false;
        }
    }
}
