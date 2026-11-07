using System.Diagnostics;
using System.Text;

namespace CoopLauncher;

/// <summary>How a setup run ended: it made a complete build, or <see cref="Failure"/> says why not.</summary>
public sealed record RecompBuildResult(SetupFailure? Failure)
{
    public bool Success => Failure is null;

    public static RecompBuildResult Ok { get; } = new((SetupFailure?)null);

    public static RecompBuildResult Failed(SetupFailureKind kind, string message, string? logPath = null) => new(new SetupFailure(kind, message, logPath));
}

/// <summary>
/// Runs the setup tool for a <see cref="RecompProfile"/> game as a child process: <c>--iso &lt;disc&gt; --home &lt;game folder&gt; --cache &lt;cache&gt;</c>.
/// Its output becomes progress (<see cref="SetupProgress"/>), its error a plain-language failure (<see cref="SetupFailures"/>), a cancel
/// ends the whole process tree (the next run resumes: downloads continue, finished steps are skipped, ninja carries on). With nothing
/// to change the tool finishes in about two seconds, so the launcher runs it before every start and lets it decide.
/// </summary>
public static class RecompBuild
{
    private const int PollMs = 250;
    private const string CacheFolder = "cache";
    private const string BuildLogName = "game.build.log";
    private const int BuildLogTailBytes = 4096;
    private const string CancelledWords = "Cancelled. Press Retry to continue where it stopped.";

    /// <summary>Builds (or checks) the game. Progress goes to <paramref name="progress"/> as the tool reports it; the caller reports the end.</summary>
    public static RecompBuildResult Run(GameProfile profile, IProgress<ModProgress>? progress, CancellationToken cancel)
    {
        var recomp = profile.Recomp!;
        var disc = RecompInstall.DiscPath(profile);
        if (disc is null) return RecompBuildResult.Failed(SetupFailureKind.NoDisc, $"{recomp.DiscPrompt} is not set, or the file is gone. Pick it in the settings.");
        var tool = RecompInstall.SetupPath(profile);
        if (!File.Exists(tool)) return RecompBuildResult.Failed(SetupFailureKind.ToolMissing, $"The setup tool was not found at {tool}.");
        var gameDir = RecompInstall.GameDir(recomp);
        try
        {
            Directory.CreateDirectory(gameDir);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return RecompBuildResult.Failed(SetupFailureKind.Other, $"The game folder {gameDir} could not be made: {e.Message}");
        }
        var start = new ProcessStartInfo(tool)
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8, WorkingDirectory = Path.GetDirectoryName(tool)!,
        };
        foreach (var argument in new[] { "--iso", disc, "--home", gameDir, "--cache", Path.Combine(gameDir, CacheFolder) }) start.ArgumentList.Add(argument);
        Log.Info($"Building {profile.Name}: {tool} --iso \"{disc}\" --home \"{gameDir}\"");
        return Execute(profile, start, Path.Combine(RecompInstall.LogsDir(profile, gameDir), BuildLogName), progress, cancel);
    }

    private static RecompBuildResult Execute(GameProfile profile, ProcessStartInfo start, string buildLog, IProgress<ModProgress>? progress, CancellationToken cancel)
    {
        var tracker = new SetupProgress();
        var error = new StringBuilder();
        var inError = false;
        var gate = new object();
        void OnLine(string? line)
        {
            if (line is null) return;
            lock (gate)
            {
                if (line.StartsWith(SetupFailures.ErrorPrefix, StringComparison.Ordinal)) inError = true;
                if (inError) error.AppendLine(line.StartsWith(SetupFailures.ErrorPrefix, StringComparison.Ordinal) ? line[SetupFailures.ErrorPrefix.Length..].Trim() : line);
                else if (tracker.Feed(line) is { } report) progress?.Report(report);
            }
        }
        var startedUtc = DateTime.UtcNow;
        using var process = Process.Start(start) ?? throw new InvalidOperationException("The setup tool did not start");
        process.OutputDataReceived += (_, e) => OnLine(e.Data);
        process.ErrorDataReceived += (_, e) => OnLine(e.Data);
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        while (!process.WaitForExit(PollMs))
        {
            if (cancel.IsCancellationRequested)
            {
                process.Kill(entireProcessTree: true);
                process.WaitForExit();
                Log.Info($"The {profile.Name} build was cancelled");
                return RecompBuildResult.Failed(SetupFailureKind.Cancelled, CancelledWords);
            }
            lock (gate)
            {
                if (ReadBuildLog(buildLog, startedUtc) is { } text && tracker.FeedBuildLog(text) is { } report) progress?.Report(report);
            }
        }
        process.WaitForExit();
        if (process.ExitCode == 0) return RecompBuildResult.Ok;
        lock (gate)
        {
            var failure = SetupFailures.Classify(error.Length > 0 ? error.ToString() : $"The setup tool stopped with exit code {process.ExitCode}.");
            Log.Info($"The {profile.Name} build failed ({failure.Kind}): {failure.Message}");
            return new RecompBuildResult(failure);
        }
    }

    /// <summary>The tail of ninja's log while the game builds; null when it is not there yet.</summary>
    private static string? ReadBuildLog(string path, DateTime startedUtc)
    {
        try
        {
            if (File.GetLastWriteTimeUtc(path) < startedUtc) return null;  // the log of an earlier run: a step the tool skips leaves it as it was
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            stream.Seek(Math.Max(0, stream.Length - BuildLogTailBytes), SeekOrigin.Begin);
            return new StreamReader(stream).ReadToEnd();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
