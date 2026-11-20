// gameOS front-end — design system implementation.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.hpp"

#include "i18n.hpp"
#include "perf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>

namespace gameos {

SDL_Color C_TEXT   = { 232, 236, 247, 255 };
SDL_Color C_DIM    = { 138, 147, 178, 255 };
SDL_Color C_ACCENT = {  53, 208, 186, 255 };
SDL_Color C_PANEL  = {  20,  27,  51, 235 };
SDL_Color C_BG     = {  11,  16,  32, 255 };
SDL_Color C_TRACK  = {  28,  38,  58, 255 };
const SDL_Color C_AMBER = { 255, 180,  84, 255 };
const SDL_Color C_RED   = { 255,  99,  99, 255 };
const SDL_Color C_GREEN = { 105, 220, 140, 255 };
SDL_Color g_accent = C_ACCENT;
int g_tint_top = 14, g_tint_bot = 26;

UiFonts g_ui;

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------
SDL_Color rgba(int r, int g, int b, int a) {
    return SDL_Color{ (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a };
}
SDL_Color with_alpha(SDL_Color c, int a) {
    c.a = (Uint8)std::clamp(a, 0, 255);
    return c;
}
SDL_Color mix_color(SDL_Color a, SDL_Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return SDL_Color{
        (Uint8)(a.r + (b.r - a.r) * t),
        (Uint8)(a.g + (b.g - a.g) * t),
        (Uint8)(a.b + (b.b - a.b) * t),
        (Uint8)(a.a + (b.a - a.a) * t)
    };
}
SDL_Color lighten(SDL_Color c, float amount) {
    return mix_color(c, rgba(255, 255, 255, c.a), amount);
}

static bool dark_theme() {
    return (C_BG.r * 299 + C_BG.g * 587 + C_BG.b * 114) / 1000 < 128;
}
static SDL_Color on_accent() {
    int lum = (C_ACCENT.r * 299 + C_ACCENT.g * 587 + C_ACCENT.b * 114) / 1000;
    return lum > 150 ? rgba(10, 16, 28, 255) : rgba(255, 255, 255, 255);
}
static SDL_Color hairline() {
    return dark_theme() ? rgba(255, 255, 255, 16) : rgba(15, 22, 40, 20);
}

// ---------------------------------------------------------------------------
// Base drawing
// ---------------------------------------------------------------------------
void fill_rect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c)
{
    perf::note_draw();
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(r, &rc);
}

void draw_tex(SDL_Renderer* r, SDL_Texture* t, SDL_Rect dst, Uint8 alpha)
{
    if (!t) return;
    perf::note_draw();
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(r, t, nullptr, &dst);
}

void set_clip(SDL_Renderer* r, SDL_Rect* rc)
{
    SDL_RenderSetClipRect(r, rc);
}

// Text cache, keyed by (renderer, font, colour, string).
struct TextKey {
    SDL_Renderer* r;
    TTF_Font* f;
    SDL_Color c;
    std::string s;
    bool operator<(const TextKey& o) const {
        if (r != o.r) return r < o.r;
        if (f != o.f) return f < o.f;
        if (c.r != o.c.r) return c.r < o.c.r;
        if (c.g != o.c.g) return c.g < o.c.g;
        if (c.b != o.c.b) return c.b < o.c.b;
        if (c.a != o.c.a) return c.a < o.c.a;
        return s < o.s;
    }
};
struct CachedText {
    SDL_Texture* t;
    Uint32 used;
};
static std::map<TextKey, CachedText> g_text_cache;
static Uint32 g_text_tick = 0;

void clear_text_cache()
{
    for (auto& kv : g_text_cache) SDL_DestroyTexture(kv.second.t);
    g_text_cache.clear();
}

void text(SDL_Renderer* r, TTF_Font* f, const std::string& s,
          int x, int y, SDL_Color c, Align align)
{
    if (!r || !f || s.empty()) return;
    TextKey key{ r, f, c, s };
    SDL_Texture* t;
    int tw, th;
    auto it = g_text_cache.find(key);
    if (it != g_text_cache.end()) {
        t = it->second.t;
        it->second.used = ++g_text_tick;
        SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
    } else {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
        if (!surf) return;
        t = SDL_CreateTextureFromSurface(r, surf);
        tw = surf->w; th = surf->h;
        SDL_FreeSurface(surf);
        if (!t) return;
        if (g_text_cache.size() >= 160) {
            std::vector<std::pair<Uint32, const TextKey*>> order;
            order.reserve(g_text_cache.size());
            for (auto& kv : g_text_cache) order.push_back({ kv.second.used, &kv.first });
            std::sort(order.begin(), order.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            size_t drop = std::max<size_t>(8, order.size() / 4);
            for (size_t i = 0; i < drop; i++) {
                auto hit = g_text_cache.find(*order[i].second);
                if (hit != g_text_cache.end()) {
                    SDL_DestroyTexture(hit->second.t);
                    g_text_cache.erase(hit);
                }
            }
        }
        g_text_cache.emplace(key, CachedText{ t, ++g_text_tick });
    }
    int dx = x;
    if (align == Align::Center) dx = x - tw / 2;
    else if (align == Align::Right) dx = x - tw;
    SDL_Rect dst = { dx, y, tw, th };
    perf::note_draw();
    SDL_SetTextureAlphaMod(t, 255);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(r, t, nullptr, &dst);
}

int text_width(TTF_Font* f, const std::string& s)
{
    int w = 0, h = 0;
    if (f) TTF_SizeUTF8(f, s.c_str(), &w, &h);
    return w;
}

std::vector<std::string> wrap(TTF_Font* f, const std::string& s, int maxw)
{
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0, sp = std::string::npos;
    while (i < s.size()) {
        if (s[i] == '\n') {
            lines.push_back(cur); cur.clear(); sp = std::string::npos; i++; continue;
        }
        size_t len = 1;
        unsigned char ch = (unsigned char)s[i];
        if (ch >= 0xF0) len = 4; else if (ch >= 0xE0) len = 3;
        else if (ch >= 0xC0) len = 2;
        std::string piece = s.substr(i, len);
        if (text_width(f, cur + piece) > maxw && !cur.empty()) {
            if (sp != std::string::npos && piece != " ") {
                lines.push_back(cur.substr(0, sp));
                cur = cur.substr(sp + 1);
            } else {
                lines.push_back(cur);
                cur.clear();
            }
            sp = cur.rfind(' ');
            while (text_width(f, cur + piece) > maxw && !cur.empty()) {
                lines.push_back(cur);
                cur.clear();
                sp = std::string::npos;
            }
            if (piece == " " && cur.empty()) { i += len; continue; }
        }
        if (piece == " ") sp = cur.size();
        cur += piece;
        i += len;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

std::string fmt(const char* f, const char* arg)
{
    int n = snprintf(nullptr, 0, f, arg);
    std::string s(n + 1, '\0');
    snprintf(&s[0], n + 1, f, arg);
    s.pop_back();
    return s;
}

std::string ellipsize(TTF_Font* f, const std::string& s, int maxw)
{
    if (maxw <= 0 || text_width(f, s) <= maxw) return s;
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        size_t len = 1;
        unsigned char ch = (unsigned char)s[i];
        if (ch >= 0xF0) len = 4; else if (ch >= 0xE0) len = 3;
        else if (ch >= 0xC0) len = 2;
        if (i + len > s.size()) break;
        std::string piece = s.substr(i, len);
        if (text_width(f, out + piece + "…") > maxw) break;
        out += piece;
        i += len;
    }
    return out + "…";
}

std::string two(int v)
{
    char b[8];
    snprintf(b, sizeof(b), "%02d", v);
    return b;
}

std::string fmt_duration(long long secs)
{
    if (secs < 0) secs = 0;
    char b[96];
    if (secs >= 3600)
        snprintf(b, sizeof(b), tr(T_DUR_HM_FMT),
                 (int)(secs / 3600), (int)((secs % 3600) / 60));
    else if (secs >= 60)
        snprintf(b, sizeof(b), tr(T_DUR_M_FMT), (int)(secs / 60));
    else
        snprintf(b, sizeof(b), tr(T_DUR_S_FMT), (int)secs);
    return b;
}

std::string fmt_when(long long epoch)
{
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    char b[32];
    snprintf(b, sizeof(b), "%02d-%02d %02d:%02d",
             tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return b;
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
namespace {

constexpr float PI = 3.14159265358979f;

SDL_Color geom_color(SDL_Color a, SDL_Color b, float y, float y0, float y1)
{
    if (y1 <= y0) return a;
    return mix_color(a, b, (y - y0) / (y1 - y0));
}

// Filled convex fan with an optional vertical gradient.
void fan_fill(SDL_Renderer* r, const SDL_FPoint* pts, int n,
              SDL_Color top, SDL_Color bottom, float y0, float y1)
{
    if (n < 3) return;
    std::vector<SDL_Vertex> v;
    v.reserve(n + 1);
    SDL_FPoint c = { 0, 0 };
    for (int i = 0; i < n; i++) { c.x += pts[i].x; c.y += pts[i].y; }
    c.x /= n; c.y /= n;
    v.push_back(SDL_Vertex{ c, geom_color(top, bottom, c.y, y0, y1), { 0, 0 } });
    for (int i = 0; i < n; i++)
        v.push_back(SDL_Vertex{ pts[i], geom_color(top, bottom, pts[i].y, y0, y1), { 0, 0 } });
    std::vector<int> idx;
    idx.reserve(n * 3);
    for (int i = 0; i < n; i++) {
        idx.push_back(0);
        idx.push_back(1 + i);
        idx.push_back(1 + (i + 1) % n);
    }
    perf::note_draw();
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(),
                       idx.data(), (int)idx.size());
}

void rrect_points(const SDL_Rect& rc, int radius, SDL_FPoint* pts)
{
    int rad = std::clamp(radius, 0, std::min(rc.w, rc.h) / 2);
    const int SEG = rad > 24 ? 10 : 6;
    struct Corner { float cx, cy, a0; };
    const Corner corners[4] = {
        { (float)(rc.x + rad),         (float)(rc.y + rad),         PI },
        { (float)(rc.x + rc.w - rad),  (float)(rc.y + rad),         -PI / 2 },
        { (float)(rc.x + rc.w - rad),  (float)(rc.y + rc.h - rad),  0 },
        { (float)(rc.x + rad),         (float)(rc.y + rc.h - rad),  PI / 2 },
    };
    int n = 0;
    for (const auto& c : corners) {
        for (int s = 0; s <= SEG; s++) {
            float a = c.a0 + (PI / 2) * s / SEG;
            pts[n++] = { c.cx + cosf(a) * rad, c.cy + sinf(a) * rad };
        }
    }
}

void ring_geom(SDL_Renderer* r, const SDL_Rect& rc, int radius, int width,
               SDL_Color c)
{
    int rad = std::clamp(radius, 0, std::min(rc.w, rc.h) / 2);
    width = std::max(1, std::min(width, rad));
    const int SEG = rad > 24 ? 10 : 6;
    std::vector<SDL_FPoint> outer, inner;
    struct Corner { float cx, cy, a0; };
    const Corner corners[4] = {
        { (float)(rc.x + rad),        (float)(rc.y + rad),        PI },
        { (float)(rc.x + rc.w - rad), (float)(rc.y + rad),        -PI / 2 },
        { (float)(rc.x + rc.w - rad), (float)(rc.y + rc.h - rad), 0 },
        { (float)(rc.x + rad),        (float)(rc.y + rc.h - rad), PI / 2 },
    };
    for (const auto& cn : corners) {
        for (int s = 0; s <= SEG; s++) {
            float a = cn.a0 + (PI / 2) * s / SEG;
            outer.push_back({ cn.cx + cosf(a) * rad, cn.cy + sinf(a) * rad });
            float ir = (float)(rad - width);
            inner.push_back({ cn.cx + cosf(a) * ir, cn.cy + sinf(a) * ir });
        }
    }
    int n = (int)outer.size();
    std::vector<SDL_Vertex> v;
    v.reserve(n * 2);
    for (int i = 0; i < n; i++) {
        v.push_back(SDL_Vertex{ outer[i], c, { 0, 0 } });
        v.push_back(SDL_Vertex{ inner[i], c, { 0, 0 } });
    }
    std::vector<int> idx;
    idx.reserve(n * 6);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        idx.push_back(i * 2); idx.push_back(i * 2 + 1); idx.push_back(j * 2);
        idx.push_back(j * 2); idx.push_back(i * 2 + 1); idx.push_back(j * 2 + 1);
    }
    perf::note_draw();
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(),
                       idx.data(), (int)idx.size());
}

} // namespace

void rrect(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c)
{
    if (rc.w <= 0 || rc.h <= 0 || c.a == 0) return;
    SDL_FPoint pts[44];
    rrect_points(rc, radius, pts);
    fan_fill(r, pts, 4 * (radius > 24 ? 11 : 7), c, c, 0, 1);
}

void rrect_grad(SDL_Renderer* r, SDL_Rect rc, int radius,
                SDL_Color top, SDL_Color bottom)
{
    if (rc.w <= 0 || rc.h <= 0) return;
    SDL_FPoint pts[44];
    rrect_points(rc, radius, pts);
    fan_fill(r, pts, 4 * (radius > 24 ? 11 : 7), top, bottom,
             (float)rc.y, (float)(rc.y + rc.h));
}

void rrect_stroke(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c,
                  int width)
{
    if (rc.w <= 0 || rc.h <= 0 || c.a == 0) return;
    ring_geom(r, rc, radius, width, c);
}

void rrect_shadow(SDL_Renderer* r, SDL_Rect rc, int radius, int y_off,
                  int blur, Uint8 alpha)
{
    for (int i = blur; i >= 1; i--) {
        SDL_Rect s = { rc.x - i, rc.y + y_off - i, rc.w + i * 2, rc.h + i * 2 };
        rrect(r, s, radius + i, rgba(0, 0, 0, alpha / (i + 1)));
    }
    SDL_Rect s = { rc.x, rc.y + y_off, rc.w, rc.h };
    rrect(r, s, radius, rgba(0, 0, 0, alpha));
}

void circle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c)
{
    if (radius <= 0 || c.a == 0) return;
    const int n = radius > 24 ? 32 : 20;
    std::vector<SDL_FPoint> pts(n);
    for (int i = 0; i < n; i++) {
        float a = 2 * PI * i / n;
        pts[i] = { cx + cosf(a) * radius, cy + sinf(a) * radius };
    }
    fan_fill(r, pts.data(), n, c, c, 0, 1);
}

void ring(SDL_Renderer* r, int cx, int cy, int radius, int width, SDL_Color c)
{
    if (radius <= 0 || width <= 0 || c.a == 0) return;
    int ir = std::max(0, radius - width);
    const int n = radius > 24 ? 32 : 20;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (int i = 0; i < n; i++) {
        float a = 2 * PI * i / n;
        v.push_back(SDL_Vertex{ { cx + cosf(a) * radius, cy + sinf(a) * radius }, c, { 0, 0 } });
        v.push_back(SDL_Vertex{ { cx + cosf(a) * ir, cy + sinf(a) * ir }, c, { 0, 0 } });
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        idx.insert(idx.end(), { i * 2, i * 2 + 1, j * 2, j * 2, i * 2 + 1, j * 2 + 1 });
    }
    perf::note_draw();
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(),
                       idx.data(), (int)idx.size());
}

