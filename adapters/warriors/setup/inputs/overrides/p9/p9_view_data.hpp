#pragma once

#include "p9_game.hpp"

#include <cstdint>

// The players the view code looks at in one frame (p9_views.cpp reads them, p9_framing.cpp frames them).
namespace warriors::p9 {

struct Players {
    std::uint32_t count = 0, viewable = 0;      // list slots in the game, and the bit of each one a camera shows
    std::uint32_t human[kMaxPlayers] = {};
    float position[kMaxPlayers][3] = {};        // feet, x y z (z up)
};

inline float distance_sq(const Players &players, std::uint32_t a, std::uint32_t b) {
    float sum = 0;
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        const float d = players.position[a][axis] - players.position[b][axis];
        sum += d * d;
    }
    return sum;
}

}
