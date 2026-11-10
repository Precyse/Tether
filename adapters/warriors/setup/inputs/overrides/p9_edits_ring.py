"""Edits of the generated ground ring code and radar code (p9_edits_radar.py, merged below) for four players (see apply_p10.py for the mechanics, p9\\p9_ring.cpp/.hpp for the storage and what the hooks do).

rings_step and ring_queue_health keep their state in arrays of two (one word per player slot), in a fade-in list of four entries and, for one frame, in a
four entry stack buffer. Every place is moved to the range in p9_ring.hpp, which has four words per slot array and room for eight list entries: the edits replace the
absolute address (or the stack address) of each access and nothing else, so the cycle count of a one or two player game is the original's. The names in the new text
are the constants of p9_ring.hpp.
"""

import p9_edits_radar as radar

P9 = "warriors::p9::"
HEADER = "p9_ring.hpp"   # includes p9_radar.hpp

ENTRY_HOOKS = [
    (0x0024B780, P9 + "ring_frame_start();"),   # rings_step: takes the original words over (once), empties this frame's list
] + radar.ENTRY_HOOKS

FUNC_EDITS = list(radar.FUNC_EDITS)


def edit(address, old, new, count=1):
    FUNC_EDITS.append((address, old, new, count))


# --- rings_step ------------------------------------------------------------------------------------------------------------------------------------
RINGS_STEP = 0x0024B780
# The fade-in list (0x006c72d8, flag 0x006c7308): initialised once, passed to ring_find_fresh_fade, walked for the recent entries, rewritten at the end.
edit(RINGS_STEP, "set32(c.r[3], ld32((c.r[2].w[0] + 29448u)));", "set32(c.r[3], ld32(%skRingFadeReady));" % P9)
edit(RINGS_STEP, "st32((c.r[6].w[0] + 29448u), c.r[2].w[0]);", "st32(%skRingFadeReady, c.r[2].w[0]);" % P9)
edit(RINGS_STEP, "set32(c.r[4], c.r[2].w[0] + 29400u);", "set32(c.r[4], %skRingFadeList);" % P9)
edit(RINGS_STEP, "set32(c.r[17], c.r[2].w[0] + 29400u);", "set32(c.r[17], %skRingFadeList);" % P9)
edit(RINGS_STEP, "set32(c.r[4], c.r[9].w[0] + 29400u);", "set32(c.r[4], %skRingFadeList);" % P9, 2)   # the argument of ring_find_fresh_fade, two loops
# The list of this frame was the stack buffer sp+0: its first entry (cleared to -1), the timestamp column (sp+8), the pointer to the next entry and the final copy.
edit(RINGS_STEP, "c.r[2].d[0] = c.r[29].d[0] + 0ull;\n    set32(c.r[3], 0u + 3u);", "c.r[2].d[0] = %skRingWorkList;\n    set32(c.r[3], 0u + 3u);" % P9)
edit(RINGS_STEP, "set32(c.r[8], c.r[29].w[0] + 8u);", "set32(c.r[8], %skRingWorkList + 8u);" % P9)
edit(RINGS_STEP, "set32(c.r[6], c.r[29].w[0] + 8u);", "set32(c.r[6], %skRingWorkList + 8u);" % P9, 2)
edit(RINGS_STEP, "set32(c.r[4], c.r[4].w[0] + c.r[29].w[0]);\n    st32((c.r[29].w[0] + 92u), c.r[4].w[0]);",
     "set32(c.r[4], c.r[4].w[0] + %skRingWorkList);\n    st32((c.r[29].w[0] + 92u), c.r[4].w[0]);" % P9)
edit(RINGS_STEP, "set32(c.r[8], c.r[3].w[0] + c.r[29].w[0]);", "set32(c.r[8], c.r[3].w[0] + %skRingWorkList);" % P9)
for offset in (0, 8, 16, 24, 32, 40):
    suffix = "" if offset == 0 else " + %du" % offset
    edit(RINGS_STEP, "ld64((c.r[29].w[0]%s))" % suffix, "ld64(%skRingWorkList%s)" % (P9, suffix))
edit(RINGS_STEP, "st64((c.r[8].w[0] + 29400u), c.r[3].d[0]);", "st64(%skRingFadeList, c.r[3].d[0]);" % P9)
edit(RINGS_STEP, "st32((c.r[9].w[0] + 1376u), 0u);\n    ld128((c.r[29].w[0] + 240u) & ~15u, c.r[16]);",
     "st32((c.r[9].w[0] + 1376u), 0u);\n    %sring_end_frame();\n    ld128((c.r[29].w[0] + 240u) & ~15u, c.r[16]);" % P9)