void glow(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c,
          int layers, int max_alpha)
{
    for (int i = layers; i >= 1; i--) {
        int rad = radius * i / layers;
        Uint8 a = (Uint8)(max_alpha * (layers - i + 1) / (layers * 2));
        circle(r, cx, cy, rad, with_alpha(c, a));
    }
}

void grad_rect(SDL_Renderer* r, SDL_Rect rc, SDL_Color top, SDL_Color bottom)
{
    SDL_Vertex v[4] = {
        { { (float)rc.x, (float)rc.y }, top, { 0, 0 } },
        { { (float)(rc.x + rc.w), (float)rc.y }, top, { 0, 0 } },
        { { (float)(rc.x + rc.w), (float)(rc.y + rc.h) }, bottom, { 0, 0 } },
        { { (float)rc.x, (float)(rc.y + rc.h) }, bottom, { 0, 0 } },
    };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    perf::note_draw();
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

void thick_line(SDL_Renderer* r, float x1, float y1, float x2, float y2,
                float width, SDL_Color c)
{
    if (c.a == 0) return;
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) { circle(r, (int)x1, (int)y1, (int)(width / 2), c); return; }
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;
    SDL_FPoint pts[4] = {
        { x1 + nx, y1 + ny }, { x2 + nx, y2 + ny },
        { x2 - nx, y2 - ny }, { x1 - nx, y1 - ny },
    };
    std::vector<SDL_Vertex> v;
    for (auto& p : pts) v.push_back(SDL_Vertex{ p, c, { 0, 0 } });
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    perf::note_draw();
    SDL_RenderGeometry(r, nullptr, v.data(), 4, idx, 6);
    int cap = std::max(1, (int)(width / 2));
    circle(r, (int)x1, (int)y1, cap, c);
    circle(r, (int)x2, (int)y2, cap, c);
}

