// The Warriors over the launcher's UDP relay, on one PC: a host engine and N joiner engines (the engine the existing recomp build made, run only),
// each pair connected through UdpRelayLink and the profile's peer budget. Reports the join time, the datagrams carried and whether the engines'
// state digests agree. Only processes this rig started are ever stopped. Usage: dotnet run -c Release --project tools/warriors/relay_rig -- --out <dir> [--frames N] [--joiners N] [--delay ms]
// With --build instead it runs the launcher's build step against the real setup tool (COOP_LAUNCHER_DATA names the launcher's data folder, whose settings name the disc and the tool).
using System.Diagnostics;
using Tether;
using RelayRig;

const string ScratchRoot = @"C:\Users\ahmal\coop-scratch\warriors";
const int HostSlotFirstJoiner = 1;
const int PumpMs = 10;
const int JoinerStartGapMs = 3000;
const int TimeoutSeconds = 900;
const string ConnectingMark = "connecting to host";
const string AnsweredMark = "host answered:";
const string ReceivedMark = "snapshot received";
const string StartedMark = "lockstep starts at frame";
const string InSyncMark = "in sync at frame";

if (args.Contains("--build")) return BuildStep.Run(args.SkipWhile(arg => arg != "--cancel-after").Skip(1).Select(int.Parse).FirstOrDefault());

var options = args.Chunk(2).Where(pair => pair.Length == 2).ToDictionary(pair => pair[0], pair => pair[1]);
string Option(string name, string fallback) => options.GetValueOrDefault(name, fallback);
var engine = Option("--engine", Path.Combine(ScratchRoot, @"recomp\build\native-warriors_p5\ps2run.exe"));
var elf = Option("--elf", Path.Combine(ScratchRoot, @"disc\SLUS_212.15"));
var iso = Option("--iso", @"C:\Users\ahmal\Downloads\Warriors, The (USA) (En,Fr,De,Es,It)\Warriors, The (USA) (En,Fr,De,Es,It).iso");
var outDir = Path.GetFullPath(Option("--out", "relay_rig_out"));
var frames = Option("--frames", "4000");
var joiners = int.Parse(Option("--joiners", "1"));
var delayMs = int.Parse(Option("--delay", "0"));
var digestEvery = Option("--digest", "300");
var profile = GameProfile.Load("warriors");
Directory.CreateDirectory(outDir);

var clock = Stopwatch.StartNew();
var runs = new List<EngineRun>();
var failures = 0;

void Check(bool ok, string what)
{
    Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {what}");
    if (!ok) failures++;
}

IEnumerable<string> Common(string name) => [elf, "--disc", iso, "--memcard", Path.Combine(outDir, name + ".card.ps2"), "--frames", frames, "--digest", $"{digestEvery}:{Path.Combine(outDir, name + ".dig")}"];

try
{
    var hostPort = RecompLaunch.FreeUdpPort();
    var host = new EngineRun("host", engine, Common("host").Concat(RecompLaunch.HostArgs(hostPort, joiners + 1)), Path.Combine(outDir, "host.log"), clock);
    runs.Add(host);
    using var hostRelay = new UdpRelayLink(RelayRole.Host, hostPort, () => host.Running, new EngineLogTail(host.LogPath));
    var wire = new PeerWire(hostRelay, profile.PeerBudget, delayMs, clock);
    var relays = new List<UdpRelayLink>();
    var started = 0;
    var nextStartMs = JoinerStartGapMs;
    string[] markers = [ConnectingMark, AnsweredMark, ReceivedMark, StartedMark, InSyncMark];

    while (clock.Elapsed.TotalSeconds < TimeoutSeconds && runs.Any(run => run.Running || run.ExitCode is null))
    {
        if (started < joiners && clock.ElapsedMilliseconds >= nextStartMs)
        {
            var name = $"join{++started}";
            var relay = new UdpRelayLink(RelayRole.Joiner, 0, () => runs.First(run => run.Name == name).Running);
            relays.Add(relay);
            wire.AddJoiner((byte)(HostSlotFirstJoiner + started - 1), relay);
            var args2 = Common(name).Concat(["--net-join", $"127.0.0.1:{relay.Port}"]);
            runs.Add(new EngineRun(name, engine, args2, Path.Combine(outDir, name + ".log"), clock));
            nextStartMs += JoinerStartGapMs;
        }
        wire.Pump();
        foreach (var run in runs) run.Poll(markers);
        Thread.Sleep(PumpMs);
    }

    foreach (var run in runs) run.Poll(markers);
    Console.WriteLine($"carried: {wire.DatagramsToJoiners} datagrams ({wire.BytesToJoiners / 1024 / 1024.0:F1} MiB) host to joiners, {wire.DatagramsToHost} joiners to host, {wire.Dropped} dropped by the budget");
    foreach (var run in runs.Where(run => run.Name != "host"))
    {
        long At(string mark) => run.Seen.TryGetValue(mark, out var ms) ? ms - run.StartedMs : -1;
        Console.WriteLine($"{run.Name}: connecting at {At(ConnectingMark)} ms, host answered {At(AnsweredMark)} ms, snapshot received {At(ReceivedMark)} ms, lockstep started {At(StartedMark)} ms (after its start)");
        Console.WriteLine($"{run.Name}: snapshot transfer {At(ReceivedMark) - At(AnsweredMark)} ms, host answered to lockstep {At(StartedMark) - At(AnsweredMark)} ms");
        Check(At(StartedMark) >= 0, $"{run.Name} joined the lockstep");
    }
    Check(host.Seen.ContainsKey(StartedMark), "the host started the lockstep");
    Check(runs.All(run => run.ExitCode == 0), $"every engine exited 0 ({string.Join(", ", runs.Select(run => $"{run.Name}={run.ExitCode?.ToString() ?? "running"}"))})");
    foreach (var run in runs.Where(run => run.Name != "host")) Check(DigestsAgree(Path.Combine(outDir, "host.dig"), Path.Combine(outDir, run.Name + ".dig")), $"{run.Name}: digests agree with the host's");
}
finally
{
    var mine = runs.Select(run => run.ProcessId).ToHashSet();
    foreach (var run in runs) run.Dispose();
    var left = Process.GetProcessesByName("ps2run").Where(process => mine.Contains(process.Id)).Select(process => process.Id).ToList();
    Check(left.Count == 0, $"none of the engines this rig started is left running ({string.Join(", ", mine)})");
}

Console.WriteLine(failures == 0 ? "relay_rig: all checks passed" : $"relay_rig: {failures} check(s) failed");
return failures == 0 ? 0 : 1;

static bool DigestsAgree(string hostFile, string joinerFile)
{
    if (!File.Exists(hostFile) || !File.Exists(joinerFile)) return false;
    var host = File.ReadAllLines(hostFile).ToDictionary(line => line.Split(' ')[0]);
    var common = File.ReadAllLines(joinerFile).Where(line => host.ContainsKey(line.Split(' ')[0])).ToList();
    var differing = common.Where(line => host[line.Split(' ')[0]] != line).ToList();
    Console.WriteLine($"digest checkpoints compared: {common.Count}, differing: {differing.Count}");
    return common.Count > 0 && differing.Count == 0;
}
