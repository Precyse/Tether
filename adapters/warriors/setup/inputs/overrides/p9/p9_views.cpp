#include "p9_camera.hpp"

#include "p9_game.hpp"
#include "p9_framing.hpp"
#include "p9_view_data.hpp"
#include "ps2/system.hpp"

#include <cstring>

// Which players share a view. The game's own merge/split logic (Cam_Follow_DecideSplit and its helpers) is written for exactly two cameras and does nothing for three
// or four. With three or more players in normal play (every player on a follow camera) this file decides instead: players who stay close together share one view,
// shown by the camera of the lowest of them; the others have a view of their own. Up to four views, laid out by the game's own grid (Cameras_LayoutViews: mode 2 for
// more than two views). The two player rules are kept: a player counts as long as Camera_HumanCounts says so (a downed player's view goes), the players join after
// kMergeFrames of staying within kMergeDistanceSq, and split as soon as they are farther apart than kSplitDistanceSq.
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kCamerasLayoutViews = 0x00122ED0, kCamHumanCounts = 0x00123500, kHandleResolve = 0x00390268, kFollowPrime = 0x00127C10;
constexpr std::uint32_t kFollowRefreshLookAt = 0x00127D48;
constexpr std::uint32_t kLastTarget = 0x005D91B8, kSwitchViewsByCount = 0x0050B1C0;
constexpr std::uint32_t kPositions = 0x00714B00, kPositionStride = 0x20, kPositionIndexOffset = 0x92;
constexpr std::uint32_t kHumanHandleOffset = 0x90;

// Camera (ICamera) layout.
constexpr std::uint32_t kVtableType = 0x1EC, kVtableTypeAdjust = 0x1E8, kVtablePlayer = 0x27C, kVtablePlayerAdjust = 0x278;
constexpr std::uint32_t kVtableSetViewOn = 0x294, kVtableSetViewOnAdjust = 0x290, kVtableViewOn = 0x284, kVtableViewOnAdjust = 0x280;
constexpr std::uint32_t kFollowCameraType = 2;
constexpr std::uint32_t kFollowTargets = 0x314, kFollowTargetCount = 0x444, kFollowFocusHandle = 0x31C, kFollowTargetsChanged = 0x469, kFollowSingleTargetStep = 0x446;
constexpr std::uint32_t kFollowLookAt = 0x180, kFollowLookAtSnap = 0x240, kFollowLookAtBytes = 16;

// The distances of the two player rules, squared (metres), and the time players stay close before they share a view (0.72 s at 60 Hz).
constexpr float kMergeDistanceSq = 56.25f, kSplitDistanceSq = 121.0f;
constexpr std::uint32_t kMergeFrames = 43;
constexpr std::uint32_t kNoOwner = 0xFF;

// View state in extra RAM (p9_game.hpp, kCamViewState).
// kStampAt: the frame (plus one) of the last decision; kHandledAt: whether this file made it; kBusyAt: set while the decision runs.
constexpr std::uint32_t kStampAt = kCamViewState, kKnownAt = kCamViewState + 4, kJoinedAt = kCamViewState + 8, kBusyAt = kCamViewState + 12;
constexpr std::uint32_t kOwnerAt = kCamViewState + 0x10, kTimerAt = kCamViewState + 0x20, kHandleScratch = kCamViewState + 0x40, kHandledAt = kCamViewState + 0x44;

float to_float(std::uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::uint32_t vcall(ps2::Cpu &c, std::uint32_t object, std::uint32_t function, std::uint32_t adjust, std::uint32_t argument = 0) {
    const std::uint32_t table = ps2::ld32(object);
    return ps2::call_guest(c, ps2::ld32(table + function), object + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(table + adjust))), argument);
}

std::uint32_t camera_type(ps2::Cpu &c, std::uint32_t camera) { return vcall(c, camera, kVtableType, kVtableTypeAdjust); }
std::uint32_t camera_player(ps2::Cpu &c, std::uint32_t camera) { return vcall(c, camera, kVtablePlayer, kVtablePlayerAdjust); }

