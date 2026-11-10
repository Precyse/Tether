#pragma once

#include "p9_view_data.hpp"

#include <cstdint>

namespace warriors::p9 {

// True when `camera`, standing `reach` metres back (at most the cap), would hold every member's feet and head inside `frame` (a share of the half picture, 1
// is the edge). The camera gives the direction it looks in; the members' centre is where it looks at.
bool fits_one_view(std::uint32_t camera, const Players &players, std::uint32_t members, float frame, float reach);
// How much of the half picture the members take in the camera as it is now, the worst of their feet and heads: 1 means a player on the edge.
float picture_share(std::uint32_t camera, const Players &players, std::uint32_t members);
// Whether the world keeps the camera from standing farther back, and how far back it stands.
bool camera_blocked(std::uint32_t camera);
float camera_distance(std::uint32_t camera);
// The follow camera of the combined view: takes the two members farthest apart as its targets (a follow camera has two target slots), looks at the centre of all members
// and sets the band's wanted near edge (the game's own zoom, follow_cam_zoom_ease) to the distance that keeps every member in frame, up to the cap.
void frame_cluster(std::uint32_t camera, std::uint32_t owner, const Players &players, std::uint32_t members, float cap, std::uint32_t &first, std::uint32_t &second);
// A camera that shows one player again: its band goes back to the standing default.
void release_framing(std::uint32_t camera, std::uint32_t owner);

}
