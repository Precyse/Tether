using System.Security.Cryptography;

namespace Tether.Gui;

/// <summary>A game's pictures for the window: the art its profile links (downloaded once into the art folder, kept only when its
/// SHA-256 matches), else Steam's (<see cref="SteamArt"/>). Missing art is normal: a lookup returns null and the page shows without it.</summary>
internal static class GameArt
{
    /// <summary>The pictures a game has.</summary>
    public enum Kind { Capsule, Hero, Logo }

    private const string LibraryFolderName = "library";
    private const string UserAgent = "Tether";
    private const string PartSuffix = ".part";
    private static readonly TimeSpan DownloadTimeout = TimeSpan.FromSeconds(30);
    private static readonly string[] Extensions = { ".png", ".jpg" };
    private static readonly HttpClient Client = CreateClient();
    private static readonly HashSet<string> Failed = new();

    /// <summary>The file of a game's picture, or null.</summary>
    public static string? FindFile(GameProfile profile, Kind kind) =>
        LinkOf(profile, kind) is { } link ? Downloaded(profile, kind, link) : profile.SteamAppId > 0 ? SteamArt.FindFile(profile.SteamAppId, kind) : null;

    /// <summary>Downloads the linked pictures a game does not have yet (blocking: call it off the UI thread). A failure is logged once
    /// per picture and run, and the next call tries again.</summary>
    public static void Fetch(GameProfile profile)
    {
        foreach (var kind in Enum.GetValues<Kind>())
        {
            if (LinkOf(profile, kind) is not { } link || Downloaded(profile, kind, link) != null || CachePath(profile, kind, link) is not { } path) continue;
            try
            {
                Download(link, path);
            }
            catch (Exception e) when (e is HttpRequestException or IOException or UnauthorizedAccessException or TaskCanceledException or InvalidDataException)
            {
                lock (Failed)
                    if (Failed.Add(path)) Log.Info($"{profile.Name} {kind} art not downloaded: {e.Message}");
            }
        }
    }

    private static ArtLink? LinkOf(GameProfile profile, Kind kind) => kind switch
    {
        Kind.Capsule => profile.Art?.Capsule,
        Kind.Hero => profile.Art?.Hero,
        _ => profile.Art?.Logo,
    };

    /// <summary>The downloaded file of a linked picture, or null while it is not there.</summary>
    private static string? Downloaded(GameProfile profile, Kind kind, ArtLink link) =>
        CachePath(profile, kind, link) is { } path && File.Exists(path) ? path : null;

    /// <summary>Where a linked picture is kept: named by the game, the kind and the start of its SHA-256, so a changed link is a new
    /// file. Null for a link that is not https or not a .png or .jpg.</summary>
    private static string? CachePath(GameProfile profile, Kind kind, ArtLink link)
    {
        if (!Uri.TryCreate(link.Url, UriKind.Absolute, out var uri) || uri.Scheme != Uri.UriSchemeHttps) return null;
        var extension = Path.GetExtension(uri.AbsolutePath).ToLowerInvariant();
        if (!Extensions.Contains(extension) || link.Sha256.Length < 8) return null;
        var name = $"{profile.Id}_{kind.ToString().ToLowerInvariant()}_{link.Sha256[..8].ToLowerInvariant()}{extension}";
        return Path.Combine(AppData.ArtDir, LibraryFolderName, name);
    }

    private static void Download(ArtLink link, string path)
    {
        using var cancel = new CancellationTokenSource(DownloadTimeout);
        var bytes = Client.GetByteArrayAsync(link.Url, cancel.Token).GetAwaiter().GetResult();
        if (!Convert.ToHexString(SHA256.HashData(bytes)).Equals(link.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"{link.Url} does not match its SHA-256");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var part = path + PartSuffix;
        File.WriteAllBytes(part, bytes);
        File.Move(part, path, overwrite: true);
    }

    private static HttpClient CreateClient()
    {
        var client = new HttpClient();
        client.DefaultRequestHeaders.UserAgent.ParseAdd(UserAgent);
        return client;
    }
}
