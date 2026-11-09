#include "p9_camera.hpp"
#include "p9_game.hpp"
#include "p9_hooks.hpp"
#include "p9_flow.hpp"
#include "p9_hud.hpp"
#include "p9_inventory.hpp"
#include "ps2/system.hpp"
#include "warriors_display.hpp"

namespace warriors::p9 {

namespace {

constexpr std::uint32_t kThrowArcClear = 0x0018F230, kThrowArcCreate = 0x0018F268;   // ThrowArc_Clear / ThrowArc_Create(arc)
constexpr std::uint32_t kAreaSize = 0x2000;

void zero(std::uint32_t at, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) ps2::st32(at + i, 0);
}

void copy(std::uint32_t to, std::uint32_t from, std::uint32_t bytes) {
    for (std::uint32_t i = 0; i < bytes; i += 4) ps2::st32(to + i, ps2::ld32(from + i));
}

}

// The storage of players 3 and 4 starts empty with each game state (a new level builds a new heap, so the arcs of the last one are gone).
void game_construct(ps2::Cpu &) {
    zero(kExtraStart, kAreaSize - (kExtraStart - kBase));
    ps2::st32(kMagic, kMagicValue);
}

// A snapshot made before the storage moved holds the tick counter and the two arcs inside the game state block: they are taken over once.
void ensure_area(ps2::Cpu &) {
    if (!game() || ps2::ld32(kMagic) == kMagicValue) return;
    zero(kExtraStart, kAreaSize - (kExtraStart - kBase));
    ps2::st32(kMagic, kMagicValue);
    ps2::st32(kTick, ps2::ld32(game() + kTickOffset));
    ps2::st32(kField234, ps2::ld32(game() + kTickOffset + 4));
    copy(kThrowArcs, game() + kThrowArcsOffset, kOriginalPlayers * kThrowArcSize);
}

namespace {
std::uint64_t g_last_traced = 0;   // the game's frames can step by one or by two: the trace prints once per ten; it reads memory only (a guest call would change the cycles)
}

void frame_start(ps2::Cpu &c) {
    install_display_hints();
    ensure_area(c);
    place_new_players(c);
    leave_check(c);
    flow_frame(c);
    publish_local_view();
    if (tracing() && ps2::frame_count() / 10 != g_last_traced && game()) {
        g_last_traced = ps2::frame_count() / 10;
        const std::uint32_t fade = ps2::ld32(0x005FDEB8);
        P9_TRACE("[p9] frame %llu views %u flow %u fade %u/%08x players %u", static_cast<unsigned long long>(ps2::frame_count()), ps2::ld32(0x0050B198), ps2::ld32(game() + 0x14C), ps2::ld32(fade + 0x1E8), ps2::ld32(fade + 0x1EC), player_count());
    }
}

// The arc of list slot 2 or 3 is built the way GameState_Construct builds the first two (cleared, then created), when its player is added.
bool player_list_add(ps2::Cpu &c) {
    const std::uint32_t slot = ps2::ld16(c.r[4].w[0] + kPlayerCountOffset);
    if (slot < kOriginalPlayers || slot >= kMaxPlayers) return false;
    build_hud2(c);
    const std::uint32_t arc = kThrowArcs + slot * kThrowArcSize;
    if (ps2::ld32(arc)) return false;   // already built for this game state
    ps2::call_guest(c, kThrowArcClear, arc);
    ps2::call_guest(c, kThrowArcCreate, arc);
    inventory_init_slot(slot);
    return false;
}

void trace_players(ps2::Cpu &) {
    if (!tracing() || ps2::frame_count() < 29750 || ps2::frame_count() > 29756) return;
    for (std::uint32_t slot = 0; slot < player_count(); ++slot) {
        const std::uint32_t handle = ps2::ld32(game() + kPlayerHandlesOffset + slot * 4);
        P9_TRACE("[p9] slot %u handle %08x", slot, handle);
        for (std::uint32_t i = 0; i < 60; ++i) {
            const std::uint32_t human = 0x00640C80 + i * 0x6D0;
            if (ps2::ld32(human + 0x90) != handle) continue;
            const std::uint32_t state = ps2::ld32(human + 0xD4);
            P9_TRACE("[p9] %llu slot %u human %08x state %08x flags %08x%08x", static_cast<unsigned long long>(ps2::frame_count()), slot, human, state, state ? ps2::ld32(state + 4) : 0u, state ? ps2::ld32(state) : 0u);
        }
    }
}

void trace_call(ps2::Cpu &c, const char *name) {
    if (c.r[4].w[0] == 0x5DD860u) P9_TRACE("  record1 ring=%u held=%04x owner=%04x plugged=%u", ps2::ld32(c.r[4].w[0] + 0x2C), ps2::ld16(c.r[4].w[0] + 0x1C + ps2::ld32(c.r[4].w[0] + 0x2C) * 2), ps2::ld16(c.r[4].w[0] + 0x42), ps2::ld32(c.r[4].w[0] + 0x4C));
    P9_TRACE("[p9] %lld %s(%08x, %08x, %08x) players=%u ra=%08x", static_cast<long long>(ps2::frame_count()), name, c.r[4].w[0], c.r[5].w[0], c.r[6].w[0], player_count(), c.r[31].w[0]);
}

std::uint32_t throw_arc_offset() { return kThrowArcs - game(); }

// `list slot * 0x5c` of the functions that address the player state blocks inline (Human_UpdateActions, Human_AddRage, Rage_NoteHit, GameState_ResetForLevel): the
// blocks of slots 2 and 3 are not at game + 0x168 + slot * 0x5c (that is the handle list) but in the phase 9 area.
void state_mult(ps2::Cpu &c, int pipe, std::uint32_t slot, std::uint32_t stride) {
    ps2::mmi::mult(c, pipe, slot, stride);
    if (stride != kPlayerStateSize || slot < kOriginalPlayers || slot >= kMaxPlayers || slot >= player_count()) return;
    const std::uint32_t block = kPlayerStates + (slot - kOriginalPlayers) * kPlayerStateSize;
    c.lo.sd[pipe] = static_cast<std::int32_t>(block - kPlayerStatesOffset - game());
}

bool player_state(ps2::Cpu &c) {
    const std::uint32_t human = c.r[4].w[0];
    const std::int32_t panel = static_cast<std::int32_t>(ps2::ld32(human + kPanelOffset));
    if (ps2::ld8(human + kPlayerSlotOffset) == 0xFF || panel < kOriginalPlayers || panel >= kMaxPlayers) return false;
    c.r[2].sd[0] = static_cast<std::int32_t>(kPlayerStates + static_cast<std::uint32_t>(panel - kOriginalPlayers) * kPlayerStateSize);
    c.pc = c.r[31].w[0];
    return true;
}

}
