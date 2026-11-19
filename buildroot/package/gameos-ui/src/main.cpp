// gameOS front-end — main application.
//
// Custom SDL2 launcher for the Anbernic RG DS Plus:
//   top panel    : console carousel / game list / settings
//   bottom panel : ambient clock & branding
//
// Controls (physical layout):
//   right button (A) = confirm      bottom button (B) = back
//   D-pad / stick    = navigate     START = settings / home
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"
#include "i18n.hpp"
#include "sound.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

using namespace gameos;

namespace {

constexpr int W = 1024;
constexpr int H = 768;

// Home carousel: five consoles, then the two libraries, then settings.
constexpr int CAR_N        = 8;
constexpr int CAR_ACTIVITY = 5;
constexpr int CAR_QUICK    = 6;
constexpr int CAR_SETTINGS = 7;

// Mutable palette: apply_theme() swaps the whole set (light themes need
// dark text and light panels), so every draw call stays theme-correct.
SDL_Color C_TEXT   = { 232, 236, 247, 255 };
SDL_Color C_DIM    = { 138, 147, 178, 255 };
SDL_Color C_ACCENT = {  53, 208, 186, 255 };
const SDL_Color C_AMBER  = { 255, 180,  84, 255 };
SDL_Color C_PANEL  = {  20,  27,  51, 235 };
SDL_Color C_BG     = {  11,  16,  32, 255 };
SDL_Color C_TRACK  = {  28,  38,  58, 255 };
const SDL_Color C_RED    = { 255,  99,  99, 255 };
const SDL_Color C_GREEN  = { 105, 220, 140, 255 };
int g_tint_top = 14, g_tint_bot = 26;
SDL_Color g_accent = C_ACCENT;

enum class Screen { Home, List, Settings, Wifi, Bluetooth, Saves, Update, Setup,
                    Activity, Quick, Msg, Confirm };
enum class ConfirmAct { None, PowerOff, Reboot, DeleteSave, Update };

struct BtDev {
    std::string mac, name, kind;
    bool paired = false, connected = false;
};

// A favorite/history key resolved against the scanned library. The ROM may be
// gone (deleted, or the TF2 card pulled): the row stays visible and is marked
// missing rather than silently disappearing from the user's collection.
struct QuickEntry {
    int con = 0;               // index into consoles()
    std::string sys;           // system id as stored in the key
    Game game;
    bool missing = false;
    long long seconds = 0;
    long long last = 0;
};

struct WifiNet {
    std::string ssid;
    int  signal = 0;      // dBm
    bool secure = false;
    bool saved = false;
};

struct App {
    SDL_Window*   win_top = nullptr;
    SDL_Window*   win_bot = nullptr;
    SDL_Renderer* ren_top = nullptr;
    SDL_Renderer* ren_bot = nullptr;

    TTF_Font* f_logo  = nullptr;   // Orbitron 52
    TTF_Font* f_title = nullptr;   // Noto 40
    TTF_Font* f_body  = nullptr;   // Noto 30
    TTF_Font* f_small = nullptr;   // Noto 22
    TTF_Font* f_clock = nullptr;   // Orbitron 96

    SDL_Texture* bg = nullptr;
    SDL_Texture* ambient = nullptr;
    SDL_Texture* logo = nullptr;
    SDL_Texture* icons[5] = {};
    SDL_Texture* icon_settings = nullptr;
    SDL_Texture* icon_activity = nullptr;
    SDL_Texture* icon_quick = nullptr;

    SDL_GameController* pad = nullptr;

    Settings settings;
    Screen screen = Screen::Home;
    int carousel = 0;              // 0..4 consoles, 5 = settings
    std::vector<Game> games;
    int sel = 0;
    int scroll = 0;
    int set_sel = 0;
    bool fav_only = false;
    std::vector<SaveEntry> saves;
    int save_sel = 0;
    int save_scroll = 0;
    int save_del_idx = -1;
    std::string msg_title, msg_body;
    ConfirmAct confirm = ConfirmAct::None;

    // Bluetooth screen state (ops run via gameos-bt in the background)
    bool bt_on = false;
    bool bt_busy = false;
    std::string bt_op;                 // what the running op is: up/down/scan/devices/pair/...
    std::string bt_status;             // one-line feedback under the list
    std::vector<BtDev> bt_devs;
    int bt_sel = 0;                    // 0 = on/off row, 1..N = devices
    int bt_scroll = 0;
    Uint32 bt_poll_at = 0;
    Uint32 bt_op_at = 0;               // when the running op started (timeout)

    // WiFi screen state (ops run via gameos-wifi in the background)
    bool wifi_busy = false;
    std::string wifi_op;               // scan/connect/forget
    std::string wifi_status;           // one-line feedback under the list
    std::string wifi_state;            // wpa_state/NOHW from gameos-wifi status
    std::string wifi_link;             // associated SSID
    std::string wifi_ip;
    std::vector<WifiNet> wifi_nets;
    std::vector<std::string> wifi_known;
    int wifi_sel = 0, wifi_scroll = 0;
    Uint32 wifi_poll_at = 0;
    Uint32 wifi_op_at = 0;
    Uint32 wifi_status_at = 0;
    // password keyboard
    bool wifi_kb = false;
    std::string wifi_target, wifi_pw;
    int kb_r = 1, kb_c = 0, kb_shift = 0;
    // the same keyboard also edits wizard fields; kb_buf then points at one of
    // the App strings below instead of wifi_pw
    bool kb_generic = false;
    std::string* kb_buf = nullptr;
    std::string kb_orig, kb_title;
    int kb_limit = 63;
    bool kb_empty_ok = false;
    bool kb_mask = false;

    // system update screen (gameos-update runs in the background)
    std::string upd_state, upd_version, upd_msg;
    std::string upd_cur;               // /etc/gameos-version
    std::string upd_op;                // running op: check/apply
    int upd_pct = 0;
    bool upd_busy = false;
    Uint32 upd_poll_at = 0;
    Uint32 upd_op_at = 0;
    Uint32 upd_refresh_at = 0;

    // second microSD slot (gameos-tf2 mounts it at /mnt/tf2; refreshed 1/s)
    bool tf2_mounted = false;

    // first-boot wizard (gameos-setup / gameos-covers run in the background)
    int  setup_step = 0;               // 0 lang, 1 wifi, 2 tz, 3 name, 4 ra, 5 covers, 6 done
    int  setup_sel = 0;                // row inside the current step
    int  setup_scroll = 0;
    int  setup_col = 0;                // timezone step: 0 region, 1 zone
    int  setup_region = 0;             // region whose zones are listed
    bool setup_busy = false;
    std::string setup_op;              // timezone/hostname/ra-login/ra-clear/wifi-test/covers
    std::string setup_status;          // one-line feedback
    Uint32 setup_poll_at = 0;
    Uint32 setup_op_at = 0;
    std::string setup_test[4];         // link/route/dns/net -> ok|fail|wait
    std::string setup_tz;              // zone currently applied
    std::string setup_name;            // hostname being edited
    std::string setup_ra_user, setup_ra_pass;
    bool setup_ra_hardcore = false;
    bool setup_ra_logged = false;
    std::string setup_ra_name;         // account reported by the last login
    int  cov_total = 0, cov_done = 0, cov_ok = 0, cov_miss = 0, cov_fail = 0;
    std::string cov_state, cov_current;

    // activity statistics screen
    int act_tab = 0;                   // 0 overview, 1 timeline, 2 trends, 3 picks
    int act_sel = 0, act_scroll = 0;
    long long act_total = 0;
    std::vector<PlaySession> act_sessions;
    std::vector<GameStat> act_games;
    std::vector<long long> act_daily;
    std::vector<Pick> act_picks;

    // favorites / history quick screen
    int quick_tab = 0;                 // 0 favorites, 1 history
    int quick_sel = 0, quick_scroll = 0;
    std::vector<QuickEntry> quick;

    bool running = true;
    int exit_code = 0;

    // navigation repeat
    int hold_dir = 0;              // -1 / +1 vertical or horizontal
    Uint32 hold_since = 0;
    Uint32 last_repeat = 0;
};

App app;

// ---------------------------------------------------------------------------
// per-console game list cache
// ---------------------------------------------------------------------------
// draw_home() runs every frame (~60 fps). Scanning the ROM tree each time
// would hammer the SD card and burn CPU, so scan once per console and reuse
// the result until invalidated (entering a console, returning from an
// emulator, or an explicit refresh).
static std::vector<Game> g_games_cache[5];
static bool g_games_valid[5] = { false, false, false, false, false };

static const std::vector<Game>& games_for(int idx)
{
    static const std::vector<Game> empty;
    if (idx < 0 || idx >= 5) return empty;
    if (!g_games_valid[idx]) {
        g_games_cache[idx] = scan_games(consoles()[idx]);
        g_games_valid[idx] = true;
    }
    return g_games_cache[idx];
}

static void invalidate_games(int idx)
{
    if (idx >= 0 && idx < 5) g_games_valid[idx] = false;
}

// ---------------------------------------------------------------------------
// drawing helpers
// ---------------------------------------------------------------------------
void fill_rect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(r, &rc);
}

void draw_tex(SDL_Renderer* r, SDL_Texture* t, SDL_Rect dst, Uint8 alpha = 255)
{
    if (!t) return;
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_RenderCopy(r, t, nullptr, &dst);
}

enum class Align { Left, Center, Right };

// text() runs many times per frame (clock, titles, labels) with mostly-static
// strings. Re-rendering the TTF surface and creating/destroying a texture on
// every call is costly on the Cortex-A55. Cache the rendered texture keyed by
// (renderer, font, color, string). Textures belong to a renderer, so the cache
// MUST be cleared before renderers are destroyed — see shutdown_gfx(). Evict
// all when full to bound memory (clock/battery strings churn once a minute).
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
std::map<TextKey, SDL_Texture*> g_text_cache;

void clear_text_cache()
{
    for (auto& kv : g_text_cache) SDL_DestroyTexture(kv.second);
    g_text_cache.clear();
}

void text(SDL_Renderer* r, TTF_Font* f, const std::string& s,
          int x, int y, SDL_Color c, Align align = Align::Left)
{
    if (!f || s.empty()) return;
    TextKey key{ r, f, c, s };
    SDL_Texture* t;
    int tw, th;
    auto it = g_text_cache.find(key);
    if (it != g_text_cache.end()) {
        t = it->second;
        SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
    } else {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
        if (!surf) return;
        t = SDL_CreateTextureFromSurface(r, surf);
        tw = surf->w; th = surf->h;
        SDL_FreeSurface(surf);
        if (!t) return;
        if (g_text_cache.size() >= 128) clear_text_cache();
        g_text_cache.emplace(key, t);
    }
    int dx = x;
    if (align == Align::Center) dx = x - tw / 2;
    else if (align == Align::Right) dx = x - tw;
    SDL_Rect dst = { dx, y, tw, th };
    SDL_RenderCopy(r, t, nullptr, &dst);
}

int text_width(TTF_Font* f, const std::string& s)
{
    int w = 0, h = 0;
    if (f) TTF_SizeUTF8(f, s.c_str(), &w, &h);
    return w;
}

// word-wrap CJK/latin mixed text to a pixel width
std::vector<std::string> wrap(TTF_Font* f, const std::string& s, int maxw)
{
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\n') { lines.push_back(cur); cur.clear(); i++; continue; }
        // take one UTF-8 codepoint
        size_t len = 1;
        unsigned char ch = (unsigned char)s[i];
        if (ch >= 0xF0) len = 4; else if (ch >= 0xE0) len = 3;
        else if (ch >= 0xC0) len = 2;
        std::string piece = s.substr(i, len);
        if (text_width(f, cur + piece) > maxw && !cur.empty()) {
            lines.push_back(cur);
            cur.clear();
        }
        cur += piece;
        i += len;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// Shorten a label to a pixel width, marking the cut with an ellipsis. Used for
// game rows so a long title cannot run into the TF2 badge or the cover panel.
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

// Play time as a short localised string: "2 小时 5 分" / "12 分钟" / "40 秒".
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

// "11-19 14:32" — numeric on purpose, so it needs no translation and stays
// the same width in every language.
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

int console_index(const std::string& id)
{
    for (int i = 0; i < 5; i++)
        if (id == consoles()[i].id) return i;
    return 5;   // unknown system: sorts last, never launches
}

// Short badge for a stored system id. A log entry can outlive the console list
// (or come from a card that is no longer inserted), so never index blindly.
std::string console_badge(const std::string& id)
{
    int i = console_index(id);
    return (i < 5) ? consoles()[i].name_en : "?";
}

// ---------------------------------------------------------------------------
// graphics init / teardown (also used around emulator launches)
// ---------------------------------------------------------------------------
SDL_Texture* load_tex(SDL_Renderer* r, const std::string& path)
{
    SDL_Texture* t = IMG_LoadTexture(r, path.c_str());
    return t; // nullptr is fine — fallbacks are drawn
}

// Tiny persistent debug log: also kept on the data partition so input
// problems can be diagnosed by reading the card on a PC after a reboot.
void ui_dbg(const char* s)
{
    fputs(s, stderr);
    FILE* f = fopen("/userdata/system/ui.log", "a");
    if (!f) f = fopen("/boot/ui.log", "a");
    if (f) { fputs(s, f); fclose(f); }
}

void open_controller()
{
    char dbg[512];
    int n = SDL_NumJoysticks();
    snprintf(dbg, sizeof(dbg), "gameos-ui: open_controller: %d joystick(s)\n", n);
    ui_dbg(dbg);
    for (int i = 0; i < n; i++) {
        const char* name = SDL_JoystickNameForIndex(i);
        if (!SDL_IsGameController(i)) {
            // Register our virtual gamepad with SDL on the fly.
            if (name && strcmp(name, "gameOS Gamepad") == 0) {
                char guid[64];
                SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),
                                          guid, sizeof(guid));
                // SDL evdev order: BTN_JOYSTICK..KEY_MAX first, then 0..BTN_JOYSTICK,
                // so BTN_BACK(0x116) lands LAST (b17); sticks/dpad are b11..b16.
                std::string map = std::string(guid) +
                    ",gameOS Gamepad,platform:Linux,"
                    "a:b0,b:b1,x:b3,y:b2,"
                    "back:b17,start:b9,guide:b10,"
                    "leftshoulder:b4,rightshoulder:b5,"
                    "lefttrigger:b6,righttrigger:b7,"
                    "leftstick:b11,rightstick:b12,"
                    "dpup:b13,dpdown:b14,dpleft:b15,dpright:b16,"
                    "leftx:a0,lefty:a1,";
                int mr = SDL_GameControllerAddMapping(map.c_str());
                snprintf(dbg, sizeof(dbg), "gameos-ui: mapping %s -> %d (%s)\n",
                         guid, mr, mr < 0 ? SDL_GetError() : "ok");
                ui_dbg(dbg);
            }
        }
        snprintf(dbg, sizeof(dbg), "gameos-ui: joy %d name=%s isgc=%d\n",
                 i, name ? name : "?", (int)SDL_IsGameController(i));
        ui_dbg(dbg);
        if (SDL_IsGameController(i)) {
            app.pad = SDL_GameControllerOpen(i);
            snprintf(dbg, sizeof(dbg), "gameos-ui: controller open %s (%s)\n",
                     app.pad ? "ok" : "FAILED", app.pad ? "pad" : SDL_GetError());
            ui_dbg(dbg);
            if (app.pad) break;
        }
    }
}

// (Re)load the text fonts for the active language. Korean needs its own
// font (Noto Sans SC has no Hangul); every other language is covered by
// Noto Sans SC, including traditional Chinese, kana and Cyrillic.
void reload_fonts()
{
    clear_text_cache();   // cached textures reference the fonts closed below
    std::string fonts = fonts_dir();
    std::string noto = fonts +
        (g_lang == LANG_KO ? "/NotoSansKR-Regular.ttf" : "/NotoSansSC-Regular.otf");
    for (TTF_Font** f : { &app.f_title, &app.f_body, &app.f_small }) {
        if (*f) TTF_CloseFont(*f);
        *f = nullptr;
    }
    app.f_title = TTF_OpenFont(noto.c_str(), 40);
    app.f_body  = TTF_OpenFont(noto.c_str(), 30);
    app.f_small = TTF_OpenFont(noto.c_str(), 22);
    if (!app.f_title) {   // missing KR font must not leave the UI fontless
        noto = fonts + "/NotoSansSC-Regular.otf";
        app.f_title = TTF_OpenFont(noto.c_str(), 40);
        app.f_body  = TTF_OpenFont(noto.c_str(), 30);
        app.f_small = TTF_OpenFont(noto.c_str(), 22);
    }
}

void apply_theme(int idx)
{
    ThemePal p = theme_palette(idx);
    C_TEXT   = { p.text.r,   p.text.g,   p.text.b,   255 };
    C_DIM    = { p.dim.r,    p.dim.g,    p.dim.b,    255 };
    C_ACCENT = { p.accent.r, p.accent.g, p.accent.b, 255 };
    C_PANEL  = { p.panel.r,  p.panel.g,  p.panel.b,  p.panel_a };
    C_BG     = { p.bg.r,     p.bg.g,     p.bg.b,     255 };
    C_TRACK  = { p.track.r,  p.track.g,  p.track.b,  255 };
    g_accent = C_ACCENT;
    g_tint_top = p.tint_top;
    g_tint_bot = p.tint_bot;
}

// Whole-set theme assets: background, ambient, console icons, hub icons and
// logo all come from themes/<key>/ so a switch never mixes styles.
void load_theme_assets()
{
    const std::string dir = assets_dir() + "/themes/" + theme_key(app.settings.theme);
    if (app.bg)      { SDL_DestroyTexture(app.bg);      app.bg = nullptr; }
    if (app.ambient) { SDL_DestroyTexture(app.ambient); app.ambient = nullptr; }
    if (app.logo)    { SDL_DestroyTexture(app.logo);    app.logo = nullptr; }
    for (auto& t : app.icons) { if (t) SDL_DestroyTexture(t); t = nullptr; }
    if (app.icon_settings) { SDL_DestroyTexture(app.icon_settings); app.icon_settings = nullptr; }
    if (app.icon_activity) { SDL_DestroyTexture(app.icon_activity); app.icon_activity = nullptr; }
    if (app.icon_quick)    { SDL_DestroyTexture(app.icon_quick);    app.icon_quick = nullptr; }
    app.bg      = load_tex(app.ren_top, dir + "/home_bg.png");
    app.ambient = app.ren_bot ? load_tex(app.ren_bot, dir + "/ambient_bg.png") : nullptr;
    app.logo    = load_tex(app.ren_top, dir + "/logo.png");
    const char* ids[] = { "nds", "gba", "gbc", "ps1", "psp" };
    for (int i = 0; i < 5; i++)
        app.icons[i] = load_tex(app.ren_top, dir + "/console_" + ids[i] + ".png");
    app.icon_settings = load_tex(app.ren_top, dir + "/icon_settings.png");
    app.icon_activity = load_tex(app.ren_top, dir + "/icon_activity.png");
    app.icon_quick    = load_tex(app.ren_top, dir + "/icon_quick.png");
}

