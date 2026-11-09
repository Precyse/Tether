#pragma once

#include "p9_view_data.hpp"

#include <cstdint>

namespace warriors::p9 {

// The follow camera of a view that shows two or more players: takes the two players at the sides of the picture as its targets (its look-at point is their midpoint),
// sets the band's wanted near edge (the game's own zoom, Cam_Follow_EaseBand) to the distance that keeps every player in frame, up to a limit (a larger one in a level
// that forces a single view), and the wanted field of view when the world keeps the camera from standing that far back.
void frame_cluster(std::uint32_t camera, std::uint32_t owner, const Players &players, std::uint32_t members, std::uint32_t &first, std::uint32_t &second);
// A camera that shows one player again: its band goes back to the standing default.
void release_framing(std::uint32_t camera, std::uint32_t owner);

}
