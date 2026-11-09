#pragma once

#include "p9_audio.hpp"
#include "ps2/generated.hpp"
#include "ps2/video.hpp"

#include <cstdint>

// Entry points the edited camera code calls (p9_edits_cam.py). The camera manager's per-player arrays hold four entries in extra RAM (p9_game.hpp, kCam).
namespace warriors::p9 {

// Takes over the words of a snapshot made before the arrays moved; the first thing every function that reads them does.
void ensure_cameras(ps2::Cpu &c);
// Cameras_AddPlayer(slot): a player beyond the original two gets the camera switches of player 2 and widens the loops over the slots.
void cameras_add_player(ps2::Cpu &c);
// The camera shown in view `view` (Camera_GetPlayerCamera), 0 when there is none.
std::uint32_t player_view_camera(std::uint32_t view);
// How many slots the manager's two-entry loops cover, as the last index: 1 until a third player has a camera.
std::uint32_t cam_last_slot();
// Cameras_ClearSlots: the slot count its loop clears; afterwards only the original two slots are in use.
std::uint32_t release_cam_slots();
// Camera_Release(camera, force): a camera that is a further player's base camera is not released unless forced.
bool release_blocked(ps2::Cpu &c);
// The end of Camera_Release: the further players' base and view entries that held the camera are cleared.
void release_extra_slots(std::uint32_t camera);

// Cameras_DecideViews: with three or more players in normal play the views follow the clusters of players who stay together. True when handled.
bool decide_views(ps2::Cpu &c);
// Cam_Follow_GatherTargets(camera): the targets of a follow camera under those views. True when handled.
bool gather_targets(ps2::Cpu &c);

// The cap on the follow camera's field of view easing (float bits, degrees per second): the game's 7.5, or 30 with three or more players, whose framing widens the
// field of view when a wall keeps the camera from standing back (p9_framing.cpp).
std::uint32_t fov_rate_cap();
// The view (in view order) that player `slot` is shown in and his place among the players sharing it. False outside normal play with three or more players.
bool view_place(std::uint32_t slot, std::uint32_t &view, std::uint32_t &stack);
// PlayerHUD_Update: with the screen split, a panel sits at the corner of its player's view (p9_panels.cpp).
void split_panel_base(ps2::Cpu &c, std::uint32_t player_hud);

// Once a frame: works out which part of the split screen the player of this machine sees in an online session and tells the window (ps2::set_view_probe, display
// only: it reads guest memory and makes no guest call, since what it looks for differs from machine to machine).
void publish_local_view();
// The window's probe: the rectangle published by publish_local_view; false while the whole picture is to be shown.
bool local_view(ps2::ViewRect &view);

// Cam_Follow_LookAt, after it averaged the targets: a camera that frames a cluster looks at the centre of the whole cluster (p9_framing.cpp).
void cluster_look_at(std::uint32_t camera);

}