void arc(SDL_Renderer* r, float cx, float cy, float radius,
         float a0, float a1, float width, SDL_Color c)
{
    int steps = std::max(4, (int)(fabsf(a1 - a0) * radius / 8));
    SDL_FPoint prev = { cx + cosf(a0) * radius, cy + sinf(a0) * radius };
    for (int i = 1; i <= steps; i++) {
        float a = a0 + (a1 - a0) * i / steps;
        SDL_FPoint p = { cx + cosf(a) * radius, cy + sinf(a) * radius };
        thick_line(r, prev.x, prev.y, p.x, p.y, width, c);
        prev = p;
    }
}

void polygon(SDL_Renderer* r, const SDL_FPoint* pts, int n, SDL_Color c)
{
    fan_fill(r, pts, n, c, c, 0, 1);
}

// ---------------------------------------------------------------------------
// Vector icons
// ---------------------------------------------------------------------------
namespace {
float icon_w(int size) { return std::max(2.0f, size * 0.095f); }
SDL_FPoint P(int x, int y, int size, float px, float py) {
    return { x + size * px, y + size * py };
}
void stroke_path(SDL_Renderer* r, const SDL_FPoint* p, int n, float w, SDL_Color c)
{
    for (int i = 1; i < n; i++)
        thick_line(r, p[i - 1].x, p[i - 1].y, p[i].x, p[i].y, w, c);
}
} // namespace

