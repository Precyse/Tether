namespace Tether.Gui;

/// <summary>The words for a refused start.</summary>
public static class StatusText
{
    private const string FallbackHost = "The host";
    private const string EndedFormat = "Session ended ({0}): {1}";
    private const string EndedForFormat = "Session ended for {0} ({1}): {2}";
    private const string BuildDiffersAdvice = "update Tether, then press Rebuild";

    /// <summary><paramref name="host"/> names the host where the words do (the other side of a build difference, an unreachable host). The end of a session that
    /// was going reads <c>Session ended (&lt;code&gt;): &lt;why&gt;</c>, the code being the refusal's name, which the game shows too.</summary>
    public static string Refusal(Refusal refusal, string host = "") =>
        refusal.Ended ? string.Format(EndedFormat, refusal.Kind, EndedReason(refusal, host)) : Plain(refusal, host);

    /// <summary>Why a session ended, in a plain sentence that says what to do where there is something to do: the game's or the launcher's own reason
    /// (the detail) where it gave one, else the words of the kind.</summary>
    private static string EndedReason(Refusal refusal, string host) => refusal.Kind switch
    {
        RefusalKind.HostLeft => OrDetail(refusal, "the host left the game"),
        RefusalKind.Removed => "the host removed you from the lobby",
        RefusalKind.RejoinGaveUp => "could not get back into the lobby: join again with the host's code",
        RefusalKind.GameDesync => OrDetail(refusal, "the players' games no longer match: start a new game"),
        RefusalKind.PeerGone => OrDetail(refusal, "a player stopped answering"),
        RefusalKind.BuildDiffers => OrDetail(refusal, YourBuildDiffers(string.Empty)),
        RefusalKind.GameCrashed => OrDetail(refusal, "the game stopped unexpectedly: start it again from Tether"),
        RefusalKind.LoadFailed => OrDetail(refusal, "a mission or the host's game could not be loaded: start the game again"),
        _ => Plain(refusal, host),
    };

    private static string OrDetail(Refusal refusal, string words) => refusal.Detail.Length > 0 ? refusal.Detail : words;

    /// <summary>The words of a refusal that is not the end of a session.</summary>
    private static string Plain(Refusal refusal, string host) => refusal.Kind switch
    {
        RefusalKind.LauncherBuild => LauncherBuild(refusal.Detail),
        RefusalKind.GameBuild => GameBuild(refusal.Detail, host),
        RefusalKind.ModFiles => "Mod files differ",
        RefusalKind.ModNotInstalled => IsBuiltGame(refusal.GameId) ? "Game not built: pick your disc and press Build" : "Mod not installed",
        RefusalKind.BuildDiffers => YourBuildDiffers(refusal.Detail),
        RefusalKind.GameDesync => "The game stopped: the players' games no longer match",
        RefusalKind.PeerGone => "The game stopped: the other players are gone",
        RefusalKind.GameCrashed => $"The game stopped unexpectedly{(refusal.Detail.Length > 0 ? $" {refusal.Detail}" : string.Empty)}: start it again from Tether",
        RefusalKind.LoadFailed => refusal.Detail.Length > 0 ? refusal.Detail : "A mission or the host's game could not be loaded: start the game again",
        RefusalKind.BuildFailed => refusal.Detail.Length > 0 ? $"The game could not be built: {refusal.Detail}" : "The game could not be built",
        RefusalKind.GameUnsupported => GameUnsupported(refusal),
        RefusalKind.GameNotFound => "Game not found",
        RefusalKind.RequirementMissing => refusal.Detail.Length > 0 ? Requirement(refusal.Detail, otherVersion: false) : "Required file not found",
        RefusalKind.RequirementOtherVersion => Requirement(refusal.Detail, otherVersion: true),
        RefusalKind.UnknownGame => "Game not in this launcher",
        RefusalKind.LobbyFull => "Lobby full",
        RefusalKind.LobbyClosed => "Lobby closed",
        RefusalKind.LobbyNotAllowed => "Lobby join refused",
        RefusalKind.LobbyFailed => "Lobby join failed",
        RefusalKind.NotCoopLobby => "Not a co-op lobby",
        RefusalKind.SteamOffline => "Steam offline",
        RefusalKind.SaveTimedOut => "Save transfer timed out",
        RefusalKind.SaveFolderMissing => "Save folder not found",
        RefusalKind.SaveDiffers => "Save differs from host's",
        RefusalKind.HostLeft => "Host left",
        RefusalKind.Removed => "Removed from the lobby",
        RefusalKind.RejoinGaveUp => "Could not rejoin",
        RefusalKind.PortInUse => $"Port {refusal.Detail} in use",
        RefusalKind.LaunchFailed => "Game did not start",
        RefusalKind.HostUnreachable => $"Can't reach {(host.Length > 0 ? host : refusal.Detail.Length > 0 ? refusal.Detail : "the host")} through Steam",
        RefusalKind.GameRunning => $"Close {(refusal.Detail.Length > 0 ? refusal.Detail : "the game")}, then start again",
        RefusalKind.SaveResignFailed => $"{(refusal.Detail.Length > 0 ? refusal.Detail : "A save file")} could not be converted to your Steam account",
        RefusalKind.FrameworkNotLoaded => $"{(refusal.Detail.Length > 0 ? refusal.Detail : "The mod loader")} did not load",
        RefusalKind.ModNotLoaded => "The co-op mod did not load",
        RefusalKind.OtherCoopMod => $"Another co-op mod ({(refusal.Detail.Length > 0 ? refusal.Detail : "unknown")}) is installed",
        RefusalKind.ModNotStarted => refusal.Detail.Length > 0 ? $"The co-op mod could not start: {refusal.Detail}" : "The co-op mod could not start",
        RefusalKind.SaveNotProtected => "The mod could not protect your save: quit to the title",
        _ => refusal.Kind.ToString(),
    };

