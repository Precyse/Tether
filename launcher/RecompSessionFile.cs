using System.Globalization;
using System.Text;

namespace Tether;

/// <summary>One player of the session as the game reads it: the session slot (also its pad port: the host is slot 0, pad 1) and the display name.</summary>
public sealed record SessionPlayer(int Slot, string Name);

/// <summary>
/// What the lobby decided, handed to the game when it starts so it can show who is in the session in its own menus: the INI file
/// <c>game/session.ini</c>, written whole before each start and removed when the session ends. The format is the one the game's co-op
/// front end design asks for (<c>--coop-session</c>, see <see cref="RecompProfile.SessionArg"/>):
/// <code>
/// [session]
/// epoch=1            ; the slot map's epoch
/// session_id=1097752 ; the Steam lobby's id, the same on every machine
/// role=host          ; host or guest
/// local_slot=0       ; this machine's pad port
/// max_players=4      ; the host's setting
/// status_file=C:\...\status.txt   ; where the game writes what it is doing (<see cref="EngineStatusFile"/>)
/// [roster]           ; slot = pad port, the same on every machine; an open slot has no name
/// 0=Alex
/// 1=Sam
/// 2=
/// 3=
/// [host]
/// profile_slot=-1    ; the lobby does not pick a profile
/// memcard=C:\...\saves\card.ps2   ; this machine's card (a guest's: the copy of the host's the launcher sent)
/// friendly_fire=0    ; the host's other terms (every host option of the profile but the player count): shared by all machines
/// [local]            ; this machine's own options (resolution, anti-aliasing, volume, controller): never shared; omitted when none
/// scale=4
/// </code>
/// Names are one line with no <c>=</c> or control characters; the game cuts them to what its font can draw. The refusals the launcher can show are
/// <see cref="RefusalKind"/> names, stable and only ever appended, so the game may use them as reason codes.
/// </summary>
public static class RecompSessionFile
{
    private const char Replacement = ' ';

    /// <summary>The file's text for a session.</summary>
    public static string Build(uint epoch, ulong sessionId, bool host, int localSlot, int maxPlayers, IEnumerable<SessionPlayer> roster, string memcard, string statusFile,
        IEnumerable<KeyValuePair<string, string>> hostTerms, IEnumerable<KeyValuePair<string, string>> localOptions)
    {
        var names = roster.ToDictionary(player => player.Slot, player => player.Name);
        var text = new StringBuilder();
        text.AppendLine("[session]");
        text.AppendLine(string.Create(CultureInfo.InvariantCulture, $"epoch={epoch}"));
        text.AppendLine(string.Create(CultureInfo.InvariantCulture, $"session_id={sessionId}"));
        text.AppendLine($"role={(host ? "host" : "guest")}");
        text.AppendLine(string.Create(CultureInfo.InvariantCulture, $"local_slot={localSlot}"));
        text.AppendLine(string.Create(CultureInfo.InvariantCulture, $"max_players={maxPlayers}"));
        text.AppendLine($"status_file={statusFile}");
        text.AppendLine("[roster]");
        for (var slot = 0; slot < maxPlayers; slot++)
            text.AppendLine(string.Create(CultureInfo.InvariantCulture, $"{slot}={(names.TryGetValue(slot, out var name) ? OneLine(name) : string.Empty)}"));
        text.AppendLine("[host]");
        text.AppendLine("profile_slot=-1");
        text.AppendLine($"memcard={memcard}");
        foreach (var (key, value) in hostTerms) text.AppendLine($"{key}={OneLine(value)}");
        var local = localOptions.ToList();
        if (local.Count == 0) return text.ToString();
        text.AppendLine("[local]");
        foreach (var (key, value) in local) text.AppendLine($"{key}={OneLine(value)}");
        return text.ToString();
    }

    /// <summary>A name the INI can hold: control characters, '=' and ';' become spaces, the ends are trimmed.</summary>
    public static string OneLine(string name) =>
        new string(name.Select(c => char.IsControl(c) || c is '=' or ';' ? Replacement : c).ToArray()).Trim();

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
