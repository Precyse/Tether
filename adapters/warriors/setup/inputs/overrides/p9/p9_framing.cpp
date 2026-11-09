#include "p9_framing.hpp"

#include "p9_camera.hpp"

#include "ps2/memory.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <cstring>

// How far back a follow camera that shows several players stands. The game's follow camera is a leash camera: it keeps its distance to the look-at point inside a band
// [near, near + 0.5], and eases the near edge toward a wanted value (+0x34c, Cam_Follow_EaseBand: the zoom of the sprint and combat cameras). With the players of a
// view spread over the screen that wanted value is set here every update to the distance at which each player's feet and head project inside the picture with a margin,
// between the band's standing default and a limit that keeps the characters readable.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kPosition = 0x10, kLookAt = 0x180, kHalfWidth = 0x5C, kHalfHeight = 0x60;   // camera: place, look-at point, tangents of the half field of view
constexpr std::uint32_t kWantedNear = 0x34C, kDefaultNear = 0x344;
constexpr std::uint32_t kBandNear = 0x32C, kDt = 0x1A0, kBlockedNow = 0x45B, kBlockedSticky = 0x45C;   // the band's near edge, the update's seconds, the collision step's blocked flags
constexpr std::uint32_t kPitch = 0x3B4, kPitchUpper = 0x3AC, kDistance = 0x440;   // wanted pitch, its upper limit (radians), the current distance
constexpr std::uint32_t kAim = 0x240, kFov = 0x44, kDefaultFov = 0x310, kWantedFov = 0x394, kFovRate = 0x39C;   // facing point; field of view now, configured, wanted, rate
constexpr float kHeadHeight = 1.9f;          // metres above a player's feet that must stay in the picture
constexpr float kLookAtHeight = 1.4f;        // the look-at point's height above the targets' feet
constexpr float kFrameMargin = 1.25f;        // a player must project inside 1 / kFrameMargin of the half picture
constexpr float kMaxClusterDistance = 9.5f;  // farthest the camera goes back for a view that splits when its players move on
constexpr float kMaxFov = 85.0f;             // widest field of view when a wall keeps the camera from standing back (degrees)
constexpr float kFovMargin = 1.15f;          // the picture margin the field of view works to (the distance has had its chance first)
constexpr float kFovKeep = 0.8f;             // the field of view only narrows again once the players need less than this share of the picture
constexpr float kWidenRate = 30.0f, kNarrowRate = 12.0f;   // degrees per second
constexpr float kDegrees = 57.29578f;
constexpr float kFovArrived = 0.1f;          // degrees from the standing field of view at which the easing is over
constexpr float kRiseRate = 4.0f, kFallRate = 2.0f;   // metres per second the wanted near edge may move (the game's easing follows it)
constexpr float kMaxStep = 0.1f;             // seconds: longest update step the rates are applied to
constexpr float kMaxExtraPitch = 0.80f;      // radians the camera may look down more than usual when a wall keeps it from standing back
constexpr float kPitchRaiseRate = 0.35f, kPitchLowerRate = 0.15f;   // radians per second
constexpr float kPitchShortfall = 0.5f, kPitchSlack = 1.5f;         // metres the camera is short of the wanted distance before it climbs, and ahead of it before it comes down
constexpr float kLookAtDeadZone = 0.7f;      // metres the cluster's centre moves before the look-at point follows (the part beyond is followed)
constexpr float kMinDirection = 0.5f;        // below this distance between camera and look-at point the view direction is not known
constexpr std::uint32_t kFramedAt = kCamViewState + 0x70, kWantedAt = kCamViewState + 0x80, kCentreAt = kCamViewState + 0xA0, kPitchAt = kCamViewState + 0xC0, kHeldAt = kCamViewState + 0xD0;
// per owner: whether the framing is ours (2: giving the field of view and pitch back), the near edge asked for last, the cluster's centre (x, y), the extra pitch applied, the look-at point held (x, y)

struct Vec3 {
    float x, y, z;
};

float read_float(std::uint32_t at) {
    const std::uint32_t bits = ps2::ld32(at);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

void write_float(std::uint32_t at, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    ps2::st32(at, bits);
}

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

// The distance from the look-at point at which every member's feet and head are inside the picture, looking from the camera's place toward the look-at point.
// -1 when the view direction is not known.
float required_distance(std::uint32_t camera, const Players &players, std::uint32_t members, Vec3 look_at) {
    const Vec3 eye = {read_float(camera + kPosition), read_float(camera + kPosition + 4), read_float(camera + kPosition + 8)};
    const Vec3 to_look = look_at - eye;
    if (dot(to_look, to_look) < kMinDirection * kMinDirection) return -1;
    const Vec3 forward = normalized(to_look), right = normalized(cross(forward, {0, 0, 1})), up = cross(right, forward);
    const float half_width = read_float(camera + kHalfWidth), half_height = read_float(camera + kHalfHeight);
    float need = 0;
    for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
        if (!(members & (1u << slot))) continue;
        for (const float lift : {0.0f, kHeadHeight}) {
            const Vec3 offset = point(players, slot, lift) - look_at;
            const float along = dot(offset, forward);
            need = std::max({need, std::fabs(dot(offset, right)) * kFrameMargin / half_width - along, std::fabs(dot(offset, up)) * kFrameMargin / half_height - along});
        }
    }
    return need;
}

