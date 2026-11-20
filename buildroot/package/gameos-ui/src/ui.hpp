// gameOS front-end — design system: palette, shapes, type, icons, motion.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <string>
#include <vector>

namespace gameos {

// ---------------------------------------------------------------------------
// Palette (swapped by apply_theme; every screen reads these)
// ---------------------------------------------------------------------------
extern SDL_Color C_TEXT, C_DIM, C_ACCENT, C_PANEL, C_BG, C_TRACK, g_accent;
extern const SDL_Color C_AMBER, C_RED, C_GREEN;
extern int g_tint_top, g_tint_bot;

struct UiFonts {
    TTF_Font* logo = nullptr;
    TTF_Font* title = nullptr;
    TTF_Font* body = nullptr;
    TTF_Font* small = nullptr;
    TTF_Font* clock = nullptr;
    TTF_Font* hero = nullptr;    // Noto 50, screen hero titles
};
extern UiFonts g_ui;

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------
SDL_Color rgba(int r, int g, int b, int a = 255);
SDL_Color with_alpha(SDL_Color c, int a);
SDL_Color mix_color(SDL_Color a, SDL_Color b, float t);
SDL_Color lighten(SDL_Color c, float amount);

// ---------------------------------------------------------------------------
// Base drawing (also used by every existing screen)
// ---------------------------------------------------------------------------
enum class Align { Left, Center, Right };

void fill_rect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c);
void draw_tex(SDL_Renderer* r, SDL_Texture* t, SDL_Rect dst, Uint8 alpha = 255);
void text(SDL_Renderer* r, TTF_Font* f, const std::string& s,
          int x, int y, SDL_Color c, Align align = Align::Left);
int text_width(TTF_Font* f, const std::string& s);
std::vector<std::string> wrap(TTF_Font* f, const std::string& s, int maxw);
std::string ellipsize(TTF_Font* f, const std::string& s, int maxw);
std::string fmt(const char* f, const char* arg);
std::string two(int v);
std::string fmt_duration(long long secs);
std::string fmt_when(long long epoch);
void clear_text_cache();

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------
void rrect(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c);
void rrect_grad(SDL_Renderer* r, SDL_Rect rc, int radius,
                SDL_Color top, SDL_Color bottom);
void rrect_stroke(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c,
                  int width = 1);
void rrect_shadow(SDL_Renderer* r, SDL_Rect rc, int radius, int y_off = 4,
                  int blur = 2, Uint8 alpha = 70);
void circle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c);
void ring(SDL_Renderer* r, int cx, int cy, int radius, int width, SDL_Color c);
void glow(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c,
          int layers = 10, int max_alpha = 60);
void grad_rect(SDL_Renderer* r, SDL_Rect rc, SDL_Color top, SDL_Color bottom);
void thick_line(SDL_Renderer* r, float x1, float y1, float x2, float y2,
                float width, SDL_Color c);
void arc(SDL_Renderer* r, float cx, float cy, float radius,
         float a0, float a1, float width, SDL_Color c);
void polygon(SDL_Renderer* r, const SDL_FPoint* pts, int n, SDL_Color c);
void set_clip(SDL_Renderer* r, SDL_Rect* rc);

// ---------------------------------------------------------------------------
// Vector icons, drawn inside a size x size square at (x, y)
// ---------------------------------------------------------------------------
void icon_chevron(SDL_Renderer* r, int x, int y, int size, SDL_Color c, bool left);
void icon_play(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_pause(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_star(SDL_Renderer* r, int x, int y, int size, SDL_Color c, bool filled);
void icon_wifi(SDL_Renderer* r, int x, int y, int size, SDL_Color c, int level = 3);
void icon_bt(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_refresh(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_gear(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_power(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_clock(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_trash(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_check(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_lock(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_arrow_down(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_arrow_up(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_speaker(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_sun(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_home(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_globe(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_gamepad(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_image(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_trophy(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_download(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_keyboard(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void icon_backspace(SDL_Renderer* r, int x, int y, int size, SDL_Color c);
void battery_icon(SDL_Renderer* r, int x, int y, int w, int h,
                  int pct, bool charging);

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------
void card(SDL_Renderer* r, SDL_Rect rc, int radius, bool focus,
          Uint8 alpha = 242);
void button(SDL_Renderer* r, SDL_Rect rc, const std::string& label,
            bool primary, bool focus, bool pressed = false);
void pill(SDL_Renderer* r, int x, int y, const std::string& label,
          SDL_Color bg, SDL_Color fg, int h = 34, int pad = 16);
int pill_width(TTF_Font* f, const std::string& label, int pad = 32);
void progress_bar(SDL_Renderer* r, SDL_Rect rc, float p,
                  SDL_Color fill, SDL_Color track);
void slider(SDL_Renderer* r, SDL_Rect track, float p,
            bool active, SDL_Color fill);
void toggle(SDL_Renderer* r, SDL_Rect rc, float value, SDL_Color accent);
void focus_ring(SDL_Renderer* r, SDL_Rect rc, int radius, Uint8 alpha);
void icon_badge(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color bg);

// ---------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------
float damp(float cur, float target, float rate = 14.0f);
float ease_out(float p);
float ease_in_out(float p);
float ease_out_back(float p);
float spring_step(float cur, float& vel, float target, float dt,
                  float stiffness = 190.0f, float damping = 24.0f);
float now_sec();

// ---------------------------------------------------------------------------
// Touch hit registry. The bottom deck registers every tappable / draggable
// rectangle while it renders; the touch dispatcher tests the last frame.
// ---------------------------------------------------------------------------
struct UiHit {
    SDL_Rect rect;
    int id;
    int a;
    int b;
};
void ui_hit_begin();
void ui_hit_add(SDL_Rect rc, int id, int a = 0, int b = 0);
const UiHit* ui_hit_at(int x, int y);

} // namespace gameos
