"""Display-only changes for three or more players: HUD panels at 0.75 scale and the fade of a player's model at the camera (see p9\\p9_display.hpp, p9_hud_scale.cpp,
p9_near_fade.cpp). Entry hooks only: each one starts the function's own code again through call_guest with the change in force, or changes the argument of the original.
"""

P9 = "warriors::p9::"

HEADER = "p9_display.hpp"

ENTRY_HOOKS = [
    (0x00213290, 'if (%spanel_render(c)) return;' % P9),   # player_panel_draw
    (0x00182DE0, P9 + 'scale_sprite(c);'),                 # sprite_submit
    (0x00174320, 'if (%shuman_draw(c)) return;' % P9),     # person_model_draw
]
