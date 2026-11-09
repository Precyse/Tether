"""Shared helpers of warriors-setup: progress lines, plain-language failures, hashing, running a program with a log."""
import hashlib
import json
import os
import subprocess
import sys
import time

CHUNK = 1 << 20
KILL_GRACE_SECONDS = 5


class SetupError(Exception):
    """A problem the player can act on; the message says what happened and what to do."""


def say(text):
    print(text, flush=True)


class Step:
    """`with Step("2/9", "title"):` prints the title, then how long it took (or that it was skipped)."""

    def __init__(self, number, title):
        self.label = "[%s] %s" % (number, title)
        self.skipped = False
        self.seconds = 0.0

    def __enter__(self):
        say(self.label)
        self.started = time.monotonic()
        return self

    def skip(self, why):
        self.skipped = True
        say("      up to date (%s)" % why)

    def __exit__(self, kind, value, trace):
        self.seconds = time.monotonic() - self.started
        if kind is None:
            say("      %s in %.1f s" % ("skipped" if self.skipped else "done", self.seconds))


def file_hash(path, algorithm="sha256"):
    digest = hashlib.new(algorithm)
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(CHUNK), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_manifest(root, skip_dirs=()):
    """Sorted '<relative path with />  <sha256 of contents without CR>' lines of every file under root; text files only."""
    lines = []
    for folder, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for name in names:
            path = os.path.join(folder, name)
            with open(path, "rb") as f:
                data = f.read().replace(b"\r", b"")
            lines.append("%s %s" % (os.path.relpath(path, root).replace(os.sep, "/"), hashlib.sha256(data).hexdigest()))
    return sorted(lines)


def tree_hash(root, skip_dirs=()):
    return hashlib.sha256("\n".join(tree_manifest(root, skip_dirs)).encode()).hexdigest()


def read_json(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path, encoding="utf8") as f:
        return json.load(f)


def write_json(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf8") as f:
        json.dump(value, f, indent=1, sort_keys=True)


def run(command, log_path, env=None, cwd=None, timeout=None):
    """Runs a program with its output going to log_path; raises SetupError naming the log when it fails or runs too long."""
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    with open(log_path, "wb") as log:
        log.write(("> %s\n" % " ".join(command)).encode())
        log.flush()
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env, cwd=cwd)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(KILL_GRACE_SECONDS)
            raise SetupError("%s did not finish in %d s and was stopped. Log: %s" % (os.path.basename(command[0]), timeout, log_path))
    if code != 0:
        raise SetupError("%s failed (exit code %d). Log: %s" % (os.path.basename(command[0]), code, log_path))


def free_bytes(path):
    import shutil
    while not os.path.exists(path):
        path = os.path.dirname(path)
    return shutil.disk_usage(path).free
