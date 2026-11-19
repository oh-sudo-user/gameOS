// gameOS boot splash — the first thing the panels show.
//
// Started by /etc/init.d/S00splash before every other service, so the boot
// never exposes console text: the kernel console lives on the serial port
// only (see the APPEND line in post-image.sh), and this daemon paints an
// animated brand screen on both DSI panels until the front-end is ready.
//
// The init scripts publish milestones with gameos-bootstage; this daemon
// renders them as a localised stage line plus a progress bar, so a slow first
// boot (partition creation, WiFi firmware) looks deliberate instead of hung.
// The language is picked up from /userdata/system/gameos.ini as soon as the
// data partition is mounted, and falls back to Simplified Chinese before that.
//
// Handoff: gameos-ui writes /run/gameos/splash.stop just before SDL_Init;
// we cross-fade into the UI's own background over ~250 ms, release DRM
// master and exit, so the front-end's first frame lands on identical
// pixels — no flash, no black gap.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "i18n.hpp"

namespace {

constexpr int W = 1024;
constexpr int H = 768;

// Mutable: the saved theme is read once /userdata mounts and the whole
// brand motif + cross-fade background switch to match the UI's palette.
SDL_Color C_BG     = {  11,  16,  32, 255 };
SDL_Color C_ACCENT = {  53, 208, 186, 255 };
SDL_Color C_SHEEN  = { 214, 255, 247, 255 };
SDL_Color C_TEXT   = { 226, 236, 245, 255 };
SDL_Color C_DIM    = { 140, 158, 178, 255 };
SDL_Color C_TRACK  = {  28,  38,  58, 255 };
int g_theme = 0;
bool g_theme_dirty = false;

void apply_splash_theme(int idx)
{
    gameos::ThemePal p = gameos::theme_palette(idx);
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

// Double-fork so rcS continues immediately; stdio goes to a log on tmpfs
// (/userdata does not exist yet at S00 time). The pid file is how the UI
// knows whom to wait for during the handoff.
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

// Soft radial sprite (halo / spinner dot), additive so it glows on the
// dark background instead of looking like a pasted sticker.
SDL_Texture* make_radial(SDL_Renderer* ren, int size, float power, Uint8 peak)
{
    SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC, size, size);
    if (!t) return nullptr;
    Uint32* px = new Uint32[size * size];
    float c = (size - 1) / 2.f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float d = std::sqrt((x - c) * (x - c) + (y - c) * (y - c)) / c;
            float a = d >= 1.f ? 0.f : std::pow(1.f - d, power);
            Uint8 al = (Uint8)(a * peak);
            px[y * size + x] = (Uint32(al) << 24) | (Uint32(C_ACCENT.r) << 16)
                             | (Uint32(C_ACCENT.g) << 8) | Uint32(C_ACCENT.b);
        }
    }
    SDL_UpdateTexture(t, nullptr, px, size * 4);
    delete[] px;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    return t;
}

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
    float pct = 0.f;                          // smoothed, drives the bar
    int   target = 0;                         // last published percent
    gameos::TKey stage = gameos::T_BOOT_DISPLAY;
};

// The stage file is rewritten by tmp+rename, so a read either sees the old
// contents or the new ones, never a mix. Poll at 6 Hz: init milestones are
// seconds apart and this runs on the same CPU as the boot itself.
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

struct Panel {
    SDL_Window*   win   = nullptr;
    SDL_Renderer* ren   = nullptr;
    SDL_Texture*  logo  = nullptr;   // brand text in the theme accent
    SDL_Texture*  sheen = nullptr;   // same text, bright: the light sweep
    SDL_Texture*  glow  = nullptr;   // breathing halo behind the brand
    SDL_Texture*  dot   = nullptr;   // soft dot for the orbit spinner
    SDL_Texture*  bg    = nullptr;   // the UI background, for the cross-fade
    int lw = 0, lh = 0;              // logo texture size
    float scale = 1.f;               // bottom panel draws a smaller motif
    std::string fontdir;
    TTF_Font* f_stage = nullptr;     // localised boot stage line
    TTF_Font* f_pct   = nullptr;     // percent label
    SDL_Texture* stage_tex = nullptr;
    SDL_Texture* pct_tex   = nullptr;
    int stage_w = 0, stage_h = 0, pct_w = 0, pct_h = 0;
    int cache_stage = -1, cache_lang = -1, cache_pct = -1;
};

