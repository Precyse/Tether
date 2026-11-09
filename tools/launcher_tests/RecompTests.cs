using System.Security.Cryptography;
using System.Text;
using Tether;
using Tether.Gui;
using Tether.Web;

namespace LauncherTests;

/// <summary>A game with no Steam app that the launcher builds (The Warriors): its profile, the state of its build folder, the fingerprint two
/// machines must share, the words and the buttons of a build.</summary>
public static class RecompTests
{
    private const string BuildJson = """
        { "fingerprint": { "game": "16ab34b74bab36eb", "runtime": "6f77a002e4bfd1af" }, "patch_list_sha256": "ad81", "engine_sha256": "{engine}" }
        """;

    private const string ProfileJson = """
        { "id": "built", "name": "Built", "exe": "game/B.exe", "maxPlayers": 2, "port": 0, "adapterFiles": [], "adapterIni": "game/b.ini",
          "recomp": { "setup": "tools/b.cmd", "discPrompt": "Disc", "installDir": "games/b", "buildInfo": "game/build.json", "engine": "game/engine/e.exe",
                      "logsDir": "work/logs", "engineLog": "game/logs/b.log", "card": "game/saves/card.ps2", "sessionCard": "game/saves/session/card.ps2", "sessionFile": "game/session.ini", "statusFile": "game/status.txt" } }
        """;

    public static void Run()
    {
        Profile();
        BuildFolder();
        Fingerprint();
        Words();
        RunView();
        SessionFile();
    }

    /// <summary>The names of the refusals as the game may use them for reason codes: only ever appended to.</summary>
    private const string RefusalCodes = "LauncherBuild GameBuild ModFiles ModNotInstalled GameUnsupported GameNotFound RequirementMissing UnknownGame LobbyFull LobbyClosed LobbyNotAllowed "
        + "LobbyFailed NotCoopLobby SteamOffline SaveTimedOut SaveFolderMissing SaveDiffers HostLeft Removed RejoinGaveUp PortInUse LaunchFailed RequirementOtherVersion HostUnreachable "
        + "GameRunning SaveResignFailed FrameworkNotLoaded ModNotLoaded SaveNotProtected OtherCoopMod ModNotStarted BuildDiffers BuildFailed GameDesync PeerGone GameCrashed LoadFailed";

