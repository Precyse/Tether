"""Edits and entry hooks of the phase 9 core: the player list, throw arcs, player states, inventories and pads (see apply_p9.py for the mechanics)."""

P9 = "warriors::p9::"

# (function address, statement put after its label). "if (...) return;" statements end the function when the hook handled it.
ENTRY_HOOKS = [
    (0x001454A8, P9 + "frame_start(c);"),                               # pads_frame_step
    (0x00149770, "if (%spad_port_poll(c)) return;" % P9),               # pad_port_poll
    (0x00418588, P9 + "game_construct(c);"),                            # session_state_init
    (0x0041A370, P9 + "ensure_area(c);"),                               # session_step
    (0x004197A8, P9 + "ensure_area(c);"),                               # session_failure_check
    (0x00419DA0, P9 + "ensure_area(c);"),                               # throw_arcs_draw
    (0x00244770, P9 + "ensure_area(c);"),                               # throw_aim_step
    (0x00222B18, "if (%splayer_state(c)) return;" % P9),                # player_slot_state
    (0x0041E420, "if (%sloadout_amount(c)) return;" % P9),               # loadout_amount
    (0x0041E3D8, "if (%sloadout_has_item(c)) return;" % P9),                # loadout_has_item
    (0x0041E5B0, "if (%sloadout_give_item(c)) return;" % P9),                # loadout_give_item
    (0x0041E9D8, "if (%sloadout_set_amount(c)) return;" % P9),               # loadout_set_amount
    (0x0041EC60, "if (%sloadout_clear_for_player(c)) return;" % P9),    # loadout_clear_for_player
    (0x0041E0B8, "if (%sloadout_snapshot_store(c)) return;" % P9),         # loadout_snapshot_store
    (0x0041E168, "if (%sloadout_snapshot_load(c)) return;" % P9),      # loadout_snapshot_load
    (0x0041E040, "if (%sloadout_empty(c)) return;" % P9),                   # loadout_empty
    (0x00419B10, "if (%ssession_add_player(c)) return;" % P9),             # session_add_player
]

EXTRA = 0x02000000   # ps2::kExtraBase, the co-op storage (p9_game.hpp)
TICK = "0x%08Xu" % (EXTRA + 0x04)
FIELD234 = "0x%08Xu" % (EXTRA + 0x08)
ARCS = "0x%08Xu" % (EXTRA + 0x100)
ARC_OFFSET = P9 + "throw_arc_offset()"

# (unit file, old text, new text, expected matches)
EDITS = [
    # pads_frame_step's loop visits records 0 and 4 only: all eight once pads beyond the two are plugged.
    ("unit_0011.cpp", "L_0014562C:\n    set32(c.r[20], c.r[20].w[0] + 4u);\n", "L_0014562C:\n    set32(c.r[20], c.r[20].w[0] + %spad_loop_step());\n" % P9, 1),
    ("unit_0011.cpp", "set32(c.r[21], c.r[21].w[0] + 320u);  if (t) { if (c.budget < 0 && preempt(c)) { c.pc = 0x001454F8u | kUnwind; return; } goto L_001454F8; }",
     "set32(c.r[21], c.r[21].w[0] + %spad_loop_step() * 80u);  if (t) { if (c.budget < 0 && preempt(c)) { c.pc = 0x001454F8u | kUnwind; return; } goto L_001454F8; }" % P9, 1),
    # session_step's counter and the unused word after it leave the handle list's way (W_GameState +0x230 and +0x234).
    ("unit_00C6.cpp", "st32((c.r[17].w[0] + 564u), c.r[2].w[0]);", "st32(%s, c.r[2].w[0]);" % FIELD234, 1),
    ("unit_00C6.cpp", "st32((c.r[17].w[0] + 560u), 0u);", "st32(%s, 0u);" % TICK, 1),
    ("unit_00C6.cpp", "set32(c.r[4], ld32((c.r[17].w[0] + 560u)));  goto L_004199A4;", "set32(c.r[4], ld32(%s));  goto L_004199A4;" % TICK, 1),
    ("unit_00C6.cpp", "set32(c.r[2], ld32((c.r[16].w[0] + 560u)));", "set32(c.r[2], ld32(%s));" % TICK, 1),
    ("unit_00C6.cpp", "st32((c.r[16].w[0] + 560u), c.r[2].w[0]);", "st32(%s, c.r[2].w[0]);" % TICK, 1),
    ("unit_00C6.cpp", "set32(c.r[4], ld32((c.r[16].w[0] + 560u)));", "set32(c.r[4], ld32(%s));" % TICK, 1),
    # session_add_player: room for four players.
    ("unit_00C6.cpp", "c.r[16].sd[0] = static_cast<std::int16_t>(ld16((c.r[18].w[0] + 548u)));\n    c.r[2].d[0] = c.r[16].sd[0] < 2ll ? 1ull : 0ull;",
     "c.r[16].sd[0] = static_cast<std::int16_t>(ld16((c.r[18].w[0] + 548u)));\n    c.r[2].d[0] = c.r[16].sd[0] < 4ll ? 1ull : 0ull;", 1),
    # session_match_player_count: a join is refused at four players, not two.
    ("unit_00C6.cpp", "{ const bool t = c.r[19].d[0] == c.r[21].d[0]; c.r[5].d[0] = c.r[18].d[0] + 0ull;  if (t) { goto L_0041A628; } }",
     "{ const bool t = c.r[19].d[0] == 4ull; c.r[5].d[0] = c.r[18].d[0] + 0ull;  if (t) { goto L_0041A628; } }", 1),
    # ThrowPaths: outside the game state block (they would end at +0x5800, past its end at +0x57c0). session_state_init, session_add_player,
    # throw_arcs_draw and throw_arc_trace. The game builds arcs 0 and 1; arcs 2 and 3 are built when a third and fourth player join (session_add_player).
    ("unit_00C6.cpp", "set32(c.r[23], c.r[17].w[0] + 22336u);", "set32(c.r[23], %s);" % ARCS, 1),
    ("unit_00C6.cpp", "set32(c.r[18], c.r[17].w[0] + 22336u);", "set32(c.r[18], %s);" % ARCS, 1),
    ("unit_00C6.cpp", "set32(c.r[4], c.r[4].w[0] + 22336u);", "set32(c.r[4], c.r[4].w[0] + %s);" % ARC_OFFSET, 2),
    ("unit_00C6.cpp", "set32(c.r[17], 0u + 22336u);", "set32(c.r[17], %s);" % ARC_OFFSET, 1),
    ("unit_0051.cpp", "set32(c.r[4], c.r[4].w[0] + 22336u);", "set32(c.r[4], c.r[4].w[0] + %s);" % ARC_OFFSET, 1),
]
