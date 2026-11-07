using System.Globalization;
using System.Text.RegularExpressions;

namespace CoopLauncher;

/// <summary>
/// Reads what warriors-setup prints and turns it into one progress bar. The tool prints <c>[n/9] title</c> for each of its steps and, in the
/// download step, <c>name: 10 of 263 MB</c>; its longest step (the game build, 8) writes ninja's <c>[k/total]</c> to a log the caller reads.
/// Each step owns a share of the bar (<see cref="StepWeights"/>, measured on a clean run: the downloads 20 %, the build 65 %); inside a step
/// the bar follows what that step reports. The percent never goes down.
/// </summary>
public sealed partial class SetupProgress
{
    public const int StepCount = 9;

    /// <summary>The share of the bar of steps 0 to 9 (step 0 is the Python the tool fetches for itself); the sum is 100.</summary>
    public static readonly int[] StepWeights = [1, 2, 2, 20, 2, 2, 2, 2, 65, 2];

    /// <summary>The download size the tool prints no total for (llvm, cmake, wgpu, together about 330 MB).</summary>
    public const int ExpectedDownloadMb = 330;

    public const int DownloadStep = 3;
    public const int BuildStep = 8;

    private const int Whole = 100;

    private readonly Dictionary<string, (int Done, int Total)> _downloads = new();
    private string _currentDownload = string.Empty;
    private int _step;
    private string _title = string.Empty;
    private double _fraction;
    private int _last;

    [GeneratedRegex(@"^\[(\d+)/9\]\s+(.+?)\s*$")]
    private static partial Regex StepLine();

    [GeneratedRegex(@"^\s+(\S.*?):\s+(\d+) of (\d+) MB\s*$")]
    private static partial Regex DownloadLine();

    [GeneratedRegex(@"\[(\d+)/(\d+)\]")]
    private static partial Regex NinjaLine();

    /// <summary>True when the line opens a step: <c>[3/9] Fetching the pinned build tools</c>.</summary>
    public static bool TryParseStep(string line, out int step, out string title)
    {
        var match = StepLine().Match(line);
        step = match.Success ? int.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture) : 0;
        title = match.Success ? match.Groups[2].Value : string.Empty;
        return match.Success && step <= StepCount;
    }

    /// <summary>True when the line is a download's progress: <c>      cmake-4.2.3.zip: 10 of 48 MB</c>.</summary>
    public static bool TryParseDownload(string line, out string file, out int doneMb, out int totalMb)
    {
        var match = DownloadLine().Match(line);
        file = match.Success ? match.Groups[1].Value : string.Empty;
        doneMb = match.Success ? int.Parse(match.Groups[2].Value, CultureInfo.InvariantCulture) : 0;
        totalMb = match.Success ? int.Parse(match.Groups[3].Value, CultureInfo.InvariantCulture) : 0;
        return match.Success;
    }

    /// <summary>The newest ninja counter (<c>[17/376]</c>) in a text, as the fraction of the build done; null when there is none.</summary>
    public static double? NinjaFraction(string logText)
    {
        var last = NinjaLine().Matches(logText).LastOrDefault();
        if (last is null) return null;
        var done = int.Parse(last.Groups[1].Value, CultureInfo.InvariantCulture);
        var total = int.Parse(last.Groups[2].Value, CultureInfo.InvariantCulture);
        return total == 0 ? null : Math.Min(1.0, done / (double)total);
    }

    /// <summary>The percent of the whole run at a point inside a step (0 to 1): the steps before it, plus its share of its own weight.</summary>
    public static int Percent(int step, double fraction)
    {
        var before = StepWeights.Take(step).Sum();
        return Math.Min(Whole - 1, before + (int)(StepWeights[Math.Min(step, StepWeights.Length - 1)] * Math.Clamp(fraction, 0, 1)));
    }

    /// <summary>The step the tool is in, 0 before it said anything.</summary>
    public int Step => _step;

    /// <summary>One line of the tool's output: the report to show, or null when the line changes nothing on the bar.</summary>
    public ModProgress? Feed(string line)
    {
        if (TryParseStep(line, out var step, out var title))
        {
            _step = step;
            _title = title;
            _fraction = 0;
            return Report();
        }
        if (_step == DownloadStep && TryParseDownload(line, out var file, out var done, out var total))
        {
            _downloads[file] = (done, total);
            _currentDownload = file;
            _fraction = _downloads.Sum(entry => entry.Key == _currentDownload ? entry.Value.Done : entry.Value.Total) / (double)ExpectedDownloadMb;
            return Report();
        }
        return null;
    }

    /// <summary>The build step's own progress, from the text of the log ninja writes.</summary>
    public ModProgress? FeedBuildLog(string logText)
    {
        if (_step != BuildStep || NinjaFraction(logText) is not { } fraction || fraction <= _fraction) return null;
        _fraction = fraction;
        return Report();
    }

    private ModProgress Report()
    {
        _last = Math.Max(_last, Percent(_step, _fraction));
        return new ModProgress(ModStep.Building, _last, _step, StepCount, $"{_title} ({_step}/{StepCount})");
    }
}