std::uint32_t base_camera(std::uint32_t slot) { return ps2::ld32(kCamBases + slot * 4); }
std::uint32_t follow_camera(std::uint32_t slot) { return ps2::ld32(kCamFollow + slot * 4); }

std::uint32_t resolve_handle(ps2::Cpu &c, std::uint32_t handle) {
    ps2::st32(kHandleScratch, handle);
    return ps2::call_guest(c, kHandleResolve, kHandleScratch);
}

// True when every player is on his own follow camera (normal play). Scenes and scripted cameras use the game's own rules for the first two players.
bool all_on_follow_cameras(ps2::Cpu &c, std::uint32_t count) {
    for (std::uint32_t slot = 0; slot < count; ++slot) {
        const std::uint32_t camera = base_camera(slot);
        if (!camera || camera != follow_camera(slot) || camera_type(c, camera) != kFollowCameraType) return false;
    }
    return true;
}

Players read_players(ps2::Cpu &c) {
    Players players;
    players.count = player_count();
    for (std::uint32_t slot = 0; slot < players.count && slot < kMaxPlayers; ++slot) {
        const std::uint32_t human = resolve_handle(c, ps2::ld32(game() + kPlayerHandlesOffset + slot * 4));
        players.human[slot] = human;
        if (!human || ps2::ld8(human + kPlayerSlotOffset) == 0xFF || !ps2::call_guest(c, kCamHumanCounts, human)) continue;
        players.viewable |= 1u << slot;
        const std::uint32_t at = kPositions + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(human + kPositionIndexOffset))) * kPositionStride;
        for (std::uint32_t axis = 0; axis < 3; ++axis) players.position[slot][axis] = to_float(ps2::ld32(at + axis * 4));
    }
    return players;
}

std::uint32_t pair_index(std::uint32_t a, std::uint32_t b) { return a * (2 * kMaxPlayers - 1 - a) / 2 + (b - a - 1); }

// Pairs of players who stay within the merge distance for kMergeFrames join; joined players split when they pass the split distance. A player who just became
// viewable joins at once when he is close (a joining player starts beside the leader).
std::uint32_t update_joined(const Players &players) {
    const std::uint32_t known = ps2::ld32(kKnownAt);
    std::uint32_t joined = ps2::ld32(kJoinedAt);
    for (std::uint32_t a = 0; a < players.count; ++a) {
        for (std::uint32_t b = a + 1; b < players.count; ++b) {
            const std::uint32_t pair = pair_index(a, b), timer_at = kTimerAt + pair * 4, bit = 1u << pair;
            const std::uint32_t both = (1u << a) | (1u << b);
            if ((players.viewable & both) != both) {
                joined &= ~bit;
                ps2::st32(timer_at, 0);
                continue;
            }
            const float d2 = distance_sq(players, a, b);
            if (joined & bit) {
                if (d2 > kSplitDistanceSq) joined &= ~bit;
                continue;
            }
            std::uint32_t timer = (known & both) == both ? ps2::ld32(timer_at) : kMergeFrames;
            timer = d2 <= kMergeDistanceSq ? timer + 1 : 0;
            ps2::st32(timer_at, timer);
            if (timer >= kMergeFrames) joined |= bit;
        }
    }
    ps2::st32(kJoinedAt, joined);
    ps2::st32(kKnownAt, players.viewable);
    return joined;
}

// The owner of each viewable player's view: the lowest player of the cluster he joins (a cluster is players who are all joined to each other).
void assign_owners(const Players &players, std::uint32_t joined) {
    std::uint32_t owner[kMaxPlayers];
    for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) owner[slot] = kNoOwner;
    for (std::uint32_t slot = 0; slot < players.count; ++slot) {
        if (!(players.viewable & (1u << slot))) continue;
        owner[slot] = slot;
        for (std::uint32_t head = 0; head < slot; ++head) {
            if (owner[head] != head) continue;
            bool fits = true;
            for (std::uint32_t member = head; member < slot && fits; ++member)
                fits = owner[member] != head || (joined & (1u << pair_index(member, slot)));
            if (fits) {
                owner[slot] = head;
                break;
            }
        }
    }
    for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) ps2::st32(kOwnerAt + slot * 4, owner[slot]);
}

