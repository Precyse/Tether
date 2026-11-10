#include "p9_radar.hpp"

#include "p9_game.hpp"
#include "p9_hud.hpp"
#include "ps2/guest_call.hpp"
#include "ps2/system.hpp"

namespace warriors::p9 {

namespace {

// The HUD's radars and crime panels (hud.md): offsets in the HUD, strides between players, fields of a radar.
constexpr std::uint32_t kRadarOffset = 0x15D0, kRadarStride = 0x2940, kCrimeOffset = 0x177D0, kCrimeStride = 0xAB0;
constexpr std::uint32_t kRadarCreated = 0x00, kRadarShown = 0x08;
constexpr std::uint32_t kRadarPosition = 0x2930, kRadarPositionBytes = 16, kRadarZoom = 0x2920, kRadarRangeNear = 0x20, kRadarRangeFar = 0x24, kRadarRangeMax = 0x28;
constexpr std::uint32_t kListenerStride = 0x20;                 // level_driver_step's audio listener: position and orientation of a player's camera
constexpr std::uint32_t kFirstExtraRadar = 2;                   // players 3 and 4: radars 2 and 3, entries 0 and 1 of HUD2
constexpr std::uint32_t kRadarsPerHud = 2;
constexpr std::uint32_t kItemsPerRadar = 2;                    // the radar and its crime panel

// The game's functions (decomp\names.tsv).
constexpr std::uint32_t kMinimapWidgetInit = 0x001C3DE0, kMinimapWidgetFree = 0x001C41A0, kMinimapWidgetExists = 0x001C42E0, kMinimapWidgetBlipsOff = 0x001C42E8;
constexpr std::uint32_t kMinimapWidgetHide = 0x001C4448, kMinimapWidgetShow = 0x001C4388, kMinimapWidgetStep = 0x001C5210, kMinimapWidgetBlipKind = 0x001C4A40;
constexpr std::uint32_t kWantedPanelInit = 0x001AA290, kWantedPanelFree = 0x001AA5C0, kWantedPanelPlace = 0x001AA630, kWantedPanelActive = 0x001AA6B8;
constexpr std::uint32_t kWantedPanelHide = 0x001AA6C0, kWantedPanelStep = 0x001AA9B0;
constexpr std::uint32_t kMinimapEnable = 0x001B2610, kMinimapDisable = 0x001B2658;
constexpr std::uint32_t kLuaBoth = 2;                           // minimap_show_all / Off(2): both players

std::uint32_t radar_of(std::uint32_t hud, std::uint32_t k) { return hud + kRadarOffset + k * kRadarStride; }
std::uint32_t crime_of(std::uint32_t hud, std::uint32_t k) { return hud + kCrimeOffset + k * kCrimeStride; }

bool radars_exist() { return panel_count() > kOriginalPlayers && ps2::ld32(radar_of(kHud2, 0) + kRadarCreated) != 0; }

// A radar centres on the human of its list slot: it is updated and drawn only while that player is in the game.
bool in_game(std::uint32_t k) { return kFirstExtraRadar + k < player_count(); }

void copy_position(std::uint32_t k) {
    for (std::uint32_t i = 0; i < kRadarPositionBytes; i += 4) ps2::st32(radar_of(kHud2, k) + kRadarPosition + i, ps2::ld32(radar_of(kHud, k) + kRadarPosition + i));
}

// The same call as the one being made, for HUD2: arguments a0-a3, t0-t3 and the float in f12 as they are now, a0 replaced.
void repeat_for_hud2(ps2::Cpu &c, std::uint32_t function, std::uint32_t a0) {
    ps2::GuestArgs call;
    for (int i = 0; i < 8; ++i) call.a[i] = c.r[4 + i].w[0];
    call.f12 = c.f[12];
    call.a[0] = a0;
    ps2::call_guest(c, function, call);
}

}

void radar_build(ps2::Cpu &c) {
    if (panel_count() <= kOriginalPlayers) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k) {
        const std::uint32_t player = kFirstExtraRadar + k;
        if (!ps2::call_guest(c, kMinimapWidgetExists, radar_of(kHud2, k))) {
            ps2::call_guest(c, kMinimapWidgetInit, radar_of(kHud2, k), player);
            copy_position(k);
        }
        if (!ps2::call_guest(c, kWantedPanelActive, crime_of(kHud2, k))) ps2::call_guest(c, kWantedPanelInit, crime_of(kHud2, k), player);
    }
    P9_TRACE("[p9] HUD2 radars %08x %08x crime panels %08x %08x", radar_of(kHud2, 0), radar_of(kHud2, 1), crime_of(kHud2, 0), crime_of(kHud2, 1));
}

