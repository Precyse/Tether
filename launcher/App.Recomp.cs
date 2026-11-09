namespace Tether;

/// <summary>What the loop does differently for a game the launcher builds (<see cref="RecompProfile"/>): the engine's datagram relay instead
/// of an adapter link, the build check before the start, the engine's role and ports, and the engine's goodbye when a player leaves.</summary>
public sealed partial class App
{
    private const long EngineLeaveMs = 5000;

    /// <summary>The check that runs the setup tool is named a build once it has run this long: a tool with nothing to change finishes in about two
    /// seconds and is only a check.</summary>
    private const long BuildShownAfterMs = 3000;

    private enum LaunchGate { Wait, Go, Refused }

    /// <summary>What the setup tool reported last during the start's build check: the percent of its step's count (none when the step has no count) and what it is doing.</summary>
    private sealed record BuildReading(int Percent, string Detail);

    private Task<RecompBuildResult>? _recompCheck;
    private long _recompCheckStartedMs;
    private volatile BuildReading? _buildReading;
    private EngineStatusFile? _engineStatus;
    private SessionEnd? _surfacedEnd;
    private byte[]? _builtHash;
    private GameProfile? _closingEngine;
    private long _closingEngineSinceMs;
    private bool _leaveWhenEngineGone;
    private bool _engineSeen;

    /// <summary>Raised on the main loop thread when the game has started and owns the session: the window may step aside.</summary>
    public event Action? GameTookOver;

    /// <summary>Raised on the main loop thread when the game's session needs the launcher again: the engine ended on its own, or a refusal was set.</summary>
    public event Action? AttentionNeeded;

    /// <summary>The relay of this session's engine datagrams: the host's engine listens on a free local port, a joiner's joins a port the
    /// relay opens. The build the session starts with is remembered, so a rebuild before the start cannot go unnoticed.</summary>
    private IAdapterLink OpenUdpRelay(bool isHost)
    {
        var profile = _profile!;
        _builtHash = RecompInstall.ContentHash(profile, _gameDir!);
        var log = new EngineLogTail(RecompInstall.EngineLogPath(profile, _gameDir!));
        _engineStatus = new EngineStatusFile(RecompInstall.StatusFilePath(profile, _gameDir!));
        bool EngineRunning() => GameLauncher.IsRunning(profile);
        return isHost
            ? new UdpRelayLink(RelayRole.Host, RecompLaunch.FreeUdpPort(), EngineRunning, log, _engineStatus)
            : new UdpRelayLink(RelayRole.Joiner, 0, EngineRunning, log, _engineStatus);
    }

    /// <summary>Starts the engine as this session's host (waiting for the players that are here) or joiner (on the copy of the host's memory card
    /// the save sync delivered: the snapshot holds the card's read position, not its contents).</summary>
    private LaunchResult LaunchRecomp()
    {
        if (_bridge is not UdpRelayLink relay || _session is not { } session) return LaunchResult.SteamStartFailed;
        var args = session.IsHost
            ? RecompLaunch.HostArgs(relay.EnginePort, session.PeerIds.Count + 1)
            : RecompLaunch.JoinArgs(relay.Port, RecompInstall.SessionCardPath(_profile!, _gameDir!));
        var sessionFile = WriteSessionFile(session);
        _engineStatus?.Clear();
        _surfacedEnd = null;
        if (_profile!.Recomp!.SessionArg is { } sessionArg) args = [.. args, sessionArg, sessionFile];
        return RecompLaunch.Start(_profile, _gameDir!, args);
    }

    /// <summary>Hands the game what the lobby decided (<see cref="RecompSessionFile"/>): its role and slot, who is in the session under which name, the host's
    /// setting, its memory card. Returns the file's path.</summary>
    private string WriteSessionFile(Session session)
    {
        var roster = _lobby!.Members
            .Select(member => (Member: member, Slot: session.SlotOf(member.Id)))
            .Where(entry => entry.Slot != null)
            .Select(entry => new SessionPlayer(entry.Slot!.Value, entry.Member.Name));
        var card = session.IsHost ? RecompInstall.CardPath(_profile!, _gameDir!) : RecompInstall.SessionCardPath(_profile!, _gameDir!);
        var options = AdapterOptions();
        var shared = (_profile!.Options ?? []).Where(option => option.Host && option.Key != GameOptions.SeatsKey).Select(option => option.Key).ToHashSet();
        var text = RecompSessionFile.Build(session.Epoch, _lobby.Id, session.IsHost, session.SlotOf(_transport!.LocalId) ?? SlotAssigner.HostSlot,
            GameOptions.Seats(_profile, _lobby.HostOptions), roster, card, RecompInstall.StatusFilePath(_profile, _gameDir!),
            options.Where(pair => shared.Contains(pair.Key)), options.Where(pair => (_profile.Options ?? []).Any(option => !option.Host && option.Key == pair.Key)));
        var path = RecompInstall.SessionFilePath(_profile!, _gameDir!);
        RecompSessionFile.Write(path, text);
        return path;
    }

    /// <summary>The window's Relaunch: the game again with the same role (the loop thread runs it).</summary>
    public void Relaunch() => _commands.Enqueue(() =>
    {
        if (_profile == null) return;
        if (_profile.Recomp != null) LaunchRecomp();
        else GameLauncher.Launch(_profile, _gameDir);
    });

