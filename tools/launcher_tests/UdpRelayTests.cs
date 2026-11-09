using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using Tether;

namespace LauncherTests;

/// <summary>UdpRelayLink with plain UDP sockets as the engines: a joiner's datagram reaches the host's engine from a port of its own, replies
/// find their way back to the right slot whatever the order, the engine's goodbye still goes out after it ended, and the profile's peer budget lets a
/// join snapshot through.</summary>
public static class UdpRelayTests
{
    private const int WaitMs = 3000;
    private const int QuietMs = 200;
    private const int PollMs = 5;
    private const int RelayPollWaitMs = 1500;
    private const int ChunkBytes = 1400;
    private const int SnapshotChunks = 13_000;
    private const int PerSlot = 50;
    private const byte HostSlot = SlotAssigner.HostSlot;

    private sealed class Engine : IDisposable
    {
        private readonly UdpClient _socket = new(new IPEndPoint(IPAddress.Loopback, 0));

        public Engine()
        {
            new Thread(ReadLoop) { IsBackground = true }.Start();
        }

        public int Port => ((IPEndPoint)_socket.Client.LocalEndPoint!).Port;

        public ConcurrentQueue<(byte[] Data, int FromPort)> Received { get; } = new();

        public void SendTo(int port, byte[] data) => _socket.Send(data, data.Length, new IPEndPoint(IPAddress.Loopback, port));

        public void Dispose() => _socket.Close();

        private void ReadLoop()
        {
            var from = new IPEndPoint(IPAddress.Any, 0);
            while (true)
            {
                try
                {
                    Received.Enqueue((_socket.Receive(ref from), from.Port));
                }
                catch (SocketException e) when (e.SocketErrorCode == SocketError.ConnectionReset)
                {
                }
                catch (Exception e) when (e is SocketException or ObjectDisposedException)
                {
                    return;
                }
            }
        }
    }

    private static Frame Datagram(byte slot, byte[] payload) => new(UdpRelayLink.DatagramType, 0, slot, payload);

    private static bool PumpUntil(UdpRelayLink relay, Func<bool> done, int timeoutMs = WaitMs)
    {
        var until = Environment.TickCount64 + timeoutMs;
        while (!done() && Environment.TickCount64 < until)
        {
            relay.Pump();
            Thread.Sleep(PollMs);
        }
        return done();
    }

    public static void Run()
    {
        HostRelay();
        Ordering();
        JoinerRelay();
        Readiness();
        SnapshotBudget();
    }

    private static void HostRelay()
    {
        using var engine = new Engine();
        using var relay = new UdpRelayLink(RelayRole.Host, engine.Port, () => true);
        var back = new List<Frame>();
        relay.GameFrame += back.Add;
        relay.Send(Datagram(1, [1]));
        relay.Send(Datagram(2, [2]));
        relay.Send(Datagram(1, [3]));
        relay.Send(new Frame(0x0102, 0, 1, [9]));
        Check.That(SpinUntil(() => engine.Received.Count >= 3), "host: the engine gets the datagrams of both slots");
        Thread.Sleep(QuietMs);
        var received = engine.Received.ToArray();
        Check.That(received.Length == 3, "host: a frame of another type is not the engine's");
        var portOf = received.GroupBy(r => r.Data[0] == 2 ? 2 : 1).ToDictionary(g => g.Key, g => g.Select(r => r.FromPort).Distinct().ToList());
        Check.That(portOf[1].Count == 1 && portOf[2].Count == 1 && portOf[1][0] != portOf[2][0], "host: each slot reaches the engine from a source port of its own, the same one every time");
        Check.That(portOf[1][0] != engine.Port && portOf[2][0] != engine.Port, "host: the source ports are not the engine's own");
        Check.That(relay.OpenSlots.Order().SequenceEqual([(byte)1, (byte)2]), "host: a socket per slot that sent");

        engine.SendTo(portOf[2][0], [20, 20]);
        engine.SendTo(portOf[1][0], [10]);
        Check.That(PumpUntil(relay, () => back.Count == 2), "host: the engine's replies come out");
        Check.That(back.All(f => f.Type == UdpRelayLink.DatagramType && f.Flags == 0), "host: replies are unreliable datagram frames");
        Check.That(back.Single(f => f.Slot == 2).Payload.SequenceEqual(new byte[] { 20, 20 }) && back.Single(f => f.Slot == 1).Payload.SequenceEqual(new byte[] { 10 }),
            "host: each reply is addressed to the slot whose port it answered");

        relay.Send(new Frame(Msg.PeerDown, Framing.FlagReliable, 0, [1]));
        Check.That(relay.OpenSlots.SequenceEqual([(byte)2]), "host: PEER_DOWN closes that slot's socket");
        relay.Send(Datagram(1, [4]));
        Check.That(SpinUntil(() => engine.Received.Count >= 4), "host: a slot that comes back is carried again");
        Check.That(engine.Received.ToArray()[3].FromPort != portOf[1][0], "host: from a new source port, as a new joiner");
    }