void minimap_widget_free(ps2::Cpu &c) {
    if (panel_count() <= kOriginalPlayers) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k) {
        if (ps2::call_guest(c, kMinimapWidgetExists, radar_of(kHud2, k))) ps2::call_guest(c, kMinimapWidgetFree, radar_of(kHud2, k));
        if (ps2::call_guest(c, kWantedPanelActive, crime_of(kHud2, k))) ps2::call_guest(c, kWantedPanelFree, crime_of(kHud2, k));
    }
}

void radar_mirror(ps2::Cpu &c, std::uint32_t function) {
    if (c.r[4].w[0] == kHud && radars_exist()) repeat_for_hud2(c, function, kHud2);
}

void radar_mirror_blip_type(ps2::Cpu &c) {
    if (c.r[4].w[0] != radar_of(kHud, 1) || !radars_exist()) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k) repeat_for_hud2(c, kMinimapWidgetBlipKind, radar_of(kHud2, k));
}

void radar_range(ps2::Cpu &c) {
    if (c.r[4].w[0] != kHud || !radars_exist()) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k) {
        ps2::st32(radar_of(kHud2, k) + kRadarRangeNear, c.f[12]);
        ps2::st32(radar_of(kHud2, k) + kRadarRangeMax, c.f[12]);
        ps2::st32(radar_of(kHud2, k) + kRadarRangeFar, c.f[13]);
    }
}

void radar_zoom(ps2::Cpu &c) {
    if (!radars_exist()) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k) ps2::st32(radar_of(kHud2, k) + kRadarZoom, c.f[12]);
}

void radar_toggle(ps2::Cpu &c, bool enable, bool only_all) {
    if (!radars_exist() || (only_all && c.r[4].w[0] != kLuaBoth)) return;
    for (std::uint32_t player = kFirstExtraRadar; player < static_cast<std::uint32_t>(kMaxPlayers); ++player) ps2::call_guest(c, enable ? kMinimapEnable : kMinimapDisable, kHud, player);
}

void radar_hide_blips(ps2::Cpu &c) {
    if (!radars_exist()) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k) {
        ps2::call_guest(c, kMinimapWidgetBlipsOff, radar_of(kHud2, k));
        ps2::call_guest(c, kWantedPanelHide, crime_of(kHud2, k));
    }
}

void radar_hide_for_menu(ps2::Cpu &c) {
    if (!radars_exist()) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k) ps2::call_guest(c, kMinimapWidgetHide, radar_of(kHud2, k));
}

void radar_shared_hide(ps2::Cpu &c) {
    if (!radars_exist()) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k)
        if (ps2::ld32(radar_of(kHud2, k) + kRadarShown)) ps2::call_guest(c, kMinimapWidgetHide, radar_of(kHud2, k));
}

void radar_split_update(ps2::Cpu &c, std::uint32_t listener0) {
    if (!radars_exist()) return;
    for (std::uint32_t k = 0; k < kRadarsPerHud; ++k) {
        const std::uint32_t radar = radar_of(kHud2, k), crime = crime_of(kHud2, k);
        if (!in_game(k)) {
            if (ps2::ld32(radar + kRadarShown)) ps2::call_guest(c, kMinimapWidgetHide, radar);
            continue;
        }
        copy_position(k);
        ps2::call_guest(c, kWantedPanelPlace, crime, radar + kRadarPosition);
        if (!ps2::ld32(radar + kRadarShown)) ps2::call_guest(c, kMinimapWidgetShow, radar);
        ps2::call_guest(c, kMinimapWidgetStep, radar, listener0 + (kFirstExtraRadar + k) * kListenerStride);
        ps2::call_guest(c, kWantedPanelStep, crime);
    }
}

void radar_render_reset() { ps2::st32(kRadarDrawNext, 0); }

std::uint32_t radar_render_next() {
    if (!radars_exist()) return 0;
    for (std::uint32_t item = ps2::ld32(kRadarDrawNext) + 1; item <= kRadarsPerHud * kItemsPerRadar; ++item) {
        if (!in_game((item - 1) / kItemsPerRadar)) continue;
        ps2::st32(kRadarDrawNext, item);
        return item;
    }
    ps2::st32(kRadarDrawNext, kRadarsPerHud * kItemsPerRadar);
    return 0;
}

std::uint32_t radar_render_target(std::uint32_t item) {
    const std::uint32_t k = (item - 1) / kItemsPerRadar;
    return item % kItemsPerRadar ? radar_of(kHud2, k) : crime_of(kHud2, k);
}

}