// DSI-2 is the physical top (lid) panel on both RG DS models; SDL enumerates
// DSI-1 first, so pick the display by connector name instead of index.
static int top_display()
{
    int n = SDL_GetNumVideoDisplays();
    for (int i = 0; i < n; i++) {
        const char *name = SDL_GetDisplayName(i);
        if (name && strstr(name, "DSI-2")) return i;
    }
    return 0;
}

bool init_gfx()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "gameos-ui: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "gameos-ui: TTF_Init: %s\n", TTF_GetError());
        return false;
    }
    IMG_Init(IMG_INIT_PNG);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_ShowCursor(SDL_DISABLE);

    std::string fonts = fonts_dir();
    std::string orbi = fonts + "/Orbitron.ttf";

    app.f_logo  = TTF_OpenFont(orbi.c_str(), 52);
    app.f_clock = TTF_OpenFont(orbi.c_str(), 96);
    reload_fonts();
    if (!app.f_logo)  app.f_logo  = TTF_OpenFont((fonts_dir() + "/NotoSansSC-Regular.otf").c_str(), 48);
    if (!app.f_clock) app.f_clock = TTF_OpenFont((fonts_dir() + "/NotoSansSC-Regular.otf").c_str(), 90);
    if (!app.f_title) {
        fprintf(stderr, "gameos-ui: cannot open text fonts in %s\n", fonts.c_str());
        return false;
    }

    int ndisp = SDL_GetNumVideoDisplays();
    int dtop = top_display();
    int dbot = (ndisp > 1) ? (dtop == 0 ? 1 : 0) : -1;
    app.win_top = SDL_CreateWindow("gameOS",
        SDL_WINDOWPOS_CENTERED_DISPLAY(dtop), SDL_WINDOWPOS_CENTERED_DISPLAY(dtop),
        W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!app.win_top) {
        fprintf(stderr, "gameos-ui: window: %s\n", SDL_GetError());
        return false;
    }
    app.ren_top = SDL_CreateRenderer(app.win_top, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!app.ren_top)
        app.ren_top = SDL_CreateRenderer(app.win_top, -1, SDL_RENDERER_SOFTWARE);
    if (!app.ren_top) {
        // Both accelerated and software renderers failed. Bail out instead of
        // returning true with a null renderer, which would crash the draw loop
        // (SDL_SetRenderDrawColor/RenderCopy on nullptr) every frame.
        fprintf(stderr, "gameos-ui: cannot create renderer: %s\n", SDL_GetError());
        return false;
    }
    // Design space stays 1024x768; on the 640x480 RG DS panels SDL scales it.
    SDL_RenderSetLogicalSize(app.ren_top, W, H);

    if (dbot >= 0) {
        app.win_bot = SDL_CreateWindow("gameOS ambient",
            SDL_WINDOWPOS_CENTERED_DISPLAY(dbot), SDL_WINDOWPOS_CENTERED_DISPLAY(dbot),
            W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (app.win_bot) {
            app.ren_bot = SDL_CreateRenderer(app.win_bot, -1,
                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!app.ren_bot) {
                SDL_DestroyWindow(app.win_bot);
                app.win_bot = nullptr;
            } else {
                SDL_RenderSetLogicalSize(app.ren_bot, W, H);
            }
        }
    }

    load_theme_assets();

    open_controller();
    sound_init("/usr/share/gameos/sounds");   // silent fallback if audio fails
    return true;
}

void clear_cover_cache();   // defined with the cover helpers below

void shutdown_gfx()
{
    sound_quit();          // release ALSA before the emulator takes over
    clear_text_cache();  // text textures belong to the renderers destroyed below
    clear_cover_cache();
    if (app.pad) { SDL_GameControllerClose(app.pad); app.pad = nullptr; }
    for (auto& t : app.icons) { if (t) SDL_DestroyTexture(t); t = nullptr; }
    if (app.icon_settings) { SDL_DestroyTexture(app.icon_settings); app.icon_settings = nullptr; }
    if (app.icon_activity) { SDL_DestroyTexture(app.icon_activity); app.icon_activity = nullptr; }
    if (app.icon_quick)    { SDL_DestroyTexture(app.icon_quick);    app.icon_quick = nullptr; }
    if (app.bg)      { SDL_DestroyTexture(app.bg); app.bg = nullptr; }
    if (app.ambient) { SDL_DestroyTexture(app.ambient); app.ambient = nullptr; }
    if (app.logo)    { SDL_DestroyTexture(app.logo); app.logo = nullptr; }
    for (TTF_Font* f : { app.f_logo, app.f_title, app.f_body, app.f_small, app.f_clock })
        if (f) TTF_CloseFont(f);
    app.f_logo = app.f_title = app.f_body = app.f_small = app.f_clock = nullptr;
    if (app.ren_bot) { SDL_DestroyRenderer(app.ren_bot); app.ren_bot = nullptr; }
    if (app.win_bot) { SDL_DestroyWindow(app.win_bot); app.win_bot = nullptr; }
    if (app.ren_top) { SDL_DestroyRenderer(app.ren_top); app.ren_top = nullptr; }
    if (app.win_top) { SDL_DestroyWindow(app.win_top); app.win_top = nullptr; }
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}

// ---------------------------------------------------------------------------
// top-bar (logo, battery, clock)
// ---------------------------------------------------------------------------
void draw_topbar(SDL_Renderer* r)
{
    if (app.logo) {
        int tw = 0, th = 0;
        SDL_QueryTexture(app.logo, nullptr, nullptr, &tw, &th);
        if (th > 0) {
            SDL_Rect dst = { 32, 16, tw * 44 / th, 44 };
            draw_tex(r, app.logo, dst);
        }
    } else {
        text(r, app.f_logo, "gameOS", 32, 20, C_ACCENT);
    }

    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    std::string hhmm = two(tmv.tm_hour) + ":" + two(tmv.tm_min);
    text(r, app.f_body, hhmm, W - 32, 34, C_DIM, Align::Right);

    int pct = battery_percent();
    if (pct >= 0) {
        int bx = W - 190, by = 36;
        SDL_Color bc = pct > 40 ? C_GREEN : (pct > 15 ? C_AMBER : C_RED);
        fill_rect(r, bx, by, 48, 22, { 0, 0, 0, 0 });
        SDL_SetRenderDrawColor(r, C_DIM.r, C_DIM.g, C_DIM.b, 255);
        SDL_Rect outline = { bx, by, 48, 22 };
        SDL_RenderDrawRect(r, &outline);
        SDL_Rect tip = { bx + 48, by + 6, 4, 10 };
        SDL_RenderFillRect(r, &tip);
        fill_rect(r, bx + 3, by + 3, 42 * pct / 100, 16, bc);
        char b[16];
        snprintf(b, sizeof(b), "%d%%", pct);
        text(r, app.f_small, b, bx - 12, by - 1, C_DIM, Align::Right);
        if (battery_charging())
            text(r, app.f_small, "+", bx + 58, by - 1, C_GREEN);
    }
}

void draw_hint(SDL_Renderer* r, const std::string& s)
{
    fill_rect(r, 0, H - 56, W, 56, C_PANEL);
    text(r, app.f_small, s, W / 2, H - 42, C_DIM, Align::Center);
}

// Bounded box-art cache (covers belong to a renderer; cleared on shutdown).
std::map<std::string, SDL_Texture*> g_cover_cache;
void clear_cover_cache()
{
    for (auto& kv : g_cover_cache) SDL_DestroyTexture(kv.second);
    g_cover_cache.clear();
}
SDL_Texture* cover_tex(SDL_Renderer* r, const std::string& path)
{
    if (path.empty()) return nullptr;
    auto it = g_cover_cache.find(path);
    if (it != g_cover_cache.end()) return it->second;
    SDL_Texture* t = load_tex(r, path);
    if (!t) return nullptr;
    if (g_cover_cache.size() >= 24) clear_cover_cache();
    g_cover_cache.emplace(path, t);
    return t;
}

void draw_background(SDL_Renderer* r)
{
    if (app.bg) {
        SDL_Rect dst = { 0, 0, W, H };
        draw_tex(r, app.bg, dst);
    } else {
        fill_rect(r, 0, 0, W, H, C_BG);
        fill_rect(r, 0, 0, W, 6, g_accent);
    }
    // subtle theme tint over the top panel
    fill_rect(r, 0, 0, W, H, { g_accent.r, g_accent.g, g_accent.b, (Uint8)g_tint_top });
}

// ---------------------------------------------------------------------------
// screens
// ---------------------------------------------------------------------------
void draw_home(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    const auto& cons = consoles();
    int total = CAR_N;

    for (int i = 0; i < total; i++) {
        int off = i - app.carousel;
        if (std::abs(off) > 2) continue;
        float scale = (off == 0) ? 1.0f : 0.68f;
        int size = (int)(250 * scale);
        int cx = W / 2 + off * 270;
        int cy = 330;
        Uint8 alpha = (off == 0) ? 255 : 130;

        SDL_Texture* icon = (i < 5) ? app.icons[i]
                          : (i == CAR_ACTIVITY) ? app.icon_activity
                          : (i == CAR_QUICK)    ? app.icon_quick
                                                : app.icon_settings;
        const char* badge = (i < 5) ? cons[i].name_en
                          : (i == CAR_ACTIVITY) ? "ACT"
                          : (i == CAR_QUICK)    ? "FAV" : "SET";
        if (icon) {
            SDL_Rect dst = { cx - size / 2, cy - size / 2, size, size };
            draw_tex(r, icon, dst, alpha);
        } else {
            fill_rect(r, cx - size / 2, cy - size / 2, size, size, C_PANEL);
            text(r, app.f_title, badge, cx, cy - 20, C_ACCENT, Align::Center);
        }
    }

    // selection ring / glow under the active icon
    fill_rect(r, W / 2 - 130, 470, 260, 4, C_ACCENT);

    std::string name, sub;
    if (app.carousel < 5) {
        const Console& c = cons[app.carousel];
        name = console_name(c.id);
        const auto& games = games_for(app.carousel);
        char b[128];
        snprintf(b, sizeof(b), tr(T_GAMES_FMT), c.name_en, games.size());
        sub = b;
    } else if (app.carousel == CAR_ACTIVITY) {
        name = tr(T_ACTIVITY);
        sub = tr(T_ACTIVITY_SUB);
    } else if (app.carousel == CAR_QUICK) {
        name = tr(T_QUICK);
        sub = tr(T_QUICK_SUB);
    } else {
        name = tr(T_SETTINGS);
        sub = tr(T_SETTINGS_SUB);
    }
    text(r, app.f_title, name, W / 2, 500, C_TEXT, Align::Center);
    text(r, app.f_small, sub, W / 2, 556, C_DIM, Align::Center);

    // carousel dots
    for (int i = 0; i < total; i++) {
        SDL_Color dc = (i == app.carousel) ? C_ACCENT : C_DIM;
        fill_rect(r, W / 2 - total * 12 + i * 24, 610, 10, 10, dc);
    }

    draw_hint(r, tr(T_HOME_HINT));
}

void draw_list(SDL_Renderer* r)
{
    draw_background(r);
    const Console& c = consoles()[app.carousel];

    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, console_name(c.id), 32, 26, C_TEXT);
    char b[96];
    snprintf(b, sizeof(b), tr(T_LIST_COUNT_FMT), app.games.size());
    std::string cnt = b;
    if (app.fav_only) cnt += tr(T_FAV_SUFFIX);
    if (app.tf2_mounted) cnt += tr(T_TF2_ON);
    text(r, app.f_body, cnt, W - 32, 34, C_DIM, Align::Right);
    fill_rect(r, 0, 96, W, 3, g_accent);

    if (app.games.empty()) {
        text(r, app.f_body, app.fav_only ? tr(T_NO_FAV) : tr(T_NO_GAMES),
             W / 2, 300, C_TEXT, Align::Center);
        char p[256];
        if (app.fav_only) {
            snprintf(p, sizeof(p), "%s", tr(T_FAV_HELP));
        } else {
            snprintf(p, sizeof(p), tr(T_COPY_ROMS_FMT), c.id);
        }
        text(r, app.f_small, p, W / 2, 360, C_DIM, Align::Center);
        if (!app.fav_only)
            text(r, app.f_small, tr(T_TF_HELP),
                 W / 2, 400, C_DIM, Align::Center);
    } else {
        const int rowh = 66, top = 116, vis = 9;
        const int listw = W - 360;   // leave room for the cover panel
        if (app.sel < app.scroll) app.scroll = app.sel;
        if (app.sel >= app.scroll + vis) app.scroll = app.sel - vis + 1;
        for (int i = 0; i < vis; i++) {
            int idx = app.scroll + i;
            if (idx >= (int)app.games.size()) break;
            int y = top + i * rowh;
            if (idx == app.sel) {
                fill_rect(r, 24, y, listw - 48, rowh - 6, C_PANEL);
                fill_rect(r, 24, y, 5, rowh - 6, g_accent);
            }
            SDL_Color tc = (idx == app.sel) ? C_TEXT : C_DIM;
            const Game& g = app.games[idx];
            std::string label = g.title;
            if (fav_contains(c.id, g.title)) label = "★ " + label;
            // Games from the second card carry a TF2 badge; shorten the title
            // so the two never overlap.
            bool tf2 = on_tf2(g.path);
            label = ellipsize(app.f_body, label, listw - 96 - (tf2 ? 76 : 0));
            text(r, app.f_body, label, 48, y + 14, tc);
            if (tf2)
                text(r, app.f_small, "TF2", listw - 40, y + 18, C_DIM, Align::Right);
        }
        // scrollbar
        if ((int)app.games.size() > vis) {
            int trackh = vis * rowh;
            int barh = std::max(30, trackh * vis / (int)app.games.size());
            int bary = top + (trackh - barh) * app.scroll /
                       std::max(1, (int)app.games.size() - vis);
            fill_rect(r, listw - 14, top, 4, trackh, { C_DIM.r, C_DIM.g, C_DIM.b, 90 });
            fill_rect(r, listw - 14, bary, 4, barh, g_accent);
        }
        // cover preview for the selected game
        const Game& g = app.games[app.sel];
        int px = W - 320, py = 140, pw = 280, ph = 280;
        fill_rect(r, px - 8, py - 8, pw + 16, ph + 16, C_PANEL);
        SDL_Texture* cv = cover_tex(r, cover_path(c.id, g.title));
        if (cv) {
            SDL_Rect dst = { px, py, pw, ph };
            draw_tex(r, cv, dst);
        } else if (app.icons[app.carousel]) {
            SDL_Rect dst = { px + 40, py + 40, pw - 80, ph - 80 };
            draw_tex(r, app.icons[app.carousel], dst, 160);
        }
        text(r, app.f_small, tr(T_COVER), px + pw / 2, py + ph + 20, C_DIM, Align::Center);
    }
    draw_hint(r, tr(T_LIST_HINT));
}

// ---------------------------------------------------------------------------
// Bluetooth: gameos-bt ops run in the background; results arrive as files
// (/run/gameos/bt-op.out + bt-op.rc) polled from the main loop, so a 40 s
// pairing never freezes the UI.
// ---------------------------------------------------------------------------
bool bt_mac_valid(const std::string& m)
{
    if (m.size() != 17) return false;
    for (int i = 0; i < 17; i++) {
        char c = m[i];
        if (i % 3 == 2) { if (c != ':') return false; }
        else if (!isxdigit((unsigned char)c)) return false;
    }
    return true;
}

void bt_start(const std::string& op, const std::string& arg = "")
{
    unlink("/run/gameos/bt-op.rc");
    std::string cmd = "mkdir -p /run/gameos; gameos-bt " + op;
    if (!arg.empty()) cmd += " " + arg;
    cmd += " > /run/gameos/bt-op.out 2>&1; echo $? > /run/gameos/bt-op.rc &";
    if (system(cmd.c_str()) != 0) { /* ignore */ }
    app.bt_op = op;
    app.bt_busy = true;
    app.bt_op_at = SDL_GetTicks();
}

void bt_parse_devs(const std::string& out)
{
    app.bt_devs.clear();
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        // MAC \t paired \t connected \t kind \t name
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        size_t t3 = line.find('\t', t2 + 1);
        if (t3 == std::string::npos) continue;
        size_t t4 = line.find('\t', t3 + 1);
        if (t4 == std::string::npos) continue;
        BtDev d;
        d.mac = line.substr(0, t1);
        if (!bt_mac_valid(d.mac)) continue;
        d.paired = line[t1 + 1] == '1';
        d.connected = line[t2 + 1] == '1';
        d.kind = line.substr(t3 + 1, t4 - t3 - 1);
        d.name = line.substr(t4 + 1);
        if (!d.name.empty() && d.name.back() == '\r') d.name.pop_back();
        app.bt_devs.push_back(d);
    }
    app.bt_sel = std::clamp(app.bt_sel, 0, (int)app.bt_devs.size());
}

