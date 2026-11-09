"""Edits of the generated audio code for four listeners (see apply_p10.py for the mechanics, p9\\p9_audio.cpp/.hpp for the storage and what the hooks do).

The sound code holds one listener per player in arrays of two. Gm_Level_Update's listener array and SoundTask_Update's volume pairs were stack buffers, so a third
entry would land on their scratch variables; they move to the ranges of p9_audio.hpp (the edits replace the stack address and nothing else, no cycle is added or
removed). The audio manager's own copies (+0x20, +0x40) cannot grow, +0x60 is another field: listeners 0 and 1 stay, 2 and 3 are in extra RAM and the two
loops that walk the manager's array step over the gap through listener_step. The hooks do nothing for one or two players.
"""

P9 = "warriors::p9::"
HEADER = "p9_audio.hpp"

ENTRY_HOOKS = []

FUNC_EDITS = []


def edit(address, old, new, count=1):
    FUNC_EDITS.append((address, old, new, count))


# --- Gm_Level_Update: path A (no cutscene flag) fills the listener array in a loop over the players, passes it to AudioManager_Update and HUD_Update, and rewrites
# listener 1 when the player count just became 2. Path B (cutscene) keeps its own two entry stack array, which Tasks_Update widens by repeating its last entry.
GM_LEVEL_UPDATE = 0x00158728
edit(GM_LEVEL_UPDATE, "set32(c.r[3], c.r[29].w[0] + 32u);\n    st32((c.r[29].w[0] + 96u), 0u);",
     "set32(c.r[3], %skLevelListeners + %skListenerBytes);\n    st32((c.r[29].w[0] + 96u), 0u);" % (P9, P9))               # the pointer to listener 1 HUD_Update gets
edit(GM_LEVEL_UPDATE, "c.r[18].d[0] = c.r[29].d[0] + 0ull;\n    set32(c.r[22], c.r[3].w[0] + 8u);",
     "c.r[18].d[0] = %skLevelListeners;\n    set32(c.r[22], c.r[3].w[0] + 8u);" % P9)                                                                     # the pointer the loop fills
edit(GM_LEVEL_UPDATE, "ld128((c.r[29].w[0]) & ~15u, c.r[2]);  if (t) { goto L_00158EB4; }",
     "ld128(%skLevelListeners & ~15u, c.r[2]);  if (t) { goto L_00158EB4; }" % P9)                                                                  # no camera: listener 0's first half
edit(GM_LEVEL_UPDATE, "ld128((c.r[29].w[0] + 16u) & ~15u, c.r[3]);\n    c.budget -= 2;\nL_00158EBC:",
     "ld128((%skLevelListeners + 16u) & ~15u, c.r[3]);\n    c.budget -= 2;\nL_00158EBC:" % P9)                                                    # no camera: listener 0's second half
edit(GM_LEVEL_UPDATE, "c.r[5].d[0] = c.r[29].d[0] + 0ull;\n    set32(c.r[4], ld32((c.r[2].w[0] - 21884u)));",
     "c.r[5].d[0] = %skLevelListeners;\n    set32(c.r[4], ld32((c.r[2].w[0] - 21884u)));" % P9)                                                          # AudioManager_Update's array
edit(GM_LEVEL_UPDATE, "c.r[5].d[0] = c.r[29].d[0] + 0ull;  if (depth < kMaxDepth) unit_002B(c, 306u",
     "c.r[5].d[0] = %skLevelListeners;  if (depth < kMaxDepth) unit_002B(c, 306u" % P9, 2)                                                           # HUD_Update's array, both calls
edit(GM_LEVEL_UPDATE, "st128((c.r[29].w[0] + 32u) & ~15u, c.r[2]);\n    st128((c.r[29].w[0] + 48u) & ~15u, c.r[3]);",
     "st128((%skLevelListeners + 32u) & ~15u, c.r[2]);\n    st128((%skLevelListeners + 48u) & ~15u, c.r[3]);" % (P9, P9))                                    # listener 1 rewritten for the new count
