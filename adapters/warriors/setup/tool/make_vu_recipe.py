"""make_vu_recipe.py <elf> <recorded vu dir> <out json>: dev tool, not part of the player's run.

Each recorded VU1 program image (16 KiB of VU1 code memory) is described as pieces of the game's own executable: (VU1 address, ELF file offset, length).
The recipe holds only those numbers and the SHA-256 of the whole image; the player's setup rebuilds the images from the player's own ELF and checks the hash.
"""
import glob
import hashlib
import json
import os
import sys

IMAGE_SIZE = 16384
MIN_PIECE = 16


def longest_match(elf, image, at):
    """(file offset, length) of the longest stretch of `image` from `at` found in `elf`; None if even MIN_PIECE bytes are absent."""
    best = None
    head = image[at:at + MIN_PIECE]
    start = elf.find(head)
    while start >= 0:
        length = len(head)
        while at + length < IMAGE_SIZE and start + length < len(elf) and image[at + length] == elf[start + length]:
            length += 1
        if best is None or length > best[1]:
            best = (start, length)
        start = elf.find(head, start + 1)
    return best


def pieces_of(elf, image):
    pieces, at = [], 0
    while at < IMAGE_SIZE:
        if image[at:at + MIN_PIECE] == bytes(MIN_PIECE):
            at += MIN_PIECE
            continue
        found = longest_match(elf, image, at)
        if found is None:
            raise SystemExit("bytes at VU1 0x%X are not in the executable" % at)
        pieces.append([at, found[0], found[1]])
        at += found[1]
    return pieces


def rebuild(elf, pieces):
    image = bytearray(IMAGE_SIZE)
    for vu, offset, length in pieces:
        image[vu:vu + length] = elf[offset:offset + length]
    return bytes(image)


def main():
    elf = open(sys.argv[1], "rb").read()
    recipe = {}
    for path in sorted(glob.glob(os.path.join(sys.argv[2], "*.bin"))):
        image = open(path, "rb").read()
        pieces = pieces_of(elf, image)
        if rebuild(elf, pieces) != image:
            raise SystemExit("%s: pieces do not rebuild the image" % path)
        recipe[os.path.basename(path)] = {"sha256": hashlib.sha256(image).hexdigest(), "pieces": pieces}
    with open(sys.argv[3], "w") as out:
        entries = ["%s: %s" % (json.dumps(name), json.dumps(entry, separators=(",", ":"))) for name, entry in recipe.items()]
        out.write("{\n" + ",\n".join(entries) + "\n}\n")
    print("%d programs, %d pieces" % (len(recipe), sum(len(r["pieces"]) for r in recipe.values())))


if __name__ == "__main__":
    main()
