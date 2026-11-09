#include "warriors_display.hpp"

#include "p9_camera.hpp"
#include "ps2/memory.hpp"
#include "ps2/video.hpp"

#include <cstring>

namespace warriors {

namespace {

// Cameras_SetAspectAll (0x00122ca0) stores the game camera's aspect ratio here; RwDevice_SetWidescreen (0x00194e28) sets it to 4:3 (1.3333) or the
// anamorphic 16:9 setting (1.6667, or 1.59 for its other wide mode). The HUD aspect is stored beside it (0x0050b208).
constexpr std::uint32_t kGameAspect = 0x0050B204;
constexpr float kWideAspectFrom = 1.5f;   // between 4:3 and the wide settings

bool game_is_wide() {
    float aspect;
    const std::uint32_t bits = ps2::ld32(kGameAspect);
    std::memcpy(&aspect, &bits, sizeof aspect);
    return aspect > kWideAspectFrom;
}

}

void install_display_hints() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    ps2::set_wide_probe(game_is_wide);
    ps2::set_view_probe(p9::local_view);
}

}
