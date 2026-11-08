# Tether

Online co-op for single-player PC games. Tether is one launcher that installs the co-op mod for each supported game, hosts and joins sessions through Steam, keeps both players on the same game and mod version, and starts the game already linked to your partner.

## Games

| Game | Players | How co-op works | Status |
|---|---|---|---|
| Resident Evil 0 HD | 2 | Every machine runs its own world; characters, enemies, doors, items and story flags are replicated, with the host deciding what both share. The host plays Rebecca, the guest Billy. | Released |
| Saints Row 2 | up to 4 | The game's own co-op netcode, raised from 2 to 4 players and carried over a Steam tunnel. Needs the Juiced community patch installed; the launcher checks for it. | Released |
| Dragon's Dogma 2 | up to 3 | A REFramework plugin (the launcher installs REFramework too): partners join the host's world from the host's save, loaded through the game's own load call. | Preview |
| Crimson Desert | 2 | A proxy-DLL mod built on the game's own client/server messages. | Preview |
| Death Stranding 2 | 2 | In development, not in releases. | In development |

## Install

1. Download [Coop.zip](https://github.com/Precyse/Tether/releases/download/latest/Coop.zip) from the latest release and unzip it anywhere, for example `%LocalAppData%\CoopLauncher`.
2. Start `launcher\app\coop-launcher.exe`. Steam must be running.
3. Pick a game and press **Install** (the game must be closed). The launcher installs every file the mod needs (for Dragon's Dogma 2 also REFramework) and backs up anything it replaces. **Uninstall** in Settings puts your files back.

From then on Tether updates itself and the installed mods. Preview builds of the newest work are on the [preview release](https://github.com/Precyse/Tether/releases/tag/preview).

## Playing

- **Host:** pick the game, press **Host** and send your friend the lobby code.
- **Join:** paste the code and press **Join**, or join a friend listed under the Host row.

Both players need the same game version and the same Tether build; the launcher says so if they differ. Each player's card shows what their game is doing (installing, starting, loading, in the world) and, when something goes wrong, why and what to do. Settings has the logs folder and a report zip to send when something breaks.

## Repository

| Path | What it is |
|---|---|
| `launcher/` | The C# launcher: Steam lobbies and relayed connections, mod install and update, save sync, session flow; the window is a WebView2 page in `launcher/web/` |
| `launcher/games/*.json` | One profile per game: install files, requirements, options, start checks |
| `adapters/<game>/` | The in-game co-op mod for each game, with its unit tests |
| `docs/` | The launcher-adapter contract (`CONTRACT.md`), per-game notes and the reverse-engineering pattern library (`docs/re/PATTERNS.md`) |
| `tools/` | Test tools, research tools per game, release packaging |
| `CODEMAP.md` | Where everything lives; start here before changing code |

Every push to `main` builds the `latest` release and every push to `next` the `preview` release (`.github/workflows/release.yml`).

## Notes

- Tether only changes files of games you own and never ships game files. Decompiled or extracted game data stays out of this repository.
- Online play uses Steam's relayed networking; nothing is opened on your router.
- REFramework is downloaded from its own release; Juiced comes from its own distribution. Both keep their own licences.
