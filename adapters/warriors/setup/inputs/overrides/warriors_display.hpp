#pragma once

// Display hints of The Warriors (SLUS_212.15) for the runtime's output stage. Local only, like the generated code.
namespace warriors {

// Tells the runtime when the game renders for a 16:9 screen (its Options > Video choice), so the window shows the picture at 16:9. Reads guest
// memory only; the guest never sees it. Safe to call every frame.
void install_display_hints();

}