    /// <summary>What another player's refusal says on its card: the player is named where the words are about it.</summary>
    public static string RefusalOf(Refusal refusal, string name) =>
        refusal.Ended ? string.Format(EndedForFormat, name, refusal.Kind, EndedReasonOf(refusal, name)) : PlainOf(refusal, name);

    private static string EndedReasonOf(Refusal refusal, string name) => refusal.Kind switch
    {
        RefusalKind.BuildDiffers => OrDetail(refusal, TheirBuildDiffers(name, string.Empty)),
        RefusalKind.Removed => "the host removed them from the lobby",
        RefusalKind.RejoinGaveUp => "could not get back into the lobby",
        _ => EndedReason(refusal, string.Empty),
    };

    private static string PlainOf(Refusal refusal, string name) => refusal.Kind switch
    {
        RefusalKind.BuildDiffers => TheirBuildDiffers(name, refusal.Detail),
        RefusalKind.BuildFailed => $"{name}'s game could not be built{(refusal.Detail.Length > 0 ? $" ({refusal.Detail})" : string.Empty)}: {name} must fix it, then rebuild",
        RefusalKind.ModNotInstalled when IsBuiltGame(refusal.GameId) => $"{name} has not built the game: {name} must pick the disc and press Build",
        RefusalKind.HostUnreachable => $"{name} can't reach {(refusal.Detail.Length > 0 ? refusal.Detail : "the host")} through Steam: {name} must check the connection and join again",
        RefusalKind.SaveTimedOut => $"{name}'s save transfer timed out: {name} must join again",
        RefusalKind.GameRunning => $"{name} must close {(refusal.Detail.Length > 0 ? refusal.Detail : "the game")}, then start again",
        RefusalKind.LaunchFailed => $"{name}'s game did not start: {name} must start it again from Tether",
        RefusalKind.GameCrashed => $"{name}'s game stopped unexpectedly{(refusal.Detail.Length > 0 ? $" {refusal.Detail}" : string.Empty)}",
        RefusalKind.LoadFailed => $"{name}'s game could not load{(refusal.Detail.Length > 0 ? $": {refusal.Detail}" : string.Empty)}",
        RefusalKind.LauncherBuild when RefusalVersions.TryParse(refusal.Detail, out var host, out var you) =>
            host > you ? $"{name} needs launcher {host}" : $"Host needs launcher {you}",
        RefusalKind.GameBuild when RefusalVersions.TryParse(refusal.Detail, out var hostBuild, out var youBuild) =>
            hostBuild > youBuild ? $"{name} has an older game version: update it in Steam" : "Your game version is older: update it in Steam",
        RefusalKind.SaveResignFailed => $"{name} could not use the save",
        _ => Plain(refusal, string.Empty),
    };

    /// <summary>This machine's build is not the host's. The detail names the parts that differ (<see cref="RecompBuildInfo.Differences"/>).</summary>
    private static string YourBuildDiffers(string detail) =>
        detail.Length > 0 ? $"Your build differs from the host's ({detail}): {BuildDiffersAdvice}" : $"Your build differs from the host's: {BuildDiffersAdvice}";

    private static string TheirBuildDiffers(string name, string detail) =>
        detail.Length > 0 ? $"{name}'s build differs from the host's ({detail}): {name} must update" : $"{name}'s build differs from the host's: {name} must update";

    /// <summary>The host's launcher is older than this one, or this one is older than the host's (the detail is host/you).</summary>
    private static string LauncherBuild(string detail)
    {
        if (!RefusalVersions.TryParse(detail, out var host, out var you)) return "Launcher build differs";
        return host > you ? $"Your launcher is out of date (host {host}, you {you})" : $"Host needs launcher {you} (host {host}, you {you})";
    }

    /// <summary>Steam build ids only grow, so the lower one is the machine that has to update (the detail is host/you).</summary>
    private static string GameBuild(string detail, string host)
    {
        if (!RefusalVersions.TryParse(detail, out var hostBuild, out var you)) return "Game version differs";
        return hostBuild > you
            ? "Your game version is older than the host's: update it in Steam"
            : $"{(host.Length > 0 ? host : FallbackHost)} has an older game version: update it in Steam";
    }

    /// <summary>The installed Steam build is not one the mod supports (the detail is the installed build).</summary>
    private static string GameUnsupported(Refusal refusal)
    {
        if (refusal.GameId == null || !GameProfile.ListIds().Contains(refusal.GameId)) return "Game build unsupported";
        var profile = GameProfile.Load(refusal.GameId);
        var supported = profile.SupportedBuilds is { Count: > 0 } builds ? builds[^1].ToString() : string.Empty;
        var installed = refusal.Detail.Length > 0 ? $" (build {refusal.Detail})" : string.Empty;
        return supported.Length > 0 ? $"{profile.Name} was updated by Steam{installed}; this mod supports {supported}" : "Game build unsupported";
    }

    /// <summary>The game is one the launcher builds from the player's disc (<see cref="RecompProfile"/>), so there is no mod to install.</summary>
    private static bool IsBuiltGame(string? gameId) => gameId != null && GameProfile.ListIds().Contains(gameId) && GameProfile.Load(gameId).Recomp != null;

    /// <summary>The words for a required file the game folder does not satisfy: absent, or another version than the accepted ones.</summary>
    public static string Requirement(string name, bool otherVersion) => otherVersion ? $"{name} is another version" : $"{name} not found";
}
