#include "p9_game.hpp"
#include "p9_hooks.hpp"
#include "ps2/pad.hpp"
#include "ps2/system.hpp"

namespace warriors::p9 {

namespace {

constexpr std::uint8_t kCenter = 0x80;
constexpr std::uint32_t kOriginalRecordStep = 4, kEveryRecord = 1;   // pads_frame_step visited records 0 and 4 only (one per port)

// libpad's pressure order: right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2 (button bits 5, 7, 4, 6, 12 to 15, 10, 11, 8, 9).
constexpr int kPressureBits[ps2::PadSample::kPressures] = {5, 7, 4, 6, 12, 13, 14, 15, 10, 11, 8, 9};

void fill_record(std::uint32_t record, const ps2::PadSample &s) {
    ps2::st32(record + kPortReadyOffset, 1);
    ps2::st8(record + kPortLeftXOffset, s.axes[2]);
    ps2::st8(record + kPortLeftXOffset + 1, s.axes[3]);
    ps2::st8(record + kPortLeftXOffset + 2, s.axes[0]);
    ps2::st8(record + kPortLeftXOffset + 3, s.axes[1]);
    ps2::st16(record + kPortButtonsOffset, swap_button_bytes(s.buttons));
    for (int i = 0; i < ps2::PadSample::kPressures; ++i)
        ps2::st8(record + kPortPressureOffset + static_cast<std::uint32_t>(i), s.pressure[static_cast<std::size_t>(kPressureBits[i] - ps2::PadSample::kFirstPressureBit)]);
}

void clear_record(std::uint32_t record) {
    ps2::st32(record + kPortReadyOffset, 0);
    for (std::uint32_t i = 0; i < 4; ++i) ps2::st8(record + kPortLeftXOffset + i, kCenter);
    ps2::st16(record + kPortButtonsOffset, 0);
    for (std::uint32_t i = 0; i < ps2::PadSample::kPressures; ++i) ps2::st8(record + kPortPressureOffset + i, 0);
}

bool extra_pad_plugged() {
    ps2::PadSample sample;
    for (int i = 0; i < kExtraPadRecords; ++i)
        if (ps2::read_pad_sample(kFirstExtraPad + i, sample)) return true;
    return false;
}

}

// pad_port_poll: a record on a multitap slot comes from the runtime's pads 3 and 4 (libpad has no port for them); slot 0 records go through libpad as before.
bool pad_port_poll(ps2::Cpu &c) {
    const std::uint32_t record = c.r[4].w[0];
    if (ps2::ld32(record + kPortSlotOffset) == 0) return false;
    const std::uint32_t index = (record - kPortRecords) / kPortRecordStride;
    ps2::PadSample sample;
    const int pad = kFirstExtraPad + static_cast<int>(index) - 1;
    std::uint16_t motors = 0;
    if (index >= 1 && index <= static_cast<std::uint32_t>(kExtraPadRecords) && ps2::read_pad_sample(pad, sample)) {
        fill_record(record, sample);
        motors = static_cast<std::uint16_t>((ps2::ld8(record + kPortSmallMotorOffset) ? 1u : 0u) | ps2::ld8(record + kPortLargeMotorOffset) << 8);
    } else {
        clear_record(record);
    }
    if (motors != ps2::pad_rumble(pad)) P9_TRACE("[p9] %llu pad %d motors %04x", static_cast<unsigned long long>(ps2::frame_count()), pad, motors);
    ps2::set_pad_rumble(pad, motors);
    c.pc = c.r[31].w[0];
    return true;
}

// pads_frame_step's loop moves on by this many records: 4 as in the original (records 0 and 4 only) until pads beyond the two are plugged.
std::uint32_t pad_loop_step() { return extra_pad_plugged() ? kEveryRecord : kOriginalRecordStep; }

}
