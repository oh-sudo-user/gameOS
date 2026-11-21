// gameOS front-end — frame command buffer and damage tracking.
//
// Every design-system primitive funnels through this module. When recording
// is off (the default) each call goes straight to SDL, exactly as before.
// When it is on, the calls are recorded per renderer and target, diffed
// against the previous frame, and only the damaged rectangles are replayed.
// Recording never changes what a frame looks like; it only decides how much
// of it has to be drawn again.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <vector>

namespace gameos {
namespace drawcall {

struct Cmd {
    enum class Kind : Uint8 { Fill, Tex, Geom };

    Kind kind = Kind::Fill;
    SDL_Rect dst{};
    SDL_Rect src{};
    bool has_src = false;
    SDL_Color color{ 0, 0, 0, 0 };
    Uint8 alpha = 255;
    SDL_Texture* tex = nullptr;
    SDL_Rect clip{};
    bool has_clip = false;
    SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
    Uint32 v_off = 0, v_n = 0;
    Uint32 i_off = 0, i_n = 0;
    uint64_t sig = 0;
    SDL_Rect bounds{};
};

struct Frame {
    std::vector<Cmd> cmds;
    std::vector<SDL_Vertex> verts;
    std::vector<int> idx;
    void clear() { cmds.clear(); verts.clear(); idx.clear(); }
};

struct Result {
    int ops = 0;        // commands recorded this frame
    int replays = 0;    // commands actually submitted during replay
    int rects = 0;      // dirty rectangles replayed
    double dirty = 0;   // repainted area / panel area, before clipping
    bool full = false;  // whole panel was replayed
    bool skipped = false; // nothing visible changed; no draws submitted
};

// GAMEOS_UI_DIRTY=1 or the marker /userdata/system/ui-dirty.on turns the
// recorder on. GAMEOS_UI_DIRTY=0 always wins over the marker.
bool enabled();
bool active(SDL_Renderer* r);

// Returns true when calls for this renderer should be recorded. The caller
// then draws the frame normally and must call end() + replay().
bool begin(SDL_Renderer* r, SDL_Texture* target, int w, int h);
void end(SDL_Renderer* r);
void replay(SDL_Renderer* r);

// Recording-aware replacements for the raw SDL calls.
void clip(SDL_Renderer* r, const SDL_Rect* rc);
void fill(SDL_Renderer* r, SDL_Rect rc, SDL_Color c);
void tex(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src,
         SDL_Rect dst, Uint8 alpha);
void geom(SDL_Renderer* r, const SDL_Vertex* v, int n,
          const int* idx, int m);

// Repaint the last recorded frame in full into the current target. Used by
// the pixel-diff harness and by diagnostics that need a guaranteed repaint.
bool replay_full(SDL_Renderer* r);

// The previous frame references textures that may be reloaded or evicted.
void invalidate();
// Use instead of SDL_DestroyTexture for anything a frame may reference.
void destroy_texture(SDL_Texture* t);
// Destroy textures deferred until after the last replay.
void flush();
void shutdown();

const Result& last(SDL_Renderer* r);

// Pure helpers, exposed for the host tests.
SDL_Rect intersect_rect(const SDL_Rect& a, const SDL_Rect& b);
bool rects_equal(const SDL_Rect& a, const SDL_Rect& b);
std::vector<SDL_Rect> merge_rects(std::vector<SDL_Rect> in, int w, int h,
                                  int max_rects, double max_area, bool* full);
uint64_t hash_geometry(const SDL_Vertex* v, int n, const int* idx, int m);

} // namespace drawcall
} // namespace gameos
