# warriors-setup: your own The Warriors (USA) disc, turned into a native Warriors.exe on your PC

Local state of this folder: 2026-11-08. Nothing here was committed, uploaded or shipped. Read `REDISTRIBUTION.md` for what may leave this PC and
`LAUNCHER_DESIGN.md` for how the co-op launcher hosts the result.

## Use

```
warriors-setup.cmd --iso "D:\Games\Warriors, The (USA) (En,Fr,De,Es,It).iso"
```

Options: `--home DIR` (everything the tool makes, default `%LOCALAPPDATA%\WarriorsRecomp`), `--cache DIR` (downloaded tools, default `HOME\cache`),
`--out DIR` (where `Warriors.exe` goes, default `HOME\game`), `--fullscreen`, `--force` (redo every step). The ISO is only read.

Needs: Windows 10/11 x64, PowerShell 5.1 (built in), internet on the first run, 3 GB free while building, about 1 GB of RAM for one step (unpacking the
LLVM download), and **Visual Studio Build Tools with "Desktop development with C++"** (the compiler needs Microsoft's C++ headers and the Windows SDK;
they cannot be redistributed). If they are missing the tool stops at step 2 and prints the `winget` command that installs them.

Then start `HOME\game\Warriors.exe` (keyboard is pad 1 in the window; XInput pads work). `warriors.ini` next to it holds the disc path, window mode and extra
engine arguments; anything on the `Warriors.exe` command line is passed to the engine (for example `--net-join host:port`). The engine's output is in
`game\logs\warriors.log` (the previous run in `warriors.previous.log`); memory card in `game\saves\card.ps2`.

## What it does (9 steps, each prints what it is doing and how long it took)

| Step | What |
| --- | --- |
| 0 (`warriors-setup.ps1`) | fetches Python 3.14.3 embeddable into the cache (SHA-256 pinned); no Python or bash needed on the PC |
| 1 | opens the ISO (ISO 9660 root only), requires `SLUS_212.15` (SHA-1 `e9cb2cc4...`, 4,903,976 bytes), `WARRIORS.DIR` (SHA-1 `0170cf19...`), `WARRIORS.WAD` size and `SYSTEM.CNF` size; refuses anything else with a plain message. Hashes the whole image once and says whether it is the known dump (SHA-1 `3b68f15c...`); a re-padded copy with identical game files is accepted |
| 2 | free disk space, finds VS Build Tools through `vswhere`, captures the `vcvars64` environment |
| 3 | downloads LLVM/clang 23.1.5, CMake 4.2.3, Ninja 1.12.1, SDL2 2.32.10, wgpu-native 29.0.1.1: each into its own directory, resumable (`.part` + HTTP Range), SHA-256 checked before use, retried 3 times, 4xx not retried. Only the compiler, linker, archiver and clang headers are kept from LLVM (391 MB of the 4 GB) |
| 4 | downloads the pinned ps2-web-recomp commit `e026b97` from GitHub (the zip is not byte stable, so it is checked by a **tree hash** of its contents), applies `clone-vs-e026b97.patch` with a built-in strict patcher, checks the patched tree hash |
| 5 | builds `eerecomp` and `vurecomp` |
| 6 | extracts `SLUS_212.15` to `work\disc`, runs `eerecomp --entries ghidra_entries.txt`, rebuilds the 37 recorded VU1 program images from pieces of that executable (checked by SHA-256 each) and runs `vurecomp` |
| 7 | `apply_p10.py` (given the recompiled tree and `ghidra_entries.txt`): 15 unit edits, 251 function edits and 126 entry hooks in the generated units plus 55 hook files (genuine players 3 and 4, the co-op front end, enemy scaling); each edit must match the stated number of times |
| 8 | builds the window `ps2run.exe` (ninja, Release, `-O3`), after giving every source whose content changed a fresh file time (no stale object, the PHASE7 bug) |
| 9 | writes `Warriors.exe` (a small launcher compiled from `inputs\stub\warriors_launcher.cpp`), `engine\ps2run.exe` + `SDL2.dll` + `wgpu_native.dll`, `lockstep.patches`, `warriors.ini`, `build.json` (fingerprint, hashes including `engine_sha256` of the installed engine, toolchain versions) |

Second run with nothing changed: every step compares content hashes (disc files, recipe, overrides, generated trees, toolchain versions) and skips.
Error paths tested by hand: missing file, not an ISO, ISO without `SLUS_212.15`, wrong-size `SLUS_212.15`, download hash mismatch (file deleted, message),
unreachable URL, resume from a partial download. Compiler failures print the program, exit code and the log path (`work\logs`). **Not tested:** the
low-disk message, the missing-VS message (this PC has VS), a PC with a different MSVC version, any non-NTSC-U disc beyond the synthetic ones above.

## Measured on this PC (16 cores, RTX 5080, 2026-11-07)

Clean run: new empty `--home`, empty cache, `proof\clean_run.log` (113 s wall clock):

| Step | s |
| --- | --- |
| 1 disc check (hash of 4.3 GB) | 2.7 |
| 2 PC check | 1.8 |
| 3 fetch tools (about 330 MB of downloads, unpack LLVM) | 23.1 |
| 4 source + patch | 2.4 |
| 5 recompiler tools | 6.7 |
| 6 recompile + VU1 programs | 2.2 |
| 7 overrides | 1.8 |
| 8 game build (376 sources) | 71.5 |
| 9 install | 0.8 |

