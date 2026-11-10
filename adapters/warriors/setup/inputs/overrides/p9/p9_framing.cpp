#include "p9_framing.hpp"

#include "p9_camera.hpp"

#include "ps2/memory.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

// How far back the follow camera of the combined view stands. The game's follow camera is a leash camera: it keeps its distance to the look-at point inside a band
// [near, near + kBandWidth], and eases the near edge toward a wanted value (+0x34c, follow_cam_zoom_ease: the zoom of the sprint and combat cameras). While players share
// the view that wanted value is set here every update to the distance at which each player's feet and head project inside the picture with a margin, between the band's
// standing default and the cap (kMaxCombinedDistance, kFightMaxDistance in a fight). The field of view stays the game's own: players who would not fit by the cap get views of their own (p9_views.cpp).
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kPosition = 0x10, kLookAt = 0x180, kHalfWidth = 0x5C, kHalfHeight = 0x60;   // camera: place, look-at point, tangents of the half field of view
constexpr std::uint32_t kWantedNear = 0x34C, kDefaultNear = 0x344;
constexpr std::uint32_t kBandNear = 0x32C, kBlockedNow = 0x45B, kBlockedSticky = 0x45C, kAim = 0x240, kDistance = 0x440;   // the band's near edge, the collision step's blocked flags, the point the camera faces, the distance to the look-at point
constexpr float kLookAtHeight = 1.4f;        // the look-at point's height above the targets' feet
constexpr float kFrameMargin = 1.25f;        // the combined camera keeps the players inside 1 / kFrameMargin of the half picture
constexpr float kBandWidth = 0.5f;           // metres between the band's near and far edge
constexpr float kLookAtDeadZone = 0.7f;      // metres the cluster's centre moves before the look-at point follows (the part beyond is followed)
constexpr float kMinDirection = 0.5f;        // below this distance between camera and look-at point the view direction is not known
constexpr std::uint32_t kFramedAt = kCamViewState + 0x70, kCentreAt = kCamViewState + 0xA0, kHeldAt = kCamViewState + 0xD0;
// per owner: whether the framing is ours, the cluster's centre (x, y), the look-at point held (x, y)

struct Vec3 {
    float x, y, z;
};

Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

Vec3 normalized(Vec3 v) {
    const float length = std::sqrt(dot(v, v));
    return {v.x / length, v.y / length, v.z / length};
}

Vec3 point(const Players &players, std::uint32_t slot, float lift) {
    return {players.position[slot][0], players.position[slot][1], players.position[slot][2] + lift};
}

// The members' bounding box in the ground plane: the look-at point's x and y are its centre (continuous in the players' positions, unlike a pair's midpoint), z is
// the members' mean height plus the look-at height.
Vec3 cluster_centre(const Players &players, std::uint32_t members) {
    float low[2] = {1e9f, 1e9f}, high[2] = {-1e9f, -1e9f}, height = 0;
    std::uint32_t count = 0;
    for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
        if (!(members & (1u << slot))) continue;
        for (std::uint32_t axis = 0; axis < 2; ++axis) {
            low[axis] = std::min(low[axis], players.position[slot][axis]);
            high[axis] = std::max(high[axis], players.position[slot][axis]);
        }
        height += players.position[slot][2];
        ++count;
    }
    return {(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, height / static_cast<float>(count) + kLookAtHeight};
}

// The distance from `look_at` at which every member's feet and head are inside `frame` of the half picture, looking in the direction from the camera's place to its own
// look-at point. -1 when that direction is not known.
float required_distance(std::uint32_t camera, const Players &players, std::uint32_t members, Vec3 look_at, float frame) {
    const Vec3 eye = {read_float(camera + kPosition), read_float(camera + kPosition + 4), read_float(camera + kPosition + 8)};
    const Vec3 to_look = Vec3{read_float(camera + kLookAt), read_float(camera + kLookAt + 4), read_float(camera + kLookAt + 8)} - eye;
    if (dot(to_look, to_look) < kMinDirection * kMinDirection) return -1;
    const Vec3 forward = normalized(to_look), right = normalized(cross(forward, {0, 0, 1})), up = cross(right, forward);
    const float half_width = read_float(camera + kHalfWidth), half_height = read_float(camera + kHalfHeight);
    float need = 0;
    for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
        if (!(members & (1u << slot))) continue;
        for (const float lift : {0.0f, kHeadHeight}) {
            const Vec3 offset = point(players, slot, lift) - look_at;
            const float along = dot(offset, forward);
            need = std::max({need, std::fabs(dot(offset, right)) / (frame * half_width) - along, std::fabs(dot(offset, up)) / (frame * half_height) - along});
        }
    }
    return need;
}

// The near edge to ask for: the distance the members need, between the standing default and the cap; not farther out while the world blocks the camera (it would only
// push against the wall). The game's own easing (follow_cam_zoom_ease) moves the band there.
float wanted_near(std::uint32_t camera, float need, float cap) {
    const float standing = read_float(camera + kDefaultNear), current = read_float(camera + kBandNear);
    const float target = std::min(std::max(need, standing), cap - kBandWidth);
    return camera_blocked(camera) ? std::min(target, std::max(current, standing)) : target;
}

