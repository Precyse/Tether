using Tether;

namespace RelayRig;

/// <summary>The launcher's build step against the real setup tool: <see cref="RecompBuild.Run"/> for the warriors profile with the progress it reports, then the state of
/// the build folder. The launcher's data folder (COOP_LAUNCHER_DATA) holds the game folder and the settings that name the disc and the tool.</summary>
internal static class BuildStep
{
    /// <param name="cancelAfterSeconds">Cancels the build after this many seconds (0: lets it finish), as the Cancel button would.</param>
    public static int Run(int cancelAfterSeconds)
    {
        using var cancel = new CancellationTokenSource();
        if (cancelAfterSeconds > 0) cancel.CancelAfter(TimeSpan.FromSeconds(cancelAfterSeconds));
        var profile = GameProfile.Load("warriors");
        var gameDir = RecompInstall.GameDir(profile.Recomp!);
        Console.WriteLine($"game folder {gameDir}, disc {RecompInstall.DiscPath(profile)}, tool {RecompInstall.SetupPath(profile)}");
        var started = DateTime.UtcNow;
        var last = -1;
        var progress = new Progress<ModProgress>(report =>
        {
            if (report.Percent == last) return;
            last = report.Percent;
            Console.WriteLine($"{(DateTime.UtcNow - started).TotalSeconds,6:F1} s  {report.Percent,3}%  {report.Reason}");
        });
        var result = RecompBuild.Run(profile, progress, cancel.Token);
        Thread.Sleep(200);
        Console.WriteLine(result.Success ? "build ok" : $"build failed ({result.Failure!.Kind}): {result.Failure.Message}");
        var status = RecompInstall.Status(profile, gameDir);
        Console.WriteLine($"status {status.State}, fingerprint {RecompBuildInfo.Read(RecompInstall.BuildInfoPath(profile, gameDir))?.Fingerprint}");
        return result.Success && status.State == ModState.Installed ? 0 : 1;
    }
}