// How much of the half picture the members take, the worst of their feet and heads: 1 means a player on the edge. The camera faces its aim point.
float picture_share(std::uint32_t camera, const Players &players, std::uint32_t members) {
    const Vec3 eye = {read_float(camera + kPosition), read_float(camera + kPosition + 4), read_float(camera + kPosition + 8)};
    const Vec3 aim = {read_float(camera + kAim), read_float(camera + kAim + 4), read_float(camera + kAim + 8)};
    const Vec3 to_aim = aim - eye;
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

// The field of view that puts the members inside the picture at the camera's actual place, once the band is as far back as the world lets the camera go; the wanted
// value and rate go to the game's own field of view easing (Cam_Follow_Update step 6).
void widen_field_of_view(std::uint32_t camera, const Players &players, std::uint32_t members) {
    const float now = read_float(camera + kFov), standing = read_float(camera + kDefaultFov);
    const float share = picture_share(camera, players, members) * kFovMargin;
    float wanted = now;
    if (share > 1.0f || share < kFovKeep) wanted = 2 * std::atan(std::tan(now / (2 * kDegrees)) * share) * kDegrees;
    wanted = std::min(std::max(wanted, standing), kMaxFov);
    write_float(camera + kWantedFov, wanted);
    write_float(camera + kFovRate, wanted > now ? kWidenRate : kNarrowRate);
}

// The near edge to ask for: the distance the members need, between the standing default and the limit; not farther out while the world blocks the camera (it would only
// push against the wall), and moved at limited speed so the camera does not lurch when the players' pair or spread changes.
float wanted_near(std::uint32_t camera, std::uint32_t owner, float need, float limit) {
    const float standing = read_float(camera + kDefaultNear), current = read_float(camera + kBandNear);
    float target = std::min(std::max(need, standing), limit);
    if (ps2::ld8(camera + kBlockedNow) || ps2::ld8(camera + kBlockedSticky)) target = std::min(target, std::max(current, standing));
    const float last = ps2::ld32(kFramedAt + owner * 4) == 1 ? read_float(kWantedAt + owner * 4) : current;
    const float step = std::min(read_float(camera + kDt), kMaxStep);
    const float wanted = target > last ? std::min(target, last + kRiseRate * step) : std::max(target, last - kFallRate * step);
    write_float(kWantedAt + owner * 4, wanted);
    return wanted;
}

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

// Looks down more (the wanted pitch and its upper limit move together) while the camera is short of the distance the players need, which a wall behind it allows
// no other way to gain; comes back down slowly once it has distance to spare. The extra pitch is applied as a change, so the game's own pitch rules keep working.
void adjust_pitch(std::uint32_t camera, std::uint32_t owner, float need, bool give_back) {
    const float last = read_float(kPitchAt + owner * 4), step = std::min(read_float(camera + kDt), kMaxStep), shortfall = need - read_float(camera + kDistance);
    float extra = last;
    if (give_back || shortfall < -kPitchSlack) extra -= kPitchLowerRate * step;
    else if (shortfall > kPitchShortfall) extra += kPitchRaiseRate * step;
    extra = std::min(std::max(extra, 0.0f), kMaxExtraPitch);
    write_float(kPitchAt + owner * 4, extra);
    write_float(camera + kPitch, read_float(camera + kPitch) + extra - last);
    write_float(camera + kPitchUpper, read_float(camera + kPitchUpper) + extra - last);
}

void frame_cluster(std::uint32_t camera, std::uint32_t owner, const Players &players, std::uint32_t members, std::uint32_t &first, std::uint32_t &second) {
    farthest_pair(players, members, first, second);
    const Vec3 centre = cluster_centre(players, members);
    write_float(kCentreAt + owner * 8, centre.x);
    write_float(kCentreAt + owner * 8 + 4, centre.y);
    const float need = std::max(required_distance(camera, players, members, centre), 0.0f);
    adjust_pitch(camera, owner, need, false);
    widen_field_of_view(camera, players, members);
    write_float(camera + kWantedNear, wanted_near(camera, owner, need, kMaxClusterDistance));
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
    const std::uint32_t framed = ps2::ld32(kFramedAt + owner * 4);
    if (!framed) return;
    const float now = read_float(camera + kFov), standing = read_float(camera + kDefaultFov);
    if (framed == 1) {
        write_float(camera + kWantedNear, read_float(camera + kDefaultNear));
        write_float(camera + kWantedFov, standing);
        write_float(camera + kFovRate, kNarrowRate);
        write_float(kHeldAt + owner * 8, 0);
        write_float(kHeldAt + owner * 8 + 4, 0);
        ps2::st32(kFramedAt + owner * 4, 2);
    }
    adjust_pitch(camera, owner, 0, true);
    if (std::fabs(now - standing) < kFovArrived && read_float(kPitchAt + owner * 4) <= 0) {
        write_float(camera + kFovRate, 0);
        ps2::st32(kFramedAt + owner * 4, 0);
    }
}

}