/// <summary>Why a setup run failed, as the player can act on it.</summary>
public enum SetupFailureKind { NoDisc, ToolMissing, NoBuildTools, DiskSpace, DownloadFailed, WrongDisc, CompilerFailed, Cancelled, Other }

/// <summary>A failed setup run: the kind, one plain sentence (or two) that says what to do, and the log the tool names, if it names one.</summary>
public sealed record SetupFailure(SetupFailureKind Kind, string Message, string? LogPath = null);

/// <summary>Reads the tool's last words (the text after its <c>ERROR:</c> line) into a <see cref="SetupFailure"/>.</summary>
public static partial class SetupFailures
{
    public const string ErrorPrefix = "ERROR:";

    private const string NoBuildToolsMark = "C++ build tools from Microsoft are not installed";
    private const string DiskMark = "Not enough free disk space";
    private const string DownloadMark = "Could not download";
    private const string AlteredDownloadMark = "does not match its expected SHA-256";
    private const string CompilerMark = "Log:";

    private static readonly string[] DiscMarks =
    [
        "is not an ISO 9660", "does not exist. Give the path", "This is not The Warriors", "missing", "different or modified release",
        "not the retail one", "incomplete or trimmed",
    ];

    [GeneratedRegex(@"(?:Log:|details are in)\s+(\S.*?)\s*$", RegexOptions.Multiline)]
    private static partial Regex LogPathMark();

    /// <summary>The failure for the text the tool printed after <c>ERROR:</c>, line breaks folded into spaces.</summary>
    public static SetupFailure Classify(string errorText)
    {
        var text = string.Join(' ', errorText.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
        var log = LogPathMark().Match(errorText) is { Success: true } match ? match.Groups[1].Value : null;
        if (text.Contains(NoBuildToolsMark, StringComparison.Ordinal)) return new SetupFailure(SetupFailureKind.NoBuildTools, text);
        if (text.Contains(DiskMark, StringComparison.Ordinal)) return new SetupFailure(SetupFailureKind.DiskSpace, text);
        if (text.Contains(DownloadMark, StringComparison.Ordinal) || text.Contains(AlteredDownloadMark, StringComparison.Ordinal))
            return new SetupFailure(SetupFailureKind.DownloadFailed, text);
        if (DiscMarks.Any(mark => text.Contains(mark, StringComparison.Ordinal))) return new SetupFailure(SetupFailureKind.WrongDisc, text);
        if (text.Contains(CompilerMark, StringComparison.Ordinal)) return new SetupFailure(SetupFailureKind.CompilerFailed, $"The game could not be built: {text.Split(CompilerMark)[0].Trim()} Open the log for the details.", log);
        return new SetupFailure(SetupFailureKind.Other, text.Length > 0 ? text : "The build stopped without saying why.", log);
    }
}
