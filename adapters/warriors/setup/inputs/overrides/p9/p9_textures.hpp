#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// The art of players 3 and 4 (overhead markers, radar sprites, name banners), put into the game's own resource tables when the first extra player is added.
// The texels come from the texture pack built from the player's disc (assets\tools\build_texture_pack.py); apply_p10.py puts calls to these into
// the generated resource, object type, HUD and radar code. Without the pack every function answers as the original game does.
namespace warriors::p9 {

// archive_open created the WAD index: the pack's new resource names get entries of their own, as copies of the entries they stand in for (see p9_textures.cpp).
void wad_index_created(std::uint32_t index);
// archive_index_lookup missed: the entry of one of the pack's names, or 0.
std::uint32_t wad_alias_entry(std::uint32_t hash);
// on_file_read: the file just read is replaced by the pack's version of it: a new name always, an existing file once the pack is in use.
void file_loaded(ps2::Cpu &c);
// on_object_list_read, on_sprite_sheet_table_read, on_sprite_page_read, object_type_register: the object list, the sheet table, the sheet's rectangles
// and the object types gain the pack's records.
void object_list_loaded(std::uint32_t resource_manager);
void sheet_table_loaded();
void page_loaded(std::uint32_t page);
void type_added(ps2::Cpu &c, std::uint32_t type_record);

// The tables and the sheet the pack extends are extended in place, once, when the first extra player is added. session_add_player calls extra_player_joining.
void retrofit_art(ps2::Cpu &c);
void extra_player_joining(ps2::Cpu &c);

// Overhead markers. The name the game passes to overhead_marker_show for a slot, the entry hook that maps "dyn_p_three"/"dyn_p_four" to the object types,
// the per-update upkeep of the markers of players 3 and 4 (rings_target_step), and floating_marker_setup's list of models that do not spin.
std::uint32_t overhead_icon_name(std::uint32_t slot);
void overhead_icon_request(ps2::Cpu &c);
void sync_extra_markers(ps2::Cpu &c);
bool player_marker_hash(std::uint32_t model_hash);

// Name banner: the sheet record player_panel_set_title draws, for the panel of the PlayerPanel object (PLAYER 1/2 records become PLAYER 3/4 for panels 2 and 3).
std::uint32_t banner_record(std::uint32_t record, std::uint32_t player_hud);

// Radar: the arrow sprite of a player's blip, which player an arrow sprite belongs to, the partner tag a radar shows in place of another player's arrow
// (true when it handled the blip), and whether a sprite is one of the new tags.
std::uint32_t radar_arrow_sprite(std::uint32_t slot);
std::uint32_t arrow_player(std::uint32_t sprite);
bool radar_extra_tag(std::uint32_t radar, std::uint32_t widget);
bool radar_is_new_tag(std::uint32_t sprite);

}
