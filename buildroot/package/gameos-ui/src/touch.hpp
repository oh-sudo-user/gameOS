// gameOS front-end — GT911 touch input for the lower panel.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace gameos {

enum class TouchPhase { Down, Move, Up };

struct TouchEvent {
    TouchPhase phase = TouchPhase::Down;
    float x = 0;
    float y = 0;
};

// Opens the panel touchscreen (first evdev node with ABS_MT_POSITION_X/Y,
// preferring the Goodix panel) and maps its raw range to the 1024x768 design
// space, so the same deck code works on the 640x480 RG DS.
void touch_init();
// Pops one pending event. Call in a loop every frame.
bool touch_poll(TouchEvent& ev);
bool touch_available();
void touch_shutdown();

} // namespace gameos
