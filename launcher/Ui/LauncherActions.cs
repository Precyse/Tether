using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace CoopLauncher.Gui;

/// <summary>The launcher's own actions that touch Windows (a folder dialog, the clipboard, Explorer) and the settings that go with
/// them, used by the web window's controller. All run on the UI thread.</summary>
internal static class LauncherActions
{
    private const string SteamOpenUri = "steam://open/main";
    private const string DiscFilter = "Disc images (*.iso)|*.iso|All files (*.*)|*.*";

    /// <summary>Asks for a game's folder; true when the player chose one that holds the game and it was saved.</summary>
    public static bool BrowseFor(GameProfile game)
    {
        if (game.Recomp != null) return BrowseForDisc(game);
        using var dialog = new FolderBrowserDialog { InitialDirectory = GameFolders.Find(game) ?? string.Empty };
        if (dialog.ShowDialog() != DialogResult.OK) return false;
        if (!File.Exists(Path.Combine(dialog.SelectedPath, game.Exe)))
        {
            Log.Info($"{game.Exe} is not in {dialog.SelectedPath}");
            return false;
        }
        AppSettings.Update(settings => settings.WithGameFolder(game.Id, dialog.SelectedPath));
        Log.Info($"{game.Name} folder set to {dialog.SelectedPath}");
        return true;
    }

    /// <summary>Asks for the disc image a game the launcher builds is made from (read, never changed); true when the player chose one and it was saved.</summary>
    public static bool BrowseForDisc(GameProfile game)
    {
        using var dialog = new OpenFileDialog { Title = game.Recomp!.DiscPrompt, Filter = DiscFilter, CheckFileExists = true };
        if (dialog.ShowDialog() != DialogResult.OK) return false;
        AppSettings.Update(settings => settings.WithOption(game.Id, RecompInstall.DiscOption, dialog.FileName));
        Log.Info($"{game.Name} disc image set to {dialog.FileName}");
        return true;
    }

    /// <summary>Shows a file in Explorer, or opens a folder.</summary>
    public static void OpenPath(string path) =>
        StartExplorer(File.Exists(path) ? $"/select,\"{path}\"" : $"\"{path}\"");

    /// <summary>A game's folder goes back to the one Steam knows.</summary>
    public static void ResetFolder(GameProfile game)
    {
        AppSettings.Update(settings => settings.WithGameFolder(game.Id, null));
        Log.Info($"{game.Name} folder reset to the one Steam knows");
    }

    /// <summary>Zips the logs for sending, puts the zip's path on the clipboard and shows the zip in Explorer.</summary>
    public static void CreateReport(IEnumerable<GameProfile> games)
    {
        if (ReportBundle.Create(games) is not { } path) return;
        try
        {
            Clipboard.SetText(path);
        }
        catch (ExternalException e)
        {
            Log.Info($"Could not copy the report path: {e.Message}");
        }
        StartExplorer($"/select,\"{path}\"");
    }

    /// <summary>Puts the lobby's code on the clipboard; true when it was copied.</summary>
    public static bool CopyLobbyCode(ulong lobbyId)
    {
        try
        {
            Clipboard.SetText(lobbyId.ToString());
            Log.Info("Lobby code copied");
            return true;
        }
        catch (ExternalException e)
        {
            Log.Info($"Could not copy the lobby code: {e.Message}");
            return false;
        }
    }

    public static void SetCheckForUpdates(bool on)
    {
        AppSettings.Update(settings => settings with { CheckForUpdates = on });
        Log.Info(on ? "Checking for updates at start" : "Not checking for updates at start");
    }

    public static void OpenLogsFolder()
    {
        try
        {
            Directory.CreateDirectory(AppData.LogsDir);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not create the logs folder: {e.Message}");
            return;
        }
        StartExplorer($"\"{AppData.LogsDir}\"");
    }

    /// <summary>Opens the game's saved sessions folder in Explorer.</summary>
    public static void OpenArchive(GameProfile game)
    {
        if (SettingsModel.ArchiveFolder(game) is { } folder) StartExplorer($"\"{folder}\"");
    }

    /// <summary>Asks the shell to open Steam; the app's own retry notices when it is up.</summary>
    public static void StartSteam()
    {
        try
        {
            Process.Start(new ProcessStartInfo(SteamOpenUri) { UseShellExecute = true });
        }
        catch (Win32Exception e)
        {
            Log.Info($"Could not start Steam: {e.Message}");
        }
    }

    private static void StartExplorer(string arguments)
    {
        try
        {
            Process.Start(new ProcessStartInfo("explorer.exe", arguments));
        }
        catch (Win32Exception e)
        {
            Log.Info($"Could not open Explorer: {e.Message}");
        }
    }
}
