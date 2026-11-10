#include "p9_display.hpp"

#include "p9_camera.hpp"
#include "p9_world.hpp"
#include "ps2/system.hpp"

#include <algorithm>
#include <cmath>

// Player models at the lens. The follow camera of a view that is limited by a wall can end up a metre from a player, whose head then fills the picture; the game has no
// fade for that (its own fades are for world objects far away). person_model_draw draws a human once for every view, with the view as its second argument, and takes the
// model's alpha from the render object's colour (+0x2b). For a player closer to the camera of the view being drawn than kNearFadeStart the alpha is lowered for that
// one draw and put back at its end: the other views, and everything the game keeps, see the model as it was.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kFadeFromPlayers = 3;
constexpr std::uint32_t kPersonModelDraw = 0x00174320;
constexpr std::uint32_t kRenderHuman = 0x2B8, kRenderAlpha = 0x2B;   // render object: the human it draws, the alpha byte of its colour
constexpr std::uint32_t kEyeSlot = 0x21C, kEyeAdjust = 0x218;        // camera vtable: the accessor of its place, and the adjustment of `this` before it
constexpr float kNearFadeStart = 2.6f;                               // metres from the camera: the model starts to fade
constexpr float kNearFadeEnd = 1.6f;                                 // metres from the camera: the model is gone

bool g_drawing = false;
bool g_faded[kMaxPlayers][kMaxPlayers];   // per view and player: the last draw was faded (for the trace only)

// Where the camera of a view is (the accessor object_model_draw measures its own distances from: slot 0x21c, `this` moved by the s16 at 0x218).
bool camera_eye(ps2::Cpu &c, std::uint32_t camera, float (&eye)[3]) {
    const std::uint32_t table = ps2::ld32(camera);
    const std::uint32_t at = ps2::call_guest(c, ps2::ld32(table + kEyeSlot), camera + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(table + kEyeAdjust))));
    if (!at) return false;
    for (std::uint32_t i = 0; i < 3; ++i) eye[i] = read_float(at + i * 4);
    return true;
}

// The distance from the eye to the closest of a player's feet, middle and head.
float nearest_distance(const float (&eye)[3], std::uint32_t human) {
    float feet[3];
    human_position(human, feet);
    float nearest = INFINITY;
    for (const float lift : {0.0f, kHeadHeight / 2, kHeadHeight}) {
        const float dx = feet[0] - eye[0], dy = feet[1] - eye[1], dz = feet[2] + lift - eye[2];
        nearest = std::min(nearest, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return nearest;
}

// How much of the model stays: 1 at kNearFadeStart or farther, 0 at kNearFadeEnd or closer.
float opacity_at(float distance) { return std::clamp((distance - kNearFadeEnd) / (kNearFadeStart - kNearFadeEnd), 0.0f, 1.0f); }

bool is_player_human(std::uint32_t human) {
    return human >= kHumans && human < kHumans + kHumanCount * kHumanStride && ps2::ld8(human + kPlayerSlotOffset) < kMaxPlayers;
}

}

bool human_draw(ps2::Cpu &c) {
    if (g_drawing || player_count() < kFadeFromPlayers) return false;
    const std::uint32_t render = c.r[4].w[0], view = c.r[5].w[0];
    const std::uint32_t camera = player_view_camera(view);
    const std::uint32_t human = ps2::ld32(render + kRenderHuman);
    if (!camera || !is_player_human(human)) return false;
    float eye[3];
    if (!camera_eye(c, camera, eye)) return false;
    const std::uint32_t slot = ps2::ld8(human + kPlayerSlotOffset);
    const float distance = nearest_distance(eye, human), opacity = opacity_at(distance);
    if ((opacity < 1.0f) != g_faded[view][slot]) {
        g_faded[view][slot] = opacity < 1.0f;
        P9_TRACE("[p9] near fade %s: view %u, player %u, %.2f m (frame %llu)", g_faded[view][slot] ? "on" : "off", view, slot, distance, static_cast<unsigned long long>(ps2::frame_count()));
    }
    if (opacity >= 1.0f) return false;
    const std::uint8_t alpha = ps2::ld8(render + kRenderAlpha);
    ps2::st8(render + kRenderAlpha, static_cast<std::uint8_t>(alpha * opacity));
    g_drawing = true;
    const std::uint32_t result = ps2::call_guest(c, kPersonModelDraw, render, view);
    g_drawing = false;
    ps2::st8(render + kRenderAlpha, alpha);
    c.r[2].sd[0] = static_cast<std::int32_t>(result);
    c.pc = c.r[31].w[0];
    return true;
}

}