void bt_poll()
{
    if (!app.bt_busy) return;
    Uint32 now = SDL_GetTicks();
    if (now - app.bt_op_at > 90000) {   // a stuck op must not lock the screen
        app.bt_busy = false;
        app.bt_op.clear();
        app.bt_status = tr(T_BT_TIMEOUT);
        return;
    }
    if (now - app.bt_poll_at < 150) return;
    app.bt_poll_at = now;
    FILE* f = fopen("/run/gameos/bt-op.rc", "r");
    if (!f) return;
    int rc = 1;
    if (fscanf(f, "%d", &rc) != 1) rc = 1;
    fclose(f);
    app.bt_busy = false;
    std::string out = read_file("/run/gameos/bt-op.out");
    std::string op = app.bt_op;
    app.bt_op.clear();
    if (op == "devices" || op == "scan") {
        bt_parse_devs(out);
        if (op == "scan") app.bt_status = tr(T_SCAN_DONE);
    } else if (op == "up") {
        app.bt_on = (rc == 0);
        app.settings.bluetooth = app.bt_on ? 1 : 0;
        save_settings(app.settings);
        app.bt_status = app.bt_on ? tr(T_BT_ON_OK) : tr(T_BT_ON_FAIL);
        if (app.bt_on) bt_start("devices");
    } else if (op == "down") {
        app.bt_on = false;
        app.settings.bluetooth = 0;
        save_settings(app.settings);
        app.bt_devs.clear();
        app.bt_sel = 0;
        app.bt_status = tr(T_BT_OFF_OK);
    } else {
        // pair / connect / disconnect / forget: last line "OK" or "ERROR <why>"
        std::string last;
        std::istringstream ss(out);
        std::string line;
        while (std::getline(ss, line))
            if (!line.empty() && line != "\r") last = line;
        if (rc == 0) {
            if (op == "pair") app.bt_status = tr(T_PAIR_OK);
            else if (op == "connect") app.bt_status = tr(T_CONNECTED);
            else if (op == "disconnect") app.bt_status = tr(T_DISCONNECTED);
            else app.bt_status = tr(T_FORGOT);
        } else {
            app.bt_status = last.empty() ? tr(T_OP_FAIL) : last;
        }
        bt_start("devices");
    }
}

void bt_enter()
{
    app.bt_on = (system("gameos-bt status 2>/dev/null | grep -q '^on$'") == 0);
    app.bt_sel = 0;
    app.bt_scroll = 0;
    app.bt_status.clear();
    app.bt_devs.clear();
    if (app.bt_on && !app.bt_busy) bt_start("devices");
    app.screen = Screen::Bluetooth;
}

// ---------------------------------------------------------------------------
// WiFi: gameos-wifi ops run in the background, polled like the Bluetooth ones.
// The helper owns wpa_supplicant/udhcpc; the UI only issues short commands and
// reads the status file, so scanning and association never stall rendering.
// ---------------------------------------------------------------------------
std::string run_cmd(const char* cmd)
{
    std::string out;
    FILE* f = popen(cmd, "r");
    if (!f) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, f)) out += buf;
    pclose(f);
    return out;
}

bool wifi_saved(const std::string& ssid)
{
    return std::find(app.wifi_known.begin(), app.wifi_known.end(), ssid)
           != app.wifi_known.end();
}

void wifi_load_known()
{
    app.wifi_known.clear();
    std::istringstream ss(run_cmd("gameos-wifi known 2>/dev/null"));
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) app.wifi_known.push_back(line);
    }
    for (auto& n : app.wifi_nets) n.saved = wifi_saved(n.ssid);
}

void wifi_refresh_status()
{
    std::string out = run_cmd("gameos-wifi status 2>/dev/null");
    std::istringstream ss(out);
    std::string line;
    app.wifi_state.clear();
    app.wifi_link.clear();
    app.wifi_ip.clear();
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "state") app.wifi_state = v;
        else if (k == "ssid") app.wifi_link = v;
        else if (k == "ip") app.wifi_ip = v;
    }
    for (auto& n : app.wifi_nets) n.saved = wifi_saved(n.ssid);
}

void wifi_start(const std::string& op)
{
    unlink("/run/gameos/wifi-op.rc");
    std::string cmd = "mkdir -p /run/gameos; gameos-wifi " + op +
                      " > /run/gameos/wifi-op.out 2>&1; echo $? > /run/gameos/wifi-op.rc &";
    if (system(cmd.c_str()) != 0) { /* ignore */ }
    app.wifi_op = op;
    app.wifi_busy = true;
    app.wifi_op_at = SDL_GetTicks();
}

void wifi_parse_nets(const std::string& out)
{
    app.wifi_nets.clear();
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        WifiNet n;
        n.signal = atoi(line.substr(0, t1).c_str());
        n.secure = line[t1 + 1] == '1';
        n.ssid = line.substr(t2 + 1);
        if (n.ssid.empty()) continue;
        n.saved = wifi_saved(n.ssid);
        bool dup = false;
        for (auto& e : app.wifi_nets)
            if (e.ssid == n.ssid) { dup = true; if (n.signal > e.signal) e = n; break; }
        if (!dup) app.wifi_nets.push_back(n);
    }
    std::sort(app.wifi_nets.begin(), app.wifi_nets.end(),
              [](const WifiNet& a, const WifiNet& b) { return a.signal > b.signal; });
    app.wifi_sel = std::clamp(app.wifi_sel, 0,
                              std::max(0, (int)app.wifi_nets.size() - 1));
}

void wifi_begin_connect(const std::string& ssid, const std::string& pw)
{
    app.wifi_target = ssid;
    write_file("/run/gameos/wifi-arg", ssid + "\n" + pw + "\n");
    app.wifi_status = tr(T_WIFI_CONNECTING);
    wifi_start("connect");
}

void wifi_press_net()
{
    if (app.wifi_sel < 0 || app.wifi_sel >= (int)app.wifi_nets.size()) return;
    const WifiNet& n = app.wifi_nets[app.wifi_sel];
    if (n.secure && !n.saved) {          // ask for the passphrase
        app.wifi_target = n.ssid;
        app.wifi_pw.clear();
        app.wifi_kb = true;
        app.kb_r = 1; app.kb_c = 0; app.kb_shift = 0;
    } else {
        wifi_begin_connect(n.ssid, "");  // open network or saved key
    }
}

void wifi_forget_net()
{
    if (app.wifi_sel < 0 || app.wifi_sel >= (int)app.wifi_nets.size()) return;
    const WifiNet& n = app.wifi_nets[app.wifi_sel];
    if (!n.saved) return;
    write_file("/run/gameos/wifi-arg", n.ssid + "\n");
    wifi_start("forget");
}

const std::vector<std::vector<std::string>>& kb_rows()
{
    static const std::vector<std::vector<std::string>> rows = {
        { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
        { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
        { "a", "s", "d", "f", "g", "h", "j", "k", "l", "-" },
        { "z", "x", "c", "v", "b", "n", "m", "_", ".", "@" },
        { "ABC", " ", "DEL", "OK", "X" },
    };
    return rows;
}

void setup_kb_done(std::string* target);   // defined with the wizard below

void kb_close()
{
    app.wifi_kb = false;
    app.kb_generic = false;
    app.kb_buf = nullptr;
}

void kb_cancel()
{
    if (app.kb_generic && app.kb_buf) *app.kb_buf = app.kb_orig;
    kb_close();
}

// Point the shared keyboard at a wizard field. buf must outlive the keyboard
// (all callers pass an App member).
void kb_open(std::string& buf, const std::string& title, int limit,
             bool empty_ok, bool mask)
{
    app.kb_generic = true;
    app.kb_buf = &buf;
    app.kb_orig = buf;
    app.kb_title = title;
    app.kb_limit = limit;
    app.kb_empty_ok = empty_ok;
    app.kb_mask = mask;
    app.wifi_kb = true;
    app.kb_r = 1; app.kb_c = 0; app.kb_shift = 0;
}

void kb_press()
{
    const auto& rows = kb_rows();
    if (app.kb_r < 0 || app.kb_r >= (int)rows.size()) return;
    const auto& row = rows[app.kb_r];
    if (app.kb_c < 0 || app.kb_c >= (int)row.size()) return;
    const std::string k = row[app.kb_c];
    std::string& buf = app.kb_generic ? *app.kb_buf : app.wifi_pw;
    const int limit = app.kb_generic ? app.kb_limit : 63;
    if (k == "ABC") { app.kb_shift ^= 1; return; }
    if (k == "DEL") { if (!buf.empty()) buf.pop_back(); return; }
    if (k == " ")   { if ((int)buf.size() < limit) buf += " "; return; }
    if (k == "X")   { kb_cancel(); return; }
    if (k == "OK") {
        // a WPA key cannot be empty; wizard fields opt in via kb_empty_ok
        if (buf.empty() && !(app.kb_generic && app.kb_empty_ok)) return;
        if (app.kb_generic) {
            std::string* target = app.kb_buf;
            kb_close();
            setup_kb_done(target);
            return;
        }
        wifi_begin_connect(app.wifi_target, app.wifi_pw);
        kb_close();
        return;
    }
    std::string ch = k;
    if (app.kb_shift && ch.size() == 1 && ch[0] >= 'a' && ch[0] <= 'z')
        ch[0] = (char)(ch[0] - 'a' + 'A');
    if ((int)buf.size() < limit) buf += ch;
}

void wifi_poll()
{
    Uint32 now = SDL_GetTicks();
    // live link status while the page is open (wpa_cli is cheap)
    if ((app.screen == Screen::Wifi ||
         (app.screen == Screen::Setup && app.setup_step == 1)) &&
        now - app.wifi_status_at > 2000) {
        app.wifi_status_at = now;
        wifi_refresh_status();
    }
    if (!app.wifi_busy) return;
    if (now - app.wifi_op_at > 60000) {   // scan/assoc timeout
        app.wifi_busy = false;
        app.wifi_op.clear();
        app.wifi_status = tr(T_OP_FAIL);
        return;
    }
    if (now - app.wifi_poll_at < 150) return;
    app.wifi_poll_at = now;
    FILE* f = fopen("/run/gameos/wifi-op.rc", "r");
    if (!f) return;
    int rc = 1;
    if (fscanf(f, "%d", &rc) != 1) rc = 1;
    fclose(f);
    app.wifi_busy = false;
    std::string out = read_file("/run/gameos/wifi-op.out");
    std::string op = app.wifi_op;
    app.wifi_op.clear();
    if (op == "scan") {
        wifi_parse_nets(out);
        app.wifi_status = app.wifi_nets.empty() ? tr(T_WIFI_NO_NETS) : "";
        wifi_load_known();
    } else if (op == "connect") {
        std::string last;
        std::istringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) last = line;
        }
        if (rc == 0 && last.rfind("OK", 0) == 0) {
            char b[96];
            std::string ip = last.size() > 3 ? last.substr(3) : "";
            snprintf(b, sizeof(b), tr(T_WIFI_CONNECTED_FMT), ip.c_str());
            app.wifi_status = b;
        } else {
            app.wifi_status = last.empty() ? tr(T_WIFI_CONNECT_FAIL) : last;
        }
        wifi_load_known();
        wifi_refresh_status();
    } else if (op == "forget") {
        app.wifi_status = tr(T_WIFI_FORGET_DONE);
        wifi_load_known();
        wifi_refresh_status();
        wifi_start("scan");
    }
}

void wifi_enter()
{
    app.wifi_status.clear();
    kb_close();
    app.wifi_pw.clear();
    app.wifi_target.clear();
    app.wifi_sel = 0;
    app.wifi_scroll = 0;
    app.wifi_nets.clear();
    wifi_refresh_status();
    wifi_load_known();
    app.screen = Screen::Wifi;
    if (app.wifi_state != "NOHW") {
        app.wifi_status = tr(T_WIFI_SCANNING);
        wifi_start("scan");
    }
}

// ---------------------------------------------------------------------------
// first-boot wizard
//
// Six questions then a done page. A (right button) acts on the highlighted row,
// B (bottom button) skips the current step, START skips the whole wizard. The
// marker on /userdata is what makes this a *first*-boot experience; Settings
// can run it again at any time.
// ---------------------------------------------------------------------------
constexpr int SETUP_STEPS = 7;                 // 6 questions + the done page
const char* SETUP_MARKER = "/userdata/system/setup-done";

struct TzRegion { TKey name; std::vector<const char*> zones; };

// Curated from the tzdata "default" zonelist, so every entry is really present
// in the image and gameos-setup's valid_zone() will accept it.
const std::vector<TzRegion>& tz_regions()
{
    static const std::vector<TzRegion> r = {
        { T_TZ_ASIA,    { "Asia/Shanghai", "Asia/Tokyo", "Asia/Seoul",
                          "Asia/Singapore", "Asia/Kolkata", "Asia/Dubai" } },
        { T_TZ_EUROPE,  { "Europe/London", "Europe/Berlin", "Europe/Madrid",
                          "Europe/Moscow" } },
        { T_TZ_AMERICA, { "America/New_York", "America/Chicago", "America/Denver",
                          "America/Los_Angeles", "America/Sao_Paulo" } },
        { T_TZ_AFRICA,  { "Africa/Cairo", "Africa/Johannesburg" } },
        { T_TZ_OCEANIA, { "Australia/Sydney", "Pacific/Auckland" } },
        { T_TZ_UTC,     { "UTC" } },
    };
    return r;
}

int tz_index_of(const std::string& z, int region)
{
    const auto& zs = tz_regions()[region].zones;
    for (int i = 0; i < (int)zs.size(); i++) if (z == zs[i]) return i;
    return 0;
}

bool userdata_mounted()
{
    return read_file("/proc/self/mounts").find(" /userdata ") != std::string::npos;
}

// Mirrors gameos-setup valid_hostname(). Rejecting everything outside
// [A-Za-z0-9-] here also means the value can be safely single-quoted into the
// shell command below.
bool valid_name_ui(const std::string& h)
{
    if (h.empty() || h.size() > 63) return false;
    if (h.front() == '-' || h.back() == '-') return false;
    for (char c : h)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

// Wall clock in another zone without leaving the process TZ changed: the
// top-bar clock calls localtime_r() every frame.
std::string zone_time(const std::string& zone)
{
    const char* old = getenv("TZ");
    std::string save = old ? old : "";
    setenv("TZ", zone.c_str(), 1);
    tzset();
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    char b[48];
    snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d",
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    if (old) setenv("TZ", save.c_str(), 1); else unsetenv("TZ");
    tzset();
    return b;
}

std::string last_line(const std::string& out)
{
    std::istringstream ss(out);
    std::string line, last;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) last = line;
    }
    return last;
}

int setup_rows()
{
    switch (app.setup_step) {
    case 0:  return (int)LANG_COUNT;
    case 1:  return (int)app.wifi_nets.size() + 2;   // networks + test + next
    case 2:  return app.setup_col == 0
                 ? (int)tz_regions().size()
                 : (int)tz_regions()[app.setup_region].zones.size();
    case 3:  return 2;                               // name + next
    case 4:  return 6;                               // user/pass/hardcore/login/logout/next
    case 5:  return 2;                               // start-stop + next
    default: return 1;
    }
}

// One result file pair PER OP, not one shared pair: skipping a step with B
// leaves its script running, and the next step's script would otherwise
// overwrite the same .out/.rc and have its result read as the wrong op's.
std::string setup_op_base(const std::string& op)
{
    return "/run/gameos/op-" + op;
}

void setup_run(const std::string& op, const std::string& cmd)
{
    const std::string base = setup_op_base(op);
    unlink((base + ".rc").c_str());
    std::string full = "mkdir -p /run/gameos; " + cmd +
        " > " + base + ".out 2>&1; echo $? > " + base + ".rc &";
    system(full.c_str());
    app.setup_op = op;
    app.setup_busy = true;
    app.setup_op_at = SDL_GetTicks();
}

void setup_finish()
{
    FILE* f = fopen(SETUP_MARKER, "w");
    if (f) { fputs("1\n", f); fclose(f); }
    kb_close();
    app.setup_busy = false;
    app.setup_op.clear();
    clear_cover_cache();
    for (int i = 0; i < (int)consoles().size(); i++) invalidate_games(i);
    app.screen = Screen::Home;
}

void setup_next()
{
    app.setup_step++;
    app.setup_sel = 0;
    app.setup_scroll = 0;
    app.setup_col = 0;
    app.setup_status.clear();
    // Skipping a step while its op is in flight: the script still runs to
    // completion and persists the choice, but its result must not land on the
    // next step (a timezone result would otherwise advance twice).
    app.setup_busy = false;
    app.setup_op.clear();
    if (app.setup_step >= SETUP_STEPS) { setup_finish(); return; }
    if (app.setup_step == 1) {
        wifi_refresh_status();
        wifi_load_known();
        if (app.wifi_state != "NOHW" && !app.wifi_busy) {
            app.wifi_status = tr(T_WIFI_SCANNING);
            wifi_start("scan");
        }
    } else if (app.setup_step == 5) {
        unlink("/run/gameos/covers-state");
        app.cov_state.clear();
        app.cov_current.clear();
        app.cov_total = app.cov_done = app.cov_ok = app.cov_miss = app.cov_fail = 0;
        setup_run("covers-count", "gameos-covers count");
    }
}

