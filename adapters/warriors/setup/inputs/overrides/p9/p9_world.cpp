#include "p9_world.hpp"

#include "p9_camera.hpp"

#include "ps2/system.hpp"

#include <cmath>
#include <cstring>

namespace warriors::p9 {

namespace {

constexpr std::uint32_t kPersonInScene = 0x00227D28, kHandleToObject = 0x00390268;
constexpr std::uint32_t kFollowCamera = 2, kRailCamera = 9, kPlainCamera = 0, kLevelFlowOffset = 0x14C, kViewCount = 0x0050B198;
constexpr std::uint32_t kHandleScratch = kBase + 0x14;    // a handle in guest memory for handle_to_object
constexpr std::uint32_t kCameraTypeSlot = 0x1EC, kCameraAdjustSlot = 0x1E8;   // vtable: type function, and the s16 `this` adjustment before it

}

void human_position(std::uint32_t human, float (&out)[3]) {
    const std::uint32_t at = kPositions + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(human + kPositionIndexOffset))) * kPositionStride;
    for (std::uint32_t i = 0; i < 3; ++i) {
        const std::uint32_t bits = ps2::ld32(at + i * 4);
        std::memcpy(&out[i], &bits, sizeof(float));
    }
}

float human_distance(std::uint32_t a, std::uint32_t b) {
    float pa[3], pb[3];
    human_position(a, pa);
    human_position(b, pb);
    const float dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool in_scene(ps2::Cpu &c, std::uint32_t human) { return ps2::call_guest(c, kPersonInScene, human) != 0; }

std::uint32_t resolve_handle(ps2::Cpu &c, std::uint32_t handle) {
    ps2::st32(kHandleScratch, handle);
    return ps2::call_guest(c, kHandleToObject, kHandleScratch);
}

std::uint32_t camera_type(ps2::Cpu &c) {
    const std::uint32_t camera = player_view_camera(0);
    if (!camera) return 0;
    const std::uint32_t table = ps2::ld32(camera);
    return ps2::call_guest(c, ps2::ld32(table + kCameraTypeSlot), camera + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(table + kCameraAdjustSlot))));
}

// The game's own gate of a START join (level_driver_step): the level flow is idle and some view shows a follow, a rail or a plain camera. A rail section (a chase) allows
// it as well; the leader stands in no scene, so the join's own scene starts cleanly.
bool join_gate_open(ps2::Cpu &c) {
    if (!game() || ps2::ld32(game() + kLevelFlowOffset) != 0) return false;
    if (in_scene(c, resolve_handle(c, ps2::ld32(game() + kPlayerHandlesOffset)))) return false;
    for (std::uint32_t view = 0; view < ps2::ld32(kViewCount) && view < kMaxPlayers; ++view) {
        const std::uint32_t camera = player_view_camera(view);
        if (!camera) continue;
        const std::uint32_t table = ps2::ld32(camera);
        const std::uint32_t type = ps2::call_guest(c, ps2::ld32(table + kCameraTypeSlot), camera + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(table + kCameraAdjustSlot))));
        if (type == kFollowCamera || type == kRailCamera || type == kPlainCamera) return true;
    }
    return false;
}

bool normal_play(ps2::Cpu &c) {
    if (!game() || ps2::ld32(game() + kLevelFlowOffset) != 0) return false;
    const std::uint32_t leader = ps2::ld32(game() + kPlayerHandlesOffset);
    return !in_scene(c, resolve_handle(c, leader)) && camera_type(c) == kFollowCamera;
}

}
