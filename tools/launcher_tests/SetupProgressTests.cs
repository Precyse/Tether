using CoopLauncher;

namespace LauncherTests;

/// <summary>What warriors-setup prints becomes one monotonic progress bar, and its last words become a failure a player can act on.</summary>
public static class SetupProgressTests
{
    private const string CleanRun = """
        [1/9] Checking the disc image
              hashing the whole image (4 GB)
              done in 2.7 s
        [2/9] Checking this PC
              Visual Studio build tools found (C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools), 68 GB free
        [3/9] Fetching the pinned build tools (checked by SHA-256)
              llvm 23.1.5
              clang+llvm-23.1.5-x86_64-pc-windows-msvc.tar.zst: 10 of 263 MB
              clang+llvm-23.1.5-x86_64-pc-windows-msvc.tar.zst: 260 of 263 MB
              cmake 4.2.3
              cmake-4.2.3-windows-x86_64.zip: 10 of 48 MB
              cmake-4.2.3-windows-x86_64.zip: 40 of 48 MB
              wgpu-windows-x86_64-msvc-release.zip: 10 of 16 MB
        [4/9] Preparing the runtime source (pinned ps2-web-recomp plus our patch)
        [8/9] Building the game (the long step)
        [9/9] Writing Warriors.exe and its settings
        """;

    public static void Run()
    {
        Lines();
        Bar();
        Failures();
    }

    private static void Lines()
    {
        Check.That(SetupProgress.TryParseStep("[3/9] Fetching the pinned build tools (checked by SHA-256)", out var step, out var title)
            && step == 3 && title == "Fetching the pinned build tools (checked by SHA-256)", "line: a step opens with its number and title");
        Check.That(SetupProgress.TryParseStep("[0/9] Fetching Python 3.14.3 (checked by SHA-256)", out var zero, out _) && zero == 0, "line: step 0 is the tool's own Python");
        Check.That(!SetupProgress.TryParseStep("      done in 2.7 s", out _, out _) && !SetupProgress.TryParseStep("[10/9] too far", out _, out _), "line: other lines are no step");
        Check.That(SetupProgress.TryParseDownload("      clang+llvm-23.1.5-x86_64-pc-windows-msvc.tar.zst: 10 of 263 MB", out var file, out var done, out var total)
            && file.EndsWith(".tar.zst") && done == 10 && total == 263, "line: a download names its file and its megabytes");
        Check.That(!SetupProgress.TryParseDownload("      llvm 23.1.5", out _, out _, out _), "line: a tool's name is no download");
        Check.That(SetupProgress.NinjaFraction("[1/6] Building\n[3/6] Generating\n") is { } half && Math.Abs(half - 0.5) < 1e-9, "ninja: the newest counter is the fraction");
        Check.That(SetupProgress.NinjaFraction("> cmake --build\n") is null, "ninja: no counter, no fraction");
    }

    private static void Bar()
    {
        Check.That(SetupProgress.StepWeights.Sum() == 100 && SetupProgress.StepWeights.Length == SetupProgress.StepCount + 1, "weights: ten steps share the whole bar");
        Check.That(SetupProgress.Percent(0, 0) == 0 && SetupProgress.Percent(SetupProgress.BuildStep, 0) == 33 && SetupProgress.Percent(9, 1) == 99, "bar: a step starts after the ones before it, 99 at most");
        var progress = new SetupProgress();
        var seen = new List<ModProgress>();
        foreach (var line in CleanRun.Split('\n').Select(l => l.TrimEnd('\r')))
            if (progress.Feed(line) is { } report) seen.Add(report);
        Check.That(seen.Select(r => r.Percent).SequenceEqual(seen.Select(r => r.Percent).Order()), "bar: never goes back through a whole run");
        Check.That(seen.All(r => r.Step == ModStep.Building && r.Files == SetupProgress.StepCount && r.Percent < 100), "bar: building steps, step n of 9, never 100 before the end");
        var downloads = seen.Where(r => r.File == SetupProgress.DownloadStep).ToList();
        Check.That(downloads.Count >= 5 && downloads[0].Percent < downloads[^1].Percent && downloads[^1].Percent <= SetupProgress.Percent(SetupProgress.DownloadStep, 1),
            "bar: the download step moves with the megabytes and stays inside its share");
        Check.That(seen[^1].Reason == "Writing Warriors.exe and its settings (9/9)", "bar: the text is the step's title and its number");
        var build = new SetupProgress();
        Check.That(build.FeedBuildLog("[5/10] x") is null, "build: log counters count only in the build step");
        build.Feed("[8/9] Building the game (the long step)");
        var first = build.FeedBuildLog("[188/376] Building CXX object a.obj");
        Check.That(first is { } report1 && report1.Percent == SetupProgress.Percent(SetupProgress.BuildStep, 0.5), "build: ninja's counter moves the bar inside the build step");
        Check.That(build.FeedBuildLog("[100/376] older text") is null, "build: a counter that is behind changes nothing");
    }