    private static void SessionFile()
    {
        var roster = new[] { new SessionPlayer(1, "Bo"), new SessionPlayer(0, "Amy=\r\n[host]") };
        var text = RecompSessionFile.Build(7, 109775240912345, host: false, localSlot: 1, maxPlayers: 4, roster, @"C:\g\saves\session\card.ps2", @"C:\g\status.txt",
            [new("friendly_fire", "1")], [new("scale", "2")]);
        var lines = text.Split(["\r\n", "\n"], StringSplitOptions.RemoveEmptyEntries);
        Check.That(lines.SequenceEqual(["[session]", "epoch=7", "session_id=109775240912345", "role=guest", "local_slot=1", "max_players=4", @"status_file=C:\g\status.txt", "[roster]", "0=Amy   [host]", "1=Bo", "2=", "3=",
            "[host]", "profile_slot=-1", @"memcard=C:\g\saves\session\card.ps2", "friendly_fire=1", "[local]", "scale=2"]), "session file: the sections and keys of the game's design, the roster by slot with open slots, a name kept on its line");
        var path = Path.Combine(Path.GetTempPath(), $"session_{Guid.NewGuid():N}", "game", "session.ini");
        try
        {
            Check.That(RecompSessionFile.Write(path, text) && File.ReadAllText(path) == text && !File.Exists(path + ".tmp"), "session file: written whole, no temporary left");
            RecompSessionFile.Delete(path);
            Check.That(!File.Exists(path), "session file: removed with the session");
        }
        finally
        {
            Directory.Delete(Path.GetDirectoryName(Path.GetDirectoryName(path))!, recursive: true);
        }
        var warriors = GameProfile.Load("warriors");
        Check.That(warriors.SaveSync is { FilePattern: "card.ps2" } sync && sync.SessionDir + "/card.ps2" == warriors.Recomp!.SessionCard && warriors.Recomp.Card.EndsWith("/card.ps2")
            && warriors.Recomp.SessionArg == "--coop-session" && warriors.Recomp.StatusFile == "game/status.txt", "card: the host's card is what the save sync sends, a guest gets it in the session folder its engine plays on");
        Check.That(Path.Combine(SaveSyncCoordinator.HostSaveDir(warriors, warriors.SaveSync!, null)!, "card.ps2") == RecompInstall.CardPath(warriors, RecompInstall.GameDir(warriors.Recomp!)).Replace('/', '\\'),
            "card: the folder the save sync sends from holds the card the host's game plays on");
        Check.That(NetLog.EndRefusal(GameNet.Desync) == RefusalKind.GameDesync && NetLog.EndRefusal(GameNet.Refused) == RefusalKind.BuildDiffers
            && NetLog.EndRefusal(GameNet.Failed) == RefusalKind.PeerGone && NetLog.EndRefusal(GameNet.Stalled) == RefusalKind.PeerGone
            && NetLog.EndRefusal(GameNet.InSync) is null && NetLog.EndRefusal(GameNet.PeerLeft) is null && NetLog.EndRefusal(GameNet.Unknown) is null,
            "exit: an engine that ended after a desync, a refused build or a lost peer is a refusal, one that ended in sync is not");
        Check.That(string.Join(' ', Enum.GetNames<RefusalKind>()) == RefusalCodes, "refusal codes: the names are stable, new ones come last");
    }

