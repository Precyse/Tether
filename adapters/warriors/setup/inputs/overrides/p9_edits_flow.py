"""Level flows of players 3 and 4: the level scripts' moves of player 1 and 2 reach players 3 and 4 too (edit table of the area, see apply_p10.py). The carry-over
across level loads is a per-frame check (p9_flow.cpp) and needs no generated-code edit."""

P9 = "warriors::p9::"

HEADER = "p9_flow.hpp"

ENTRY_HOOKS = [
    (0x00385BB8, "if (%steleport_object(c)) return;" % P9),     # Object_Teleport
    (0x00385DB0, "if (%steleport_to_flag(c)) return;" % P9),    # Object_TeleportToFlag
    (0x0023B138, "if (%steleport_near(c)) return;" % P9),       # Human_TeleportNear
    (0x004134F8, "if (%splayer_box_update(c)) return;" % P9),   # PlayerBox_Update
    (0x00413218, P9 + "player_boxes_reset(c);"),                # PlayerBoxPool_Create
]
