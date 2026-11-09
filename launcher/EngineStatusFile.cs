using System.Text;

namespace Tether;

/// <summary>
/// The status file of a game with its own front end (The Warriors, <c>game/status.txt</c>): one line of GAME_STATE pairs
/// (<c>stage=loading;mission=1;done=6400;total=10000;unit=bytes</c>, docs/CONTRACT.md) that the game rewrites whole, by a temporary file
/// moved over it, when something changes. <see cref="ReadChanged"/> returns the state when the file differs from the one read last; a file the
/// game is replacing, missing or empty returns none.
/// </summary>
public sealed class EngineStatusFile(string path)
{
    private string _last = string.Empty;

    /// <summary>Removes what an earlier run left, so the first state read is this run's.</summary>
    public void Clear()
    {
        _last = string.Empty;
        try
        {
            File.Delete(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not remove the status file: {e.Message}");
        }
    }

    public GameState? ReadChanged()
    {
        string text;
        try
        {
            if (!File.Exists(path)) return null;
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            text = new StreamReader(stream, Encoding.UTF8).ReadToEnd().Trim();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return null;
        }
        if (text.Length == 0 || text == _last) return null;
        _last = text;
        return GameState.FromPairs(GameState.Pairs(text));
    }

    /// <summary>The state with what the status file says: the stage with its counts, the error, and the place that follows from the stage (the
    /// front end is the title, a connection or a load is loading, a mission is the world).</summary>
    public static GameState Apply(GameState state, GameState status) => state with
    {
        Stage = status.Stage, Done = status.Done, Total = status.Total, Unit = status.Unit, Mission = status.Mission, Text = status.Text,
        Error = status.Error, Reason = status.Reason,
        Place = status.Stage switch
        {
            GameStage.Booting or GameStage.Lobby => GamePlace.Title,
            GameStage.Connecting or GameStage.Receiving or GameStage.Sending or GameStage.Loading => GamePlace.Loading,
            GameStage.Playing => GamePlace.World,
            _ => state.Place,
        },
    };
}
