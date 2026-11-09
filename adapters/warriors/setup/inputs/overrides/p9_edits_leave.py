"""Leave, virtual pads and the other per-player state of players 3 and 4 that is not camera, HUD layout, rings or art (edit table of the area, see apply_p10.py).
Leaving is a per-frame check (p9_leave.cpp) and needs no generated-code edit."""

P9 = "warriors::p9::"

HEADER = "p9_misc.hpp"

ENTRY_HOOKS = [
    (0x001AE898, "if (%svpad_get(c)) return;" % P9),      # HUD_GetVirtualPad
    (0x001AE8D8, "if (%svpad_bind(c)) return;" % P9),     # HUD_BindPad
    (0x001AE4D8, "if (%svpad_ensure(c)) return;" % P9),   # HUD_EnsureVirtualPads
    (0x001AE5A0, "if (%svpad_reset(c)) return;" % P9),    # HUD_ResetPadBindings
    (0x004228A0, "if (%sstats_get_player(c)) return;" % P9),        # Stats_GetPlayer
    (0x00422C60, "if (%sstats_save_checkpoint(c)) return;" % P9),   # Stats_SaveCheckpoint
    (0x00422D00, "if (%sstats_restore_checkpoint(c)) return;" % P9),   # Stats_RestoreCheckpoint
    (0x00422930, "if (%sstats_reset_players(c)) return;" % P9),     # Stats_ResetPlayers
    (0x00422DA0, "if (%sstats_merge_player2(c)) return;" % P9),     # Stats_MergePlayer2
    (0x00418C68, "if (%sclear_spot_bytes(c)) return;" % P9),        # GameState_ResetForLevel
]

# Brain_SpotPlayers, Brain_SeesPlayer and Brain_HasSeenPlayer: the per-panel bytes of players 3 and 4 (p9_brain.cpp). Same instructions, other addresses.
SPOT = P9 + "spot_addr"
FUNC_EDITS = [
    (0x0028BF40, "set32(c.r[3], c.r[4].w[0] + c.r[5].w[0]);\n    c.r[2].d[0] = ld8((c.r[3].w[0] + 334u));",
     "set32(c.r[3], %s(c.r[4].w[0], 0, c.r[5].w[0]));\n    c.r[2].d[0] = ld8((c.r[3].w[0]));" % SPOT, 1),
    (0x0028BF40, "set32(c.r[2], c.r[4].w[0] + c.r[5].w[0]);\n    c.r[3].d[0] = ld8((c.r[2].w[0]));",
     "set32(c.r[2], %s(c.r[4].w[0] - 334u, 0, c.r[5].w[0]));\n    c.r[3].d[0] = ld8((c.r[2].w[0]));" % SPOT, 1),
    (0x0028BF40, "c.r[2].d[0] = c.r[5].d[0] < static_cast<std::uint64_t>(2ll) ? 1ull : 0ull;",
     "c.r[2].d[0] = c.r[5].d[0] < static_cast<std::uint64_t>(%spanel_count()) ? 1ull : 0ull;" % P9, 1),
    (0x0028BF88, "c.r[2].d[0] = ld8((c.r[3].w[0] + 336u));", "c.r[2].d[0] = ld8(%s(c.r[4].w[0], 1, c.r[5].w[0]));" % SPOT, 1),
    (0x0028AC80, "set32(c.r[2], c.r[3].w[0] + c.r[2].w[0]);\n    c.r[3].d[0] = ld8((c.r[2].w[0]));",
     "set32(c.r[2], %s(c.r[21].w[0], 0, c.r[2].w[0]));\n    c.r[3].d[0] = ld8((c.r[2].w[0]));" % SPOT, 1),
    (0x0028AC80, "set32(c.r[2], c.r[23].w[0] + c.r[2].w[0]);\n    st8((c.r[2].w[0]), static_cast<std::uint8_t>(c.r[16].w[0]));",
     "set32(c.r[2], %s(c.r[21].w[0], 0, c.r[2].w[0]));\n    st8((c.r[2].w[0]), static_cast<std::uint8_t>(c.r[16].w[0]));" % SPOT, 1),
    (0x0028AC80, "set32(c.r[4], c.r[4].w[0] + c.r[3].w[0]);\n    c.r[2].d[0] = ld8((c.r[4].w[0]));",
     "set32(c.r[4], %s(c.r[21].w[0], 1, c.r[3].w[0]));\n    c.r[2].d[0] = ld8((c.r[4].w[0]));" % SPOT, 1),
]
