// gameOS boot splash — the first thing the panels show.
//
// Started by /etc/init.d/S00splash before every other service, so the boot
// never exposes console text: the kernel console lives on the serial port
// only (see the APPEND line in post-image.sh), and this daemon paints an
// animated brand screen on both DSI panels until the front-end is ready.
//
// The two panels are deliberately different, like a piece of hardware:
//   top    — brand hero: wordmark, orbital rings, light sweep, hairline bar
//   bottom — boot monitor: handheld silhouette, stage checklist, waveform
// It is the same system telling the story from two sides instead of one logo
// duplicated at two sizes.
//
// The init scripts publish milestones with gameos-bootstage; the monitor
// turns them into a localised stage line, checklist and progress. The
// language is picked up from /userdata/system/gameos.ini as soon as the data
// partition is mounted, and falls back to Simplified Chinese before that.
//
// Handoff: gameos-ui writes /run/gameos/splash.stop just before SDL_Init;
// we cross-fade into the UI's own background over ~250 ms, release DRM
// master and exit, so the front-end's first frame lands on identical
// pixels — no flash, no black gap.
//
// Preview: GAMEOS_SPLASH_PREVIEW=1 keeps the process in the foreground and
// GAMEOS_SPLASH_SHOT=<dir> GAMEOS_SPLASH_AT=<seconds> saves both panels as
// PNG at that moment (used by the host build; harmless on the device).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "displays.hpp"
#include "i18n.hpp"

