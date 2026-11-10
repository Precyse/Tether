#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// Entry points the generated code calls (apply_ai.py puts them in place of the load or test they stand for; each returns the original value when fewer than three players are in).
namespace warriors::ai {

// crew_spawner_step: a spawner's limit of living members (negative: the size of a wave). crew_player_info_refresh: the game's spawn maximum.
std::int32_t spawner_cap(std::uint32_t gang, std::int32_t cap);
std::int32_t spawn_max(std::int32_t max);

// damage_commit: the damage about to be taken off. A player hit by a player takes none while the host's friendly fire switch is on; with three or four players a gang soldier, cop or
// boss hit from the players' side takes the damage divided by the health percentage.
std::int32_t enemy_damage(std::uint32_t victim, std::uint32_t foe, std::uint32_t record, std::int32_t damage);

// Test aid (PS2_AI_BOT=<first frame>), once per frame at pads_frame_step: every player brawls (ai_bot.cpp).
void bot_frame(ps2::Cpu &c);

// session_difficulty_read: one difficulty step up for a session of three players, two for four (ai_difficulty.cpp). True when it answered.
bool profile_difficulty(ps2::Cpu &c);

// script_score_query: the score the level scripts read for player 2 is the sum of players 2 to 4 with three or four players in (the rank sum of global.lua adds player and player2). True when it answered.
bool stat_score(ps2::Cpu &c);

// damage_commit: true when a boss's hit armour also holds against the hits of players (three or four players in the game).
bool boss_armour_vs_players(std::uint32_t victim);

// The host's friendly fire switch (guest word kFriendlyFireOff).
bool friendly_fire_off();

// player_ai_step (the weapon swap prompt): of the partner found so far and a further player, the nearer one to `self`.
std::uint32_t nearer_partner(std::uint32_t self, std::uint32_t found, std::uint32_t candidate);

// pads_frame_step (once per frame): players 3 and 4 take player 2's mugging callback.
void share_mug_callback(ps2::Cpu &c);

// turret_goal_choose_victim: how many players the shooter picks among (the original's two with fewer than three players).
std::uint32_t shooter_choices();

}
