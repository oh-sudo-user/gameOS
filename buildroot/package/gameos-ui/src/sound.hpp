// gameOS front-end — UI sound effects.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace gameos {

enum SoundId {
    SND_MOVE,     // cursor movement
    SND_SWITCH,   // console / view switch
    SND_ADJUST,   // settings value tweak
    SND_CONFIRM,  // confirm / launch
    SND_BACK,     // back
    SND_FAV,      // favorite toggle
    SND_ERROR,    // gentle "not yet"
    SND_COUNT
};

// Open the audio device and load the WAV set from dir. Failure is not
// fatal: the UI simply runs silent (sound_play becomes a no-op).
bool sound_init(const std::string& dir);
void sound_play(SoundId id);
void sound_quit();

} // namespace gameos