void setup_poll()
{
    if (app.screen != Screen::Setup) return;
    Uint32 now = SDL_GetTicks();
    if (now - app.setup_poll_at < 200) return;
    app.setup_poll_at = now;

    // A rescan can shrink the network list under the cursor, leaving setup_sel
    // past the last row where A would silently do nothing.
    int rows = setup_rows();
    if (rows > 0 && app.setup_sel >= rows) app.setup_sel = rows - 1;

    // cover progress is published continuously, not just at the end
    if (app.setup_step == 5) {
        std::istringstream ss(read_file("/run/gameos/covers-state"));
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if      (k == "state")   app.cov_state = v;
            else if (k == "current") app.cov_current = v;
            else if (k == "total")   app.cov_total = atoi(v.c_str());
            else if (k == "done")    app.cov_done = atoi(v.c_str());
            else if (k == "ok")      app.cov_ok = atoi(v.c_str());
            else if (k == "miss")    app.cov_miss = atoi(v.c_str());
            else if (k == "fail")    app.cov_fail = atoi(v.c_str());
        }
    }

    if (!app.setup_busy) return;
    // a cover run can legitimately take many minutes; the user stops it with A
    if (app.setup_op != "covers" && now - app.setup_op_at > 60000) {
        app.setup_busy = false;
        app.setup_op.clear();
        app.setup_status = tr(T_OP_FAIL);
        return;
    }
    const std::string base = setup_op_base(app.setup_op);
    FILE* f = fopen((base + ".rc").c_str(), "r");
    if (!f) return;
    int rc = 1;
    if (fscanf(f, "%d", &rc) != 1) rc = 1;
    fclose(f);
    app.setup_busy = false;
    std::string out = read_file(base + ".out");
    std::string op = app.setup_op;
    app.setup_op.clear();
    std::string last = last_line(out);
    char b[256];

    if (op == "timezone") {
        if (rc == 0 && last.rfind("OK", 0) == 0) {
            if (last.size() > 3) app.setup_tz = last.substr(3);
            tzset();                       // the top-bar clock follows at once
            setup_next();
        } else {
            app.setup_status = tr(T_OP_FAIL);
        }
    } else if (op == "hostname") {
        if (rc == 0 && last.rfind("OK", 0) == 0) {
            snprintf(b, sizeof(b), tr(T_NAME_APPLIED_FMT), app.setup_name.c_str());
            app.setup_status = b;
        } else {
            app.setup_status = tr(T_NAME_INVALID);
        }
    } else if (op == "ra-login") {
        if (rc == 0 && last.rfind("OK", 0) == 0) {
            app.setup_ra_logged = true;
            app.setup_ra_name = last.size() > 3 ? last.substr(3) : app.setup_ra_user;
            app.setup_ra_pass.clear();     // only the token is kept, on disk
            snprintf(b, sizeof(b), tr(T_RA_OK_FMT), app.setup_ra_name.c_str());
            app.setup_status = b;
        } else {
            std::string why = last.rfind("ERROR ", 0) == 0 ? last.substr(6) : last;
            if (why.empty()) why = "login-failed";
            snprintf(b, sizeof(b), tr(T_RA_FAIL_FMT), why.c_str());
            app.setup_status = b;
        }
    } else if (op == "ra-clear") {
        app.setup_ra_logged = false;
        app.setup_ra_name.clear();
        app.setup_status = tr(T_RA_LOGOUT);
    } else if (op == "wifi-test") {
        const char* keys[4] = { "link", "route", "dns", "net" };
        for (int i = 0; i < 4; i++) app.setup_test[i] = "wait";
        std::istringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            for (int i = 0; i < 4; i++) if (k == keys[i]) app.setup_test[i] = v;
        }
        app.setup_status = rc == 0 ? std::string() : std::string(tr(T_SETUP_NEED_NET));
    } else if (op == "covers-count") {
        app.cov_total = atoi(last.c_str());
    } else if (op == "covers") {
        clear_cover_cache();
        if (last.rfind("OK", 0) == 0 || last.rfind("STOPPED", 0) == 0) {
            snprintf(b, sizeof(b), tr(T_COVER_DONE_FMT), app.cov_ok, app.cov_miss);
            app.setup_status = b;
        } else {
            app.setup_status = tr(T_SETUP_NEED_NET);
        }
    }
}

void setup_activate()
{
    // the cover download stays stoppable while it is in flight
    if (app.setup_step == 5 && app.setup_sel == 0 &&
        app.setup_busy && app.setup_op == "covers") {
        FILE* f = fopen("/run/gameos/covers.stop", "w");
        if (f) fclose(f);
        return;
    }
    if (app.setup_busy || app.wifi_busy) return;
    int n = setup_rows();
    if (app.setup_sel < 0 || app.setup_sel >= n) return;

    switch (app.setup_step) {
    case 0:                                        // language
        app.settings.language = app.setup_sel;
        g_lang = (Lang)app.setup_sel;
        save_settings(app.settings);
        reload_fonts();                            // Korean uses another font
        setup_next();
        break;
    case 1: {                                      // wifi + connectivity test
        int nets = (int)app.wifi_nets.size();
        if (app.setup_sel < nets) {
            app.wifi_sel = app.setup_sel;
            wifi_press_net();
        } else if (app.setup_sel == nets) {
            for (int i = 0; i < 4; i++) app.setup_test[i] = "wait";
            app.setup_status = tr(T_WIFI_TESTING);
            setup_run("wifi-test", "gameos-wifi test");
        } else {
            setup_next();
        }
        break;
    }
    case 2:                                        // timezone
        if (app.setup_col == 0) {
            app.setup_region = app.setup_sel;
            app.setup_col = 1;
            app.setup_sel = tz_index_of(app.setup_tz, app.setup_region);
            app.setup_scroll = 0;
        } else {
            std::string z = tz_regions()[app.setup_region].zones[app.setup_sel];
            setup_run("timezone", "gameos-setup timezone '" + z + "'");
        }
        break;
    case 3:                                        // machine name
        if (app.setup_sel == 0)
            kb_open(app.setup_name, tr(T_SETUP_NAME_TITLE), 63, false, false);
        else
            setup_next();
        break;
    case 4:                                        // retroachievements
        if (app.setup_sel == 0) {
            kb_open(app.setup_ra_user, tr(T_RA_USER), 63, false, false);
        } else if (app.setup_sel == 1) {
            // empty is allowed: it re-authenticates with the stored token
            kb_open(app.setup_ra_pass, tr(T_RA_PASS), 128, true, true);
        } else if (app.setup_sel == 2) {
            app.setup_ra_hardcore = !app.setup_ra_hardcore;
        } else if (app.setup_sel == 3) {
            if (app.setup_ra_user.empty()) {
                kb_open(app.setup_ra_user, tr(T_RA_USER), 63, false, false);
                break;
            }
            // 0600 from creation, not fopen-then-chmod: the password must not
            // be world-readable even for the instant between the two calls.
            int fd = open("/run/gameos/setup-arg", O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) { app.setup_status = tr(T_OP_FAIL); break; }
            FILE* f = fdopen(fd, "w");
            if (!f) { close(fd); app.setup_status = tr(T_OP_FAIL); break; }
            fprintf(f, "%s\n%s\n%d\n", app.setup_ra_user.c_str(),
                    app.setup_ra_pass.c_str(), app.setup_ra_hardcore ? 1 : 0);
            fclose(f);
            app.setup_status = tr(T_RA_LOGGING_IN);
            setup_run("ra-login", "gameos-setup ra-login");
        } else if (app.setup_sel == 4) {
            if (app.setup_ra_logged) setup_run("ra-clear", "gameos-setup ra-clear");
        } else {
            setup_next();
        }
        break;
    case 5:                                        // covers
        if (app.setup_sel == 0) {
            app.setup_status = tr(T_COVER_RUNNING);
            setup_run("covers", "gameos-covers run");
        } else {
            setup_next();
        }
        break;
    default:                                       // done page
        setup_finish();
        break;
    }
}

// Called by the shared keyboard when OK is pressed on a wizard field. The RA
// fields need no action here; the machine name is applied straight away.
void setup_kb_done(std::string* target)
{
    if (target != &app.setup_name) return;
    if (!valid_name_ui(app.setup_name)) {
        app.setup_status = tr(T_NAME_INVALID);
        return;
    }
    setup_run("hostname", "gameos-setup hostname '" + app.setup_name + "'");
}

void setup_nav(int dx, int dy)
{
    if (app.setup_step == 2 && dx) {               // region <-> zone column
        if (dx > 0 && app.setup_col == 0) {
            app.setup_region = app.setup_sel;
            app.setup_col = 1;
            app.setup_sel = tz_index_of(app.setup_tz, app.setup_region);
            app.setup_scroll = 0;
        } else if (dx < 0 && app.setup_col == 1) {
            app.setup_col = 0;
            app.setup_sel = app.setup_region;
            app.setup_scroll = 0;
        }
        return;
    }
    int n = setup_rows();
    int d = dy ? dy : dx;
    if (n <= 0 || !d) return;
    app.setup_sel = std::clamp(app.setup_sel + d, 0, n - 1);
}

void setup_enter()
{
    system("mkdir -p /run/gameos");
    app.setup_step = 0;
    app.setup_sel = 0;
    app.setup_scroll = 0;
    app.setup_col = 0;
    app.setup_region = 0;
    app.setup_busy = false;
    app.setup_op.clear();
    app.setup_status.clear();
    for (int i = 0; i < 4; i++) app.setup_test[i] = "wait";
    app.setup_ra_pass.clear();
    app.cov_state.clear();
    app.cov_current.clear();
    app.cov_total = app.cov_done = app.cov_ok = app.cov_miss = app.cov_fail = 0;
    kb_close();

    // prefill from what is already persisted, so re-running from Settings
    // shows the current values instead of blanks
    app.setup_tz.clear();
    app.setup_name.clear();
    app.setup_ra_logged = false;
    app.setup_ra_name.clear();
    app.setup_ra_hardcore = false;
    std::istringstream ss(run_cmd("gameos-setup status"));
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if      (k == "timezone")    app.setup_tz = v;
        else if (k == "hostname")    app.setup_name = v;
        else if (k == "ra")          app.setup_ra_logged = (v == "1");
        else if (k == "ra_user")     app.setup_ra_name = v;
        else if (k == "ra_hardcore") app.setup_ra_hardcore = (v == "true");
    }
    app.setup_ra_user = app.setup_ra_logged ? app.setup_ra_name : std::string();
    if (app.setup_name.empty()) {
        char h[64] = {};
        if (gethostname(h, sizeof(h) - 1) == 0) app.setup_name = h;
    }
    if (app.setup_tz.empty()) {
        std::string z = read_file("/etc/timezone");
        while (!z.empty() && (z.back() == '\n' || z.back() == '\r')) z.pop_back();
        app.setup_tz = z;
    }
    for (int i = 0; i < (int)tz_regions().size(); i++) {
        const auto& zs = tz_regions()[i].zones;
        for (int j = 0; j < (int)zs.size(); j++)
            if (app.setup_tz == zs[j]) app.setup_region = i;
    }
    app.screen = Screen::Setup;
}

// ---------------------------------------------------------------------------
// wizard drawing
// ---------------------------------------------------------------------------
void draw_kb(SDL_Renderer* r);   // defined with the WiFi screen below

void setup_row(SDL_Renderer* r, int x, int y, int w, int h, bool sel,
               const std::string& label, const std::string& value,
               SDL_Color vc = C_DIM)
{
    if (sel) {
        fill_rect(r, x, y, w, h - 6, C_PANEL);
        fill_rect(r, x, y, 5, h - 6, g_accent);
    }
    text(r, app.f_body, label, x + 24, y + 12, sel ? C_TEXT : C_DIM);
    if (!value.empty())
        text(r, app.f_body, ellipsize(app.f_body, value, w - 400),
             x + w - 24, y + 12, vc, Align::Right);
}

void setup_scroll_into_view(int vis)
{
    if (app.setup_sel < app.setup_scroll) app.setup_scroll = app.setup_sel;
    if (app.setup_sel >= app.setup_scroll + vis)
        app.setup_scroll = app.setup_sel - vis + 1;
    if (app.setup_scroll < 0) app.setup_scroll = 0;
}

void draw_setup_lang(SDL_Renderer* r)
{
    const int rowh = 62, top = 126;
    setup_scroll_into_view(7);
    for (int i = 0; i < (int)LANG_COUNT; i++) {
        int idx = app.setup_scroll + i;
        if (idx >= (int)LANG_COUNT) break;
        int y = top + i * rowh;
        bool sel = idx == app.setup_sel;
        setup_row(r, 32, y, W - 64, rowh, sel, lang_name(idx), "");
        if (idx == app.settings.language)
            fill_rect(r, W - 72, y + 22, 16, 16, g_accent);
    }
}

void draw_setup_wifi(SDL_Renderer* r)
{
    const int rowh = 56, top = 116, vis = 6;
    int nets = (int)app.wifi_nets.size();
    int n = nets + 2;
    setup_scroll_into_view(vis);
    for (int i = 0; i < vis; i++) {
        int idx = app.setup_scroll + i;
        if (idx >= n) break;
        int y = top + i * rowh;
        bool sel = idx == app.setup_sel;
        if (idx < nets) {
            const WifiNet& w = app.wifi_nets[idx];
            setup_row(r, 32, y, W - 64, rowh, sel, w.ssid, "");
            if (w.saved)
                text(r, app.f_small, tr(T_WIFI_SAVED), W - 140, y + 16,
                     C_GREEN, Align::Right);
            else if (!w.secure)
                text(r, app.f_small, tr(T_WIFI_OPEN), W - 140, y + 16,
                     C_DIM, Align::Right);
            int bars = w.signal >= -55 ? 4 : w.signal >= -67 ? 3
                     : w.signal >= -78 ? 2 : 1;
            for (int b = 0; b < 4; b++) {
                int h = 6 + b * 5;
                fill_rect(r, W - 104 + b * 13, y + 34 - h, 8, h,
                          b < bars ? g_accent : SDL_Color{ C_DIM.r, C_DIM.g, C_DIM.b, 90 });
            }
        } else {
            setup_row(r, 32, y, W - 64, rowh, sel,
                      idx == nets ? tr(T_WIFI_TEST) : tr(T_NEXT), "");
        }
    }
    // On its own line below the action rows: centred on the first row it
    // collides with the "test connectivity" label in ru/es.
    if (nets == 0 && !app.wifi_busy)
        text(r, app.f_small, tr(T_WIFI_NO_NETS), W / 2, top + n * rowh + 24,
             C_DIM, Align::Center);

    // four staged results: association, route, DNS, real internet access
    const TKey lab[4] = { T_TEST_LINK, T_TEST_ROUTE, T_TEST_DNS, T_TEST_NET };
    for (int i = 0; i < 4; i++) {
        int x = 40 + i * 245;
        const std::string& v = app.setup_test[i];
        SDL_Color c = v == "ok" ? C_GREEN : v == "fail" ? C_RED : C_DIM;
        text(r, app.f_small, tr(lab[i]), x, 478, C_DIM);
        text(r, app.f_body,
             v == "ok" ? tr(T_TEST_OK) : v == "fail" ? tr(T_TEST_FAIL) : tr(T_TEST_WAIT),
             x, 506, c);
    }

    std::string link = tr(T_WIFI_DISCONNECTED);
    SDL_Color lc = C_DIM;
    if (app.wifi_state == "NOHW") {
        link = tr(T_WIFI_UNAVAILABLE);
    } else if (!app.wifi_ip.empty()) {
        char b[96];
        snprintf(b, sizeof(b), tr(T_WIFI_CONNECTED_FMT), app.wifi_ip.c_str());
        link = b;
        lc = C_GREEN;
    }
    text(r, app.f_body, link, W / 2, 566, lc, Align::Center);
    if (app.wifi_busy || !app.wifi_status.empty())
        text(r, app.f_small, app.wifi_busy ? tr(T_PROCESSING) : app.wifi_status,
             W / 2, 610, C_AMBER, Align::Center);
}

void draw_setup_tz(SDL_Renderer* r)
{
    const int rowh = 56, top = 126;
    const auto& regs = tz_regions();
    for (int i = 0; i < (int)regs.size(); i++) {
        int y = top + i * rowh;
        bool sel = app.setup_col == 0 && i == app.setup_sel;
        bool cur = i == app.setup_region;
        setup_row(r, 32, y, 328, rowh, sel, tr(regs[i].name), "");
        if (cur) fill_rect(r, 348, y + 20, 8, 8, g_accent);
    }
    const auto& zs = regs[app.setup_region].zones;
    setup_scroll_into_view(6);
    for (int i = 0; i < 6; i++) {
        int idx = app.setup_scroll + i;
        if (idx >= (int)zs.size()) break;
        int y = top + i * rowh;
        bool sel = app.setup_col == 1 && idx == app.setup_sel;
        setup_row(r, 392, y, W - 424, rowh, sel, zs[idx], "");
        if (app.setup_tz == zs[idx])
            fill_rect(r, W - 72, y + 20, 14, 14, g_accent);
    }
    std::string preview = app.setup_col == 0
        ? zs[0] : zs[std::clamp(app.setup_sel, 0, (int)zs.size() - 1)];
    char b[128];
    snprintf(b, sizeof(b), tr(T_TZ_NOW_FMT), zone_time(preview).c_str());
    text(r, app.f_body, b, W / 2, 500, C_TEXT, Align::Center);
    if (!app.setup_tz.empty())
        text(r, app.f_small, app.setup_tz, W / 2, 548, C_DIM, Align::Center);
    text(r, app.f_small, tr(T_SETUP_TZ_HINT), W / 2, 600, C_DIM, Align::Center);
}

void draw_setup_name(SDL_Renderer* r)
{
    auto lines = wrap(app.f_small, tr(T_SETUP_NAME_HELP), W - 120);
    int y = 130;
    for (const auto& l : lines) { text(r, app.f_small, l, 60, y, C_DIM); y += 30; }

    setup_row(r, 32, 230, W - 64, 70, app.setup_sel == 0,
              tr(T_SETUP_NAME_TITLE), app.setup_name, C_TEXT);
    setup_row(r, 32, 320, W - 64, 70, app.setup_sel == 1, tr(T_NEXT), "");

    auto rl = wrap(app.f_small, tr(T_NAME_INVALID), W - 120);
    y = 440;
    for (const auto& l : rl) { text(r, app.f_small, l, 60, y, C_DIM); y += 30; }
}

void draw_setup_ra(SDL_Renderer* r)
{
    auto lines = wrap(app.f_small, tr(T_SETUP_RA_HELP), W - 120);
    int y = 122;
    for (const auto& l : lines) { text(r, app.f_small, l, 60, y, C_DIM); y += 28; }

    const int rowh = 62, top = 190;
    char b[192];
    std::string login = tr(T_RA_LOGIN);
    if (app.setup_ra_logged) {
        snprintf(b, sizeof(b), tr(T_RA_LOGGED_FMT), app.setup_ra_name.c_str());
        login = b;
    }
    setup_row(r, 32, top + 0 * rowh, W - 64, rowh, app.setup_sel == 0,
              tr(T_RA_USER), app.setup_ra_user, C_TEXT);
    setup_row(r, 32, top + 1 * rowh, W - 64, rowh, app.setup_sel == 1,
              tr(T_RA_PASS), std::string(app.setup_ra_pass.size(), '*'), C_TEXT);
    setup_row(r, 32, top + 2 * rowh, W - 64, rowh, app.setup_sel == 2,
              tr(T_RA_HARDCORE),
              app.setup_ra_hardcore ? tr(T_ON) : tr(T_OFF),
              app.setup_ra_hardcore ? C_AMBER : C_DIM);
    setup_row(r, 32, top + 3 * rowh, W - 64, rowh, app.setup_sel == 3,
              login, "", C_GREEN);
    setup_row(r, 32, top + 4 * rowh, W - 64, rowh, app.setup_sel == 4,
              tr(T_RA_LOGOUT), "");
    setup_row(r, 32, top + 5 * rowh, W - 64, rowh, app.setup_sel == 5,
              tr(T_NEXT), "");

    auto hl = wrap(app.f_small, tr(T_RA_HARDCORE_HELP), W - 120);
    y = 580;
    for (const auto& l : hl) { text(r, app.f_small, l, 60, y, C_DIM); y += 28; }
}