void set_text(SDL_Renderer* r, TTF_Font* f, const char* s, const SDL_Color& c,
              SDL_Texture*& tex, int& w, int& h)
{
    if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
    w = h = 0;
    if (!f || !s || !*s) return;
    SDL_Surface* su = TTF_RenderUTF8_Blended(f, s, c);
    if (!su) return;
    w = su->w;
    h = su->h;
    tex = SDL_CreateTextureFromSurface(r, su);
    SDL_FreeSurface(su);
}

// Korean needs its own face; the SC font is used for every other language.
// Text is scaled less aggressively than the brand motif so the stage line
// stays readable on the bottom panel.
void panel_fonts(Panel& p, int lang)
{
    if (p.f_stage) { TTF_CloseFont(p.f_stage); p.f_stage = nullptr; }
    if (p.f_pct)   { TTF_CloseFont(p.f_pct);   p.f_pct   = nullptr; }
    float ts = 0.6f + 0.4f * p.scale;
    std::string n = p.fontdir
        + (lang == gameos::LANG_KO ? "/NotoSansKR-Regular.ttf"
                                   : "/NotoSansSC-Regular.otf");
    p.f_stage = TTF_OpenFont(n.c_str(), (int)(30 * ts));
    p.f_pct   = TTF_OpenFont(n.c_str(), (int)(22 * ts));
    if (!p.f_stage || !p.f_pct) {
        std::string a = p.fontdir + "/NotoSansSC-Regular.otf";
        if (!p.f_stage) p.f_stage = TTF_OpenFont(a.c_str(), (int)(30 * ts));
        if (!p.f_pct)   p.f_pct   = TTF_OpenFont(a.c_str(), (int)(22 * ts));
    }
    p.cache_stage = -1;
    p.cache_pct = -1;
}

// (Re)build the brand motif in the current palette. New textures are created
// first and only swapped in on full success, so a mid-boot theme change can
// never leave a panel with a null logo.
bool build_brand(Panel& p, const std::string& fonts)
{
    TTF_Font* f = TTF_OpenFont((fonts + "/Orbitron.ttf").c_str(), (int)(110 * p.scale));
    if (!f) f = TTF_OpenFont((fonts + "/NotoSansSC-Regular.otf").c_str(), (int)(96 * p.scale));
    if (!f) return false;
    SDL_Surface* s = TTF_RenderUTF8_Blended(f, "gameOS", C_ACCENT);
    SDL_Surface* b = TTF_RenderUTF8_Blended(f, "gameOS", C_SHEEN);
    TTF_CloseFont(f);
    if (!s || !b) {
        if (s) SDL_FreeSurface(s);
        if (b) SDL_FreeSurface(b);
        return false;
    }
    int lw = s->w, lh = s->h;
    SDL_Texture* logo  = SDL_CreateTextureFromSurface(p.ren, s);
    SDL_Texture* sheen = SDL_CreateTextureFromSurface(p.ren, b);
    SDL_FreeSurface(s);
    SDL_FreeSurface(b);
    SDL_Texture* glow = make_radial(p.ren, 256, 2.2f, 150);
    SDL_Texture* dot  = make_radial(p.ren, 24, 1.6f, 255);
    if (!logo || !sheen || !glow || !dot) {
        for (SDL_Texture* t : { logo, sheen, glow, dot })
            if (t) SDL_DestroyTexture(t);
        return false;
    }
    for (SDL_Texture* t : { p.logo, p.sheen, p.glow, p.dot })
        if (t) SDL_DestroyTexture(t);
    p.logo = logo; p.sheen = sheen; p.glow = glow; p.dot = dot;
    p.lw = lw; p.lh = lh;
    SDL_SetTextureBlendMode(p.sheen, SDL_BLENDMODE_ADD);
    return true;
}

void load_panel_bg(Panel& p, const std::string& assets, const char* bgname)
{
    if (p.bg) { SDL_DestroyTexture(p.bg); p.bg = nullptr; }
    p.bg = IMG_LoadTexture(p.ren,
        (assets + "/themes/" + gameos::theme_key(g_theme) + "/" + bgname).c_str());
}

bool setup_panel(Panel& p, int display, float scale, const std::string& fonts,
                 const std::string& assets, const char* bgname)
{
    p.scale = scale;
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
    // Design space stays 1024x768; on the 640x480 RG DS panels SDL scales it.
    SDL_RenderSetLogicalSize(p.ren, W, H);

    if (!build_brand(p, fonts)) return false;
    load_panel_bg(p, assets, bgname);
    panel_fonts(p, gameos::g_lang);
    p.cache_lang = gameos::g_lang;
    return true;
}

