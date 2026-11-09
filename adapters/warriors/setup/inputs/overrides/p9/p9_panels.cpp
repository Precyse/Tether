#include "p9_camera.hpp"

#include "p9_game.hpp"
#include "p9_world.hpp"
#include "netplay/netplay.hpp"
#include "ps2/system.hpp"

#include <atomic>
#include <cstring>

// HUD panels in a split screen. In the shared screen the layout table puts the panels of players 1 and 2 at the top-left and top-right and those of players 3 and 4
// under them. With the screen split into views (three or more players) a panel belongs in the corner of its player's view instead: the left column of views anchors
// its panels at the left edge, the right column at the right edge, each row of views starts one row of the screen lower, and the panels of players who share a view
// stack downwards in player order.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kPlayerHudPanel = 0x40F4, kPlayerHudBase = 0x40E0;     // PlayerHUD: the panel it draws, and its base position (x, y, z, w floats)
constexpr std::uint32_t kPanelBases = 0x0050FA10, kPanelBaseStride = 0x10;       // PlayerHUD's layout bases: player 0 top-left, player 1 top-right
constexpr std::uint32_t kViewViews = 0x0050B198, kViewColumns = 0x0050B1A8, kViewRows = 0x0050B1AA;
constexpr std::uint32_t kHandleResolve = 0x00390268, kHandleScratch = kCamViewState + 0x48;
constexpr float kPanelStack = 0.13f;                                            // vertical distance of two panels that share a view (the shared screen's)

std::uint32_t panel_slot(ps2::Cpu &c, std::uint32_t panel) {
    for (std::uint32_t slot = 0; slot < player_count() && slot < kMaxPlayers; ++slot) {
        ps2::st32(kHandleScratch, ps2::ld32(game() + kPlayerHandlesOffset + slot * 4));
        const std::uint32_t human = ps2::call_guest(c, kHandleResolve, kHandleScratch);
        if (human && ps2::ld32(human + kPanelOffset) == panel) return slot;
    }
    return kMaxPlayers;
}

// The view of the player of this machine, for the window: left, top, right and bottom as shares of the picture, and whether to cut the picture at all.
std::atomic<float> g_local_view[4];
std::atomic<bool> g_local_view_cut{false};
constexpr std::uint32_t kPlayerPadRecordStep = 4;   // the game's second port is pad record 4; players 3 and 4 are records 1 and 2 (p9_pads.cpp)

std::uint32_t pad_record_of_port(int port) {
    if (port == 0) return 0;
    return port == 1 ? kPlayerPadRecordStep : static_cast<std::uint32_t>(port - kFirstExtraPad) + 1;
}

// The human a handle names, found in the human table without a guest call (the machines of a session ask different questions here, and a guest call costs cycles).
std::uint32_t human_of_handle(std::uint32_t handle) {
    for (std::uint32_t i = 0; i < kHumanCount; ++i)
        if (handle_of(human_at(i)) == handle) return human_at(i);
    return 0;
}

float read_float(std::uint32_t at) {
    const std::uint32_t bits = ps2::ld32(at);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void write_float(std::uint32_t at, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    ps2::st32(at, bits);
}

}

void split_panel_base(ps2::Cpu &c, std::uint32_t player_hud) {
    if (player_count() <= kOriginalPlayers || ps2::ld32(kViewViews) < 2) return;
    const std::uint32_t columns = ps2::ld16(kViewColumns), rows = ps2::ld16(kViewRows);
    std::uint32_t view, stack;
    if (!columns || !rows || !view_place(panel_slot(c, ps2::ld32(player_hud + kPlayerHudPanel)), view, stack)) return;
    const std::uint32_t column = view % columns, row = view / columns;
    const std::uint32_t anchor = kPanelBases + (column ? kPanelBaseStride : 0);
    write_float(player_hud + kPlayerHudBase, read_float(anchor));
    write_float(player_hud + kPlayerHudBase + 4, read_float(kPanelBases + 4) + static_cast<float>(row) / static_cast<float>(rows) + static_cast<float>(stack) * kPanelStack);
}

void publish_local_view() {
    const int port = ps2::netplay::local_port();
    const std::uint32_t views = ps2::ld32(kViewViews), columns = ps2::ld16(kViewColumns), rows = ps2::ld16(kViewRows);
    bool cut = false;
    if (ps2::netplay::locked() && game() && views >= 2 && columns && rows) {
        const std::uint32_t record = pad_record_of_port(port);
        for (std::uint32_t slot = 0; slot < player_count() && slot < kMaxPlayers && !cut; ++slot) {
            const std::uint32_t human = human_of_handle(ps2::ld32(game() + kPlayerHandlesOffset + slot * 4));
            std::uint32_t view = slot, stack;
            if (!human || ps2::ld8(record_of(human) + kRecordPadOffset) != record) continue;
            if (!view_place(slot, view, stack) && !(player_count() == kOriginalPlayers && views == kOriginalPlayers)) continue;
            const float column = static_cast<float>(view % columns), row = static_cast<float>(view / columns);
            const float width = 1.0f / static_cast<float>(columns), height = 1.0f / static_cast<float>(rows);
            g_local_view[0] = column * width;
            g_local_view[1] = row * height;
            g_local_view[2] = (column + 1) * width;
            g_local_view[3] = (row + 1) * height;
            cut = true;
        }
    }
    g_local_view_cut = cut;
}

bool local_view(ps2::ViewRect &view) {
    if (!g_local_view_cut) return false;
    view = {g_local_view[0], g_local_view[1], g_local_view[2], g_local_view[3]};
    return true;
}

}
