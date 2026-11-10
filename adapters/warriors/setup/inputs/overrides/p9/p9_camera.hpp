#pragma once

#include "p9_audio.hpp"
#include "ps2/generated.hpp"
#include "ps2/video.hpp"

#include <cstdint>

// Entry points the edited camera code calls (p9_edits_cam.py). The camera manager's per-player arrays hold four entries in extra RAM (p9_game.hpp, kCam).
namespace warriors::p9 {

constexpr float kHeadHeight = 1.9f;               // metres above a player's feet to the top of his head: what must stay in the picture, and what a camera must not get close to

// Takes over the words of a snapshot made before the arrays moved; the first thing every function that reads them does.
void ensure_cameras(ps2::Cpu &c);
// views_attach_player(slot): a player beyond the original two gets the camera switches of player 2 and widens the loops over the slots.
void views_attach_player(ps2::Cpu &c);
// The camera shown in view `view` (view_camera_lookup), 0 when there is none.
std::uint32_t player_view_camera(std::uint32_t view);
// view_camera_lookup(index) for an index the view list does not have (the game's code that treats a player's slot as a view index, such as the ground rings, asks for the
// camera of player slot 2 or 3 while the screen shows fewer views): answers with the camera of the view that shows that player (the lead view in COMBINED, his own in SPLIT),
// the game's first view where the views are the game's own (scripted cameras). True when answered; an index the list has is left to the original.
bool view_camera_for_slot(ps2::Cpu &c);
// How many slots the manager's two-entry loops cover, as the last index: 1 until a third player has a camera.
std::uint32_t cam_last_slot();
// views_wipe_slots: the slot count its loop clears; afterwards only the original two slots are in use.
std::uint32_t release_cam_slots();
// view_camera_drop(camera, force): a camera that is a further player's base camera is not released unless forced.
bool release_blocked(ps2::Cpu &c);
// The end of view_camera_drop: the further players' base and view entries that held the camera are cleared.
void release_extra_slots(std::uint32_t camera);

// views_choose_sharing: with three or more players in normal play the views are one combined view or one view per player (p9_views.cpp). True when handled.
bool decide_views(ps2::Cpu &c);
// follow_cam_collect_targets(camera): the targets of a follow camera under those views. True when handled.
bool gather_targets(ps2::Cpu &c);

// The split screen setting (p9_split_mode.cpp): true when the layout is held on one view per player. poll_split_request, once a frame, changes the setting when a pad
// holds the request chord (SELECT, L3 and R3) that was not held the frame before.
bool split_always();
void poll_split_request();

// Whether the view code made the decision for this frame: three or more players in normal play on follow cameras.
bool views_handled();
// The view (in view order) that player `slot` is shown in and his place among the players sharing it. False outside normal play with three or more players.
bool view_place(std::uint32_t slot, std::uint32_t &view, std::uint32_t &stack);
// player_panel_layout: with the screen split, a panel sits at the corner of its player's view (p9_panels.cpp).
void split_panel_base(ps2::Cpu &c, std::uint32_t player_hud);

// Once a frame: works out which part of the split screen the player of this machine sees in an online session and tells the window (ps2::set_view_probe, display
// only: it reads guest memory and makes no guest call, since what it looks for differs from machine to machine).
void publish_local_view();
// The window's probe: the rectangle published by publish_local_view; false while the whole picture is to be shown.
bool local_view(ps2::ViewRect &view);

// follow_cam_aim_point, after it averaged the targets: the combined camera looks at the centre of all the players it frames (p9_framing.cpp).
void cluster_look_at(std::uint32_t camera);

}