void icon_chevron(SDL_Renderer* r, int x, int y, int size, SDL_Color c, bool left)
{
    float w = icon_w(size);
    float cx = x + size * (left ? 0.62f : 0.38f);
    SDL_FPoint p[3] = {
        { cx + (left ? size * 0.16f : -size * 0.16f), y + size * 0.22f },
        { cx - (left ? size * 0.16f : -size * 0.16f), y + size * 0.5f },
        { cx + (left ? size * 0.16f : -size * 0.16f), y + size * 0.78f },
    };
    stroke_path(r, p, 3, w, c);
}

void icon_play(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    SDL_FPoint p[3] = {
        { x + size * 0.32f, y + size * 0.2f },
        { x + size * 0.82f, y + size * 0.5f },
        { x + size * 0.32f, y + size * 0.8f },
    };
    polygon(r, p, 3, c);
}

void icon_pause(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    rrect(r, { x + (int)(size * 0.28f), y + (int)(size * 0.22f),
               (int)(size * 0.15f), (int)(size * 0.56f) },
          (int)(size * 0.05f), c);
    rrect(r, { x + (int)(size * 0.57f), y + (int)(size * 0.22f),
               (int)(size * 0.15f), (int)(size * 0.56f) },
          (int)(size * 0.05f), c);
}

void icon_star(SDL_Renderer* r, int x, int y, int size, SDL_Color c, bool filled)
{
    SDL_FPoint pts[10];
    float cx = x + size / 2.0f, cy = y + size / 2.0f;
    float ro = size * 0.46f, ri = ro * 0.44f;
    for (int i = 0; i < 10; i++) {
        float a = -PI / 2 + i * PI / 5;
        float rad = (i % 2 == 0) ? ro : ri;
        pts[i] = { cx + cosf(a) * rad, cy + sinf(a) * rad };
    }
    if (filled) {
        polygon(r, pts, 10, c);
    } else {
        SDL_FPoint closed[11];
        for (int i = 0; i < 10; i++) closed[i] = pts[i];
        closed[10] = pts[0];
        stroke_path(r, closed, 11, icon_w(size) * 0.9f, c);
    }
}

void icon_wifi(SDL_Renderer* r, int x, int y, int size, SDL_Color c, int level)
{
    float w = icon_w(size);
    float cx = x + size * 0.5f;
    float cy = y + size * 0.82f;
    int bars = std::clamp(level, 0, 3);
    float radii[3] = { size * 0.2f, size * 0.36f, size * 0.52f };
    for (int i = 0; i < 3; i++) {
        if (i >= bars) break;
        arc(r, cx, cy, radii[i], -PI * 0.78f, -PI * 0.22f, w, c);
    }
    if (bars > 0) circle(r, (int)cx, (int)cy, (int)(size * 0.055f), c);
}

