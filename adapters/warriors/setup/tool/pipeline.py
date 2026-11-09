"""The build steps of warriors-setup: source, recompilers, recompile, VU1 programs, overrides, game build. Each takes paths and returns facts for the state file."""
import hashlib
import os
import shutil
import zipfile

import fetch
import patchapply
from touch import touch_changed
from util import SetupError, run, tree_hash

BUILD_TIMEOUT_SECONDS = 3600
RECOMPILE_TIMEOUT_SECONDS = 600
CMAKE_BUILD_TYPE = "Release"
VU_IMAGE_SIZE = 16384


class Tools:
    """Paths and environment of the unpacked toolchain."""

    def __init__(self, dirs, msvc_env):
        self.llvm_bin = os.path.join(dirs["llvm"], "bin")
        self.cmake = os.path.join(dirs["cmake"], "cmake-4.2.3-windows-x86_64", "bin", "cmake.exe")
        self.ninja_dir = dirs["ninja"]
        self.sdl2_cmake = os.path.join(dirs["sdl2"], "SDL2-2.32.10", "cmake")
        self.sdl2_dll = os.path.join(dirs["sdl2"], "SDL2-2.32.10", "lib", "x64", "SDL2.dll")
        self.wgpu_dir = dirs["wgpu"]
        self.env = dict(msvc_env)
        self.env["PATH"] = os.pathsep.join([self.llvm_bin, os.path.dirname(self.cmake), self.ninja_dir, msvc_env.get("PATH", "")])

    def check(self):
        for path in (self.cmake, os.path.join(self.ninja_dir, "ninja.exe"), os.path.join(self.llvm_bin, "clang++.exe"),
                     os.path.join(self.llvm_bin, "lld-link.exe"), self.sdl2_dll, os.path.join(self.wgpu_dir, "lib", "wgpu_native.dll.lib")):
            if not os.path.exists(path):
                raise SetupError("A downloaded tool is incomplete (%s is missing). Delete the cache folder's 'tools' directory and run again." % path)


def prepare_source(pin, patch_path, cache, work):
    """work/src = the pinned ps2-web-recomp export with our patch applied; both the export and the result are checked by content."""
    target = os.path.join(work, "src")
    if os.path.isdir(target) and tree_hash(target) == pin["patched_tree_sha256"]:
        return target
    archive = fetch.download(pin["url"], os.path.join(cache, "downloads", "ps2-web-recomp-" + pin["version"][:7]), None, pin["file"])
    staging = target + ".new"
    shutil.rmtree(staging, ignore_errors=True)
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            if not entry.is_dir():
                relative = entry.filename.split("/", 1)[1]
                destination = os.path.join(staging, *relative.split("/"))
                os.makedirs(os.path.dirname(destination), exist_ok=True)
                with open(destination, "wb") as out:
                    out.write(z.read(entry))
    if tree_hash(staging) != pin["tree_sha256"]:
        shutil.rmtree(staging, ignore_errors=True)
        raise SetupError("The ps2-web-recomp source downloaded from GitHub is not the pinned commit (content differs). Run again; if it repeats, "
                         "the project may have been rewritten and this release of warriors-setup needs updating.")
    patchapply.apply_patch(patch_path, staging)
    if tree_hash(staging) != pin["patched_tree_sha256"]:
        shutil.rmtree(staging, ignore_errors=True)
        raise SetupError("Applying our runtime patch gave an unexpected result. This is a bug in warriors-setup; nothing was built.")
    shutil.rmtree(target, ignore_errors=True)
    os.replace(staging, target)
    return target


def _forget_moved_build(build):
    """A build folder that was moved (the launcher renamed its data folder) keeps its old place in the CMake cache and the ninja files: it is made again."""
    cache = os.path.join(build, "CMakeCache.txt")
    if not os.path.exists(cache):
        return
    with open(cache, encoding="utf8", errors="replace") as f:
        made_in = next((line.split("=", 1)[1].strip() for line in f if line.startswith("CMAKE_CACHEFILE_DIR:")), build)
    if os.path.normcase(os.path.abspath(made_in)) != os.path.normcase(os.path.abspath(build)):
        shutil.rmtree(build)


def _cmake_build(tools, source, build, targets, extra, log):
    configure = [tools.cmake, "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + CMAKE_BUILD_TYPE,
                 "-DCMAKE_MAKE_PROGRAM=" + os.path.join(tools.ninja_dir, "ninja.exe"), "-DCMAKE_CXX_COMPILER=clang++"] + extra
    run(configure, log + ".configure.log", env=tools.env, timeout=BUILD_TIMEOUT_SECONDS)
    run([tools.cmake, "--build", build, "--target"] + targets, log + ".build.log", env=tools.env, timeout=BUILD_TIMEOUT_SECONDS)


def build_recompilers(tools, source, build, logs):
    _forget_moved_build(build)
    _cmake_build(tools, source, build, ["eerecomp", "vurecomp"], ["-DCMAKE_C_COMPILER=clang"], os.path.join(logs, "tools"))
    return {name: os.path.join(build, name + ".exe") for name in ("eerecomp", "vurecomp")}


def recompile(eerecomp, elf_path, entries_path, out_dir, logs):
    shutil.rmtree(out_dir, ignore_errors=True)
    run([eerecomp, elf_path, "--entries", entries_path, "--out", out_dir], os.path.join(logs, "recompile.log"), timeout=RECOMPILE_TIMEOUT_SECONDS)


def rebuild_vu_images(elf, recipe, out_dir):
    """Writes each recorded VU1 program image, assembled from pieces of the player's own executable and checked against the recipe's SHA-256."""
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir)
    for name, entry in recipe.items():
        image = bytearray(VU_IMAGE_SIZE)
        for vu_address, elf_offset, length in entry["pieces"]:
            image[vu_address:vu_address + length] = elf[elf_offset:elf_offset + length]
        if hashlib.sha256(image).hexdigest() != entry["sha256"]:
            raise SetupError("The vector-unit program %s rebuilt from the game executable does not match what was expected. "
                             "The disc's executable is not the one this release of warriors-setup was made for." % name)
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(image)


def precompile_vu(vurecomp, images_dir, code_dir, logs):
    run([vurecomp, images_dir, code_dir], os.path.join(logs, "vu_programs.log"), timeout=RECOMPILE_TIMEOUT_SECONDS)


def apply_overrides(python, script, source, target, entries, logs):
    """The co-op hooks into the recompiled tree: `script` is apply_p10.py of inputs/overrides, `entries` the function starts it needs to bound each function."""
    run([python, "-I", script, "--source", source, "--target", target, "--entries", entries], os.path.join(logs, "overrides.log"), timeout=RECOMPILE_TIMEOUT_SECONDS)


def build_game(tools, source, game_tree, build, logs):
    """Builds the window version of ps2run from the patched source and the game tree. Returns the fingerprint the build compiled in."""
    _forget_moved_build(build)
    touched, total = touch_changed(build, [game_tree, os.path.join(source, "runtime"), os.path.join(source, "host"), os.path.join(source, "cmake")])
    extra = ["-DPS2_GENERATED=" + game_tree, "-DPS2_WGPU_DIR=" + tools.wgpu_dir, "-DSDL2_DIR=" + tools.sdl2_cmake]
    _cmake_build(tools, source, build, ["ps2run"], extra, os.path.join(logs, "game"))
    return fingerprint_of(build), touched, total


def fingerprint_of(build):
    text = open(os.path.join(build, "fingerprint.cpp")).read()
    return {name: text.split("k%sHash = 0x" % name.capitalize())[1].split("ull")[0] for name in ("game", "runtime")}