namespace {

constexpr int W = 1024;
constexpr int H = 768;
constexpr float PI = 3.14159265f;

// Mutable: the saved theme is read once /userdata mounts and both panels
// switch to the UI's palette.
SDL_Color C_BG     = {  11,  16,  32, 255 };
SDL_Color C_ACCENT = {  53, 208, 186, 255 };
SDL_Color C_SHEEN  = { 214, 255, 247, 255 };
SDL_Color C_TEXT   = { 226, 236, 245, 255 };
SDL_Color C_DIM    = { 140, 158, 178, 255 };
SDL_Color C_TRACK  = {  28,  38,  58, 255 };
int g_theme = 0;
bool g_theme_dirty = false;

void apply_splash_theme(int idx, const std::string& assets)
{
    gameos::ThemePal p = gameos::theme_palette(idx);
    gameos::load_theme_palette(assets + "/themes/" + gameos::theme_key(idx) +
                               "/palette.conf", &p);
    C_BG     = { p.bg.r,     p.bg.g,     p.bg.b,     255 };
    C_ACCENT = { p.accent.r, p.accent.g, p.accent.b, 255 };
    C_SHEEN  = { p.sheen.r,  p.sheen.g,  p.sheen.b,  255 };
    C_TEXT   = { p.text.r,   p.text.g,   p.text.b,   255 };
    C_DIM    = { p.dim.r,    p.dim.g,    p.dim.b,    255 };
    C_TRACK  = { p.track.r,  p.track.g,  p.track.b,  255 };
}

const char* RUN_DIR   = "/run/gameos";
const char* PID_FILE  = "/run/gameos/splash.pid";
const char* STOP_FILE = "/run/gameos/splash.stop";
const char* STAGE_FILE = "/run/gameos/boot-stage";
const char* SETTINGS_FILE = "/userdata/system/gameos.ini";

// --- small helpers ---------------------------------------------------------
float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
float ease_out(float t) { t = clampf(t, 0.f, 1.f); return 1.f - (1.f - t) * (1.f - t); }
float ease_io(float t)
{
    t = clampf(t, 0.f, 1.f);
    return t < 0.5f ? 2.f * t * t : 1.f - 2.f * (1.f - t) * (1.f - t);
}
SDL_Color mixc(SDL_Color a, SDL_Color b, float t)
{
    t = clampf(t, 0.f, 1.f);
    return { (Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t),
             (Uint8)(a.b + (b.b - a.b) * t), 255 };
}
SDL_Color dark(SDL_Color c, float t) { return mixc(c, { 0, 0, 0, 255 }, t); }
SDL_Color with_alpha(SDL_Color c, int a)
{
    c.a = (Uint8)(a < 0 ? 0 : (a > 255 ? 255 : a));
    return c;
}

std::string env_dir(const char* var, const char* fallback)
{
    const char* v = getenv(var);
    return (v && *v) ? v : fallback;
}

void cleanup_files()
{
    unlink(PID_FILE);
    unlink(STOP_FILE);
}

// Double-fork so rcS continues immediately; stdio goes to a log on tmpfs.
void daemonize()
{
    pid_t p = fork();
    if (p < 0) _exit(1);
    if (p > 0) _exit(0);
    setsid();
    p = fork();
    if (p < 0) _exit(1);
    if (p > 0) _exit(0);

    mkdir(RUN_DIR, 0755);
    int fd = open("/run/gameos/splash.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
    int n = open("/dev/null", O_RDONLY);
    if (n >= 0) {
        dup2(n, STDIN_FILENO);
        if (n > 2) close(n);
    }
    char buf[24];
    snprintf(buf, sizeof buf, "%d", (int)getpid());
    fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        ssize_t w = write(fd, buf, strlen(buf));
        (void)w;
        close(fd);
    }
    unlink(STOP_FILE);   // stale flag from a crashed previous run
}

// --- geometry --------------------------------------------------------------
void fill(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c)
{
    if (c.a == 0 || w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(r, &rc);
}

void vgrad(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color top, SDL_Color bot)
{
    if (w <= 0 || h <= 0 || x > W || y > H) return;
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    SDL_Vertex v[4] = {
        { { (float)x, (float)y }, top, { 0, 0 } },
        { { (float)(x + w), (float)y }, top, { 0, 0 } },
        { { (float)(x + w), (float)(y + h) }, bot, { 0, 0 } },
        { { (float)x, (float)(y + h) }, bot, { 0, 0 } },
    };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

void line(SDL_Renderer* r, float x1, float y1, float x2, float y2, float w, SDL_Color c)
{
    if (c.a == 0) return;
    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    SDL_Vertex v[4] = {
        { { x1 + nx, y1 + ny }, c, { 0, 0 } },
        { { x2 + nx, y2 + ny }, c, { 0, 0 } },
        { { x2 - nx, y2 - ny }, c, { 0, 0 } },
        { { x1 - nx, y1 - ny }, c, { 0, 0 } },
    };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

void disc(SDL_Renderer* r, float cx, float cy, float rad, SDL_Color c, int seg = 72)
{
    if (rad <= 0.2f || c.a == 0) return;
    std::vector<SDL_Vertex> v;
    v.reserve(seg + 2);
    v.push_back({ { cx, cy }, c, { 0, 0 } });
    for (int i = 0; i <= seg; i++) {
        float a = 2.f * PI * i / seg;
        v.push_back({ { cx + std::cos(a) * rad, cy + std::sin(a) * rad }, c, { 0, 0 } });
    }
    std::vector<int> idx;
    idx.reserve(seg * 3);
    for (int i = 0; i < seg; i++) {
        idx.push_back(0);
        idx.push_back(1 + i);
        idx.push_back(2 + i);
    }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void ring(SDL_Renderer* r, float cx, float cy, float rad, float width, SDL_Color c,
          int seg = 96)
{
    float r0 = rad - width * 0.5f, r1 = rad + width * 0.5f;
    if (r0 < 0.f || c.a == 0) return;
    std::vector<SDL_Vertex> v;
    v.reserve((seg + 1) * 2);
    for (int i = 0; i <= seg; i++) {
        float a = 2.f * PI * i / seg;
        float ca = std::cos(a), sa = std::sin(a);
        v.push_back({ { cx + ca * r0, cy + sa * r0 }, c, { 0, 0 } });
        v.push_back({ { cx + ca * r1, cy + sa * r1 }, c, { 0, 0 } });
    }
    std::vector<int> idx;
    idx.reserve(seg * 6);
    for (int i = 0; i < seg; i++) {
        int b = i * 2;
        idx.push_back(b);     idx.push_back(b + 1); idx.push_back(b + 3);
        idx.push_back(b);     idx.push_back(b + 3); idx.push_back(b + 2);
    }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

struct EllipsePt { float x, y; };

EllipsePt ellipse_pt(float cx, float cy, float rx, float ry, float rot, float ang)
{
    float lx = std::cos(ang) * rx, ly = std::sin(ang) * ry;
    float cr = std::cos(rot), sr = std::sin(rot);
    return { cx + lx * cr - ly * sr, cy + lx * sr + ly * cr };
}

// Elliptical orbit ring with a rotation, drawn as a quad strip.
void ering(SDL_Renderer* r, float cx, float cy, float rx, float ry, float rot,
           float width, SDL_Color c, int seg = 160)
{
    std::vector<SDL_Vertex> v;
    v.reserve((seg + 1) * 2);
    for (int i = 0; i <= seg; i++) {
        float a = 2.f * PI * i / seg;
        float lx = std::cos(a) * rx, ly = std::sin(a) * ry;
        float cr = std::cos(rot), sr = std::sin(rot);
        float px = lx * cr - ly * sr, py = lx * sr + ly * cr;
        v.push_back({ { cx + px * (1.f - width / 2.f / rx),
                        cy + py * (1.f - width / 2.f / ry) }, c, { 0, 0 } });
        v.push_back({ { cx + px * (1.f + width / 2.f / rx),
                        cy + py * (1.f + width / 2.f / ry) }, c, { 0, 0 } });
    }
    std::vector<int> idx;
    idx.reserve(seg * 6);
    for (int i = 0; i < seg; i++) {
        int b = i * 2;
        idx.push_back(b);     idx.push_back(b + 1); idx.push_back(b + 3);
        idx.push_back(b);     idx.push_back(b + 3); idx.push_back(b + 2);
    }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

// Outline of a rounded rectangle (seg points per corner), used by both the
// fill fan and the stroked variant.
void rrect_outline(float x, float y, float w, float h, float rad, float* px, float* py,
                   int per = 10)
{
    struct Corner { float cx, cy, a0; };
    const Corner cs[4] = {
        { x + w - rad, y + rad, -PI / 2.f },
        { x + w - rad, y + h - rad, 0.f },
        { x + rad, y + h - rad, PI / 2.f },
        { x + rad, y + rad, PI },
    };
    int k = 0;
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i <= per; i++) {
            float a = cs[c].a0 + (PI / 2.f) * i / per;
            px[k] = cs[c].cx + std::cos(a) * rad;
            py[k] = cs[c].cy + std::sin(a) * rad;
            k++;
        }
    }
}

void rrect(SDL_Renderer* r, float x, float y, float w, float h, float rad, SDL_Color c)
{
    if (c.a == 0 || w <= 0 || h <= 0) return;
    rad = std::min(rad, std::min(w, h) * 0.5f);
    const int per = 10, n = (per + 1) * 4;
    float px[64], py[64];
    rrect_outline(x, y, w, h, rad, px, py, per);
    std::vector<SDL_Vertex> v;
    v.reserve(n + 1);
    v.push_back({ { x + w * 0.5f, y + h * 0.5f }, c, { 0, 0 } });
    for (int i = 0; i < n; i++) v.push_back({ { px[i], py[i] }, c, { 0, 0 } });
    std::vector<int> idx;
    idx.reserve(n * 3);
    for (int i = 0; i < n; i++) {
        idx.push_back(0);
        idx.push_back(1 + i);
        idx.push_back(1 + (i + 1) % n);
    }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void rrect_line(SDL_Renderer* r, float x, float y, float w, float h, float rad,
                float width, SDL_Color c, int per = 8)
{
    const int n = (per + 1) * 4;
    float px[64], py[64];
    rrect_outline(x, y, w, h, rad, px, py, per);
    for (int i = 0; i < n; i++)
        line(r, px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], width, c);
}

SDL_Texture* make_radial(SDL_Renderer* ren, int size, float power, Uint8 peak,
                         SDL_Color color)
{
    SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC, size, size);
    if (!t) return nullptr;
    std::vector<Uint32> px(size * size);
    float c = (size - 1) / 2.f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c)) / c;
            float a = d >= 1.f ? 0.f : std::pow(1.f - d, power);
            Uint8 al = (Uint8)(a * peak);
            px[y * size + x] = (Uint32(al) << 24) | (Uint32(color.r) << 16)
                             | (Uint32(color.g) << 8) | Uint32(color.b);
        }
    }
    SDL_UpdateTexture(t, nullptr, px.data(), size * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    return t;
}

void blit(SDL_Renderer* r, SDL_Texture* t, float x, float y, float w, float h, Uint8 a)
{
    if (!t || a == 0) return;
    SDL_SetTextureAlphaMod(t, a);
    SDL_Rect d = { (int)x, (int)y, (int)w, (int)h };
    SDL_RenderCopy(r, t, nullptr, &d);
}

// Cached single-line text. Rebuilds only when the string or colour changes,
// so the per-frame cost on the device is one blit per label.
struct Txt {
    SDL_Texture* t = nullptr;
    std::string s;
    SDL_Color c = { 0, 0, 0, 0 };
    int w = 0, h = 0;

