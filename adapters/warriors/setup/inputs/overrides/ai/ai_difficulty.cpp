#include "ai_game.hpp"
#include "ai_hooks.hpp"

#include "netplay/session_file.hpp"

// The game's own difficulty step for bigger sessions. global.lua picks the difficulty script of a level (config_easy, config_normal, config_hard, config_fury) from GetProfileDifficulty
// (game state +0x43c), once, when the level loads; the scripts set every power class (attack delay, hurt threshold, weapon stun) and re-run CfgChar. So the step is taken where the
// game reads it: GetProfileDifficulty answers one step higher for a session of three players and two for four, and the level then loads the harder script itself. The size of the
// session is what the launcher's roster says (the same on every machine, known at level load, unlike the players who have joined by then). Levels that force their own difficulty in
// global.lua (the lessons) never ask. Running the next script in the middle of a level is not possible from a hook (run_script_file needs the thread scheduler).
namespace warriors::ai {

namespace {

constexpr std::uint32_t kProfileDifficultyOffset = 0x43C;
constexpr std::uint32_t kFury = 3, kRumble = 4;   // the highest difficulty a step may reach; Rumble's own script is never stepped
constexpr int kOriginalSessionSize = 2;
constexpr int kMaxSteps = 2;

int session_size() {
    if (const char *text = std::getenv("PS2_AI_SESSION")) return std::atoi(text);   // test aid: the session size of a run without a launcher
    int size = 0;
    for (int slot = 0; slot < ps2::kPadCount; ++slot) size += !ps2::netplay::roster_name(slot).empty();
    return size;
}

}

bool profile_difficulty(ps2::Cpu &c) {
    int step = session_size() - kOriginalSessionSize;
    if (step <= 0) return false;
    if (step > kMaxSteps) step = kMaxSteps;
    const std::uint32_t base = ps2::ld8(p9::game() + kProfileDifficultyOffset);
    if (base >= kFury) return false;
    const std::uint32_t stepped = base + static_cast<std::uint32_t>(step);
    AI_TRACE("profile difficulty %u -> %u (session of %d)", base, stepped > kFury ? kFury : stepped, session_size());
    c.r[2].sd[0] = static_cast<std::int32_t>(stepped > kFury ? kFury : stepped);
    c.pc = c.r[31].w[0];
    return true;
}

}
