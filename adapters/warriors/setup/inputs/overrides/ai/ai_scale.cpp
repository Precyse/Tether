#include "ai_game.hpp"
#include "ai_hooks.hpp"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

// Enemy strength for 3 and 4 players, plus the host's friendly fire switch. The original has no co-op scaling: the same enemies face twice the targets. Two levers, both through the game's own numbers (the difficulty step is ai_difficulty.cpp):
//  * how many enemies the spawners keep alive (the limit a spawner compares its living members with, and the global spawn maximum), read where the game reads them, so a
//    level script's own caps (GangSetMaxConcurrent, SetSpawnMax) are scaled whenever they were set and whoever joined later;
//  * how much damage takes an enemy's health down: damage_commit subtracts the pending damage from the health, so dividing it by the health percentage is the same as
//    that much more health (the part that does not fit a whole point is carried per human in guest RAM). Bosses (character category 13) get their own, larger, percentage.
// The percentages are per player count, in one table. The attackers per target need nothing: a target allows its own places for attackers (ai_reserve_attack_slot), so
// each further player brings his own.
namespace warriors::ai {

namespace {

// Total enemy durability for 3 and 4 players is about x1.25 and x1.5 (an estimate: a human deals several times an AI Warrior's damage, and a human replaces an AI Warrior of the crew, so the side
// kills about 25 and 50 percent faster). Spawner driven fights get it as more enemies times tougher enemies (1.12 x 1.12 = 1.25, 1.22 x 1.22 = 1.5), cast fights as tougher enemies alone, bosses as
// a larger share of it.
constexpr Pair kCountPercent = {112, 122};
constexpr Pair kHealthPercent = {112, 122};
constexpr Pair kBossPercent = {125, 150};
constexpr int kNoScale = 100;
constexpr std::size_t kTraceLines = 40;

int percent_for(const Pair &table) {
    const int count = players();
    return count >= 4 ? table.p4 : count >= kScaledFrom ? table.p3 : kNoScale;
}

int count_percent() { static const Pair table = tuned("COUNT", kCountPercent); return percent_for(table); }
int health_percent() { static const Pair table = tuned("HEALTH", kHealthPercent); return percent_for(table); }
int boss_percent() { static const Pair table = tuned("BOSS", kBossPercent); return percent_for(table); }

std::uint32_t brain_type(std::uint32_t human) { return ps2::ld32(kBrains + p9::human_index(human) * kBrainStride + kBrainTypeOffset); }
std::uint32_t category(std::uint32_t human) { return ps2::ld8(kCharClasses + ps2::ld16(human + kTypeOffset) * kCharClassSize + kCategoryOffset); }
bool is_player(std::uint32_t human) { return s8(human + kSlotOffset) != -1; }

// The value raised by the count percentage, up to `limit`; a value the level set above the limit stays as it was.
std::int32_t scaled_count(std::int32_t value, std::int32_t limit) {
    const std::int32_t scaled = (value * count_percent() + kNoScale / 2) / kNoScale;
    return scaled < value ? value : scaled > limit ? (value > limit ? value : limit) : scaled;
}

}

bool trace_once(const char *text) {
    static std::set<std::string> seen;
    if (!seen.insert(text).second || seen.size() > kTraceLines) return false;
    std::fprintf(stderr, "[ai] %s\n", text);
    return true;
}

Pair tuned(const char *name, Pair fallback) {
    char key[32];
    std::snprintf(key, sizeof key, "PS2_AI_%s", name);
    if (const char *text = std::getenv(key)) std::sscanf(text, "%d,%d", &fallback.p3, &fallback.p4);
    return fallback;
}

std::int32_t spawner_cap(std::uint32_t gang, std::int32_t cap) {
    if (count_percent() == kNoScale || ps2::ld32(gang + kGangKindOffset) == kPoliceKind) return cap;
    const std::int32_t magnitude = cap < 0 ? -cap : cap;
    const std::int32_t scaled = scaled_count(magnitude, kMaxGangMembers);
    AI_TRACE("spawner limit %d -> %d (gang %08x, %d players)", cap, cap < 0 ? -scaled : scaled, gang, players());
    return cap < 0 ? -scaled : scaled;
}

std::int32_t spawn_max(std::int32_t max) {
    const std::int32_t scaled = count_percent() == kNoScale ? max : scaled_count(max, kMaxSpawnMax);
    if (scaled != max) AI_TRACE("spawn maximum %d -> %d (%d players)", max, scaled, players());
    return scaled;
}

bool boss_armour_vs_players(std::uint32_t victim) {
    return players() >= kScaledFrom && category(victim) == kBossCategory;
}

std::int32_t enemy_damage(std::uint32_t victim, std::uint32_t foe, std::uint32_t record, std::int32_t damage) {
    if (damage > 0 && foe && foe != victim && is_player(victim) && is_player(foe) && friendly_fire_off()) {   // the host's switch: a player's hit on a player takes the AI's path (no damage)
        ps2::st16(record + kDamageOffset, 0);
        return 0;
    }
    if (players() < kScaledFrom || damage <= 0 || !foe || is_player(victim)) return damage;
    const std::uint32_t victim_brain = brain_type(victim), foe_brain = brain_type(foe);
    const bool boss = category(victim) == kBossCategory;
    if (!boss && victim_brain != kBrainGangSoldier && victim_brain != kBrainCop) return damage;
    if (foe_brain != kBrainPlayer && foe_brain != kBrainWarrior) return damage;
    const int percent = boss ? boss_percent() : health_percent();
    const std::uint32_t carry_at = kDamageCarry + p9::human_index(victim) * 2;
    const std::int32_t exact = damage * kNoScale + static_cast<std::int32_t>(ps2::ld16(carry_at));
    const std::int32_t taken = exact / percent;
    ps2::st16(carry_at, static_cast<std::uint16_t>(exact - taken * percent));
    ps2::st16(record + kDamageOffset, static_cast<std::uint16_t>(taken));
    return taken;
}

}
