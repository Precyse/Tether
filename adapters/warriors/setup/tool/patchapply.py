"""Applies a git-format patch (modified and new files, text only) to a source tree, exactly or not at all. Stands in for `git apply`."""
import os
import re

from util import SetupError

HUNK = re.compile(rb"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


def split_files(patch):
    """[(path, is_new, [hunk lines...])] from a patch given as bytes."""
    files, current = [], None
    for line in patch.split(b"\n"):
        if line.startswith(b"diff --git "):
            current = {"path": line.split(b" b/", 1)[1].decode(), "new": False, "lines": [], "in_hunk": False}
            files.append(current)
        elif current is None:
            continue
        elif line.startswith(b"new file mode"):
            current["new"] = True
        elif line.startswith((b"deleted file", b"rename ", b"Binary files", b"GIT binary")):
            raise SetupError("The patch contains an operation this tool does not do (%s)." % line[:40].decode())
        elif line.startswith(b"@@"):
            current["in_hunk"] = True
            current["lines"].append(line)
        elif current["in_hunk"]:
            current["lines"].append(line)
    return files


def apply_hunks(path, original, lines):
    """Returns the new text of `original` (a list of lines) after the hunks in `lines`; every context and removed line must match in place."""
    out, position, index = [], 0, 0
    while index < len(lines):
        header = HUNK.match(lines[index])
        if header is None:
            if lines[index] == b"" and index == len(lines) - 1:
                break
            raise SetupError("%s: the patch is damaged near %r." % (path, lines[index][:40]))
        start = int(header.group(1)) - (1 if int(header.group(2) or 1) else 0)
        wanted_old, wanted_new = int(header.group(2) or 1), int(header.group(4) or 1)
        out.extend(original[position:start])
        position, index, old, new = start, index + 1, 0, 0
        while index < len(lines) and (old < wanted_old or new < wanted_new):
            kind, text = lines[index][:1], lines[index][1:]
            if kind in (b" ", b"-"):
                if position >= len(original) or original[position] != text:
                    raise SetupError("%s does not match what the patch expects at line %d. The source download is not the pinned version." % (path, position + 1))
                position += 1
                old += 1
                if kind == b" ":
                    out.append(text)
                    new += 1
            elif kind == b"+":
                out.append(text)
                new += 1
            else:
                raise SetupError("%s: the patch is damaged near %r." % (path, lines[index][:40]))
            index += 1
    out.extend(original[position:])
    return out


def apply_patch(patch_path, root):
    with open(patch_path, "rb") as f:
        files = split_files(f.read())
    for entry in files:
        target = os.path.join(root, entry["path"])
        if entry["new"]:
            if os.path.exists(target):
                raise SetupError("%s already exists but the patch creates it." % entry["path"])
            original = []
        else:
            if not os.path.exists(target):
                raise SetupError("%s is missing from the source download." % entry["path"])
            with open(target, "rb") as f:
                data = f.read()
            original = data.split(b"\n")
            if original and original[-1] == b"":
                original.pop()
        result = apply_hunks(entry["path"], original, entry["lines"])
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(b"\n".join(result) + b"\n")
    return len(files)
