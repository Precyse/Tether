"""warriors-setup: turns the player's own The Warriors (USA) disc image into a native Warriors.exe on this PC.

usage: warriors_setup.py --iso FILE [--home DIR] [--cache DIR] [--out DIR] [--fullscreen] [--force]

Nothing from the disc or generated from it leaves this PC. A second run with nothing changed does no work (every step compares content hashes).
"""
import argparse
import os
import shutil
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import iso
import pipeline
import toolchain
from util import SetupError, Step, file_hash, free_bytes, read_json, run, say, tree_hash, write_json

INPUTS = os.path.join(os.path.dirname(HERE), "inputs")
REQUIRED_FREE_BYTES = 3 << 30
STUB_SOURCE = os.path.join(INPUTS, "stub", "warriors_launcher.cpp")
STEP_COUNT = 9
GAME_FILES = ("SDL2.dll", "wgpu_native.dll")


def default_home():
    return os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "WarriorsRecomp")


def parse_arguments():
    parser = argparse.ArgumentParser(description="Builds a native The Warriors (PS2) from your own disc image.")
    parser.add_argument("--iso", required=True, help="your The Warriors (USA) .iso (NTSC-U, SLUS-21215); it is only read")
    parser.add_argument("--home", default=default_home(), help="folder for everything this tool makes (default %%LOCALAPPDATA%%\\WarriorsRecomp)")
    parser.add_argument("--cache", help="downloaded build tools (default HOME\\cache)")
    parser.add_argument("--out", help="where Warriors.exe is put (default HOME\\game)")
    parser.add_argument("--fullscreen", action="store_true", help="start the game fullscreen")
    parser.add_argument("--force", action="store_true", help="redo every step even when nothing changed")
    arguments = parser.parse_args()
    arguments.home = os.path.abspath(arguments.home)
    arguments.cache = os.path.abspath(arguments.cache or os.path.join(arguments.home, "cache"))
    arguments.out = os.path.abspath(arguments.out or os.path.join(arguments.home, "game"))
    arguments.iso = os.path.abspath(arguments.iso)
    arguments.work = os.path.join(arguments.home, "work")
    return arguments


