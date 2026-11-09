#include "p9_brain.hpp"

#include "p9_game.hpp"
#include "ps2/system.hpp"

namespace warriors::p9 {

namespace {

constexpr std::uint32_t kBrains = 0x006D53F0, kBrainStride = 0x2F0, kBrainCount = 60;
constexpr std::uint32_t kSeenOffset = 0x14E, kStickyOffset = 0x150;   // from the brain: two bytes each for panels 0 and 1
constexpr std::uint32_t kBytesPerBrain = 4;                            // panels 2 and 3: two seen bytes, two sticky bytes
constexpr std::uint32_t kKindBytes = 2;

}

std::uint32_t spot_addr(std::uint32_t brain, std::uint32_t kind, std::uint32_t panel) {
    if (panel < kOriginalPlayers) return brain + (kind == kSeenBytes ? kSeenOffset : kStickyOffset) + panel;
    return kBrainSpot + ((brain - kBrains) / kBrainStride) * kBytesPerBrain + kind * kKindBytes + (panel - kOriginalPlayers);
}

bool clear_spot_bytes(ps2::Cpu &) {
    for (std::uint32_t i = 0; i < kBrainCount * kBytesPerBrain; i += 4) ps2::st32(kBrainSpot + i, 0);
    return false;
}

}
