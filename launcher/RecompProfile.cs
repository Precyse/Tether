using System.Collections.Concurrent;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Tether;

/// <summary>
/// A game with no Steam app that the launcher builds on the player's PC from their own disc image (The Warriors' native build, made by
/// the warriors-setup tool). The game folder is <see cref="InstallDir"/> under the launcher's data folder; the profile's Exe is
/// relative to it. Paths other than Setup and InstallDir are relative to the game folder.
/// </summary>
/// <param name="Setup">The setup tool, relative to the repo root; a path in the settings (<see cref="RecompInstall.SetupOption"/>) replaces it.</param>
/// <param name="DiscPrompt">What the disc picker asks for.</param>
/// <param name="InstallDir">The game folder, relative to the launcher's data folder.</param>
/// <param name="BuildInfo">The file the tool writes when a build is complete (<see cref="RecompBuildInfo"/>).</param>
/// <param name="Engine">The engine the build made, whose hash the build info records.</param>
/// <param name="LogsDir">The tool's own logs.</param>
/// <param name="EngineLog">The engine's stderr, written by the game's start stub.</param>
/// <param name="Card">The host's memory card (the tool's default one). The netplay snapshot holds the card's read position, not its contents, so a
/// guest plays on a copy of it: the profile's save sync sends this file to every guest.</param>
/// <param name="SessionCard">Where that copy lands on a guest (the save sync's session folder); deleted with the session.</param>
/// <param name="SessionFile">What the lobby decided, for the game to read at its start (<see cref="RecompSessionFile"/>).</param>
/// <param name="StatusFile">What the game is doing, written by the game for the launcher to read while it runs (<see cref="EngineStatusFile"/>).</param>
/// <param name="SessionArg">The engine argument that takes the session file, once the engine has one; none: the file is written and not passed.</param>
/// <param name="LocalCoop">The game can start a local co-op session (<see cref="LocalCoop"/>): every player on this PC, no lobby; needs <paramref name="SessionArg"/>.</param>
public sealed record RecompProfile(
    string Setup, string DiscPrompt, string InstallDir, string BuildInfo, string Engine, string LogsDir, string EngineLog, string Card, string SessionCard, string SessionFile,
    string StatusFile, string? SessionArg = null, bool LocalCoop = false);

/// <summary>The build a setup run made, from its <c>build.json</c>: the three parts of the fingerprint the engines compare when they meet
/// (generated game code, runtime sources, memory patches) and the hash of the engine it installed. Two machines play together only when
/// the fingerprints are equal.</summary>
public sealed record RecompBuildInfo(string Game, string Runtime, string Patches, string? EngineSha256)
{
    private const char PartSeparator = ':';
    private const int ShortHashChars = 6;

    /// <summary>The names the players know the parts of the fingerprint by, in the order of <see cref="Fingerprint"/>.</summary>
    private static readonly string[] PartNames = ["game code", "engine", "patches"];

    /// <summary>The fingerprint as one text, the parts in the order the engine names them.</summary>
    public string Fingerprint => $"{Game}{PartSeparator}{Runtime}{PartSeparator}{Patches}";

    /// <summary>The parts that differ between the host's fingerprint and this machine's, each with both short hashes (<c>engine 18e437 vs 8b7c74</c>), host first;
    /// empty when the texts are not fingerprints.</summary>
    public static string Differences(string host, string local)
    {
        var hostParts = host.Split(PartSeparator);
        var localParts = local.Split(PartSeparator);
        if (hostParts.Length != PartNames.Length || localParts.Length != PartNames.Length) return string.Empty;
        return string.Join(", ", PartNames.Select((name, i) => (name, host: hostParts[i], local: localParts[i])).Where(part => part.host != part.local)
            .Select(part => $"{part.name} {Short(part.host)} vs {Short(part.local)}"));
    }

    private static string Short(string hash) => hash.Length > ShortHashChars ? hash[..ShortHashChars] : hash;

    /// <summary>SHA-256 of the fingerprint text: what BUILD_INFO carries in the place of a mod hash.</summary>
    public byte[] Hash => SHA256.HashData(Encoding.UTF8.GetBytes(Fingerprint));

    /// <summary>The build info in a file; null when the file is missing, is not JSON or lacks a part of the fingerprint.</summary>
    public static RecompBuildInfo? Read(string path)
    {
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(path));
            var root = document.RootElement;
            if (!root.TryGetProperty("fingerprint", out var fingerprint)) return null;
            var game = Text(fingerprint, "game");
            var runtime = Text(fingerprint, "runtime");
            var patches = Text(root, "patch_list_sha256");
            return game is null || runtime is null || patches is null ? null : new RecompBuildInfo(game, runtime, patches, Text(root, "engine_sha256"));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }

    private static string? Text(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String && value.GetString() is { Length: > 0 } text ? text : null;
}

