"""Reads the unedited generated code of single functions for p9_edits_cam.py: the function's text (its labels, in whichever units hold them) and the
sites where it reaches an address block through `lui` + offset (`c.r[Y].w[0] - K`), each with the base the register provably holds there."""
import csv
import os
import re

RECOMP = r"C:\Users\ahmal\coop-scratch\warriors\recomp"
SOURCE = os.environ.get("WARRIORS_SOURCE") or os.path.join(RECOMP, "generated", "warriors_vu")   # apply_p10.py sets both from its command line
INDEX = os.environ.get("WARRIORS_ENTRIES") or r"C:\Users\ahmal\coop-scratch\warriors\decomp\index.tsv"

LABEL = re.compile(r"^L_([0-9A-F]{8}):\n", re.M)
LUI = re.compile(r"c\.r\[(\d+)\]\.sd\[0\] = static_cast<std::int32_t>\(0x([0-9A-F]{4})0000u\);")
COPY = re.compile(r"c\.r\[(\d+)\]\.d\[0\] = c\.r\[(\d+)\]\.d\[0\] \+ 0ull;")
WRITE = re.compile(r"(?:set32\(c\.r\[(\d+)\],|c\.r\[(\d+)\]\.s?d\[0\] =(?!=)|(?:ld8|ld16|ldl|ldr|ld64)\(c\.r\[(\d+)\],)")
SITE = re.compile(r"c\.r\[(\d+)\]\.w\[0\] ([-+]) (\d+)u")
TAKEN_BRANCH = re.compile(r"if \([^{}]*\) \{([^{}]*?(?:goto|return)[^{}]*)\}")


def function_starts():
    with open(INDEX) as f:
        return sorted(int(row[0], 16) for row in csv.reader(f, delimiter="\t") if row[0] != "addr")


_units = {}


def unit_texts():
    """The unedited units, read once."""
    if not _units:
        for name in sorted(os.listdir(SOURCE)):
            if name.startswith("unit_"):
                with open(os.path.join(SOURCE, name), encoding="utf8", newline="") as f:
                    _units[name] = f.read()
    return _units


def function_text(address):
    """[(unit file, text of the function's labels)] in the unedited tree."""
    starts = function_starts()
    end = next((s for s in starts if s > address), address + 0x1000)
    found = []
    for name, text in unit_texts().items():
        begin = None
        for match in LABEL.finditer(text):
            inside = address <= int(match.group(1), 16) < end
            if inside and begin is None:
                begin = match.start()
            elif not inside and begin is not None:
                found.append((name, text[begin:match.start()]))
                begin = None
        if begin is not None:
            found.append((name, text[begin:]))
    return found


def sites(text):
    """Every `c.r[Y].w[0] +/- K` of the text with the lui base register Y holds at that point (None when unknown): (exact text, base, signed offset)."""
    text = TAKEN_BRANCH.sub(lambda m: m.group(0)[:m.start(1) - m.start()] + " " * len(m.group(1)) + "}", text)   # what only the taken path of a branch writes does not reach the code after it
    events = []
    for pattern, kind in ((WRITE, "write"), (COPY, "copy"), (LUI, "lui")):   # at one statement's end a plain write comes first, then the lui or copy that sets the base
        events += [(text.find(";", m.start()), kind, m) for m in pattern.finditer(text)]   # a statement reads its registers before it writes one
    events += [(m.start(), "site", m) for m in SITE.finditer(text)]
    events.sort(key=lambda e: e[0])
    held, found = {}, []
    for _, kind, m in events:
        if kind == "lui":
            held[int(m.group(1))] = int(m.group(2), 16) << 16
        elif kind == "copy":
            held[int(m.group(1))] = held.get(int(m.group(2)))
        elif kind == "write":
            register = next(int(g) for g in m.groups() if g is not None)
            held[register] = None
        else:
            register, offset = int(m.group(1)), int(m.group(3)) * (1 if m.group(2) == "+" else -1)
            found.append((m.group(0), held.get(register), offset))
    return found
