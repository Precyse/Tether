"""Reads the root directory of an ISO 9660 disc image and checks it is the retail NTSC-U The Warriors. Read only."""
import hashlib
import os
import struct

from util import CHUNK, SetupError

SECTOR = 2048
PVD_SECTOR = 16
PVD_MAGIC = b"\x01CD001"
PVD_ROOT_RECORD = 156
BOOT_FILE = "SLUS_212.15"
KNOWN_DISC = {
    "size": 4295917568,
    "sha1": "3b68f15cc0c831597467b477ecd9c0944b08c17f",
}
KNOWN_FILES = {
    BOOT_FILE: {"size": 4903976, "sha1": "e9cb2cc49aa046b9e494313dce2f5038ed17b2f4"},
    "WARRIORS.DIR": {"size": 128428, "sha1": "0170cf199ed10947f8d20ecd40a5f4b0bd7877f7"},
    "WARRIORS.WAD": {"size": 1495371776, "sha1": None},
    "SYSTEM.CNF": {"size": 57, "sha1": None},
}


class Disc:
    def __init__(self, path):
        self.path = path
        self.size = os.path.getsize(path)
        self.file = open(path, "rb")
        self.files = self._root_files()

    def close(self):
        self.file.close()

    def _read(self, sector, length):
        self.file.seek(sector * SECTOR)
        return self.file.read(length)

    def _root_files(self):
        pvd = self._read(PVD_SECTOR, SECTOR)
        if pvd[:6] != PVD_MAGIC:
            raise SetupError("%s is not an ISO 9660 disc image (no volume descriptor). Use the .iso of your own copy of The Warriors (USA)." % self.path)
        extent, length = struct.unpack_from("<I", pvd, PVD_ROOT_RECORD + 2)[0], struct.unpack_from("<I", pvd, PVD_ROOT_RECORD + 10)[0]
        listing, files, offset = self._read(extent, length), {}, 0
        while offset < length:
            record_length = listing[offset]
            if record_length == 0:
                offset = (offset // SECTOR + 1) * SECTOR
                continue
            record = listing[offset:offset + record_length]
            offset += record_length
            name = record[33:33 + record[32]].decode("ascii", "replace").split(";")[0]
            if name not in ("\x00", "\x01"):
                files[name] = (struct.unpack_from("<I", record, 2)[0], struct.unpack_from("<I", record, 10)[0])
        return files

    def read_file(self, name):
        extent, size = self.files[name]
        return self._read(extent, size)

    def file_sha1(self, name):
        return hashlib.sha1(self.read_file(name)).hexdigest()


def full_sha1(path, progress):
    digest, done = hashlib.sha1(), 0
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(CHUNK * 8), b""):
            digest.update(block)
            done += len(block)
            progress(done)
    return digest.hexdigest()


def verify(path, progress):
    """Returns {'sha1': full image hash, 'elf_sha1': ..., 'dir_sha1': ...} or raises SetupError saying why this is not the right disc."""
    if not os.path.isfile(path):
        raise SetupError("The disc image %s does not exist. Give the path of your The Warriors (USA) .iso." % path)
    disc = Disc(path)
    try:
        if BOOT_FILE not in disc.files:
            others = ", ".join(sorted(n for n in disc.files if n.upper().startswith("SLUS") or n.upper().startswith("SLES")) or ["none"])
            raise SetupError("This is not The Warriors (USA): the disc has no %s (boot files found: %s). Only the NTSC-U release, "
                             "serial SLUS-21215, is supported." % (BOOT_FILE, others))
        for name, expected in KNOWN_FILES.items():
            if name not in disc.files:
                raise SetupError("The disc image is missing %s; it looks like an incomplete or trimmed dump. Use a full dump of the retail disc." % name)
            if disc.files[name][1] != expected["size"]:
                raise SetupError("%s on this disc is %d bytes, the retail disc has %d. This is a different or modified release; "
                                 "only the unmodified NTSC-U retail disc is supported." % (name, disc.files[name][1], expected["size"]))
        elf_sha1, dir_sha1 = disc.file_sha1(BOOT_FILE), disc.file_sha1("WARRIORS.DIR")
        if elf_sha1 != KNOWN_FILES[BOOT_FILE]["sha1"] or dir_sha1 != KNOWN_FILES["WARRIORS.DIR"]["sha1"]:
            raise SetupError("The game executable on this disc is not the retail one (SHA-1 %s). Patched or fan-modified images are not supported; "
                             "use an unmodified dump of the retail NTSC-U disc." % elf_sha1)
        elf = disc.read_file(BOOT_FILE)
    finally:
        disc.close()
    return {"elf_sha1": elf_sha1, "dir_sha1": dir_sha1, "elf": elf}