void icon_bt(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    SDL_FPoint main[4] = {
        P(x, y, size, 0.22f, 0.5f), P(x, y, size, 0.78f, 0.5f),
        P(x, y, size, 0.5f, 0.16f), P(x, y, size, 0.5f, 0.84f),
    };
    thick_line(r, main[0].x, main[0].y, main[1].x, main[1].y, w, c);
    thick_line(r, main[2].x, main[2].y, main[3].x, main[3].y, w, c);
    thick_line(r, main[2].x, main[2].y, main[1].x, main[1].y, w, c);
    thick_line(r, main[1].x, main[1].y, main[3].x, main[3].y, w, c);
    thick_line(r, main[2].x, main[2].y, main[0].x, main[0].y, w, c);
    thick_line(r, main[3].x, main[3].y, main[0].x, main[0].y, w, c);
}

void icon_refresh(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    float cx = x + size / 2.0f, cy = y + size / 2.0f;
    float rad = size * 0.32f;
    arc(r, cx, cy, rad, -PI * 0.35f, PI * 1.25f, w, c);
    SDL_FPoint tip = { cx + cosf(-PI * 0.35f) * rad, cy + sinf(-PI * 0.35f) * rad };
    thick_line(r, tip.x - size * 0.16f, tip.y - size * 0.02f, tip.x, tip.y, w, c);
    thick_line(r, tip.x, tip.y, tip.x - size * 0.02f, tip.y + size * 0.16f, w, c);
}

void icon_gear(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    float cx = x + size / 2.0f, cy = y + size / 2.0f;
    float r1 = size * 0.2f, r2 = size * 0.38f;
    for (int i = 0; i < 8; i++) {
        float a = i * PI / 4;
        thick_line(r, cx + cosf(a) * r1, cy + sinf(a) * r1,
                   cx + cosf(a) * r2, cy + sinf(a) * r2, w, c);
    }
    ring(r, (int)cx, (int)cy, (int)(size * 0.35f), (int)w, c);
    circle(r, (int)cx, (int)cy, (int)(size * 0.07f), c);
}

void icon_power(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    float cx = x + size / 2.0f, cy = y + size / 2.0f;
    arc(r, cx, cy, size * 0.3f, -PI * 0.32f, PI * 1.32f, w, c);
    thick_line(r, cx, y + size * 0.14f, cx, y + size * 0.48f, w, c);
}

void icon_clock(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    float cx = x + size / 2.0f, cy = y + size / 2.0f;
    ring(r, (int)cx, (int)cy, (int)(size * 0.36f), (int)w, c);
    thick_line(r, cx, cy, cx, cy - size * 0.18f, w, c);
    thick_line(r, cx, cy, cx + size * 0.14f, cy + size * 0.1f, w, c);
}

void icon_trash(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    thick_line(r, x + size * 0.2f, y + size * 0.28f, x + size * 0.8f, y + size * 0.28f, w, c);
    thick_line(r, x + size * 0.38f, y + size * 0.2f, x + size * 0.62f, y + size * 0.2f, w, c);
    SDL_FPoint body[5] = {
        { x + size * 0.26f, y + size * 0.3f },
        { x + size * 0.74f, y + size * 0.3f },
        { x + size * 0.68f, y + size * 0.82f },
        { x + size * 0.32f, y + size * 0.82f },
        { x + size * 0.26f, y + size * 0.3f },
    };
    stroke_path(r, body, 5, w, c);
    thick_line(r, x + size * 0.42f, y + size * 0.4f, x + size * 0.42f, y + size * 0.7f, w * 0.8f, c);
    thick_line(r, x + size * 0.58f, y + size * 0.4f, x + size * 0.58f, y + size * 0.7f, w * 0.8f, c);
}

void icon_check(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size) * 1.2f;
    SDL_FPoint p[3] = {
        { x + size * 0.2f, y + size * 0.52f },
        { x + size * 0.42f, y + size * 0.72f },
        { x + size * 0.82f, y + size * 0.28f },
    };
    stroke_path(r, p, 3, w, c);
}

void icon_lock(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    rrect_stroke(r, { x + (int)(size * 0.24f), y + (int)(size * 0.44f),
                      (int)(size * 0.52f), (int)(size * 0.4f) },
                 (int)(size * 0.1f), c, (int)w);
    arc(r, x + size * 0.5f, y + size * 0.44f, size * 0.18f, PI, 2 * PI, w, c);
    thick_line(r, x + size * 0.32f, y + size * 0.44f, x + size * 0.32f, y + size * 0.34f, w, c);
    thick_line(r, x + size * 0.68f, y + size * 0.44f, x + size * 0.68f, y + size * 0.34f, w, c);
    circle(r, (int)(x + size * 0.5f), (int)(y + size * 0.64f), (int)(size * 0.05f), c);
}

void icon_arrow_down(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    thick_line(r, x + size * 0.5f, y + size * 0.2f, x + size * 0.5f, y + size * 0.64f, w, c);
    thick_line(r, x + size * 0.5f, y + size * 0.64f, x + size * 0.3f, y + size * 0.44f, w, c);
    thick_line(r, x + size * 0.5f, y + size * 0.64f, x + size * 0.7f, y + size * 0.44f, w, c);
    thick_line(r, x + size * 0.25f, y + size * 0.78f, x + size * 0.75f, y + size * 0.78f, w, c);
}

void icon_arrow_up(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    thick_line(r, x + size * 0.5f, y + size * 0.8f, x + size * 0.5f, y + size * 0.36f, w, c);
    thick_line(r, x + size * 0.5f, y + size * 0.36f, x + size * 0.3f, y + size * 0.56f, w, c);
    thick_line(r, x + size * 0.5f, y + size * 0.36f, x + size * 0.7f, y + size * 0.56f, w, c);
    thick_line(r, x + size * 0.25f, y + size * 0.22f, x + size * 0.75f, y + size * 0.22f, w, c);
}