    private static void Failures()
    {
        const string winget = "The C++ build tools from Microsoft are not installed (Visual Studio Build Tools with 'Desktop development with C++', which includes the Windows SDK). Install them once with:\n    winget install --id Microsoft.VisualStudio.BuildTools --override \"--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended\"\nthen run warriors-setup again.";
        var noTools = SetupFailures.Classify(winget);
        Check.That(noTools.Kind == SetupFailureKind.NoBuildTools && noTools.Message.Contains("winget install --id Microsoft.VisualStudio.BuildTools") && !noTools.Message.Contains('\n'),
            "failure: no build tools names the winget command, on one line");
        Check.That(SetupFailures.Classify("Not enough free disk space on the drive for C:\\x: 1.2 GB free, 3 GB needed while building (about 1.1 GB stay afterwards). Free some space.").Kind == SetupFailureKind.DiskSpace,
            "failure: disk space");
        Check.That(SetupFailures.Classify("Could not download https://x/y.zip (timed out). Check the internet connection and run warriors-setup again; the download resumes.").Kind == SetupFailureKind.DownloadFailed
            && SetupFailures.Classify("The file downloaded from https://x does not match its expected SHA-256 and was deleted. Run again.").Kind == SetupFailureKind.DownloadFailed,
            "failure: a download that failed or was altered");
        foreach (var disc in new[]
        {
            "D:\\a.iso is not an ISO 9660 disc image (no volume descriptor). Use the .iso of your own copy of The Warriors (USA).",
            "This is not The Warriors (USA): the disc has no SLUS_212.15 (boot files found: X). Only the NTSC-U release, is supported.",
            "The disc image is missing WARRIORS.WAD; it looks like an incomplete or trimmed dump. Use a full dump of the retail disc.",
            "The game executable on this disc is not the retail one (SHA-1 abc). Patched or fan-modified images are not supported.",
        })
            Check.That(SetupFailures.Classify(disc).Kind == SetupFailureKind.WrongDisc && SetupFailures.Classify(disc).Message == disc, $"failure: wrong disc, in the tool's own words ({disc[..20]})");
        var compiler = SetupFailures.Classify("clang++.exe failed (exit code 1). Log: C:\\Games\\warriors\\work\\logs\\game.build.log");
        Check.That(compiler.Kind == SetupFailureKind.CompilerFailed && compiler.LogPath == "C:\\Games\\warriors\\work\\logs\\game.build.log" && !compiler.Message.Contains("Log:"),
            "failure: the compiler failing names the log to open");
        var crash = SetupFailures.Classify("warriors-setup hit an unexpected problem; details are in C:\\Games\\warriors\\work\\logs\\setup-crash.txt");
        Check.That(crash.Kind == SetupFailureKind.Other && crash.LogPath!.EndsWith("setup-crash.txt"), "failure: an unexpected problem names its report");
        Check.That(SetupFailures.Classify("").Kind == SetupFailureKind.Other, "failure: no words, still a failure");
    }
}
