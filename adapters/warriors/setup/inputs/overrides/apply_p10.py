r"""apply_p10.py: makes generated\warriors_p10 from generated\warriors_vu plus every co-op hook: genuine players 3 and 4 (p9\), the co-op front end (ui\), enemy scaling and rules (ai\).

usage: python apply_p10.py [--source DIR] [--target DIR] [--entries FILE] [--reference]   (then: scripts\rebuild_p10.sh native|win)
--source is the recompiled tree (units and the precompiled VU1 programs), --entries the function starts (a hex address first on each line: the decompile export or the setup tool's
ghidra_entries.txt). Without them the paths of this PC's working tree are used; warriors-setup passes both.
Every edit names the exact generated text it replaces and must match the stated number of times, so a regenerated tree that
moved things fails loudly. Three kinds of edit, from the *_edits*.py tables:
  EDITS        (unit file, old text, new text, count): anywhere in one unit.
  FUNC_EDITS   (function address, old text, new text, count or None for one or more): only inside that function (its labels, in whichever units hold them),
               so the same statement in another function is not touched.
  ENTRY_HOOKS  (function address, statement): put after the function's label; "if (...) return;" statements end the function when the hook handled it.
Only changed files are written, so a rebuild recompiles only those. Generated code is game data: the output stays local.
"""
import csv
import importlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True   # the edit tables are imported from this folder; warriors-setup hashes the folder to know whether the hooks changed


