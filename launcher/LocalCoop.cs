namespace Tether;

/// <summary>
/// A local co-op session of a built game: every player on this PC, the controllers (and the keyboard for player 1) playing, no lobby and no link. The game gets a
/// session file with <c>role=local</c> and starts straight into its own co-op front end; players 2 to 4 join the game's own way. The settings are the player's
/// own option values, leaving out the Online ones (seats, input delay, the controller a window reads).
/// </summary>
public static class LocalCoop
{
    private const uint LocalEpoch = 0;
    private const ulong LocalSessionId = 0;

    /// <summary>Whether the game can start a local session.</summary>
    public static bool Supported(GameProfile profile) => profile.Recomp is { LocalCoop: true, SessionArg: not null };

    /// <summary>The session file's text for these option values.</summary>
    public static string SessionText(GameProfile profile, string gameDir, IReadOnlyDictionary<string, string> options)
    {
        var offline = (profile.Options ?? []).Where(option => !option.Online).ToList();
        KeyValuePair<string, string>[] Of(bool host) => offline.Where(option => option.Host == host && options.ContainsKey(option.Key))
            .Select(option => new KeyValuePair<string, string>(option.Key, options[option.Key])).ToArray();
        return RecompSessionFile.Build(LocalEpoch, LocalSessionId, SessionRole.Local, SlotAssigner.HostSlot, profile.MaxPlayers, [], RecompInstall.CardPath(profile, gameDir),
            RecompInstall.StatusFilePath(profile, gameDir), Of(host: true), Of(host: false));
    }

    /// <summary>Writes the session file and starts the game with it. Anything but Started or AlreadyRunning means nothing started.</summary>
    public static LaunchResult Start(GameProfile profile, string gameDir)
    {
        if (!Supported(profile)) return LaunchResult.GameUnsupported;
        var path = RecompInstall.SessionFilePath(profile, gameDir);
        if (!RecompSessionFile.Write(path, SessionText(profile, gameDir, GameOptions.Current(profile, gameDir)))) return LaunchResult.SteamStartFailed;
        return RecompLaunch.Start(profile, gameDir, [profile.Recomp!.SessionArg!, path]);
    }
}
