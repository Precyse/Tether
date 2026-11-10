#include "p9_camera.hpp"

#include "p9_game.hpp"
#include "p9_framing.hpp"
#include "p9_view_data.hpp"
#include "ps2/system.hpp"

#include <algorithm>
#include <cstring>

// Which views the screen shows with three or four players. The game's own merge/split logic (follow_cam_share_or_split and its helpers) is written for exactly two cameras
// and does nothing for three or four. With three or more players in normal play (every player on a follow camera) this file decides instead, between two layouts only:
//   COMBINED: one view, shown by the camera of the lowest viewable player, which frames every viewable player (p9_framing.cpp).
//   SPLIT: one view for every player the game has, in a fixed place by player number (player 1 top left, 2 top right, 3 bottom left, 4 bottom right: the game's own grid,
//          views_arrange_grid, mode 2, fills its places in player order), each camera following its own player only.
// SPLIT frames everyone with the lowest viewable player's camera first and merges once that camera holds them inside kArrivedFrame. COMBINED goes to SPLIT the moment a viewable player would be outside kSafeFrame of the picture with the combined camera as far back as it goes (or is, once it has settled). SPLIT goes back to
// COMBINED only after every viewable player has fitted kMergeFrame for kMergeHoldFrames in a row. The split screen setting can hold the layout on SPLIT (p9_split_mode.cpp).
namespace warriors::p9 {

namespace {

constexpr std::uint32_t kViewsArrangeGrid = 0x00122ED0, kViewPlayerCounts = 0x00123500, kHandleToObject = 0x00390268, kFollowCamPrime = 0x00127C10;
constexpr std::uint32_t kFollowCamAimRefresh = 0x00127D48;
constexpr std::uint32_t kLastTarget = 0x005D91B8, kSwitchViewsByCount = 0x0050B1C0;
constexpr std::uint32_t kPositions = 0x00714B00, kPositionStride = 0x20, kPositionIndexOffset = 0x92;
constexpr std::uint32_t kHumanHandleOffset = 0x90, kHumanIsInCombat = 0x00237DB8;   // HuInCombat(handle): whether the human fights (his brain is engaged)

// Camera (ICamera) layout.
constexpr std::uint32_t kVtableType = 0x1EC, kVtableTypeAdjust = 0x1E8, kVtablePlayer = 0x27C, kVtablePlayerAdjust = 0x278;
constexpr std::uint32_t kVtableSetViewOn = 0x294, kVtableSetViewOnAdjust = 0x290, kVtableViewOn = 0x284, kVtableViewOnAdjust = 0x280;
constexpr std::uint32_t kFollowCameraType = 2;
constexpr std::uint32_t kFollowTargets = 0x314, kFollowTargetCount = 0x444, kFollowFocusHandle = 0x31C, kFollowTargetsChanged = 0x469, kFollowSingleTargetStep = 0x446;
constexpr std::uint32_t kFollowLookAt = 0x180, kFollowLookAtSnap = 0x240, kFollowLookAtBytes = 16;

// The frames of the layout rules: the share of the half picture every viewable player must stay inside to remain COMBINED, the tighter share they must fit to become
// COMBINED again, and how long (frames at 60 Hz, counted from the frame count: the camera updates run at 30 Hz) they must fit it first.
constexpr float kSafeFrame = 0.9f, kMergeFrame = 0.75f;
constexpr std::uint32_t kMergeHoldFrames = 60;
// COMBINED also splits when its camera still shows a player outside kSafeFrame once the layout has lasted kMinLayoutFrames. A try that a wall defeats (the camera cannot stand where the combined view needs it) limits the next merge to the
// distance it reached (kReachAt), and that limit grows back toward the cap by kReachRecovery metres a frame.
constexpr std::uint32_t kMinLayoutFrames = 120;   // a layout lasts this long before a camera that cannot show everyone (not a player beyond the cap) ends it, or a merge may begin
constexpr float kArrivedFrame = 0.85f;           // the lead camera has arrived when every viewable player is inside this share of the picture
constexpr float kReachRecovery = 1.0f / 60;
constexpr std::uint32_t kCombatHoldFrames = 300;     // frames a fight keeps counting after the last blow
constexpr std::uint32_t kPrepareFrames = 90;     // frames the lead camera gets to frame everyone before a merge is given up
constexpr std::uint32_t kMaxElapsedFrames = 4;   // longest gap between two decisions that counts as time passed (a state just loaded has a stale stamp)
constexpr std::uint32_t kNoOwner = 0xFF;
enum Layout : std::uint32_t { kUndecided = 0, kSplit = 1, kCombined = 2 };   // kUndecided: no decision yet (a fresh game, or a state from before the layouts)

// View state in extra RAM (p9_game.hpp, kCamViewState; the words that outlive a frame are named there).
// kStampAt: the frame (plus one) of the last decision; kHandledAt: whether this file made it; kBusyAt: set while the decision runs.
constexpr std::uint32_t kStampAt = kCamViewState, kBusyAt = kCamViewState + 12;
constexpr std::uint32_t kOwnerAt = kCamViewState + 0x10, kHandleScratch = kCamViewState + 0x40, kHandledAt = kCamViewState + 0x44;

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
    return ps2::call_guest(c, kHandleToObject, kHandleScratch);
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
        if (!human || ps2::ld8(human + kPlayerSlotOffset) == 0xFF) continue;
        players.joined |= 1u << slot;
        if (!ps2::call_guest(c, kViewPlayerCounts, human)) continue;
        players.viewable |= 1u << slot;
        const std::uint32_t at = kPositions + static_cast<std::uint32_t>(static_cast<std::int16_t>(ps2::ld16(human + kPositionIndexOffset))) * kPositionStride;
        for (std::uint32_t axis = 0; axis < 3; ++axis) players.position[slot][axis] = to_float(ps2::ld32(at + axis * 4));
    }
    return players;
}