void set_view(ps2::Cpu &c, std::uint32_t slot, bool on) {
    ps2::st32(kCamViewEnable + slot * 4, on ? 1 : 0);
    if (follow_camera(slot)) vcall(c, follow_camera(slot), kVtableSetViewOn, kVtableSetViewOnAdjust, on ? 1 : 0);
}

bool view_on(ps2::Cpu &c, std::uint32_t slot) {
    return follow_camera(slot) && vcall(c, follow_camera(slot), kVtableViewOn, kVtableViewOnAdjust) && ps2::ld32(kCamViewEnable + slot * 4);
}

// A camera that gets a view again starts at its player (a hidden camera has not followed anyone).
void show_camera(ps2::Cpu &c, std::uint32_t slot) {
    const std::uint32_t camera = follow_camera(slot);
    if (!camera) return;
    ps2::call_guest(c, kFollowRefreshLookAt, camera, 1);
    for (std::uint32_t word = 0; word < kFollowLookAtBytes; word += 4) ps2::st32(camera + kFollowLookAtSnap + word, ps2::ld32(camera + kFollowLookAt + word));
    ps2::call_guest(c, kFollowPrime, camera);
}

// A level that turned the views off (CamEnable(3, off)) makes Cameras_LayoutViews show one view whatever the cameras say; while this file decides the views the layout is
// built as if they were on, and the switch goes back to what the level set.
void layout_views(ps2::Cpu &c) {
    const std::uint32_t level_setting = ps2::ld32(kSwitchViewsByCount);
    ps2::st32(kSwitchViewsByCount, 1);
    ps2::call_guest(c, kCamerasLayoutViews);
    ps2::st32(kSwitchViewsByCount, level_setting);
}

void apply_views(ps2::Cpu &c, const Players &players) {
    std::uint32_t was_off = 0, changed = 0;
    for (std::uint32_t slot = 0; slot < players.count; ++slot) {
        const bool want = ps2::ld32(kOwnerAt + slot * 4) == slot, has = view_on(c, slot);
        if (want && !has) was_off |= 1u << slot;
        if (want != has) changed |= 1u << slot;
        if (changed & (1u << slot)) set_view(c, slot, want);
    }
    if (!changed) return;
    P9_TRACE("[p9] views: frame %llu owners %u %u %u %u", static_cast<unsigned long long>(ps2::frame_count()), ps2::ld32(kOwnerAt), ps2::ld32(kOwnerAt + 4), ps2::ld32(kOwnerAt + 8), ps2::ld32(kOwnerAt + 12));
    layout_views(c);
    for (std::uint32_t slot = 0; slot < players.count; ++slot)
        if (was_off & (1u << slot)) show_camera(c, slot);
}

// Extra players have no view outside normal play: the first two follow the game's own rules.
void hide_extra_views(ps2::Cpu &c, std::uint32_t count) {
    bool changed = false;
    for (std::uint32_t slot = kOriginalPlayers; slot < count && slot < kMaxPlayers; ++slot) {
        changed |= ps2::ld32(kCamViewEnable + slot * 4) != 0;
        set_view(c, slot, false);
        ps2::st32(kOwnerAt + slot * 4, kNoOwner);
    }
    if (changed) ps2::call_guest(c, kCamerasLayoutViews);
}

void note_last_target(const Players &players) {
    for (std::uint32_t slot = 0; slot < players.count; ++slot) {
        if (!(players.viewable & (1u << slot))) continue;
        ps2::st32(kLastTarget, ps2::ld32(players.human[slot] + kHumanHandleOffset));
        return;
    }
}