    void set(SDL_Renderer* r, TTF_Font* f, const std::string& str, SDL_Color col)
    {
        if (t && s == str && c.r == col.r && c.g == col.g && c.b == col.b) return;
        clear();
        s = str;
        c = col;
        if (!f || str.empty()) return;
        SDL_Surface* su = TTF_RenderUTF8_Blended(f, str.c_str(), col);
        if (!su) return;
        w = su->w;
        h = su->h;
        t = SDL_CreateTextureFromSurface(r, su);
        SDL_FreeSurface(su);
    }
    void draw(SDL_Renderer* r, int x, int y, int a = 255, bool center = false) const
    {
        if (!t) return;
        SDL_SetTextureAlphaMod(t, (Uint8)std::max(0, std::min(255, a)));
        SDL_Rect d = { center ? x - w / 2 : x, y, w, h };
        SDL_RenderCopy(r, t, nullptr, &d);
    }
    void clear()
    {
        if (t) { SDL_DestroyTexture(t); t = nullptr; }
        s.clear();
        w = h = 0;
    }
};

// --- boot progress ---------------------------------------------------------
gameos::TKey stage_key(const std::string& s)
{
    if (s == "storage") return gameos::T_BOOT_STORAGE;
    if (s == "data")    return gameos::T_BOOT_DATA;
    if (s == "network") return gameos::T_BOOT_NETWORK;
    if (s == "audio")   return gameos::T_BOOT_AUDIO;
    if (s == "input")   return gameos::T_BOOT_INPUT;
    if (s == "ready")   return gameos::T_BOOT_READY;
    return gameos::T_BOOT_DISPLAY;
}

struct Boot {
    float pct = 0.f;                          // smoothed, drives the bars
    int   target = 0;                         // last published percent
    gameos::TKey stage = gameos::T_BOOT_DISPLAY;
};

void poll_boot(Boot& b)
{
    static Uint32 next = 0;
    static bool lang_done = false;
    Uint32 now = SDL_GetTicks();
    if (now < next) return;
    next = now + 160;

    std::ifstream f(STAGE_FILE);
    std::string line;
    while (std::getline(f, line)) {
        if (line.compare(0, 4, "pct=") == 0) {
            int v = atoi(line.c_str() + 4);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            b.target = v;
        } else if (line.compare(0, 6, "stage=") == 0) {
            b.stage = stage_key(line.substr(6));
        }
    }

    // /userdata only exists from S20gameos onwards; until then keep zh-CN.
    if (!lang_done) {
        std::ifstream s(SETTINGS_FILE);
        while (std::getline(s, line)) {
            if (line.compare(0, 9, "language=") == 0) {
                int v = atoi(line.c_str() + 9);
                if (v >= 0 && v < gameos::LANG_COUNT) {
                    gameos::g_lang = (gameos::Lang)v;
                    lang_done = true;
                }
            } else if (line.compare(0, 6, "theme=") == 0) {
                int v = atoi(line.c_str() + 6);
                if (v >= 0 && v <= 3 && v != g_theme) {
                    g_theme = v;
                    g_theme_dirty = true;
                }
            }
        }
    }
}

// --- panels ----------------------------------------------------------------
struct Panel {
    SDL_Window*   win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture*  bg = nullptr;          // UI background, for the cross-fade
    SDL_Texture*  word = nullptr;        // "gameOS" in the brand colour
    SDL_Texture*  sheen = nullptr;       // same text, bright: the light sweep
    int ww = 0, wh = 0;
    SDL_Texture*  glow_a = nullptr;      // accent halo
    SDL_Texture*  glow_s = nullptr;      // sheen halo
    SDL_Texture*  dot_a = nullptr;       // soft satellite
    SDL_Texture*  dot_w = nullptr;       // tiny hard star

    TTF_Font* f_word = nullptr;          // Orbitron 148
    TTF_Font* f_num = nullptr;           // Orbitron 24 / 15
    TTF_Font* f_num_s = nullptr;         // Orbitron 13 / 16
    TTF_Font* f_big = nullptr;           // Orbitron 88 (monitor percent)
    TTF_Font* f_noto = nullptr;          // Noto 24-26 (labels)
    TTF_Font* f_noto_s = nullptr;        // Noto 15-22

