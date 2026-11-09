using System.Text;
using Tether;
using Tether.Gui;

namespace LauncherTests;

/// <summary>The game's status channel and the journey words built on it: the status file the game writes (a fake writer here), the stage and counts that reach the
/// cards, the session-end lines, a crash, the fingerprint parts a differing build names, and the build step published to the other players.</summary>
public static class StatusChannelTests
{
    private const string Host = "Alex";

    public static void Run()
    {
        Pairs();
        Words();
        StatusFile();
        Relay();
        Ends();
        EndedWords();
        Build();
        Journey();
    }

    private static string Encode(GameState state) => string.Join(';', state.EncodePairs());

    private static void Pairs()
    {
        var state = new GameState(Stage: GameStage.Loading, Done: 6400, Total: 10000, Unit: GameUnit.Items, Mission: "1", Text: "Sam", Error: RefusalKind.LoadFailed, Reason: "bad; file=x");
        var text = Encode(state);
        Check.That(text == "stage=loading;done=6400;total=10000;unit=items;mission=1;text=Sam;error=LoadFailed;reason=bad, file:x", "pairs: the stage with its counts, mission, text and error, free text without separators");
        Check.That(GameState.Parse(Encoding.ASCII.GetBytes(text)) == state with { Reason = "bad, file:x" } && state.Percent == 64, "pairs: they read back, the percent comes from the counts");
        Check.That(new GameState(Stage: GameStage.Lobby).Percent is null && GameState.Parse("stage=nonsense;done=-4;total=x;error=1"u8.ToArray()).IsEmpty, "pairs: no total, no percent; unknown or malformed pairs are skipped");
    }

    private static void Words()
    {
        string? W(GameState state) => StageWords.Of(state);
        Check.That(W(new GameState()) is null, "words: no stage, no words");
        Check.That(W(new GameState(Stage: GameStage.Booting)) == "Starting the game"
            && W(new GameState(Stage: GameStage.Lobby, Done: 2, Total: 4, Unit: GameUnit.Players)) == "In the co-op lobby: 2 of 4 players"
            && W(new GameState(Stage: GameStage.Connecting)) == "Connecting to the host", "words: starting, the lobby with its players, connecting");
        Check.That(W(new GameState(Stage: GameStage.Receiving, Done: 12_897_484, Total: 18_664_652, Unit: GameUnit.Bytes)) == "Receiving the host's game: 12.3 of 17.8 MB, 69%"
            && W(new GameState(Stage: GameStage.Sending, Done: 40, Total: 100, Unit: GameUnit.Items, Text: "Sam")) == "Sending the game to Sam: 40%"
            && W(new GameState(Stage: GameStage.Sending)) == "Sending the game to a player", "words: the snapshot with its megabytes, or to whom");
        Check.That(W(new GameState(Stage: GameStage.Loading, Done: 6400, Total: 10000, Unit: GameUnit.Items, Mission: "1")) == "Loading mission 1: 64%"
            && W(new GameState(Stage: GameStage.Loading)) == "Loading" && W(new GameState(Stage: GameStage.Playing, Mission: "1")) == "In mission 1"
            && W(new GameState(Stage: GameStage.Playing)) == "In the game" && W(new GameState(Stage: GameStage.Closed)) == "Game closed", "words: loading a mission with its count, playing, closed");
        var sam = new PlayerSlot(2, "Sam", false, false, Step: SessionStep.InGame, Game: new GameState(GamePlace.Loading, Stage: GameStage.Loading, Done: 64, Total: 100, Unit: GameUnit.Items, Mission: "1"));
        Check.That(CardStates.Words(sam) == "Loading mission 1: 64%" && CardStates.Of(sam) == CardState.InGame, "card: Sam's stage reaches his card");
        var stalled = sam with { Game = sam.Game! with { Net = GameNet.Stalled } };
        Check.That(CardStates.Words(stalled) == "Waiting for a player's input" && CardStates.Words(sam with { Game = sam.Game! with { Net = GameNet.InSync } }) == "Loading mission 1: 64%",
            "card: a stalled link comes before the stage, a link in sync does not");
        var failed = sam with { Game = new GameState(Error: RefusalKind.LoadFailed, Reason: "mission 2 could not be read from the disc image") };
        Check.That(CardStates.Words(failed) == "mission 2 could not be read from the disc image" && CardStates.Of(failed) == CardState.Problem, "card: an error the game names is a problem, in the game's words");
    }

