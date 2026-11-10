#pragma once

#include "ps2/generated.hpp"

#include <cstdint>

// The brains' per-player "has seen player" bytes (brain +0x14e + panel) and sticky "has seen" bytes (+0x150 + panel) are two arrays of two: panels 2 and 3 would land on the
// sticky bytes and on two unknown fields. Their bytes live in extra RAM, per brain; panels 0 and 1 keep the original place, so a game of two players reads and writes
// exactly what it did.
namespace warriors::p9 {

constexpr std::uint32_t kSeenBytes = 0, kStickyBytes = 1;

// The address of the byte of `kind` (kSeenBytes or kStickyBytes) for `panel` of the brain at `brain`.
std::uint32_t spot_addr(std::uint32_t brain, std::uint32_t kind, std::uint32_t panel);
// session_level_reset: a new level starts with nobody seen.
bool clear_spot_bytes(ps2::Cpu &c);

}