void icon_speaker(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    SDL_FPoint body[6] = {
        { x + size * 0.16f, y + size * 0.38f },
        { x + size * 0.36f, y + size * 0.38f },
        { x + size * 0.56f, y + size * 0.22f },
        { x + size * 0.56f, y + size * 0.78f },
        { x + size * 0.36f, y + size * 0.62f },
        { x + size * 0.16f, y + size * 0.62f },
    };
    polygon(r, body, 6, c);
    arc(r, x + size * 0.58f, y + size * 0.5f, size * 0.18f, -PI / 3, PI / 3, w * 0.9f, c);
    arc(r, x + size * 0.58f, y + size * 0.5f, size * 0.3f, -PI / 3, PI / 3, w * 0.8f, c);
}

void icon_sun(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    float cx = x + size / 2.0f, cy = y + size / 2.0f;
    circle(r, (int)cx, (int)cy, (int)(size * 0.2f), c);
    for (int i = 0; i < 8; i++) {
        float a = i * PI / 4;
        thick_line(r, cx + cosf(a) * size * 0.3f, cy + sinf(a) * size * 0.3f,
                   cx + cosf(a) * size * 0.42f, cy + sinf(a) * size * 0.42f, w * 0.8f, c);
    }
}

void icon_home(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    SDL_FPoint roof[4] = {
        { x + size * 0.14f, y + size * 0.46f },
        { x + size * 0.5f, y + size * 0.16f },
        { x + size * 0.86f, y + size * 0.46f },
        { x + size * 0.14f, y + size * 0.46f },
    };
    stroke_path(r, roof, 4, w, c);
    SDL_FPoint body[5] = {
        { x + size * 0.22f, y + size * 0.44f },
        { x + size * 0.22f, y + size * 0.84f },
        { x + size * 0.78f, y + size * 0.84f },
        { x + size * 0.78f, y + size * 0.44f },
        { x + size * 0.78f, y + size * 0.44f },
    };
    stroke_path(r, body, 4, w, c);
    rrect(r, { x + (int)(size * 0.44f), y + (int)(size * 0.6f),
               (int)(size * 0.12f), (int)(size * 0.24f) },
          (int)(size * 0.04f), c);
}

void icon_globe(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    float cx = x + size / 2.0f, cy = y + size / 2.0f;
    ring(r, (int)cx, (int)cy, (int)(size * 0.36f), (int)w, c);
    thick_line(r, x + size * 0.16f, cy, x + size * 0.84f, cy, w * 0.8f, c);
    thick_line(r, cx, y + size * 0.14f, cx, y + size * 0.86f, w * 0.8f, c);
    SDL_FPoint ell_l[5], ell_r[5];
    for (int i = 0; i < 5; i++) {
        float t = -1.0f + i * 0.5f;
        float ox = cosf(t * PI / 2 + PI / 2) * size * 0.24f;
        ell_l[i] = { cx - size * 0.02f - ox * 0.6f, cy + t * size * 0.28f };
        ell_r[i] = { cx + size * 0.02f + ox * 0.6f, cy + t * size * 0.28f };
    }
    stroke_path(r, ell_l, 5, w * 0.6f, c);
    stroke_path(r, ell_r, 5, w * 0.6f, c);
}

void icon_gamepad(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    rrect_stroke(r, { x + (int)(size * 0.12f), y + (int)(size * 0.32f),
                      (int)(size * 0.76f), (int)(size * 0.36f) },
                 (int)(size * 0.18f), c, (int)w);
    thick_line(r, x + size * 0.28f, y + size * 0.5f, x + size * 0.4f, y + size * 0.5f, w * 0.9f, c);
    thick_line(r, x + size * 0.34f, y + size * 0.44f, x + size * 0.34f, y + size * 0.56f, w * 0.9f, c);
    circle(r, (int)(x + size * 0.66f), (int)(y + size * 0.46f), (int)(size * 0.035f), c);
    circle(r, (int)(x + size * 0.74f), (int)(y + size * 0.54f), (int)(size * 0.035f), c);
}

void icon_image(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    rrect_stroke(r, { x + (int)(size * 0.14f), y + (int)(size * 0.2f),
                      (int)(size * 0.72f), (int)(size * 0.6f) },
                 (int)(size * 0.1f), c, (int)w);
    circle(r, (int)(x + size * 0.34f), (int)(y + size * 0.38f), (int)(size * 0.055f), c);
    SDL_FPoint mount[4] = {
        { x + size * 0.2f, y + size * 0.72f },
        { x + size * 0.44f, y + size * 0.5f },
        { x + size * 0.6f, y + size * 0.64f },
        { x + size * 0.8f, y + size * 0.46f },
    };
    stroke_path(r, mount, 4, w * 0.8f, c);
}

void icon_trophy(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    SDL_FPoint cup[5] = {
        { x + size * 0.3f, y + size * 0.18f },
        { x + size * 0.7f, y + size * 0.18f },
        { x + size * 0.62f, y + size * 0.52f },
        { x + size * 0.38f, y + size * 0.52f },
        { x + size * 0.3f, y + size * 0.18f },
    };
    stroke_path(r, cup, 5, w, c);
    arc(r, x + size * 0.3f, y + size * 0.3f, size * 0.12f, PI * 0.5f, PI * 1.5f, w * 0.8f, c);
    arc(r, x + size * 0.7f, y + size * 0.3f, size * 0.12f, -PI * 0.5f, PI * 0.5f, w * 0.8f, c);
    thick_line(r, x + size * 0.5f, y + size * 0.52f, x + size * 0.5f, y + size * 0.7f, w, c);
    thick_line(r, x + size * 0.34f, y + size * 0.82f, x + size * 0.66f, y + size * 0.82f, w, c);
    thick_line(r, x + size * 0.4f, y + size * 0.7f, x + size * 0.6f, y + size * 0.7f, w, c);
}