    private static void StatusFile()
    {
        var folder = Path.Combine(Path.GetTempPath(), $"status_{Guid.NewGuid():N}");
        Directory.CreateDirectory(folder);
        var path = Path.Combine(folder, "status.txt");
        try
        {
            var file = new EngineStatusFile(path);
            Check.That(file.ReadChanged() is null, "file: not written yet");
            Write(path, "stage=booting");
            Check.That(file.ReadChanged() is { Stage: GameStage.Booting } && file.ReadChanged() is null, "file: a change is returned once");
            Write(path, "stage=loading;mission=1;done=10;total=40;unit=items");
            Check.That(file.ReadChanged() is { Stage: GameStage.Loading, Mission: "1", Done: 10, Total: 40, Unit: GameUnit.Items }, "file: the next state");
            File.WriteAllText(path, string.Empty);
            Check.That(file.ReadChanged() is null, "file: an empty file says nothing");
            file.Clear();
            Check.That(!File.Exists(path) && file.ReadChanged() is null, "file: cleared before a run");
            Write(path, "stage=loading;mission=1;done=10;total=40;unit=items");
            Check.That(file.ReadChanged() is { Stage: GameStage.Loading }, "file: the same text after a clear is new again");
            var merged = EngineStatusFile.Apply(new GameState(GamePlace.Title, Net: GameNet.Hosting), GameState.Parse("stage=playing;mission=2"u8.ToArray()));
            Check.That(merged is { Place: GamePlace.World, Stage: GameStage.Playing, Mission: "2", Net: GameNet.Hosting }, "file: the stage sets the place and keeps the netplay state");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    /// <summary>The way a game writes it: the whole file by a temporary beside it, moved over.</summary>
    private static void Write(string path, string text)
    {
        File.WriteAllText(path + ".tmp", text);
        File.Move(path + ".tmp", path, overwrite: true);
    }

    private static void Relay()
    {
        var folder = Path.Combine(Path.GetTempPath(), $"relay_{Guid.NewGuid():N}");
        Directory.CreateDirectory(folder);
        var statusPath = Path.Combine(folder, "status.txt");
        var logPath = Path.Combine(folder, "warriors.log");
        File.WriteAllText(logPath, string.Empty);
        try
        {
            var reported = new List<GameState>();
            using var link = new UdpRelayLink(RelayRole.Host, RecompLaunch.FreeUdpPort(), () => true, new EngineLogTail(logPath), new EngineStatusFile(statusPath));
            link.GameStateReceived += payload => reported.Add(GameState.Parse(payload));
            void Poll()
            {
                link.Pump();
                Thread.Sleep(520);
                link.Pump();
            }
            Poll();
            Write(statusPath, "stage=receiving;done=1048576;total=4194304;unit=bytes");
            Poll();
            Check.That(reported.Last() is { Place: GamePlace.Loading, Stage: GameStage.Receiving, Done: 1048576, Total: 4194304 } && link.State.Percent == 25, "relay: the fake game's status reaches GAME_STATE with its counts");
            File.AppendAllText(logPath, "[net] lockstep starts at frame 601: you are pad 2 of 2\n");
            Write(statusPath, "stage=playing;mission=1");
            Poll();
            Check.That(reported.Last() is { Place: GamePlace.World, Net: GameNet.InSync, Stage: GameStage.Playing, Mission: "1" }, "relay: the engine's log and its status file make one state");
            Check.That(link.Ended is null, "relay: nothing ended yet");
            File.AppendAllText(logPath, "[net] session ended (HostLeft): the host left the game; this game goes on alone\n");
            Poll();
            Check.That(link.Ended is { Kind: RefusalKind.HostLeft, Reason: "the host left the game; this game goes on alone" }, "relay: the engine's session end is kept with its reason");
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    private static void Ends()
    {
        Check.That(NetLog.EndOf("[net] session ended (GameDesync): the games went out of step; this game goes on alone") is { Kind: RefusalKind.GameDesync, Reason: "the games went out of step; this game goes on alone" }
            && NetLog.EndOf("[net] session ended (BuildDiffers): the host refused this build (different build, it differs from the host's in: patches)") is { Kind: RefusalKind.BuildDiffers },
            "end: the engine's line gives the refusal's name and its plain reason");
        Check.That(NetLog.EndOf("[net] session ended (Nonsense): x") is null && NetLog.EndOf("[net] session ended (7): x") is null && NetLog.EndOf("[net] pad 2 left the game at its frame 5") is null
            && NetLog.EndOf("session ended (HostLeft): no prefix") is null, "end: another code, a number or another line is no end");
        Check.That(NetLog.Gone(new GameState(Stage: GameStage.Loading, Mission: "1", Done: 64, Total: 100, Unit: GameUnit.Items)) == (RefusalKind.GameCrashed, "while loading mission 1: 64%")
            && NetLog.Gone(new GameState(Stage: GameStage.Booting)) == (RefusalKind.GameCrashed, "while starting the game"), "gone: an engine that stopped mid-stage crashed, and the words say where");
        Check.That(NetLog.Gone(new GameState(Stage: GameStage.Closed)) is null && NetLog.Gone(new GameState()) is null && NetLog.Gone(new GameState(Net: GameNet.InSync)) is null
            && NetLog.Gone(new GameState(Net: GameNet.Desync)) == (RefusalKind.GameDesync, ""), "gone: closed on purpose is normal; an older engine's last net state decides");
    }

    private static void EndedWords()
    {
        Refusal Ended(RefusalKind kind, string detail = "") => new(kind, detail, "warriors", 5, Ended: true);
        Check.That(StatusText.Refusal(Ended(RefusalKind.GameDesync, "the games went out of step; this game goes on alone")) == "Session ended (GameDesync): the games went out of step; this game goes on alone",
            "ended: the code and the engine's own reason");
        Check.That(StatusText.Refusal(Ended(RefusalKind.HostLeft)) == "Session ended (HostLeft): the host left the game"
            && StatusText.Refusal(Ended(RefusalKind.Removed)) == "Session ended (Removed): the host removed you from the lobby"
            && StatusText.Refusal(Ended(RefusalKind.RejoinGaveUp)).StartsWith("Session ended (RejoinGaveUp): could not get back into the lobby")
            && StatusText.Refusal(Ended(RefusalKind.PeerGone)) == "Session ended (PeerGone): a player stopped answering"
            && StatusText.Refusal(Ended(RefusalKind.GameDesync)).StartsWith("Session ended (GameDesync): the players' games no longer match"), "ended: every launcher-side end has its plain reason");
        Check.That(StatusText.Refusal(Ended(RefusalKind.GameCrashed, "while loading mission 1: 64%")) == "Session ended (GameCrashed): while loading mission 1: 64%"
            && StatusText.Refusal(new Refusal(RefusalKind.GameCrashed, "while loading mission 1")) == "The game stopped unexpectedly while loading mission 1: start it again from Tether",
            "ended: a crash says where; the same refusal that did not end a session reads on its own");
        Check.That(StatusText.RefusalOf(Ended(RefusalKind.PeerGone), "Sam") == "Session ended for Sam (PeerGone): a player stopped answering"
            && StatusText.RefusalOf(Ended(RefusalKind.GameCrashed, "while in mission 1"), "Sam") == "Session ended for Sam (GameCrashed): while in mission 1", "ended: another player's card names the player");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.HostLeft)) == "Host left", "ended: a refusal that is not an end keeps its short words");
        Check.That(StatusText.Refusal(new Refusal(RefusalKind.BuildDiffers, "engine 18e437 vs 8b7c74")) == "Your build differs from the host's (engine 18e437 vs 8b7c74): update Tether, then press Rebuild"
            && StatusText.RefusalOf(new Refusal(RefusalKind.BuildDiffers, "engine a1b2c3 vs c3d4e5"), "Kim") == "Kim's build differs from the host's (engine a1b2c3 vs c3d4e5): Kim must update",
            "build: who differs, in which part, and who must update");
        Check.That(StatusText.RefusalOf(new Refusal(RefusalKind.BuildFailed, "Not enough free disk space"), "Kim") == "Kim's game could not be built (Not enough free disk space): Kim must fix it, then rebuild"
            && StatusText.RefusalOf(new Refusal(RefusalKind.ModNotInstalled, GameId: "warriors"), "Kim") == "Kim has not built the game: Kim must pick the disc and press Build"
            && StatusText.RefusalOf(new Refusal(RefusalKind.GameCrashed, "while loading mission 1"), "Kim") == "Kim's game stopped unexpectedly while loading mission 1", "build: another player's failures name the player and what to do");
        Check.That(StatusText.RefusalOf(new Refusal(RefusalKind.HostUnreachable, Host), "Kim") == "Kim can't reach Alex through Steam: Kim must check the connection and join again"
            && StatusText.RefusalOf(new Refusal(RefusalKind.SaveTimedOut), "Kim") == "Kim's save transfer timed out: Kim must join again"
            && StatusText.RefusalOf(new Refusal(RefusalKind.GameRunning, "The Warriors"), "Kim") == "Kim must close The Warriors, then start again"
            && StatusText.RefusalOf(new Refusal(RefusalKind.LaunchFailed), "Kim") == "Kim's game did not start: Kim must start it again from Tether", "build: the start failures name the player too");
        var crashed = new PlayerSlot(2, "Sam", false, false, Step: SessionStep.Refused, Refusal: RefusalKind.GameCrashed, Detail: "while loading mission 1: 64%", Ended: true);
        Check.That(CardStates.Words(crashed) == "Session ended for Sam (GameCrashed): while loading mission 1: 64%" && CardStates.Of(crashed) == CardState.Problem
            && CardStates.Words(crashed with { IsLocal = true, Detail = "a player stopped answering", Refusal = RefusalKind.PeerGone }) == "Session ended (PeerGone): a player stopped answering",
            "ended: the card of the player who crashed names him and where, his own card names the end");
        Check.That(StepCodec.TryDecode(StepCodec.Encode(new StepReport(SessionStep.Refused, 0, RefusalKind.PeerGone, "a player stopped answering", Ended: true)), out var report)
            && report is { Step: SessionStep.Refused, Refusal: RefusalKind.PeerGone, Detail: "a player stopped answering", Ended: true }
            && StepCodec.TryDecode(StepCodec.Encode(new StepReport(SessionStep.Refused, 0, RefusalKind.PeerGone)), out var plain) && !plain.Ended, "ended: the end travels in the player's published step");
    }

    private static void Build()
    {
        var host = new RecompBuildInfo("df5a7c8b54b3c8e0", "18e437d22edc688e", "ad8136528971f6cb", null);
        var kim = new RecompBuildInfo("df5a7c8b54b3c8e0", "8b7c74c3aaaaaaaa", "ad8136528971f6cb", null);
        Check.That(RecompBuildInfo.Differences(host.Fingerprint, kim.Fingerprint) == "engine 18e437 vs 8b7c74" && RecompBuildInfo.Differences(host.Fingerprint, host.Fingerprint).Length == 0
            && RecompBuildInfo.Differences(host.Fingerprint, new RecompBuildInfo("1234567890", "2", "3", null).Fingerprint) == "game code df5a7c vs 123456, engine 18e437 vs 2, patches ad8136 vs 3"
            && RecompBuildInfo.Differences("a", "b").Length == 0, "fingerprint: the parts that differ, with both short hashes, host first");
        var info = new BuildInfo(3, GameBuilds.Unknown, host.Hash, host.Fingerprint);
        Check.That(BuildInfo.TryDecode(info.Encode(), out var back) && back.Fingerprint == host.Fingerprint && back.ModHash!.SequenceEqual(host.Hash) && back.LauncherBuild == 3,
            "build info: the fingerprint text travels after the hash");
        Check.That(BuildInfo.TryDecode(new BuildInfo(3, 5, host.Hash).Encode(), out var plain) && plain.Fingerprint is null && BuildInfo.TryDecode(info.Encode()[..(8 + BuildInfo.HashBytes)], out var older) && older.Fingerprint is null,
            "build info: no fingerprint from a game that has none or an older launcher");
        Check.That(BuildInfo.Detail(BuildMismatch.ModFiles, info, new BuildInfo(3, GameBuilds.Unknown, kim.Hash, kim.Fingerprint), 0, 0) == "engine 18e437 vs 8b7c74"
            && BuildInfo.Detail(BuildMismatch.ModFiles, new BuildInfo(3, 0, host.Hash), new BuildInfo(3, 0, kim.Hash), 7, 5) == "7/5", "build info: the detail names the parts, or the mod builds for a mod");
    }

    private static void Journey()
    {
        var building = new PlayerSlot(2, "Kim", false, false, Step: SessionStep.BuildingGame, Percent: 82, Detail: "compiling the game code: 142 of 270 files (52%), overall 82%");
        Check.That(CardStates.Words(building) == "Building game: compiling the game code: 142 of 270 files (52%), overall 82%" && CardStates.Of(building) == CardState.Progress, "journey: Kim's card says what the build is doing, with its count");
        var recompiling = building with { Percent = ProgressMath.NoPercent, Detail = "converting the game code and graphics programs, step 6 of 9, overall 55%" };
        Check.That(CardStates.Words(recompiling) == "Building game: converting the game code and graphics programs, step 6 of 9, overall 55%", "journey: a step that counts nothing shows no percent");
        var text = StepCodec.Encode(new StepReport(SessionStep.BuildingGame, 82, Detail: "compiling the game code: 142 of 270 files (52%), overall 82%"));
        Check.That(text == "step=BuildingGame;pct=82;detail=compiling the game code: 142 of 270 files (52%), overall 82%" && StepCodec.TryDecode(text, out var report) && report is { Percent: 82, Detail: "compiling the game code: 142 of 270 files (52%), overall 82%" }, "journey: the build step travels with its count");
        Check.That(StepCodec.Encode(new StepReport(SessionStep.BuildingGame, ProgressMath.NoPercent, Detail: "x")) == "step=BuildingGame;detail=x"
            && StepCodec.TryDecode("step=BuildingGame;detail=x", out var none) && none.Percent == ProgressMath.NoPercent, "journey: no count, no percent published");
        Check.That(SessionProgress.Step(new StepInputs(true, true, false, false, false, true, true, true, false, BuildingGame: true)) == SessionStep.BuildingGame
            && SessionProgress.Step(new StepInputs(true, true, false, false, false, true, true, true, false)) == SessionStep.StartingGame, "journey: the build is its own step between the save and the start");
        var local = new PlayerSlot(1, "Sam", false, true, Step: SessionStep.BuildingGame, Percent: 82, Detail: "compiling the game code: 142 of 270 files (52%), overall 82%");
        var hostSlot = new PlayerSlot(2, Host, true, false, Step: SessionStep.Ready, RttMs: 20);
        var status = new AppStatus(AppState.Joined, 5) { GameId = "warriors", Step = SessionStep.BuildingGame, Seats = 4, Bridged = true, Players = [hostSlot, local] };
        var warriors = GameProfile.Load("warriors");
        var built = new GameStatus(new ModStatus(ModState.Installed, 0), true, 0, Built: true);
        var joining = SessionScreens.Joining(status, warriors, built, null, GameCondition.Fine, 0, 100_000);
        var row = joining.Steps.Single(step => step.Mark == StepMark.Active);
        Check.That(row.Text == "Building game: compiling the game code: 142 of 270 files (52%), overall 82%" && row.Trailing == "82%" && row.Bar == 0.82, "journey: the joining screen shows the guest's own build, with the bar of its count");
        var hosting = SessionScreens.Session(status with { Players = [local with { IsHost = true }] }, warriors, null, built, GameCondition.Fine, [], 100_000);
        Check.That(hosting.Steps.Any(step => step.Text == "Building game: compiling the game code: 142 of 270 files (52%), overall 82%" && step.Mark == StepMark.Active), "journey: the host's steps show its build too");
        var kim = new PlayerSlot(3, "Kim", false, false, Step: SessionStep.Refused, Refusal: RefusalKind.BuildDiffers, Detail: "engine a1b2c3 vs c3d4e5");
        var waiting = SessionScreens.Session(status with { Step = SessionStep.Ready, Players = [hostSlot with { IsLocal = true, Step = SessionStep.Ready }, kim] }, warriors, null, built, GameCondition.Fine, [], 100_000);
        Check.That(!waiting.StartEnabled && waiting.StartHint == "Waiting for Kim to update", "journey: START GAME says why it waits, naming the player and what they do");
        var loadingState = new GameState(GamePlace.Loading, Stage: GameStage.Loading, Done: 64, Total: 100, Unit: GameUnit.Items, Mission: "1");
        var lobby = SessionScreens.Session(status with { Step = SessionStep.InGame, State = AppState.GameRunning, Players = [hostSlot with { IsLocal = true, Step = SessionStep.InGame, Game = loadingState }, local with { Step = SessionStep.InGame, IsLocal = false, Game = loadingState }] },
            warriors, null, built, GameCondition.Fine, [], 100_000);
        Check.That(lobby.State?.Words == "Loading mission 1", "journey: the lobby says a mission is loading until everyone is in");
    }
}