    private static void Profile()
    {
        var warriors = GameProfile.Load("warriors");
        Check.That(warriors is { SteamAppId: 0, MaxPlayers: 4, SessionKind: SessionKind.Lobby } && warriors.Recomp is { } recomp && recomp.InstallDir == "games/warriors",
            "profile: the warriors profile has no Steam app, 4 players and a build folder");
        Check.That(warriors.Options!.Single(o => o.Key == GameOptions.SeatsKey) is { Host: true } seats && seats.Values.Select(v => v.Value).SequenceEqual(["4", "3", "2"]),
            "profile: the host picks 2 to 4 players");
        Check.That(warriors.PeerBudget is { UnreliableBytesPerSecond: > 1024 * 1024 }, "profile: a bigger budget for the snapshot's datagrams");
        var parsed = GameProfile.Parse("built", ProfileJson, RepoPaths.RepoRoot);
        Check.That(parsed.SteamAppId == 0 && parsed.Recomp!.Setup == "tools/b.cmd" && parsed.Recomp.SessionCard == "game/saves/session/card.ps2" && parsed.PeerBudget is null,
            "profile: a profile without steamAppId parses, its recomp block with it");
        Check.That(GameFolders.Find(parsed) == RecompInstall.GameDir(parsed.Recomp!) && Directory.GetParent(GameFolders.Find(parsed)!)!.Name == "games",
            "folder: the game lives under the launcher's data folder, built or not");
        Check.That(GameBuilds.Installed(parsed, null) == GameBuilds.Unknown && GameBuilds.IsSupported(parsed, GameBuilds.Unknown), "build: no Steam build to compare");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.ModNotInstalled, GameId: "warriors")) == "Game not built: pick your disc and press Build"
            && StatusText.Refusal(new Refusal(RefusalKind.ModNotInstalled, GameId: "re0")) == "Mod not installed", "words: a built game is not built, a modded game has no mod");
    }

    private static void BuildFolder()
    {
        var root = Path.Combine(Path.GetTempPath(), $"recomp_{Guid.NewGuid():N}");
        try
        {
            var profile = GameProfile.Parse("built", ProfileJson, RepoPaths.RepoRoot);
            Check.That(ModInstaller.Status(profile, root).State == ModState.NotInstalled && ModInstaller.ContentHash(profile, root) is null, "folder: nothing built");
            Directory.CreateDirectory(Path.Combine(root, "game", "engine"));
            File.WriteAllText(Path.Combine(root, "game", "build.json"), BuildJson.Replace("{engine}", "0"));
            Check.That(RecompInstall.Status(profile, root).State == ModState.NotInstalled, "folder: build info alone is not a build");
            File.WriteAllText(Path.Combine(root, "game", "B.exe"), "stub");
            var engine = Path.Combine(root, "game", "engine", "e.exe");
            File.WriteAllText(engine, "engine bytes");
            Check.That(ModInstaller.Status(profile, root).State == ModState.Modified, "folder: an engine that is not the one the build info names is damaged");
            File.WriteAllText(Path.Combine(root, "game", "build.json"), BuildJson.Replace("{engine}", Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(engine))).ToLowerInvariant()));
            Check.That(ModInstaller.Status(profile, root).State == ModState.Installed && ModInstaller.ContentHash(profile, root) is { Length: 32 }, "folder: a complete build, with a fingerprint hash");
            File.WriteAllText(Path.Combine(root, "game", "build.json"), """{ "fingerprint": { "game": "1" } }""");
            Check.That(RecompInstall.Status(profile, root).State == ModState.NotInstalled, "folder: a build info without all parts of the fingerprint is none");
            File.WriteAllText(Path.Combine(root, "game", "build.json"), "not json");
            Check.That(RecompBuildInfo.Read(Path.Combine(root, "game", "build.json")) is null, "folder: unreadable build info is none");
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    private static void Fingerprint()
    {
        var ours = new RecompBuildInfo("g", "r", "p", null);
        Check.That(ours.Fingerprint == "g:r:p" && ours.Hash.SequenceEqual(SHA256.HashData(Encoding.UTF8.GetBytes("g:r:p"))), "fingerprint: game, runtime and patches, hashed");
        Check.That(ours.Hash.SequenceEqual(new RecompBuildInfo("g", "r", "p", "engine").Hash), "fingerprint: the engine's own hash is not part of it");
        Check.That(new[] { new RecompBuildInfo("G", "r", "p", null), new RecompBuildInfo("g", "R", "p", null), new RecompBuildInfo("g", "r", "P", null) }.All(other => !other.Hash.SequenceEqual(ours.Hash)),
            "fingerprint: each part counts");
        var host = new BuildInfo(3, GameBuilds.Unknown, ours.Hash);
        Check.That(BuildInfo.Compare(host, new BuildInfo(3, GameBuilds.Unknown, new RecompBuildInfo("g", "r", "p", "x").Hash)) == BuildMismatch.None, "compare: the same fingerprint gets a slot");
        var different = BuildInfo.Compare(host, new BuildInfo(3, GameBuilds.Unknown, new RecompBuildInfo("g", "r2", "p", null).Hash));
        Check.That(different == BuildMismatch.ModFiles && Refusals.FromBuildMismatch(different, builtGame: true) == RefusalKind.BuildDiffers && Refusals.FromBuildMismatch(different) == RefusalKind.ModFiles,
            "compare: another fingerprint is refused as another build");
        Check.That(BuildInfo.Compare(host, new BuildInfo(3, GameBuilds.Unknown, null)) == BuildMismatch.ModNotInstalled, "compare: a guest with no build is told so");
    }

    private static void Words()
    {
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.BuildDiffers)) == "Your build differs from the host's: update Tether, then press Rebuild"
            && StatusText.Refusal(new Refusal(RefusalKind.BuildFailed, "Not enough disk space")).Contains("Not enough disk space"), "words: a different build, a failed build");
        Check.That(StatusText.RefusalOf(new Refusal(RefusalKind.BuildDiffers), "Amy") == "Amy's build differs from the host's: Amy must update", "words: another player's refusal names the player");
        Check.That(StatusFor(RefusalKind.BuildDiffers) == NoticeFix.Rebuild && StatusFor(RefusalKind.BuildFailed) == NoticeFix.Rebuild, "fix: both offer Rebuild");
        var built = new GameStatus(new ModStatus(ModState.NotInstalled, 0), true, 0, Built: true);
        Check.That(built.Line == "Not built" && built.Action is { Label: "Build", Enabled: true } && built.SettingsMod == "Not built" && built.SettingsGame == "Disc not set", "status: not built offers Build");
        var damaged = built with { Mod = new ModStatus(ModState.Modified, 0), DiscSet = true };
        Check.That(damaged.Line == "Build damaged" && damaged.Action is { Label: "Rebuild" } && damaged.SettingsGame == "Disc set", "status: a damaged build offers Rebuild");
        Check.That((built with { Mod = new ModStatus(ModState.Installed, 0) }).CanHost && (built with { Mod = new ModStatus(ModState.Installed, 0) }).SettingsMod == "Built", "status: a built game can host");
        var net = new PlayerSlot(1, "Amy", false, false, Step: SessionStep.InGame, Game: new GameState(GamePlace.World, Net: GameNet.Desync));
        Check.That(CardStates.Of(net) == CardState.Problem && CardStates.Words(net) == "Out of sync, the game stopped", "card: a desync is a problem, in words");
        var synced = net with { Game = new GameState(GamePlace.World, Net: GameNet.InSync) };
        Check.That(CardStates.Of(synced) == CardState.InGame && CardStates.Words(synced) == "In sync", "card: in sync is in game");
    }

    private static NoticeFix StatusFor(RefusalKind kind) =>
        SessionCards.FixFor(AppStatus.Idle with { Refusal = new Refusal(kind, GameId: "warriors", LobbyId: 5) }, newerBuild: 0);

    private static void RunView()
    {
        var running = new ModRunView(ModRunKind.Install, new ModProgress(ModStep.Building, 40, 3, 9, "downloading x.zip: 10 of 25 MB"), Cancellable: true);
        var dto = ModRunViews.Dto(running);
        Check.That(dto.Cancel is { Label: "Cancel", Enabled: true } && dto.Log is null && !dto.Failed && dto.Step.Text == "Building game: downloading x.zip: 10 of 25 MB" && dto.Step.Trailing == "40%",
            "view: a running build shows its step and Cancel");
        var failed = ModRunViews.Dto(new ModRunView(ModRunKind.Install, new ModProgress(ModStep.Failed, 0, 0, 0, "The game could not be built"), LogPath: "C:\\x\\game.build.log"));
        Check.That(failed.Failed && failed.Retry is { Label: "Retry" } && failed.Log is { Label: "Open log" } && failed.Cancel is null, "view: a failed build offers Retry and Open log");
        var noTools = ModRunViews.Dto(new ModRunView(ModRunKind.Install, new ModProgress(ModStep.Failed, 0, 0, 0, "The C++ build tools from Microsoft are not installed"), BuildToolsMissing: true));
        Check.That(noTools.Fix is { Label: "Install build tools", Enabled: true } && failed.Fix is null, "view: missing build tools offer the guided install, nothing else does");
        var plain = ModRunViews.Dto(new ModRunView(ModRunKind.Install, new ModProgress(ModStep.Failed, 0, 0, 0, "Close it first")));
        Check.That(plain.Log is null && plain.Retry is not null, "view: a failure without a log has no Open log");
        Check.That(ModRunViews.Dto(new ModRunView(ModRunKind.Install, new ModProgress(ModStep.Building, ProgressMath.NoPercent, 0, 0))) is { Step: { Text: "Building game", Trailing: "", Indeterminate: true } }, "view: a build with no step text yet is a bar of unknown length");
    }
}