    private static void Ordering()
    {
        using var engine = new Engine();
        using var relay = new UdpRelayLink(RelayRole.Host, engine.Port, () => true);
        var back = new List<Frame>();
        relay.GameFrame += back.Add;
        var sent = new List<(byte Slot, byte[] Data)>();
        for (var i = 0; i < PerSlot; i++)
            foreach (var slot in new byte[] { 3, 1, 2 }.OrderBy(_ => Random.Shared.Next()))
                sent.Add((slot, [slot, (byte)i, .. new byte[ChunkBytes - 2]]));
        foreach (var (slot, data) in sent) relay.Send(Datagram(slot, data));
        Check.That(SpinUntil(() => engine.Received.Count == sent.Count), "order: every datagram of three slots arrives");
        var arrived = engine.Received.ToArray();
        var portsBySlot = arrived.GroupBy(r => r.Data[0]).ToDictionary(g => g.Key, g => g.Select(r => r.FromPort).Distinct().Count());
        Check.That(portsBySlot.Values.All(count => count == 1), "order: one source port per slot however the datagrams interleave");
        Check.That(arrived.Select(r => (r.Data[0], r.Data[1])).Order().SequenceEqual(sent.Select(s => (s.Data[0], s.Data[1])).Order()), "order: the same datagrams, in any order");
        Check.That(arrived.All(r => r.Data.Length == ChunkBytes), "order: datagrams keep their size");

        var portOfSlot = arrived.GroupBy(r => r.Data[0]).ToDictionary(g => g.Key, g => g.First().FromPort);
        foreach (var (slot, port) in portOfSlot.OrderBy(_ => Random.Shared.Next())) engine.SendTo(port, [slot]);
        Check.That(PumpUntil(relay, () => back.Count == 3), "order: three replies out");
        Check.That(back.Select(f => f.Slot).Order().SequenceEqual([(byte)1, (byte)2, (byte)3]) && back.All(f => f.Payload[0] == f.Slot), "order: every reply carries its own slot");
    }

    private static void JoinerRelay()
    {
        using var engine = new Engine();
        using var relay = new UdpRelayLink(RelayRole.Joiner, 0, () => true);
        var back = new List<Frame>();
        relay.GameFrame += back.Add;
        relay.Send(Datagram(HostSlot, [7]));
        Thread.Sleep(QuietMs);
        Check.That(engine.Received.IsEmpty, "joiner: nothing is sent before the engine has said where it is");
        engine.SendTo(relay.Port, [1, 2, 3]);
        Check.That(PumpUntil(relay, () => back.Count == 1), "joiner: the engine's datagram comes out");
        Check.That(back[0].Slot == HostSlot && back[0].Payload.SequenceEqual(new byte[] { 1, 2, 3 }) && back[0].Flags == 0, "joiner: addressed to the host, unreliable, as is");
        relay.Send(Datagram(HostSlot, [8, 8]));
        Check.That(SpinUntil(() => engine.Received.Count == 1), "joiner: the host's reply reaches the engine");
        Check.That(engine.Received.ToArray()[0].FromPort == relay.Port, "joiner: from the port the engine joined");
    }

    private static void Readiness()
    {
        var running = false;
        using var relay = new UdpRelayLink(RelayRole.Joiner, 0, () => running);
        var ready = 0;
        relay.AdapterReady += () => ready++;
        relay.Pump();
        Check.That(!relay.IsReady && ready == 0, "ready: not while the engine is not running");
        running = true;
        Check.That(PumpUntil(relay, () => relay.IsReady, RelayPollWaitMs) && ready == 1, "ready: once the engine runs, announced once");
        running = false;
        Check.That(PumpUntil(relay, () => !relay.IsReady, RelayPollWaitMs), "ready: not when the engine is gone");
    }

    private static void SnapshotBudget()
    {
        var limits = GameProfile.Load("warriors").PeerBudget;
        var frame = new Frame(UdpRelayLink.DatagramType, 0, 0, new byte[ChunkBytes]);
        Check.That(Admitted(new PeerBudget(limits), frame) == SnapshotChunks, "budget: the warriors profile lets a join snapshot of 13000 datagrams through at once");
        Check.That(Admitted(new PeerBudget(), frame) < SnapshotChunks / 10, "budget: the default budget would hold most of it back");
    }

    private static int Admitted(PeerBudget budget, Frame frame) => Enumerable.Range(0, SnapshotChunks).Count(_ => budget.Admit(HostSlot, frame, 0));

    private static bool SpinUntil(Func<bool> done)
    {
        var until = Environment.TickCount64 + WaitMs;
        while (!done() && Environment.TickCount64 < until) Thread.Sleep(PollMs);
        return done();
    }
}
