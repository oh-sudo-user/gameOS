// gameOS front-end — dual-panel display mapping.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace gameos {

// SDL display indices for the two DSI panels. bottom is -1 when there is only
// one display (development builds under X11, or a panel that failed to probe).
struct Panels {
    int top = 0;
    int bottom = -1;
};

// Maps SDL's display indices to the physical connectors. The kmsdrm backend
// does not expose connector names through SDL_GetDisplayName(), so the DRM
// connector list is enumerated directly, in the same order SDL uses.
// GAMEOS_TOP_DISPLAY=n overrides the result (used by the test harness).
Panels panel_displays();

} // namespace gameos