// The two members farthest apart: the camera's targets (the look-at point is set from the whole cluster, see cluster_look_at; the targets still serve the camera's
// collision and its target count).
void farthest_pair(const Players &players, std::uint32_t members, std::uint32_t &first, std::uint32_t &second) {
    float far = -1;
    for (std::uint32_t a = 0; a < kMaxPlayers; ++a) {
        for (std::uint32_t b = a + 1; b < kMaxPlayers; ++b) {
            if (!(members & (1u << a)) || !(members & (1u << b)) || distance_sq(players, a, b) <= far) continue;
            far = distance_sq(players, a, b);
            first = a;
            second = b;
        }
    }
}

}

bool fits_one_view(std::uint32_t camera, const Players &players, std::uint32_t members, float frame, float reach) {
    const float need = required_distance(camera, players, members, cluster_centre(players, members), frame);
    return need >= 0 && need <= reach;
}

float picture_share(std::uint32_t camera, const Players &players, std::uint32_t members) {
    const Vec3 eye = {read_float(camera + kPosition), read_float(camera + kPosition + 4), read_float(camera + kPosition + 8)};
    const Vec3 to_aim = Vec3{read_float(camera + kAim), read_float(camera + kAim + 4), read_float(camera + kAim + 8)} - eye;
    if (dot(to_aim, to_aim) < kMinDirection * kMinDirection) return 0;
    const Vec3 forward = normalized(to_aim), right = normalized(cross(forward, {0, 0, 1})), up = cross(right, forward);
    const float half_width = read_float(camera + kHalfWidth), half_height = read_float(camera + kHalfHeight);
    float share = 0;
    for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
        if (!(members & (1u << slot))) continue;
        for (const float lift : {0.0f, kHeadHeight}) {
            const Vec3 offset = point(players, slot, lift) - eye;
            const float depth = std::max(dot(offset, forward), kMinDirection);
            share = std::max({share, std::fabs(dot(offset, right)) / (depth * half_width), std::fabs(dot(offset, up)) / (depth * half_height)});
        }
    }
    return share;
}

bool camera_blocked(std::uint32_t camera) { return ps2::ld8(camera + kBlockedNow) || ps2::ld8(camera + kBlockedSticky); }

float camera_distance(std::uint32_t camera) { return read_float(camera + kDistance); }

void frame_cluster(std::uint32_t camera, std::uint32_t owner, const Players &players, std::uint32_t members, float cap, std::uint32_t &first, std::uint32_t &second) {
    farthest_pair(players, members, first, second);
    const Vec3 centre = cluster_centre(players, members);
    write_float(kCentreAt + owner * 8, centre.x);
    write_float(kCentreAt + owner * 8 + 4, centre.y);
    const float need = std::max(required_distance(camera, players, members, centre, 1 / kFrameMargin), 0.0f);
    write_float(camera + kWantedNear, wanted_near(camera, need, cap));
    ps2::st32(kFramedAt + owner * 4, 1);
}

// The look-at point follows the cluster's centre only beyond a dead zone, so a player's twitch does not move the camera; the held point starts at the look-at point.
void cluster_look_at(std::uint32_t camera) {
    for (std::uint32_t owner = 0; owner < kMaxPlayers; ++owner) {
        if (ps2::ld32(kFramedAt + owner * 4) != 1 || ps2::ld32(kCamFollow + owner * 4) != camera) continue;
        const float centre[2] = {read_float(kCentreAt + owner * 8), read_float(kCentreAt + owner * 8 + 4)};
        float held[2] = {read_float(kHeldAt + owner * 8), read_float(kHeldAt + owner * 8 + 4)};
        const float away = std::hypot(centre[0] - held[0], centre[1] - held[1]);
        if (held[0] == 0 && held[1] == 0) {
            held[0] = centre[0];
            held[1] = centre[1];
        } else if (away > kLookAtDeadZone) {
            const float follow = (away - kLookAtDeadZone) / away;
            held[0] += (centre[0] - held[0]) * follow;
            held[1] += (centre[1] - held[1]) * follow;
        }
        write_float(kHeldAt + owner * 8, held[0]);
        write_float(kHeldAt + owner * 8 + 4, held[1]);
        write_float(camera + kLookAt, held[0]);
        write_float(camera + kLookAt + 4, held[1]);
        return;
    }
}

void release_framing(std::uint32_t camera, std::uint32_t owner) {
    if (!ps2::ld32(kFramedAt + owner * 4)) return;
    write_float(camera + kWantedNear, read_float(camera + kDefaultNear));
    write_float(kHeldAt + owner * 8, 0);
    write_float(kHeldAt + owner * 8 + 4, 0);
    ps2::st32(kFramedAt + owner * 4, 0);
}

}
