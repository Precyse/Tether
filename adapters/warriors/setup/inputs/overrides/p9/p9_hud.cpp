#include "p9_hud.hpp"

#include "p9_game.hpp"
#include "p9_radar.hpp"
#include "ps2/system.hpp"

#include <cstring>

// Players 3 and 4 in the HUD. The HUD (0x00600840, 0x22600 bytes) holds every per-player part as an array of two, packed one behind the other, so a third entry
// cannot grow in place. Instead a second object of the same layout (HUD2, extra RAM) is built by the game's own HUD_Construct; its entry 0 and 1 are the
// widgets of players 3 and 4. The generated HUD code reaches an element as `hud + array + index * stride` (a mult, or a pointer stepped by the stride), and
// apply_p9.py routes those two operations here: index 2 and 3 become entry 0 and 1 of HUD2 by adding the distance between the objects.
namespace warriors::p9 {

namespace {

// Strides of the HUD's per-player arrays (hud.md) and of PlayerHUD's layout tables.
constexpr std::uint32_t kMashStride = 0x430, kLockPickStride = 0x540, kStereoStride = 0xB10, kStatusStride = 0x450, kPromptStride = 0x590, kRadarStride = 0x2940, kCrimePanelStride = 0xAB0;
constexpr std::uint32_t kPanelStride = 0x4A50;
constexpr std::uint32_t kLayoutStride = 0x240, kTallyStride = 0xC0;
constexpr std::uint32_t kPanelBaseStride = 0x10;
constexpr std::uint32_t kFirstExtraPanel = 2;
// Players 3 and 4 are stacked under players 1 and 2 on the shared screen (design\UI_SPEC.md 3.3): their panel bases are those of players 1 and 2 with y 0.13 lower.
constexpr std::uint32_t kPanelBases = 0x0050FA10, kBaseY = 8;
constexpr float kStackedY = 0.18f;

bool hud2_ready() { return ps2::ld32(kHudState) == kHudReady; }

void copy(std::uint32_t to, std::uint32_t from, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) ps2::st32(to + i, ps2::ld32(from + i));
}

bool extra_panel(std::uint32_t index) { return index >= kFirstExtraPanel && index < kMaxPlayers; }

std::uint32_t delta_of(std::uint32_t stride) {
    switch (stride) {
    case kMashStride:
    case kLockPickStride:
    case kStereoStride:
    case kStatusStride:
    case kPromptStride:
    case kRadarStride:
    case kCrimePanelStride:
    case kPanelStride: return kHud2Delta;
    case kLayoutStride:
    case kTallyStride: return kTableDelta;
    default: return 0;
    }
}

}

std::uint32_t panel_count() { return hud2_ready() ? kMaxPlayers : kOriginalPlayers; }
std::uint32_t last_panel() { return panel_count() - 1; }

void build_hud2(ps2::Cpu &c) {
    if (hud2_ready()) return;
    for (std::uint32_t i = 0; i < kHudSize; i += 4) ps2::st32(kHud2 + i, 0);
    ps2::call_guest(c, kHudConstruct, kHud2);
    copy(kCrew, kHud + kCrewOffset, kCrewBytes);
    ps2::st32(kHud + kHudPanelPointers + 2 * 4, ps2::ld32(kHud2 + kHudPanelPointers));
    ps2::st32(kHud + kHudPanelPointers + 3 * 4, ps2::ld32(kHud2 + kHudPanelPointers + 4));
    copy(kTableCopy, kTableFirst, kTableBytes);
    for (std::uint32_t i = 0; i < kOriginalPlayers; ++i) {
        const float y = kStackedY;
        std::uint32_t bits;
        std::memcpy(&bits, &y, sizeof bits);
        ps2::st32(kPanelBases + kTableDelta + i * kPanelBaseStride + kBaseY, bits);
    }
    ps2::st32(kHudState, kHudReady);
    radar_build(c);
    P9_TRACE("[p9] HUD2 built: panels %08x %08x", ps2::ld32(kHud + kHudPanelPointers + 8), ps2::ld32(kHud + kHudPanelPointers + 12));
}

std::uint32_t crew_widget(std::uint32_t hud) { return hud == kHud && hud2_ready() ? kCrew : hud + kCrewOffset; }

void hud_mult(ps2::Cpu &c, int pipe, std::uint32_t index, std::uint32_t stride) {
    const std::uint32_t delta = hud2_ready() && extra_panel(index) ? delta_of(stride) : 0;
    if (!delta && hud2_ready() && extra_panel(index)) P9_TRACE("[p9] hud_mult index %u stride %u not routed (ra %08x)", index, stride, c.r[31].w[0]);
    ps2::mmi::mult(c, pipe, delta ? index & 1 : index, stride);
    if (delta) c.lo.sd[pipe] = static_cast<std::int32_t>(c.lo.w[pipe * 2] + delta);
}

std::uint32_t panel_base_offset(std::uint32_t panel) {
    return hud2_ready() && extra_panel(panel) ? ((panel & 1) * kPanelBaseStride) + kTableDelta : panel * kPanelBaseStride;
}

std::uint32_t array_step(std::uint32_t pointer, std::uint32_t array_offset, std::uint32_t stride) {
    const std::uint32_t next = pointer + stride;
    return next == kHud + array_offset + 2 * stride ? kHud2 + array_offset : next;
}

}
