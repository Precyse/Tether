namespace Tether;

/// <summary>The launcher's per-user folder (%AppData%\Tether, or the folder named by COOP_LAUNCHER_DATA, which tests
/// point at a temp folder): its settings, its logs, the pictures it took from games and the web view's data. The folder of the
/// launcher's former name (%AppData%\CoopLauncher) is moved to it at the first use; while it cannot be moved (the former
/// launcher still runs in it) that folder keeps being used.</summary>
public static class AppData
{
    /// <summary>The timestamp every file the launcher names by time uses (archives, reports, peer logs).</summary>
    public const string StampFormat = "yyyyMMdd-HHmmss";

    public const string DataRootVariable = "COOP_LAUNCHER_DATA";

    private const string FolderName = "Tether";
    private const string FormerFolderName = "CoopLauncher";
    private const string SettingsFileName = "settings.json";
    private const string LogsFolderName = "logs";
    private const string ArtFolderName = "art";
    private const string WebViewFolderName = "webview2";
    private const string LogFileName = "launcher.log";
    private const string PreviousLogFileName = "launcher.prev.log";

    public static string SettingsFile => Path.Combine(Root, SettingsFileName);

    public static string LogsDir => Path.Combine(Root, LogsFolderName);

    public static string LogFile => Path.Combine(LogsDir, LogFileName);

    /// <summary>Pictures taken from a game's files, one folder per game.</summary>
    public static string ArtDir => Path.Combine(Root, ArtFolderName);

    /// <summary>The WebView2 user data folder (cache, cookies and the browser's own files of the window's page).</summary>
    public static string WebViewDir => Path.Combine(Root, WebViewFolderName);

    /// <summary>The log of the run before this one.</summary>
    public static string PreviousLogFile => Path.Combine(LogsDir, PreviousLogFileName);

    /// <summary>A path relative to the launcher's data folder (where games the launcher builds live).</summary>
    public static string Resolve(string relative) => Path.GetFullPath(Path.Combine(Root, relative));

    private static readonly Lazy<string> UserFolder = new(() => MoveFormerFolder(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData)));

    private static string Root =>
        Environment.GetEnvironmentVariable(DataRootVariable) is { Length: > 0 } custom ? custom : UserFolder.Value;

    /// <summary>The launcher's folder in <paramref name="appData"/>: the former one is moved to it when only that exists, and stays in use when it cannot be moved.</summary>
    internal static string MoveFormerFolder(string appData)
    {
        var current = Path.Combine(appData, FolderName);
        var former = Path.Combine(appData, FormerFolderName);
        if (Directory.Exists(current) || !Directory.Exists(former)) return current;
        try
        {
            Directory.Move(former, current);
            return current;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return former;
        }
    }
}