def option(name, default):
    """The directory or file after `name` on the command line (absolute), else `default`."""
    return os.path.abspath(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


RECOMP = r"C:\Users\ahmal\coop-scratch\warriors\recomp"
SOURCE = option("--source", os.path.join(RECOMP, "generated", "warriors_vu"))
VU_SOURCE = SOURCE if "--source" in sys.argv else os.path.join(RECOMP, "generated", "warriors_perf")   # the 37 precompiled VU1 programs (vu1_*.cpp); with --source they sit in that tree
TARGET = option("--target", os.path.join(RECOMP, "generated", "warriors_p10"))
INDEX = option("--entries", r"C:\Users\ahmal\coop-scratch\warriors\decomp\index.tsv")   # function starts (they bound a function's labels)
os.environ["WARRIORS_SOURCE"], os.environ["WARRIORS_ENTRIES"] = SOURCE, INDEX   # p9_cam_scan reads the same tree and function starts while the edit tables are built
sys.path.insert(0, HERE)
import p9_edits_core as core  # noqa: E402
import p9_edits_hud as hud  # noqa: E402

# Edit tables of the further areas, each its own file and imported when present (each defines any of ENTRY_HOOKS, EDITS, FUNC_EDITS and HEADER, a hook header
# every edited unit includes): textures and object list, ground rings and radars, cameras and split screen, leave and revive.
AREA_MODULES = ["p9_edits_tex", "p9_edits_ring", "p9_edits_cam", "p9_edits_leave", "p9_edits_flow", "ui_edits", "ai_edits"]
SELECTED = os.environ["P9_AREAS"].split(",") if "P9_AREAS" in os.environ else AREA_MODULES   # P9_AREAS=a,b builds only those areas (an agent's tree without the others' unfinished work)
HOOK_DIRS = [os.path.join(HERE, "p9"), os.path.join(HERE, "ui"), os.path.join(HERE, "ai")]   # p9 hooks, the co-op front end (ui_*), enemy scaling and rules (ai_*)
sys.path.extend(HOOK_DIRS[1:])
areas = [importlib.import_module(name) for name in SELECTED if name and any(os.path.exists(os.path.join(d, name + ".py")) for d in [HERE] + HOOK_DIRS)]

SHARED_HOOK_FILES = ["warriors_display.hpp", "warriors_display.cpp"]

INCLUDE = '#include "units.hpp"\n'
HOOK_INCLUDE = INCLUDE + '#include "p9_hooks.hpp"\n#include "p9_hud.hpp"\n' + "".join('#include "%s"\n' % a.HEADER for a in areas if hasattr(a, "HEADER"))
LABEL = re.compile(r"^L_([0-9A-F]{8}):\n", re.M)

ENTRY_HOOKS = core.ENTRY_HOOKS + hud.ENTRY_HOOKS + [h for a in areas for h in getattr(a, "ENTRY_HOOKS", [])]
EDITS = core.EDITS + [e for a in areas for e in getattr(a, "EDITS", [])]
FUNC_EDITS = hud.FUNC_EDITS + [e for a in areas for e in getattr(a, "FUNC_EDITS", [])]


def read(path):
    with open(path, encoding="utf8", newline="") as f:
        return f.read()


def write_if_changed(path, text):
    if os.path.exists(path) and read(path) == text:
        return False
    with open(path, "w", encoding="utf8", newline="") as f:
        f.write(text)
    return True


def function_starts():
    with open(INDEX) as f:
        return sorted(int(row[0], 16) for row in csv.reader(f, delimiter="\t") if row[0] != "addr")


def function_end(starts, address):
    return next((s for s in starts if s > address), address + 0x1000)


def regions(text, address, end):
    """Character ranges of `text` whose labels fall in [address, end): from the first such label to the next label outside, repeated."""
    found, begin = [], None
    for match in LABEL.finditer(text):
        inside = address <= int(match.group(1), 16) < end
        if inside and begin is None:
            begin = match.start()
        elif not inside and begin is not None:
            found.append((begin, match.start()))
            begin = None
    if begin is not None:
        found.append((begin, len(text)))
    return found


def edit_function(texts, edited, starts, address, old, new, expected):
    end = function_end(starts, address)
    total = 0
    for unit in sorted(texts):
        while True:
            spans = [(b, e) for b, e in regions(texts[unit], address, end) if old in texts[unit][b:e]]
            if not spans:
                break
            begin, stop = spans[0]
            body = texts[unit][begin:stop]
            total += body.count(old)
            texts[unit] = texts[unit][:begin] + body.replace(old, new) + texts[unit][stop:]
            edited.add(unit)
            if new.find(old) >= 0:   # the replacement contains the old text: one pass per region
                break
    if (expected is None and total == 0) or (expected is not None and total != expected):
        sys.exit("function 0x%08X: expected %s matches for %r, found %d" % (address, expected if expected is not None else ">= 1", old[:70], total))


def write_reference(texts):
    os.makedirs(TARGET, exist_ok=True)
    changed = sum(write_if_changed(os.path.join(TARGET, name), text) for name, text in texts.items())
    print("%s ready: the unedited tree, %d files changed" % (TARGET, changed))


def main():
    global TARGET
    reference = "--reference" in sys.argv
    if reference and "--target" not in sys.argv:
        TARGET = os.path.join(RECOMP, "generated", "warriors_p10ref")
    os.makedirs(TARGET, exist_ok=True)
    texts = {name: read(os.path.join(SOURCE, name)) for name in os.listdir(SOURCE)}
    texts.update({name: read(os.path.join(VU_SOURCE, name)) for name in os.listdir(VU_SOURCE) if name.startswith("vu1_")})
    if reference:
        return write_reference(texts)
    edited = set()
    for unit, old, new, expected in EDITS:
        found = texts[unit].count(old)
        if found != expected:
            sys.exit("%s: expected %d matches for %r, found %d" % (unit, expected, old[:70], found))
        texts[unit] = texts[unit].replace(old, new)
        edited.add(unit)
    starts = function_starts()
    for address, old, new, expected in FUNC_EDITS:
        edit_function(texts, edited, starts, address, old, new, expected)
    locations = {}
    for name, text in texts.items():
        for match in LABEL.finditer(text):
            locations.setdefault(int(match.group(1), 16), name)
    for address, statement in ENTRY_HOOKS:
        unit = locations.get(address)
        if unit is None:
            sys.exit("no unit holds function 0x%08X" % address)
        label = "L_%08X:\n" % address
        if texts[unit].count(label) != 1:
            sys.exit("%s: label %s found %d times" % (unit, label.strip(), texts[unit].count(label)))
        texts[unit] = texts[unit].replace(label, label + "    " + statement + "\n")
        edited.add(unit)
    for unit in edited:
        if texts[unit].count(INCLUDE) != 1:
            sys.exit("%s: expected one include of units.hpp" % unit)
        texts[unit] = texts[unit].replace(INCLUDE, HOOK_INCLUDE)
    changed = sum(write_if_changed(os.path.join(TARGET, name), text) for name, text in texts.items())
    hooks = [os.path.join(d, n) for d in HOOK_DIRS for n in sorted(os.listdir(d)) if n.endswith((".cpp", ".hpp"))] + [os.path.join(HERE, n) for n in SHARED_HOOK_FILES]
    for path in hooks:
        changed += write_if_changed(os.path.join(TARGET, os.path.basename(path)), read(path))
    print("%s ready: %d unit edits, %d function edits, %d entry hooks, %d hook files, %d files changed"
          % (TARGET, len(EDITS), len(FUNC_EDITS), len(ENTRY_HOOKS), len(hooks), changed))


if __name__ == "__main__":
    main()