void draw_setup_covers(SDL_Renderer* r)
{
    auto lines = wrap(app.f_small, tr(T_SETUP_COVER_HELP), W - 120);
    int y = 126;
    for (const auto& l : lines) { text(r, app.f_small, l, 60, y, C_DIM); y += 28; }

    char b[192];
    if (app.setup_busy && app.setup_op == "covers-count") {
        text(r, app.f_body, tr(T_PROCESSING), W / 2, 220, C_DIM, Align::Center);
    } else if (app.cov_total == 0 && !app.setup_busy) {
        text(r, app.f_body, tr(T_COVER_NONE), W / 2, 220, C_DIM, Align::Center);
    } else {
        snprintf(b, sizeof(b), tr(T_COVER_PROGRESS_FMT),
                 app.cov_done, app.cov_total, app.cov_ok, app.cov_miss);
        text(r, app.f_body, b, W / 2, 210, C_TEXT, Align::Center);
        int bw = W - 160, bx = 80, by = 268;
        fill_rect(r, bx, by, bw, 12, C_TRACK);
        int fw = app.cov_total > 0 ? bw * app.cov_done / app.cov_total : 0;
        if (fw > 0) fill_rect(r, bx, by, fw, 12, g_accent);
        if (!app.cov_current.empty())
            text(r, app.f_small, ellipsize(app.f_small, app.cov_current, bw),
                 W / 2, 300, C_DIM, Align::Center);
    }

    bool running = app.setup_busy && app.setup_op == "covers";
    setup_row(r, 32, 400, W - 64, 70, app.setup_sel == 0,
              running ? tr(T_COVER_STOP) : tr(T_COVER_START), "");
    setup_row(r, 32, 490, W - 64, 70, app.setup_sel == 1, tr(T_NEXT), "");
}

void draw_setup(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    const char* title = tr(T_SETUP_TITLE);
    switch (app.setup_step) {
    case 0: title = tr(T_LANGUAGE); break;
    case 1: title = tr(T_WIFI); break;
    case 2: title = tr(T_SETUP_TZ_TITLE); break;
    case 3: title = tr(T_SETUP_NAME_TITLE); break;
    case 4: title = tr(T_SETUP_RA_TITLE); break;
    case 5: title = tr(T_COVERS); break;
    default: break;
    }
    text(r, app.f_title, title, 32, 26, C_TEXT);
    char sb[64];
    snprintf(sb, sizeof(sb), tr(T_SETUP_STEP_FMT),
             app.setup_step + 1, SETUP_STEPS - 1);
    text(r, app.f_body, sb, W - 32, 34, C_DIM, Align::Right);
    fill_rect(r, 0, 96, W, 3, g_accent);

    switch (app.setup_step) {
    case 0: draw_setup_lang(r); break;
    case 1: draw_setup_wifi(r); break;
    case 2: draw_setup_tz(r); break;
    case 3: draw_setup_name(r); break;
    case 4: draw_setup_ra(r); break;
    case 5: draw_setup_covers(r); break;
    default: {
        text(r, app.f_title, tr(T_SETUP_DONE_TITLE), W / 2, 250,
             g_accent, Align::Center);
        auto lines = wrap(app.f_body, tr(T_SETUP_DONE_BODY), W - 200);
        int y = 350;
        for (const auto& l : lines) {
            text(r, app.f_body, l, W / 2, y, C_TEXT, Align::Center);
            y += 44;
        }
        break;
    }
    }

    if (!app.setup_status.empty())
        text(r, app.f_small, ellipsize(app.f_small, app.setup_status, W - 80),
             W / 2, H - 96, C_AMBER, Align::Center);
    draw_hint(r, app.setup_step >= SETUP_STEPS - 1
              ? tr(T_CONFIRM_HINT) : tr(T_SETUP_HINT));
    if (app.wifi_kb) draw_kb(r);
}

// ---------------------------------------------------------------------------
// system update (gameos-update writes /run/gameos/update-state)
// ---------------------------------------------------------------------------
void update_refresh()
{
    if (app.upd_cur.empty()) {
        std::string v = read_file("/etc/gameos-version");
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
        app.upd_cur = v;
    }
    std::string out = read_file("/run/gameos/update-state");
    app.upd_state.clear();
    app.upd_version.clear();
    app.upd_msg.clear();
    app.upd_pct = 0;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "state")        app.upd_state = v;
        else if (k == "version") app.upd_version = v;
        else if (k == "msg")     app.upd_msg = v;
        else if (k == "pct")     app.upd_pct = std::clamp(atoi(v.c_str()), 0, 100);
    }
}

void update_start(const std::string& op)
{
    unlink("/run/gameos/update-op.rc");
    std::string cmd = "mkdir -p /run/gameos; gameos-update " + op +
                      " >/dev/null 2>&1 &";
    if (system(cmd.c_str()) != 0) { /* ignore */ }
    app.upd_busy = true;
    app.upd_op = op;
    app.upd_op_at = SDL_GetTicks();
    app.upd_pct = 0;
    app.upd_msg.clear();
    app.upd_state = (op == "apply") ? "downloading" : "checking";
}

void update_poll()
{
    Uint32 now = SDL_GetTicks();
    if (app.screen == Screen::Update && now - app.upd_refresh_at > 500) {
        app.upd_refresh_at = now;
        update_refresh();
    }
    if (!app.upd_busy) return;
    Uint32 limit = (app.upd_op == "apply") ? 1800000u : 120000u;
    if (now - app.upd_op_at > limit) {
        app.upd_busy = false;
        app.upd_op.clear();
        app.upd_state = "failed";
        app.upd_msg = "timeout";
        return;
    }
    if (now - app.upd_poll_at < 300) return;
    app.upd_poll_at = now;
    FILE* f = fopen("/run/gameos/update-op.rc", "r");
    if (!f) return;
    int rc = 1;
    if (fscanf(f, "%d", &rc) != 1) rc = 1;
    fclose(f);
    app.upd_busy = false;
    app.upd_op.clear();
    update_refresh();
    if (rc != 0 && app.upd_state != "failed" && app.upd_state != "nowifi")
        app.upd_state = "failed";
}

void update_enter()
{
    update_refresh();
    app.screen = Screen::Update;
    if (!app.upd_busy && (app.upd_state.empty() || app.upd_state == "idle"))
        update_start("check");
}

void draw_update(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, tr(T_UPDATE), 32, 26, C_TEXT);
    fill_rect(r, 0, 96, W, 3, g_accent);

    char b[192];
    if (!app.upd_cur.empty()) {
        snprintf(b, sizeof(b), tr(T_UPDATE_CURRENT), app.upd_cur.c_str());
        text(r, app.f_small, b, 32, 120, C_DIM);
    }

    std::string st;
    SDL_Color c = C_TEXT;
    if (app.upd_busy && app.upd_op == "apply" && app.upd_state == "downloading") {
        snprintf(b, sizeof(b), tr(T_UPDATE_DOWNLOADING), app.upd_pct);
        st = b; c = g_accent;
    } else if (app.upd_busy && app.upd_op == "apply") {
        st = tr(T_UPDATE_INSTALLING); c = C_AMBER;
    } else if (app.upd_busy) {
        st = tr(T_UPDATE_CHECKING); c = C_DIM;
    } else if (app.upd_state == "available") {
        snprintf(b, sizeof(b), tr(T_UPDATE_AVAILABLE), app.upd_version.c_str());
        st = b; c = C_GREEN;
    } else if (app.upd_state == "uptodate") {
        st = tr(T_UPDATE_UPTODATE); c = C_GREEN;
    } else if (app.upd_state == "ready") {
        st = tr(T_UPDATE_READY); c = C_GREEN;
    } else if (app.upd_state == "nowifi") {
        st = tr(T_UPDATE_NOWIFI); c = C_AMBER;
    } else if (app.upd_state == "failed") {
        snprintf(b, sizeof(b), tr(T_UPDATE_FAILED), app.upd_msg.empty() ? "?" : app.upd_msg.c_str());
        st = b; c = C_RED;
    } else {
        st = tr(T_UPDATE_IDLE); c = C_DIM;
    }
    auto lines = wrap(app.f_body, st, W - 160);
    int y = 230;
    for (const auto& l : lines) {
        text(r, app.f_body, l, W / 2, y, c, Align::Center);
        y += 52;
    }

    if (app.upd_busy && app.upd_op == "apply" && app.upd_state == "downloading") {
        fill_rect(r, W / 2 - 300, 420, 600, 18, { C_DIM.r, C_DIM.g, C_DIM.b, 90 });
        fill_rect(r, W / 2 - 300, 420, 600 * app.upd_pct / 100, 18, g_accent);
        snprintf(b, sizeof(b), "%d%%", app.upd_pct);
        text(r, app.f_small, b, W / 2, 452, C_DIM, Align::Center);
    }
    if (!app.upd_busy && app.upd_state == "ready") {
        text(r, app.f_body, tr(T_UPDATE_REBOOT), W / 2, 430, C_AMBER, Align::Center);
    }
    draw_hint(r, tr(T_UPDATE_HINT));
}

void draw_kb(SDL_Renderer* r)
{
    fill_rect(r, 0, 0, W, H, { C_BG.r, C_BG.g, C_BG.b, 205 });    int pw = 944, ph = 476, px = (W - pw) / 2, py = (H - ph) / 2;
    fill_rect(r, px, py, pw, ph, C_PANEL);
    fill_rect(r, px, py, pw, 4, g_accent);
    const std::string& buf = app.kb_generic ? *app.kb_buf : app.wifi_pw;
    text(r, app.f_title, app.kb_generic ? app.kb_title : tr(T_WIFI_PASSWORD),
         W / 2, py + 22, C_TEXT, Align::Center);
    text(r, app.f_small, app.kb_generic ? std::string() : app.wifi_target,
         W / 2, py + 78, C_DIM, Align::Center);

    fill_rect(r, px + 56, py + 108, pw - 112, 52, { C_DIM.r, C_DIM.g, C_DIM.b, 120 });
    std::string disp = app.kb_mask ? std::string(buf.size(), '*') : buf;
    if (disp.size() > 42) disp = disp.substr(disp.size() - 42);
    text(r, app.f_body, disp + "_", px + 72, py + 120, C_TEXT);

    const auto& rows = kb_rows();
    const int kh = 54, gap = 6, ky0 = py + 182;
    for (int ri = 0; ri < (int)rows.size(); ri++) {
        const auto& row = rows[ri];
        int kw, kgap;
        if (ri == (int)rows.size() - 1) {   // wide action row
            kw = (pw - 112 - 4 * 8) / 5;
            kgap = 8;
        } else {
            kw = 80; kgap = gap;
        }
        int total = (int)row.size() * (kw + kgap) - kgap;
        int x0 = px + (pw - total) / 2;
        int y = ky0 + ri * (kh + gap);
        for (int ci = 0; ci < (int)row.size(); ci++) {
            int x = x0 + ci * (kw + kgap);
            bool sel = app.kb_r == ri && app.kb_c == ci;
            fill_rect(r, x, y, kw, kh, sel ? g_accent : C_PANEL);
            std::string lab = row[ci];
            if (lab == "ABC") lab = app.kb_shift ? "abc" : "ABC";
            else if (lab == " ") lab = tr(T_KB_SPACE);
            else if (lab == "DEL") lab = tr(T_KB_DEL);
            else if (lab == "OK") lab = tr(T_KB_OK);
            else if (lab == "X") lab = tr(T_KB_CANCEL);
            else if (app.kb_shift && lab.size() == 1)
                lab[0] = (char)toupper((unsigned char)lab[0]);
            text(r, app.f_small, lab, x + kw / 2, y + 15,
                 sel ? C_BG : C_TEXT, Align::Center);
        }
    }
    text(r, app.f_small, tr(T_CONFIRM_HINT), W / 2, py + ph - 34, C_DIM, Align::Center);
}

void draw_wifi(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, tr(T_WIFI), 32, 26, C_TEXT);

    std::string right = tr(T_WIFI_DISCONNECTED);
    SDL_Color rc = C_DIM;
    if (app.wifi_state == "NOHW") {
        right = tr(T_WIFI_UNAVAILABLE);
    } else if (!app.wifi_ip.empty()) {
        char b[64];
        snprintf(b, sizeof(b), tr(T_WIFI_CONNECTED_FMT), app.wifi_ip.c_str());
        right = b;
        rc = C_GREEN;
    }
    text(r, app.f_body, right, W - 32, 34, rc, Align::Right);
    fill_rect(r, 0, 96, W, 3, g_accent);

    const int rowh = 62, top = 116, vis = 7;
    int n = (int)app.wifi_nets.size();
    if (app.wifi_sel < app.wifi_scroll) app.wifi_scroll = app.wifi_sel;
    if (app.wifi_sel >= app.wifi_scroll + vis) app.wifi_scroll = app.wifi_sel - vis + 1;
    if (app.wifi_scroll < 0) app.wifi_scroll = 0;
    for (int i = 0; i < vis; i++) {
        int idx = app.wifi_scroll + i;
        if (idx >= n) break;
        int y = top + i * rowh;
        const WifiNet& w = app.wifi_nets[idx];
        if (idx == app.wifi_sel) {
            fill_rect(r, 24, y, W - 48, rowh - 6, C_PANEL);
            fill_rect(r, 24, y, 5, rowh - 6, g_accent);
        }
        SDL_Color tc = (idx == app.wifi_sel) ? C_TEXT : C_DIM;
        text(r, app.f_body, w.ssid, 48, y + 12, tc);
        if (w.saved)
            text(r, app.f_small, tr(T_WIFI_SAVED), W - 128, y + 18, C_GREEN, Align::Right);
        else if (!w.secure)
            text(r, app.f_small, tr(T_WIFI_OPEN), W - 128, y + 18, C_DIM, Align::Right);
        int bars = w.signal >= -55 ? 4 : w.signal >= -67 ? 3 : w.signal >= -78 ? 2 : 1;
        for (int b = 0; b < 4; b++) {
            int h = 6 + b * 5;
            SDL_Color bc = (b < bars) ? g_accent : SDL_Color{ C_DIM.r, C_DIM.g, C_DIM.b, 90 };
            fill_rect(r, W - 92 + b * 13, y + 36 - h, 8, h, bc);
        }
    }
    if (n == 0 && !app.wifi_busy)
        text(r, app.f_body, tr(T_WIFI_NO_NETS), W / 2, top + 2 * rowh + 20,
             C_DIM, Align::Center);

    std::string st = app.wifi_busy ? tr(T_PROCESSING) : app.wifi_status;
    if (!st.empty())
        text(r, app.f_small, st, W / 2, H - 96, C_AMBER, Align::Center);
    draw_hint(r, tr(T_WIFI_HINT));
    if (app.wifi_kb) draw_kb(r);
}

const char* bt_kind_name(const std::string& k)
{
    if (k == "audio") return tr(T_KIND_AUDIO);
    if (k == "gamepad") return tr(T_KIND_PAD);
    if (k == "keyboard") return tr(T_KIND_KBD);
    if (k == "mouse") return tr(T_KIND_MOUSE);
    return tr(T_KIND_DEV);
}

void draw_bluetooth(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, tr(T_BT), 32, 26, C_TEXT);
    text(r, app.f_body, app.bt_on ? tr(T_ENABLED) : tr(T_DISABLED), W - 32, 34,
         app.bt_on ? C_GREEN : C_DIM, Align::Right);
    fill_rect(r, 0, 96, W, 3, g_accent);

    const int rowh = 62, top = 116;
    if (app.bt_sel == 0) {
        fill_rect(r, 24, top, W - 48, rowh - 6, C_PANEL);
        fill_rect(r, 24, top, 5, rowh - 6, g_accent);
    }
    text(r, app.f_body, tr(T_BT_SWITCH), 48, top + 12, app.bt_sel == 0 ? C_TEXT : C_DIM);
    text(r, app.f_body, app.bt_on ? tr(T_ON) : tr(T_OFF), W - 48, top + 12,
         app.bt_on ? g_accent : C_DIM, Align::Right);

    if (app.bt_on) {
        const int vis = 8;
        int n = (int)app.bt_devs.size();
        if (app.bt_sel - 1 < app.bt_scroll) app.bt_scroll = app.bt_sel - 1;
        if (app.bt_sel - 1 >= app.bt_scroll + vis) app.bt_scroll = app.bt_sel - vis;
        if (app.bt_scroll < 0) app.bt_scroll = 0;
        for (int i = 0; i < vis; i++) {
            int idx = app.bt_scroll + i;
            if (idx >= n) break;
            int y = top + (i + 1) * rowh;
            const BtDev& d = app.bt_devs[idx];
            if (app.bt_sel == idx + 1) {
                fill_rect(r, 24, y, W - 48, rowh - 6, C_PANEL);
                fill_rect(r, 24, y, 5, rowh - 6, g_accent);
            }
            SDL_Color tc = (app.bt_sel == idx + 1) ? C_TEXT : C_DIM;
            text(r, app.f_body, d.name.empty() ? d.mac : d.name, 48, y + 12, tc);
            std::string right = bt_kind_name(d.kind);
            if (d.connected) right += tr(T_CONN_SUFFIX);
            else if (d.paired) right += tr(T_PAIRED_SUFFIX);
            text(r, app.f_small, right, W - 48, y + 16,
                 d.connected ? C_GREEN : C_DIM, Align::Right);
        }
        if (n == 0 && !app.bt_busy)
            text(r, app.f_body, tr(T_NO_DEVS),
                 W / 2, top + 2 * rowh + 20, C_DIM, Align::Center);
    }

    std::string st = app.bt_busy ? tr(T_PROCESSING) : app.bt_status;
    if (!st.empty())
        text(r, app.f_small, st, W / 2, H - 96, C_AMBER, Align::Center);
    draw_hint(r, tr(T_BT_HINT));
}

