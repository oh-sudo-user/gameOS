// gameOS front-end — UI sound effects.
//
// One SDL audio device, callback-mixed. Every sound owns a voice;
// retriggering restarts that voice from the top, so rapid navigation
// ticks never queue up or smear. WAVs are converted to the device
// format once at init. Output follows the system ALSA route (and thus
// the volume setting and any connected Bluetooth audio).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sound.hpp"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>

namespace gameos {
namespace {

SDL_AudioDeviceID g_dev = 0;
bool g_ok = false;

struct Voice {
    Uint8* buf = nullptr;   // converted PCM, owned here
    Uint32 len = 0;         // bytes
    Uint32 pos = 0;
    bool active = false;
};
Voice g_voice[SND_COUNT];

const char* g_name[SND_COUNT] = {
    "move", "switch", "adjust", "confirm", "back", "fav", "error"
};

void mix_cb(void*, Uint8* stream, int len)
{
    std::memset(stream, 0, len);
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    const int n = len / 2;
    for (int v = 0; v < SND_COUNT; v++) {
        Voice& vc = g_voice[v];
        if (!vc.active) continue;
        const int16_t* src = reinterpret_cast<const int16_t*>(vc.buf + vc.pos);
        const Uint32 remain = (vc.len - vc.pos) / 2;
        const int k = (remain < (Uint32)n) ? (int)remain : n;
        for (int i = 0; i < k; i++) {
            int s = out[i] + src[i];
            if (s > 32767) s = 32767;
            else if (s < -32768) s = -32768;
            out[i] = (int16_t)s;
        }
        vc.pos += (Uint32)k * 2;
        if (vc.pos >= vc.len) vc.active = false;
    }
}

} // namespace

bool sound_init(const std::string& dir)
{
    if (g_ok) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;

    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;          // ~12 ms latency: ticks feel instant
    want.callback = mix_cb;
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_dev == 0) {
        fprintf(stderr, "gameos-ui: audio open failed (%s) - running silent\n",
                SDL_GetError());
        return false;
    }

    for (int i = 0; i < SND_COUNT; i++) {
        const std::string path = dir + "/" + g_name[i] + ".wav";
        SDL_AudioSpec spec{};
        Uint8* raw = nullptr;
        Uint32 rawlen = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &raw, &rawlen)) {
            fprintf(stderr, "gameos-ui: cannot load %s: %s\n",
                    path.c_str(), SDL_GetError());
            sound_quit();
            return false;
        }
        SDL_AudioCVT cvt{};
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
                              have.format, have.channels, have.freq) < 0) {
            SDL_FreeWAV(raw);
            sound_quit();
            return false;
        }
        cvt.buf = (Uint8*)SDL_malloc(rawlen * cvt.len_mult);
        std::memcpy(cvt.buf, raw, rawlen);
        cvt.len = rawlen;
        SDL_FreeWAV(raw);
        if (SDL_ConvertAudio(&cvt) < 0) {
            SDL_free(cvt.buf);
            sound_quit();
            return false;
        }
        g_voice[i].buf = cvt.buf;
        g_voice[i].len = cvt.len_cvt;
    }

    g_ok = true;
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}

void sound_play(SoundId id)
{
    if (!g_ok || (unsigned)id >= SND_COUNT || !g_voice[id].buf) return;
    SDL_LockAudioDevice(g_dev);
    g_voice[id].pos = 0;
    g_voice[id].active = true;
    SDL_UnlockAudioDevice(g_dev);
}

void sound_quit()
{
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);   // release ALSA for the emulator
        g_dev = 0;
    }
    for (int i = 0; i < SND_COUNT; i++) {
        if (g_voice[i].buf) SDL_free(g_voice[i].buf);
        g_voice[i] = Voice{};
    }
    g_ok = false;
}

} // namespace gameos