"""Makes ninja rebuild by content, not by file time (a source put back by a copy that keeps its old time would otherwise leave a stale object linked).

Keeps source_hashes.txt in the build directory (the content hash of every source of the last build) and gives every source whose content differs
a fresh file time. Without that file every source is touched, which means one full rebuild.
"""
import hashlib
import os

MANIFEST = "source_hashes.txt"


def _hashes(roots):
    found = {}
    for root in roots:
        for folder, _, names in os.walk(root):
            for name in names:
                path = os.path.join(folder, name)
                with open(path, "rb") as f:
                    found[path] = hashlib.sha1(f.read()).hexdigest()
    return found


def _read_manifest(path):
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf8") as f:
        return dict(line.rstrip("\n").rsplit(" ", 1) for line in f if line.strip())


def touch_changed(build_dir, roots):
    """Returns (number of sources changed since the last build, number of sources)."""
    os.makedirs(build_dir, exist_ok=True)
    manifest = os.path.join(build_dir, MANIFEST)
    before, now = _read_manifest(manifest), _hashes(roots)
    changed = [path for path, digest in now.items() if before.get(path) != digest]
    for path in changed:
        os.utime(path, None)
    with open(manifest, "w", encoding="utf8") as f:
        f.writelines("%s %s\n" % item for item in sorted(now.items()))
    return len(changed), len(now)