void draw_settings(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, tr(T_SETTINGS), 32, 26, C_TEXT);
    fill_rect(r, 0, 96, W, 3, g_accent);

    const char* rows[] = { tr(T_BRIGHTNESS), tr(T_VOLUME), tr(T_BT), tr(T_WIFI),
                           tr(T_THEME), tr(T_LANGUAGE), tr(T_UPDATE), tr(T_SAVES),
                           tr(T_POWEROFF), tr(T_REBOOT), tr(T_ABOUT), tr(T_SETUP) };
    const int rowh = 50, top = 100;
    for (int i = 0; i < 12; i++) {
        int y = top + i * rowh;
        if (i == app.set_sel) {
            fill_rect(r, 120, y, W - 240, rowh - 8, C_PANEL);
            fill_rect(r, 120, y, 5, rowh - 8, g_accent);
        }
        SDL_Color tc = (i == app.set_sel) ? C_TEXT : C_DIM;
        text(r, app.f_body, rows[i], 150, y + 10, tc);
        if (i == 0 || i == 1) {
            int v = (i == 0) ? app.settings.brightness : app.settings.volume;
            fill_rect(r, 560, y + 20, 240, 12, { C_DIM.r, C_DIM.g, C_DIM.b, 90 });
            fill_rect(r, 560, y + 20, 240 * v / 100, 12, g_accent);
            char b[16];
            snprintf(b, sizeof(b), "%d%%", v);
            text(r, app.f_body, b, 860, y + 6, tc, Align::Right);
        } else if (i == 2) {
            text(r, app.f_body, app.bt_on ? tr(T_ENABLED) : tr(T_DISABLED), 860, y + 10,
                 app.bt_on ? C_GREEN : C_DIM, Align::Right);
        } else if (i == 3) {
            const char* w = app.wifi_state == "NOHW" ? tr(T_WIFI_UNAVAILABLE)
                          : !app.wifi_link.empty() ? app.wifi_link.c_str()
                          : tr(T_WIFI_DISCONNECTED);
            text(r, app.f_body, w, 860, y + 10,
                 !app.wifi_link.empty() ? C_GREEN : C_DIM, Align::Right);
        } else if (i == 4) {
            text(r, app.f_body, theme_name(app.settings.theme), 860, y + 10, g_accent, Align::Right);
        } else if (i == 5) {
            text(r, app.f_body, lang_name(app.settings.language), 860, y + 10, g_accent, Align::Right);
        } else if (i == 6) {
            char u[96];
            const char* s;
            SDL_Color uc = C_DIM;
            if (app.upd_busy) {
                s = (app.upd_op == "apply") ? tr(T_UPDATE_INSTALLING)
                                            : tr(T_UPDATE_CHECKING);
            } else if (app.upd_state == "available") {
                snprintf(u, sizeof(u), tr(T_UPDATE_AVAILABLE), app.upd_version.c_str());
                s = u; uc = C_GREEN;
            } else if (app.upd_state == "uptodate") {
                s = tr(T_UPDATE_UPTODATE); uc = C_GREEN;
            } else if (app.upd_state == "ready") {
                s = tr(T_UPDATE_REBOOT); uc = C_GREEN;
            } else if (app.upd_state == "nowifi") {
                s = tr(T_UPDATE_NOWIFI);
            } else if (app.upd_state == "failed") {
                snprintf(u, sizeof(u), tr(T_UPDATE_FAILED),
                         app.upd_msg.empty() ? "?" : app.upd_msg.c_str());
                s = u;
            } else {
                s = tr(T_UPDATE_IDLE);
            }
            text(r, app.f_small, s, 860, y + 10, uc, Align::Right);
        } else if (i == 7) {
            char b[32];
            snprintf(b, sizeof(b), tr(T_SAVES_COUNT_FMT), app.saves.size());
            text(r, app.f_body, b, 860, y + 10, tc, Align::Right);
        }
    }
    draw_hint(r, tr(T_SETTINGS_HINT));
}

void draw_saves(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, tr(T_SAVES), 32, 26, C_TEXT);
    char b[64];
    snprintf(b, sizeof(b), tr(T_SAVE_COUNT_FMT), app.saves.size());
    text(r, app.f_body, b, W - 32, 34, C_DIM, Align::Right);
    fill_rect(r, 0, 96, W, 3, g_accent);

    if (app.saves.empty()) {
        text(r, app.f_body, tr(T_NO_SAVES), W / 2, 300, C_TEXT, Align::Center);
        text(r, app.f_small, tr(T_SAVES_HELP),
             W / 2, 360, C_DIM, Align::Center);
    } else {
        const int rowh = 60, top = 116, vis = 9;
        if (app.save_sel < app.save_scroll) app.save_scroll = app.save_sel;
        if (app.save_sel >= app.save_scroll + vis) app.save_scroll = app.save_sel - vis + 1;
        for (int i = 0; i < vis; i++) {
            int idx = app.save_scroll + i;
            if (idx >= (int)app.saves.size()) break;
            int y = top + i * rowh;
            if (idx == app.save_sel) {
                fill_rect(r, 24, y, W - 48, rowh - 6, C_PANEL);
                fill_rect(r, 24, y, 5, rowh - 6, g_accent);
            }
            SDL_Color tc = (idx == app.save_sel) ? C_TEXT : C_DIM;
            text(r, app.f_body, app.saves[idx].name, 48, y + 12, tc);
            char kb[32];
            snprintf(kb, sizeof(kb), "%ld KB", app.saves[idx].size / 1024);
            text(r, app.f_small, kb, W - 48, y + 16, C_DIM, Align::Right);
        }
    }
    draw_hint(r, tr(T_SAVES_HINT));
}

// ---------------------------------------------------------------------------
// activity statistics / favorites / history
// ---------------------------------------------------------------------------
void draw_tabbar(SDL_Renderer* r, const std::vector<std::string>& names, int active)
{
    int x = 32;
    for (int i = 0; i < (int)names.size(); i++) {
        int w = text_width(app.f_body, names[i]) + 32;
        if (i == active) {
            fill_rect(r, x, 104, w, 46, C_PANEL);
            fill_rect(r, x, 146, w, 4, g_accent);
        }
        text(r, app.f_body, names[i], x + 16, 112, (i == active) ? C_TEXT : C_DIM);
        x += w + 10;
    }
}

void draw_scrollbar(SDL_Renderer* r, int top, int vis, int rowh, int n, int scroll, int w)
{
    if (n <= vis) return;
    int trackh = vis * rowh;
    int barh = std::max(30, trackh * vis / n);
    int bary = top + (trackh - barh) * scroll / std::max(1, n - vis);
    fill_rect(r, w - 14, top, 4, trackh, { C_DIM.r, C_DIM.g, C_DIM.b, 90 });
    fill_rect(r, w - 14, bary, 4, barh, g_accent);
}

// Two-line selectable row: title above, dim meta below, one value on the right.
void draw_row(SDL_Renderer* r, int y, int w, bool sel, const std::string& title,
              const std::string& meta, const std::string& right, SDL_Color rightc)
{
    const int rowh = 76;
    if (sel) {
        fill_rect(r, 24, y, w - 48, rowh - 6, C_PANEL);
        fill_rect(r, 24, y, 5, rowh - 6, g_accent);
    }
    int rw = text_width(app.f_small, right);
    text(r, app.f_body, ellipsize(app.f_body, title, w - 100 - rw),
         48, y + 4, sel ? C_TEXT : C_DIM);
    text(r, app.f_small, ellipsize(app.f_small, meta, w - 100 - rw), 48, y + 44, C_DIM);
    if (!right.empty()) text(r, app.f_small, right, w - 40, y + 26, rightc, Align::Right);
}

// Rows the current activity tab lets you select. Trends is a chart: nothing.
static int act_row_count()
{
    switch (app.act_tab) {
    case 0:  return std::min(5, (int)app.act_games.size());
    case 1:  return (int)app.act_sessions.size();
    case 3:  return (int)app.act_picks.size();
    default: return 0;
    }
}

void draw_activity(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, tr(T_ACTIVITY), 32, 26, C_TEXT);
    text(r, app.f_body, fmt_duration(app.act_total), W - 32, 34, C_DIM, Align::Right);
    fill_rect(r, 0, 96, W, 3, g_accent);
    draw_tabbar(r, { tr(T_TAB_OVERVIEW), tr(T_TAB_TIMELINE),
                     tr(T_TAB_TREND), tr(T_TAB_PICKS) }, app.act_tab);

    const int top = 164, rowh = 76, vis = 7;

    if (app.act_tab == 0) {
        long long today = app.act_daily.empty() ? 0 : app.act_daily.back();
        long long week = 0;
        for (int i = std::max(0, (int)app.act_daily.size() - 7);
             i < (int)app.act_daily.size(); i++)
            week += app.act_daily[i];

        const char* labels[] = { tr(T_ACT_TOTAL), tr(T_ACT_SESSIONS),
                                 tr(T_ACT_GAMES_PLAYED), tr(T_ACT_TODAY) };
        // act_sessions is capped at the newest 200 for browsing; the count tile
        // must be the real total, which the per-game aggregates still carry.
        long long sess_total = 0;
        for (const auto& g : app.act_games) sess_total += g.sessions;
        char v1[32], v2[64];
        snprintf(v1, sizeof(v1), "%lld", sess_total);
        snprintf(v2, sizeof(v2), tr(T_ACT_GAMES_FMT), app.act_games.size());
        std::string vals[] = { fmt_duration(app.act_total), v1, v2, fmt_duration(today) };
        // 2x2 rather than 4-across: a localised "9999 小时 59 分" needs 213px
        // at 30pt, which a 228px tile cannot show without truncating.
        for (int i = 0; i < 4; i++) {
            int x = 32 + (i % 2) * 488, y = 170 + (i / 2) * 108;
            fill_rect(r, x, y, 472, 96, C_PANEL);
            fill_rect(r, x, y, 472, 3, g_accent);
            text(r, app.f_small, labels[i], x + 16, y + 12, C_DIM);
            text(r, app.f_body, ellipsize(app.f_body, vals[i], 440), x + 16, y + 44, C_TEXT);
        }
        text(r, app.f_small, tr(T_ACT_WEEK) + std::string(" · ") + fmt_duration(week),
             32, 390, C_DIM);
        text(r, app.f_body, tr(T_ACT_MOST), 32, 424, C_TEXT);

        if (app.act_games.empty()) {
            text(r, app.f_body, tr(T_ACT_EMPTY), W / 2, 500, C_TEXT, Align::Center);
            text(r, app.f_small, tr(T_ACT_EMPTY_HELP), W / 2, 556, C_DIM, Align::Center);
        } else {
            for (int i = 0; i < act_row_count(); i++) {
                const GameStat& g = app.act_games[i];
                int y = 466 + i * 48;
                bool sel = (i == app.act_sel);
                if (sel) {
                    fill_rect(r, 24, y, W - 48, 44, C_PANEL);
                    fill_rect(r, 24, y, 5, 44, g_accent);
                }
                char rk[8], ss[64];
                snprintf(rk, sizeof(rk), "%d", i + 1);
                snprintf(ss, sizeof(ss), tr(T_ACT_SESSIONS_FMT), g.sessions);
                text(r, app.f_body, rk, 48, y + 6, sel ? g_accent : C_DIM);
                text(r, app.f_body, ellipsize(app.f_body, g.title, 440),
                     96, y + 6, sel ? C_TEXT : C_DIM);
                text(r, app.f_small, console_badge(g.sys), W - 420, y + 11, C_DIM, Align::Right);
                text(r, app.f_small, ss, W - 250, y + 11, C_DIM, Align::Right);
                text(r, app.f_small, fmt_duration(g.seconds), W - 48, y + 11,
                     g_accent, Align::Right);
            }
        }
    } else if (app.act_tab == 1) {
        if (app.act_sessions.empty()) {
            text(r, app.f_body, tr(T_ACT_EMPTY), W / 2, 330, C_TEXT, Align::Center);
            text(r, app.f_small, tr(T_ACT_EMPTY_HELP), W / 2, 386, C_DIM, Align::Center);
        } else {
            if (app.act_sel < app.act_scroll) app.act_scroll = app.act_sel;
            if (app.act_sel >= app.act_scroll + vis) app.act_scroll = app.act_sel - vis + 1;
            for (int i = 0; i < vis; i++) {
                int idx = app.act_scroll + i;
                if (idx >= (int)app.act_sessions.size()) break;
                const PlaySession& s = app.act_sessions[idx];
                draw_row(r, top + i * rowh, W, idx == app.act_sel, s.title,
                         fmt_when(s.start) + " · " + console_badge(s.sys),
                         fmt_duration(s.seconds), g_accent);
            }
            draw_scrollbar(r, top, vis, rowh, (int)app.act_sessions.size(),
                           app.act_scroll, W);
        }
    } else if (app.act_tab == 2) {
        long long mx = 0;
        for (long long v : app.act_daily) mx = std::max(mx, v);
        text(r, app.f_body, tr(T_ACT_TREND_TITLE), 32, 170, C_TEXT);
        if (mx == 0) {
            text(r, app.f_body, tr(T_ACT_NO_TREND), W / 2, 330, C_TEXT, Align::Center);
        } else {
            const int n = (int)app.act_daily.size();
            const int cx0 = 48, cw = W - 96, base = 430, ch = 200;
            int slot = cw / n, bw = slot - 14;
            time_t now = time(nullptr);
            for (int i = 0; i < n; i++) {
                int x = cx0 + i * slot + 7;
                int h = (int)(ch * app.act_daily[i] / mx);
                fill_rect(r, x, base - ch, bw, ch, { C_DIM.r, C_DIM.g, C_DIM.b, 60 });
                if (h > 0) fill_rect(r, x, base - h, bw, h, g_accent);
                // label by local calendar day, not now - k*86400: a DST change
                // would otherwise shift every label after it by one day
                struct tm tmv;
                localtime_r(&now, &tmv);
                tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
                tmv.tm_mday -= (n - 1 - i);
                // -1, not today's tm_isdst: the shifted day can be on the other
                // side of a DST change, and a stale flag makes mktime move the
                // hour — which can roll the label back to the previous day.
                tmv.tm_isdst = -1;
                mktime(&tmv);
                char d[8];
                snprintf(d, sizeof(d), "%d", tmv.tm_mday);
                text(r, app.f_small, d, x + bw / 2, base + 8, C_DIM, Align::Center);
            }
            fill_rect(r, cx0, base, cw, 2, { C_DIM.r, C_DIM.g, C_DIM.b, 200 });
        }
        text(r, app.f_body, tr(T_ACT_BY_CONSOLE), 32, 486, C_TEXT);
        long long per[5] = { 0, 0, 0, 0, 0 };
        for (const auto& g : app.act_games) {
            int ci = console_index(g.sys);
            if (ci < 5) per[ci] += g.seconds;
        }
        long long pmx = 0;
        for (long long v : per) pmx = std::max(pmx, v);
        for (int i = 0; i < 5; i++) {
            int y = 524 + i * 36;
            text(r, app.f_small, consoles()[i].name_en, 48, y, C_DIM);
            fill_rect(r, 140, y + 6, 600, 14, { C_DIM.r, C_DIM.g, C_DIM.b, 60 });
            if (pmx > 0 && per[i] > 0)
                fill_rect(r, 140, y + 6, (int)(600 * per[i] / pmx), 14, g_accent);
            text(r, app.f_small, fmt_duration(per[i]), W - 48, y, C_DIM, Align::Right);
        }
    } else {
        if (app.act_picks.empty()) {
            text(r, app.f_body, tr(T_ACT_PICKS_EMPTY), W / 2, 330, C_TEXT, Align::Center);
            text(r, app.f_small, tr(T_ACT_PICKS_HELP), W / 2, 386, C_DIM, Align::Center);
        } else {
            if (app.act_sel < app.act_scroll) app.act_scroll = app.act_sel;
            if (app.act_sel >= app.act_scroll + vis) app.act_scroll = app.act_sel - vis + 1;
            const char* reasons[] = { tr(T_PICK_FAV_NEW), tr(T_PICK_FAV_BACK),
                                      tr(T_PICK_NEW), tr(T_PICK_BACK),
                                      tr(T_PICK_REGULAR) };
            for (int i = 0; i < vis; i++) {
                int idx = app.act_scroll + i;
                if (idx >= (int)app.act_picks.size()) break;
                const Pick& p = app.act_picks[idx];
                std::string right;
                if (p.idle_days > 0) {
                    char b[64];
                    snprintf(b, sizeof(b), tr(T_PICK_IDLE_FMT), p.idle_days);
                    right = b;
                } else if (p.seconds > 0) {
                    right = fmt_duration(p.seconds);
                }
                draw_row(r, top + i * rowh, W, idx == app.act_sel, p.title,
                         std::string(reasons[p.reason]) + " · " + console_badge(p.sys),
                         right, C_AMBER);
            }
            draw_scrollbar(r, top, vis, rowh, (int)app.act_picks.size(), app.act_scroll, W);
        }
    }
    draw_hint(r, tr(T_ACTIVITY_HINT));
}

void draw_quick(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, tr(T_QUICK), 32, 26, C_TEXT);
    char b[96];
    snprintf(b, sizeof(b), tr(T_QUICK_COUNT_FMT), app.quick.size());
    text(r, app.f_body, b, W - 32, 34, C_DIM, Align::Right);
    fill_rect(r, 0, 96, W, 3, g_accent);
    draw_tabbar(r, { tr(T_TAB_FAV), tr(T_TAB_HISTORY) }, app.quick_tab);

    const int top = 164, rowh = 76, vis = 7;
    const int listw = W - 360;   // leave room for the cover panel

    if (app.quick.empty()) {
        text(r, app.f_body,
             app.quick_tab == 0 ? tr(T_QUICK_EMPTY_FAV) : tr(T_QUICK_EMPTY_HIST),
             listw / 2, 330, C_TEXT, Align::Center);
        text(r, app.f_small,
             app.quick_tab == 0 ? tr(T_QUICK_EMPTY_FAV_HELP) : tr(T_QUICK_EMPTY_HIST_HELP),
             listw / 2, 386, C_DIM, Align::Center);
    } else {
        if (app.quick_sel < app.quick_scroll) app.quick_scroll = app.quick_sel;
        if (app.quick_sel >= app.quick_scroll + vis) app.quick_scroll = app.quick_sel - vis + 1;
        for (int i = 0; i < vis; i++) {
            int idx = app.quick_scroll + i;
            if (idx >= (int)app.quick.size()) break;
            const QuickEntry& e = app.quick[idx];
            std::string title = e.game.title;
            if (fav_contains(e.sys, title)) title = "★ " + title;
            std::string meta = console_badge(e.sys);
            if (e.missing) {
                meta += " · " + std::string(tr(T_QUICK_MISSING));
            } else if (e.last > 0) {
                meta += " · " + fmt_when(e.last) + " · " + fmt_duration(e.seconds);
            }
            // No right-hand value column: the meta line already carries the
            // duration, and duplicating it would cost the title 156px.
            draw_row(r, top + i * rowh, listw, idx == app.quick_sel, title, meta,
                     "", C_DIM);
        }
        draw_scrollbar(r, top, vis, rowh, (int)app.quick.size(), app.quick_scroll, listw);

        const QuickEntry& e = app.quick[app.quick_sel];
        int px = W - 320, py = 190, pw = 280, ph = 280;
        fill_rect(r, px - 8, py - 8, pw + 16, ph + 16, C_PANEL);
        SDL_Texture* cv = e.missing ? nullptr
                                    : cover_tex(r, cover_path(e.sys, e.game.title));
        if (cv) {
            SDL_Rect dst = { px, py, pw, ph };
            draw_tex(r, cv, dst);
        } else if (!e.missing && app.icons[e.con]) {
            SDL_Rect dst = { px + 40, py + 40, pw - 80, ph - 80 };
            draw_tex(r, app.icons[e.con], dst, 160);
        }
        text(r, app.f_small, tr(T_COVER), px + pw / 2, py + ph + 20, C_DIM, Align::Center);
    }
    draw_hint(r, tr(T_QUICK_HINT));
}

