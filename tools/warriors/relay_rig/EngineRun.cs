using System.Diagnostics;
using Tether;

namespace RelayRig;

/// <summary>One engine process the rig started: its stderr goes to a log file, a tail of that log timestamps the netplay lines, and only this process is ever killed.</summary>
internal sealed class EngineRun : IDisposable
{
    private readonly Process _process;
    private readonly EngineLogTail _tail;
    private readonly Stopwatch _clock;
    private readonly StreamWriter _log;

    public EngineRun(string name, string engine, IEnumerable<string> args, string logPath, Stopwatch clock)
    {
        Name = name;
        LogPath = logPath;
        _clock = clock;
        _log = new StreamWriter(new FileStream(logPath, FileMode.Create, FileAccess.Write, FileShare.ReadWrite)) { AutoFlush = true };
        _tail = new EngineLogTail(logPath);
        var start = new ProcessStartInfo(engine) { UseShellExecute = false, RedirectStandardError = true, RedirectStandardOutput = true, CreateNoWindow = true };
        foreach (var argument in args) start.ArgumentList.Add(argument);
        _process = Process.Start(start) ?? throw new InvalidOperationException($"{name} did not start");
        _process.ErrorDataReceived += (_, e) => WriteLine(e.Data);
        _process.OutputDataReceived += (_, _) => { };
        _process.BeginErrorReadLine();
        _process.BeginOutputReadLine();
        StartedMs = clock.ElapsedMilliseconds;
    }

    public string Name { get; }

    public string LogPath { get; }

    public int ProcessId => _process.Id;

    public long StartedMs { get; }

    public bool Running => !_process.HasExited;

    public int? ExitCode => _process.HasExited ? _process.ExitCode : null;

    /// <summary>When (rig clock, ms) each marker line first appeared in the log.</summary>
    public Dictionary<string, long> Seen { get; } = new();

    /// <summary>Reads the lines written since the last call and notes the first time of each marker text.</summary>
    public void Poll(IEnumerable<string> markers)
    {
        foreach (var line in _tail.ReadNewLines())
            foreach (var marker in markers)
                if (line.Contains(marker, StringComparison.Ordinal)) Seen.TryAdd(marker, _clock.ElapsedMilliseconds);
    }

    public void Kill()
    {
        if (!_process.HasExited) _process.Kill(entireProcessTree: true);
    }

    public void Dispose()
    {
        Kill();
        _process.WaitForExit();
        _process.Dispose();
        lock (_log) _log.Dispose();
    }

    private void WriteLine(string? line)
    {
        if (line is null) return;
        lock (_log)
        {
            try
            {
                _log.WriteLine(line);
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }
}
