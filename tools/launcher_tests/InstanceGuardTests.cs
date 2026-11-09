using Tether;

namespace LauncherTests;

/// <summary>InstanceGuard.ShouldClose over every input combination, and the command-line run mutex. No process is started or closed.</summary>
public static class InstanceGuardTests
{
    public static void Run()
    {
        // isCli, holdsWindow, answered, responding -> close
        Rule(isCli: false, holdsWindow: false, answered: false, responding: false, expected: true, "a hung stale copy is closed");
        Rule(isCli: false, holdsWindow: false, answered: false, responding: true, expected: false, "a responding stale copy stays");
        Rule(isCli: false, holdsWindow: false, answered: true, responding: false, expected: true, "a hung stale copy is closed beside a live window");
        Rule(isCli: false, holdsWindow: false, answered: true, responding: true, expected: false, "a responding stale copy beside a live window stays");
        Rule(isCli: false, holdsWindow: true, answered: false, responding: false, expected: true, "a silent window that is hung is closed");
        Rule(isCli: false, holdsWindow: true, answered: false, responding: true, expected: false, "a silent window that still responds stays");
        Rule(isCli: false, holdsWindow: true, answered: true, responding: false, expected: false, "a window that answers is never closed");
        Rule(isCli: false, holdsWindow: true, answered: true, responding: true, expected: false, "a live window is never closed");
        foreach (var holdsWindow in new[] { false, true })
            foreach (var answered in new[] { false, true })
                foreach (var responding in new[] { false, true })
                    Rule(isCli: true, holdsWindow, answered, responding, expected: false, "a command-line run is never closed");

        Check.That(FormerInstall.IsFormerExe(@"C:\x\launcher\app\coop-launcher.exe") && FormerInstall.IsFormerExe(@"C:\x\COOP-LAUNCHER.EXE")
            && !FormerInstall.IsFormerExe(@"C:\x\launcher\app\tether.exe") && !FormerInstall.IsFormerExe(null), "rename: only the former exe name hands over to tether.exe");
        FormerFolder();

        var pid = Environment.ProcessId;
        Check.That(!InstanceGuard.IsCliRun(pid), "no CLI mutex: not a CLI run");
        using (new Mutex(initiallyOwned: false, InstanceGuard.CliMutexName(pid)))
            Check.That(InstanceGuard.IsCliRun(pid), "a held CLI mutex marks the process as a CLI run");
        Check.That(!InstanceGuard.IsCliRun(pid), "released CLI mutex: not a CLI run");
    }

    private static void Rule(bool isCli, bool holdsWindow, bool answered, bool responding, bool expected, string what) =>
        Check.That(InstanceGuard.ShouldClose(isCli, holdsWindow, answered, responding) == expected,
            $"ShouldClose(cli={isCli}, holds={holdsWindow}, answered={answered}, responding={responding}): {what}");

    /// <summary>The data folder of the former name moves to the new one once; a new one that exists, or no former one, leaves things as they are.</summary>
    private static void FormerFolder()
    {
        var appData = Path.Combine(Path.GetTempPath(), $"former_{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(Path.Combine(appData, "CoopLauncher"));
            File.WriteAllText(Path.Combine(appData, "CoopLauncher", "settings.json"), "{}");
            Check.That(AppData.MoveFormerFolder(appData) == Path.Combine(appData, "Tether") && File.Exists(Path.Combine(appData, "Tether", "settings.json"))
                && !Directory.Exists(Path.Combine(appData, "CoopLauncher")), "rename: the former data folder moves to the new name, its settings with it");
            Directory.CreateDirectory(Path.Combine(appData, "CoopLauncher"));
            Check.That(AppData.MoveFormerFolder(appData) == Path.Combine(appData, "Tether") && Directory.Exists(Path.Combine(appData, "CoopLauncher")), "rename: a new folder that exists is never replaced");
            Directory.Delete(Path.Combine(appData, "CoopLauncher"));
            Directory.Delete(Path.Combine(appData, "Tether"), recursive: true);
            Check.That(AppData.MoveFormerFolder(appData) == Path.Combine(appData, "Tether"), "rename: neither folder: the new name");
        }
        finally
        {
            Directory.Delete(appData, recursive: true);
        }
    }
}
