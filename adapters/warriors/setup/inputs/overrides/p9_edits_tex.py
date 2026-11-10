"""Edits and entry hooks of the art of players 3 and 4: resource loading, the object list and object types, the overhead markers, name banners and radar sprites
(see apply_p10.py for the mechanics, p9/p9_textures.cpp for what the hooks do)."""

HEADER = "p9_textures.hpp"

P9 = "warriors::p9::"

ENTRY_HOOKS = [
    (0x00186710, P9 + "file_loaded(c);"),                 # on_file_read
    (0x002271F0, P9 + "overhead_icon_request(c);"),       # overhead_marker_show
    (0x0024B3D8, P9 + "sync_extra_markers(c);"),          # rings_target_step
    (0x00419B10, P9 + "extra_player_joining(c);"),        # session_add_player (before the core hook: the pack's records are in place when the HUD is built)
]

# (function address, old text, new text, expected matches)
FUNC_EDITS = [
    # archive_open: the WAD index just created.
    (0x0040C5E0, "    st32((c.r[16].w[0]), c.r[2].w[0]);\n    set32(c.r[5], c.r[5].w[0] - 31392u);\n",
     "    st32((c.r[16].w[0]), c.r[2].w[0]);\n    %swad_index_created(c.r[2].w[0]);\n    set32(c.r[5], c.r[5].w[0] - 31392u);\n" % P9, 1),
    # archive_index_lookup, the exit for a name the index does not hold: the pack's names are looked up in their own table (the game's index stays as it is).
    (0x001490B8, "L_00149138:\n    c.r[2].d[0] = 0ull + 0ull;\n", "L_00149138:\n    c.r[2].d[0] = %swad_alias_entry(c.r[6].w[0]);\n" % P9, 1),
    # on_object_list_read: the resource manager (r3) now points at the list.
    (0x00181170, "    st32((c.r[3].w[0] + 144u), c.r[4].w[0]);\n", "    st32((c.r[3].w[0] + 144u), c.r[4].w[0]);\n    %sobject_list_loaded(c.r[3].w[0]);\n" % P9, 1),
    # on_sprite_sheet_table_read: the sheet table is in the resource manager.
    (0x00182820, "    st32((c.r[3].w[0] + 4164u), c.r[2].w[0]);\n", "    st32((c.r[3].w[0] + 4164u), c.r[2].w[0]);\n    %ssheet_table_loaded();\n" % P9, 1),
    # on_sprite_page_read: the page (r16) has its rectangle array set.
    (0x00181B20, "    st32((c.r[16].w[0] + 12u), c.r[3].w[0]);\n", "    st32((c.r[16].w[0] + 12u), c.r[3].w[0]);\n    %spage_loaded(c.r[16].w[0]);\n" % P9, 1),
    # object_type_register: the new type record (r16) is in the table.
    (0x00390F18, "L_003910A0:\n    ld128((c.r[29].w[0] + 160u) & ~15u, c.r[16]);\n",
     "L_003910A0:\n    %stype_added(c, c.r[16].w[0]);\n    ld128((c.r[29].w[0] + 160u) & ~15u, c.r[16]);\n" % P9, 1),
    # floating_marker_setup: the markers of players 3 and 4 do not spin either.
    (0x003E8FA0, "{ const bool t = c.r[20].d[0] != c.r[2].d[0];  if (t) { goto L_003E9294; } }",
     "{ const bool t = c.r[20].d[0] != c.r[2].d[0] && !%splayer_marker_hash(c.r[20].w[0]);  if (t) { goto L_003E9294; } }" % P9, 1),
    # marker_attach_on_respawn: the marker's name by slot 0 to 3 (this branch is slot 1 and up).
    (0x0024B2A8, "set32(c.r[5], c.r[5].w[0] - 12648u);", "set32(c.r[5], %soverhead_icon_name(ld8(c.r[16].w[0] + 432u)));" % P9, 1),
    # player_panel_set_title: PLAYER 1/2 banners are PLAYER 3/4 on panels 2 and 3.
    (0x0020DC98, "L_0020DE90:\n    set32(c.r[2], c.r[29].w[0] + 48u);\n",
     "L_0020DE90:\n    c.r[18].sd[0] = static_cast<std::int32_t>(%sbanner_record(c.r[18].w[0], c.r[17].w[0]));\n    set32(c.r[2], c.r[29].w[0] + 48u);\n" % P9, 1),
    # minimap_person_glyph_refresh: the arrow sprite of a player's blip by slot.
    (0x001B3020, "set32(c.r[6], 0u + 363u);", "set32(c.r[6], %sradar_arrow_sprite(c.r[2].w[0]));" % P9, 1),
    # minimap_widget_step: the player an arrow belongs to (the heading it follows), the partner tags of the further players, tags are not turned.
    (0x001C5210, "    c.r[2].d[0] = c.r[2].d[0] ^ 362ull;\n    c.r[16].d[0] = 0ull < c.r[2].d[0] ? 1ull : 0ull;\n",
     "    c.r[16].d[0] = %sarrow_player(c.r[2].w[0]);\n" % P9, 1),
    (0x001C5210, "    set32(c.r[5], ld32((c.r[19].w[0] + 144u)));\n    c.budget -= 3;\n",
     "    if (%sradar_extra_tag(c.r[19].w[0], ld32(c.r[18].w[0] + 2376u))) { set32(c.r[2], ld32((c.r[18].w[0] + 2376u)));  goto L_001C5ED8; }\n"
     "    set32(c.r[5], ld32((c.r[19].w[0] + 144u)));\n    c.budget -= 3;\n" % P9, 1),
    (0x001C5210, "    if (c.r[4].d[0] != c.r[2].d[0]) { set32(c.r[2], ld32((c.r[18].w[0] + 2376u)));  goto L_001C5F34; }\n",
     "    if (c.r[4].d[0] != c.r[2].d[0] && !%sradar_is_new_tag(c.r[4].w[0])) { set32(c.r[2], ld32((c.r[18].w[0] + 2376u)));  goto L_001C5F34; }\n" % P9, 1),
]