class Setup:
    def __init__(self, arguments):
        self.a = arguments
        self.state_path = os.path.join(arguments.work, "state.json")
        self.state = {} if arguments.force else read_json(self.state_path, {})
        self.logs = os.path.join(arguments.work, "logs")
        self.timings = {}
        self.pins = read_json(os.path.join(INPUTS, "pins.json"))
        self.recipe_dir = os.path.join(INPUTS, "recipe")

    def current(self, name, key):
        return self.state.get(name) == key

    def remember(self, name, key):
        self.state[name] = key
        write_json(self.state_path, self.state)

    def timed(self, step):
        self.timings[step.label] = round(step.seconds, 1)

    def run(self):
        os.makedirs(self.a.work, exist_ok=True)
        self.step_disc()
        self.step_preflight()
        self.step_toolchain()
        self.step_source()
        self.step_recompilers()
        self.step_recompile()
        self.step_overrides()
        self.step_build()
        self.step_install()
        write_json(os.path.join(self.a.work, "timings.json"), self.timings)

    def step_disc(self):
        with Step("1/%d" % STEP_COUNT, "Checking the disc image") as step:
            verdict = iso.verify(self.a.iso, lambda done: None)
            stat = os.stat(self.a.iso)
            stamp = [self.a.iso, stat.st_size, stat.st_mtime_ns]
            self.elf = verdict.pop("elf")
            self.disc = verdict
            known = self.state.get("disc_full")
            if known and known["stamp"] == stamp:
                full = known["sha1"]
            else:
                say("      hashing the whole image (%d GB)" % (stat.st_size >> 30))
                full = iso.full_sha1(self.a.iso, lambda done: None)
                self.remember("disc_full", {"stamp": stamp, "sha1": full})
            if full == iso.KNOWN_DISC["sha1"] and stat.st_size == iso.KNOWN_DISC["size"]:
                say("      The Warriors (USA) SLUS-21215, the known retail dump")
            else:
                say("      The Warriors (USA) SLUS-21215: the game files are the retail ones; the image as a whole differs from the reference dump "
                    "(SHA-1 %s), which is fine for a re-pressed or re-padded copy" % full)
            self.disc["full_sha1"] = full
        self.timed(step)

    def step_preflight(self):
        with Step("2/%d" % STEP_COUNT, "Checking this PC") as step:
            free = free_bytes(self.a.home)
            if free < REQUIRED_FREE_BYTES:
                raise SetupError("Not enough free disk space on the drive for %s: %.1f GB free, %d GB needed while building (about 1.1 GB stay afterwards). "
                                 "Free some space or choose another drive with --home." % (self.a.home, free / 2**30, REQUIRED_FREE_BYTES >> 30))
            self.msvc = toolchain.msvc_environment(os.path.join(self.logs, "vcvars.log"))
            say("      Visual Studio build tools found (%s), %.0f GB free" % (self.msvc["WARRIORS_VS_PATH"], free / 2**30))
        self.timed(step)

    def step_toolchain(self):
        with Step("3/%d" % STEP_COUNT, "Fetching the pinned build tools (checked by SHA-256)") as step:
            unpackers = {"llvm": toolchain.unpack_llvm}
            self.dirs = {name: toolchain.ensure(self.pins[name], self.a.cache, unpackers.get(name, toolchain.unpack_zip))
                         for name in ("llvm", "cmake", "ninja", "sdl2", "wgpu")}
            self.tools = pipeline.Tools(self.dirs, self.msvc)
            self.tools.check()
        self.timed(step)

    def step_source(self):
        with Step("4/%d" % STEP_COUNT, "Preparing the runtime source (pinned ps2-web-recomp plus our patch)") as step:
            patch = os.path.join(INPUTS, "patches", "clone-vs-e026b97.patch")
            self.source = pipeline.prepare_source(self.pins["source"], patch, self.a.cache, self.a.work)
            self.source_key = self.pins["source"]["patched_tree_sha256"]
        self.timed(step)

    def toolchain_key(self):
        return [self.source_key, self.pins["llvm"]["sha256"], self.pins["cmake"]["sha256"], self.msvc.get("VCToolsVersion", ""), self.msvc.get("WindowsSDKVersion", "").strip("\\")]

    def step_recompilers(self):
        build = os.path.join(self.a.work, "build", "tools")
        self.recompilers = {name: os.path.join(build, name + ".exe") for name in ("eerecomp", "vurecomp")}
        with Step("5/%d" % STEP_COUNT, "Building the recompiler tools") as step:
            key = self.toolchain_key()
            if self.current("recompilers", key) and all(os.path.exists(p) for p in self.recompilers.values()):
                step.skip("same source and compiler")
            else:
                pipeline.build_recompilers(self.tools, self.source, build, self.logs)
                self.remember("recompilers", key)
        self.timed(step)

    def step_recompile(self):
        self.code_tree = os.path.join(self.a.work, "gen", "code")
        entries = os.path.join(self.recipe_dir, "ghidra_entries.txt")
        recipe_path = os.path.join(self.recipe_dir, "vu_images.json")
        with Step("6/%d" % STEP_COUNT, "Recompiling the game code and the vector-unit programs") as step:
            key = [self.disc["elf_sha1"], file_hash(entries), file_hash(recipe_path), self.toolchain_key()]
            if self.current("code", key) and os.path.isdir(self.code_tree) and tree_hash(self.code_tree) == self.state.get("code_tree"):
                step.skip("same disc executable and recipe")
            else:
                elf_path = os.path.join(self.a.work, "disc", "SLUS_212.15")
                os.makedirs(os.path.dirname(elf_path), exist_ok=True)
                with open(elf_path, "wb") as f:
                    f.write(self.elf)
                pipeline.recompile(self.recompilers["eerecomp"], elf_path, entries, self.code_tree, self.logs)
                images = os.path.join(self.a.work, "gen", "vu_images")
                pipeline.rebuild_vu_images(self.elf, read_json(recipe_path), images)
                pipeline.precompile_vu(self.recompilers["vurecomp"], images, self.code_tree, self.logs)
                shutil.rmtree(images, ignore_errors=True)
                self.state["code_tree"] = tree_hash(self.code_tree)
                self.remember("code", key)
        self.timed(step)

    def step_overrides(self):
        self.game_tree = os.path.join(self.a.work, "gen", "game")
        overrides = os.path.join(INPUTS, "overrides")
        with Step("7/%d" % STEP_COUNT, "Applying the co-op hooks and fixed-step overrides") as step:
            key = [self.state.get("code_tree"), tree_hash(overrides)]
            if self.current("game_tree_key", key) and os.path.isdir(self.game_tree) and tree_hash(self.game_tree) == self.state.get("game_tree"):
                step.skip("same code and overrides")
            else:
                pipeline.apply_overrides(sys.executable, os.path.join(overrides, "apply_p10.py"), self.code_tree, self.game_tree, os.path.join(self.recipe_dir, "ghidra_entries.txt"), self.logs)
                self.state["game_tree"] = tree_hash(self.game_tree)
                self.remember("game_tree_key", key)
        self.timed(step)

    def step_build(self):
        self.build_dir = os.path.join(self.a.work, "build", "game")
        self.engine_exe = os.path.join(self.build_dir, "ps2run.exe")
        with Step("8/%d" % STEP_COUNT, "Building the game (the long step)") as step:
            key = [self.state.get("game_tree"), self.toolchain_key()]
            if self.current("build", key) and os.path.exists(self.engine_exe):
                step.skip("same code, runtime and compiler")
            else:
                fingerprint, touched, total = pipeline.build_game(self.tools, self.source, self.game_tree, self.build_dir, self.logs)
                say("      %d of %d sources needed compiling" % (touched, total))
                self.remember("build", key)
            self.fingerprint = pipeline.fingerprint_of(self.build_dir)
            say("      build fingerprint: game %s, runtime %s" % (self.fingerprint["game"], self.fingerprint["runtime"]))
        self.timed(step)

    def step_install(self):
        with Step("9/%d" % STEP_COUNT, "Writing Warriors.exe and its settings") as step:
            out = self.a.out
            engine_dir = os.path.join(out, "engine")
            os.makedirs(engine_dir, exist_ok=True)
            os.makedirs(os.path.join(out, "saves"), exist_ok=True)
            self.copy_if_changed(self.engine_exe, os.path.join(engine_dir, "ps2run.exe"))
            self.copy_if_changed(self.tools.sdl2_dll, os.path.join(engine_dir, "SDL2.dll"))
            self.copy_if_changed(os.path.join(self.tools.wgpu_dir, "lib", "wgpu_native.dll"), os.path.join(engine_dir, "wgpu_native.dll"))
            patches = os.path.join(out, "lockstep.patches")
            self.copy_if_changed(os.path.join(self.recipe_dir, "lockstep.patches"), patches)
            elf_path = os.path.join(self.a.work, "disc", "SLUS_212.15")
            ini = ("[game]\nengine=engine\\ps2run.exe\nelf=%s\ndisc=%s\nmemcard=%s\npatches=%s\n\n[window]\nfullscreen=%d\n\n[args]\nextra=\n"
                   % (elf_path, self.a.iso, os.path.join(out, "saves", "card.ps2"), patches, int(self.a.fullscreen)))
            self.write_if_changed(os.path.join(out, "warriors.ini"), ini)
            self.install_stub(os.path.join(out, "Warriors.exe"))
            write_json(os.path.join(out, "build.json"), {
                "fingerprint": self.fingerprint, "disc_elf_sha1": self.disc["elf_sha1"], "disc_sha1": self.disc["full_sha1"],
                "source_commit": self.pins["source"]["version"], "patched_source_tree": self.pins["source"]["patched_tree_sha256"],
                "patch_list_sha256": file_hash(patches), "game_tree": self.state["game_tree"],
                "engine_sha256": file_hash(os.path.join(engine_dir, "ps2run.exe")), "llvm": self.pins["llvm"]["version"],
                "msvc": self.msvc.get("VCToolsVersion", ""), "windows_sdk": self.msvc.get("WindowsSDKVersion", "").strip("\\")})
        self.timed(step)
        say("")
        say("Done. Start the game with %s" % os.path.join(out, "Warriors.exe"))

    def install_stub(self, destination):
        stamp = file_hash(STUB_SOURCE)
        if self.current("stub", stamp) and os.path.exists(destination):
            return
        compile_log = os.path.join(self.logs, "stub.log")
        run([os.path.join(self.tools.llvm_bin, "clang++.exe"), "-std=c++20", "-O2", "-municode", "-Xlinker", "/SUBSYSTEM:WINDOWS", "-o", destination, STUB_SOURCE,
             "-luser32"], compile_log, env=self.tools.env)
        self.remember("stub", stamp)

    @staticmethod
    def copy_if_changed(source, destination):
        if not os.path.exists(destination) or file_hash(source) != file_hash(destination):
            shutil.copyfile(source, destination)

    @staticmethod
    def write_if_changed(path, text):
        if not os.path.exists(path) or open(path, encoding="utf8").read() != text:
            with open(path, "w", encoding="utf8") as f:
                f.write(text)


def main():
    arguments = parse_arguments()
    try:
        Setup(arguments).run()
    except SetupError as problem:
        say("")
        say("ERROR: %s" % problem)
        return 1
    except Exception:
        report = os.path.join(arguments.work, "logs", "setup-crash.txt")
        os.makedirs(os.path.dirname(report), exist_ok=True)
        with open(report, "w") as f:
            traceback.print_exc(file=f)
        say("")
        say("ERROR: warriors-setup hit an unexpected problem; details are in %s" % report)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
