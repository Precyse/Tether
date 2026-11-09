# CODEMAP

Spec: `docs/CONTRACT.md`. This file is the index: which file holds the table of an area. Read this, then grep the area file or `docs/codemap/where.md`; never read long docs whole. Each area file lists every source file once (file, what it owns, key members); read only the area you work in. Update the area file in the same change that adds, moves or renames a file or a key function.

| area | file | covers |
|---|---|---|
| launcher | `docs/codemap/launcher.md` | `launcher/` core, `launcher/Ui/` view models, `launcher/WebHost/` (WebView2 window), `launcher/web/` (the page) |
| RE0 | `docs/codemap/re0.md` | `adapters/re0/`, `tools/re0/` (research tools and rows of its tests) |
| DS2 | `docs/codemap/ds2.md` | `adapters/ds2/`, `tools/ds2/` rows |
| SR2 | `docs/codemap/sr2.md` | `adapters/sr2/`, `tools/sr2/` rows |
| CD | `docs/codemap/cd.md` | `adapters/cd/` (Crimson Desert), `adapters/common/`, `tools/cd/` |
| DD2 | `docs/codemap/dd2.md` | `adapters/dd2/` (Dragon's Dogma 2 REFramework plugin) |
| where | `docs/codemap/where.md` | "where to look for X" by symptom or system (grep it, never read it whole) |
| tools | `docs/codemap/tools.md` | release files (`release.yml`, README, package README, `update.json`) and an overview of the root test tools, the research tools of each game and `tools/publish_check.py` (the rows of each tool are in the file of its game) |