void draw_msg(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, H, { C_BG.r, C_BG.g, C_BG.b, 170 });
    int pw = 820, ph = 460, px = (W - pw) / 2, py = (H - ph) / 2;
    fill_rect(r, px, py, pw, ph, C_PANEL);
    fill_rect(r, px, py, pw, 4, C_AMBER);
    text(r, app.f_title, app.msg_title, W / 2, py + 40, C_AMBER, Align::Center);
    auto lines = wrap(app.f_body, app.msg_body, pw - 100);
    int y = py + 120;
    for (const auto& l : lines) {
        text(r, app.f_body, l, W / 2, y, C_TEXT, Align::Center);
        y += 44;
    }
    text(r, app.f_small, tr(T_BACK), W / 2, py + ph - 52, C_DIM, Align::Center);
}

void draw_confirm(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, H, { C_BG.r, C_BG.g, C_BG.b, 170 });
    int pw = 640, ph = 300, px = (W - pw) / 2, py = (H - ph) / 2;
    fill_rect(r, px, py, pw, ph, C_PANEL);
    fill_rect(r, px, py, pw, 4, C_RED);
    if (app.confirm == ConfirmAct::Update) {
        // second confirmation before anything is downloaded or written
        char b[192];
        snprintf(b, sizeof(b), tr(T_UPDATE_CONFIRM), app.upd_version.c_str());
        text(r, app.f_title, b, W / 2, py + 48, C_TEXT, Align::Center);
        int y = py + 116;
        for (const auto& l : wrap(app.f_small, tr(T_UPDATE_CONFIRM_BODY), pw - 96)) {
            text(r, app.f_small, l, W / 2, y, C_DIM, Align::Center);
            y += 30;
        }
        text(r, app.f_body, tr(T_CONFIRM_HINT), W / 2, py + 236, C_DIM, Align::Center);
        return;
    }
    const char* t = (app.confirm == ConfirmAct::PowerOff) ? tr(T_CONFIRM_OFF)
                  : (app.confirm == ConfirmAct::Reboot) ? tr(T_CONFIRM_REBOOT)
                  : tr(T_CONFIRM_DEL);
    text(r, app.f_title, t, W / 2, py + 60, C_TEXT, Align::Center);
    text(r, app.f_body, tr(T_CONFIRM_HINT), W / 2, py + 170, C_DIM, Align::Center);
}

// ---------------------------------------------------------------------------
// bottom panel: ambient clock
// ---------------------------------------------------------------------------
void draw_ambient()
{
    if (!app.ren_bot) return;
    SDL_Renderer* r = app.ren_bot;
    if (app.ambient) {
        SDL_Rect dst = { 0, 0, W, H };
        draw_tex(r, app.ambient, dst);
    } else {
        fill_rect(r, 0, 0, W, H, C_BG);
        fill_rect(r, 0, H - 6, W, 6, g_accent);
    }
    // theme tint over the bottom panel (stronger than top for a live feel)
    fill_rect(r, 0, 0, W, H, { g_accent.r, g_accent.g, g_accent.b, (Uint8)g_tint_bot });

    text(r, app.f_logo, "gameOS", W / 2, 170, g_accent, Align::Center);

    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    std::string hhmm = two(tmv.tm_hour) + ":" + two(tmv.tm_min);
    text(r, app.f_clock, hhmm, W / 2, 260, C_TEXT, Align::Center);

    std::string date = format_date(tmv);
    text(r, app.f_body, date, W / 2, 420, C_DIM, Align::Center);
    text(r, app.f_small, tr(T_TAGLINE), W / 2, 500, C_DIM, Align::Center);
    SDL_RenderPresent(r);
}

// ---------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------
void show_msg(const std::string& title, const std::string& body)
{
    app.msg_title = title;
    app.msg_body = body;
    app.screen = Screen::Msg;
}

// Rebuild the visible list for the current console, honouring the
// favorites-only filter, and keep the selection in range.
static void rebuild_list()
{
    const Console& c = consoles()[app.carousel];
    app.games = games_for(app.carousel);
    if (app.fav_only) {
        std::vector<Game> f;
        for (const auto& g : app.games)
            if (fav_contains(c.id, g.title)) f.push_back(g);
        app.games = std::move(f);
    }
    app.sel = std::clamp(app.sel, 0, std::max(0, (int)app.games.size() - 1));
    app.scroll = 0;
}

// Resolve a "<system>/<title>" key to something launchable. games_for() is the
// same cached scan the console list uses, so a favorite on the TF2 card keeps
// its real path and launches from there.
static QuickEntry resolve_entry(const std::string& sys, const std::string& title)
{
    QuickEntry e;
    e.sys = sys;
    e.game.title = title;
    int ci = console_index(sys);
    if (ci < 5) {
        e.con = ci;
        for (const auto& g : games_for(ci)) {
            if (g.title == title) {
                e.game = g;
                return e;
            }
        }
    }
    e.missing = true;   // con stays 0: the row is never launchable anyway
    return e;
}

static void rebuild_quick()
{
    app.quick.clear();
    std::map<std::string, GameStat> played;
    for (const auto& g : activity_games())
        played[g.sys + "\t" + g.title] = g;

    if (app.quick_tab == 0) {
        auto keys = fav_list();
        // console order first, so the list reads like the carousel
        std::stable_sort(keys.begin(), keys.end(),
                         [](const std::pair<std::string, std::string>& a,
                            const std::pair<std::string, std::string>& b) {
            int ia = console_index(a.first), ib = console_index(b.first);
            if (ia != ib) return ia < ib;
            return a.second < b.second;
        });
        for (const auto& k : keys) {
            QuickEntry e = resolve_entry(k.first, k.second);
            auto it = played.find(k.first + "\t" + k.second);
            if (it != played.end()) { e.seconds = it->second.seconds; e.last = it->second.last; }
            app.quick.push_back(std::move(e));
        }
    } else {
        std::vector<GameStat> hist;
        for (const auto& kv : played) hist.push_back(kv.second);
        std::sort(hist.begin(), hist.end(), [](const GameStat& a, const GameStat& b) {
            if (a.last != b.last) return a.last > b.last;
            if (a.sys != b.sys) return a.sys < b.sys;
            return a.title < b.title;
        });
        for (const auto& s : hist) {
            QuickEntry e = resolve_entry(s.sys, s.title);
            e.seconds = s.seconds;
            e.last = s.last;
            app.quick.push_back(std::move(e));
        }
    }
    app.quick_sel = std::clamp(app.quick_sel, 0, std::max(0, (int)app.quick.size() - 1));
    app.quick_scroll = 0;
}

static void rebuild_activity()
{
    app.act_sessions = activity_sessions();
    // the timeline is a browsing aid, not an archive: keep the newest 200 so a
    // years-old log cannot make the screen allocate and sort unbounded rows
    if (app.act_sessions.size() > 200) app.act_sessions.resize(200);
    app.act_games = activity_games();
    app.act_daily = activity_daily(14);
    app.act_total = activity_total();

    std::vector<LibEntry> lib;
    for (int i = 0; i < 5; i++)
        for (const auto& g : games_for(i))
            lib.push_back(LibEntry{ consoles()[i].id, g.title });
    app.act_picks = activity_recommend(lib, 12);

    app.act_sel = 0;
    app.act_scroll = 0;
}

// The game the highlighted activity row points at. Trends has no rows.
static bool act_selected(std::string& sys, std::string& title)
{
    if (app.act_sel >= act_row_count()) return false;
    if (app.act_tab == 0) {
        sys = app.act_games[app.act_sel].sys;
        title = app.act_games[app.act_sel].title;
    } else if (app.act_tab == 1) {
        sys = app.act_sessions[app.act_sel].sys;
        title = app.act_sessions[app.act_sel].title;
    } else {
        sys = app.act_picks[app.act_sel].sys;
        title = app.act_picks[app.act_sel].title;
    }
    return true;
}

static void toggle_fav()
{
    if (app.screen == Screen::List) {
        if (app.games.empty()) return;
        fav_toggle(consoles()[app.carousel].id, app.games[app.sel].title);
        sound_play(SND_FAV);
        if (app.fav_only) rebuild_list();
    } else if (app.screen == Screen::Quick) {
        if (app.quick.empty()) return;
        std::string sys = app.quick[app.quick_sel].sys;
        std::string title = app.quick[app.quick_sel].game.title;
        fav_toggle(sys, title);
        sound_play(SND_FAV);
        // un-favoriting on the favorites tab removes the row outright
        if (app.quick_tab == 0) rebuild_quick();
    } else if (app.screen == Screen::Activity) {
        std::string sys, title;
        if (!act_selected(sys, title)) return;
        fav_toggle(sys, title);
        sound_play(SND_FAV);
        int keep = app.act_sel;
        rebuild_activity();   // the recommendation ranking depends on favorites
        app.act_sel = std::clamp(keep, 0, std::max(0, act_row_count() - 1));
    }
}

static void toggle_fav_only()
{
    if (app.screen != Screen::List) return;
    sound_play(SND_SWITCH);
    app.fav_only = !app.fav_only;
    rebuild_list();
}

static void cycle_theme(int dir = 1)
{
    app.settings.theme = (app.settings.theme + dir + 4) % 4;
    apply_theme(app.settings.theme);
    load_theme_assets();
    save_settings(app.settings);
}

static void cycle_lang(int dir = 1)
{
    app.settings.language = (app.settings.language + dir + LANG_COUNT) % LANG_COUNT;
    g_lang = (Lang)app.settings.language;
    save_settings(app.settings);
    reload_fonts();   // Korean uses a different font file
}

void enter_selection()
{
    if (app.carousel == CAR_SETTINGS) {
        app.screen = Screen::Settings;
        app.set_sel = 0;
        return;
    }
    if (app.carousel == CAR_ACTIVITY) {
        rebuild_activity();
        app.screen = Screen::Activity;
        return;
    }
    if (app.carousel == CAR_QUICK) {
        rebuild_quick();
        app.screen = Screen::Quick;
        return;
    }
    invalidate_games(app.carousel);   // entering a console: force a fresh scan
    app.sel = 0;
    rebuild_list();
    app.screen = Screen::List;
}

// Explicit refresh (Y button / R key). On the list screen update app.games
// immediately; on home the next draw_home picks up the invalidated cache.
static void refresh_games()
{
    sound_play(SND_ADJUST);
    // the two libraries span every console, so one invalidation is not enough
    if (app.screen == Screen::Activity || app.screen == Screen::Quick) {
        for (int i = 0; i < 5; i++) invalidate_games(i);
        if (app.screen == Screen::Activity) {
            int keep = app.act_sel;
            rebuild_activity();
            app.act_sel = std::clamp(keep, 0, std::max(0, act_row_count() - 1));
        } else {
            rebuild_quick();
        }
        return;
    }
    invalidate_games(app.carousel);
    if (app.screen == Screen::List) rebuild_list();
}

// TF2 hotplug: udev runs gameos-tf2 on insert/remove, which rewrites
// /run/gameos/tf2. When that file changes, every console cache is stale, so
// drop them all — a card swapped while the UI is running shows up without the
// user having to press Ⓨ. Checked once a second; the file is a few bytes.
static void tf2_poll()
{
    static Uint32 last = 0;
    static std::string seen;
    Uint32 now = SDL_GetTicks();
    if (now - last < 1000) return;
    last = now;
    std::string s = read_file("/run/gameos/tf2");
    app.tf2_mounted = s.find("state=mounted") != std::string::npos;
    if (s == seen) return;
    seen = std::move(s);
    for (int i = 0; i < 5; i++) invalidate_games(i);
    if (app.screen == Screen::List) rebuild_list();
    else if (app.screen == Screen::Quick) rebuild_quick();
    else if (app.screen == Screen::Activity) {
        int keep = app.act_sel;
        rebuild_activity();
        app.act_sel = std::clamp(keep, 0, std::max(0, act_row_count() - 1));
    }
}

// Launch one game from any screen (console list, favorites, history, a
// recommendation). Records the session; the caller refreshes its own list.
static void launch_one(const Console& c, const Game& g)
{
    // BIOS is only needed to actually run a game; gate launching, not list
    // entry, so an empty NDS library looks like every other empty console.
    if (c.dual && !nds_bios_ok()) {
        char m[128];
        snprintf(m, sizeof(m), tr(T_BIOS_MISSING_FMT), bios_missing_list().c_str());
        sound_play(SND_ERROR);
        show_msg(tr(T_BIOS_TITLE), std::string(m) + "\n" + tr(T_BIOS_BODY));
        return;
    }

    long long wall_start = (long long)time(nullptr);
    SDL_Delay(230);                    // let the launch chime ring out
    shutdown_gfx();                    // release DRM master
    // CLOCK_MONOTONIC, not the wall clock: gameos-sleepd suspends to RAM on lid
    // close and CLOCK_REALTIME jumps forward by the sleep time on resume.
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = launch_game(c, g);
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    // Log the session BEFORE taking the panels back: if init_gfx() fails the UI
    // exits right here, and the hour just played would otherwise never be saved.
    if (rc != 127)
        activity_record(c.id, g.title, wall_start, (long long)(t1.tv_sec - t0.tv_sec));
    if (!init_gfx()) {                 // emulator exited — take panels back
        // Graphics re-init failed (DRM still held / mode broken). Exit non-zero
        // so the session supervisor backs off instead of treating it as a clean
        // exit and spinning in a tight restart loop.
        app.running = false;
        app.exit_code = 1;
        return;
    }

    if (rc == 127) {
        sound_play(SND_ERROR);
        show_msg(tr(T_EMU_FAIL), tr(T_EMU_FAIL_BODY));
    } else if (rc != 0)
        fprintf(stderr, "gameos-ui: emulator exited with %d\n", rc);
}

void do_launch()
{
    if (app.games.empty()) return;
    launch_one(consoles()[app.carousel], app.games[app.sel]);
    if (!app.running) return;

    // Back from the emulator: refresh the cached list and keep selection in
    // range (a ROM may have been added/removed, saves/screenshots changed).
    invalidate_games(app.carousel);
    rebuild_list();
}

void go_back()
{
    if (app.screen == Screen::Home) return;
    sound_play(SND_BACK);
    switch (app.screen) {
    case Screen::List:
    case Screen::Activity:
    case Screen::Quick:
        app.screen = Screen::Home;
        break;
    case Screen::Settings:
    case Screen::Msg:
        app.screen = Screen::Home;
        break;
    case Screen::Wifi:
        if (app.wifi_kb) { kb_cancel(); break; }
        app.screen = Screen::Settings;
        break;
    case Screen::Setup:
        // B skips the current step; an open keyboard is cancelled first
        if (app.wifi_kb) { kb_cancel(); break; }
        setup_next();
        break;
    case Screen::Saves:
        app.screen = Screen::Settings;
        break;
    case Screen::Bluetooth:
        app.screen = Screen::Settings;
        break;
    case Screen::Update:
        app.screen = Screen::Settings;
        break;
    case Screen::Confirm:
        app.screen = (app.confirm == ConfirmAct::DeleteSave) ? Screen::Saves
                   : (app.confirm == ConfirmAct::Update)     ? Screen::Update
                                                             : Screen::Settings;
        app.confirm = ConfirmAct::None;
        break;
    default:
        break;
    }
}

