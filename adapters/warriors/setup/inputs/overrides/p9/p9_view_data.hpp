#pragma once

#include "p9_game.hpp"

#include <cstdint>

// The players the view code looks at in one frame (p9_views.cpp reads them, p9_framing.cpp frames them). `joined` are the players the game has (each has a view in a split
// screen); `viewable` those among them that a camera shows (a downed player does not count for the combined camera).
namespace warriors::p9 {

// The farthest the combined camera stands from its look-at point (metres): the game's own field of view stays, so this is how many players fit one picture. In a fight
// (a viewable player is engaged) the cap is kFightMaxDistance: a character is 1.9 m tall, and the picture is 2 * distance * 0.48 m high at that distance, so at 12.5 m he fills
// 15 % of it, the same as in the quarter of a split screen at the game's standing distance of 6.3 m (31 % of a quarter), where the view would otherwise go.
constexpr float kMaxCombinedDistance = 10.0f, kFightMaxDistance = 12.5f;
constexpr float kBandWidth = 0.5f;   // metres between the near and far edge of the follow camera's distance band (it stands anywhere in it)

struct Players {
    std::uint32_t count = 0, joined = 0, viewable = 0;   // list slots in the game, and the bit of each one that is a player / that a camera shows
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
