namespace CoopLauncher;

/// <summary>
/// What the session and the loop need from the game's side of the launcher: game frames in both directions, the game's reports
/// and whether the game is linked. <see cref="LoopbackBridge"/> is the TCP link to an in-game adapter; <see cref="UdpRelayLink"/>
/// carries the UDP datagrams of a game that has no adapter.
/// </summary>
public interface IAdapterLink : IDisposable
{
    /// <summary>The game is linked (its adapter is connected, or its process runs).</summary>
    bool IsReady { get; }

    /// <summary>The link came up (also again after a reconnect).</summary>
    event Action? AdapterReady;

    /// <summary>Game frame (type >= 0x0100) from the game, opaque.</summary>
    event Action<Frame>? GameFrame;

    /// <summary>The game wrote a save file (SAVE_CHANGED).</summary>
    event Action? SaveChanged;

    /// <summary>The player changed an option on the game's own co-op page (OPTION_SET), with the frame's payload.</summary>
    event Action<byte[]>? OptionSet;

    /// <summary>The game says where it is (GAME_STATE), with the frame's payload (<see cref="GameState.Parse"/>).</summary>
    event Action<byte[]>? GameStateReceived;

    /// <summary>Queues the frame for the game; never blocks.</summary>
    void Send(Frame frame);

    /// <summary>Called by the main loop: raises the events of whatever arrived since the last call.</summary>
    void Pump();
}