void teardown(Panel& p)
{
    for (SDL_Texture* t : { p.bg, p.dot, p.glow, p.sheen, p.logo,
                            p.stage_tex, p.pct_tex })
        if (t) SDL_DestroyTexture(t);
    p.bg = p.dot = p.glow = p.sheen = p.logo = nullptr;
    p.stage_tex = p.pct_tex = nullptr;
    if (p.f_stage) { TTF_CloseFont(p.f_stage); p.f_stage = nullptr; }
    if (p.f_pct)   { TTF_CloseFont(p.f_pct);   p.f_pct   = nullptr; }
    if (p.ren) { SDL_DestroyRenderer(p.ren); p.ren = nullptr; }
    if (p.win) { SDL_DestroyWindow(p.win);   p.win = nullptr; }
}

// fade = 1: full animation. fade -> 0: cross-fade into the UI background.
void draw_panel(Panel& p, float t, float fade, const Boot& b)
{
    SDL_Renderer* r = p.ren;
    SDL_SetRenderDrawColor(r, C_BG.r, C_BG.g, C_BG.b, 255);
    SDL_RenderClear(r);
    if (p.bg && fade < 1.f) {
        SDL_SetTextureAlphaMod(p.bg, (Uint8)((1.f - fade) * 255));
        SDL_RenderCopy(r, p.bg, nullptr, nullptr);
    }
    if (fade <= 0.f) return;
    Uint8 fa = (Uint8)(fade * 255);

    float cx = W / 2.f;
    float cy = H * 0.42f;

    // breathing halo
    float pulse = 0.5f + 0.5f * std::sin(t * 2.1f);
    int gw = (int)(p.lw * 2.1f * (1.f + 0.06f * pulse));
    int gh = (int)(p.lh * 3.4f * (1.f + 0.06f * pulse));
    SDL_SetTextureAlphaMod(p.glow, (Uint8)(fa * (0.45f + 0.35f * pulse)));
    SDL_Rect gdst = { (int)(cx - gw / 2.f), (int)(cy - gh / 2.f), gw, gh };
    SDL_RenderCopy(r, p.glow, nullptr, &gdst);

    // brand
    SDL_Rect ldst = { (int)(cx - p.lw / 2.f), (int)(cy - p.lh / 2.f), p.lw, p.lh };
    SDL_SetTextureAlphaMod(p.logo, fa);
    SDL_RenderCopy(r, p.logo, nullptr, &ldst);

    // light sweep across the letters, once every 2.6 s
    int band = (int)(p.lw * 0.16f) + 8;
    float ph = std::fmod(t, 2.6f) / 2.6f;
    int sx = (int)(-band + ph * (p.lw + 2 * band));
    int x0 = sx < 0 ? 0 : sx;
    int x1 = sx + band > p.lw ? p.lw : sx + band;
    if (x1 > x0) {
        SDL_Rect src = { x0, 0, x1 - x0, p.lh };
        SDL_Rect dst = { ldst.x + x0, ldst.y, x1 - x0, p.lh };
        SDL_SetTextureAlphaMod(p.sheen, (Uint8)(fa * 0.85f));
        SDL_RenderCopy(r, p.sheen, &src, &dst);
    }

    // orbit spinner: 12 soft dots with a fading trail
    float scy = cy + p.lh * 0.5f + 92.f * p.scale;
    float rad = 46.f * p.scale;
    const int n = 12;
    float head = std::fmod(t * 0.9f, 1.f) * n;
    for (int i = 0; i < n; i++) {
        float ang = (i / (float)n) * 6.2831853f - 1.5707963f;
        float trail = std::fmod(head - i + n, n) / n;
        float k = 1.f - trail;
        Uint8 a = (Uint8)(fa * k * k * 235);
        if (a < 6) continue;
        int ds = (int)(18 * p.scale) + 2;
        SDL_Rect d = { (int)(cx + std::cos(ang) * rad - ds / 2.f),
                       (int)(scy + std::sin(ang) * rad - ds / 2.f), ds, ds };
        SDL_SetTextureAlphaMod(p.dot, a);
        SDL_RenderCopy(r, p.dot, nullptr, &d);
    }

    // --- boot stage line + progress bar ---
    int li = (int)gameos::g_lang;
    if (p.cache_lang != li) { panel_fonts(p, li); p.cache_lang = li; }
    int si = (int)b.stage;
    if (p.cache_stage != si) {
        set_text(r, p.f_stage, gameos::tr(b.stage), C_TEXT,
                 p.stage_tex, p.stage_w, p.stage_h);
        p.cache_stage = si;
    }
    int ip = (int)(b.pct + 0.5f);
    if (p.cache_pct != ip) {
        char buf[16];
        snprintf(buf, sizeof buf, "%d%%", ip);
        set_text(r, p.f_pct, buf, C_DIM, p.pct_tex, p.pct_w, p.pct_h);
        p.cache_pct = ip;
    }

    float bw = 520.f * p.scale;
    float bh = 8.f * p.scale;
    float bx = cx - bw / 2.f;
    float by = H * 0.79f;

    if (p.stage_tex) {
        SDL_SetTextureAlphaMod(p.stage_tex, fa);
        SDL_Rect d = { (int)(cx - p.stage_w / 2.f),
                       (int)(H * 0.735f - p.stage_h / 2.f),
                       p.stage_w, p.stage_h };
        SDL_RenderCopy(r, p.stage_tex, nullptr, &d);
    }

    SDL_SetRenderDrawColor(r, C_TRACK.r, C_TRACK.g, C_TRACK.b, fa);
    SDL_Rect track = { (int)bx, (int)by, (int)bw, (int)bh };
    SDL_RenderFillRect(r, &track);

    float fw = bw * (b.pct / 100.f);
    if (fw > 1.f) {
        SDL_SetRenderDrawColor(r, C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, fa);
        SDL_Rect fill = { (int)bx, (int)by, (int)fw, (int)bh };
        SDL_RenderFillRect(r, &fill);
        SDL_SetRenderDrawColor(r, C_SHEEN.r, C_SHEEN.g, C_SHEEN.b,
                               (Uint8)(fa * 0.7f));
        SDL_Rect tip = { (int)(bx + fw) - 2, (int)by, 2, (int)bh };
        SDL_RenderFillRect(r, &tip);
    }

    if (p.pct_tex) {
        SDL_SetTextureAlphaMod(p.pct_tex, fa);
        SDL_Rect d = { (int)(cx - p.pct_w / 2.f),
                       (int)(by + bh + 14.f * p.scale), p.pct_w, p.pct_h };
        SDL_RenderCopy(r, p.pct_tex, nullptr, &d);
    }
}

} // namespace