std::uint32_t lowest_slot(std::uint32_t mask) {
    std::uint32_t slot = 0;
    while (!(mask & (1u << slot))) ++slot;
    return slot;
}

bool shares_view(std::uint32_t members) { return members & (members - 1); }

// A fight counts for kCombatHoldFrames after a viewable player was last engaged, so the cap does not flicker with single blows.
void note_combat(ps2::Cpu &c, const Players &players) {
    const std::uint32_t frame = static_cast<std::uint32_t>(ps2::frame_count()) + 1;
    for (std::uint32_t slot = 0; slot < players.count; ++slot) {
        if (!(players.viewable & (1u << slot)) || !ps2::call_guest(c, kHumanIsInCombat, ps2::ld32(players.human[slot] + kHumanHandleOffset))) continue;
        if (!ps2::ld32(kCombatAt) || frame - ps2::ld32(kCombatAt) >= kCombatHoldFrames) P9_TRACE("[p9] combat: frame %u on", frame);
        ps2::st32(kCombatAt, frame);
        break;
    }
    const std::uint32_t last = ps2::ld32(kCombatAt);
    const bool fighting = last && frame - last < kCombatHoldFrames;
    if (!fighting && read_float(kCapAt) == kFightMaxDistance) P9_TRACE("[p9] combat: frame %u off", frame);
    write_float(kCapAt, fighting ? kFightMaxDistance : kMaxCombinedDistance);
}

// How far back the combined camera may stand: the cap now (kMaxCombinedDistance, kFightMaxDistance in a fight).
float combined_cap() {
    const float cap = read_float(kCapAt);
    return cap > 0 ? cap : kMaxCombinedDistance;
}

// How far back the combined camera is expected to stand: the cap, or less where a wall kept it close.
float combined_reach() {
    const float reach = read_float(kReachAt);
    return reach > 0 ? std::min(reach, combined_cap()) : combined_cap();
}

