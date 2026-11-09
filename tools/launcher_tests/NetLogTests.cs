using Tether;
using Tether.Gui;

namespace LauncherTests;

/// <summary>The engine's [net] lines become the state the cards show, and a log that another process appends to is followed line by line.</summary>
public static class NetLogTests
{
    public static void Run()
    {
        HostLines();
        JoinerLines();
        State();
        Tail();
    }

    private static GameState After(params string[] lines) => lines.Aggregate(new GameState(), NetLog.Apply);

    private static void HostLines()
    {
        Check.That(After("[net] hosting 2 players on 127.0.0.1:27055, build a/b/c; the game runs on until all 1 have joined") is { Net: GameNet.Hosting, Place: GamePlace.Title },
            "host: hosting waits at the title");
        Check.That(After("[net] hosting 3 players on x", "[net] player 2 of 3 connected from 127.0.0.1:5000").Net == GameNet.Hosting, "host: a player arriving changes nothing yet");
        Check.That(After("[net] every player is here: taking a snapshot of the game at the next frame") is { Net: GameNet.Sending, Place: GamePlace.Loading },
            "host: sending the game to the players is loading");
        Check.That(After("[net] snapshot of frame 600: 40.1 MB packed to 6.6 MB in 60 ms; sending it to 1 player(s), the game waits on this machine until they have it").Net == GameNet.Sending,
            "host: the snapshot line says sending");
        Check.That(After("[net] lockstep starts at frame 601: you are pad 1 of 2") is { Net: GameNet.InSync, Place: GamePlace.World }, "host: lockstep running is in the world");
        Check.That(After("[net] lockstep starts at frame 601: x", "[net] peer stalled: frame 700 is waiting for the input of pad 2 (we have sent up to frame 703)").Net == GameNet.Stalled,
            "host: a stall names the wait");
        Check.That(After("[net] peer stalled: f", "[net] peer is back after 1500 ms, frame 700 continues").Net == GameNet.InSync, "host: the peer coming back is in sync again");
        Check.That(After("[net] lockstep starts at frame 1: x", "[net] DESYNC at frame 3000 against pad 2: the two machines no longer hold the same game").Net == GameNet.Desync, "host: a desync");
        Check.That(After("[net] pad 2 reports a DESYNC at its frame 3000 and stopped").Net == GameNet.Desync, "host: the peer reporting a desync");
        Check.That(After("[net] lockstep starts at frame 1: x", "[net] pad 2 left the game at its frame 99").Net == GameNet.PeerLeft
            && After("[net] pad 2 left the game at its frame 99", "[net] in sync at frame 900 (ee=1, 10 digests compared, none differ)").Net == GameNet.InSync,
            "host: a pad leaving, and the next digest says in sync again");
        Check.That(After("[net] no input from pad 2 for 30 s: taking it to be gone").Net == GameNet.PeerLeft, "host: a pad that went silent is gone");
    }

    private static void JoinerLines()
    {
        Check.That(After("[net] joining 127.0.0.1:51246, build a/b/c", "[net] connecting to host 127.0.0.1:51246 (attempt 1)") is { Net: GameNet.Connecting, Place: GamePlace.Loading },
            "joiner: connecting is loading");
        Check.That(After("[net] host answered: you are player 2 of 2, snapshot of frame 600").Net == GameNet.Receiving
            && After("[net] snapshot download 40% (2324 of 5808 chunks)").Net == GameNet.Receiving
            && After("[net] snapshot loaded at frame 600: memory hashes match the host's (ee 1)").Net == GameNet.Receiving, "joiner: the host's game arriving and loading");
        Check.That(After("[net] snapshot loaded at frame 600: x", "[net] lockstep starts at frame 601: you are pad 2 of 2") is { Net: GameNet.InSync, Place: GamePlace.World },
            "joiner: lockstep started");
        Check.That(After("[net] host refused the connection: different build").Net == GameNet.Refused, "joiner: refused for another build");
        Check.That(After("[net] no answer from host 127.0.0.1:1 after 120 s, giving up").Net == GameNet.Failed
            && After("[net] snapshot download stopped: nothing from the host for 20 s").Net == GameNet.Failed, "joiner: no answer, or a transfer that stopped");
        Check.That(After("[gpu] NVIDIA GeForce", "[dma] gif tag id 1", "[net] summary: 113 digests cost 5.6 ms each").IsEmpty, "other lines say nothing");
        Check.That(After("lockstep starts at frame 1 without the prefix").IsEmpty, "a line without the [net] prefix says nothing");
    }

    private static void State()
    {
        var state = After("[net] lockstep starts at frame 601: x", "[net] peer stalled: x");
        var text = string.Join(';', state.EncodePairs());
        Check.That(text == "place=world;net=stalled" && GameState.Parse(System.Text.Encoding.ASCII.GetBytes(text)) == state, "state: the net phase travels with the other pairs and reads back");
        Check.That(Enum.GetValues<GameNet>().Where(net => net != GameNet.Unknown).All(net => CardStates.NetWords(net) is { Length: > 0 }) && CardStates.NetWords(GameNet.Unknown) is null,
            "words: every phase has words, none for unknown");
    }

    private static void Tail()
    {
        var path = Path.Combine(Path.GetTempPath(), $"netlog_{Guid.NewGuid():N}.log");
        try
        {
            File.WriteAllText(path, "[net] old line\n");
            var tail = new EngineLogTail(path);
            Check.That(tail.ReadNewLines().Count == 0, "tail: what the file held before is not read");
            File.AppendAllText(path, "[net] a\r\n[net] b");
            Check.That(tail.ReadNewLines().SequenceEqual(["[net] a"]), "tail: a whole line is returned, a partial one waits");
            File.AppendAllText(path, " more\n[net] c\n");
            Check.That(tail.ReadNewLines().SequenceEqual(["[net] b more", "[net] c"]), "tail: the partial line is completed");
            File.WriteAllText(path, "[net] new run\n");
            Check.That(tail.ReadNewLines().SequenceEqual(["[net] new run"]), "tail: a file that was replaced is read from its start");
            Check.That(tail.ReadNewLines().Count == 0, "tail: nothing new, nothing returned");
        }
        finally
        {
            File.Delete(path);
        }
        Check.That(new EngineLogTail(Path.Combine(Path.GetTempPath(), "no_such_engine.log")).ReadNewLines().Count == 0, "tail: a log that is not there yet is empty");
    }
}