void icon_download(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    thick_line(r, x + size * 0.5f, y + size * 0.16f, x + size * 0.5f, y + size * 0.58f, w, c);
    thick_line(r, x + size * 0.5f, y + size * 0.58f, x + size * 0.3f, y + size * 0.38f, w, c);
    thick_line(r, x + size * 0.5f, y + size * 0.58f, x + size * 0.7f, y + size * 0.38f, w, c);
    SDL_FPoint tray[4] = {
        { x + size * 0.2f, y + size * 0.7f },
        { x + size * 0.2f, y + size * 0.82f },
        { x + size * 0.8f, y + size * 0.82f },
        { x + size * 0.8f, y + size * 0.7f },
    };
    stroke_path(r, tray, 4, w, c);
}

void icon_keyboard(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    rrect_stroke(r, { x + (int)(size * 0.1f), y + (int)(size * 0.3f),
                      (int)(size * 0.8f), (int)(size * 0.4f) },
                 (int)(size * 0.08f), c, (int)w);
    for (int i = 0; i < 4; i++)
        circle(r, (int)(x + size * (0.26f + i * 0.16f)), (int)(y + size * 0.44f),
               (int)(size * 0.035f), c);
    thick_line(r, x + size * 0.28f, y + size * 0.58f, x + size * 0.72f, y + size * 0.58f, w * 0.8f, c);
}

void icon_backspace(SDL_Renderer* r, int x, int y, int size, SDL_Color c)
{
    float w = icon_w(size);
    SDL_FPoint body[5] = {
        { x + size * 0.12f, y + size * 0.5f },
        { x + size * 0.3f, y + size * 0.24f },
        { x + size * 0.86f, y + size * 0.24f },
        { x + size * 0.86f, y + size * 0.76f },
        { x + size * 0.3f, y + size * 0.76f },
    };
    stroke_path(r, body, 5, w, c);
    thick_line(r, x + size * 0.3f, y + size * 0.76f, x + size * 0.12f, y + size * 0.5f, w, c);
    thick_line(r, x + size * 0.42f, y + size * 0.42f, x + size * 0.64f, y + size * 0.58f, w * 0.9f, c);
    thick_line(r, x + size * 0.64f, y + size * 0.42f, x + size * 0.42f, y + size * 0.58f, w * 0.9f, c);
}

void battery_icon(SDL_Renderer* r, int x, int y, int w, int h, int pct, bool charging)
{
    SDL_Color col = pct > 40 ? C_GREEN : (pct > 15 ? C_AMBER : C_RED);
    if (pct < 0) col = C_DIM;
    rrect_stroke(r, { x, y, w, h }, h / 3, C_DIM, 2);
    rrect(r, { x + w + 2, y + h / 3, 3, h / 3 }, 1, C_DIM);
    if (pct >= 0) {
        int fw = (w - 6) * std::clamp(pct, 0, 100) / 100;
        if (fw > 2) rrect(r, { x + 3, y + 3, fw, h - 6 }, (h - 6) / 3, col);
    }
    if (charging) {
        SDL_FPoint bolt[4] = {
            { x + w * 0.55f, y + h * 0.12f },
            { x + w * 0.36f, y + h * 0.56f },
            { x + w * 0.52f, y + h * 0.56f },
            { x + w * 0.4f, y + h * 0.9f },
        };
        stroke_path(r, bolt, 4, std::max(1.5f, h * 0.09f), col);
    }
}

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------
void card(SDL_Renderer* r, SDL_Rect rc, int radius, bool focus, Uint8 alpha)
{
    if (focus) {
        SDL_Rect o = { rc.x - 2, rc.y - 2, rc.w + 4, rc.h + 4 };
        rrect(r, o, radius + 2, with_alpha(C_ACCENT, 26));
    }
    rrect_shadow(r, rc, radius, 5, 2, dark_theme() ? 90 : 40);
    SDL_Color fill = C_PANEL;
    fill.a = alpha;
    rrect(r, rc, radius, fill);
    rrect_stroke(r, rc, radius, focus ? with_alpha(C_ACCENT, 210) : hairline(),
                 focus ? 2 : 1);
    if (focus) {
        // soft accent wash at the top edge so the card glows without noise
        rrect_grad(r, { rc.x + 1, rc.y + 1, rc.w - 2, rc.h / 2 }, radius,
                   with_alpha(C_ACCENT, 18), with_alpha(C_ACCENT, 0));
    }
}

