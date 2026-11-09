"""Edits and entry hooks of the cameras for four players (see apply_p10.py for the mechanics, p9\\p9_camera.cpp and p9_views.cpp for what the hooks do).

The camera manager keeps its per-player state in arrays of two entries packed one behind the other: nine camera arrays (0x005d9148 + 8 * array), the per-player
camera switches (0x0050b1b0, 0x0050b1b8) and the view-enable words (0x0050b1e8). The generated code reaches an array as `lui 0x5e0000` / `lui 0x510000` and a
negative offset (`c.r[Y].w[0] - K`). RELOCATED turns each such expression, found by reading the function's code (p9_cam_scan.py), into the absolute address of the
same element in the four-entry copy in extra RAM (p9_game.hpp, kCam), so every loop and index that works for two players works for four. Every function listed
is counted: a regenerated tree that moved something fails loudly.
"""
import p9_cam_scan as scan
import p9_edits_audio as audio   # the audio listeners are part of the view code: their tables join this module's

P9 = "warriors::p9::"
HEADER = "p9_camera.hpp"

CAM = 0x02200000                     # p9_game.hpp, kCam
ENTRIES = 4                          # players per array
ARRAY_BYTES = 4 * ENTRIES
WORD = 4

# (first old address, bytes between the old arrays, words per old array, new address of array 0, bytes between the new arrays)
CAMERA_ARRAYS = (0x005D9148, 8, 2, CAM, ARRAY_BYTES)
CAMERA_ARRAY_COUNT = 9
# Per-player words and bytes of the camera code: (first old address, new address, bytes of the old two-entry array). The new arrays hold four entries.
SWITCHES = ((0x0050B1E8, CAM + CAMERA_ARRAY_COUNT * ARRAY_BYTES, 2 * WORD),         # view-enable words
            (0x0050B1B0, CAM + (CAMERA_ARRAY_COUNT + 1) * ARRAY_BYTES, 2 * WORD),   # stick switch
            (0x0050B1B8, CAM + (CAMERA_ARRAY_COUNT + 2) * ARRAY_BYTES, 2 * WORD),   # zoom switch
            (0x0050B1F0, CAM + 0xD0, 2 * WORD),                                     # right stick options of a player (words)
            (0x0050B1F8, CAM + 0xE0, 2 * WORD),                                     # (invert)
            (0x0050B240, CAM + 0xF0, 2),                                            # auto-adjust option of a player (bytes)
            (0x0050B248, CAM + 0xF4, 2))                                            # auto-follow switch (bytes)
LUI_CAMERA_BASE, LUI_GLOBALS_BASE = 0x005E0000, 0x00510000


def relocated(address):
    """New address of an element of the two-entry arrays, None for any other address."""
    first, spacing, words, new_first, new_spacing = CAMERA_ARRAYS
    if first <= address < first + CAMERA_ARRAY_COUNT * spacing:
        array, element = divmod(address - first, spacing)
        return new_first + array * new_spacing + element
    for old, new, size in SWITCHES:
        if old <= address < old + size:
            return new + address - old
    return None


# Sites whose register the scan could not follow through the function's branches. Each was read in the generated code: the register is the `lui` base of the array
# its offset names (function: count).
UNFOLLOWED = {0x0011FF50: 1, 0x00121888: 2, 0x001282A0: 1}

# Functions of the camera manager, the HUD and the pause code that reach the arrays (the census: every function whose code has such an expression).
RELOCATED = (0x0011DAA8, 0x0011DE58, 0x0011E440, 0x0011E878, 0x0011E9C0, 0x0011EAF0, 0x0011ED60, 0x0011EE08, 0x0011F9B0, 0x0011F9C8, 0x0011F9E0, 0x0011FAC8,
             0x0011FBB0, 0x0011FC98, 0x0011FD80, 0x0011FE68, 0x0011FF50, 0x00121888, 0x00121E30, 0x00122B80, 0x00122ED0, 0x001282A0, 0x00129050, 0x0013B7B8,
             0x00158728, 0x001A6D28, 0x001A7000, 0x001AF010, 0x0020E058, 0x00129C78, 0x00418A88, 0x00418AA8)


def relocation_edits():
    """(function, old text, new text, count) for every array reference of RELOCATED, read from the unedited code."""
    edits = []
    for function in RELOCATED:
        found, unfollowed = {}, 0
        for _, text in scan.function_text(function):
            for expression, base, offset in scan.sites(text):
                if base is None:
                    candidates = [b + offset for b in (LUI_CAMERA_BASE, LUI_GLOBALS_BASE) if relocated(b + offset) is not None]
                    if not candidates:
                        continue
                    unfollowed += 1
                    address = candidates[0]
                else:
                    address = base + offset
                target = relocated(address)
                if target is not None:
                    found[(expression, target)] = found.get((expression, target), 0) + 1
        if unfollowed != UNFOLLOWED.get(function, 0):
            raise SystemExit("function 0x%08X: %d array references with an unknown base register, expected %d" % (function, unfollowed, UNFOLLOWED.get(function, 0)))
        for (expression, target), count in sorted(found.items()):
            edits.append((function, expression, "0x%08Xu" % target, count))
    return edits


