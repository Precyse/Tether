using System.Globalization;
using System.Text.RegularExpressions;

namespace Tether;

/// <summary>
/// Reads what warriors-setup prints and turns it into the report of the build. The tool prints <c>[n/9] title</c> for each of its steps and, in the
/// download step, <c>name: 10 of 263 MB</c>; its two compiling steps (the recompiler tools, 5, and the game, 8) write ninja's <c>[k/total]</c> to a log the
/// caller reads (<see cref="LogName"/>). A report says what the tool is doing now with the count of that step (files compiled, megabytes read) and the percent
/// that count gives; a step with no count has no percent, only its number out of nine. Nothing is estimated: no timer, no weights.
/// </summary>
public sealed partial class SetupProgress
{
    public const int StepCount = 9;
    public const int DownloadStep = 3;
    public const int RecompilerStep = 5;
    public const int BuildStep = 8;

    private const int Whole = 100;

    /// <summary>What each step of the tool does, in plain words (the tool's own titles are for its console).</summary>
    private static readonly string[] StepWords =
    [
        "starting the setup tool", "checking the disc image", "checking this PC", "downloading the build tools", "preparing the engine source",
        "building the recompiler tools", "converting the game code and graphics programs", "applying the co-op changes", "compiling the game code", "writing Warriors.exe",
    ];

    private long _done;
    private long _total;
    private string _download = string.Empty;
    private int _step;
    private string _title = string.Empty;

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

    /// <summary>The newest ninja counter (<c>[17/376]</c>) in a text; null when there is none.</summary>
    public static (int Done, int Total)? NinjaCount(string logText)
    {
        var last = NinjaLine().Matches(logText).LastOrDefault();
        if (last is null) return null;
        var total = int.Parse(last.Groups[2].Value, CultureInfo.InvariantCulture);
        return total == 0 ? null : (Math.Min(total, int.Parse(last.Groups[1].Value, CultureInfo.InvariantCulture)), total);
    }

    /// <summary>The log (in the tool's logs folder) whose ninja counter the step's progress follows; null for a step with no such log.</summary>
    public static string? LogName(int step) => step switch
    {
        RecompilerStep => "tools.build.log",
        BuildStep => "game.build.log",
        _ => null,
    };

    /// <summary>The step the tool is in, 0 before it said anything.</summary>
    public int Step => _step;

    /// <summary>One line of the tool's output: the report to show, or null when the line changes nothing.</summary>
    public ModProgress? Feed(string line)
    {
        if (TryParseStep(line, out var step, out var title))
        {
            _step = step;
            _title = title;
            _done = _total = 0;
            _download = string.Empty;
            return Report();
        }
        if (_step != DownloadStep || !TryParseDownload(line, out var file, out var done, out var total)) return null;
        _download = file;
        _done = done;
        _total = total;
        return Report();
    }

    /// <summary>The compiling step's own count, from the text of the log ninja writes; null when it has not moved forward.</summary>
    public ModProgress? FeedBuildLog(string logText)
    {
        if (LogName(_step) is null || NinjaCount(logText) is not var (done, total) || total == _total && done <= _done) return null;
        _done = done;
        _total = total;
        return Report();
    }

    /// <summary>The report: the bar is how far through the nine steps the tool is, counting the share of the current step that its count gives; the text says which.</summary>
    private ModProgress Report()
    {
        var overall = (Math.Max(_step, 1) - 1) * Whole / StepCount + (_total > 0 ? (int)(_done * Whole / _total) : 0) / StepCount;
        return new ModProgress(ModStep.Building, overall, _step, StepCount, Detail(overall));
    }

    /// <summary>What the tool is doing, with the real count of the step where it has one and the percent that gives, then the overall progress.</summary>
    private string Detail(int overall)
    {
        var words = StepWords[Math.Min(_step, StepCount)];
        var percent = _total > 0 ? $" ({_done * Whole / _total}%)" : string.Empty;
        return _step switch
        {
            DownloadStep when _total > 0 => $"downloading {_download}: {_done} of {_total} MB{percent}, overall {overall}%",
            RecompilerStep or BuildStep when _total > 0 => $"{words}: {_done} of {_total} files{percent}, overall {overall}%",
            _ => $"{words}, step {_step} of {StepCount}, overall {overall}%",
        };
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