/// <summary>Where a <see cref="RecompProfile"/> game lives and what state its build is in.</summary>
public static class RecompInstall
{
    /// <summary>The per-game setting that holds the disc image the player picked.</summary>
    public const string DiscOption = "disc";

    /// <summary>The per-game setting that holds another setup tool than the profile's.</summary>
    public const string SetupOption = "setup";

    private static readonly ConcurrentDictionary<(string Path, long Length, DateTime Written), string> EngineHashes = new();

    /// <summary>The game folder: where the tool builds and where the game runs.</summary>
    public static string GameDir(RecompProfile recomp) => AppData.Resolve(recomp.InstallDir);

    /// <summary>The disc image the player picked, null when none is set or the file is gone.</summary>
    public static string? DiscPath(GameProfile profile) =>
        AppSettings.Current.Option(profile.Id, DiscOption) is { Length: > 0 } disc && File.Exists(disc) ? disc : null;

    /// <summary>The setup tool: the one in the settings, else the profile's under the repo.</summary>
    public static string SetupPath(GameProfile profile) =>
        AppSettings.Current.Option(profile.Id, SetupOption) is { Length: > 0 } custom ? custom : Path.Combine(RepoPaths.RepoRoot, profile.Recomp!.Setup);

    public static string BuildInfoPath(GameProfile profile, string gameDir) => Path.Combine(gameDir, profile.Recomp!.BuildInfo);

    public static string LogsDir(GameProfile profile, string gameDir) => Path.Combine(gameDir, profile.Recomp!.LogsDir);

    public static string EngineLogPath(GameProfile profile, string gameDir) => Path.Combine(gameDir, profile.Recomp!.EngineLog);

    public static string CardPath(GameProfile profile, string gameDir) => Path.Combine(gameDir, profile.Recomp!.Card);

    public static string SessionCardPath(GameProfile profile, string gameDir) => Path.Combine(gameDir, profile.Recomp!.SessionCard);

    public static string SessionFilePath(GameProfile profile, string gameDir) => Path.Combine(gameDir, profile.Recomp!.SessionFile);

    public static string StatusFilePath(GameProfile profile, string gameDir) => Path.Combine(gameDir, profile.Recomp!.StatusFile);

    /// <summary>The build in the game folder: Installed when the build info, the start stub and the engine are there and the engine is the
    /// one the build info names, Modified when the engine differs (a half-copied or edited install: building again repairs it),
    /// NotInstalled otherwise.</summary>
    public static ModStatus Status(GameProfile profile, string gameDir)
    {
        if (RecompBuildInfo.Read(BuildInfoPath(profile, gameDir)) is not { } info) return new ModStatus(ModState.NotInstalled, 0);
        var engine = Path.Combine(gameDir, profile.Recomp!.Engine);
        if (!File.Exists(Path.Combine(gameDir, profile.Exe)) || !File.Exists(engine)) return new ModStatus(ModState.NotInstalled, 0);
        var engineMatches = info.EngineSha256 is null || string.Equals(EngineHash(engine), info.EngineSha256, StringComparison.OrdinalIgnoreCase);
        return new ModStatus(engineMatches ? ModState.Installed : ModState.Modified, 0);
    }

    /// <summary>The fingerprint text of the complete build in the game folder, null when there is none.</summary>
    public static string? Fingerprint(GameProfile profile, string gameDir) =>
        Status(profile, gameDir).State == ModState.Installed ? RecompBuildInfo.Read(BuildInfoPath(profile, gameDir))?.Fingerprint : null;

    /// <summary>The hash BUILD_INFO carries for this build; null when there is no complete build.</summary>
    public static byte[]? ContentHash(GameProfile profile, string gameDir) =>
        Status(profile, gameDir).State == ModState.Installed ? RecompBuildInfo.Read(BuildInfoPath(profile, gameDir))?.Hash : null;

    /// <summary>Removes the built game: the start stub, the engine and the build info. The memory card, the logs and the tool's downloads
    /// and work stay (a rebuild is quick, a save is not).</summary>
    public static bool Remove(GameProfile profile, string gameDir)
    {
        try
        {
            File.Delete(Path.Combine(gameDir, profile.Exe));
            File.Delete(BuildInfoPath(profile, gameDir));
            var engineDir = Path.GetDirectoryName(Path.Combine(gameDir, profile.Recomp!.Engine))!;
            if (Directory.Exists(engineDir)) Directory.Delete(engineDir, recursive: true);
            Log.Info($"Removed the {profile.Name} build");
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not remove the {profile.Name} build: {e.Message}");
            return false;
        }
    }

    private static string EngineHash(string path)
    {
        var info = new FileInfo(path);
        return EngineHashes.GetOrAdd((info.FullName, info.Length, info.LastWriteTimeUtc), _ => ComponentUpdate.Sha256Of(path));
    }
}
