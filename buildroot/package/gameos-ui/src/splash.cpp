// gameOS boot splash — the first thing the panels show.
//
// Started by /etc/init.d/S00splash before every other service, so the boot
// never exposes console text: the kernel console lives on the serial port
// only (see the APPEND line in post-image.sh), and this daemon paints an
// animated brand screen on both DSI panels until the front-end is ready.
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
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int W = 1024;
constexpr int H = 768;

const SDL_Color C_BG     = {  11,  16,  32, 255 };
const SDL_Color C_ACCENT = {  53, 208, 186, 255 };
const SDL_Color C_SHEEN  = { 214, 255, 247, 255 };

const char* RUN_DIR   = "/run/gameos";
const char* PID_FILE  = "/run/gameos/splash.pid";
const char* STOP_FILE = "/run/gameos/splash.stop";

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
};

bool setup_panel(Panel& p, int display, float scale, const std::string& fonts,
                 const std::string& assets, const char* bgname)
{
    p.scale = scale;
    p.win = SDL_CreateWindow("gameOS boot",
        SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display),
        W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!p.win) return false;
    p.ren = SDL_CreateRenderer(p.win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!p.ren) p.ren = SDL_CreateRenderer(p.win, -1, SDL_RENDERER_SOFTWARE);
    if (!p.ren) return false;
    SDL_SetRenderDrawBlendMode(p.ren, SDL_BLENDMODE_BLEND);

    TTF_Font* f = TTF_OpenFont((fonts + "/Orbitron.ttf").c_str(), (int)(110 * scale));
    if (!f) f = TTF_OpenFont((fonts + "/NotoSansSC-Regular.otf").c_str(), (int)(96 * scale));
    if (!f) return false;
    SDL_Surface* s = TTF_RenderUTF8_Blended(f, "gameOS", C_ACCENT);
    SDL_Surface* b = TTF_RenderUTF8_Blended(f, "gameOS", C_SHEEN);
    TTF_CloseFont(f);
    if (!s || !b) {
        if (s) SDL_FreeSurface(s);
        if (b) SDL_FreeSurface(b);
        return false;
    }
    p.lw = s->w;
    p.lh = s->h;
    p.logo  = SDL_CreateTextureFromSurface(p.ren, s);
    p.sheen = SDL_CreateTextureFromSurface(p.ren, b);
    SDL_FreeSurface(s);
    SDL_FreeSurface(b);
    if (p.sheen) SDL_SetTextureBlendMode(p.sheen, SDL_BLENDMODE_ADD);
    p.glow = make_radial(p.ren, 256, 2.2f, 150);
    p.dot  = make_radial(p.ren, 24, 1.6f, 255);
    p.bg   = IMG_LoadTexture(p.ren, (assets + "/" + bgname).c_str());
    return p.logo && p.sheen && p.glow && p.dot;
}

void teardown(Panel& p)
{
    for (SDL_Texture* t : { p.bg, p.dot, p.glow, p.sheen, p.logo })
        if (t) SDL_DestroyTexture(t);
    p.bg = p.dot = p.glow = p.sheen = p.logo = nullptr;
    if (p.ren) { SDL_DestroyRenderer(p.ren); p.ren = nullptr; }
    if (p.win) { SDL_DestroyWindow(p.win);   p.win = nullptr; }
}

// fade = 1: full animation. fade -> 0: cross-fade into the UI background.
void draw_panel(Panel& p, float t, float fade)
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
    if (!setup_panel(top, 0, 1.f, fonts, assets, "home_bg.png")) {
        fprintf(stderr, "splash: top panel: %s\n", SDL_GetError());
        teardown(top);
        TTF_Quit(); IMG_Quit(); SDL_Quit();
        cleanup_files();
        return 1;
    }
    if (SDL_GetNumVideoDisplays() > 1)
        setup_panel(bot, 1, 0.62f, fonts, assets, "ambient_bg.png");  // best effort

    Uint32 t0 = SDL_GetTicks();
    float fade = 1.f;
    bool stopping = false;
    for (;;) {
        // Failsafe: never hold DRM master forever if the UI never comes.
        if (!stopping && (access(STOP_FILE, F_OK) == 0 || SDL_GetTicks() - t0 > 90000))
            stopping = true;
        float t = (SDL_GetTicks() - t0) / 1000.f;
        if (stopping) {
            fade -= 1.f / 15.f;              // ~250 ms cross-fade at 60 fps
            if (fade < 0.f) fade = 0.f;
        }
        draw_panel(top, t, fade);
        if (bot.ren) draw_panel(bot, t, fade * 0.8f);
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