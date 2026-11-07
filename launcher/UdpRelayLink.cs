using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace CoopLauncher;

/// <summary>Which end of the session this launcher's engine is: the host's engine listens on a UDP port, a joiner's engine sends to one.</summary>
public enum RelayRole { Host, Joiner }

/// <summary>
/// The adapter-less link of a game whose engine speaks UDP to its peers (The Warriors' netplay): the launcher carries the datagrams
/// between the engines over the peer link, as opaque unreliable frames of type <see cref="DatagramType"/>. The engines see
/// ordinary UDP on 127.0.0.1 and need no change.
/// <list type="bullet">
/// <item>Host: the engine listens on <c>enginePort</c>. Each remote joiner gets its own loopback socket (a distinct source port,
/// opened with its first datagram and closed with its PEER_DOWN), so the engine tells the joiners apart as it does on a LAN.</item>
/// <item>Joiner: one loopback socket, <see cref="Port"/>, which the engine is told to join; its datagrams go to the host's launcher
/// (slot <see cref="SlotAssigner.HostSlot"/>) and the host's replies come back to the address the engine sent from.</item>
/// </list>
/// The link is ready while the engine's process runs; the socket threads only queue what arrives, the loop thread raises it from
/// <see cref="Pump"/>. A datagram the engine sent just before it exited (its goodbye) still goes out.
/// </summary>
public sealed class UdpRelayLink : IAdapterLink
{
    public const ushort DatagramType = 0x0400;

    private const int MaxDatagramBytes = 65_535;
    private const long EnginePollMs = 500;

    private readonly RelayRole _role;
    private readonly int _enginePort;
    private readonly Func<bool> _engineRunning;
    private readonly ConcurrentQueue<Frame> _inbox = new();
    private readonly Dictionary<byte, UdpClient> _joiners = new();
    private readonly UdpClient? _joinerSocket;
    private readonly EngineLogTail? _log;
    private volatile IPEndPoint? _engine;
    private GameState _state = new();
    private bool _ready;
    private long _nextPollMs;
    private bool _disposed;

    /// <param name="enginePort">Host: the port the engine listens on. Joiner: the port to open for it (0: any free one, read <see cref="Port"/>).</param>
    /// <param name="engineRunning">Whether the engine's process runs now.</param>
    /// <param name="log">The engine's log, whose <c>[net]</c> lines become GAME_STATE (<see cref="NetLog"/>); none: no state is reported.</param>
    public UdpRelayLink(RelayRole role, int enginePort, Func<bool> engineRunning, EngineLogTail? log = null)
    {
        _role = role;
        _enginePort = enginePort;
        _engineRunning = engineRunning;
        _log = log;
        if (role != RelayRole.Joiner) return;
        _joinerSocket = new UdpClient(new IPEndPoint(IPAddress.Loopback, enginePort));
        StartReader(_joinerSocket, 0);
        Log.Info($"UDP relay: joiner socket on 127.0.0.1:{Port}");
    }

    public event Action? AdapterReady;

    public event Action<Frame>? GameFrame;

    event Action? IAdapterLink.SaveChanged { add { } remove { } }

    event Action<byte[]>? IAdapterLink.OptionSet { add { } remove { } }

    public event Action<byte[]>? GameStateReceived;

    /// <summary>The port the host's engine listens on (the joiner's relay has none to give: 0 or the port it was asked for).</summary>
    public int EnginePort => _enginePort;

    /// <summary>The joiner's socket port, the one its engine joins (0 for the host's relay).</summary>
    public int Port => _joinerSocket is null ? 0 : ((IPEndPoint)_joinerSocket.Client.LocalEndPoint!).Port;

    public bool IsReady => _ready;

    /// <summary>Slots that have a socket to the host's engine (the host's relay only).</summary>
    public IReadOnlyCollection<byte> OpenSlots => _joiners.Keys;

