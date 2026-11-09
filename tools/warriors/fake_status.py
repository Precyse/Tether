"""A fake Warriors engine for the status channel: writes the status file and the [net] log lines a real engine would, so the launcher's cards can be seen without a game.

usage: python tools/warriors/fake_status.py --status FILE --log FILE [--scenario join|host|crash|end] [--step-ms 700]

FILE is the `status_file` of game/session.ini and the engine log (game/logs/warriors.log). The format is design/STATUS_CONTRACT.md in coop-scratch\\warriors:
one GAME_STATE line, written by a temporary file and a replace. Counts move in real steps of a made-up job; nothing here is a timer-driven fill.
Scenarios: `join` boots, connects, receives the host's game, loads mission 1 and plays, then closes; `host` waits in the lobby, sends the game and plays;
`crash` stops in the middle of loading mission 1 without closing; `end` plays, then the engine reports the end of its session.
"""
import argparse
import os
import time

SNAPSHOT_BYTES = 18_664_652
SNAPSHOT_STEPS = 6
LOAD_ITEMS = 40
LOAD_STEPS = 8


def write_status(path, **pairs):
    text = ";".join(f"{key}={value}" for key, value in pairs.items()) + "\n"
    with open(path + ".tmp", "w", encoding="utf-8") as stream:
        stream.write(text)
    os.replace(path + ".tmp", path)


def log_line(path, line):
    with open(path, "a", encoding="utf-8") as stream:
        stream.write(line + "\n")


def counted(path, wait, total, steps, **pairs):
    for step in range(1, steps + 1):
        write_status(path, done=total * step // steps, total=total, **pairs)
        wait()


def join(path, log, wait):
    write_status(path, stage="booting")
    wait()
    write_status(path, stage="lobby", done=1, total=2, unit="players")
    wait()
    write_status(path, stage="connecting")
    log_line(log, "[net] connecting to host 127.0.0.1:51246 (attempt 1)")
    wait()
    counted(path, wait, SNAPSHOT_BYTES, SNAPSHOT_STEPS, stage="receiving", unit="bytes")
    log_line(log, "[net] lockstep joined at frame 601: you are pad 2 of 2")
    counted(path, wait, LOAD_ITEMS, LOAD_STEPS, stage="loading", mission=1, unit="items")
    write_status(path, stage="playing", mission=1)
    wait()


def host(path, log, wait):
    write_status(path, stage="booting")
    wait()
    for joined in (1, 2):
        write_status(path, stage="lobby", done=joined, total=2, unit="players")
        wait()
    log_line(log, "[net] every player is here: taking a snapshot of the game at the next frame")
    counted(path, wait, SNAPSHOT_BYTES, SNAPSHOT_STEPS, stage="sending", unit="bytes", text="Sam")
    log_line(log, "[net] lockstep starts at frame 601: you are pad 1 of 2")
    write_status(path, stage="playing", mission=1)
    wait()


def crash(path, log, wait):
    write_status(path, stage="booting")
    wait()
    for step in range(1, LOAD_STEPS // 2 + 1):
        write_status(path, stage="loading", mission=1, unit="items", done=LOAD_ITEMS * step // LOAD_STEPS, total=LOAD_ITEMS)
        wait()


def end(path, log, wait):
    write_status(path, stage="playing", mission=1)
    log_line(log, "[net] lockstep starts at frame 601: you are pad 1 of 2")
    wait()
    log_line(log, "[net] session ended (PeerGone): a player stopped answering; this game goes on alone")
    wait()


SCENARIOS = {"join": join, "host": host, "crash": crash, "end": end}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--status", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), default="join")
    parser.add_argument("--step-ms", type=int, default=700)
    args = parser.parse_args()
    scenario = SCENARIOS[args.scenario]
    scenario(args.status, args.log, lambda: time.sleep(args.step_ms / 1000))
    if args.scenario != "crash":
        write_status(args.status, stage="closed")


if __name__ == "__main__":
    main()
