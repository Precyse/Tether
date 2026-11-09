#include "p9_audio.hpp"

#include "p9_game.hpp"

// Listeners 2 and 3 of the sound code (see p9_audio.hpp). Every hook answers as the original two player game does when it has no more than two players.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kOriginal = kOriginalPlayers, kMax = kMaxPlayers;
constexpr std::uint32_t kAudioManagerPointer = 0x0050AA84;   // PTR_DAT_0050aa84: the address of the audio manager
constexpr std::uint32_t kManagerFirstListener = 0x20;        // the manager's listener array starts here
constexpr std::uint32_t kManagerAfterListeners = kManagerFirstListener + kOriginal * kListenerBytes;   // +0x60: other fields, not listener 2
constexpr std::uint32_t kAmbientFlagBytes = 4;               // one word per player in the ambient emitters' flag array

void copy_listener(std::uint32_t to, std::uint32_t from) {
    for (std::uint32_t i = 0; i < kListenerBytes; i += 4) ps2::st32(to + i, ps2::ld32(from + i));
}

}

std::uint32_t listener_step(std::uint32_t pointer) {
    const std::uint32_t next = pointer + kListenerBytes;
    const bool past_second = next == ps2::ld32(kAudioManagerPointer) + kManagerAfterListeners;
    return past_second && player_count() > kOriginal ? kManagerListeners : next;
}

void copy_extra_listeners(std::uint32_t source_end) {
    const std::uint32_t count = player_count() < kMax ? player_count() : kMax;
    if (count <= kOriginal) return;
    const std::uint32_t source = source_end - kOriginal * kListenerBytes;
    const bool four_entries = source == kLevelListeners;
    for (std::uint32_t player = kOriginal; player < count; ++player) {
        const std::uint32_t from = four_entries ? source + player * kListenerBytes : source_end - kListenerBytes;
        copy_listener(kManagerListeners + (player - kOriginal) * kListenerBytes, from);
    }
}

void ambient_extra_flags(std::uint32_t flags) {
    const std::uint32_t count = player_count();
    if (count <= kOriginal) return;
    for (std::uint32_t player = kOriginal; player < kMax; ++player)
        ps2::st32(flags + player * kAmbientFlagBytes, player < count ? 1 : 0);
}

bool ambient_extra_heard(std::uint32_t flags) {
    if (player_count() <= kOriginal) return false;
    for (std::uint32_t player = kOriginal; player < kMax; ++player)
        if (ps2::ld32(flags + player * kAmbientFlagBytes) != 0) return true;
    return false;
}

}
