# CODEMAP: tools

Repo-level test, release and research tools. Index: `CODEMAP.md`; "where to look for X": `docs/codemap/where.md`.

## Tests and release

| file | owns |
|---|---|
| .github/workflows/release.yml | push to `main` (channel `latest`) or `next` (prerelease): parallel jobs `re0`, `sr2` (x86 build + unit tests), `dd2`, `cd` (x64 build + unit tests; each DLL cached by a source hash, uploaded as an artifact) and `launcher`; then `package`: `publish_check.py`, assemble (DS2 left out; `launcher/app/coop-launcher.exe` is a copy of `tether.exe` for installs made before the rename, `launcher/FormerInstall.cs`), `tools/release_manifest.py` (unchanged components ship the previous zip), update the channel's release in place, manifest last, move the tag |
| README.md | the GitHub front page: what Tether is, supported games and their status, install, playing, repository layout |
| launcher/package/README.txt | player README shipped in the package |
| launcher/update.json | `{ "repo": "Precyse/Tether", "channel": "preview" }` copied next to the exe; a missing file or the `OWNER/REPO` placeholder disables updating (the release build writes the real repo and channel into the package) |
| tools/launcher_tests/ | C# console unit tests of the launcher's pure rules and file/process rules (mod install, updater, save sync, UI snapshot, release manifest, component update); `dotnet run --project tools/launcher_tests`, subsets with `LAUNCHER_TESTS_ONLY=web|mod|files`; tests that touch settings or logs set `COOP_LAUNCHER_DATA` |
| tools/warriors/fake_status.py | a fake Warriors engine for the status channel: writes the status file and `[net]` lines of a join, a host, a crash or a session end in real steps, to see the launcher's cards without a game (spec `coop-scratch\warriors\design\STATUS_CONTRACT.md`) |
| tools/warriors/relay_rig/ | The Warriors over the launcher's relay on one PC: a host engine and N joiner engines (the existing recomp build, run only) connected through `UdpRelayLink` and the profile's peer budget, with an optional one-way delay; reports the join time and whether the digests agree; only its own processes are stopped. `--build` instead runs the launcher's build step against the real setup tool (`COOP_LAUNCHER_DATA` names the data folder, `--cancel-after N` cancels). `dotnet run -c Release --project tools/warriors/relay_rig -- --out <dir> [--frames N] [--joiners N] [--delay ms]` |
| tools/save_sync_test.py | two local launchers: the save transfer and the reset |
| tools/diagnostics_test.py | guest log lines and a crash dump reach the host |
| tools/build_check_test.py | mismatched builds, game builds and mod files refuse; matching ones carry on |
| tools/peer_link_test.py | `--gap S` (held vs PEER_DOWN) and `--flood N` (peer budget) against a host launcher with `fake_adapter.py` |
| tools/fake_adapter.py, tools/echo_peer.py, tools/spoof_peer.py | stand-in adapter; echo of 0x0100/0x0101 and the puppet frames; a wrong slot byte over local transport |
| tools/publish_check.py | gates pushes and releases (no game files, keys or stray binaries) |
| tools/release_manifest.py, tools/release_manifest_test.py | the package into `Tether.zip`, `launcher.zip`, `mod-<id>.zip`, `manifest.json`; unchanged components ship the previous zip |
| tools/sr2/tunnel_launcher_test.py, tools/sr2/session_test, tools/sr2/player_name_test | SR2 Steam tunnel on local transport; `Session` with a scripted lobby; `PlayerName` |

## RE0 research (`tools/re0/`)

| file | owns |
|---|---|
| gamectl.py | keys and screenshots to the RE0 window only (refuses unless it is in front; `idle` = seconds since the user's last input) |
| probe.py, disasm.py | live reads; offline disassembly of the decrypted image (`fn`, `vtable`, `xrefs`) |
| watch_write.py | hardware write watch (stop it through its stop file; killing it crashes the game) |
| fake_session.py | stand-in launcher with a scripted peer (doors, rooms, party mode, join snapshot, appearance) for split rooms on one PC |
| party_state.py, enemies.py | both characters' room and position; the loaded room's enemies |
| dump_stack.py | registers and return addresses of a `coop\crash-*.dmp` / `hang-*.dmp` |

## DS2 research (`tools/ds2/`, output in the git-ignored `tools/ds2/out/`)

| file | owns |
|---|---|
| rtti_scan.py, disasm.py, symbols.py | Decima reflection types and fields; x64 disassembly, xrefs, RTTI; script-exported names |
| probe.py | read-only live reads (class instances, pointer refs, transforms, handlers) |
| save_sync_test.py | DS2 save sync on local transport |
| fake_launcher.py, fake_peer.py | stand-in launcher and scripted second player for crater, vehicle, weapon, cutscene, cargo and pickup flows |
| gamectl.py | the DS2 window through `tools/re0/gamectl.py`, plus launch, close and relative mouse |
