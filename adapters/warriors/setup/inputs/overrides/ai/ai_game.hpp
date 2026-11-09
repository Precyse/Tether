#pragma once

#include "p9_game.hpp"
#include "p9_world.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Co-op enemies (3 to 4 players): addresses of the game structures the hooks read, with the decompiled function each comes from (decomp\names.tsv), and the guest RAM they own.
// A 1 or 2 player game never runs any of this: every hook returns the original value first thing when fewer than three players are in the list.
namespace warriors::ai {

constexpr int kScaledFrom = 3;                         // players from which anything is scaled

// Human (Human_Init): player slot s8 (-1 for AI), the character type s16, the combat record (health at +0x144, maximum +0x146, pending damage +0x118).
constexpr std::uint32_t kSlotOffset = 0x1B0, kTypeOffset = 0xCC, kCombatRecordOffset = 0xD4, kPositionIndexOffset = 0x92;
constexpr std::uint32_t kDamageOffset = 0x118;         // s16 of the combat record: the damage Human_ApplyPendingDamage is about to take off
constexpr std::uint32_t kPositions = 0x00714B00, kPositionStride = 0x20;   // x, y, z, w floats by position index

// Brains (Brains_RegisterTypeHandlers): 0x6D53F0 + human index * 0x2F0, type at +4 (0 player, 1 cop, 2 gang soldier, 3 Warrior, 4 civilian).
constexpr std::uint32_t kBrains = 0x006D53F0, kBrainStride = 0x2F0, kBrainTypeOffset = 4;
enum BrainType : std::uint32_t { kBrainPlayer = 0, kBrainCop = 1, kBrainGangSoldier = 2, kBrainWarrior = 3 };

// Character classes (CfgChar): 0x684620 + type * 0x1ac, category byte at +0x11b (11 gang soldier, 13 boss or big fighter, 14 Warrior).
constexpr std::uint32_t kCharClasses = 0x00684620, kCharClassSize = 0x1AC, kCategoryOffset = 0x11B;
constexpr std::uint32_t kBossCategory = 13;

// Gangs (Gang_Update, GangSpawner_Update): the gang's kind at +0x2c (1: police); spawner records follow at +0x640, 0x130 bytes each.
constexpr std::uint32_t kGangKindOffset = 0x2C, kPoliceKind = 1;
constexpr std::int32_t kMaxSpawnMax = 48;              // the human pool holds 60 (players, Warriors and scripted cast included)
constexpr std::int32_t kMaxGangMembers = 16;           // a gang's member list (HandleArray16)

// Guest RAM of the hooks (the runtime's extra RAM above the PS2's 32 MB; part of every state file, digest and netplay snapshot). 0x02300000 to 0x0230ffff.
constexpr std::uint32_t kAiBase = 0x02300000;
constexpr std::uint32_t kFriendlyFireOff = kAiBase;    // u32: non-zero = a player's hit on a player does no damage (host setting; 0 is the original game)
constexpr std::uint32_t kDamageCarry = kAiBase + 0x100;   // u16 per human index: the part of a scaled damage that did not fit a whole point

inline std::int32_t s8(std::uint32_t a) { return static_cast<std::int8_t>(ps2::ld8(a)); }
inline std::int32_t s16(std::uint32_t a) { return static_cast<std::int16_t>(ps2::ld16(a)); }

// PS2_AI_TRACE=1 prints each distinct decision once, to stderr (test aid).
bool trace_once(const char *text);
#define AI_TRACE(format, ...) do { if (::warriors::ai::tracing()) { char line[160]; std::snprintf(line, sizeof line, format, ##__VA_ARGS__); ::warriors::ai::trace_once(line); } } while (0)
inline bool tracing() { static const bool on = std::getenv("PS2_AI_TRACE") != nullptr; return on; }

inline int players() { return warriors::p9::player_count(); }

// Test aid: PS2_AI_<NAME>=p3,p4 overrides a tuning pair; the shipped values are the constants below.
struct Pair { int p3, p4; };
Pair tuned(const char *name, Pair fallback);

}