    /// <summary>Runs the setup tool once before the start (nothing to change takes about two seconds, so it decides) and checks that it left
    /// the build the session was opened with. Wait while it runs; a failure or another build is a refusal.</summary>
    private LaunchGate CheckRecompBuild()
    {
        if (_recompCheck == null)
        {
            _recompCheckStartedMs = Environment.TickCount64;
            var progress = new ThrottledProgress(new Progress<ModProgress>(report => _buildReading = new BuildReading(report.Percent, report.Reason ?? string.Empty)), () => Environment.TickCount64);
            _recompCheck = Task.Run(() => RecompBuild.Run(_profile!, progress, CancellationToken.None));
        }
        if (!_recompCheck.IsCompleted) return LaunchGate.Wait;
        _buildReading = null;
        if (_recompCheck.IsFaulted)
        {
            Log.Info($"The {_profile!.Name} build check broke: {_recompCheck.Exception!.GetBaseException().Message}");
            Refuse(RefusalKind.BuildFailed, _recompCheck.Exception.GetBaseException().Message);
            return LaunchGate.Refused;
        }
        if (_recompCheck.Result.Failure is { } failure)
        {
            Refuse(RefusalKind.BuildFailed, failure.Message);
            return LaunchGate.Refused;
        }
        var now = RecompInstall.ContentHash(_profile!, _gameDir!);
        if (now is null || _builtHash is null || !now.AsSpan().SequenceEqual(_builtHash))
        {
            Log.Info($"The {_profile!.Name} build changed since the session opened");
            Refuse(RefusalKind.BuildDiffers);
            return LaunchGate.Refused;
        }
        return LaunchGate.Go;
    }

    /// <summary>The setup tool has been running long enough to be a build, not a check.</summary>
    private bool BuildShown() => _buildReading != null && Environment.TickCount64 - _recompCheckStartedMs >= BuildShownAfterMs;

    /// <summary>Leave: the engine says goodbye to the other engines through the relay when it is asked to close, and the session ends once it is
    /// gone (or after <see cref="EngineLeaveMs"/>). False when there is no engine to wait for.</summary>
    private bool AskEngineToLeave()
    {
        if (_profile is not { Recomp: not null } profile || _session == null || !GameLauncher.IsRunning(profile)) return false;
        CloseEngine(profile);
        _leaveWhenEngineGone = true;
        return true;
    }

    /// <summary>A session that ends other ways (the host left, the player was removed): the engine is asked to close too, it has no one to play with.</summary>
    private void ReleaseEngine()
    {
        _leaveWhenEngineGone = false;
        if (_profile is { Recomp: not null } profile && GameLauncher.IsRunning(profile)) CloseEngine(profile);
    }

    private void CloseEngine(GameProfile profile)
    {
        if (_closingEngine != null) return;
        Log.Info($"Asking {profile.Name} to close");
        GameCloser.RequestClose(profile);
        _closingEngine = profile;
        _closingEngineSinceMs = Environment.TickCount64;
    }

    /// <summary>An engine that was asked to close: the session ends when it is gone, and an engine that does not go is ended.</summary>
    private void PumpEngine()
    {
        WatchEngine();
        if (_closingEngine is not { } profile) return;
        if (GameCloser.IsGone(profile))
        {
            _closingEngine = null;
            if (!_leaveWhenEngineGone) return;
            _leaveWhenEngineGone = false;
            EndSession();
        }
        else if (Environment.TickCount64 - _closingEngineSinceMs >= EngineLeaveMs)
        {
            Log.Info($"{profile.Name} did not close, ending it");
            GameCloser.Kill(profile);
        }
    }

    /// <summary>Once the game runs it owns the session and shows who is in it: the window steps aside (<see cref="GameTookOver"/>) and comes back only when the
    /// engine ends without being asked to (<see cref="AttentionNeeded"/>); a refusal does the same (<see cref="Refuse(Refusal)"/>). When the engine says its session
    /// ended (<c>[net] session ended (&lt;code&gt;): &lt;why&gt;</c>) it goes back to its title and plays on alone: the refusal is shown at once, with the engine's reason.
    /// An engine that is gone without having said so is a crash unless it closed on purpose (stage Closed in its status file); an older engine says nothing, and its
    /// last [net] line decides (<see cref="NetLog.EndRefusal"/>).</summary>
    private void WatchEngine()
    {
        if (_profile is not { Recomp: not null } || _bridge is not UdpRelayLink link) return;
        if (link.Ended is { } ended && !ReferenceEquals(ended, _surfacedEnd))
        {
            _surfacedEnd = ended;
            Refuse(EndedRefusal(ended));
        }
        if (link.IsReady) _engineSeen = true;
        else if (_engineSeen)
        {
            _engineSeen = false;
            if (_closingEngine != null || _surfacedEnd != null) return;
            if (NetLog.Gone(link.State) is var (kind, detail)) Refuse(new Refusal(kind, detail, _profile.Id, _lobby?.Id ?? 0, Ended: true));
            else AttentionNeeded?.Invoke();
        }
    }

    /// <summary>The end the engine reported as a refusal that ends the session. The engine cannot name the host: an unreachable one is named here.</summary>
    private Refusal EndedRefusal(SessionEnd ended) =>
        new(ended.Kind, ended.Kind == RefusalKind.HostUnreachable ? MemberName(_hostId) : ended.Reason, _profile!.Id, _lobby?.Id ?? 0, Ended: true);

    /// <summary>The session's files go with the session: the file the game was told, and (by the save sync's reset) a guest's copy of the card.</summary>
    private void DropRecompSession()
    {
        _recompCheck = null;
        _builtHash = null;
        _buildReading = null;
        _engineSeen = false;
        _surfacedEnd = null;
        _engineStatus?.Clear();
        _engineStatus = null;
        if (_profile is { Recomp: not null } built && _gameDir != null) RecompSessionFile.Delete(RecompInstall.SessionFilePath(built, _gameDir));
    }
}
