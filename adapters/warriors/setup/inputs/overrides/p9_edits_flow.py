"""Level flows of players 3 and 4: the level scripts' moves of player 1 and 2 reach players 3 and 4 too (edit table of the area, see apply_p10.py). The carry-over
across level loads is a per-frame check (p9_flow.cpp) and needs no generated-code edit."""

P9 = "warriors::p9::"

HEADER = "p9_flow.hpp"

ENTRY_HOOKS = [
    (0x00385BB8, "if (%swarp_object(c)) return;" % P9),     # warp_object
    (0x00385DB0, "if (%swarp_object_to_marker(c)) return;" % P9),    # warp_object_to_marker
    (0x0023B138, "if (%swarp_person_beside(c)) return;" % P9),       # warp_person_beside
    (0x004134F8, "if (%strigger_box_step(c)) return;" % P9),   # trigger_box_step
    (0x00413218, P9 + "player_boxes_reset(c);"),                # trigger_box_pool_init
]