void button(SDL_Renderer* r, SDL_Rect rc, const std::string& label,
            bool primary, bool focus, bool pressed)
{
    SDL_Rect b = rc;
    if (pressed) { b.x += 2; b.y += 2; b.w -= 4; b.h -= 4; }
    int rad = std::min(18, b.h / 3);
    if (primary) {
        SDL_Color top = lighten(C_ACCENT, 0.12f);
        SDL_Color bot = C_ACCENT;
        if (pressed) { top = mix_color(top, C_BG, 0.08f); bot = mix_color(bot, C_BG, 0.08f); }
        rrect_shadow(r, b, rad, 4, 2, dark_theme() ? 90 : 30);
        rrect_grad(r, b, rad, top, bot);
        text(r, g_ui.body, label, b.x + b.w / 2, b.y + (b.h - 38) / 2,
             on_accent(), Align::Center);
    } else {
        rrect_shadow(r, b, rad, 3, 1, dark_theme() ? 60 : 24);
        SDL_Color fill = C_PANEL;
        fill.a = pressed ? 255 : 235;
        rrect(r, b, rad, fill);
        rrect_stroke(r, b, rad, focus ? with_alpha(C_ACCENT, 200) : hairline(),
                     focus ? 2 : 1);
        text(r, g_ui.body, label, b.x + b.w / 2, b.y + (b.h - 38) / 2,
             focus ? C_ACCENT : C_TEXT, Align::Center);
    }
}

void pill(SDL_Renderer* r, int x, int y, const std::string& label,
          SDL_Color bg, SDL_Color fg, int h, int pad)
{
    int w = text_width(g_ui.small, label) + pad * 2;
    rrect(r, { x, y, w, h }, h / 2, bg);
    text(r, g_ui.small, label, x + w / 2, y + (h - 30) / 2, fg, Align::Center);
}

int pill_width(TTF_Font* f, const std::string& label, int pad)
{
    return text_width(f, label) + pad * 2;
}

void progress_bar(SDL_Renderer* r, SDL_Rect rc, float p,
                  SDL_Color fill, SDL_Color track)
{
    p = std::clamp(p, 0.0f, 1.0f);
    int rad = std::max(1, rc.h / 2);
    rrect(r, rc, rad, track);
    int w = (int)(rc.w * p);
    if (w > 2) {
        rrect(r, { rc.x, rc.y, w, rc.h }, rad, fill);
    }
}

void slider(SDL_Renderer* r, SDL_Rect track, float p, bool active, SDL_Color fill)
{
    p = std::clamp(p, 0.0f, 1.0f);
    int h = 8;
    SDL_Rect tr = { track.x, track.y + (track.h - h) / 2, track.w, h };
    rrect(r, tr, h / 2, C_TRACK);
    int w = (int)(track.w * p);
    if (w > 2) rrect(r, { tr.x, tr.y, w, h }, h / 2, fill);
    int knob = active ? 30 : 26;
    int cx = track.x + (int)(track.w * p);
    int cy = track.y + track.h / 2;
    if (active) glow(r, cx, cy, knob + 10, C_ACCENT, 8, 70);
    circle(r, cx, cy, knob / 2, lighten(C_TEXT, 0.2f));
    circle(r, cx, cy, knob / 2 - 4, fill);
}

void toggle(SDL_Renderer* r, SDL_Rect rc, float value, SDL_Color accent)
{
    value = std::clamp(value, 0.0f, 1.0f);
    int rad = rc.h / 2;
    rrect(r, rc, rad, mix_color(C_TRACK, accent, value));
    int knob = rc.h - 8;
    int x = rc.x + 4 + (int)((rc.w - knob - 8) * value);
    circle(r, x + knob / 2, rc.y + rc.h / 2, knob / 2, rgba(255, 255, 255, 245));
}

void focus_ring(SDL_Renderer* r, SDL_Rect rc, int radius, Uint8 alpha)
{
    rrect_stroke(r, rc, radius, with_alpha(C_ACCENT, alpha), 2);
    SDL_Rect o = { rc.x - 4, rc.y - 4, rc.w + 8, rc.h + 8 };
    rrect_stroke(r, o, radius + 4, with_alpha(C_ACCENT, alpha / 5), 4);
}

void icon_badge(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color bg)
{
    glow(r, cx, cy, radius + 16, bg, 8, 50);
    circle(r, cx, cy, radius, bg);
}

// ---------------------------------------------------------------------------
// Motion / timing
// ---------------------------------------------------------------------------
float ease_in_out(float p)
{
    p = std::clamp(p, 0.0f, 1.0f);
    return p < 0.5f ? 4 * p * p * p : 1 - powf(-2 * p + 2, 3) / 2;
}
float ease_out(float p)
{
    p = std::clamp(p, 0.0f, 1.0f);
    float q = 1.0f - p;
    return 1.0f - q * q * q;
}
float ease_out_back(float p)
{
    p = std::clamp(p, 0.0f, 1.0f);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * powf(p - 1, 3) + c1 * powf(p - 1, 2);
}
float spring_step(float cur, float& vel, float target, float dt,
                  float stiffness, float damping)
{
    float f = (target - cur) * stiffness - vel * damping;
    vel += f * dt;
    cur += vel * dt;
    if (fabsf(target - cur) < 0.002f && fabsf(vel) < 0.02f) {
        cur = target; vel = 0;
    }
    return cur;
}
float now_sec()
{
    return SDL_GetTicks64() / 1000.0f;
}

// ---------------------------------------------------------------------------
// Touch hit registry
// ---------------------------------------------------------------------------
static std::vector<UiHit> g_hits;

void ui_hit_begin() { g_hits.clear(); }
void ui_hit_add(SDL_Rect rc, int id, int a, int b)
{
    if (rc.w <= 0 || rc.h <= 0) return;
    g_hits.push_back(UiHit{ rc, id, a, b });
}
const UiHit* ui_hit_at(int x, int y)
{
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it) {
        SDL_Point p = { x, y };
        if (SDL_PointInRect(&p, &it->rect)) return &*it;
    }
    return nullptr;
}

} // namespace gameos