// The layout for this decision, `elapsed` frames after the last (the camera updates run at 30 Hz). The lowest viewable player's camera is the one that would show the
// combined view, so it gives the direction the fit is worked out in.
Layout next_layout(const Players &players, std::uint32_t elapsed) {
    const Layout layout = static_cast<Layout>(ps2::ld32(kLayoutAt));
    const std::uint32_t camera = players.viewable ? follow_camera(lowest_slot(players.viewable)) : 0;
    if (split_always() || !camera) return kSplit;
    // The first decision starts combined when the lead camera, as it stands, already holds every viewable player inside kSafeFrame.
    if (layout == kUndecided) return picture_share(camera, players, players.viewable) <= kSafeFrame ? kCombined : kSplit;
    const std::uint32_t age = ps2::ld32(kLayoutFramesAt) + elapsed;
    ps2::st32(kLayoutFramesAt, age);
    if (layout == kCombined) {
        // A player beyond the cap ends the layout at once; a camera that cannot show everyone (a wall) only after the layout has lasted kMinLayoutFrames.
        if (!fits_one_view(camera, players, players.viewable, kSafeFrame, combined_cap())) return kSplit;
        if (age < kMinLayoutFrames || picture_share(camera, players, players.viewable) <= kSafeFrame) return kCombined;
        if (camera_blocked(camera)) write_float(kReachAt, camera_distance(camera));
        return kSplit;
    }
    const float reach = std::min(combined_reach() + kReachRecovery * static_cast<float>(elapsed), combined_cap());
    write_float(kReachAt, reach);
    const std::uint32_t fitted = fits_one_view(camera, players, players.viewable, kMergeFrame, reach) ? ps2::ld32(kFitFramesAt) + elapsed : 0;
    ps2::st32(kFitFramesAt, fitted);
    if (fitted < kMergeHoldFrames || age < kMinLayoutFrames) {
        ps2::st32(kPrepareAt, 0);
        ps2::st32(kPrepareFramesAt, 0);
        return kSplit;
    }
    // The players have fitted long enough: the lead camera frames them all first (gather_targets, while its view still shows its own quarter) and the layout merges once it
    // holds everyone inside kArrivedFrame. A world that keeps it from getting there ends the try, so a merge never starts with players outside the picture.
    const std::uint32_t lead = lowest_slot(players.viewable), trying = ps2::ld32(kPrepareFramesAt) + elapsed;
    ps2::st32(kPrepareAt, lead + 1);
    ps2::st32(kPrepareFramesAt, trying);
    if (trying > 0 && picture_share(camera, players, players.viewable) <= kArrivedFrame) return kCombined;
    if (trying > kPrepareFrames) {
        write_float(kReachAt, camera_distance(camera));
        ps2::st32(kFitFramesAt, 0);
        ps2::st32(kPrepareAt, 0);
        ps2::st32(kPrepareFramesAt, 0);
    }
    return kSplit;
}

// The owner of each player's view: the lowest viewable player for all of them in COMBINED, the player himself in SPLIT.
void assign_owners(const Players &players, Layout layout) {
    for (std::uint32_t slot = 0; slot < kMaxPlayers; ++slot) {
        std::uint32_t owner = kNoOwner;
        if (players.joined & (1u << slot)) owner = layout == kCombined ? lowest_slot(players.viewable) : slot;
        ps2::st32(kOwnerAt + slot * 4, owner);
    }
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
    ps2::call_guest(c, kFollowCamAimRefresh, camera, 1);
    for (std::uint32_t word = 0; word < kFollowLookAtBytes; word += 4) ps2::st32(camera + kFollowLookAtSnap + word, ps2::ld32(camera + kFollowLookAt + word));
    ps2::call_guest(c, kFollowCamPrime, camera);
}

// A level that turned the views off (CamEnable(3, off)) makes views_arrange_grid show one view whatever the cameras say; while this file decides the views the layout is
// built as if they were on, and the switch goes back to what the level set.
void layout_views(ps2::Cpu &c) {
    const std::uint32_t level_setting = ps2::ld32(kSwitchViewsByCount);
    ps2::st32(kSwitchViewsByCount, 1);
    ps2::call_guest(c, kViewsArrangeGrid);
    ps2::st32(kSwitchViewsByCount, level_setting);
}

// Turns the views on or off to match the owners. Every camera that gets a view starts at its player; so does the camera that showed the combined view when the layout
// splits (it stands where the players were framed, which may not hold its own player).
void apply_views(ps2::Cpu &c, const Players &players, bool became_split) {
    std::uint32_t changed = 0, snap = became_split ? players.joined : 0;
    for (std::uint32_t slot = 0; slot < players.count; ++slot) {
        const bool want = ps2::ld32(kOwnerAt + slot * 4) == slot, has = view_on(c, slot);
        if (want == has) continue;
        changed |= 1u << slot;
        if (want) snap |= 1u << slot;
        set_view(c, slot, want);
    }
    if (!changed && !snap) return;
    P9_TRACE("[p9] views: frame %llu owners %u %u %u %u", static_cast<unsigned long long>(ps2::frame_count()), ps2::ld32(kOwnerAt), ps2::ld32(kOwnerAt + 4), ps2::ld32(kOwnerAt + 8), ps2::ld32(kOwnerAt + 12));
    if (changed) layout_views(c);
    for (std::uint32_t slot = 0; slot < players.count; ++slot)
        if (snap & (1u << slot)) show_camera(c, slot);
}