# Each hook is put right after the function's label, so the last one listed runs first: a snapshot or a fresh game takes over the arrays (ensure_cameras) before
# any code of these functions, hooks included, reads them.
ENTRY_HOOKS = [
    (0x0011EAF0, P9 + "cameras_add_player(c);"),                         # Cameras_AddPlayer
    (0x0011E440, "if (%srelease_blocked(c)) return;" % P9),             # Camera_Release
    (0x00121888, "if (%sdecide_views(c)) return;" % P9),                # Cameras_DecideViews
    (0x001282A0, "if (%sgather_targets(c)) return;" % P9),              # Cam_Follow_GatherTargets
] + [(function, P9 + "ensure_cameras(c);") for function in RELOCATED]

FUNC_EDITS = relocation_edits()


def edit(address, old, new, count=1):
    FUNC_EDITS.append((address, old, new, count))


# --- Loops over the two entries of the arrays: they run over every slot a player has used (two unless players 3 or 4 joined), so a two player game is unchanged. ---
LAST_SLOT = P9 + "cam_last_slot()"
edit(0x0011E9C0, "set32(c.r[10], 0u + 1u);", "set32(c.r[10], %srelease_cam_slots() - 1u);" % P9)                      # Cameras_ClearSlots: clears every slot, then only two are in use
edit(0x00122B80, "set32(c.r[5], 0u + 1u);", "set32(c.r[5], %s);" % LAST_SLOT)                                          # Cameras_ResetLevel: three switch arrays back to 1
ZOOM_SWITCH = CAM + (CAMERA_ARRAY_COUNT + 2) * ARRAY_BYTES
edit(0x0011DE58, "set32(c.r[3], 0u + 1u);\n    set32(c.r[2], 0x%08Xu);\n    set32(c.r[2], c.r[2].w[0] + 4u);" % ZOOM_SWITCH,
     "set32(c.r[3], %s);\n    set32(c.r[2], 0x%08Xu + 4u * %s);" % (LAST_SLOT, ZOOM_SWITCH, LAST_SLOT))   # CamEnable(switch 0, all players): from the last slot down
# Camera_Release ends by clearing the base and view entries of the two players that held the camera: the further players' too.
edit(0x0011E440, "L_0011E84C:\n    c.r[4].d[0] = c.r[17].d[0] + 0ull;",
     "L_0011E84C:\n    %srelease_extra_slots(c.r[17].w[0]);\n    c.r[4].d[0] = c.r[17].d[0] + 0ull;" % P9)


# --- The audio listener arrays (p9_edits_audio.py). -------------------------------------------------------------------------------------------------------
ENTRY_HOOKS += audio.ENTRY_HOOKS
FUNC_EDITS += audio.FUNC_EDITS
EDITS = list(audio.EDITS) if hasattr(audio, "EDITS") else []

# --- HUD panels in a split screen: PlayerHUD_Update copies the panel's base position from the layout table, then lays its parts out from it. -----------------------
edit(0x00214138, "L_002141F8:\n    set32(c.r[2], ld32((c.r[19].w[0] + 16632u)));", "L_002141F8:\n    %ssplit_panel_base(c, c.r[19].w[0]);\n    set32(c.r[2], ld32((c.r[19].w[0] + 16632u)));" % P9)

# --- The light manager keeps two per-view contexts (resources 7 and 8, at manager +0xb0): the third and fourth view use the first two again. ---------------------
edit(0x0017D880, "set32(c.r[2], c.r[5].w[0] << 2);\n    st128((c.r[29].w[0] + 240u) & ~15u, c.r[18]);", "set32(c.r[2], (c.r[5].w[0] & 1u) << 2);\n    st128((c.r[29].w[0] + 240u) & ~15u, c.r[18]);")

# --- Cam_Follow_Update eases the field of view at most 7.5 degrees a second; the framing of several players needs more (the same with up to two players). ---------
edit(0x0012AE58, "c.r[1].sd[0] = static_cast<std::int32_t>(0x40F00000u);\n    c.f[1] = c.r[1].w[0];\n    c.f[21] = fp::min(c.f[2], c.f[1]);",
     "c.r[1].sd[0] = static_cast<std::int32_t>(%sfov_rate_cap());\n    c.f[1] = c.r[1].w[0];\n    c.f[21] = fp::min(c.f[2], c.f[1]);" % P9)

# --- Cam_Follow_LookAt averages its (two) targets: with a cluster in view the look-at point is the cluster's centre instead (after the two target special case, before
# the look-at height is added and the point's move is limited). ---------------------------------------------------------------------------------------------
edit(0x00127D88, "L_001280D4:\n    c.f[0] = ld32((c.r[17].w[0] + 520u));", "L_001280D4:\n    %scluster_look_at(c.r[17].w[0]);\n    c.f[0] = ld32((c.r[17].w[0] + 520u));" % P9)