// The decision of decide_views, once a frame (the camera updates call it many times): true when this file made it.
bool decide(ps2::Cpu &c, std::uint32_t count) {
    if (ps2::ld32(kBusyAt)) return true;
    const std::uint32_t frame = static_cast<std::uint32_t>(ps2::frame_count()) + 1;
    if (ps2::ld32(kStampAt) == frame) return ps2::ld32(kHandledAt) != 0;
    ps2::st32(kBusyAt, 1);
    const Players players = read_players(c);
    const bool handled = ps2::ld32(kSwitchViewsByCount) <= 1 && players.viewable && all_on_follow_cameras(c, count);
    if (handled) {
        assign_owners(players, update_joined(players));
        apply_views(c, players);
        note_last_target(players);
    } else {
        hide_extra_views(c, count);
    }
    ps2::st32(kBusyAt, 0);
    ps2::st32(kStampAt, frame);
    ps2::st32(kHandledAt, handled ? 1 : 0);
    return handled;
}

}

// Cameras_DecideViews: handled here with three or more players in normal play; otherwise the original runs (its views for the first two players), after the extra
// players' views are switched off.
bool decide_views(ps2::Cpu &c) {
    const std::uint32_t count = player_count();
    if (count <= kOriginalPlayers || hook_off("views") || !decide(c, count)) return false;
    c.pc = c.r[31].w[0];
    return true;
}

// Cam_Follow_GatherTargets(camera): the camera of a cluster's lowest player frames the cluster, the others their own player.
bool gather_targets(ps2::Cpu &c) {
    const std::uint32_t camera = c.r[4].w[0], count = player_count();
    if (count <= kOriginalPlayers || !camera || hook_off("views") || !ps2::ld32(kHandledAt)) return false;
    const std::uint32_t slot = camera_player(c, camera);
    if (slot >= count || slot >= kMaxPlayers || ps2::ld32(kOwnerAt + slot * 4) == kNoOwner) return false;
    std::uint32_t members = 1u << slot;
    if (ps2::ld32(kOwnerAt + slot * 4) == slot) {
        members = 0;
        for (std::uint32_t other = 0; other < count; ++other)
            if (ps2::ld32(kOwnerAt + other * 4) == slot) members |= 1u << other;
    }
    const Players players = read_players(c);
    members &= players.viewable;
    if (!members) return false;
    std::uint32_t first = 0, second = 0, targets = 1;
    while (!(members & (1u << first))) ++first;
    second = first;
    if (members & (members - 1)) {
        frame_cluster(camera, slot, players, members, first, second);
        targets = 2;
    } else if (ps2::ld32(kOwnerAt + slot * 4) == slot) {
        release_framing(camera, slot);
    }
    const std::uint32_t old_targets = ps2::ld16(camera + kFollowTargetCount);
    ps2::st32(camera + kFollowTargets, players.human[first]);
    ps2::st32(camera + kFollowTargets + 4, targets == 2 ? players.human[second] : 0);
    ps2::st16(camera + kFollowTargetCount, targets);
    ps2::st32(camera + kFollowFocusHandle, ps2::ld32(players.human[first] + kHumanHandleOffset));
    if (targets > old_targets) {
        ps2::st8(camera + kFollowTargetsChanged, 1);
        if (old_targets == 1) ps2::st16(camera + kFollowSingleTargetStep, 0);
    }
    c.pc = c.r[31].w[0];
    return true;
}

// The view a player's HUD panel belongs to (views in slot order of their owners) and the place of the panel among the players who share that view.
bool view_place(std::uint32_t slot, std::uint32_t &view, std::uint32_t &stack) {
    if (slot >= kMaxPlayers || !ps2::ld32(kHandledAt)) return false;
    const std::uint32_t owner = ps2::ld32(kOwnerAt + slot * 4);
    if (owner == kNoOwner) return false;
    view = stack = 0;
    for (std::uint32_t other = 0; other < kMaxPlayers; ++other) {
        const std::uint32_t other_owner = ps2::ld32(kOwnerAt + other * 4);
        if (other < owner && other_owner == other) ++view;
        if (other < slot && other_owner == owner) ++stack;
    }
    return true;
}

}
