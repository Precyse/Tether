#include "ai_game.hpp"
#include "ai_hooks.hpp"

#include <cstring>

// Rules that counted two players: friendly fire, the weapon swap prompt and the stationary shooter's pick of a target.
namespace warriors::ai {

namespace {

float position_component(std::uint32_t human, std::uint32_t axis) {
    const std::uint32_t word = ps2::ld32(kPositions + static_cast<std::uint32_t>(s16(human + kPositionIndexOffset)) * kPositionStride + axis * 4);
    float value;
    std::memcpy(&value, &word, sizeof value);
    return value;
}

float distance_squared(std::uint32_t a, std::uint32_t b) {
    float sum = 0;
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        const float d = position_component(a, axis) - position_component(b, axis);
        sum += d * d;
    }
    return sum;
}

}

bool friendly_fire_off() { return ps2::ld32(kFriendlyFireOff) != 0; }

std::uint32_t nearer_partner(std::uint32_t self, std::uint32_t found, std::uint32_t candidate) {
    if (!found || distance_squared(self, candidate) < distance_squared(self, found)) return candidate;
    return found;
}

std::uint32_t shooter_choices() {
    const int count = players();
    return count > p9::kOriginalPlayers ? static_cast<std::uint32_t>(count) : static_cast<std::uint32_t>(p9::kOriginalPlayers);
}

}
