# CODEMAP: `warriors\package` (warriors-setup)

| File | Owns | Key functions |
| --- | --- | --- |
| `warriors-setup.cmd`, `warriors-setup.ps1` | entry point: fetches the pinned embeddable Python (SHA-256), runs `tool\warriors_setup.py` with the same arguments | |
| `tool\warriors_setup.py` | the 9 steps, state file (`work\state.json`, step keys = content hashes), timings, error reporting | `Setup.step_*`, `install_stub`, `main` |
| `tool\iso.py` | read-only ISO 9660 root reader, the known disc and file hashes, the refusal messages | `verify`, `full_sha1`, `Disc` |
| `tool\fetch.py` | resumable download with SHA-256 check, retries | `download` |
| `tool\toolchain.py` | pinned tool download/unpack (LLVM subset from the 4 GB zstd tar), VS Build Tools detection and `vcvars64` environment | `ensure`, `unpack_llvm`, `msvc_environment` |
| `tool\pipeline.py` | source prepare (tree-hash checked), cmake/ninja builds, recompile, VU1 images from the executable, overrides, game build, fingerprint read-back | `prepare_source`, `build_recompilers`, `rebuild_vu_images`, `build_game`, `fingerprint_of` |
| `tool\patchapply.py` | strict git-format patch applier (modified and new text files) | `apply_patch` |
| `tool\touch.py` | content-based file times so a build never links a stale object | `touch_changed` |
| `tool\util.py` | `SetupError`, `Step`, hashes, `run` (logged process, timeout, kill) | |
| `tool\make_vu_recipe.py` | dev tool: makes `inputs\recipe\vu_images.json` from recorded VU1 images and the executable | |
| `inputs\pins.json` | every download: URL, version, SHA-256 (source: tree hashes) | |
| `inputs\patches\clone-vs-e026b97.patch` | our runtime changes on ps2-web-recomp `e026b97` | |
| `inputs\recipe\` | `ghidra_entries.txt` (function starts), `vu_images.json` (the 37 recorded VU1 programs as executable pieces), `lockstep.patches` | |
| `inputs\overrides\` | `apply_p10.py` (`--source`, `--target`, `--entries`), the `p9_edits_*.py` tables, `names_ours.tsv` (our name for every function and structure the hooks mention, by address), hook sources `p9\`, `ui\`, `ai\`, `warriors_display.*` (snapshot of `recomp\games\warriors\overrides`) | |
| `inputs\stub\warriors_launcher.cpp` | `Warriors.exe` | |
| `proof\` | boot, digest and reference-build scripts and logs; `home2\` is a full local install (game data, never ship) | |
| `REDISTRIBUTION.md`, `LAUNCHER_DESIGN.md`, `README.md` | what may ship; launcher hosting design; usage and measurements | |

Where to look for: a step failing, `HOME\work\logs\<step>.log` (named in the error); why a step reran, `work\state.json` keys; a changed fingerprint, `game\build.json`.