    /// <summary>A datagram from a peer for the engine, or PEER_DOWN of a peer (its socket closes). Other frames are not the engine's.</summary>
    public void Send(Frame frame)
    {
        if (_disposed) return;
        if (frame.Type == Msg.PeerDown && frame.Payload.Length > 0) CloseJoiner(frame.Payload[0]);
        else if (frame.Type == DatagramType) Deliver(frame.Slot, frame.Payload);
    }

    public void Pump()
    {
        var now = Environment.TickCount64;
        if (now >= _nextPollMs)
        {
            _nextPollMs = now + EnginePollMs;
            var running = _engineRunning();
            if (running && !_ready)
            {
                _state = new GameState();
                AdapterReady?.Invoke();
            }
            _ready = running;
            ReportEngineState();
        }
        while (_inbox.TryDequeue(out var frame)) GameFrame?.Invoke(frame);
    }

    /// <summary>The engine's new <c>[net]</c> lines move the state; a changed state is reported as the GAME_STATE text of an adapter.</summary>
    private void ReportEngineState()
    {
        if (_log is null) return;
        var next = _log.ReadNewLines().Aggregate(_state, NetLog.Apply);
        if (next == _state) return;
        _state = next;
        GameStateReceived?.Invoke(Encoding.ASCII.GetBytes(string.Join(';', next.EncodePairs())));
    }

    public void Dispose()
    {
        _disposed = true;
        _joinerSocket?.Close();
        foreach (var socket in _joiners.Values) socket.Close();
        _joiners.Clear();
    }

    private void Deliver(byte fromSlot, byte[] datagram)
    {
        try
        {
            if (_role == RelayRole.Joiner)
            {
                if (_engine is { } engine) _joinerSocket!.Send(datagram, datagram.Length, engine);
            }
            else
            {
                if (!_joiners.TryGetValue(fromSlot, out var socket)) socket = OpenJoiner(fromSlot);
                socket.Send(datagram, datagram.Length, new IPEndPoint(IPAddress.Loopback, _enginePort));
            }
        }
        catch (Exception e) when (e is SocketException or ObjectDisposedException)
        {
            Log.Info($"UDP relay: could not pass a datagram from slot {fromSlot} to the engine: {e.Message}");
        }
    }

    private UdpClient OpenJoiner(byte slot)
    {
        var socket = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        _joiners[slot] = socket;
        StartReader(socket, slot);
        Log.Info($"UDP relay: slot {slot} reaches the engine from 127.0.0.1:{((IPEndPoint)socket.Client.LocalEndPoint!).Port}");
        return socket;
    }

    private void CloseJoiner(byte slot)
    {
        if (!_joiners.Remove(slot, out var socket)) return;
        socket.Close();
        Log.Info($"UDP relay: slot {slot} closed");
    }

    /// <summary>Queues what the engine sends to this socket as frames for <paramref name="toSlot"/>; a joiner's socket also learns the
    /// engine's address from it.</summary>
    private void StartReader(UdpClient socket, byte toSlot)
    {
        new Thread(() =>
        {
            var from = new IPEndPoint(IPAddress.Any, 0);
            while (true)
            {
                byte[] datagram;
                try
                {
                    datagram = socket.Receive(ref from);
                }
                catch (SocketException e) when (e.SocketErrorCode == SocketError.ConnectionReset)
                {
                    continue;  // an earlier send reached a port nobody listens on (the engine is not up yet, or has left)
                }
                catch (Exception e) when (e is SocketException or ObjectDisposedException)
                {
                    return;  // the socket was closed
                }
                if (datagram.Length > MaxDatagramBytes) continue;
                if (_role == RelayRole.Joiner) _engine = new IPEndPoint(from.Address, from.Port);
                _inbox.Enqueue(new Frame(DatagramType, 0, toSlot, datagram));
            }
        }) { IsBackground = true, Name = $"udp-relay-{toSlot}" }.Start();
    }
}
