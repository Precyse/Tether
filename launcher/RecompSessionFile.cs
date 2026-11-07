using System.Text.Json;

namespace CoopLauncher;

/// <summary>One player of the session as the game reads it: the session slot (also its pad: the host is slot 0, pad 1), the display name, and who is who.</summary>
public sealed record SessionPlayer(int Slot, string Name, bool Host, bool Local);

/// <summary>
/// What the lobby decided, handed to the game when it starts so it can show who is in the session in its own menus: the file
/// <c>game/session.json</c> (JSON, camelCase, written whole before each start and removed when the session ends).
/// <code>
/// { "version": 1, "sessionId": "109775240912345", "role": "host" | "guest", "localSlot": 0, "hostSlot": 0, "maxPlayers": 4,
///   "roster": [ { "slot": 0, "name": "Amy", "host": true, "local": true }, ... ],
///   "settings": { "max_players": "4" } }
/// </code>
/// <c>sessionId</c> is the Steam lobby's id, the same on every machine; <c>roster</c> lists the players in the slot map (host first);
/// <c>settings</c> are the host's options as the lobby carries them. The refusals the launcher can show are
/// <see cref="RefusalKind"/> names, stable and only ever appended, so the game may use them as reason codes.
/// </summary>
public static class RecompSessionFile
{
    public const int Version = 1;

    private static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true };

    /// <summary>The file's text for a session.</summary>
    public static string Build(ulong sessionId, bool host, int localSlot, int maxPlayers, IEnumerable<SessionPlayer> roster, IReadOnlyDictionary<string, string> settings) =>
        JsonSerializer.Serialize(new
        {
            version = Version,
            sessionId = sessionId.ToString(System.Globalization.CultureInfo.InvariantCulture),
            role = host ? "host" : "guest",
            localSlot,
            hostSlot = (int)SlotAssigner.HostSlot,
            maxPlayers,
            roster = roster.OrderBy(player => player.Slot).ToList(),
            settings,
        }, Json);

    /// <summary>Writes the file whole beside itself and moves it over the old one, so the game never reads half of it. False when it could not be written.</summary>
    public static bool Write(string path, string text)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path + ".tmp", text);
            File.Move(path + ".tmp", path, overwrite: true);
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not write the session file: {e.Message}");
            return false;
        }
    }

    public static void Delete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not remove the session file: {e.Message}");
        }
    }
}