edit(GM_LEVEL_UPDATE, "st128((c.r[29].w[0] + 48u) & ~15u, c.r[3]);\n    c.r[2].sd[0] = static_cast<std::int32_t>(0x00600000u);",
     "st128((%skLevelListeners + 48u) & ~15u, c.r[3]);\n    c.r[2].sd[0] = static_cast<std::int32_t>(0x00600000u);" % P9)

# --- Tasks_Update: the loop of two copies the caller's first two listeners into the manager; listeners 2 and 3 follow into extra RAM.
edit(0x00112B10, "set32(c.r[16], ld32((c.r[17].w[0])));\n    c.budget -= 3;\n    { const bool t = c.r[16].d[0] == 0ull; c.r[2].sd[0] = static_cast<std::int32_t>(0x00550000u);",
     "%scopy_extra_listeners(c.r[5].w[0]);\n    set32(c.r[16], ld32((c.r[17].w[0])));\n    c.budget -= 3;\n    { const bool t = c.r[16].d[0] == 0ull; c.r[2].sd[0] = static_cast<std::int32_t>(0x00550000u);" % P9)

# --- Listeners_NearestDistance: walks manager + 0x20 * (player + 1).
edit(0x00113188, "set32(c.r[17], c.r[17].w[0] + 32u);", "set32(c.r[17], %slistener_step(c.r[17].w[0]));" % P9)

# --- SoundTask_Update: walks the manager's listeners the same way, and keeps a left/right volume pair per player (the stack buffer) that it then folds into the loudest.
SOUND_TASK_UPDATE = 0x0011A170
edit(SOUND_TASK_UPDATE, "set32(c.r[22], c.r[22].w[0] + 32u);", "set32(c.r[22], %slistener_step(c.r[22].w[0]));" % P9)
edit(SOUND_TASK_UPDATE, "c.r[20].d[0] = c.r[29].d[0] + 0ull;\n    set32(c.r[22], ld32((c.r[29].w[0] + 224u)));",
     "c.r[20].d[0] = %skSoundPairs;\n    set32(c.r[22], ld32((c.r[29].w[0] + 224u)));" % P9)                                                           # the pair pointer the loop steps by 8
edit(SOUND_TASK_UPDATE, "c.f[3] = ld32((c.r[29].w[0]));", "c.f[3] = ld32(%skSoundPairs);" % P9)                                                  # one listener: pair 0 as it is
edit(SOUND_TASK_UPDATE, "c.f[1] = ld32((c.r[29].w[0] + 4u));", "c.f[1] = ld32(%skSoundPairs + 4u);" % P9)
edit(SOUND_TASK_UPDATE, "c.r[2].d[0] = c.r[29].d[0] + 0ull;\n    c.budget -= 2;\nL_0011ADA8:",
     "c.r[2].d[0] = %skSoundPairs;\n    c.budget -= 2;\nL_0011ADA8:" % P9)                                                                              # the loudest of the pairs

# --- AmbientManager_UpdateEmitters: a flag per player says whether the player can hear the emitter (the first two are written by the original code, a loop clears
# the flags of players the emitter does not suit); the emitter is dropped when none is left.
AMBIENT_UPDATE_EMITTERS = 0x0010C100
edit(AMBIENT_UPDATE_EMITTERS, "L_0010C224:\n    c.r[4].sd[0] = static_cast<std::int32_t>(0x00510000u);\n    set32(c.r[2], c.r[22].w[0] + 1u);",
     "L_0010C224:\n    %sambient_extra_flags(c.r[29].w[0]);\n    c.r[4].sd[0] = static_cast<std::int32_t>(0x00510000u);\n    set32(c.r[2], c.r[22].w[0] + 1u);" % P9)
edit(AMBIENT_UPDATE_EMITTERS, "set32(c.r[2], ld32((c.r[29].w[0] + 4u)));\n    c.budget -= 3;\n    { const bool t = c.r[2].d[0] == 0ull;  if (t) { goto L_0010CBF0; } }",
     "set32(c.r[2], ld32((c.r[29].w[0] + 4u)));\n    c.budget -= 3;\n    { const bool t = c.r[2].d[0] == 0ull && !%sambient_extra_heard(c.r[29].w[0]);  if (t) { goto L_0010CBF0; } }" % P9)
