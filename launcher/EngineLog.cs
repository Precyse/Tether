using System.Text;

namespace Tether;

/// <summary>Why the engine's session ended, from its <c>[net] session ended (&lt;code&gt;): &lt;reason&gt;</c> line: the code is a <see cref="RefusalKind"/> name,
/// the reason the engine's plain sentence.</summary>
public sealed record SessionEnd(RefusalKind Kind, string Reason);

/// <summary>
/// Turns the <c>[net]</c> lines of The Warriors' engine into what the cards say. The engine reports no GAME_STATE; everything a player
/// needs is on its stderr (the stub writes it to <c>logs/warriors.log</c>): hosting, connecting, the snapshot going out or in, lockstep
/// running, a stall naming the pad, a desync, a pad that left. A line it does not know changes nothing.
/// </summary>
public static class NetLog
{
    private const string Prefix = "[net] ";
    private const string EndedMark = "session ended (";
    private const string EndedClose = "):";

    private static readonly (string Contains, GameNet Net)[] Phases =
    [
        ("lockstep starts at frame", GameNet.InSync),
        ("lockstep joined at frame", GameNet.InSync),
        ("joins the running game", GameNet.InSync),
        ("in sync at frame", GameNet.InSync),
        ("peer is back after", GameNet.InSync),
        ("peer stalled:", GameNet.Stalled),
        ("peer still stalled", GameNet.Stalled),
        ("DESYNC at frame", GameNet.Desync),
        ("reports a DESYNC", GameNet.Desync),
        ("left the game at its frame", GameNet.PeerLeft),
        ("stopped with an error at its frame", GameNet.PeerLeft),
        ("taking it to be gone", GameNet.PeerLeft),
        ("host refused the connection", GameNet.Refused),
        ("no answer from host", GameNet.Failed),
        ("the host never started the game", GameNet.Failed),
        ("snapshot download stopped", GameNet.Failed),
        ("snapshot upload stopped", GameNet.Failed),
        ("stopping:", GameNet.Failed),
        ("every player is here", GameNet.Sending),
        ("asks to join the running game", GameNet.Sending),
        ("sending it to", GameNet.Sending),
        ("for the player joining on pad", GameNet.Sending),
        ("connecting to host", GameNet.Connecting),
        ("host answered:", GameNet.Receiving),
        ("snapshot download", GameNet.Receiving),
        ("snapshot received", GameNet.Receiving),
        ("snapshot unpacked", GameNet.Receiving),
        ("snapshot loaded at frame", GameNet.Receiving),
        (" players on ", GameNet.Hosting),
    ];

    /// <summary>Why an engine that ended on its own ended, from the last thing its log said (it exits with code 3 on any netplay failure): the other
    /// machines' game no longer matches, the host's build is not this one, or the other players stopped answering. Null when the log names no failure (the
    /// engine ended in sync or after a player left: a normal end, the window closed, a crash).</summary>
    public static RefusalKind? EndRefusal(GameNet last) => last switch
    {
        GameNet.Desync => RefusalKind.GameDesync,
        GameNet.Refused => RefusalKind.BuildDiffers,
        GameNet.Failed or GameNet.Stalled => RefusalKind.PeerGone,
        _ => null,
    };

    /// <summary>Why an engine that is gone without having said its session ended was not a normal end: it stopped before it said it closed (a crash, said with where it
    /// was), or an older engine's last netplay state names a failure (<see cref="EndRefusal"/>). Null for a normal end.</summary>
    public static (RefusalKind Kind, string Detail)? Gone(GameState state)
    {
        if (state.Stage is not (GameStage.Unknown or GameStage.Closed)) return (RefusalKind.GameCrashed, $"while {StageWords.Of(state)!.ToLowerInvariant()}");
        return EndRefusal(state.Net) is { } kind ? (kind, string.Empty) : null;
    }

    /// <summary>The end the engine reports in a line (<c>[net] session ended (HostLeft): the host left the game</c>); null for any other line or a code that is not a
    /// <see cref="RefusalKind"/>.</summary>
    public static SessionEnd? EndOf(string line)
    {
        if (!line.StartsWith(Prefix, StringComparison.Ordinal)) return null;
        var open = line.IndexOf(EndedMark, Prefix.Length, StringComparison.Ordinal);
        if (open != Prefix.Length) return null;
        var close = line.IndexOf(EndedClose, open, StringComparison.Ordinal);
        if (close < 0) return null;
        var code = line[(open + EndedMark.Length)..close];
        return !char.IsDigit(code.FirstOrDefault()) && Enum.TryParse<RefusalKind>(code, out var kind) && Enum.IsDefined(kind)
            ? new SessionEnd(kind, line[(close + EndedClose.Length)..].Trim())
            : null;
    }

    /// <summary>The state after one engine line; a line that is not a <c>[net]</c> line of a known kind returns the state as it was.</summary>
    public static GameState Apply(GameState state, string line)
    {
        if (!line.StartsWith(Prefix, StringComparison.Ordinal)) return state;
        foreach (var (contains, net) in Phases)
            if (line.Contains(contains, StringComparison.Ordinal)) return With(state, net);
        return state;
    }

    /// <summary>The state with the phase set: a host waiting and a joiner connecting or loading are before the world, a running lockstep is in it.</summary>
    private static GameState With(GameState state, GameNet net) => state with
    {
        Net = net,
        Place = net switch
        {
            GameNet.Hosting => GamePlace.Title,
            GameNet.Connecting or GameNet.Sending or GameNet.Receiving => GamePlace.Loading,
            GameNet.InSync or GameNet.Stalled or GameNet.Desync or GameNet.PeerLeft => GamePlace.World,
            _ => state.Place,
        },
    };
}

/// <summary>
/// Follows a log file that another process appends to: <see cref="ReadNewLines"/> returns the whole lines written since the last call. What the
/// file held when the tail began is not read. A file that shrinks or is replaced (the stub moves the old log aside at every start) is read from
/// its start.
/// </summary>
public sealed class EngineLogTail
{
    private readonly string _path;
    private readonly StringBuilder _partial = new();
    private long _offset;
    private DateTime _created;

    public EngineLogTail(string path)
    {
        _path = path;
        if (!File.Exists(path)) return;
        var info = new FileInfo(path);
        _offset = info.Length;
        _created = info.CreationTimeUtc;
    }

    public IReadOnlyList<string> ReadNewLines()
    {
        var lines = new List<string>();
        try
        {
            if (!File.Exists(_path)) return lines;
            var info = new FileInfo(_path);
            if (info.Length < _offset || info.CreationTimeUtc != _created)
            {
                _offset = 0;
                _partial.Clear();
            }
            _created = info.CreationTimeUtc;
            if (info.Length == _offset) return lines;
            using var stream = new FileStream(_path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            stream.Seek(_offset, SeekOrigin.Begin);
            var bytes = new byte[stream.Length - _offset];
            var read = stream.Read(bytes, 0, bytes.Length);
            _offset += read;
            _partial.Append(Encoding.UTF8.GetString(bytes, 0, read));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not read the engine log: {e.Message}");
            return lines;
        }
        var text = _partial.ToString();
        var lastBreak = text.LastIndexOf('\n');
        if (lastBreak < 0) return lines;
        lines.AddRange(text[..lastBreak].Split('\n').Select(line => line.TrimEnd('\r')));
        _partial.Clear().Append(text[(lastBreak + 1)..]);
        return lines;
    }
}
