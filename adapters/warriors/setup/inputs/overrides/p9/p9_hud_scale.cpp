#include "p9_display.hpp"

#include "p9_camera.hpp"
#include "p9_game.hpp"
#include "p9_screen.hpp"
#include "ps2/system.hpp"

#include <cmath>

// HUD panels at 0.75 scale with three or more players. A panel is not one object with a size: its bars, texts, icons, counters and tally marks are placed by
// player_panel_init, _Update, _UpdateCounters, _LayoutTallyMarks, _SetBanner and the widget classes underneath, each from the panel's origin (+0x40e0) plus table offsets.
// Instead of touching those places, the panel is drawn at the end of the chain: player_panel_draw runs the game's own code with `g_scaling` on, and every sprite it
// hands to sprite_submit (bars, icons and the glyphs of every text) is a copy with its place moved toward the corner of the panel's view and its size scaled.
// Nothing the game keeps is changed, and with two players nothing here runs.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kScaleFromPlayers = 3;
constexpr std::uint32_t kPlayerPanelDraw = 0x00213290;
constexpr float kRightColumnFrom = 0.5f;                           // an origin at or beyond this share of the screen belongs to a panel of the right column of views

// The screen object (0x50cdb4) turns a widget's place into the one its sprite holds (screen_to_overlay_convert through widget_place): slot 0x94 of the table at +8,
// `this` moved by the s16 at 0x90. It leaves (across, 1.1, 0.5 - vertical) and the widget lays that out as the sprite's place (across, 0.5 - vertical, -1.1).
constexpr std::uint32_t kScreenPointer = 0x0050CDB4, kScreenTable = 8, kConvertAdjust = 0x90, kConvertSlot = 0x94;
constexpr std::uint32_t kConvertedVertical = 8;
constexpr std::uint32_t kPivotAt = kHudScaleScratch, kSpriteCopy = kHudScaleScratch + 0x20;

// The sprite record (sprite_submit): 0x80 bytes with the place at +0x40 and the size (width, height) at +0x50. An instance flag selects the four corner form
// instead, which the HUD does not use.
constexpr std::uint32_t kSpriteBytes = 0x80, kSpritePlace = 0x40, kSpriteSize = 0x50, kInstanceFlags = 0x28, kCornerForm = 8;
constexpr std::uint32_t kPlaceAcross = 0, kPlaceVertical = 4;      // the place's words that run across the screen and down it

bool g_scaling = false;
bool g_corner_form_seen = false;                                   // for the trace only
float g_pivot[2];                                                  // the corner in the sprite's own units: across, vertical
float g_placed[kMaxPlayers][2];                                    // per panel, the corner (in table units) it was last drawn about, for the trace only

// The corner of the view a panel belongs to, in table units: the screen's left or right edge by the column its origin is in, the top of the row it is in.
void view_corner(std::uint32_t panel, float &across, float &vertical) {
    const std::uint32_t rows = ps2::ld32(kViewViews) >= 2 ? ps2::ld16(kViewRows) : 1;
    across = read_float(panel + kPlayerHudBase) >= kRightColumnFrom ? 1.0f : 0.0f;
    vertical = rows ? std::floor(read_float(panel + kPlayerHudBase + kBaseVertical) * rows) / rows : 0.0f;
}

// The corner as a sprite holds it: the screen object's conversion of the table place.
bool corner_in_sprite_units(ps2::Cpu &c, float across, float vertical) {
    const std::uint32_t screen = ps2::ld32(kScreenPointer);
    if (!screen) return false;
    const std::uint32_t table = ps2::ld32(screen + kScreenTable);
    write_float(kPivotAt, across);
    write_float(kPivotAt + 4, 0.0f);
    write_float(kPivotAt + 8, vertical);
    write_float(kPivotAt + 12, 1.0f);
    const std::uint32_t self = screen + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(table + kConvertAdjust)));
    ps2::call_guest(c, ps2::ld32(table + kConvertSlot), self, kPivotAt);
    g_pivot[0] = read_float(kPivotAt);
    g_pivot[1] = read_float(kPivotAt + kConvertedVertical);
    return true;
}

float toward_pivot(float value, float pivot) { return pivot + (value - pivot) * kPanelScale; }

}

bool panel_render(ps2::Cpu &c) {
    if (g_scaling || player_count() < kScaleFromPlayers) return false;
    const std::uint32_t panel = c.r[4].w[0];
    float across, vertical;
    view_corner(panel, across, vertical);
    if (!corner_in_sprite_units(c, across, vertical)) return false;
    const std::uint32_t index = ps2::ld32(panel + kPlayerHudPanel);
    if (index < kMaxPlayers && (g_placed[index][0] != across || g_placed[index][1] != vertical)) {
        g_placed[index][0] = across;
        g_placed[index][1] = vertical;
        P9_TRACE("[p9] panel %u scaled about the corner %.2f %.2f of its view (origin %.3f %.3f, frame %llu)", index, across, vertical, read_float(panel + kPlayerHudBase),
                 read_float(panel + kPlayerHudBase + kBaseVertical), static_cast<unsigned long long>(ps2::frame_count()));
    }
    g_scaling = true;
    ps2::call_guest(c, kPlayerPanelDraw, panel);
    g_scaling = false;
    c.pc = c.r[31].w[0];
    return true;
}

void scale_sprite(ps2::Cpu &c) {
    if (!g_scaling) return;
    const std::uint32_t sprite = c.r[5].w[0];
    if (ps2::ld32(c.r[4].w[0] + kInstanceFlags) & kCornerForm) {
        if (!g_corner_form_seen) P9_TRACE("[p9] a panel sprite in corner form is not scaled");
        g_corner_form_seen = true;
        return;
    }
    for (std::uint32_t i = 0; i < kSpriteBytes; i += 4) ps2::st32(kSpriteCopy + i, ps2::ld32(sprite + i));
    write_float(kSpriteCopy + kSpritePlace + kPlaceAcross, toward_pivot(read_float(sprite + kSpritePlace + kPlaceAcross), g_pivot[0]));
    write_float(kSpriteCopy + kSpritePlace + kPlaceVertical, toward_pivot(read_float(sprite + kSpritePlace + kPlaceVertical), g_pivot[1]));
    write_float(kSpriteCopy + kSpriteSize, read_float(sprite + kSpriteSize) * kPanelScale);
    write_float(kSpriteCopy + kSpriteSize + 4, read_float(sprite + kSpriteSize + 4) * kPanelScale);
    c.r[5].sd[0] = static_cast<std::int32_t>(kSpriteCopy);
}

}
