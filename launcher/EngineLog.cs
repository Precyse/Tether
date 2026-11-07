using System.Text;

namespace CoopLauncher;

/// <summary>
/// Turns the <c>[net]</c> lines of The Warriors' engine into what the cards say. The engine reports no GAME_STATE; everything a player
/// needs is on its stderr (the stub writes it to <c>logs/warriors.log</c>): hosting, connecting, the snapshot going out or in, lockstep
/// running, a stall naming the pad, a desync, a pad that left. A line it does not know changes nothing.
/// </summary>
public static class NetLog
{
    private const string Prefix = "[net] ";

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
