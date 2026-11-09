# What may be redistributed (The Warriors native build)

Rule: the player's PC turns the player's own disc into the program. Nothing from the disc, and nothing generated from it, ever leaves the PC. This is a
working decision for the project owner to confirm, not legal advice.

## Shippable: ours or MIT, no game bytes

| Item | Where | Why it is fine | Checked |
| --- | --- | --- | --- |
| warriors-setup (scripts, tool modules) | `warriors-setup.*`, `tool\` | our code | no game data in it |
| Pins (URLs, versions, SHA-256, tree hashes) | `inputs\pins.json` | metadata | |
| Runtime patch | `inputs\patches\clone-vs-e026b97.patch` | a diff against MIT-licensed ps2-web-recomp `e026b97` (the diff carries upstream context lines, so ship it with a pointer to upstream's LICENSE); our code in new files | |
| Function-boundary list | `inputs\recipe\ghidra_entries.txt` | 13,982 lines, each only a hex address (a list of where functions start, no sizes, names or bytes) | every line matches `^[0-9a-f]+$`; none of 225,818 distinct 16-byte windows of the executable occurs in any file of `inputs\` |
| VU1 image recipe | `inputs\recipe\vu_images.json` | 37 programs (16 distinct images), 303 pieces, each three integers (VU1 address, file offset, length) plus a SHA-256 of the rebuilt image: it says where to look in the player's own executable, it contains none of it | same byte search; the one hit (`GetFileAttribute`, 16 ASCII characters of a Win32 API name in the stub source) is a coincidence of a public API name |
| Fixed-step patch list | `inputs\recipe\lockstep.patches` | one line, an address and a value we chose | |
| Overrides (co-op hooks) | `inputs\overrides\` | our C++ source; addresses and offsets are facts about the executable | see "Open points" 1 and 2 |
| Launcher stub | `inputs\stub\warriors_launcher.cpp` | our code | |
| Design docs, this file | | | |

The setup downloads LLVM (Apache-2.0 with LLVM exception), CMake (BSD-3), Ninja (Apache-2.0), SDL2 (zlib), wgpu-native (MIT/Apache-2.0), Python (PSF) and the
ps2-web-recomp source (MIT) straight from their publishers on the player's PC; we redistribute none of them. If a launcher package ever bundles any, it must carry that
tool's licence text.

## Never shippable

| Item | Why |
| --- | --- |
| The ISO, `SLUS_212.15`, anything under `disc\` | Rockstar's |
| `work\disc\SLUS_212.15` (the extracted executable copy the setup makes) | the same |
| `work\gen\code`, `work\gen\game` (generated C++) | a translation of the executable: derivative |
| the 37 VU1 images (`vu_images` while the setup runs; recorded `recomp\games\warriors\vu_rec`) | game microcode |
| `ps2run.exe`, `Warriors.exe` as built, objects, `.lib` files | contain the translated game. The stub source is ours, but its built copy sits in the same folder and the folder is one unit: never ship it |
| `.state` files, memory cards, frame dumps, screenshots (`proof\boot_clean`), digest files made from a run | machine snapshots and game imagery; digests are hashes only but stay local with the rest |
| `proof\home2\` as a whole | all of the above |

`publish_check.py` (the launcher repo's gate) must refuse: `*.cpp` under any `gen\` folder, `*.state`, `*.iso`, `ps2run.exe`, `SLUS_*`, `*.bin` of 16,384 bytes,
`*.png` under `proof\`. Add a content rule too: no file may contain a 16-byte window of the executable (the check above, run over the release folder).

## GPL

* Nothing from PCSX2 and nothing from Coney (`warriors\src`, GPL-3) is in `tool\`, `inputs\` or the build: no code, no copied text. PCSX2's GS or SPU2 would make
  the runtime GPL-3 and are not used; ps2-web-recomp has its own GS (MIT).
* `ghidra_entries.txt` and the hook sources were not copied from Coney.

## Open points (decide before publishing)

1. **Function names in the hook sources.** `coop_game.hpp` and the other hooks name decompiled functions and structures (`Human_MakePlayer`, `W_GameState`, ...). Those
   names come from `decomp\names.tsv`, which was built from Coney's research notes. Names and addresses are facts about the executable, but a GPL-3 project's
   curated name table is the one place a claim could be made. Options: confirm the names were re-derived by our own analysis, or rename to neutral `fn_00419C68` style
   before release. I did not change anything.
2. **Quoted generated text in `apply_p10.py` and its edit tables.** The edit tables find their place by exact matches of lines of generated C++ (short strings such as
   `set32(c.r[20], c.r[20].w[0] + 4u);`). These are our recompiler's rendering of single instructions, not the game's bytes, and the file is useless without the
   executable, but they are quotes of generated output. A cleaner route is an address-keyed hook list consumed by the recompiler or the setup (insert a call at
   function entry `0xADDR`); the two loop-step edits inside `Pads_Update` would still need an instruction-level hook.
3. **`ghidra_entries.txt` provenance.** It is the address column of the Ghidra export of the analysed executable (names from Coney were applied on top of
   Ghidra's own analysis). Addresses of function starts are facts, but re-exporting from a Ghidra run without name import and diffing the list would settle it.
5. **The 16-byte window check (2026-11-09) finds 23 files.** Every hit is text, never code or data: the game's own script-binding
   names that our hooks call by name (`GetProfileDifficulty`, `HuTeleportNearHuman`, `SetRadarZoomScale`, `StationaryShooter...`), its text
   colour tag `<COLOR B2B2B2FF>`, `GetFileAttribute` (a Win32 name) and plain English (" does not reach "). The names are the game's own
   identifiers, so they are not taken from Coney's table, but they are strings of the executable. Decide: accept identifier and phrase hits
   (the check then skips printable ASCII), or rename them in our sources. Nothing is published until this is decided.
4. **ps2-web-recomp has no tags and its repo may change or vanish**; keep a mirrored copy of `e026b97` with its LICENSE for our releases.
