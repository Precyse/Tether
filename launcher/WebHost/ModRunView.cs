using Tether.Gui;

namespace Tether.Web;

/// <summary>Which mod action a run is: installing (also updating and repairing) or uninstalling.</summary>
internal enum ModRunKind { Install, Uninstall }

/// <summary>The mod action of one game on show: what it is and the last thing it reported. A run that ended Done is shown for a
/// moment; one that Failed stays until it is retried or its cause is gone. Cancellable: a build the player can stop; LogPath: the log
/// a failed build names (a file or a folder). BuildToolsMissing: the failure is the missing Visual Studio Build Tools, which the Install build tools button sets up.</summary>
internal sealed record ModRunView(ModRunKind Kind, ModProgress Progress, bool Cancellable = false, string? LogPath = null, bool BuildToolsMissing = false)
{
    public bool Running => Progress.Step is not (ModStep.Done or ModStep.Failed);

    public bool Failed => Progress.Step == ModStep.Failed;
}

/// <summary>The running (or last) build of a game the launcher builds: the source that ends it, and the log its failure names.</summary>
internal sealed class BuildJob
{
    public CancellationTokenSource Cancel { get; } = new();

    public string? LogPath { get; set; }

    public bool BuildToolsMissing { get; set; }
}

/// <summary>The pure mapping of a mod run to what the page draws (the step list's row: the step text, a percent and its bar, a
/// green check at the end, a red cross with the reason and a Retry on a failure) and to the buttons it locks.</summary>
internal static class ModRunViews
{
    public const string RetryLabel = "Retry";
    public const string CancelLabel = "Cancel";
    public const string LogLabel = "Open log";
    public const string InstallToolsLabel = "Install build tools";
    private const double PercentScale = 100.0;

    private static readonly Dictionary<ModStep, string> Labels = new()
    {
        [ModStep.Checking] = "Checking files",
        [ModStep.BackingUp] = "Backing up",
        [ModStep.Copying] = "Copying",
        [ModStep.Verifying] = "Verifying",
        [ModStep.RemovingOld] = "Removing old files",
        [ModStep.Removing] = "Removing files",
        [ModStep.Downloading] = "Downloading",
        [ModStep.Installing] = "Installing",
        [ModStep.Building] = "Building game",
        [ModStep.Done] = "Done",
    };

    public static ModRunDto Dto(ModRunView run)
    {
        var progress = run.Progress;
        if (run.Failed)
            return new ModRunDto(new StepDto(progress.Reason ?? "Failed", StepMark.Failed, string.Empty, null, false, [], []), true, new BtnDto(RetryLabel, true),
                Log: run.LogPath != null ? new BtnDto(LogLabel, true) : null, Fix: run.BuildToolsMissing ? new BtnDto(InstallToolsLabel, true) : null);
        var mark = progress.Step == ModStep.Done ? StepMark.Done : StepMark.Active;
        var counted = progress.Percent != ProgressMath.NoPercent;
        return new ModRunDto(new StepDto(Text(progress), mark, counted ? $"{progress.Percent}%" : string.Empty, counted ? progress.Percent / PercentScale : null, !counted, [], []),
            false, null, run.Cancellable ? new BtnDto(CancelLabel, true) : null);
    }

    /// <summary>The step's name, with the file out of the files of the step when there are several; the note a step carries replaces it.</summary>
    public static string Text(ModProgress progress) =>
        progress.Step == ModStep.Building && progress.Reason is { } doing ? $"{Labels[ModStep.Building]}: {doing}" :
        progress.Reason is { } note && progress.Step != ModStep.Failed ? note :
        progress.Files > 1 && progress.File > 0 && progress.Step != ModStep.Done ? $"{Labels[progress.Step]} {progress.File}/{progress.Files}" : Labels[progress.Step];

    /// <summary>The home screen while a mod action runs: nothing that starts a session or another action can be pressed.</summary>
    public static HomeView WhileRunning(HomeView view, bool running) =>
        running ? view with { HostEnabled = false, LocalEnabled = false, JoinEnabled = false, CodeEnabled = false, ModEnabled = false } : view;
}
