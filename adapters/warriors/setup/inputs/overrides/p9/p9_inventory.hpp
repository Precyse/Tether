#pragma once

#include <cstdint>

namespace warriors::p9 {

// A joining player's inventory blocks (live and checkpoint) are made from slot 1's definitions, with no counts.
void inventory_init_slot(std::uint32_t slot);

}
