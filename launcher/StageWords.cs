using System.Globalization;

namespace Tether;

/// <summary>What a game with its own front end says it is doing (<see cref="GameState.Stage"/>), in words, with the real counts it reported.</summary>
public static class StageWords
{
    private const double BytesPerMb = 1048576.0;

    /// <summary>The words of the stage (<c>Loading mission 1: 6/10 (60%)</c>); null when the game says nothing.</summary>
    public static string? Of(GameState? game)
    {
        if (game is not { Stage: not GameStage.Unknown }) return null;
        var counts = Counts(game);
        string With(string words) => counts.Length > 0 ? $"{words}: {counts}" : words;
        return game.Stage switch
        {
            GameStage.Booting => With("Starting the game"),
            GameStage.Lobby => With("In the co-op lobby"),
            GameStage.Connecting => With("Connecting to the host"),
            GameStage.Receiving => With("Receiving the host's game"),
            GameStage.Sending => With(game.Text.Length > 0 ? $"Sending the game to {game.Text}" : "Sending the game to a player"),
            GameStage.Loading => With(game.Mission.Length > 0 ? $"Loading mission {game.Mission}" : "Loading"),
            GameStage.Playing => game.Mission.Length > 0 ? $"In mission {game.Mission}" : "In the game",
            _ => "Game closed",
        };
    }

    /// <summary>The counts of a stage as the game reported them: bytes in megabytes with the percent, items as the percent, players as "2 of 4 players". Empty when the game gave no total.</summary>
    public static string Counts(GameState game)
    {
        if (game.Total <= 0) return string.Empty;
        return game.Unit switch
        {
            GameUnit.Bytes => string.Create(CultureInfo.InvariantCulture, $"{game.Done / BytesPerMb:0.#} of {game.Total / BytesPerMb:0.#} MB, {game.Percent}%"),
            GameUnit.Players => string.Create(CultureInfo.InvariantCulture, $"{game.Done} of {game.Total} players"),
            _ => $"{game.Percent}%",
        };
    }
}
