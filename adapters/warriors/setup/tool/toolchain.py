"""The pinned build tools: downloaded into the cache (one directory each), hash checked, unpacked into their own empty directory."""
import os
import shutil
import subprocess
import tarfile
import zipfile
from compression import zstd

import fetch
from util import SetupError, file_hash, say

DONE_MARK = ".unpacked"
ZSTD_WINDOW_LOG_MAX = 30  # the LLVM release is packed with a 1 GiB window; the unpack needs about that much memory
LLVM_PREFIX = "clang+llvm-23.1.5-x86_64-pc-windows-msvc/"
LLVM_KEEP_FILES = ("bin/clang.exe", "bin/clang++.exe", "bin/lld-link.exe", "bin/llvm-ar.exe", "bin/llvm-ranlib.exe", "bin/llvm-lib.exe")
LLVM_KEEP_DIRS = ("lib/clang/",)
COPY_BIN = "bin/clang.exe"


def ensure(pin, cache, unpack):
    """Downloads `pin` (url, sha256, file name, version) and unpacks it once; returns the directory holding the unpacked tool."""
    key = "%s-%s" % (pin["id"], pin["version"])
    target = os.path.join(cache, "tools", key)
    if os.path.exists(os.path.join(target, DONE_MARK)) and open(os.path.join(target, DONE_MARK)).read() == pin["sha256"]:
        return target
    say("      %s %s" % (pin["id"], pin["version"]))
    archive = fetch.download(pin["url"], os.path.join(cache, "downloads", key), pin["sha256"], pin["file"])
    shutil.rmtree(target, ignore_errors=True)
    os.makedirs(target)
    unpack(archive, target)
    os.remove(archive)
    with open(os.path.join(target, DONE_MARK), "w") as f:
        f.write(pin["sha256"])
    return target


def unpack_zip(archive, target):
    with zipfile.ZipFile(archive) as z:
        z.extractall(target)


def unpack_llvm(archive, target):
    """Streams the 4 GB compressed LLVM release and keeps only the compiler, linker, archiver and compiler headers/runtime (about 0.5 GB)."""
    with zstd.open(archive, "rb", options={zstd.DecompressionParameter.window_log_max: ZSTD_WINDOW_LOG_MAX}) as raw, tarfile.open(fileobj=raw, mode="r|") as tar:
        for member in tar:
            name = member.name[len(LLVM_PREFIX):] if member.name.startswith(LLVM_PREFIX) else ""
            if not (name in LLVM_KEEP_FILES or name.startswith(LLVM_KEEP_DIRS)) or member.isdir():
                continue
            destination = os.path.join(target, *name.split("/"))
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            if member.isreg():
                with open(destination, "wb") as out:
                    shutil.copyfileobj(tar.extractfile(member), out)
            elif member.islnk() or member.issym():
                source = os.path.join(target, *member.linkname[len(LLVM_PREFIX):].split("/"))
                shutil.copyfile(source, destination)
    for name in LLVM_KEEP_FILES:
        if not os.path.exists(os.path.join(target, *name.split("/"))):
            if name == "bin/clang++.exe":
                shutil.copyfile(os.path.join(target, *COPY_BIN.split("/")), os.path.join(target, "bin", "clang++.exe"))
            elif name in ("bin/llvm-ranlib.exe", "bin/llvm-lib.exe"):
                continue
            else:
                raise SetupError("The LLVM download does not contain %s; the pinned archive is not what was expected." % name)


def msvc_environment(log_path):
    """The environment of a Visual Studio x64 build prompt (vcvars64) as a dict, or a SetupError telling how to install it."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Microsoft Visual Studio", "Installer", "vswhere.exe")
    missing = ("The C++ build tools from Microsoft are not installed (Visual Studio Build Tools with 'Desktop development with C++', "
               "which includes the Windows SDK). Install them once with:\n"
               "    winget install --id Microsoft.VisualStudio.BuildTools --override \"--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended\"\n"
               "then run warriors-setup again.")
    if not os.path.exists(vswhere):
        raise SetupError(missing)
    found = subprocess.run([vswhere, "-products", "*", "-latest", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                            "-property", "installationPath"], capture_output=True, text=True).stdout.strip()
    if not found:
        raise SetupError(missing)
    vcvars = os.path.join(found, "VC", "Auxiliary", "Build", "vcvars64.bat")
    if not os.path.exists(vcvars):
        raise SetupError(missing)
    output = subprocess.run('cmd /d /c "call "%s" >nul && set"' % vcvars, capture_output=True, text=True, shell=True).stdout
    environment = {("PATH" if name.upper() == "PATH" else name): value for name, value in (line.split("=", 1) for line in output.splitlines() if "=" in line)}
    if "INCLUDE" not in environment or "LIB" not in environment or "WindowsSdkDir" not in environment:
        raise SetupError("The Visual Studio build environment could not be started (see %s). Repair the Build Tools install and retry." % vcvars)
    environment["WARRIORS_VS_PATH"] = found
    return environment
