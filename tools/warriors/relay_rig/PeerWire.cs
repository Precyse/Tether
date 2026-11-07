using System.Diagnostics;
using CoopLauncher;

namespace RelayRig;

/// <summary>Stands in for two sessions and the transport between them: a host relay and the relays of its joiners. What a relay hands out is
/// passed through the profile's peer budget (as <see cref="Session"/> does with what arrives from a peer) and, after the one-way delay, given to the
/// relay on the other side with the source slot the receiving session would write.</summary>
internal sealed class PeerWire
{
    private readonly UdpRelayLink _host;
    private readonly Dictionary<byte, UdpRelayLink> _joiners = new();
    private readonly PeerBudget _hostBudget;
    private readonly Dictionary<byte, PeerBudget> _joinerBudgets = new();
    private readonly PeerBudgetLimits? _limits;
    private readonly Stopwatch _clock;
    private readonly int _delayMs;
    private readonly Queue<(long DueMs, Action Deliver)> _inFlight = new();

    public PeerWire(UdpRelayLink host, PeerBudgetLimits? limits, int delayMs, Stopwatch clock)
    {
        _host = host;
        _limits = limits;
        _delayMs = delayMs;
        _clock = clock;
        _hostBudget = new PeerBudget(limits);
        host.GameFrame += frame => Forward(frame, toJoiner: true);
    }

    public long DatagramsToJoiners { get; private set; }

    public long DatagramsToHost { get; private set; }

    public long BytesToJoiners { get; private set; }

    public long Dropped { get; private set; }

    public void AddJoiner(byte slot, UdpRelayLink joiner)
    {
        _joiners[slot] = joiner;
        _joinerBudgets[slot] = new PeerBudget(_limits);
        joiner.GameFrame += frame => Forward(frame with { Slot = slot }, toJoiner: false);
    }

    /// <summary>The loop's turn: both ends pump, and what has been in flight long enough arrives.</summary>
    public void Pump()
    {
        _host.Pump();
        foreach (var joiner in _joiners.Values) joiner.Pump();
        while (_inFlight.TryPeek(out var next) && next.DueMs <= _clock.ElapsedMilliseconds)
        {
            _inFlight.Dequeue();
            next.Deliver();
        }
    }

    /// <summary>A frame from a relay: for a joiner, its Slot names the joiner; from a joiner (its Slot rewritten to the joiner's own), the host.</summary>
    private void Forward(Frame frame, bool toJoiner)
    {
        var due = _clock.ElapsedMilliseconds + _delayMs;
        if (toJoiner)
        {
            if (!_joiners.TryGetValue(frame.Slot, out var joiner)) return;
            var toSlot = frame.Slot;
            _inFlight.Enqueue((due, () => Arrive(joiner, _joinerBudgets[toSlot], frame with { Slot = SlotAssigner.HostSlot }, SlotAssigner.HostSlot)));
            DatagramsToJoiners++;
            BytesToJoiners += frame.Payload.Length;
        }
        else
        {
            var fromSlot = frame.Slot;
            _inFlight.Enqueue((due, () => Arrive(_host, _hostBudget, frame, fromSlot)));
            DatagramsToHost++;
        }
    }

    private void Arrive(UdpRelayLink receiver, PeerBudget budget, Frame frame, byte sourceSlot)
    {
        if (budget.Admit(sourceSlot, frame, Environment.TickCount64)) receiver.Send(frame);
        else Dropped++;
    }
}
