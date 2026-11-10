#include "ui_hooks.hpp"

#include "ps2/boot_options.hpp"
#include "ui_game.hpp"

// The startup movies. `boot_sequence` (0x001446d0) plays LOGO, PLOGO and L1_IN through video_play_clip before the front end starts; video_play_clip does its usual set-up around the
// movie (music and sound stopped, the sound unit's effects, the fades) and hands the Bink session to video_session_run. With the movies skipped, that call is the
// only thing left out: video_session_run returns at once, as it does when the file cannot be opened (the movie object stays unstarted, +0x40 = 0), and video_play_clip
// finishes as it does after a movie. Movies played by the game's scripts (other names) are not touched.
namespace warriors::ui {

namespace {

// video_skip_poll (0x0042a820): any button on any of the eight pads ends a skippable movie. In a co-op session only the host's pad does, so one impatient player cannot
// skip the story for the others; the rest of the function (the caption kind, the pad state machines, the button that skipped is not pressed on) is the game's own.
constexpr std::uint32_t kButtonHistoryFetch = 0x00144A08, kButtonStateSet = 0x00144F98, kPadsStateReset = 0x00145670, kCaptionKindSet = 0x001CB010;
constexpr std::uint32_t kPadRecords = 0x005DD810, kPadRecordBytes = 0x50, kPadCount = 8, kCaptions = 0x00619570, kCaptionKindSkipped = 4;
constexpr std::uint32_t kPadStatesWord = 0x006FECC0, kStartBit = 0x800;
constexpr std::uint32_t kHostPadRecord = kPadRecords;

constexpr std::uint32_t kBootMovieNames[] = {0x00549C30, 0x00549C38, 0x00549C40};   // "LOGO", "PLOGO", "L1_IN": boot_sequence's arguments
constexpr std::uint32_t kMovieStarted = 0x40;   // video_session_close removes its GIF callback when this is set

}

void movie_play_entry(ps2::Cpu &c) {
    bool boot_movie = false;
    for (const std::uint32_t name : kBootMovieNames) boot_movie = boot_movie || c.r[4].w[0] == name;
    ps2::st32(kSkipNextMovie, boot_movie && !ps2::play_intro() ? 1 : 0);
}

bool video_skip_poll(ps2::Cpu &c) {
    if (!coop_session()) return false;
    const bool skip = guest(c, kButtonHistoryFetch, {kHostPadRecord, 0}) != 0;
    if (skip) {
        guest(c, kCaptionKindSet, {kCaptions, kCaptionKindSkipped});
        if (ps2::ld32(kPadStatesWord) != 0) guest(c, kPadsStateReset);
        for (std::uint32_t pad = 0; pad < kPadCount; ++pad) {
            const std::uint32_t record = kPadRecords + pad * kPadRecordBytes;
            guest(c, kButtonStateSet, {record, guest(c, kButtonHistoryFetch, {record, 0}) & ~kStartBit});
        }
    }
    c.r[2].sd[0] = skip ? 1 : 0;
    c.pc = c.r[31].w[0];
    return true;
}

bool movie_skip(ps2::Cpu &c) {
    if (ps2::ld32(kSkipNextMovie) == 0) return false;
    ps2::st32(kSkipNextMovie, 0);
    const std::uint32_t movie = c.r[4].w[0];
    ps2::st32(movie + kMovieStarted, 0);
    c.r[2].sd[0] = static_cast<std::int32_t>(movie);
    c.pc = c.r[31].w[0];
    return true;
}

}