int main()
{
    daemonize();

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
    // DSI-2 is the physical top (lid) panel; SDL enumerates DSI-1 first.
    int dtop = 0, ndisp = SDL_GetNumVideoDisplays();
    for (int i = 0; i < ndisp; i++) {
        const char *dn = SDL_GetDisplayName(i);
        if (dn && strstr(dn, "DSI-2")) { dtop = i; break; }
    }
    int dbot = (ndisp > 1) ? (dtop == 0 ? 1 : 0) : -1;
    if (!setup_panel(top, dtop, 1.f, fonts, assets, "home_bg.png")) {
        fprintf(stderr, "splash: top panel: %s\n", SDL_GetError());
        teardown(top);
        TTF_Quit(); IMG_Quit(); SDL_Quit();
        cleanup_files();
        return 1;
    }
    if (dbot >= 0)
        setup_panel(bot, dbot, 0.62f, fonts, assets, "ambient_bg.png");  // best effort

    Uint32 t0 = SDL_GetTicks();
    float fade = 1.f;
    bool stopping = false;
    Boot boot;
    for (;;) {
        // Failsafe: never hold DRM master forever if the UI never comes.
        if (!stopping && (access(STOP_FILE, F_OK) == 0 || SDL_GetTicks() - t0 > 90000))
            stopping = true;
        float t = (SDL_GetTicks() - t0) / 1000.f;
        poll_boot(boot);
        if (g_theme_dirty) {
            g_theme_dirty = false;
            apply_splash_theme(g_theme);
            build_brand(top, fonts);
            load_panel_bg(top, assets, "home_bg.png");
            if (bot.ren) {
                build_brand(bot, fonts);
                load_panel_bg(bot, assets, "ambient_bg.png");
            }
        }
        if (stopping) {
            boot.target = 100;               // finish the bar during the handoff
            fade -= 1.f / 15.f;              // ~250 ms cross-fade at 60 fps
            if (fade < 0.f) fade = 0.f;
        }
        boot.pct += (boot.target - boot.pct) * 0.15f;
        if (std::fabs(boot.target - boot.pct) < 0.4f) boot.pct = (float)boot.target;
        draw_panel(top, t, fade, boot);
        if (bot.ren) draw_panel(bot, t, fade * 0.8f, boot);
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