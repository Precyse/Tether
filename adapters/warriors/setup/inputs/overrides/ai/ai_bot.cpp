#include "ai_game.hpp"
#include "ai_hooks.hpp"

#include "ps2/system.hpp"

#include <cmath>
#include <cstring>

// Test aid, off unless PS2_AI_BOT=<first frame> is set: from that frame every player is a brawler that closes with the nearest enemy and attacks, so fights with 2 to 4 players
// can be compared (the random scripted pads of soak_args.py rarely reach anyone). A player more than a few metres from the nearest standing enemy is put beside it with the game's own
// Human_TeleportNear, then his pad (the runtime's pad of his player slot) presses combinations of the attack buttons; the game's auto lock faces the enemy. Local runs only: it writes pads
// from inside the game, so it is for one process, never for a session.
namespace warriors::ai {

namespace {

constexpr std::uint32_t kTeleportNear = 0x0023B138;
constexpr float kReachSquared = 3.0f * 3.0f;
constexpr std::uint64_t kTeleportEvery = 90, kTeleportSpread = 23, kCombo = 6, kPressFrames = 4;
constexpr std::uint16_t kTriangle = 1u << 12, kCircle = 1u << 13, kCross = 1u << 14, kSquare = 1u << 15;
constexpr std::uint16_t kCombos[] = {kSquare, kSquare, kCross, kSquare, kTriangle, kSquare, kCircle, kCross};
constexpr std::int32_t kStandingAbove = 1;   // the script leaves a beaten fighter at 1 health

float position(std::uint32_t human, std::uint32_t axis) {
    const std::uint32_t word = ps2::ld32(kPositions + static_cast<std::uint32_t>(s16(human + kPositionIndexOffset)) * kPositionStride + axis * 4);
    float value;
    std::memcpy(&value, &word, sizeof value);
    return value;
}

float distance_squared(std::uint32_t a, std::uint32_t b) {
    float sum = 0;
    for (std::uint32_t axis = 0; axis < 3; ++axis) sum += (position(a, axis) - position(b, axis)) * (position(a, axis) - position(b, axis));
    return sum;
}

// The nearest enemy (gang soldier or cop) of `human` that is standing, or 0.
std::uint32_t nearest_enemy(std::uint32_t human) {
    std::uint32_t best = 0;
    float best_distance = 0;
    for (std::uint32_t i = 0; i < p9::kHumanCount; ++i) {
        const std::uint32_t other = p9::human_at(i);
        if (ps2::ld32(other + p9::kHandleOffset) == 0xFFFFFFFFu || s8(other + kSlotOffset) != -1) continue;
        const std::uint32_t type = ps2::ld32(kBrains + i * kBrainStride + kBrainTypeOffset);
        if (type != kBrainGangSoldier && type != kBrainCop) continue;
        if (s16(ps2::ld32(other + kCombatRecordOffset) + 0x144) <= kStandingAbove) continue;
        const float distance = distance_squared(human, other);
        if (!best || distance < best_distance) best = other, best_distance = distance;
    }
    return best;
}

}

void bot_frame(ps2::Cpu &c) {
    static const char *text = std::getenv("PS2_AI_BOT");
    const std::uint64_t frame = ps2::frame_count();
    if (!text || frame < std::strtoull(text, nullptr, 10)) return;
    for (int slot = 0; slot < players() && slot < ps2::kPadCount; ++slot) {
        const std::uint32_t human = p9::resolve_handle(c, ps2::ld32(p9::game() + p9::kPlayerHandlesOffset + static_cast<std::uint32_t>(slot) * 4));
        if (!human) continue;
        const std::uint32_t enemy = nearest_enemy(human);
        if (enemy && distance_squared(human, enemy) > kReachSquared && (frame + static_cast<std::uint64_t>(slot) * kTeleportSpread) % kTeleportEvery == 0)
            ps2::call_guest(c, kTeleportNear, ps2::ld32(human + p9::kHandleOffset), ps2::ld32(enemy + p9::kHandleOffset));
        const bool press = enemy && frame % kCombo < kPressFrames;
        ps2::set_pad_buttons(press ? kCombos[(frame / kCombo + static_cast<std::uint64_t>(slot)) % (sizeof kCombos / sizeof kCombos[0])] : 0, slot);
    }
}

}