    Txt over, tag, stage_t, pct_t, corner_l, corner_r;
    Txt head, sub, pct_big, curr, micro;
    Txt chip_lab, chip_val, arrow_t, foot;
    Txt bus_lab, bus_val;
    Txt row[6], row_st[6];
    std::string fontdir;
    bool top = true;
    int cache_lang = -1;
};

// The six milestones the monitor lists, in the order init publishes them.
const gameos::TKey STAGE_ROWS[6] = {
    gameos::T_BOOT_STORAGE, gameos::T_BOOT_DATA, gameos::T_BOOT_NETWORK,
    gameos::T_BOOT_AUDIO,   gameos::T_BOOT_INPUT, gameos::T_BOOT_READY,
};
// Completion threshold per row, from gameos-bootstage calls in init.d.
const int STAGE_DONE_AT[6] = { 20, 60, 75, 80, 92, 98 };

void open_fonts(Panel& p)
{
    const std::string dir = p.fontdir;
    auto orb = [&](int px) {
        TTF_Font* f = TTF_OpenFont((dir + "/Orbitron.ttf").c_str(), px);
        if (!f) f = TTF_OpenFont((dir + "/NotoSansSC-Regular.otf").c_str(), px);
        return f;
    };
    auto noto = [&](int px) {
        return TTF_OpenFont((dir + "/NotoSansSC-Regular.otf").c_str(), px);
    };
    p.f_word   = p.top ? orb(148) : nullptr;
    p.f_num    = orb(p.top ? 24 : 22);
    p.f_num_s  = orb(p.top ? 16 : 13);
    p.f_big    = p.top ? nullptr : orb(88);
    p.f_noto   = noto(p.top ? 26 : 24);
    p.f_noto_s = noto(p.top ? 22 : 15);
}

void close_fonts(Panel& p)
{
    for (TTF_Font* f : { p.f_word, p.f_num, p.f_num_s, p.f_big, p.f_noto, p.f_noto_s })
        if (f) TTF_CloseFont(f);
    p.f_word = p.f_num = p.f_num_s = p.f_big = p.f_noto = p.f_noto_s = nullptr;
}

bool build_brand(Panel& p)
{
    if (!p.f_word) return false;
    SDL_Surface* s = TTF_RenderUTF8_Blended(p.f_word, "gameOS", C_TEXT);
    SDL_Surface* b = TTF_RenderUTF8_Blended(p.f_word, "gameOS", { 255, 255, 255, 255 });
    if (!s || !b) {
        if (s) SDL_FreeSurface(s);
        if (b) SDL_FreeSurface(b);
        return false;
    }
    SDL_Texture* word = SDL_CreateTextureFromSurface(p.ren, s);
    SDL_Texture* sheen = SDL_CreateTextureFromSurface(p.ren, b);
    p.ww = s->w;
    p.wh = s->h;
    SDL_FreeSurface(s);
    SDL_FreeSurface(b);
    if (!word || !sheen) {
        if (word) SDL_DestroyTexture(word);
        if (sheen) SDL_DestroyTexture(sheen);
        return false;
    }
    if (p.word) SDL_DestroyTexture(p.word);
    if (p.sheen) SDL_DestroyTexture(p.sheen);
    p.word = word;
    p.sheen = sheen;
    SDL_SetTextureBlendMode(p.sheen, SDL_BLENDMODE_ADD);
    return true;
}

void build_glows(Panel& p)
{
    SDL_Texture* ga = make_radial(p.ren, 256, 2.4f, 220, C_ACCENT);
    SDL_Texture* gs = make_radial(p.ren, 256, 2.8f, 130, C_SHEEN);
    SDL_Texture* da = make_radial(p.ren, 32, 1.7f, 255, C_ACCENT);
    SDL_Texture* dw = make_radial(p.ren, 12, 1.4f, 255, { 255, 255, 255, 255 });
    for (SDL_Texture* t : { p.glow_a, p.glow_s, p.dot_a, p.dot_w })
        if (t) SDL_DestroyTexture(t);
    p.glow_a = ga;
    p.glow_s = gs;
    p.dot_a = da;
    p.dot_w = dw;
}

void load_panel_bg(Panel& p, const std::string& assets, const char* bgname)
{
    if (p.bg) { SDL_DestroyTexture(p.bg); p.bg = nullptr; }
    p.bg = IMG_LoadTexture(p.ren,
        (assets + "/themes/" + gameos::theme_key(g_theme) + "/" + bgname).c_str());
}

bool setup_panel(Panel& p, int display, bool top, const std::string& fonts,
                 const std::string& assets, const char* bgname)
{
    p.top = top;
    p.fontdir = fonts;
    p.win = SDL_CreateWindow("gameOS boot",
        SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display),
        W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!p.win) return false;
    p.ren = SDL_CreateRenderer(p.win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!p.ren) p.ren = SDL_CreateRenderer(p.win, -1, SDL_RENDERER_SOFTWARE);
    if (!p.ren) return false;
    SDL_SetRenderDrawBlendMode(p.ren, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(p.ren, W, H);
    open_fonts(p);
    if (top && !build_brand(p)) return false;
    build_glows(p);
    load_panel_bg(p, assets, bgname);
    p.cache_lang = gameos::g_lang;
    return true;
}

void teardown(Panel& p)
{
    Txt* txts[] = { &p.over, &p.tag, &p.stage_t, &p.pct_t, &p.corner_l, &p.corner_r,
                    &p.head, &p.sub, &p.pct_big, &p.curr, &p.micro,
                    &p.chip_lab, &p.chip_val, &p.arrow_t, &p.foot,
                    &p.bus_lab, &p.bus_val };
    for (Txt* t : txts) t->clear();
    for (Txt& t : p.row) t.clear();
    for (Txt& t : p.row_st) t.clear();
    for (SDL_Texture* t : { p.bg, p.word, p.sheen, p.glow_a, p.glow_s, p.dot_a, p.dot_w })
        if (t) SDL_DestroyTexture(t);
    p.bg = p.word = p.sheen = p.glow_a = p.glow_s = p.dot_a = p.dot_w = nullptr;
    close_fonts(p);
    if (p.ren) { SDL_DestroyRenderer(p.ren); p.ren = nullptr; }
    if (p.win) { SDL_DestroyWindow(p.win);   p.win = nullptr; }
}

// --- top panel: brand hero -------------------------------------------------
void draw_top(Panel& p, float t, float fade, const Boot& b)
{
    SDL_Renderer* r = p.ren;
    Uint8 fa = (Uint8)(fade * 255);

    vgrad(r, 0, 0, W, H, dark(C_BG, 0.55f), dark(C_BG, 0.92f));
    if (p.bg && fade < 1.f) blit(r, p.bg, 0, 0, W, H, (Uint8)((1.f - fade) * 255));

    // aurora: two slow accent clouds
    float bx = W * 0.5f + std::sin(t * 0.21f) * 70.f;
    float by = H * 0.40f + std::cos(t * 0.17f) * 34.f;
    blit(r, p.glow_a, bx - 330, by - 330, 660, 660,
         (Uint8)(fa * (0.26f + 0.05f * std::sin(t * 1.1f))));
    blit(r, p.glow_s, W * 0.42f + std::cos(t * 0.13f) * 90 - 240,
         H * 0.52f + std::sin(t * 0.19f) * 40 - 240, 480, 480,
         (Uint8)(fa * 0.10f));

    // starfield
    for (int i = 0; i < 34; i++) {
        int x = (i * 337 + 71) % W;
        int y = (i * 197 + 43) % H;
        float tw = 0.5f + 0.5f * std::sin(t * (0.7f + (i % 5) * 0.23f) + i);
        Uint8 a = (Uint8)(fa * (0.10f + 0.20f * tw) * (y < 560 ? 1.f : 0.4f));
        if (a < 6) continue;
        float s = (i % 7 == 0) ? 8.f : 4.f;
        blit(r, p.dot_w, x - s / 2, y - s / 2, s, s, a);
    }

    // orbit rings + satellites
    float cx = W * 0.5f, cy = 318.f;
    ering(r, cx, cy, 352.f, 118.f, -0.24f, 1.4f, with_alpha(C_ACCENT, (int)(fa * 0.24f)));
    ering(r, cx, cy, 286.f, 92.f, 0.20f, 1.0f, with_alpha(C_SHEEN, (int)(fa * 0.14f)));
    ering(r, cx, cy, 196.f, 62.f, -0.05f, 1.0f, with_alpha(C_ACCENT, (int)(fa * 0.12f)));

    struct Sat { float rx, ry, rot, speed, off; int size; };
    const Sat sats[2] = {
        { 352.f, 118.f, -0.24f, 0.32f, 0.f, 34 },
        { 286.f, 92.f, 0.20f, -0.47f, 2.1f, 24 },
    };
    for (const Sat& s : sats) {
        float ang = t * s.speed + s.off;
        for (int k = 7; k >= 0; k--) {
            EllipsePt q = ellipse_pt(cx, cy, s.rx, s.ry, s.rot, ang - k * 0.035f);
            Uint8 a = (Uint8)(fa * (k == 0 ? 0.85f : 0.30f * (1.f - k / 8.f)));
            float sz = k == 0 ? s.size : s.size * 0.55f;
            blit(r, p.dot_a, q.x - sz / 2, q.y - sz / 2, sz, sz, a);
        }
    }

    // wordmark
    float wa = clampf((t - 0.20f) / 0.55f, 0.f, 1.f);
    int wy = (int)(252 + (1.f - ease_out(wa)) * 16);
    if (p.ww) {
        blit(r, p.glow_a, cx - 420, wy + p.wh * 0.5f - 150, 840, 300,
             (Uint8)(fa * wa * (0.20f + 0.05f * std::sin(t * 1.4f))));
        SDL_SetTextureAlphaMod(p.word, (Uint8)(fa * wa));
        SDL_Rect d = { (int)(cx - p.ww / 2), wy, p.ww, p.wh };
        SDL_RenderCopy(r, p.word, nullptr, &d);

        // sheen: one narrow band every 3.6 s
        float ph = t < 1.2f ? -1.f : std::fmod(t - 1.2f, 3.6f) / 3.6f;
        if (ph >= 0.f && wa > 0.9f) {
            int band = (int)(p.ww * 0.11f) + 10;
            int sx = (int)(-band + ph * (p.ww + 2 * band));
            int x0 = std::max(0, sx), x1 = std::min(p.ww, sx + band);
            if (x1 > x0) {
                SDL_Rect src = { x0, 0, x1 - x0, p.wh };
                SDL_Rect dst = { d.x + x0, d.y, x1 - x0, p.wh };
                SDL_SetTextureAlphaMod(p.sheen, (Uint8)(fa * 0.55f));
                SDL_RenderCopy(r, p.sheen, &src, &dst);
            }
        }
    }

    // overline + tagline
    p.over.set(r, p.f_num_s, "G A M E O S   S Y S T E M", C_ACCENT);
    if (t > 0.15f) p.over.draw(r, (int)cx, 198, (int)(fa * clampf((t - 0.15f) / 0.4f, 0, 1) * 0.85f), true);
    p.tag.set(r, p.f_noto_s, "EVERY GAME. ONE SYSTEM.", mixc(C_DIM, C_TEXT, 0.35f));
    if (t > 0.55f) p.tag.draw(r, (int)cx, 494, (int)(fa * clampf((t - 0.55f) / 0.5f, 0, 1) * 0.9f), true);

    // underline draws itself, then a node breathes at its centre
    float ul = ease_out(clampf((t - 0.45f) / 0.7f, 0.f, 1.f));
    float half = 170.f * ul;
    int uy = wy + p.wh + 22;
    if (half > 2.f) {
        line(r, cx - half, (float)uy, cx + half, (float)uy, 2.f,
             with_alpha(C_ACCENT, (int)(fa * 0.75f)));
        line(r, cx - half * 0.55f, (float)uy, cx + half * 0.55f, (float)uy, 1.f,
             with_alpha(C_SHEEN, (int)(fa * 0.35f)));
        float pulse = 0.5f + 0.5f * std::sin(t * 2.2f);
        blit(r, p.glow_a, cx - 26, uy - 26, 52, 52, (Uint8)(fa * (0.35f + 0.25f * pulse)));
        disc(r, cx, (float)uy, 2.6f, with_alpha(C_SHEEN, (int)(fa * 0.9f)), 24);
    }

    // boot stage, centred, with a pulsing dot
    p.stage_t.set(r, p.f_noto, gameos::tr(b.stage), C_TEXT);
    int sw = p.stage_t.w, sh = p.stage_t.h;
    float dotp = 0.5f + 0.5f * std::sin(t * 3.0f);
    blit(r, p.dot_a, cx - sw / 2.f - 26, 588 + sh / 2.f - 9, 18, 18,
         (Uint8)(fa * (0.35f + 0.5f * dotp)));
    p.stage_t.draw(r, (int)cx, 588, (int)(fa * 0.92f), true);

    // hairline progress bar with ticks, percent on the right
    const int bx0 = 132, bx1 = 892, byy = 668;
    fill(r, bx0, byy, bx1 - bx0, 3, with_alpha(C_TRACK, (int)(fa * 0.9f)));
    for (int i = 1; i < 10; i++) {
        int x = bx0 + (bx1 - bx0) * i / 10;
        fill(r, x, byy - 3, 1, 9, with_alpha(C_DIM, (int)(fa * 0.28f)));
    }
    int fw = (int)((bx1 - bx0) * (b.pct / 100.f));
    if (fw > 1) {
        vgrad(r, bx0, byy, fw, 3, with_alpha(C_ACCENT, fa),
              with_alpha(mixc(C_ACCENT, C_SHEEN, 0.4f), fa));
        blit(r, p.glow_a, bx0 + fw - 22, byy - 20, 44, 44, (Uint8)(fa * 0.55f));
    }
    p.micro.set(r, p.f_num_s, "BOOT", C_DIM);
    p.micro.draw(r, bx0, byy - 26, (int)(fa * 0.6f));
    char pb[16];
    snprintf(pb, sizeof pb, "%d%%", (int)(b.pct + 0.5f));
    p.pct_t.set(r, p.f_num, pb, C_TEXT);
    p.pct_t.draw(r, bx1 - p.pct_t.w, byy - 36, fa);

    // corner microcopy
    p.corner_l.set(r, p.f_num_s, "GAMEOS // BOOT", C_DIM);
    p.corner_l.draw(r, 40, 36, (int)(fa * 0.55f));
    p.corner_r.set(r, p.f_num_s, "RK3568 // DSI x2", C_DIM);
    p.corner_r.draw(r, W - 40 - p.corner_r.w, 36, (int)(fa * 0.45f));
}

// --- bottom panel: boot monitor --------------------------------------------
void draw_bottom(Panel& p, float t, float fade, const Boot& b)
{
    SDL_Renderer* r = p.ren;
    Uint8 fa = (Uint8)(fade * 255);

    vgrad(r, 0, 0, W, H, dark(C_BG, 0.78f), dark(C_BG, 0.96f));
    if (p.bg && fade < 1.f) blit(r, p.bg, 0, 0, W, H, (Uint8)((1.f - fade) * 255));

    // blueprint grid
    for (int x = 0; x <= W; x += 64)
        fill(r, x, 0, 1, H, with_alpha(C_DIM, (int)(fa * 0.045f)));
    for (int y = 0; y <= H; y += 64)
        fill(r, 0, y, W, 1, with_alpha(C_DIM, (int)(fa * 0.045f)));

    blit(r, p.glow_a, 60, 120, 560, 560, (Uint8)(fa * 0.13f));
    blit(r, p.glow_a, 620, 30, 440, 440, (Uint8)(fa * 0.10f));

    // slow scanline
    float sy = std::fmod(t * 74.f, H + 160.f) - 80.f;
    if (sy > 0 && sy < H)
        fill(r, 0, (int)sy, W, 1, with_alpha(C_ACCENT, (int)(fa * 0.10f)));

    // header
    fill(r, 60, 46, 4, 28, with_alpha(C_ACCENT, fa));
    p.head.set(r, p.f_num, "BOOT MONITOR", C_TEXT);
    p.head.draw(r, 76, 44, fa);
    p.sub.set(r, p.f_noto_s, "GAMEOS DIAGNOSTICS", C_DIM);
    p.sub.draw(r, 77, 78, (int)(fa * 0.75f));

    // percent + segmented gauge
    char pb[16];
    snprintf(pb, sizeof pb, "%d%%", (int)(b.pct + 0.5f));
    p.pct_big.set(r, p.f_big, pb, C_TEXT);
    p.pct_big.draw(r, W - 60 - p.pct_big.w, 22, fa);
    const int segs = 24, seg_w = 9, gap = 4;
    int gx = W - 60 - segs * (seg_w + gap) + gap, gy = 146;
    int lit = (int)std::floor(b.pct / 100.f * segs);
    for (int i = 0; i < segs; i++) {
        SDL_Color c = i < lit ? with_alpha(C_ACCENT, fa)
                              : with_alpha(C_TRACK, (int)(fa * 0.9f));
        if (i == lit && b.pct < 99.5f) {
            float pulse = 0.5f + 0.5f * std::sin(t * 6.f);
            c = with_alpha(mixc(C_ACCENT, C_SHEEN, 0.35f), (int)(fa * (0.35f + 0.6f * pulse)));
        }
        fill(r, gx + i * (seg_w + gap), gy, seg_w, 6, c);
    }

    // handheld silhouette
    float bx = 70, by = 178, bw = 360, bh = 330;
    rrect(r, bx, by, bw, bh, 46, with_alpha(mixc(C_TRACK, C_BG, 0.35f), (int)(fa * 0.9f)));
    rrect_line(r, bx, by, bw, bh, 46, 2.f, with_alpha(C_DIM, (int)(fa * 0.30f)), 10);
    rrect(r, bx + 22, by + 20, bw - 44, bh - 44, 30, with_alpha(dark(C_BG, 0.5f), fa));

    // two screens
    float sx0 = bx + 40, sy0 = by + 34, sw0 = bw - 80, sh0 = 144;
    rrect(r, sx0, sy0, sw0, sh0, 10, with_alpha(dark(C_BG, 0.75f), fa));
    rrect_line(r, sx0, sy0, sw0, sh0, 10, 1.f, with_alpha(C_DIM, (int)(fa * 0.25f)));
    float sy1 = sy0 + sh0 + 16, sh1 = 100;
    rrect(r, sx0, sy1, sw0, sh1, 10, with_alpha(dark(C_BG, 0.75f), fa));
    rrect_line(r, sx0, sy1, sw0, sh1, 10, 1.f, with_alpha(C_DIM, (int)(fa * 0.25f)));

    // screen content: a card on top, a live waveform on the bottom
    for (int y = (int)sy0 + 3; y < (int)(sy0 + sh0); y += 4)
        fill(r, (int)sx0 + 2, y, (int)sw0 - 4, 1, with_alpha(C_ACCENT, (int)(fa * 0.05f)));
    float ccx = sx0 + sw0 * 0.5f, ccy = sy0 + sh0 * 0.5f;
    rrect(r, ccx - 46, ccy - 34, 92, 68, 8, with_alpha(C_TRACK, fa));
    rrect_line(r, ccx - 46, ccy - 34, 92, 68, 8, 1.f, with_alpha(C_ACCENT, (int)(fa * 0.5f)));
    fill(r, (int)ccx - 38, (int)ccy - 26, 76, 5,
         with_alpha(C_ACCENT, (int)(fa * 0.8f)));
    fill(r, (int)ccx - 38, (int)ccy - 14, 52, 3, with_alpha(C_DIM, (int)(fa * 0.5f)));
    fill(r, (int)ccx - 38, (int)ccy - 6, 62, 3, with_alpha(C_DIM, (int)(fa * 0.35f)));
    float lhead = t * 0.9f;
    float lp = 0.5f + 0.5f * std::sin(lhead);
    blit(r, p.dot_a, ccx - 10, ccy + 14, 20, 20, (Uint8)(fa * (0.3f + 0.5f * lp)));
    for (int i = 0; i < 20; i++) {
        float v = std::sin(t * 4.2f + i * 0.65f) * std::sin(t * 1.7f + i * 0.31f);
        int hgt = 3 + (int)(std::fabs(v) * 34.f * (b.pct / 100.f));
        fill(r, (int)sx0 + 12 + i * 13, (int)(sy1 + sh1 * 0.5f) - hgt / 2,
             6, hgt, with_alpha(C_ACCENT, (int)(fa * (0.35f + 0.3f * std::fabs(v)))));
    }

    // LED + power trace
    float led_pulse = 0.5f + 0.5f * std::sin(t * 3.2f);
    SDL_Color led = mixc({ 255, 92, 74, 255 }, C_ACCENT, b.pct / 100.f);
    blit(r, p.dot_a, bx + 10, by + 8, 24, 24, (Uint8)(fa * (0.25f + 0.35f * led_pulse)));
    disc(r, bx + 22, by + 20, 5.f, with_alpha(led, fa), 24);
    line(r, 18, 300, 44, 300, 2.f, with_alpha(C_ACCENT, (int)(fa * 0.35f)));
    line(r, 44, 300, 44, by + 20, 2.f, with_alpha(C_ACCENT, (int)(fa * 0.35f)));
    line(r, 44, by + 20, bx + 16, by + 20, 2.f, with_alpha(C_ACCENT, (int)(fa * 0.35f)));
    {
        // a packet runs the trace every 2.6 s
        float u = std::fmod(t, 2.6f) / 2.6f;
        float px, py;
        if (u < 0.16f) { px = 18 + (44 - 18) * (u / 0.16f); py = 300; }
        else if (u < 0.72f) { float k = (u - 0.16f) / 0.56f; px = 44; py = 300 + (by + 20 - 300) * k; }
        else { float k = clampf((u - 0.72f) / 0.28f, 0, 1); px = 44 + (bx + 10 - 44) * k; py = by + 20; }
        blit(r, p.dot_a, px - 10, py - 10, 20, 20, (Uint8)(fa * 0.8f));
    }
    // d-pad + buttons
    float dcx = bx + 74, dcy = by + bh - 58;
    rrect(r, dcx - 22, dcy - 8, 44, 16, 5, with_alpha(C_TRACK, fa));
    rrect(r, dcx - 8, dcy - 22, 16, 44, 5, with_alpha(C_TRACK, fa));
    rrect_line(r, dcx - 22, dcy - 8, 44, 16, 5, 1.f, with_alpha(C_DIM, (int)(fa * 0.3f)), 4);
    rrect_line(r, dcx - 8, dcy - 22, 16, 44, 5, 1.f, with_alpha(C_DIM, (int)(fa * 0.3f)), 4);
    const float ab[4][2] = { { bx + bw - 78, dcy - 16 }, { bx + bw - 56, dcy },
                             { bx + bw - 78, dcy + 16 }, { bx + bw - 100, dcy } };
    for (auto& q : ab) {
        disc(r, q[0], q[1], 9.f, with_alpha(C_TRACK, fa), 32);
        ring(r, q[0], q[1], 9.f, 1.f, with_alpha(C_DIM, (int)(fa * 0.3f)), 32);
    }

    // stage checklist
    int row_y = 190, row_h = 52;
    for (int i = 0; i < 6; i++) {
        int y = row_y + i * row_h;
        bool done = b.pct >= STAGE_DONE_AT[i];
        bool active = !done && (STAGE_ROWS[i] == b.stage);
        if (active)
            rrect(r, 512, y - 7, 448, 44, 12, with_alpha(C_ACCENT, (int)(fa * 0.08f)));
        float ccx2 = 540, ccy2 = y + 12;
        if (done) {
            disc(r, ccx2, ccy2, 11.f, with_alpha(C_ACCENT, (int)(fa * 0.16f)), 32);
            ring(r, ccx2, ccy2, 11.f, 1.5f, with_alpha(C_ACCENT, (int)(fa * 0.8f)), 32);
            line(r, ccx2 - 5, ccy2, ccx2 - 1, ccy2 + 4, 2.f, with_alpha(C_ACCENT, fa));
            line(r, ccx2 - 1, ccy2 + 4, ccx2 + 5, ccy2 - 4, 2.f, with_alpha(C_ACCENT, fa));
        } else if (active) {
            float pulse = 0.5f + 0.5f * std::sin(t * 5.f);
            ring(r, ccx2, ccy2, 11.f, 1.5f,
                 with_alpha(C_ACCENT, (int)(fa * (0.4f + 0.5f * pulse))), 32);
            disc(r, ccx2, ccy2, 3.f, with_alpha(C_ACCENT, fa), 20);
        } else {
            ring(r, ccx2, ccy2, 11.f, 1.f, with_alpha(C_DIM, (int)(fa * 0.25f)), 32);
        }
        p.row[i].set(r, p.f_noto, gameos::tr(STAGE_ROWS[i]),
                     done || active ? C_TEXT : with_alpha(C_DIM, 255));
        p.row[i].draw(r, 568, y - 2,
                      (int)(fa * (done || active ? 0.95f : 0.45f)));
        const char* st = done ? "OK" : (active ? "RUN" : "—");
        p.row_st[i].set(r, p.f_num_s, st,
                        done ? C_ACCENT : (active ? C_TEXT : C_DIM));
        p.row_st[i].draw(r, W - 72 - p.row_st[i].w, y + 2,
                         (int)(fa * (done ? 0.9f : 0.55f)));
        if (i < 5)
            fill(r, 568, y + 34, W - 72 - 568, 1,
                 with_alpha(C_DIM, (int)(fa * 0.10f)));
    }

    // data bus: packets run left to right above the waveform
    {
        const int by2 = 548;
        fill(r, 60, by2, 616, 1, with_alpha(C_DIM, (int)(fa * 0.14f)));
        for (int i = 0; i <= 14; i++)
            fill(r, 60 + i * 44, by2 - 2, 1, 5, with_alpha(C_DIM, (int)(fa * 0.18f)));
        for (int i = 0; i < 3; i++) {
            float u = std::fmod(t * 0.11f + i * 0.37f, 1.f);
            int x = 60 + (int)(u * 596.f);
            fill(r, x, by2 - 1, 14, 3, with_alpha(C_ACCENT, (int)(fa * 0.55f)));
            blit(r, p.dot_a, x - 4, by2 - 10, 22, 22, (Uint8)(fa * 0.20f));
        }
        p.bus_lab.set(r, p.f_num_s, "DATA BUS", C_DIM);
        p.bus_lab.draw(r, 60, by2 + 8, (int)(fa * 0.5f));
        char hx[16];
        snprintf(hx, sizeof hx, "0x%02X", (int)(b.pct) ^ 0x2A);
        p.bus_val.set(r, p.f_num_s, hx, C_ACCENT);
        p.bus_val.draw(r, 676 - p.bus_val.w, by2 + 8, (int)(fa * 0.6f));
    }

    // waveform + SoC chip
    const int bars = 56;
    for (int i = 0; i < bars; i++) {
        float v = std::sin(t * 3.1f + i * 0.42f) * std::sin(t * 1.3f + i * 0.17f);
        int hgt = 4 + (int)(std::fabs(v) * 62.f * (0.25f + 0.75f * b.pct / 100.f));
        int x = 62 + i * 10;
        fill(r, x, 640 - hgt, 6, hgt, with_alpha(C_ACCENT, (int)(fa * 0.5f)));
        fill(r, x, 640, 6, hgt / 4, with_alpha(C_ACCENT, (int)(fa * 0.10f)));
    }
    fill(r, 60, 640, 620, 1, with_alpha(C_DIM, (int)(fa * 0.25f)));

    rrect(r, 700, 556, 116, 84, 14, with_alpha(C_TRACK, (int)(fa * 0.8f)));
    rrect_line(r, 700, 556, 116, 84, 14, 1.f, with_alpha(C_DIM, (int)(fa * 0.35f)));
    for (int i = 0; i < 5; i++) {
        fill(r, 694, 570 + i * 12, 6, 2, with_alpha(C_ACCENT, (int)(fa * 0.4f)));
        fill(r, 816, 570 + i * 12, 6, 2, with_alpha(C_ACCENT, (int)(fa * 0.4f)));
    }
    p.chip_lab.set(r, p.f_num_s, "SoC", C_DIM);
    p.chip_lab.draw(r, 758 - p.chip_lab.w / 2, 566, (int)(fa * 0.7f));
    float blink = 0.5f + 0.5f * std::sin(t * 4.f);
    disc(r, 758, 606, 6.f, with_alpha(C_ACCENT, (int)(fa * (0.4f + 0.4f * blink))), 24);
    p.chip_val.set(r, p.f_noto_s, "RK3568", C_DIM);
    p.chip_val.draw(r, 758 - p.chip_val.w / 2, 616, (int)(fa * 0.6f));

    // stage line + blinking cursor
    p.curr.set(r, p.f_noto_s, gameos::tr(b.stage), C_TEXT);
    int cx0 = 60;
    p.arrow_t.set(r, p.f_num_s, ">", C_ACCENT);
    p.arrow_t.draw(r, cx0, 686, fa);
    p.curr.draw(r, cx0 + 26, 682, (int)(fa * 0.95f));
    int cur_x = cx0 + 26 + p.curr.w + 8;
    if (std::fmod(t, 1.1f) < 0.55f)
        fill(r, cur_x, 684, 10, 20, with_alpha(C_ACCENT, (int)(fa * 0.8f)));

    p.foot.set(r, p.f_num_s, "RK3568 // LINUX", C_DIM);
    p.foot.draw(r, W - 60 - p.foot.w, 690, (int)(fa * 0.45f));
}

// --- preview ---------------------------------------------------------------
void save_panel_png(SDL_Renderer* r, const std::string& path)
{
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s && SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                  s->pixels, s->pitch) == 0)
        IMG_SavePNG(s, path.c_str());
    if (s) SDL_FreeSurface(s);
}

} // namespace