# The per-slot "ring queued" flags (0x00510530): read after the target's ring was considered, cleared at the end.
edit(RINGS_STEP, "set32(c.r[9], c.r[9].w[0] + 1328u);", "set32(c.r[9], %skRingQueued);" % P9)
edit(RINGS_STEP, "st32((c.r[3].w[0] + 1328u), 0u);\n    set32(c.r[2], c.r[3].w[0] + 1328u);\n    set32(c.r[4], c.r[8].w[0] + 29400u);",
     "st32(%skRingQueued, 0u);\n    set32(c.r[2], %skRingQueued);\n    set32(c.r[4], %skRingFadeList);" % (P9, P9, P9))
# The reticule request byte of the list position (0x00622e10 + position): panels 2 and 3 keep theirs in HUD2.
edit(RINGS_STEP, "c.r[8].sd[0] = static_cast<std::int32_t>(0x00620000u);\n    set32(c.r[8], c.r[8].w[0] + 11792u);\n    set32(c.r[2], c.r[8].w[0] + c.r[17].w[0]);",
     "set32(c.r[8], 0x00622E10u);\n    set32(c.r[2], %sring_request_byte(c.r[8].w[0], c.r[17].w[0]));" % P9)   # r8 keeps the base address the original left in it

# --- ring_queue_health ----------------------------------------------------------------------------------------------------------------------------
QUEUE_HEALTH_RINGS = 0x0024A230
edit(QUEUE_HEALTH_RINGS, "set32(c.r[2], c.r[2].w[0] + 1328u);", "set32(c.r[2], %skRingQueued);" % P9)
# The rage flash: armed flag (0x00510538), flash count (0x00510540), stamp of the last step (0x006c72c8) and the flag that the stamps were set (0x006c72d0).
edit(QUEUE_HEALTH_RINGS, "set32(c.r[2], ld32((c.r[20].w[0] + 29392u)));", "set32(c.r[2], ld32(%skRingFlashStampReady));" % P9)
edit(QUEUE_HEALTH_RINGS, "st32((c.r[20].w[0] + 29392u), c.r[2].w[0]);", "st32(%skRingFlashStampReady, c.r[2].w[0]);" % P9)
edit(QUEUE_HEALTH_RINGS, "set32(c.r[5], c.r[17].w[0] + 29384u);\n    ldl(c.r[2], (c.r[29].w[0] + 71u));\n    ldr(c.r[2], (c.r[29].w[0] + 64u));\n    sdl(c.r[2], (c.r[5].w[0] + 7u));\n    sdr(c.r[2], (c.r[5].w[0]));",
     "set32(c.r[5], %skRingFlashStamp);\n    ldl(c.r[2], (c.r[29].w[0] + 71u));\n    ldr(c.r[2], (c.r[29].w[0] + 64u));\n    sdl(c.r[2], (c.r[5].w[0] + 7u));\n    sdr(c.r[2], (c.r[5].w[0]));\n    %sring_stamp_all_slots();" % (P9, P9))
edit(QUEUE_HEALTH_RINGS, "set32(c.r[20], c.r[3].w[0] + 1336u);", "set32(c.r[20], %skRingRageFull);" % P9)
edit(QUEUE_HEALTH_RINGS, "set32(c.r[3], c.r[16].w[0] + 1336u);", "set32(c.r[3], %skRingRageFull);" % P9)
edit(QUEUE_HEALTH_RINGS, "set32(c.r[4], c.r[17].w[0] + 1344u);", "set32(c.r[4], %skRingFlashCount);" % P9)
edit(QUEUE_HEALTH_RINGS, "set32(c.r[21], c.r[17].w[0] + 1344u);", "set32(c.r[21], %skRingFlashCount);" % P9)
edit(QUEUE_HEALTH_RINGS, "set32(c.r[3], c.r[3].w[0] + 29384u);", "set32(c.r[3], %skRingFlashStamp);" % P9)
edit(QUEUE_HEALTH_RINGS, "set32(c.r[19], c.r[2].w[0] + 29384u);", "set32(c.r[19], %skRingFlashStamp);" % P9)

# --- overlay_ring_request: hud + 0x225d0 + panel; panels 2 and 3 are in HUD2. ---------------------------------------------------------------------------
edit(0x001B28A0, "set32(c.r[4], c.r[4].w[0] + c.r[5].w[0]);", "set32(c.r[4], %sring_request_byte(c.r[4].w[0], c.r[5].w[0]));" % P9)