Plus about 5 s for step 0 and process start. On a PC with fewer cores step 8 is the one that grows (it is parallel); the download time depends on the line.
Second run: 2.2 s. After changing one override source: 4 s (1 of 376 sources rebuilt; the fingerprint changed and went back when the edit was reverted).

Disk after a clean run: cache 635 MB (llvm 391, cmake 144, wgpu 66, sdl2 24, python and the rest), work 377 MB (generated code 166, build 195, source 13),
game 62 MB: about 1.1 GB. The LLVM download (264 MB) is deleted after it is unpacked.

## Proof that it is the same game as the `recomp\` build

* **Boot:** `proof\home2\game\Warriors.exe` started with no arguments, window captured with PrintWindow (no OS input) at 20 s and 45 s
  (`proof\boot_clean\t20.png`, `t45.png`: Rockstar intro, then the title card "The Warriors" on the subway platform), 58.7 to 59.3 fps on the GPU path,
  hard-killed afterwards, no leftover process. These frames are game imagery: local only.
* **Same generated code (2026-11-08):** a clean run (`work\home37`) makes a game tree of 282 files, byte-identical to `recomp\generated\warriors_perf` (the co-op `warriors_p5` tree plus the 37 recorded VU1
  programs); the 37 images rebuilt from the executable equal `recomp\games\warriors\vu_rec` byte for byte and match `vu_images.json`'s SHA-256 each.
* **Same fingerprint:** game `df5a7c8b54b3c8e0`, runtime `18e437d22edc688e` in two builds: warriors-setup (`work\home37\game\build.json`) and `recomp\build\win-perfvu`. `lockstep.patches` has the same
  SHA-256 (`ad813652...`) as `recomp\games\warriors\lockstep.patches`. The runtime half comes from `inputs\patches\clone-vs-e026b97.patch`, **SHA-256 `db848d6e6eacf7d74f987d3084cc3ba4269e7d564b34a09a911995cf22746ff3`**
  (339,226 bytes, the `recomp\patches` copy of 2026-11-07 22:46; patched tree hash `628e46f8...`, the same from the built-in patcher and from `git apply`). Clean run: step 8 compiled 398 of 398 sources in
  165 s at below-normal priority with 4 jobs.
* **Same behaviour (first build, game `16ab34b74bab36eb`, runtime `6f77a002e4bfd1af`, patch `ca41ce21...`):** headless, deterministic, `--patches lockstep.patches`, 2400 frames from boot, state digests at 600/1200/1800/2400 (`proof\digest_compare.sh`):
  the EE, scratchpad and IOP hashes and the cycle counts are identical in all three exes. The `.machine` hash is not (it also differs between two runs of the
  same exe, address randomisation, as PHASE7 notes).

## What turned out different from the brief

* `git archive` on this PC gives CRLF files (global `core.autocrlf=true`), so the reference input was made with `git -c core.autocrlf=false archive`. Tree
  hashes ignore CR, so the fingerprints do not care either way.
* `inputs\overrides`, `inputs\patches` and `inputs\recipe\vu_images.json` are snapshots of `recomp`, refreshed on 2026-11-09 to the final p10 build: the runtime patch is `git diff e026b97` of `recomp\ps2-web-recomp` (made with a scratch index, checked by applying it with `tool\patchapply.py` to `git -c core.autocrlf=false archive e026b97` and comparing all 181 files), `pins.json` carries the new patched tree hash, and `inputs\overrides` is `games\warriors\overrides` (`apply_p10.py`, the `p9_edits_*.py`, `p9\`, `ui\`, `ai\`, `warriors_display.*`). A full run into a fresh home built the same game fingerprint as `recomp\build\native-p10` (game `f5f39ea3956fcb0c`, runtime `e2eb81ce0baf96da`). Refresh the same way after any later `recomp` change.

## Known limits and risks

* **Microsoft's C++ tools are a prerequisite.** Not tested on a PC without them. A fully self-contained route (llvm-mingw, or xwin-style SDK download with the
  Microsoft licence prompt) was not tried; it would also change the compiler the lockstep fingerprint does not cover (see below).
* **The fingerprint hashes sources, not the compiler.** `build.json` records LLVM, MSVC and SDK versions; the toolchain is part of the rebuild key but peers are
  not compared on it. VU `ESIN/EATAN/EEXP` call the host libm (PHASE7 open issue 6).
* **The source comes from GitHub** (`snuri00/ps2-web-recomp`, no tags, no releases). If the repo disappears, step 4 fails with the hash message; mirror the MIT
  source (with its LICENSE) next to our release.
* `Warriors.exe` always runs with `lockstep.patches` (fixed 1/30 s game step). Solo feel with it was not judged.
* Only a boot to the intro was run through `Warriors.exe`; no netplay run was made through it (the engine is the same code that PHASE7 tested online).
* LLVM's archive uses a 1 GiB zstd window, so Python needs `window_log_max=30` and about 1 GB of RAM for that step.

## Files

See `CODEMAP.md`. Local only (never ship): `work\`, `proof\home2\` (disc executable copy, generated code, objects, exe), `proof\digest`, `proof\boot_clean`.