void confirm_btn()
{
    sound_play(SND_CONFIRM);
    switch (app.screen) {
    case Screen::Home:
        enter_selection();
        break;
    case Screen::List:
        do_launch();
        break;
    case Screen::Activity: {
        std::string sys, title;
        if (!act_selected(sys, title)) break;
        QuickEntry e = resolve_entry(sys, title);
        if (e.missing) { sound_play(SND_ERROR); break; }
        int con = e.con, keep = app.act_sel;
        Game g = e.game;
        launch_one(consoles()[con], g);
        if (!app.running) break;
        invalidate_games(con);
        rebuild_activity();
        app.act_sel = std::clamp(keep, 0, std::max(0, act_row_count() - 1));
        break;
    }
    case Screen::Quick: {
        if (app.quick.empty()) break;
        if (app.quick[app.quick_sel].missing) { sound_play(SND_ERROR); break; }
        int con = app.quick[app.quick_sel].con;
        Game g = app.quick[app.quick_sel].game;
        launch_one(consoles()[con], g);
        if (!app.running) break;
        invalidate_games(con);
        rebuild_quick();
        break;
    }
    case Screen::Settings:
        if (app.set_sel == 2) bt_enter();
        else if (app.set_sel == 3) wifi_enter();
        else if (app.set_sel == 4) cycle_theme();
        else if (app.set_sel == 5) cycle_lang();
        else if (app.set_sel == 6) update_enter();
        else if (app.set_sel == 7) {
            app.saves = scan_saves();
            app.save_sel = 0;
            app.save_scroll = 0;
            app.screen = Screen::Saves;
        }
        else if (app.set_sel == 8) { app.confirm = ConfirmAct::PowerOff; app.screen = Screen::Confirm; }
        else if (app.set_sel == 9) { app.confirm = ConfirmAct::Reboot; app.screen = Screen::Confirm; }
        else if (app.set_sel == 10)
            show_msg(tr(T_ABOUT), tr(T_ABOUT_BODY));
        else if (app.set_sel == 11) setup_enter();
        break;
    case Screen::Setup:
        if (app.wifi_kb) kb_press();
        else setup_activate();
        break;
    case Screen::Wifi:
        if (app.wifi_kb) kb_press();
        else if (!app.wifi_busy) wifi_press_net();
        break;
    case Screen::Update:
        if (app.upd_busy) break;
        if (app.upd_state == "available") {
            // never write anything without an explicit second confirmation
            app.confirm = ConfirmAct::Update;
            app.screen = Screen::Confirm;
        }
        else if (app.upd_state == "ready") { app.exit_code = 91; app.running = false; }
        else update_start("check");
        break;
    case Screen::Bluetooth:
        if (app.bt_busy) break;
        if (app.bt_sel == 0) {
            app.bt_status = app.bt_on ? tr(T_TURNING_OFF) : tr(T_TURNING_ON);
            bt_start(app.bt_on ? "down" : "up");
        } else if (app.bt_sel <= (int)app.bt_devs.size()) {
            const BtDev& d = app.bt_devs[app.bt_sel - 1];
            if (!bt_mac_valid(d.mac)) break;
            if (!d.paired) { app.bt_status = tr(T_PAIRING); bt_start("pair", d.mac); }
            else if (!d.connected) { app.bt_status = tr(T_CONNECTING); bt_start("connect", d.mac); }
            else { app.bt_status = tr(T_DISCONNECTING); bt_start("disconnect", d.mac); }
        }
        break;
    case Screen::Saves:
        if (!app.saves.empty()) {
            app.save_del_idx = app.save_sel;
            app.confirm = ConfirmAct::DeleteSave;
            app.screen = Screen::Confirm;
        }
        break;
    case Screen::Confirm:
        if (app.confirm == ConfirmAct::DeleteSave) {
            if (app.save_del_idx >= 0 && app.save_del_idx < (int)app.saves.size())
                delete_save(app.saves[app.save_del_idx].path);
            app.saves = scan_saves();
            app.save_sel = std::clamp(app.save_sel, 0, std::max(0, (int)app.saves.size() - 1));
            app.confirm = ConfirmAct::None;
            app.screen = Screen::Saves;
        } else if (app.confirm == ConfirmAct::Update) {
            app.confirm = ConfirmAct::None;
            app.screen = Screen::Update;
            update_start("apply");
        } else {
            app.exit_code = (app.confirm == ConfirmAct::PowerOff) ? 90 : 91;
            app.running = false;
            SDL_Delay(250);            // let the chime finish before teardown
        }
        break;
    default:
        break;
    }
}

void start_btn()
{
    if (app.screen == Screen::Home) {
        sound_play(SND_CONFIRM);
        app.bt_on = (system("gameos-bt status 2>/dev/null | grep -q '^on$'") == 0);
        wifi_refresh_status();   // fills in the WiFi row before it is drawn
        update_refresh();        // and the update row
        app.screen = Screen::Settings;
        app.set_sel = 0;
    }
    else if (app.screen == Screen::Setup) {
        sound_play(SND_BACK);
        setup_finish();          // START skips the whole wizard
    }
    else if (app.screen != Screen::Confirm) {
        sound_play(SND_BACK);
        app.screen = Screen::Home;
    }
}

// horizontal / vertical navigation with key repeat
void navigate(int dx, int dy)
{
    if (dx || dy) {
        if (app.screen == Screen::Home && dx)          sound_play(SND_SWITCH);
        else if (app.screen == Screen::Settings && dx) sound_play(SND_ADJUST);
        else                                           sound_play(SND_MOVE);
    }
    switch (app.screen) {
    case Screen::Home:
        if (dx) app.carousel = (app.carousel + dx + CAR_N) % CAR_N;
        break;
    case Screen::List:
        if (dy && !app.games.empty())
            app.sel = std::clamp(app.sel + dy, 0, (int)app.games.size() - 1);
        break;
    case Screen::Activity:
        if (dx) {
            app.act_tab = (app.act_tab + dx + 4) % 4;
            app.act_sel = 0;
            app.act_scroll = 0;
        }
        if (dy) {
            int n = act_row_count();
            if (n > 0) app.act_sel = std::clamp(app.act_sel + dy, 0, n - 1);
        }
        break;
    case Screen::Quick:
        if (dx) {
            app.quick_tab = (app.quick_tab + dx + 2) % 2;
            app.quick_sel = 0;
            rebuild_quick();
        }
        if (dy && !app.quick.empty())
            app.quick_sel = std::clamp(app.quick_sel + dy, 0, (int)app.quick.size() - 1);
        break;
    case Screen::Settings:
        if (dy) app.set_sel = std::clamp(app.set_sel + dy, 0, 11);
        if (dx) {
            if (app.set_sel == 0) {
                app.settings.brightness = std::clamp(app.settings.brightness + dx * 5, 10, 100);
                apply_brightness(app.settings.brightness);
                save_settings(app.settings);
            } else if (app.set_sel == 1) {
                app.settings.volume = std::clamp(app.settings.volume + dx * 5, 0, 100);
                apply_volume(app.settings.volume);
                save_settings(app.settings);
            } else if (app.set_sel == 4) {
                cycle_theme(dx);
            } else if (app.set_sel == 5) {
                cycle_lang(dx);
            }
        }
        break;
    case Screen::Wifi:
        if (app.wifi_kb) {
            const auto& rows = kb_rows();
            if (dy) {
                app.kb_r = std::clamp(app.kb_r + dy, 0, (int)rows.size() - 1);
                app.kb_c = std::min(app.kb_c, (int)rows[app.kb_r].size() - 1);
            }
            if (dx) app.kb_c = std::clamp(app.kb_c + dx, 0,
                                          (int)rows[app.kb_r].size() - 1);
        } else if (dy && !app.wifi_nets.empty()) {
            app.wifi_sel = std::clamp(app.wifi_sel + dy, 0,
                                      (int)app.wifi_nets.size() - 1);
        }
        break;
    case Screen::Setup:
        if (app.wifi_kb) {
            const auto& rows = kb_rows();
            if (dy) {
                app.kb_r = std::clamp(app.kb_r + dy, 0, (int)rows.size() - 1);
                app.kb_c = std::min(app.kb_c, (int)rows[app.kb_r].size() - 1);
            }
            if (dx) app.kb_c = std::clamp(app.kb_c + dx, 0,
                                          (int)rows[app.kb_r].size() - 1);
        } else {
            setup_nav(dx, dy);
        }
        break;
    case Screen::Bluetooth:
        if (dy) app.bt_sel = std::clamp(app.bt_sel + dy, 0,
                                        app.bt_on ? (int)app.bt_devs.size() : 0);
        break;
    case Screen::Saves:
        if (dy && !app.saves.empty())
            app.save_sel = std::clamp(app.save_sel + dy, 0, (int)app.saves.size() - 1);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// main loop
// ---------------------------------------------------------------------------
void handle_button(SDL_ControllerButtonEvent& b)
{
    if (b.state != SDL_PRESSED) return;
    switch (b.button) {
    case SDL_CONTROLLER_BUTTON_B: confirm_btn(); break;   // physical A (right)
    case SDL_CONTROLLER_BUTTON_A: go_back(); break;       // physical B (bottom)
    case SDL_CONTROLLER_BUTTON_START: start_btn(); break;
    case SDL_CONTROLLER_BUTTON_X:   // physical Y (left)
        if (app.screen == Screen::Bluetooth) {
            if (app.bt_on && !app.bt_busy) { sound_play(SND_SWITCH); app.bt_status = tr(T_SCANNING); bt_start("scan"); }
        } else if (app.screen == Screen::Wifi) {
            if (!app.wifi_busy && !app.wifi_kb && app.wifi_state != "NOHW") {
                sound_play(SND_SWITCH);
                app.wifi_status = tr(T_WIFI_SCANNING);
                wifi_start("scan");
            }
        } else if (app.screen == Screen::Update) {
            if (!app.upd_busy) { sound_play(SND_SWITCH); update_start("check"); }
        } else if (app.screen == Screen::Setup) {
            if (app.setup_step == 1 && !app.wifi_busy && !app.setup_busy &&
                !app.wifi_kb && app.wifi_state != "NOHW") {
                sound_play(SND_SWITCH);
                app.wifi_status = tr(T_WIFI_SCANNING);
                wifi_start("scan");
            }
        } else refresh_games();
        break;
    case SDL_CONTROLLER_BUTTON_Y:   // physical X (top)
        if (app.screen == Screen::Bluetooth) {
            if (!app.bt_busy && app.bt_sel > 0 && app.bt_sel <= (int)app.bt_devs.size()) {
                const BtDev& d = app.bt_devs[app.bt_sel - 1];
                if (d.paired && bt_mac_valid(d.mac)) {
                    sound_play(SND_BACK);
                    app.bt_status = tr(T_FORGETTING);
                    bt_start("forget", d.mac);
                }
            }
        } else if (app.screen == Screen::Wifi) {
            if (!app.wifi_busy && !app.wifi_kb) {
                sound_play(SND_BACK);
                wifi_forget_net();
            }
        } else if (app.screen == Screen::Update) {
            // nothing to forget here
        } else toggle_fav();
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: toggle_fav_only(); break; // physical R
    case SDL_CONTROLLER_BUTTON_BACK:   // physical HOME (adc-keys-home -> BTN_BACK)
    case SDL_CONTROLLER_BUTTON_GUIDE:  // physical MENU (BTN_MODE)
        // leaving the wizard by any deliberate exit means "do not ask again"
        if (app.screen == Screen::Setup) setup_finish();
        else if (app.screen != Screen::Confirm) app.screen = Screen::Home;
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    navigate(0, -1); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  navigate(0, 1); break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  navigate(-1, 0); break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: navigate(1, 0); break;
    default: break;
    }
}

void handle_key(SDL_KeyboardEvent& k)
{
    if (k.state != SDL_PRESSED || k.repeat) return;
    switch (k.keysym.sym) {
    case SDLK_RETURN: case SDLK_z: confirm_btn(); break;
    case SDLK_ESCAPE: case SDLK_x: go_back(); break;
    case SDLK_s: start_btn(); break;
    case SDLK_r:
        if (app.screen == Screen::Bluetooth) {
            if (app.bt_on && !app.bt_busy) { sound_play(SND_SWITCH); app.bt_status = tr(T_SCANNING); bt_start("scan"); }
        } else if (app.screen == Screen::Wifi) {
            if (!app.wifi_busy && !app.wifi_kb && app.wifi_state != "NOHW") {
                sound_play(SND_SWITCH);
                app.wifi_status = tr(T_WIFI_SCANNING);
                wifi_start("scan");
            }
        } else if (app.screen == Screen::Update) {
            if (!app.upd_busy) { sound_play(SND_SWITCH); update_start("check"); }
        } else if (app.screen == Screen::Setup) {
            if (app.setup_step == 1 && !app.wifi_busy && !app.setup_busy &&
                !app.wifi_kb && app.wifi_state != "NOHW") {
                sound_play(SND_SWITCH);
                app.wifi_status = tr(T_WIFI_SCANNING);
                wifi_start("scan");
            }
        } else refresh_games();
        break;
    case SDLK_f:
        if (app.screen == Screen::Bluetooth) {
            if (!app.bt_busy && app.bt_sel > 0 && app.bt_sel <= (int)app.bt_devs.size()) {
                const BtDev& d = app.bt_devs[app.bt_sel - 1];
                if (d.paired && bt_mac_valid(d.mac)) {
                    sound_play(SND_BACK);
                    app.bt_status = tr(T_FORGETTING);
                    bt_start("forget", d.mac);
                }
            }
        } else if (app.screen == Screen::Wifi) {
            if (!app.wifi_busy && !app.wifi_kb) {
                sound_play(SND_BACK);
                wifi_forget_net();
            }
        } else if (app.screen == Screen::Update) {
            // nothing to forget here
        } else toggle_fav();
        break;
    case SDLK_TAB: toggle_fav_only(); break;
    case SDLK_UP:    navigate(0, -1); break;
    case SDLK_DOWN:  navigate(0, 1); break;
    case SDLK_LEFT:  navigate(-1, 0); break;
    case SDLK_RIGHT: navigate(1, 0); break;
    default: break;
    }
}

// read held direction from dpad + stick for auto-repeat
void poll_repeat()
{
    int dx = 0, dy = 0;
    bool from_btn = false;
    if (app.pad) {
        if (SDL_GameControllerGetButton(app.pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  { dx = -1; from_btn = true; }
        if (SDL_GameControllerGetButton(app.pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) { dx = 1;  from_btn = true; }
        if (SDL_GameControllerGetButton(app.pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    { dy = -1; from_btn = true; }
        if (SDL_GameControllerGetButton(app.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  { dy = 1;  from_btn = true; }
        Sint16 ax = SDL_GameControllerGetAxis(app.pad, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ay = SDL_GameControllerGetAxis(app.pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -16000) { dx = -1; from_btn = false; } else if (ax > 16000) { dx = 1; from_btn = false; }
        if (ay < -16000) { dy = -1; from_btn = false; } else if (ay > 16000) { dy = 1; from_btn = false; }
    }
    int dir = dy ? dy * 2 : dx; // encode: vertical takes priority
    auto step = [&]() {
        if (dir == -2) navigate(0, -1);
        else if (dir == 2) navigate(0, 1);
        else if (dir != 0) navigate(dir, 0);
    };
    Uint32 now = SDL_GetTicks();
    if (dir != app.hold_dir) {
        app.hold_dir = dir;
        app.hold_since = now;
        app.last_repeat = now;
        // D-pad first move is handled by handle_button; a stick deflection
        // emits no button event, so step now instead of a 400ms dead delay.
        if (!from_btn) step();
        return;
    }
    if (dir == 0) return;
    Uint32 delay = (now - app.hold_since < 400) ? 400 : 130;
    if (now - app.last_repeat >= delay) {
        app.last_repeat = now;
        step();
    }
}

} // namespace

int main(int, char**)
{
    setenv("SDL_VIDEODRIVER", "kmsdrm", 0);

    // Boot-splash handoff: the splash daemon has owned DRM master since
    // early boot. Ask it to cross-fade out and wait until it releases the
    // panels, otherwise SDL_Init below cannot become DRM master.
    {
        std::string pid = read_file("/run/gameos/splash.pid");
        while (!pid.empty() && (pid.back() == '\n' || pid.back() == '\r' || pid.back() == ' '))
            pid.pop_back();
        if (!pid.empty()) {
            write_file("/run/gameos/splash.stop", "1");
            for (int i = 0; i < 100; i++) {           // up to 5 s
                if (access(("/proc/" + pid).c_str(), F_OK) != 0) break;
                usleep(50 * 1000);
            }
        }
    }

    app.settings = load_settings();
    g_lang = (Lang)std::clamp(app.settings.language, 0, (int)LANG_COUNT - 1);
    app.carousel = app.settings.last_console;
    apply_theme(app.settings.theme);
    app.saves = scan_saves();
    apply_brightness(app.settings.brightness);
    // Restore the user's saved volume on every startup. S20gameos now only
    // sets a first-boot default, so without this the mixer would stay at the
    // hardware default and ignore the saved setting.
    apply_volume(app.settings.volume);
    // Bluetooth was on when the session last ended: bring the stack back up
    // (idempotent — gameos-bt up returns at once if it is already running).
    if (app.settings.bluetooth) bt_start("up");

    if (!init_gfx()) {
        shutdown_gfx();   // release any partial resources / DRM master
        return 1;
    }

    // First boot only, and only with /userdata really mounted: without it the
    // completion marker cannot be persisted and the wizard would return every
    // boot. Settings > 首次设置 runs it again on demand.
    if (userdata_mounted() && access(SETUP_MARKER, F_OK) != 0) setup_enter();

    while (app.running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                app.running = false;
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                handle_button(e.cbutton);
                break;
            case SDL_KEYDOWN:
                handle_key(e.key);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!app.pad) open_controller();
                break;
            case SDL_JOYDEVICEADDED:
                // Unmapped joysticks (our virtual pad before its mapping is
                // registered) only emit JOYDEVICEADDED, never the controller
                // variant — retry opening here too.
                if (!app.pad) open_controller();
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (app.pad) { SDL_GameControllerClose(app.pad); app.pad = nullptr; }
                open_controller();
                break;
            default:
                break;
            }
        }

        // Belt-and-braces: if the pad was missed at startup (inputd may create
        // it after SDL_Init), retry once a second until it shows up.
        if (!app.pad) {
            static Uint32 last_pad_try = 0;
            Uint32 now = SDL_GetTicks();
            if (now - last_pad_try >= 1000) {
                last_pad_try = now;
                open_controller();
            }
        }

        poll_repeat();
        bt_poll();
        wifi_poll();
        update_poll();
        tf2_poll();
        setup_poll();

        // Tell S99update the UI really got on screen: boot.scr only clears the
        // boot counter once this marker exists, so a rootfs whose session
        // never renders still rolls back after four tries.
        static Uint32 ui_t0 = SDL_GetTicks();
        static bool ui_ok = false;
        if (!ui_ok && SDL_GetTicks() - ui_t0 > 3000) {
            ui_ok = true;
            system("mkdir -p /run/gameos; : > /run/gameos/ui-ok");
        }

        SDL_Renderer* r = app.ren_top;
        switch (app.screen) {
        case Screen::Home:      draw_home(r); break;
        case Screen::List:      draw_list(r); break;
        case Screen::Settings:  draw_settings(r); break;
        case Screen::Wifi:      draw_wifi(r); break;
        case Screen::Bluetooth: draw_bluetooth(r); break;
        case Screen::Saves:     draw_saves(r); break;
        case Screen::Activity:  draw_activity(r); break;
        case Screen::Quick:     draw_quick(r); break;
        case Screen::Update:    draw_update(r); break;
        case Screen::Setup:     draw_setup(r); break;
        case Screen::Msg:       draw_msg(r); break;
        case Screen::Confirm:   draw_confirm(r); break;
        }
        SDL_RenderPresent(r);
        draw_ambient();
        SDL_Delay(1);
    }

    // Only a console tile is a "last console"; Activity/Quick/Settings would
    // otherwise be clamped onto psp and reopen there next boot.
    if (app.carousel < 5) app.settings.last_console = app.carousel;
    save_settings(app.settings);
    shutdown_gfx();
    return app.exit_code;
}