// Extra players have no view outside normal play: the first two follow the game's own rules.
void hide_extra_views(ps2::Cpu &c, std::uint32_t count) {
    bool changed = false;
    for (std::uint32_t slot = kOriginalPlayers; slot < count && slot < kMaxPlayers; ++slot) {
        changed |= ps2::ld32(kCamViewEnable + slot * 4) != 0;
        set_view(c, slot, false);
        ps2::st32(kOwnerAt + slot * 4, kNoOwner);
    }
    if (changed) ps2::call_guest(c, kViewsArrangeGrid);
}

void note_last_target(const Players &players) {
    for (std::uint32_t slot = 0; slot < players.count; ++slot) {
        if (!(players.viewable & (1u << slot))) continue;
        ps2::st32(kLastTarget, ps2::ld32(players.human[slot] + kHumanHandleOffset));
        return;
    }
}

// The layout of this frame: decided, kept in the state and applied.
void update_layout(ps2::Cpu &c, const Players &players, std::uint32_t elapsed) {
    const Layout before = static_cast<Layout>(ps2::ld32(kLayoutAt)), layout = next_layout(players, elapsed);
    if (layout != before) {
        ps2::st32(kLayoutAt, layout);
        ps2::st32(kFitFramesAt, 0);
        ps2::st32(kLayoutFramesAt, 0);
        ps2::st32(kPrepareAt, 0);
        P9_TRACE("[p9] layout: frame %llu %s viewable %x", static_cast<unsigned long long>(ps2::frame_count()), layout == kCombined ? "combined" : "split", players.viewable);
    }
    assign_owners(players, layout);
    apply_views(c, players, layout == kSplit && before != kSplit);
}

// The decision of decide_views, once a frame (the camera updates call it many times): true when this file made it.
bool decide(ps2::Cpu &c, std::uint32_t count) {
    if (ps2::ld32(kBusyAt)) return true;
    const std::uint32_t frame = static_cast<std::uint32_t>(ps2::frame_count()) + 1, stamp = ps2::ld32(kStampAt);
    if (stamp == frame) return ps2::ld32(kHandledAt) != 0;
    const std::uint32_t elapsed = std::min(frame - stamp, kMaxElapsedFrames);
    ps2::st32(kBusyAt, 1);
    const Players players = read_players(c);
    const bool handled = ps2::ld32(kSwitchViewsByCount) <= 1 && players.joined && all_on_follow_cameras(c, count);
    if (handled != (ps2::ld32(kHandledAt) != 0)) P9_TRACE("[p9] normal play: frame %llu %s", static_cast<unsigned long long>(ps2::frame_count()), handled ? "on" : "off");
    if (handled) {
        poll_split_request();
        note_combat(c, players);
        update_layout(c, players, elapsed);
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

// views_choose_sharing: handled here with three or more players in normal play; otherwise the original runs (its views for the first two players), after the extra
// players' views are switched off.
bool decide_views(ps2::Cpu &c) {
    const std::uint32_t count = player_count();
    if (count <= kOriginalPlayers || hook_off("views") || !decide(c, count)) return false;
    c.pc = c.r[31].w[0];
    return true;
}

// follow_cam_collect_targets(camera): the camera of the combined view frames every viewable player, every other camera its own player.
bool gather_targets(ps2::Cpu &c) {
    const std::uint32_t camera = c.r[4].w[0], count = player_count();
    if (count <= kOriginalPlayers || !camera || hook_off("views") || !ps2::ld32(kHandledAt)) return false;
    const std::uint32_t slot = camera_player(c, camera);
    if (slot >= count || slot >= kMaxPlayers || ps2::ld32(kOwnerAt + slot * 4) == kNoOwner) return false;
    const Players players = read_players(c);
    const bool owns_view = ps2::ld32(kOwnerAt + slot * 4) == slot, preparing = ps2::ld32(kPrepareAt) == slot + 1;
    std::uint32_t members = 0;
    for (std::uint32_t other = 0; other < count && owns_view; ++other)
        if (ps2::ld32(kOwnerAt + other * 4) == slot) members |= 1u << other;
    members = preparing ? players.viewable : members & players.viewable;
    if (!shares_view(members)) members = 1u << slot;
    std::uint32_t first = lowest_slot(members), second = first, targets = 1;
    if (shares_view(members)) {
        frame_cluster(camera, slot, players, members, combined_cap(), first, second);
        targets = 2;
    } else if (owns_view) {
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

bool views_handled() { return ps2::ld32(kHandledAt) != 0; }

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