int main()
{
    const bool preview = getenv("GAMEOS_SPLASH_PREVIEW") != nullptr;
    if (preview) mkdir(RUN_DIR, 0755);
    else daemonize();

    // DRM nodes / mesa may still be settling this early: retry for 8 s.
    bool ok = false;
    for (int i = 0; i < 80; i++) {
        if (SDL_Init(SDL_INIT_VIDEO) == 0) { ok = true; break; }
        fprintf(stderr, "splash: SDL_Init retry %d: %s\n", i, SDL_GetError());
        SDL_Delay(100);
    }
    if (!ok || TTF_Init() < 0) {
        fprintf(stderr, "splash: init failed: %s / %s\n", SDL_GetError(), TTF_GetError());
        SDL_Quit();
        cleanup_files();
        return 1;
    }
    IMG_Init(IMG_INIT_PNG);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_ShowCursor(SDL_DISABLE);

    std::string fonts  = env_dir("GAMEOS_FONTS",  "/usr/share/gameos/fonts");
    std::string assets = env_dir("GAMEOS_ASSETS", "/usr/share/gameos/assets");

    Panel top, bot;
    const gameos::Panels panels = gameos::panel_displays();
    const int dtop = panels.top;
    const int dbot = panels.bottom;
    if (!setup_panel(top, dtop, true, fonts, assets, "home_bg.png")) {
        fprintf(stderr, "splash: top panel: %s\n", SDL_GetError());
        teardown(top);
        TTF_Quit(); IMG_Quit(); SDL_Quit();
        cleanup_files();
        return 1;
    }
    if (dbot >= 0)
        setup_panel(bot, dbot, false, fonts, assets, "ambient_bg.png");  // best effort

    Boot boot;
    bool shot_done = false;
    const char* shotdir = getenv("GAMEOS_SPLASH_SHOT");
    float shot_at = 2.4f;
    if (const char* e = getenv("GAMEOS_SPLASH_AT")) shot_at = (float)atof(e);
    const bool shot_once = getenv("GAMEOS_SPLASH_ONCE") != nullptr;
    if (const char* e = getenv("GAMEOS_SPLASH_PCT")) {
        boot.target = atoi(e);
        boot.pct = (float)boot.target;
    }
    if (const char* e = getenv("GAMEOS_SPLASH_STAGE")) boot.stage = stage_key(e);

    Uint32 t0 = SDL_GetTicks();
    float fade = 1.f;
    bool stopping = false;
    for (;;) {
        // Failsafe: never hold DRM master forever if the UI never comes.
        if (!preview && !stopping &&
            (access(STOP_FILE, F_OK) == 0 || SDL_GetTicks() - t0 > 90000))
            stopping = true;
        float t = (SDL_GetTicks() - t0) / 1000.f;
        poll_boot(boot);
        if (g_theme_dirty) {
            g_theme_dirty = false;
            apply_splash_theme(g_theme, assets);
            build_brand(top);
            build_glows(top);
            load_panel_bg(top, assets, "home_bg.png");
            if (bot.ren) {
                build_glows(bot);
                load_panel_bg(bot, assets, "ambient_bg.png");
            }
        }
        if (stopping) {
            boot.target = 100;               // finish the bars during the handoff
            fade -= 1.f / 15.f;              // ~250 ms cross-fade at 60 fps
            if (fade < 0.f) fade = 0.f;
        }
        boot.pct += (boot.target - boot.pct) * 0.15f;
        if (std::fabs(boot.target - boot.pct) < 0.4f) boot.pct = (float)boot.target;
        draw_top(top, t, fade, boot);
        if (bot.ren) draw_bottom(bot, t, fade, boot);

        if (shotdir && !shot_done && t >= shot_at) {
            shot_done = true;
            std::string dir = shotdir;
            save_panel_png(top.ren, dir + "/top.png");
            if (bot.ren) save_panel_png(bot.ren, dir + "/bot.png");
            if (shot_once) break;
        }

        SDL_RenderPresent(top.ren);
        if (bot.ren) SDL_RenderPresent(bot.ren);
        if (stopping && fade <= 0.f) break;
        SDL_Delay(1);
    }

    teardown(top);
    teardown(bot);
    TTF_Quit(); IMG_Quit(); SDL_Quit();
    cleanup_files();
    return 0;
}
