#pragma once

#include "p9_game.hpp"
#include "ps2/generated.hpp"

#include <cstdint>

// Reads of the game's live tables shared by the join and leave code: the human and brain tables, the player records, and the question "is the game in plain play".
namespace warriors::p9 {

constexpr std::uint32_t kHumans = 0x00640C80, kHumanStride = 0x6D0, kHumanCount = 60, kHandleOffset = 0x90;
constexpr std::uint32_t kPlayerRecords = 0x00660F50, kPlayerRecordStride = 0x2C, kRecordPadOffset = 0x19;
constexpr std::uint32_t kPositions = 0x00714B00, kPositionStride = 0x20, kPositionIndexOffset = 0x92;   // the game keeps a human's position in a table, indexed by an s16 of the human
constexpr std::uint32_t kArrivalWord = kBase + 0x10;      // bit per list slot (2 and 3): the extra players who joined and wait to be placed beside the leader (p9_join.cpp)
constexpr std::uint32_t kArrivalFrame = kBase + 0x18;     // the frame of that join
constexpr std::uint8_t kNone = 0xFF;   // s8 -1: no player slot, no pad

inline std::uint32_t human_at(std::uint32_t i) { return kHumans + i * kHumanStride; }
inline std::uint32_t human_index(std::uint32_t human) { return (human - kHumans) / kHumanStride; }
inline std::uint32_t handle_of(std::uint32_t human) { return ps2::ld32(human + kHandleOffset); }
inline std::uint32_t record_of(std::uint32_t human) { return kPlayerRecords + human_index(human) * kPlayerRecordStride; }

// Where a human stands (x, y, z of the game's position table), and the straight distance between two humans.
void human_position(std::uint32_t human, float (&out)[3]);
float human_distance(std::uint32_t a, std::uint32_t b);
// True while the human is bound to a scene (cut scene or scripted move).
bool in_scene(ps2::Cpu &c, std::uint32_t human);

// The object a handle names (handle_to_object), 0 when it is gone.
std::uint32_t resolve_handle(ps2::Cpu &c, std::uint32_t handle);
// The kind of camera view 0 shows (2 is the follow camera; the scenes of a join use others).
std::uint32_t camera_type(ps2::Cpu &c);
// The game's own gate of a START join: the level flow is idle, the leader is in no scene and some view shows a follow, rail or plain camera (a chase is a rail section).
bool join_gate_open(ps2::Cpu &c);
// Level running, level flow idle, the leader in no scene and the follow camera up: when a player may join, be placed or leave without a scene on screen.
bool normal_play(ps2::Cpu &c);

}
