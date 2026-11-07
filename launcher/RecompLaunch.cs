using System.ComponentModel;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;

namespace CoopLauncher;

/// <summary>
/// Starts the built game with the netplay role of this session. The engine keeps its own UDP netplay; the launcher gives it a loopback
/// address and relays the datagrams (<see cref="UdpRelayLink"/>), so the host's engine listens on a free local port and a joiner's engine
/// joins the relay's port. The arguments go on the start stub's command line (it passes them on to the engine, after the settings of
/// <c>warriors.ini</c>).
/// </summary>
public static class RecompLaunch
{
    public const int MinPlayers = 2;
    public const int MaxPlayers = 4;

    private const string LoopbackHost = "127.0.0.1";

    /// <summary>The host's engine: listens on the local port, expects this many players (itself included) before the lockstep starts.</summary>
    public static IReadOnlyList<string> HostArgs(int port, int players) =>
        ["--net-host", port.ToString(), "--net-bind", LoopbackHost, "--net-players", Math.Clamp(players, MinPlayers, MaxPlayers).ToString()];

    /// <summary>A joiner's engine: joins the relay's port, on a memory card of its own for this session.</summary>
    public static IReadOnlyList<string> JoinArgs(int relayPort, string sessionCard) =>
        ["--net-join", $"{LoopbackHost}:{relayPort}", "--memcard", sessionCard];

    /// <summary>A UDP port on the loopback address that nothing uses now.</summary>
    public static int FreeUdpPort()
    {
        using var probe = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        return ((IPEndPoint)probe.Client.LocalEndPoint!).Port;
    }

    /// <summary>Starts the game's stub with the session's arguments. Anything but Started or AlreadyRunning means the session cannot go on.</summary>
    public static LaunchResult Start(GameProfile profile, string gameDir, IReadOnlyList<string> args)
    {
        if (GameLauncher.IsRunning(profile))
        {
            Log.Info($"{profile.Name} is already running, not starting it again");
            return LaunchResult.AlreadyRunning;
        }
        if (RecompInstall.Status(profile, gameDir).State != ModState.Installed)
        {
            Log.Info($"The {profile.Name} build is missing or damaged, build it from the launcher first");
            return LaunchResult.ModNotInstalled;
        }
        var exe = Path.Combine(gameDir, profile.Exe);
        var start = new ProcessStartInfo(exe) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(exe)! };
        foreach (var argument in args) start.ArgumentList.Add(argument);
        Log.Info($"Starting {profile.Name}: {exe} {string.Join(' ', args)}");
        try
        {
            Process.Start(start)?.Dispose();
            return LaunchResult.Started;
        }
        catch (Win32Exception e)
        {
            Log.Info($"Could not start {profile.Name}: {e.Message}");
            return LaunchResult.SteamStartFailed;
        }
    }

    /// <summary>Deletes the guest's session card (a new one starts with the next session); a card the running game holds stays.</summary>
    public static void DeleteSessionCard(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"Could not delete the session card: {e.Message}");
        }
    }
}
