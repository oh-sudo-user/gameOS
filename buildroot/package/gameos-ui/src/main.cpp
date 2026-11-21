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
#include "displays.hpp"
#include "drawcall.hpp"
#include "gameos.hpp"
#include "i18n.hpp"
#include "perf.hpp"
#include "sound.hpp"
#include "touch.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
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

#include <SDL_image.h>

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
// The definitions live in ui.cpp; this file reads them through ui.hpp.

enum class Screen { Home, List, Settings, Wifi, Bluetooth, Saves, Update, Setup,
                    Covers, Activity, Quick, Msg, Confirm };
enum class ConfirmAct { None, PowerOff, Reboot, DeleteSave, Update, ForgetWifi, ForgetBt };

// Short stable tag for the frame profiler's log lines; keeps the perf log
// readable without depending on translated screen titles.
const char* screen_key(Screen s)
{
    switch (s) {
    case Screen::Home:      return "home";
    case Screen::List:      return "list";
    case Screen::Settings:  return "settings";
    case Screen::Wifi:      return "wifi";
    case Screen::Bluetooth: return "bt";
    case Screen::Saves:     return "saves";
    case Screen::Update:    return "update";
    case Screen::Setup:     return "setup";
    case Screen::Covers:    return "covers";
    case Screen::Activity:  return "activity";
    case Screen::Quick:     return "quick";
    case Screen::Msg:       return "msg";
    case Screen::Confirm:   return "confirm";
    }
    return "?";
}

// Every tappable region on the bottom deck. `a` (and `b`) carry the row /
// column / index the handler needs, so one id can serve a whole list.
enum HitId {
    HIT_NONE = 0,
    HIT_BACK, HIT_HOME, HIT_FAV_FILTER, HIT_PLAY, HIT_FAV, HIT_REFRESH,
    HIT_SYS,              // a = console index
    HIT_HUB,              // a = 0 activity, 1 quick, 2 settings
    HIT_RECENT,
    HIT_LIST_ROWS, HIT_LIST_ROW,        // row: a = index
    HIT_SET_TILE, HIT_SET_SLIDER,       // tile a = 0..12; slider a = 0 bright, 1 volume
    HIT_WIFI_NET, HIT_WIFI_SCAN, HIT_WIFI_TEST,
    HIT_COVER_ACTION,
    HIT_BT_TOGGLE, HIT_BT_DEV, HIT_BT_SCAN, HIT_BT_FORGET,
    HIT_SAVE_ROW, HIT_SAVE_DEL,
    HIT_UPD_CHECK, HIT_UPD_APPLY, HIT_UPD_REBOOT,
    HIT_ACT_TAB, HIT_ACT_ROWS, HIT_ACT_ROW,
    HIT_QUICK_TAB, HIT_QUICK_ROWS, HIT_QUICK_ROW,
    HIT_SETUP_OPT, HIT_SETUP_NEXT, HIT_SETUP_PREV, HIT_SETUP_ACTION,
    HIT_KB_KEY,
    HIT_DLG_OK, HIT_DLG_CANCEL
};

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
    bool keyed = false;   // saved with a non-empty passphrase
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
    TTF_Font* f_hero  = nullptr;   // Noto 50

    SDL_Texture* bg = nullptr;
    SDL_Texture* ambient = nullptr;
    SDL_Texture* logo = nullptr;
    SDL_Texture* logo_bot = nullptr;
    SDL_Texture* icons[5] = {};
    SDL_Texture* icon_settings = nullptr;
    SDL_Texture* icon_activity = nullptr;
    SDL_Texture* icon_quick = nullptr;
    // the deck has its own renderer; textures cannot cross renderers
    SDL_Texture* icons_bot[5] = {};
    SDL_Texture* icon_settings_bot = nullptr;
    SDL_Texture* icon_activity_bot = nullptr;
    SDL_Texture* icon_quick_bot = nullptr;

    SDL_GameController* pad = nullptr;

    Settings settings;
    Screen screen = Screen::Home;
    int carousel = 0;              // 0..4 consoles, 5 = settings
    std::vector<Game> games;
    int sel = 0;
    int scroll = 0;
    int set_sel = 0;
    int set_adjust = -1;           // focused slider (0 brightness, 1 volume), -1 = none
    bool fav_only = false;
    std::vector<SaveEntry> saves;
    int save_sel = 0;
    int save_scroll = 0;
    int save_del_idx = -1;
    std::string msg_title, msg_body;
    ConfirmAct confirm = ConfirmAct::None;
    std::string wifi_del_ssid, bt_del_mac;

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
    bool settings_dirty = false;       // deferred gameos.ini write (exFAT)
    Uint32 settings_dirty_at = 0;

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
    // status+known run in the background and publish files, so a slow (or
    // hung) helper can never freeze the UI thread
    bool wifi_probe_busy = false;
    bool wifi_probe_scan = false;
    Uint32 wifi_probe_at = 0;
    Uint32 wifi_probe_poll_at = 0;
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
    Uint32 cov_poll_at = 0;

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

    // motion: smoothed positions, glided by damp() each frame so lists and
    // the carousel move instead of jumping
    float car_f = 0;
    float list_sel_f = 0, list_scroll_f = 0;
    float save_sel_f = 0, save_scroll_f = 0;
    float quick_sel_f = 0, quick_scroll_f = 0;
    float act_sel_f = 0, act_scroll_f = 0;
    float set_sel_f = 0;
    float act_tab_f = 0, quick_tab_f = 0;

    // bottom-deck touch state
    int t_id = HIT_NONE;           // region held right now
    int t_a = 0, t_b = 0;
    int t_x0 = 0, t_y0 = 0, t_ly = 0;
    bool t_moved = false;
    float t_acc = 0;
    Uint32 t_at = 0;
    bool slider_drag = false;
    int slider_next_apply = 0;
    Uint32 slider_at = 0;

    // recent game card (resolved newest session, refreshed off the frame path)
    QuickEntry recent;
    bool recent_ok = false;
    Uint32 recent_at = 0;

    // volume feedback
    int vol_osd = -1;
    Uint32 vol_osd_at = 0;
    Uint32 vol_poll_at = 0;
};

App app;

// seconds since the previous frame, clamped so a stall (SD scan, resume from
// sleep) never teleports an animation
float g_dt = 0.016f;

// frame-rate independent exponential approach to a target
float damp(float cur, float target, float rate = 14.0f)
{
    float v = cur + (target - cur) * (1.0f - expf(-rate * g_dt));
    return fabsf(target - v) < 0.001f ? target : v;
}

// Screen transitions: every frame is rendered into one of two scene textures;
// on a screen change the new scene fades/slides over the previous one instead
// of hard-cutting. Dialogs pop in with a slight scale.
SDL_Texture* g_scene_tex[2] = { nullptr, nullptr };
int g_scene_cur = 0;
Screen g_last_screen = Screen::Home;
int g_last_setup_step = -1;
Uint32 g_trans_start = 0;
extern SDL_Texture* g_deck_tex[2];   // defined with the bottom deck

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
// drawing helpers (base shapes, text and formatting live in ui.cpp)
// ---------------------------------------------------------------------------

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

// (Re)load the text fonts. Noto Sans SC covers every shipped language:
// simplified/traditional Chinese, kana, Cyrillic and Latin.
void reload_fonts()
{
    clear_text_cache();   // cached textures reference the fonts closed below
    std::string noto = fonts_dir() + "/NotoSansSC-Regular.otf";
    for (TTF_Font** f : { &app.f_title, &app.f_body, &app.f_small, &app.f_hero }) {
        if (*f) TTF_CloseFont(*f);
        *f = nullptr;
    }
    app.f_title = TTF_OpenFont(noto.c_str(), 40);
    app.f_body  = TTF_OpenFont(noto.c_str(), 30);
    app.f_small = TTF_OpenFont(noto.c_str(), 22);
    app.f_hero  = TTF_OpenFont(noto.c_str(), 50);
    g_ui.logo = app.f_logo;
    g_ui.title = app.f_title;
    g_ui.body = app.f_body;
    g_ui.small = app.f_small;
    g_ui.clock = app.f_clock;
    g_ui.hero = app.f_hero;
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
    drawcall::invalidate();
    if (app.bg)      { drawcall::destroy_texture(app.bg);      app.bg = nullptr; }
    if (app.ambient) { drawcall::destroy_texture(app.ambient); app.ambient = nullptr; }
    if (app.logo)    { drawcall::destroy_texture(app.logo);    app.logo = nullptr; }
    if (app.logo_bot) { drawcall::destroy_texture(app.logo_bot); app.logo_bot = nullptr; }
    for (auto& t : app.icons) { if (t) drawcall::destroy_texture(t); t = nullptr; }
    if (app.icon_settings) { drawcall::destroy_texture(app.icon_settings); app.icon_settings = nullptr; }
    if (app.icon_activity) { drawcall::destroy_texture(app.icon_activity); app.icon_activity = nullptr; }
    if (app.icon_quick)    { drawcall::destroy_texture(app.icon_quick);    app.icon_quick = nullptr; }
    for (auto& t : app.icons_bot) { if (t) drawcall::destroy_texture(t); t = nullptr; }
    if (app.icon_settings_bot) { drawcall::destroy_texture(app.icon_settings_bot); app.icon_settings_bot = nullptr; }
    if (app.icon_activity_bot) { drawcall::destroy_texture(app.icon_activity_bot); app.icon_activity_bot = nullptr; }
    if (app.icon_quick_bot)    { drawcall::destroy_texture(app.icon_quick_bot);    app.icon_quick_bot = nullptr; }
    app.bg      = load_tex(app.ren_top, dir + "/home_bg.png");
    app.ambient = app.ren_bot ? load_tex(app.ren_bot, dir + "/ambient_bg.png") : nullptr;
    app.logo    = load_tex(app.ren_top, dir + "/logo.png");
    app.logo_bot = app.ren_bot ? load_tex(app.ren_bot, dir + "/logo.png") : nullptr;
    const char* ids[] = { "nds", "gba", "gbc", "ps1", "psp" };
    for (int i = 0; i < 5; i++)
        app.icons[i] = load_tex(app.ren_top, dir + "/console_" + ids[i] + ".png");
    app.icon_settings = load_tex(app.ren_top, dir + "/icon_settings.png");
    app.icon_activity = load_tex(app.ren_top, dir + "/icon_activity.png");
    app.icon_quick    = load_tex(app.ren_top, dir + "/icon_quick.png");
    if (app.ren_bot) {
        for (int i = 0; i < 5; i++)
            app.icons_bot[i] = load_tex(app.ren_bot, dir + "/console_" + ids[i] + ".png");
        app.icon_settings_bot = load_tex(app.ren_bot, dir + "/icon_settings.png");
        app.icon_activity_bot = load_tex(app.ren_bot, dir + "/icon_activity.png");
        app.icon_quick_bot    = load_tex(app.ren_bot, dir + "/icon_quick.png");
    }
}

// The top panel is DSI-2 on both RG DS models; panel_displays() maps the DRM
// connectors onto SDL's display indices (see displays.cpp).
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
    g_ui.logo = app.f_logo;
    g_ui.title = app.f_title;
    g_ui.body = app.f_body;
    g_ui.small = app.f_small;
    g_ui.clock = app.f_clock;
    g_ui.hero = app.f_hero;

    const Panels panels = panel_displays();
    const int dtop = panels.top;
    const int dbot = panels.bottom;
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
    touch_init();
    sound_init("/usr/share/gameos/sounds");   // silent fallback if audio fails
    return true;
}

void clear_cover_cache();   // defined with the cover helpers below
bool userdata_mounted();    // defined with the system helpers below

void shutdown_gfx()
{
    drawcall::shutdown();
    sound_quit();          // release ALSA before the emulator takes over
    touch_shutdown();
    clear_text_cache();  // text textures belong to the renderers destroyed below
    clear_cover_cache();
    for (auto& t : g_scene_tex) { if (t) SDL_DestroyTexture(t); t = nullptr; }
    for (auto& t : g_deck_tex) { if (t) SDL_DestroyTexture(t); t = nullptr; }
    if (app.pad) { SDL_GameControllerClose(app.pad); app.pad = nullptr; }
    for (auto& t : app.icons) { if (t) SDL_DestroyTexture(t); t = nullptr; }
    if (app.icon_settings) { SDL_DestroyTexture(app.icon_settings); app.icon_settings = nullptr; }
    if (app.icon_activity) { SDL_DestroyTexture(app.icon_activity); app.icon_activity = nullptr; }
    if (app.icon_quick)    { SDL_DestroyTexture(app.icon_quick);    app.icon_quick = nullptr; }
    for (auto& t : app.icons_bot) { if (t) SDL_DestroyTexture(t); t = nullptr; }
    if (app.icon_settings_bot) { SDL_DestroyTexture(app.icon_settings_bot); app.icon_settings_bot = nullptr; }
    if (app.icon_activity_bot) { SDL_DestroyTexture(app.icon_activity_bot); app.icon_activity_bot = nullptr; }
    if (app.icon_quick_bot)    { SDL_DestroyTexture(app.icon_quick_bot);    app.icon_quick_bot = nullptr; }
    if (app.bg)      { SDL_DestroyTexture(app.bg); app.bg = nullptr; }
    if (app.ambient) { SDL_DestroyTexture(app.ambient); app.ambient = nullptr; }
    if (app.logo)    { SDL_DestroyTexture(app.logo); app.logo = nullptr; }
    if (app.logo_bot) { SDL_DestroyTexture(app.logo_bot); app.logo_bot = nullptr; }
    for (TTF_Font* f : { app.f_logo, app.f_title, app.f_body, app.f_small, app.f_clock, app.f_hero })
        if (f) TTF_CloseFont(f);
    app.f_logo = app.f_title = app.f_body = app.f_small = app.f_clock = app.f_hero = nullptr;
    g_ui = UiFonts{};
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
std::string clock_hm()
{
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    return two(tmv.tm_hour) + ":" + two(tmv.tm_min);
}

void draw_topbar(SDL_Renderer* r)
{
    if (app.logo) {
        int tw = 0, th = 0;
        SDL_QueryTexture(app.logo, nullptr, nullptr, &tw, &th);
        if (th > 0) {
            SDL_Rect dst = { 48, 26, tw * 38 / th, 38 };
            draw_tex(r, app.logo, dst, 235);
        }
    } else {
        text(r, app.f_logo, "gameOS", 48, 24, C_ACCENT);
    }

    text(r, app.f_body, clock_hm(), W - 48, 28, C_TEXT, Align::Right);
    int pct = battery_percent();
    if (pct >= 0) {
        battery_icon(r, W - 172, 34, 48, 20, pct, battery_charging());
        char b[16];
        snprintf(b, sizeof(b), "%d%%", pct);
        text(r, app.f_small, b, W - 186, 28, C_DIM, Align::Right);
    }
}

// A quiet dark gradient with two accent glows and the theme art, dimmed. All
// screens share it so the palette feels like one system.
void draw_background(SDL_Renderer* r)
{
    SDL_Color top = mix_color(C_BG, rgba(255, 255, 255, 255), 0.035f);
    // The scene texture is a render target and RenderGeometry leaves its
    // alpha fractional when blended; the panel composite then dims the whole
    // screen. Lay an opaque base first so every scene stays solid.
    fill_rect(r, 0, 0, W, H, C_BG);
    grad_rect(r, { 0, 0, W, H }, top, C_BG);
    if (app.bg) {
        // home_bg.png is RGB without an alpha channel, so SDL creates the
        // texture with BLENDMODE_NONE. With alphaMod 55 a plain copy then
        // *writes* 55 into the scene's alpha instead of blending, leaving the
        // whole background translucent; composited over alternating
        // backbuffers that reads as a flicker. draw_tex forces blending.
        draw_tex(r, app.bg, SDL_Rect{ 0, 0, W, H }, 55);
    }
    glow(r, W - 120, 60, 420, g_accent, 12, 34);
    glow(r, 90, H - 40, 340, g_accent, 10, 16);
    fill_rect(r, 0, 0, W, H, { g_accent.r, g_accent.g, g_accent.b, (Uint8)std::min(g_tint_top, 8) });
}

// ---------------------------------------------------------------------------
// Shared stage for the secondary screens: one large icon, a hero line and two
// lines of context. The deck below carries the controls, so the top panel
// only has to answer "where am I and what is the state" at a glance.
// ---------------------------------------------------------------------------
void top_plate(SDL_Renderer* r, int cx, int cy, int radius)
{
    float breathe = 0.5f + 0.5f * sinf(now_sec() * 1.2f);
    glow(r, cx, cy, radius + 60 + (int)(breathe * 14), g_accent, 14, 34);
    circle(r, cx, cy, radius, rgba(255, 255, 255, 7));
    ring(r, cx, cy, radius, 1, rgba(255, 255, 255, 22));
}

void top_hero(SDL_Renderer* r, const char* kicker, const std::string& name,
              const std::string& status, SDL_Color sc,
              const std::string& detail)
{
    if (kicker && *kicker)
        text(r, app.f_small, kicker, W / 2, 452,
             with_alpha(C_ACCENT, 225), Align::Center);
    text(r, app.f_hero, ellipsize(app.f_hero, name, W - 160),
         W / 2, 482, C_TEXT, Align::Center);
    if (!status.empty())
        text(r, app.f_title, ellipsize(app.f_title, status, W - 200),
             W / 2, 566, sc, Align::Center);
    if (!detail.empty()) {
        auto lines = wrap(app.f_small, detail, W - 320);
        int y = 622;
        for (size_t i = 0; i < lines.size() && i < 2; i++) {
            text(r, app.f_small, lines[i], W / 2, y, C_DIM, Align::Center);
            y += 30;
        }
    }
}

void top_dots(SDL_Renderer* r, int count, int cur, int y)
{
    int total = 0;
    for (int i = 0; i < count; i++) total += (i == cur ? 22 : 6) + (i ? 10 : 0);
    int x = W / 2 - total / 2;
    for (int i = 0; i < count; i++) {
        bool on = i == cur;
        rrect(r, { x, y, on ? 22 : 6, 6 }, 3,
              on ? C_ACCENT : with_alpha(C_DIM, 95));
        x += (on ? 22 : 6) + 10;
    }
}

// Bounded box-art cache. A texture belongs to the renderer that created it,
// so top and deck keep separate maps (the same path can be shown on both).
std::map<SDL_Renderer*, std::map<std::string, SDL_Texture*>> g_cover_cache;
void clear_cover_cache()
{
    drawcall::invalidate();
    for (auto& rr : g_cover_cache)
        for (auto& kv : rr.second) drawcall::destroy_texture(kv.second);
    g_cover_cache.clear();
}
SDL_Texture* cover_tex(SDL_Renderer* r, const std::string& path)
{
    if (path.empty()) return nullptr;
    auto& cache = g_cover_cache[r];
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    SDL_Texture* t = load_tex(r, path);
    if (!t) return nullptr;
    if (cache.size() >= 24) {
        drawcall::invalidate();
        for (auto& kv : cache) drawcall::destroy_texture(kv.second);
        cache.clear();
    }
    cache.emplace(path, t);
    return t;
}

// ---------------------------------------------------------------------------
// screens
// ---------------------------------------------------------------------------
void draw_home(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    const auto& cons = consoles();
    const int total = CAR_N;

    app.car_f = damp(app.car_f, (float)app.carousel, 11.0f);

    // A slow halo and a frosted plate give the focused console its weight.
    float breathe = 0.5f + 0.5f * sinf(now_sec() * 1.3f);
    glow(r, W / 2, 322, 262 + (int)(breathe * 16), g_accent, 16, 42);

    for (int i = 0; i < total; i++) {
        float off = i - app.car_f;
        if (std::abs(off) > 2.05f) continue;
        float d = std::min(1.0f, std::abs(off));
        float scale = 1.0f - 0.34f * d;
        int size = (int)(300 * scale);
        int cx = W / 2 + (int)(off * 288);
        int cy = 326 + (int)(sinf(now_sec() * 0.8f + i * 0.7f) * 5.0f * (1 - d));
        Uint8 alpha = (Uint8)(255 - 145 * d);

        if (d < 0.04f) {
            SDL_Rect plate = { cx - size / 2 - 26, cy - size / 2 - 26,
                               size + 52, size + 52 };
            rrect(r, plate, 56, rgba(255, 255, 255, 7));
            rrect_stroke(r, plate, 56, rgba(255, 255, 255, 18), 1);
        }

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
            circle(r, cx, cy, size / 2 - 10, with_alpha(C_PANEL, alpha));
            text(r, app.f_title, badge, cx, cy - 24, with_alpha(C_ACCENT, alpha),
                 Align::Center);
        }
    }

    // A short accent underline sits exactly under the focused plate.
    rrect(r, { W / 2 - 36, 466, 72, 5 }, 3, C_ACCENT);

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
    text(r, app.f_hero, name, W / 2, 496, C_TEXT, Align::Center);
    text(r, app.f_small, ellipsize(app.f_small, sub, W - 200),
         W / 2, 566, C_DIM, Align::Center);

    // Carousel dots: the active one stretches, the rest stay quiet.
    int dotw = total * 16 + 8;
    int x = W / 2 - dotw / 2;
    for (int i = 0; i < total; i++) {
        bool on = i == app.carousel;
        rrect(r, { x, 634, on ? 22 : 6, 6 }, 3,
              on ? C_ACCENT : with_alpha(C_DIM, 95));
        x += (on ? 22 : 6) + 10;
    }
}

void draw_list(SDL_Renderer* r)
{
    draw_background(r);
    const Console& c = consoles()[app.carousel];

    // header: back pill, console name, count, card source
    rrect(r, { 48, 34, 52, 52 }, 26, with_alpha(C_PANEL, 220));
    rrect_stroke(r, { 48, 34, 52, 52 }, 26, rgba(255, 255, 255, 20), 1);
    icon_chevron(r, 58, 44, 32, C_TEXT, true);
    const std::string title = console_name(c.id);
    text(r, app.f_hero, title, 120, 30, C_TEXT);

    char b[96];
    snprintf(b, sizeof(b), tr(T_LIST_COUNT_FMT), app.games.size());
    std::string cnt = b;
    if (app.fav_only) cnt += tr(T_FAV_SUFFIX);
    int tw = text_width(app.f_hero, title);
    int pillw = text_width(app.f_small, cnt) + 32;
    if (120 + tw + 18 + pillw < W - 180) {
        rrect(r, { 120 + tw + 18, 52, pillw, 32 }, 16,
              app.fav_only ? with_alpha(C_ACCENT, 40) : rgba(255, 255, 255, 12));
        text(r, app.f_small, cnt, 120 + tw + 18 + pillw / 2, 54,
             app.fav_only ? C_ACCENT : C_DIM, Align::Center);
    }
    if (app.tf2_mounted) {
        std::string tf2 = tr(T_TF2_ON);
        if (!tf2.empty() && tf2[0] == ' ') tf2.erase(0, 1);
        int w = text_width(app.f_small, tf2) + 28;
        rrect(r, { W - 48 - w, 52, w, 32 }, 16, rgba(255, 255, 255, 12));
        text(r, app.f_small, tf2, W - 48 - w / 2, 54, C_DIM, Align::Center);
    }

    if (app.games.empty()) {
        bool data_err = !app.fav_only && !userdata_mounted();
        const char* head = app.fav_only ? tr(T_NO_FAV)
                         : data_err     ? tr(T_DATA_ERROR)
                                        : tr(T_NO_GAMES);
        SDL_Rect cardrc = { 212, 212, 600, 340 };
        card(r, cardrc, 32, false, 236);
        if (app.icons[app.carousel]) {
            SDL_Rect dst = { W / 2 - 42, 252, 84, 84 };
            draw_tex(r, app.icons[app.carousel], dst, 150);
        } else {
            icon_image(r, W / 2 - 32, 262, 64, C_DIM);
        }
        text(r, app.f_body, head, W / 2, 368,
             data_err ? C_AMBER : C_TEXT, Align::Center);
        std::string p;
        if (app.fav_only) p = tr(T_FAV_HELP);
        else if (data_err) p = tr(T_DATA_ERROR_HELP);
        else p = fmt(tr(T_COPY_ROMS_FMT), c.id);
        int ty = 424;
        for (const auto& l : wrap(app.f_small, p, 520)) {
            text(r, app.f_small, l, W / 2, ty, C_DIM, Align::Center);
            ty += 30;
        }
        if (!app.fav_only && !data_err) {
            for (const auto& l : wrap(app.f_small, tr(T_TF_HELP), 520)) {
                text(r, app.f_small, l, W / 2, ty + 6, with_alpha(C_DIM, 180),
                     Align::Center);
                ty += 30;
            }
        }
        return;
    }

    const int rowh = 88, gap = 10, top = 126, vis = 6;
    const int listw = 928, listx = 48;
    if (app.sel < app.scroll) app.scroll = app.sel;
    if (app.sel >= app.scroll + vis) app.scroll = app.sel - vis + 1;
    app.list_scroll_f = damp(app.list_scroll_f, (float)app.scroll);
    app.list_sel_f = damp(app.list_sel_f, (float)app.sel);

    SDL_Rect cliprc = { listx, top, listw, vis * rowh - gap };
    set_clip(r, &cliprc);

    int first = (int)floorf(app.list_scroll_f);
    for (int idx = first - 1; idx <= first + vis; idx++) {
        if (idx < 0 || idx >= (int)app.games.size()) continue;
        int y = top + (int)((idx - app.list_scroll_f) * rowh);
        bool sel = idx == app.sel;
        SDL_Rect rc = { listx, y, listw, rowh - gap };
        rrect(r, rc, 20, rgba(255, 255, 255, sel ? 16 : 7));
        rrect_stroke(r, rc, 20, rgba(255, 255, 255, sel ? 22 : 9), 1);
    }

    // focus follows the selected row, even while the list is gliding
    int fy = top + (int)((app.list_sel_f - app.list_scroll_f) * rowh);
    SDL_Rect focus = { listx, fy, listw, rowh - gap };
    card(r, focus, 20, true, 235);
    rrect(r, { focus.x + 18, focus.y + 22, 4, focus.h - 44 }, 2, C_ACCENT);

    for (int idx = first - 1; idx <= first + vis; idx++) {
        if (idx < 0 || idx >= (int)app.games.size()) continue;
        int y = top + (int)((idx - app.list_scroll_f) * rowh);
        const Game& g = app.games[idx];
        bool sel = idx == app.sel;
        bool fav = fav_contains(c.id, g.title);
        bool tf2 = on_tf2(g.path);
        int maxw = listw - 48 - 44 - (fav ? 40 : 0) - (tf2 ? 76 : 0);
        text(r, app.f_body, ellipsize(app.f_body, g.title, maxw),
             listx + 36, y + 20, sel ? C_TEXT : with_alpha(C_TEXT, 205));
        if (fav) icon_star(r, listx + listw - 84, y + 26, 32, C_AMBER, true);
        if (tf2) {
            rrect(r, { listx + listw - 62, y + 28, 42, 24 }, 12,
                  rgba(255, 255, 255, 16));
            text(r, app.f_small, "TF2", listx + listw - 41, y + 24, C_DIM,
                 Align::Center);
        }
    }
    set_clip(r, nullptr);

    if ((int)app.games.size() > vis) {
        int trackh = vis * rowh - gap;
        int barh = std::max(36, trackh * vis / (int)app.games.size());
        int bary = top + (int)((trackh - barh) * app.list_scroll_f /
                    std::max(1, (int)app.games.size() - vis));
        rrect(r, { W - 22, top, 4, trackh }, 2, rgba(255, 255, 255, 18));
        rrect(r, { W - 22, bary, 4, barh }, 2, C_ACCENT);
    }
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
    // The whole helper+rc group must be backgrounded: with "cmd; echo $? > rc &"
    // the shell only backgrounds the echo and system() waits for the helper.
    std::string cmd = "mkdir -p /run/gameos; ( /usr/bin/gameos-bt " + op;
    if (!arg.empty()) cmd += " " + arg;
    cmd += " > /run/gameos/bt-op.out 2>&1; echo $? > /run/gameos/bt-op.rc ) &";
    if (system(cmd.c_str()) != 0) { /* ignore */ }
    {
        char dbg[240];
        snprintf(dbg, sizeof dbg, "gameos-ui: bt op start: %s%s%s\n",
                 op.c_str(), arg.empty() ? "" : " ", arg.c_str());
        ui_dbg(dbg);
    }
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

std::string bt_last_line(const std::string& out)
{
    std::string last, line;
    std::istringstream ss(out);
    while (std::getline(ss, line))
        if (!line.empty() && line != "\r") last = line;
    return last;
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
    {
        char dbg[240];
        std::string last = bt_last_line(out);
        snprintf(dbg, sizeof dbg, "gameos-ui: bt op done: %s rc=%d last=%.160s\n",
                 op.c_str(), rc, last.c_str());
        ui_dbg(dbg);
    }
    if (op == "devices" || op == "scan") {
        bt_parse_devs(out);
        std::string last = bt_last_line(out);
        // A helper that failed (adapter off, discovery refused) must say so
        // instead of leaving an empty list that looks like "no devices found".
        if (rc != 0 && !last.empty()) app.bt_status = last;
        else if (op == "scan") app.bt_status = tr(T_SCAN_DONE);
    } else if (op == "up") {
        app.bt_on = (rc == 0);
        app.settings.bluetooth = app.bt_on ? 1 : 0;
        save_settings(app.settings);
        app.bt_status = app.bt_on ? tr(T_BT_ON_OK) : tr(T_BT_ON_FAIL);
        // Search straight away: the device list should not sit empty until
        // the user finds and presses scan.
        if (app.bt_on) { app.bt_status = tr(T_SCANNING); bt_start("scan"); }
    } else if (op == "down") {
        app.bt_on = false;
        app.settings.bluetooth = 0;
        save_settings(app.settings);
        app.bt_devs.clear();
        app.bt_sel = 0;
        app.bt_status = tr(T_BT_OFF_OK);
    } else {
        // pair / connect / disconnect / forget: last line "OK" or "ERROR <why>"
        std::string last = bt_last_line(out);
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
    app.bt_on = (system("/usr/bin/gameos-bt status 2>/dev/null | grep -q '^on$'") == 0);
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

// gameos-wifi known prints "SSID<TAB>1" when a passphrase is stored and
// "SSID<TAB>0" for an open network. The bare-SSID form is accepted too so an
// older helper still works.
bool wifi_saved(const std::string& ssid)
{
    for (const auto& k : app.wifi_known) {
        if (k == ssid) return true;
        if (k.size() > ssid.size() + 1 &&
            k.compare(0, ssid.size(), ssid) == 0 && k[ssid.size()] == '\t')
            return true;
    }
    return false;
}

bool wifi_keyed(const std::string& ssid)
{
    const size_t n = ssid.size();
    for (const auto& k : app.wifi_known) {
        if (k.size() == n + 2 && k.compare(0, n, ssid) == 0 &&
            k[n] == '\t' && k[n + 1] == '1')
            return true;
    }
    return false;
}

void wifi_apply_known(const std::string& out)
{
    app.wifi_known.clear();
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) app.wifi_known.push_back(line);
    }
    for (auto& n : app.wifi_nets) {
        n.saved = wifi_saved(n.ssid);
        n.keyed = wifi_keyed(n.ssid);
    }
}

void wifi_apply_status(const std::string& out)
{
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
    for (auto& n : app.wifi_nets) {
        n.saved = wifi_saved(n.ssid);
        n.keyed = wifi_keyed(n.ssid);
    }
}

void wifi_start(const std::string& op)
{
    unlink("/run/gameos/wifi-op.rc");
    // Background the helper together with its rc write (see bt_start): a bare
    // trailing "&" only backgrounds the final echo, leaving the scan/connect
    // running synchronously inside system().
    std::string cmd = "mkdir -p /run/gameos; ( /usr/bin/gameos-wifi " + op +
                      " > /run/gameos/wifi-op.out 2>&1; echo $? > /run/gameos/wifi-op.rc ) &";
    if (system(cmd.c_str()) != 0) { /* ignore */ }
    char dbg[128];
    snprintf(dbg, sizeof(dbg), "gameos-ui: wifi op start: %s\n", op.c_str());
    ui_dbg(dbg);
    app.wifi_op = op;
    app.wifi_busy = true;
    app.wifi_op_at = SDL_GetTicks();
}

// A user-requested scan supersedes the one queued by wifi_enter(); without
// this a tap during the status probe started a second scan as soon as the
// first one finished.
void wifi_scan_now()
{
    if (app.wifi_busy) return;
    app.wifi_probe_scan = false;
    app.wifi_status = tr(T_WIFI_SCANNING);
    wifi_start("scan");
}

// gameos-wifi status/known used to be popen()ed right when the wizard moved
// to the WiFi step (and when Settings opened), so a supplicant that was slow
// to come up froze the whole UI on the previous screen. They now run in a
// background shell that publishes two files plus a completion flag.
const char* WIFI_PROBE_RC = "/run/gameos/wifi-probe.rc";

void wifi_probe_start(bool scan_after = false)
{
    if (app.wifi_probe_busy) {
        if (scan_after) app.wifi_probe_scan = true;
        return;
    }
    unlink(WIFI_PROBE_RC);
    std::string cmd =
        "mkdir -p /run/gameos; ( /usr/bin/gameos-wifi status 2>/dev/null > /run/gameos/wifi-status.out; "
        "/usr/bin/gameos-wifi known 2>/dev/null > /run/gameos/wifi-known.out; echo 1 > "
        + std::string(WIFI_PROBE_RC) + " ) >/dev/null 2>&1 &";
    if (system(cmd.c_str()) != 0) { /* ignore */ }
    app.wifi_probe_busy = true;
    app.wifi_probe_scan = scan_after;
    app.wifi_probe_at = SDL_GetTicks();
    app.wifi_probe_poll_at = 0;
}

void wifi_probe_poll(Uint32 now)
{
    if (!app.wifi_probe_busy) return;
    const bool done = access(WIFI_PROBE_RC, F_OK) == 0;
    const bool timeout = now - app.wifi_probe_at > 15000;
    if (!done && !timeout) return;
    if (now - app.wifi_probe_poll_at < 100 && !timeout) return;
    app.wifi_probe_poll_at = now;
    app.wifi_probe_busy = false;
    wifi_apply_status(read_file("/run/gameos/wifi-status.out"));
    wifi_apply_known(read_file("/run/gameos/wifi-known.out"));
    // back off when the helper hung; the periodic poll retries later
    app.wifi_status_at = done ? now : now + 8000;
    const bool scan = app.wifi_probe_scan;
    app.wifi_probe_scan = false;
    if (scan) {
        if (app.wifi_state == "NOHW") {
            // the module may still be loading at cold boot: keep the request
            // pending so the scan starts as soon as the interface appears
            app.wifi_status.clear();
            app.wifi_probe_scan = true;
        } else if (!app.wifi_busy) {
            app.wifi_status = tr(T_WIFI_SCANNING);
            wifi_start("scan");
        }
    }
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
        n.keyed = wifi_keyed(n.ssid);
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
    // A secured network needs a real key. An entry saved with an empty
    // passphrase (legacy failed attempt) is not usable: prompt again instead
    // of silently connecting with key_mgmt=NONE and flipping back with an
    // error. A saved network whose key is known reconnects without asking.
    if (n.secure && !n.keyed) {
        app.wifi_target = n.ssid;
        app.wifi_pw.clear();
        // the shared keyboard outlives wizard fields: reset every mode flag so
        // a stale generic target cannot swallow the keystrokes
        app.kb_generic = false;
        app.kb_buf = nullptr;
        app.kb_title = tr(T_WIFI_PASSWORD);
        app.kb_limit = 63;
        app.kb_empty_ok = false;
        app.kb_mask = true;
        app.wifi_kb = true;
        app.kb_r = 1; app.kb_c = 0; app.kb_shift = 0;
    } else {
        wifi_begin_connect(n.ssid, "");  // open network or saved key
    }
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
    wifi_probe_poll(now);
    // A completed probe can start a scan, stamping wifi_op_at after the `now`
    // captured above; comparing against the stale value underflows and the
    // 60 s timeout would fire immediately.
    now = SDL_GetTicks();
    // live link status while the page is open; the probe itself is async
    if ((app.screen == Screen::Wifi ||
         (app.screen == Screen::Setup && app.setup_step == 1)) &&
        !app.wifi_busy && !app.wifi_probe_busy &&
        now - app.wifi_status_at > 2000) {
        wifi_probe_start();
    }
    if (!app.wifi_busy) return;
    if (now - app.wifi_op_at > 45000) {   // scan/assoc timeout
        app.wifi_busy = false;
        char dbg[96];
        snprintf(dbg, sizeof(dbg), "gameos-ui: wifi op %s timeout\n",
                 app.wifi_op.c_str());
        ui_dbg(dbg);
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
    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg), "gameos-ui: wifi op %s rc=%d out=%.120s\n",
                 op.c_str(), rc, out.c_str());
        ui_dbg(dbg);
    }
    if (op == "scan") {
        if (out.rfind("ERROR", 0) == 0) {
            // Never show a stale list as if the scan had worked, and say why.
            app.wifi_nets.clear();
            app.wifi_sel = 0;
            app.wifi_scroll = 0;
            if (out.find("no-wifi-hardware") != std::string::npos) {
                app.wifi_state = "NOHW";
                app.wifi_status = tr(T_WIFI_UNAVAILABLE);
            } else if (out.find("supplicant-failed") != std::string::npos)
                app.wifi_status = tr(T_WIFI_SUPPLICANT_FAIL);
            else
                app.wifi_status = tr(T_WIFI_SCAN_FAIL);
        } else {
            wifi_parse_nets(out);
            app.wifi_status = app.wifi_nets.empty() ? tr(T_WIFI_NO_NETS) : "";
        }
        wifi_probe_start();           // refresh saved markers off-thread
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
        wifi_probe_start();
    } else if (op == "forget") {
        app.wifi_status = tr(T_WIFI_FORGET_DONE);
        wifi_probe_start(true);       // refresh known, then rescan
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
    app.wifi_status = tr(T_WIFI_SCANNING);
    wifi_probe_start(true);           // status/known, then a scan if there is hardware
    app.screen = Screen::Wifi;
}

// ---------------------------------------------------------------------------
// first-boot wizard
//
// Six questions then a done page. A (right button) acts on the highlighted row,
// B (bottom button) goes back a step, START skips the whole wizard. The
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
    // Background the helper together with its rc write (see bt_start): covers
    // downloads run for minutes and must never block the wizard.
    std::string full = "mkdir -p /run/gameos; ( " + cmd +
        " > " + base + ".out 2>&1; echo $? > " + base + ".rc ) &";
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
    // Leaving a step while its op is in flight: the script still runs to
    // completion and persists the choice, but its result must not land on the
    // next step (a timezone result would otherwise advance twice).
    app.setup_busy = false;
    app.setup_op.clear();
    if (app.setup_step >= SETUP_STEPS) { setup_finish(); return; }
    if (app.setup_step == 1) {
        wifi_probe_start(true);       // status/known, then a scan if there is hardware
    } else if (app.setup_step == 5) {
        unlink("/run/gameos/covers-state");
        app.cov_state.clear();
        app.cov_current.clear();
        app.cov_total = app.cov_done = app.cov_ok = app.cov_miss = app.cov_fail = 0;
        setup_run("covers-count", "gameos-covers count");
    }
}

void setup_prev()
{
    if (app.setup_step <= 0) return;
    app.setup_step--;
    app.setup_sel = 0;
    app.setup_scroll = 0;
    app.setup_col = 0;
    app.setup_status.clear();
    app.setup_busy = false;
    app.setup_op.clear();

    if (app.setup_step == 1) {
        wifi_probe_start(true);        // refresh status/known (and scan if hardware)
    } else if (app.setup_step == 2) {
        app.setup_sel = std::clamp(app.setup_region, 0,
                                   (int)tz_regions().size() - 1);   // focus current region
    } else if (app.setup_step == 5 && app.cov_total == 0) {
        setup_run("covers-count", "gameos-covers count");
    }
}

// The wizard's cover step and the Settings cover page read the same file.
void covers_load_state()
{
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
    if (app.setup_step == 5)
        covers_load_state();

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
            app.setup_status = fmt(tr(T_RA_OK_FMT), app.setup_ra_name.c_str());
        } else {
            std::string why = last.rfind("ERROR ", 0) == 0 ? last.substr(6) : last;
            if (why.empty()) why = "login-failed";
            app.setup_status = fmt(tr(T_RA_FAIL_FMT), why.c_str());
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
        // no font reload: every language shares NotoSansSC, and reopening it
        // here was the stall felt on the wizard's first choice
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
// box-art scraping from Settings (same helper and state file as the wizard)
// ---------------------------------------------------------------------------
void covers_enter()
{
    app.cov_state.clear();
    app.cov_current.clear();
    app.cov_total = app.cov_done = app.cov_ok = app.cov_miss = app.cov_fail = 0;
    app.setup_status.clear();
    app.setup_busy = false;
    app.setup_op.clear();
    app.screen = Screen::Covers;

    // A run started from the wizard may still be downloading. Adopt it from
    // the state file instead of counting: count rewrites the same /run lists
    // the running downloader is reading.
    covers_load_state();
    if (app.cov_state == "running") return;
    setup_run("covers-count", "gameos-covers count");
}

void covers_toggle()
{
    bool running = app.cov_state == "running" ||
                   (app.setup_busy && app.setup_op == "covers");
    if (running) {
        FILE* f = fopen("/run/gameos/covers.stop", "w");
        if (f) fclose(f);
        sound_play(SND_BACK);
        return;
    }
    if (app.setup_busy) return;           // the count is still in flight
    unlink("/run/gameos/covers-state");
    app.cov_state.clear();
    app.cov_current.clear();
    app.cov_total = app.cov_done = app.cov_ok = app.cov_miss = app.cov_fail = 0;
    app.setup_status.clear();
    setup_run("covers", "gameos-covers run");
    sound_play(SND_CONFIRM);
}

// Runs on every screen (except the wizard, whose setup_poll owns the op):
// entering this page while a wizard download is still going shows its
// progress, and its completion refreshes the cached cover textures.
void covers_poll()
{
    if (app.screen == Screen::Setup) return;
    Uint32 now = SDL_GetTicks();
    if (now - app.cov_poll_at < 250) return;
    app.cov_poll_at = now;

    std::string prev = app.cov_state;
    covers_load_state();
    if (prev == "running" && app.cov_state != "running") {
        clear_cover_cache();
        // a run adopted from the wizard has no op result to read; report the
        // outcome from the state file so leaving/returning still feels final
        if (!app.setup_busy) {
            char b[256];
            if (app.cov_state == "stopped")
                snprintf(b, sizeof(b), tr(T_COVER_PROGRESS_FMT),
                         app.cov_done, app.cov_total, app.cov_ok, app.cov_miss);
            else
                snprintf(b, sizeof(b), tr(T_COVER_DONE_FMT),
                         app.cov_ok, app.cov_miss);
            app.setup_status = b;
        }
    }

    if (!app.setup_busy) return;
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

    if (op == "covers-count") {
        if (rc != 0) {
            app.setup_status = tr(T_OP_FAIL);
        } else {
            app.cov_total = atoi(last.c_str());
            if (app.cov_total == 0) app.setup_status = tr(T_COVER_NONE);
        }
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

// ---------------------------------------------------------------------------
// wizard drawing
// ---------------------------------------------------------------------------

void setup_scroll_into_view(int vis)
{
    if (app.setup_sel < app.setup_scroll) app.setup_scroll = app.setup_sel;
    if (app.setup_sel >= app.setup_scroll + vis)
        app.setup_scroll = app.setup_sel - vis + 1;
    if (app.setup_scroll < 0) app.setup_scroll = 0;
}

void draw_setup(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    const int step = app.setup_step;
    // the last page is the "done" screen, not a question: clamp so it shows
    // the final question's number instead of "7 / 6"
    int shown = std::min(step + 1, SETUP_STEPS - 1);
    char sb[64];
    snprintf(sb, sizeof(sb), tr(T_SETUP_STEP_FMT), shown, SETUP_STEPS - 1);
    std::string kick = std::string(tr(T_SETUP)) + "  ·  " + sb;

    std::string name, status, detail;
    SDL_Color sc = C_DIM;
    top_plate(r, W / 2, 290, 142);

    switch (step) {
    case 0:
        icon_globe(r, W / 2 - 62, 228, 124, C_ACCENT);
        name = lang_name(app.settings.language);
        status = tr(T_LANGUAGE);
        sc = with_alpha(C_TEXT, 235);
        detail = tr(T_SETUP_TITLE);
        break;
    case 1: {
        bool connected = !app.wifi_ip.empty();
        bool nohw = app.wifi_state == "NOHW";
        bool busy = app.wifi_busy;
        bool prompt = app.wifi_kb && !app.kb_generic;
        icon_wifi(r, W / 2 - 62, 228, 124,
                  prompt ? C_ACCENT
                         : connected ? C_ACCENT : with_alpha(C_DIM, 210),
                  prompt || connected ? 4 : 3);
        if (prompt) {
            name = tr(T_WIFI_PASSWORD);
            status = app.wifi_target;
            sc = C_ACCENT;
            detail = std::string(app.wifi_pw.size(), '*') + "   " + tr(T_KB_DECK_HINT);
            break;
        }
        name = connected && !app.wifi_link.empty() ? app.wifi_link : tr(T_WIFI);
        if (nohw) {
            status = tr(T_WIFI_UNAVAILABLE); sc = C_AMBER;
        } else if (connected) {
            status = fmt(tr(T_WIFI_CONNECTED_FMT), app.wifi_ip.c_str()); sc = C_GREEN;
        } else if (busy) {
            status = tr(T_PROCESSING); sc = C_AMBER;
        } else {
            status = tr(T_WIFI_DISCONNECTED);
        }
        if (!busy) detail = app.wifi_status;
        break;
    }
    case 2: {
        icon_clock(r, W / 2 - 62, 228, 124, C_ACCENT);
        const auto& zs = tz_regions()[app.setup_region].zones;
        std::string preview = app.setup_col == 0
            ? zs[0] : zs[std::clamp(app.setup_sel, 0, (int)zs.size() - 1)];
        name = zone_time(preview);
        status = app.setup_tz.empty() ? preview : app.setup_tz;
        sc = C_ACCENT;
        break;
    }
    case 3:
        icon_keyboard(r, W / 2 - 62, 228, 124, C_ACCENT);
        name = app.setup_name.empty() ? tr(T_SETUP_NAME_TITLE) : app.setup_name;
        status = "";
        detail = tr(T_SETUP_NAME_HELP);
        break;
    case 4:
        icon_trophy(r, W / 2 - 62, 228, 124, C_ACCENT);
        name = app.setup_ra_logged ? app.setup_ra_name
               : app.setup_ra_user.empty() ? tr(T_RA_LOGIN) : app.setup_ra_user;
        status = "RetroAchievements";
        detail = tr(T_SETUP_RA_HELP);
        break;
    case 5: {
        icon_image(r, W / 2 - 62, 228, 124,
                   app.cov_total > 0 ? C_ACCENT : with_alpha(C_DIM, 210));
        bool running = app.setup_busy && app.setup_op == "covers";
        if (app.cov_total > 0) {
            int pct = app.cov_done * 100 / app.cov_total;
            char pb[16];
            snprintf(pb, sizeof(pb), "%d%%", pct);
            name = pb;
            if (running) {
                status = tr(T_COVER_RUNNING); sc = C_AMBER;
            } else {
                char b[192];
                snprintf(b, sizeof(b), tr(T_COVER_PROGRESS_FMT),
                         app.cov_done, app.cov_total, app.cov_ok, app.cov_miss);
                status = b; sc = C_ACCENT;
            }
            detail = app.cov_current;
            SDL_Rect track = { 262, 688, 500, 10 };
            rrect(r, track, 5, with_alpha(C_DIM, 70));
            if (pct > 0)
                rrect(r, { track.x, track.y, 500 * pct / 100, 10 }, 5, C_ACCENT);
        } else {
            name = tr(T_COVERS);
            status = app.setup_busy ? tr(T_PROCESSING) : tr(T_COVER_NONE);
            detail = tr(T_SETUP_COVER_HELP);
        }
        break;
    }
    default:
        icon_check(r, W / 2 - 62, 228, 124, C_GREEN);
        name = tr(T_SETUP_DONE_TITLE);
        sc = C_GREEN;
        detail = tr(T_SETUP_DONE_BODY);
        break;
    }
    top_hero(r, kick.c_str(), name, status, sc, detail);
    top_dots(r, 6, std::min(step, 5), 712);
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
    draw_topbar(r);

    top_plate(r, W / 2, 290, 142);
    icon_download(r, W / 2 - 62, 228, 124,
                  app.upd_busy ? with_alpha(C_ACCENT, 170) : C_ACCENT);

    std::string status, detail;
    SDL_Color sc = C_DIM;
    char b[192];
    bool downloading = app.upd_busy && app.upd_op == "apply" &&
                       app.upd_state == "downloading";
    if (downloading) {
        snprintf(b, sizeof(b), tr(T_UPDATE_DOWNLOADING), app.upd_pct);
        status = b; sc = C_ACCENT;
    } else if (app.upd_busy && app.upd_op == "apply") {
        status = tr(T_UPDATE_INSTALLING); sc = C_AMBER;
    } else if (app.upd_busy) {
        status = tr(T_UPDATE_CHECKING); sc = C_DIM;
    } else if (app.upd_state == "available") {
        snprintf(b, sizeof(b), tr(T_UPDATE_AVAILABLE), app.upd_version.c_str());
        status = b; sc = C_GREEN;
        detail = tr(T_UPDATE_CONFIRM_BODY);
    } else if (app.upd_state == "uptodate") {
        status = tr(T_UPDATE_UPTODATE); sc = C_GREEN;
    } else if (app.upd_state == "ready") {
        status = tr(T_UPDATE_READY); sc = C_GREEN;
    } else if (app.upd_state == "nowifi") {
        status = tr(T_UPDATE_NOWIFI); sc = C_AMBER;
    } else if (app.upd_state == "failed") {
        snprintf(b, sizeof(b), tr(T_UPDATE_FAILED),
                 app.upd_msg.empty() ? "?" : app.upd_msg.c_str());
        status = b; sc = C_RED;
    } else {
        status = tr(T_UPD_CHECK);
    }

    char vb[128] = { 0 };
    if (!app.upd_cur.empty())
        snprintf(vb, sizeof(vb), tr(T_UPDATE_CURRENT), app.upd_cur.c_str());
    top_hero(r, tr(T_UPDATE), vb[0] ? vb : tr(T_UPDATE), status, sc, detail);

    if (downloading) {
        SDL_Rect bar = { 262, 660, 500, 10 };
        rrect(r, bar, 5, with_alpha(C_DIM, 70));
        if (app.upd_pct > 0)
            rrect(r, { bar.x, bar.y, 500 * app.upd_pct / 100, 10 }, 5, C_ACCENT);
    }
}

void draw_wifi(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    // The keyboard lives on the deck; mirror the prompt on the top panel so
    // it is obvious that the tap opened a password entry instead of doing
    // nothing (or looking like a jump back to Settings).
    if (app.wifi_kb && !app.kb_generic) {
        top_plate(r, W / 2, 290, 142);
        icon_keyboard(r, W / 2 - 62, 228, 124, C_ACCENT);
        std::string masked(app.wifi_pw.size(), '*');
        if (masked.empty()) masked = "· · ·";
        top_hero(r, tr(T_WIFI), tr(T_WIFI_PASSWORD), app.wifi_target,
                 C_ACCENT,
                 masked + "   " + std::string(tr(T_KB_DECK_HINT)));
        return;
    }

    bool connected = !app.wifi_ip.empty();
    bool nohw = app.wifi_state == "NOHW";
    bool busy = app.wifi_busy;
    top_plate(r, W / 2, 290, 142);
    icon_wifi(r, W / 2 - 62, 228, 124,
              connected ? C_ACCENT : with_alpha(C_DIM, 210),
              connected ? 4 : 3);

    std::string kicker, status;
    SDL_Color sc = C_DIM;
    if (nohw) {
        status = tr(T_WIFI_UNAVAILABLE); sc = C_AMBER;
    } else if (connected) {
        char b[64];
        snprintf(b, sizeof(b), tr(T_WIFI_CONNECTED_FMT), app.wifi_ip.c_str());
        status = b; sc = C_GREEN;
        kicker = app.wifi_link;
    } else if (busy) {
        status = tr(T_PROCESSING); sc = C_AMBER;
    } else {
        status = tr(T_WIFI_DISCONNECTED);
    }
    std::string detail = busy ? std::string() : app.wifi_status;
    top_hero(r, tr(T_WIFI), tr(T_WIFI), status, sc, detail);
}

void draw_covers(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    bool running = app.cov_state == "running" ||
                   (app.setup_busy && app.setup_op == "covers");
    bool counting = app.setup_busy && app.setup_op == "covers-count";
    top_plate(r, W / 2, 290, 142);
    icon_image(r, W / 2 - 62, 228, 124,
               app.cov_total > 0 ? C_ACCENT : with_alpha(C_DIM, 210));

    std::string name, status, detail;
    SDL_Color sc = C_DIM;
    if (app.cov_total > 0) {
        int pct = app.cov_done * 100 / app.cov_total;
        char pb[16], b[192];
        snprintf(pb, sizeof(pb), "%d%%", pct);
        name = pb;
        if (running) {
            status = tr(T_COVER_RUNNING); sc = C_AMBER;
        } else {
            snprintf(b, sizeof(b), tr(T_COVER_PROGRESS_FMT),
                     app.cov_done, app.cov_total, app.cov_ok, app.cov_miss);
            status = b; sc = C_ACCENT;
        }
        detail = app.cov_current;
        SDL_Rect track = { 262, 688, 500, 10 };
        rrect(r, track, 5, with_alpha(C_DIM, 70));
        if (pct > 0)
            rrect(r, { track.x, track.y, 500 * pct / 100, 10 }, 5, C_ACCENT);
    } else {
        name = tr(T_COVERS);
        if (counting) status = tr(T_PROCESSING);
        else if (!app.setup_status.empty()) status = app.setup_status;
        else status = tr(T_COVER_IDLE);
        detail = tr(T_SETUP_COVER_HELP);
    }
    top_hero(r, tr(T_COVERS), name, status, sc, detail);
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
    draw_topbar(r);

    top_plate(r, W / 2, 290, 142);
    icon_bt(r, W / 2 - 62, 228, 124,
            app.bt_on ? C_ACCENT : with_alpha(C_DIM, 210));

    std::string status = app.bt_on ? tr(T_ENABLED) : tr(T_DISABLED);
    SDL_Color sc = app.bt_on ? C_GREEN : C_DIM;
    std::string detail;
    if (app.bt_busy) {
        status = tr(T_PROCESSING);
        sc = C_AMBER;
    } else if (!app.bt_status.empty()) {
        detail = app.bt_status;
    } else if (app.bt_on && app.bt_devs.empty()) {
        detail = tr(T_NO_DEVS);
    }
    top_hero(r, tr(T_BT), tr(T_BT), status, sc, detail);
}

// defined with the deck tiles; declared here so the top stage can share them
const char* settings_label(int i);
std::string settings_value(int i);
void settings_icon(SDL_Renderer* r, int idx, int x, int y, int size, SDL_Color c);

void draw_settings(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    app.set_sel_f = damp(app.set_sel_f, (float)app.set_sel);
    top_plate(r, W / 2, 290, 142);
    settings_icon(r, app.set_sel, W / 2 - 62, 228, 124, C_ACCENT);

    SDL_Color vc = C_TEXT;
    if (app.set_sel == 2) vc = app.bt_on ? C_GREEN : C_DIM;
    if (app.set_sel == 3) vc = app.wifi_link.empty() ? C_DIM : C_GREEN;
    if (app.set_sel == 4 || app.set_sel == 5) vc = C_ACCENT;
    if (app.set_sel == 8) vc = C_RED;
    if (app.set_sel == 12)
        vc = (app.cov_state == "running" ||
              (app.setup_busy && app.setup_op == "covers")) ? C_AMBER : C_DIM;
    top_hero(r, tr(T_SETTINGS), settings_label(app.set_sel),
             settings_value(app.set_sel), vc, "");

    if (app.set_sel == 0 || app.set_sel == 1) {
        int pct = app.set_sel == 0 ? app.settings.brightness : app.settings.volume;
        SDL_Rect track = { 262, 648, 500, 10 };
        rrect(r, track, 5, with_alpha(C_DIM, 70));
        if (pct > 0) rrect(r, { track.x, track.y, 500 * pct / 100, 10 }, 5, C_ACCENT);
        circle(r, track.x + 500 * pct / 100, track.y + 5, 11, C_TEXT);
    }
    top_dots(r, 13, app.set_sel, 712);
}

void draw_saves(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    top_plate(r, W / 2, 290, 142);
    icon_clock(r, W / 2 - 62, 228, 124,
               app.saves.empty() ? with_alpha(C_DIM, 210) : C_ACCENT);

    char b[64];
    snprintf(b, sizeof(b), tr(T_SAVE_COUNT_FMT), app.saves.size());
    top_hero(r, tr(T_SAVES), tr(T_SAVES), b,
             app.saves.empty() ? C_DIM : C_GREEN, tr(T_SAVES_HELP));
}

// ---------------------------------------------------------------------------
// activity statistics / favorites / history
// ---------------------------------------------------------------------------
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
    draw_topbar(r);

    const char* tabs[4] = { tr(T_TAB_OVERVIEW), tr(T_TAB_TIMELINE),
                            tr(T_TAB_TREND), tr(T_TAB_PICKS) };
    bool has = !app.act_games.empty();
    top_plate(r, W / 2, 290, 142);
    icon_trophy(r, W / 2 - 62, 228, 124,
                has ? C_ACCENT : with_alpha(C_DIM, 210));

    long long today = app.act_daily.empty() ? 0 : app.act_daily.back();
    long long week = 0;
    for (int i = std::max(0, (int)app.act_daily.size() - 7);
         i < (int)app.act_daily.size(); i++)
        week += app.act_daily[i];
    // act_sessions is capped at the newest 200 for browsing; the total has to
    // come from the per-game aggregates, which are not capped.
    long long sess_total = 0;
    for (const auto& g : app.act_games) sess_total += g.sessions;

    std::string status, detail;
    SDL_Color sc = C_DIM;
    if (has) {
        char s1[64], s2[64];
        snprintf(s1, sizeof(s1), tr(T_ACT_SESSIONS_FMT), (int)sess_total);
        snprintf(s2, sizeof(s2), tr(T_ACT_GAMES_FMT), app.act_games.size());
        status = std::string(s1) + "  ·  " + s2;
        sc = with_alpha(C_TEXT, 235);
        if (app.act_tab == 0) {
            detail = std::string(tr(T_ACT_WEEK)) + " " + fmt_duration(week) +
                     "   ·   " + tr(T_ACT_TODAY) + " " + fmt_duration(today);
        }
    } else {
        status = tr(T_ACT_EMPTY);
    }
    // The trend tab answers with a 14-day sparkline instead of prose.
    long long mx = 0;
    for (long long v : app.act_daily) mx = std::max(mx, v);
    if (app.act_tab == 2 && has && mx == 0) detail = tr(T_ACT_NO_TREND);
    top_hero(r, tabs[app.act_tab], fmt_duration(app.act_total), status, sc, detail);

    if (app.act_tab == 2 && has && mx > 0 && !app.act_daily.empty()) {
        const int n = (int)app.act_daily.size();
        const int cw = 720, x0 = (W - cw) / 2, base = 694, ch = 52;
        int slot = cw / n, bw = std::max(8, slot - 10);
        for (int i = 0; i < n; i++) {
            int x = x0 + i * slot + (slot - bw) / 2;
            int h = (int)(ch * app.act_daily[i] / mx);
            rrect(r, { x, base - ch, bw, ch }, 4, with_alpha(C_DIM, 45));
            if (h > 0)
                rrect(r, { x, base - h, bw, h }, 4,
                      i == n - 1 ? C_ACCENT : with_alpha(C_ACCENT, 165));
        }
        fill_rect(r, x0, base + 1, cw, 1, with_alpha(C_DIM, 130));
    }
    top_dots(r, 4, app.act_tab, 720);
}

void draw_quick(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    bool fav = app.quick_tab == 0;
    bool has = !app.quick.empty();
    top_plate(r, W / 2, 290, 142);
    icon_star(r, W / 2 - 62, 228, 124,
              has ? (fav ? C_AMBER : C_ACCENT) : with_alpha(C_DIM, 210), fav);

    std::string hero, status, detail;
    SDL_Color sc = C_DIM;
    if (has) {
        const QuickEntry& e = app.quick[app.quick_sel];
        hero = e.game.title;
        char b[96];
        snprintf(b, sizeof(b), tr(T_QUICK_COUNT_FMT), app.quick.size());
        status = std::string(tr(fav ? T_TAB_FAV : T_TAB_HISTORY)) + "  ·  " + b;
        sc = with_alpha(C_TEXT, 235);
        detail = console_badge(e.sys);
        if (e.missing) {
            detail += "  ·  " + std::string(tr(T_QUICK_MISSING));
        } else if (e.last > 0) {
            detail += "  ·  " + fmt_when(e.last) + "  ·  " + fmt_duration(e.seconds);
        }
    } else {
        hero = tr(fav ? T_QUICK_EMPTY_FAV : T_QUICK_EMPTY_HIST);
        detail = tr(fav ? T_QUICK_EMPTY_FAV_HELP : T_QUICK_EMPTY_HIST_HELP);
    }
    top_hero(r, tr(T_QUICK), hero, status, sc, detail);
    top_dots(r, 2, app.quick_tab, 712);
}

void draw_msg(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, H, { C_BG.r, C_BG.g, C_BG.b, 185 });
    int pw = 820, ph = 460, px = (W - pw) / 2, py = (H - ph) / 2;
    glow(r, W / 2, py + ph / 2, 380, C_AMBER, 12, 18);
    card(r, { px, py, pw, ph }, 36, false, 246);

    const int cx = W / 2, cy = py + 86;
    circle(r, cx, cy, 44, with_alpha(C_AMBER, 26));
    ring(r, cx, cy, 44, 1, with_alpha(C_AMBER, 110));
    text(r, app.f_hero, "!", cx, cy - 36, C_AMBER, Align::Center);

    text(r, app.f_title, ellipsize(app.f_title, app.msg_title, pw - 100),
         cx, py + 152, C_TEXT, Align::Center);
    auto lines = wrap(app.f_body, app.msg_body, pw - 140);
    // the body must stay above the back hint; overlong errors are cut, not
    // allowed to draw over it
    const size_t maxlines = 4;
    int y = py + 212;
    for (size_t i = 0; i < lines.size() && i < maxlines; i++) {
        std::string l = lines[i];
        if (i + 1 == maxlines && lines.size() > maxlines)
            l = ellipsize(app.f_body, l + "…", pw - 140);
        text(r, app.f_body, l, cx, y, C_TEXT, Align::Center);
        y += 42;
    }
    text(r, app.f_small, tr(T_BACK), cx, py + ph - 52,
         with_alpha(C_DIM, 200), Align::Center);
}

void draw_confirm(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, H, { C_BG.r, C_BG.g, C_BG.b, 185 });
    int pw = 680, ph = 340, px = (W - pw) / 2, py = (H - ph) / 2;

    bool update = app.confirm == ConfirmAct::Update;
    bool forget = app.confirm == ConfirmAct::ForgetWifi ||
                  app.confirm == ConfirmAct::ForgetBt;
    SDL_Color ac = forget ? C_AMBER
                 : update ? C_ACCENT
                 : app.confirm == ConfirmAct::Reboot ? C_AMBER : C_RED;
    glow(r, W / 2, py + 64, 300, ac, 10, 16);
    card(r, { px, py, pw, ph }, 36, false, 246);

    const int cx = W / 2, cy = py + 64;
    circle(r, cx, cy, 42, with_alpha(ac, 26));
    ring(r, cx, cy, 42, 1, with_alpha(ac, 120));
    if (update) icon_download(r, cx - 22, cy - 22, 44, ac);
    else if (app.confirm == ConfirmAct::ForgetWifi) icon_wifi(r, cx - 22, cy - 22, 44, ac);
    else if (app.confirm == ConfirmAct::ForgetBt) icon_bt(r, cx - 22, cy - 22, 44, ac);
    else if (app.confirm == ConfirmAct::Reboot) icon_refresh(r, cx - 22, cy - 22, 44, ac);
    else if (app.confirm == ConfirmAct::PowerOff) icon_power(r, cx - 22, cy - 22, 44, ac);
    else icon_trash(r, cx - 22, cy - 22, 44, ac);

    std::string title, body;
    if (update) {
        // second confirmation before anything is downloaded or written
        title = fmt(tr(T_UPDATE_CONFIRM), app.upd_version.c_str());
        body = tr(T_UPDATE_CONFIRM_BODY);
    } else {
        title = forget ? tr(T_CONFIRM_FORGET)
              : (app.confirm == ConfirmAct::PowerOff) ? tr(T_CONFIRM_OFF)
              : (app.confirm == ConfirmAct::Reboot) ? tr(T_CONFIRM_REBOOT)
              : tr(T_CONFIRM_DEL);
        if (forget) {
            const std::string& n = app.confirm == ConfirmAct::ForgetWifi
                                 ? app.wifi_del_ssid : app.bt_del_mac;
            body = n;
        }
    }
    text(r, app.f_title, ellipsize(app.f_title, title, pw - 64),
         cx, py + 122, C_TEXT, Align::Center);
    if (!body.empty()) {
        auto lines = wrap(app.f_small, body, pw - 112);
        const size_t maxlines = 2;
        int y = py + 184;
        for (size_t i = 0; i < lines.size() && i < maxlines; i++) {
            std::string l = lines[i];
            if (i + 1 == maxlines && lines.size() > maxlines)
                l = ellipsize(app.f_small, l + "…", pw - 112);
            text(r, app.f_small, l, cx, y, C_DIM, Align::Center);
            y += 32;
        }
    }
    text(r, app.f_small, tr(T_CONFIRM_HINT), cx, py + ph - 52,
         with_alpha(C_DIM, 200), Align::Center);
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
    // All shipped languages are drawn with the same font file, so a switch
    // only changes what tr() returns. Reopening the 8 MB font (three sizes)
    // and wiping the text cache here cost a visible stall on the handheld.
    app.settings.language = (app.settings.language + dir + LANG_COUNT) % LANG_COUNT;
    g_lang = (Lang)app.settings.language;
    save_settings(app.settings);
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

// Last non-empty line of the emulator logs, for the crash dialog. Reading
// only the tail keeps this cheap even when RetroArch has logged for an hour.
static std::string log_tail_line()
{
    for (const char* path : { "/userdata/system/retroarch.log",
                              "/userdata/system/emu.log" }) {
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        long sz = -1;
        if (fseek(f, 0, SEEK_END) == 0) sz = ftell(f);
        if (sz < 0) { fclose(f); continue; }
        fseek(f, sz > 4096 ? sz - 4096 : 0, SEEK_SET);
        std::string buf;
        char tmp[1024];
        size_t n;
        while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) buf.append(tmp, n);
        fclose(f);
        size_t end = buf.find_last_not_of(" \t\r\n");
        if (end == std::string::npos) continue;
        size_t nl = buf.find_last_of('\n', end);
        std::string line = buf.substr(nl == std::string::npos ? 0 : nl + 1,
                                      end - (nl == std::string::npos ? 0 : nl + 1) + 1);
        if (line.size() > 120) line = "…" + line.substr(line.size() - 119);
        return line;
    }
    return "-";
}

// ---------------------------------------------------------------------------
// Launch animation. The top panel shows the game card dropping into the
// handheld's slot; the deck reports the read (percent, sectors, activity).
// The timeline is 2.6 animation seconds but runs at 1.7x, so the wall-clock
// cost is ~1.5 s: the UI is torn down first and the emulator starts only
// afterwards (a shared DRM master is what made the old gate fail).
// Host preview: GAMEOS_LAUNCH_SHOT=<dir> dumps both panels at the times in
// GAMEOS_LAUNCH_AT (animation seconds).
// ---------------------------------------------------------------------------
constexpr float LAUNCH_DUR = 2.6f;           // animation seconds, not wall
constexpr float LAUNCH_SPEED = 1.7f;         // animation seconds per real second
constexpr float LAUNCH_HIT = 0.82f;          // card reaches the slot

void anim_save_panel(SDL_Renderer* r, const std::string& path)
{
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32,
                                                    SDL_PIXELFORMAT_ARGB8888);
    if (s && SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                  s->pixels, s->pitch) == 0)
        IMG_SavePNG(s, path.c_str());
    if (s) SDL_FreeSurface(s);
}

int console_icon_index(const char* id)
{
    const auto& cons = consoles();
    for (int i = 0; i < (int)cons.size() && i < 5; i++)
        if (strcmp(cons[i].id, id) == 0) return i;
    return -1;
}

std::string launch_cover_path(const Console& c, const Game& g)
{
    const char* ov = getenv("GAMEOS_COVER_OVERRIDE");
    if (ov && *ov) return ov;
    return cover_path(c.id, g.title);
}

// Centre-cropped cover blit.
void cover_fit(SDL_Renderer* r, SDL_Texture* t, SDL_Rect dst, Uint8 alpha)
{
    if (!t) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
    if (tw <= 0 || th <= 0) return;
    float sa = (float)tw / th, da = (float)dst.w / dst.h;
    SDL_Rect src;
    if (sa > da) { int w2 = (int)(th * da); src = { (tw - w2) / 2, 0, w2, th }; }
    else         { int h2 = (int)(tw / da); src = { 0, (th - h2) / 2, tw, h2 }; }
    draw_tex_src(r, t, src, dst, alpha);
}

// Cover art, or a clean synthesised label (accent gradient + console glyph)
// for games that have no box art yet.
void launch_art(SDL_Renderer* r, SDL_Rect rc, int radius, const char* id,
                const std::string& title, SDL_Texture* cover, SDL_Texture* icon,
                Uint8 alpha)
{
    rrect(r, rc, radius, mix_color(C_ACCENT, C_BG, 0.78f));
    if (cover) {
        SDL_Rect inner = { rc.x + 2, rc.y + 2, rc.w - 4, rc.h - 4 };
        cover_fit(r, cover, inner, alpha);
    } else {
        grad_rect(r, { rc.x + 2, rc.y + 2, rc.w - 4, rc.h - 4 },
                  with_alpha(mix_color(C_ACCENT, C_BG, 0.55f), alpha),
                  with_alpha(mix_color(C_PANEL, C_BG, 0.35f), alpha));
        if (icon) {
            int sz = std::min(rc.w, rc.h) * 2 / 3;
            draw_tex(r, icon, { rc.x + (rc.w - sz) / 2, rc.y + (rc.h - sz) / 2, sz, sz },
                     (Uint8)(alpha * 0.9f));
        } else {
            text(r, g_ui.logo, id, rc.x + rc.w / 2,
                 rc.y + rc.h / 2 - 26, with_alpha(C_TEXT, alpha), Align::Center);
        }
        text(r, g_ui.small, ellipsize(g_ui.small, title, rc.w - 24),
             rc.x + rc.w / 2, rc.y + rc.h - 34,
             with_alpha(C_TEXT, (Uint8)(alpha * 0.85f)), Align::Center);
    }
}

// The game card itself, pre-rendered once so it can be blitted rotated.
SDL_Texture* build_launch_card(SDL_Renderer* r, const Console& c, const Game& g,
                               SDL_Texture* cover, SDL_Texture* icon)
{
    const int cw = 250, ch = 168;
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_TARGET, cw, ch);
    if (!t) return nullptr;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(r, t);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    SDL_Rect body = { 2, 2, cw - 4, ch - 4 };
    rrect_grad(r, body, 16, mix_color(C_PANEL, C_BG, 0.30f),
               mix_color(C_PANEL, C_BG, 0.70f));
    rrect_stroke(r, body, 16, with_alpha(C_TEXT, 52), 1);

    // grip ridges on the top edge
    for (int i = 0; i < 3; i++)
        rrect(r, { 18 + i * 13, 9, 8, 5 }, 2, with_alpha(C_DIM, 80));

    // label window
    SDL_Rect lab = { 16, 28, cw - 32, 96 };
    launch_art(r, lab, 8, c.id, g.title, cover, icon, 255);
    rrect_stroke(r, lab, 8, with_alpha(C_ACCENT, 110), 1);

    // console tag + game title
    text(r, g_ui.small, c.name_en, 18, 128, with_alpha(C_ACCENT, 220));
    text(r, g_ui.small, ellipsize(g_ui.small, g.title, cw - 76),
         cw - 18, 128, with_alpha(C_DIM, 210), Align::Right);

    // edge connector pins
    for (int i = 0; i < 12; i++)
        fill_rect(r, 20 + i * 18, ch - 13, 11, 7, rgba(212, 178, 90, 210));

    // clipped top-right corner, like a game card
    {
        SDL_BlendMode bm;
        SDL_GetRenderDrawBlendMode(r, &bm);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_Vertex tri[3] = {
            { { (float)(cw - 32), 2.f }, { 0, 0, 0, 0 }, { 0, 0 } },
            { { (float)(cw - 1), 2.f }, { 0, 0, 0, 0 }, { 0, 0 } },
            { { (float)(cw - 1), 33.f }, { 0, 0, 0, 0 }, { 0, 0 } },
        };
        perf::note_draw();
        SDL_RenderGeometry(r, nullptr, tri, 3, nullptr, 0);
        SDL_SetRenderDrawBlendMode(r, bm);
    }

    SDL_SetRenderTarget(r, nullptr);
    return t;
}

void draw_launch_top(SDL_Renderer* r, const Console& c, const Game& g, float t,
                     SDL_Texture* card, SDL_Texture* cover, SDL_Texture* icon)
{
    float out = 1.f - std::clamp((t - (LAUNCH_DUR - 0.22f)) / 0.22f, 0.f, 1.f);
    float breathe = 0.5f + 0.5f * sinf(t * 1.1f);

    grad_rect(r, { 0, 0, W, H }, mix_color(C_BG, rgba(0, 0, 0, 255), 0.45f),
              rgba(3, 5, 12, 255));
    for (int x = 0; x <= W; x += 64) fill_rect(r, x, 0, 1, H, with_alpha(C_DIM, 10));
    for (int y = 0; y <= H; y += 64) fill_rect(r, 0, y, W, 1, with_alpha(C_DIM, 10));
    glow(r, 512, 400, 330, C_ACCENT, 12, (int)(24 + 10 * breathe));
    glow(r, 512, 470, 170, C_ACCENT, 10, 20);

    for (int i = 0; i < 16; i++) {
        float ph = fmodf(t * (0.05f + (i % 5) * 0.011f) + i * 0.37f, 1.f);
        int x = (i * 173 + 61) % W;
        int y = (int)(H - ph * (H + 40));
        Uint8 a = (Uint8)(44 * (0.4f + 0.6f * sinf(t * 1.7f + i)));
        circle(r, x, y, (i % 4 == 0) ? 2 : 1, with_alpha(C_ACCENT, a));
    }

    text(r, g_ui.logo, "gameOS", 40, 26, C_TEXT);
    text(r, g_ui.small, "GAME CARD // SLOT A", W - 40, 52,
         with_alpha(C_TEXT, 150), Align::Right);

    // NDS is an opened vertical clamshell (two screens stacked); every other
    // system is a horizontal single-screen handheld.
    const bool dual = c.dual;
    const SDL_Color hi = mix_color(C_ACCENT, rgba(255, 255, 255, 255), 0.72f);
    // The screens stay dark wells in every theme, so text drawn on them needs
    // a light ink rather than the theme's (possibly dark) C_TEXT.
    const SDL_Color screen_ink = rgba(233, 242, 246, 255);
    const SDL_Color screen_acc = mix_color(C_ACCENT, rgba(255, 255, 255, 255), 0.42f);
    const int slot_y = dual ? 390 : 436;

    // card drop: accelerate into the slot, damped seat bounce, then swallow
    const float drop_from = -180.f;
    const float seat_y = dual ? 226.f : 284.f;
    float p = std::clamp((t - 0.10f) / (LAUNCH_HIT - 0.10f), 0.f, 1.f);
    float t2 = t - LAUNCH_HIT;
    float y = drop_from + (seat_y - drop_from) * p * p;
    float ang = 6.f * (1.f - p);
    if (p >= 1.f && t < 1.04f)
        y = seat_y + 6.f * expf(-9.f * t2) * sinf(28.f * t2);
    float shake = (t2 > 0.f && t2 < 0.3f)
                    ? 2.6f * expf(-9.f * t2) * sinf(70.f * t2) : 0.f;

    // The lid is drawn first; the card falls in front of it, then the hinge
    // and lower shell are drawn over the card so it slides into the slot.
    if (dual) {
        SDL_Rect lid = { 252 + (int)shake, 76, 520, 300 };
        rrect_shadow(r, lid, 30, 12, 10, 90);
        rrect_grad(r, lid, 30, mix_color(C_PANEL, C_BG, 0.10f),
                   mix_color(C_PANEL, C_BG, 0.55f));
        rrect_stroke(r, lid, 30, with_alpha(C_TEXT, 46), 2);
        rrect_stroke(r, { lid.x + 6, lid.y + 6, lid.w - 12, lid.h - 12 }, 25,
                     rgba(0, 0, 0, 70), 1);
        circle(r, 512, 88, 3, with_alpha(C_DIM, 110));       // camera
    }

    // impact wave from the slot (over the shell, under the card)
    {
        float ti = t - LAUNCH_HIT;
        if (ti > 0.f && ti < 0.6f) {
            float q = ti / 0.6f;
            float rad = 40 + q * (dual ? 300.f : 320.f);
            arc(r, 512, slot_y, rad, 3.14159265f, 2.f * 3.14159265f, 3,
                with_alpha(C_ACCENT, (int)(140 * (1.f - q))));
            arc(r, 512, slot_y, rad * 0.68f, 3.14159265f, 2.f * 3.14159265f, 2,
                with_alpha(hi, (int)(80 * (1.f - q))));
            glow(r, 512, slot_y, 80, hi, 10, (int)(55 * (1.f - q)));
            for (int i = 0; i < 10; i++) {
                float a = 3.14159265f * (0.15f + 0.7f * i / 9.f);
                float d = 30 + q * 170;
                int px = 512 + (int)(cosf(a) * d * 1.2f);
                int py = slot_y - (int)(sinf(a) * d);
                circle(r, px, py, (i % 3 == 0) ? 2 : 1,
                       with_alpha(C_ACCENT, (int)(160 * (1.f - q))));
            }
        }
    }

    // slot halo (behind the card)
    if (t > 0.40f) {
        float heat = std::clamp((t - 0.40f) / 0.45f, 0.f, 1.f)
                   * (1.f - 0.6f * std::clamp((t - 1.04f) / 0.4f, 0.f, 1.f));
        glow(r, 512, slot_y + 4, 150, C_ACCENT, 18, (int)(30 * heat));
        glow(r, 512, slot_y + 6, 70, hi, 12, (int)(12 * heat));
    }

    float sink = std::clamp((t - 1.04f) / 0.22f, 0.f, 1.f);
    if (card && sink < 1.f) {
        float cy = y + (dual ? 110.f : 150.f) * sink * sink;
        SDL_Rect dst = { 512 - 125, (int)cy, 250, 168 };
        if (p < 0.999f) {
            for (int k = 2; k >= 1; k--) {
                SDL_Rect gd = dst;
                gd.y -= k * 16;
                SDL_SetTextureAlphaMod(card, (Uint8)((22 / k + 10) * (1.f - sink)));
                perf::note_draw();
                SDL_RenderCopyEx(r, card, nullptr, &gd, ang + 1.6f * k,
                                 nullptr, SDL_FLIP_NONE);
            }
        }
        SDL_SetTextureAlphaMod(card, (Uint8)(255.f * (1.f - sink) + 0.5f));
        perf::note_draw();
        SDL_RenderCopyEx(r, card, nullptr, &dst, ang, nullptr, SDL_FLIP_NONE);
    }

    // handheld body
    int bx = 0, by = 0;
    if (dual) {
        SDL_Rect hinge = { 332 + (int)shake, 376, 360, 26 };
        rrect_grad(r, hinge, 13, mix_color(C_PANEL, C_BG, 0.45f),
                   mix_color(C_PANEL, rgba(0, 0, 0, 255), 0.25f));
        circle(r, hinge.x - 4, 389, 14, mix_color(C_PANEL, C_BG, 0.50f));
        circle(r, hinge.x + hinge.w + 4, 389, 14, mix_color(C_PANEL, C_BG, 0.50f));
        rrect_stroke(r, hinge, 13, with_alpha(C_DIM, 40), 1);

        SDL_Rect low = { 252 + (int)shake, 402, 520, 324 };
        rrect_shadow(r, low, 30, 14, 12, 110);
        rrect_grad(r, low, 30, mix_color(C_PANEL, C_BG, 0.18f),
                   mix_color(C_PANEL, C_BG, 0.66f));
        rrect_stroke(r, low, 30, with_alpha(C_TEXT, 42), 2);
        rrect_stroke(r, { low.x + 6, low.y + 6, low.w - 12, low.h - 12 }, 25,
                     rgba(0, 0, 0, 60), 1);
    } else {
        SDL_Rect body = { 182 + (int)shake, 436 + (int)(shake * 0.6f), 660, 252 };
        bx = body.x; by = body.y;
        rrect_shadow(r, body, 42, 12, 10, 100);
        rrect_grad(r, body, 42, mix_color(C_PANEL, rgba(0, 0, 0, 255), 0.16f),
                   mix_color(C_PANEL, C_BG, 0.62f));
        rrect_stroke(r, body, 42, with_alpha(C_TEXT, 44), 2);
        rrect_stroke(r, { bx + 4, by + 4, 652, 244 }, 39, rgba(0, 0, 0, 70), 1);
        for (int i = 0; i < 4; i++) {
            circle(r, 254 + i * 12, 656, 2, with_alpha(C_DIM, 45));
            circle(r, 734 + i * 12, 656, 2, with_alpha(C_DIM, 45));
        }
    }

    // slot lip
    SDL_Rect lip = dual ? SDL_Rect{ 432 + (int)shake, 378, 160, 30 }
                        : SDL_Rect{ 392 + (int)shake, 420, 240, 32 };
    rrect(r, { lip.x - 6, lip.y - 2, lip.w + 12, lip.h + 6 }, 14, rgba(0, 0, 0, 120));
    rrect_grad(r, lip, 11, rgba(8, 10, 18, 255), rgba(22, 29, 46, 255));
    rrect_stroke(r, lip, 11, with_alpha(C_ACCENT, 90), 1);
    for (int i = 0; i < 2; i++)
        fill_rect(r, lip.x + 10, lip.y + 9 + i * 6, lip.w - 20, 3,
                  with_alpha(C_BG, 215));
    if (t2 > 0.f && t2 < 0.35f) {
        float f = 1.f - t2 / 0.35f;
        rrect_stroke(r, { lip.x - 6, lip.y - 2, lip.w + 12, lip.h + 6 }, 14,
                     with_alpha(hi, (int)(210 * f)), 2);
        fill_rect(r, lip.x + 10, lip.y + 9, lip.w - 20, 3,
                  with_alpha(hi, (int)(235 * f)));
    }

    // screens
    const float hero_at = dual ? 1.72f : 1.47f;
    const float mini_at = dual ? 1.74f : 1.50f;
    auto screen_rect = [&](SDL_Rect rc, float start, bool hero) {
        // left off until the card is swallowed, so the card can slide across
        // the closed shell without a dark screen well cutting through it
        if (t < start) return;
        float on = std::clamp((t - start) / 0.45f, 0.f, 1.f);
        rrect(r, { rc.x - 5, rc.y - 5, rc.w + 10, rc.h + 10 }, 11, rgba(4, 6, 12, 255));
        rrect_stroke(r, { rc.x - 5, rc.y - 5, rc.w + 10, rc.h + 10 }, 11,
                     with_alpha(C_DIM, 30), 1);
        rrect(r, rc, 8, rgba(3, 5, 10, 255));
        if (on <= 0.f) return;
        grad_rect(r, rc, with_alpha(C_ACCENT, (int)(24 * on)),
                  with_alpha(mix_color(C_ACCENT, C_BG, 0.65f), (int)(10 * on)));
        if (on < 1.f) {
            int fy = rc.y + (int)(on * rc.h);
            if (fy > rc.y + 2)
                fill_rect(r, rc.x + 2, rc.y + 2, rc.w - 4, fy - rc.y - 2,
                          with_alpha(hi, 16));
            fill_rect(r, rc.x + 2, fy, rc.w - 4, 2,
                      with_alpha(hi, (int)(200 * (1.f - on * 0.5f))));
            if (fy - 6 > rc.y)
                fill_rect(r, rc.x + 2, fy - 6, rc.w - 4, 1,
                          with_alpha(hi, 90));
            if (fy - 12 > rc.y)
                fill_rect(r, rc.x + 2, fy - 12, rc.w - 4, 1,
                          with_alpha(hi, 45));
        }
        float ca = std::clamp((t - (hero ? hero_at : mini_at)) / 0.30f, 0.f, 1.f);
        if (hero) {
            if (ca > 0.f) {
                int th = rc.h * 5 / 9, tw = th * 3 / 4;
                SDL_Rect thumb = { rc.x + (rc.w - tw) / 2,
                                   rc.y + 25 - (int)((1.f - ca) * 14.f), tw, th };
                if (cover) {
                    cover_fit(r, cover, thumb, (Uint8)(255 * ca));
                    rrect_stroke(r, thumb, 5, with_alpha(screen_acc, (int)(150 * ca)), 1);
                } else if (icon) {
                    int isz = th - 10;
                    draw_tex(r, icon,
                             { thumb.x + (tw - isz) / 2, thumb.y + 5, isz, isz },
                             (Uint8)(225 * ca));
                }
                text(r, g_ui.small,
                     ellipsize(g_ui.small, g.title, rc.w - 24),
                     rc.x + rc.w / 2, thumb.y + th + 12,
                     with_alpha(screen_ink, (Uint8)(255 * ca)), Align::Center);
                text(r, g_ui.small, c.name_en,
                     rc.x + rc.w / 2, thumb.y + th + 40,
                     with_alpha(screen_acc, (Uint8)(230 * ca)), Align::Center);
            }
            int sy = rc.y + 2 + (int)(fmodf(t * 90.f, rc.h - 4.f));
            fill_rect(r, rc.x + 2, sy, rc.w - 4, 1, with_alpha(hi, 70));
        } else if (ca > 0.f) {
            float pp2 = std::clamp((t - 0.90f) / 1.35f, 0.f, 1.f);
            int pct2 = (int)(ease_out(pp2) * 100.f + 0.5f);
            char lb[32];
            snprintf(lb, sizeof lb, "CARD READ  %d%%", pct2);
            text(r, g_ui.small, lb, rc.x + rc.w / 2, rc.y + 30,
                 with_alpha(screen_ink, (Uint8)(235 * ca)), Align::Center);
            SDL_Rect mb = { rc.x + 40, rc.y + 76, rc.w - 80, 4 };
            rrect(r, mb, 2, with_alpha(C_TRACK, (Uint8)(220 * ca)));
            int mw = (mb.w * pct2) / 100;
            if (mw > 1)
                rrect(r, { mb.x, mb.y, mw, mb.h }, 2,
                      with_alpha(screen_acc, (Uint8)(255 * ca)));
        }
        rrect_stroke(r, rc, 8, with_alpha(C_ACCENT, (int)(70 * on)), 1);
    };
    if (dual) {
        SDL_Rect s1 = { 352 + (int)shake, 106, 320, 240 };
        SDL_Rect s2 = { 352 + (int)shake, 424, 320, 240 };
        screen_rect(s1, 1.26f, true);
        screen_rect(s2, 1.38f, false);
    } else {
        SDL_Rect s = { 362, by + 26, 300, 200 };
        screen_rect(s, 1.02f, true);
    }

    // d-pad + ABXY
    auto dpad = [&](int dcx, int dcy) {
        rrect(r, { dcx - 23, dcy - 9, 46, 18 }, 6, with_alpha(C_TRACK, 240));
        rrect(r, { dcx - 9, dcy - 23, 18, 46 }, 6, with_alpha(C_TRACK, 240));
        rrect_stroke(r, { dcx - 23, dcy - 9, 46, 18 }, 6, with_alpha(C_DIM, 55), 1);
        rrect_stroke(r, { dcx - 9, dcy - 23, 18, 46 }, 6, with_alpha(C_DIM, 55), 1);
    };
    if (dual) {
        dpad(302 + (int)shake, 544);
        rrect(r, { 486, 690, 18, 7 }, 4, with_alpha(C_TRACK, 230));
        rrect(r, { 510, 690, 18, 7 }, 4, with_alpha(C_TRACK, 230));
        const int ab[4][2] = { { 722, 520 }, { 746, 544 },
                               { 722, 568 }, { 698, 544 } };
        for (auto& q : ab) {
            circle(r, q[0] + (int)shake, q[1], 11, with_alpha(C_TRACK, 240));
            ring(r, q[0] + (int)shake, q[1], 11, 1, with_alpha(C_DIM, 60));
        }
    } else {
        dpad(280 + (int)shake, by + 126);
        const int ab[4][2] = { { 744, by + 102 }, { 768, by + 126 },
                               { 744, by + 150 }, { 720, by + 126 } };
        for (auto& q : ab) {
            circle(r, q[0] + (int)shake, q[1], 11, with_alpha(C_TRACK, 240));
            ring(r, q[0] + (int)shake, q[1], 11, 1, with_alpha(C_DIM, 60));
        }
    }

    // power LED
    {
        float on = std::clamp((t - 1.02f) / 0.2f, 0.f, 1.f);
        SDL_Color led = mix_color(C_RED, C_GREEN, on);
        int lx = dual ? 756 : bx + 638;
        int ly = dual ? 420 : by + 20;
        glow(r, lx, ly, 26, led, 8, (int)(70 * on + 8));
        circle(r, lx, ly, 5, with_alpha(led, (int)(120 + 135 * on)));
    }

    float fa = std::clamp((t - 1.35f) / 0.35f, 0.f, 1.f);
    if (fa > 0.f)
        text(r, g_ui.small,
             ellipsize(g_ui.small, std::string(console_name(c.id)) + "  //  " + g.title, 640),
             512, dual ? 736 : 714, with_alpha(C_DIM, (Uint8)(210 * fa)), Align::Center);

    if (out < 1.f) fill_rect(r, 0, 0, W, H, rgba(0, 0, 0, (Uint8)((1.f - out) * 255)));
}

void draw_launch_bot(SDL_Renderer* r, const Console& c, const Game& g, float t,
                     SDL_Texture* cover, SDL_Texture* icon)
{
    float out = 1.f - std::clamp((t - (LAUNCH_DUR - 0.22f)) / 0.22f, 0.f, 1.f);
    float breathe = 0.5f + 0.5f * sinf(t * 1.4f);

    grad_rect(r, { 0, 0, W, H }, mix_color(C_BG, rgba(0, 0, 0, 255), 0.30f),
              rgba(4, 7, 15, 255));
    for (int x = 0; x <= W; x += 64) fill_rect(r, x, 0, 1, H, with_alpha(C_DIM, 12));
    for (int y = 0; y <= H; y += 64) fill_rect(r, 0, y, W, 1, with_alpha(C_DIM, 12));
    glow(r, 180, 340, 260, C_ACCENT, 12, 22);
    glow(r, 760, 250, 240, C_ACCENT, 12, 16);

    fill_rect(r, 44, 44, 4, 30, C_ACCENT);
    text(r, g_ui.logo, "GAME LOAD", 60, 34, C_TEXT);
    text(r, g_ui.small, "GAMEOS LAUNCHER", 62, 92, with_alpha(C_DIM, 170));

    float pp = std::clamp((t - 0.90f) / 1.35f, 0.f, 1.f);
    int pct = (int)(ease_out(pp) * 100.f + 0.5f);
    if (pct > 100) pct = 100;
    bool done = pct >= 100;

    char pb[16];
    snprintf(pb, sizeof pb, "%d%%", pct);
    text(r, g_ui.clock, pb, W - 44, 16, C_TEXT, Align::Right);

    SDL_Rect bar = { 560, 150, 420, 8 };
    rrect(r, bar, 4, with_alpha(C_TRACK, 235));
    int fw = bar.w * pct / 100;
    if (fw > 2) {
        rrect(r, { bar.x, bar.y, fw, bar.h }, 4, C_ACCENT);
        glow(r, bar.x + fw, bar.y + 4, 26, C_ACCENT, 8, 60);
    }
    for (int i = 1; i < 10; i++)
        fill_rect(r, bar.x + bar.w * i / 10, bar.y + 12, 1, 4, with_alpha(C_DIM, 55));

    // cover plate
    SDL_Rect cov = { 64, 180, 232, 316 };
    rrect(r, { cov.x - 7, cov.y - 7, cov.w + 14, cov.h + 14 }, 23,
          with_alpha(C_PANEL, 170));
    rrect_stroke(r, { cov.x - 7, cov.y - 7, cov.w + 14, cov.h + 14 }, 23,
                 with_alpha(C_DIM, 40), 1);
    launch_art(r, cov, 16, c.id, g.title, cover, icon, 255);
    {
        auto bracket = [&](int x, int y, int dx, int dy) {
            thick_line(r, (float)x, (float)y, (float)(x + dx * 20), (float)y, 2, C_ACCENT);
            thick_line(r, (float)x, (float)y, (float)x, (float)(y + dy * 20), 2, C_ACCENT);
        };
        bracket(cov.x - 10, cov.y - 10, 1, 1);
        bracket(cov.x + cov.w + 10, cov.y - 10, -1, 1);
        bracket(cov.x - 10, cov.y + cov.h + 10, 1, -1);
        bracket(cov.x + cov.w + 10, cov.y + cov.h + 10, -1, -1);
        int sy = cov.y + 4 + (int)(fmodf(t * 0.55f, 1.f) * (cov.h - 8.f));
        fill_rect(r, cov.x + 4, sy, cov.w - 8, 2,
                  with_alpha(rgba(214, 255, 247, 255), 70));
    }

    SDL_Rect chip = { 64, 520, 92, 34 };
    rrect(r, chip, 17, with_alpha(C_ACCENT, 26));
    rrect_stroke(r, chip, 17, with_alpha(C_ACCENT, 120), 1);
    text(r, g_ui.small, c.name_en, chip.x + 46, chip.y + 6, C_ACCENT, Align::Center);
    text(r, g_ui.small, ellipsize(g_ui.small, console_name(c.id), 232),
         64, 566, C_TEXT);
    std::string file = g.path;
    size_t sl = file.find_last_of('/');
    if (sl != std::string::npos) file = file.substr(sl + 1);
    text(r, g_ui.small, ellipsize(g_ui.small, file, 232), 64, 596,
         with_alpha(C_DIM, 200));

    // title
    auto lines = wrap(g_ui.hero, g.title, 600);
    int ty = 166;
    for (size_t i = 0; i < lines.size() && i < 2; i++) {
        std::string ln = lines[i];
        if (i == 1 && lines.size() > 2)
            ln = ellipsize(g_ui.hero, ln + "…", 600);
        text(r, g_ui.hero, ln, 336, ty, C_TEXT);
        ty += 62;
    }
    text(r, g_ui.small,
         ellipsize(g_ui.small, std::string(console_name(c.id)) + "  //  " + file, 600),
         336, ty + 8, with_alpha(C_DIM, 200));

    // status line
    std::string st = done ? tr(T_LOAD_START)
                    : t < 1.55f ? tr(T_LOAD_CARD)
                    : t < 2.10f ? tr(T_LOAD_VERIFY)
                                : tr(T_LOAD_START);
    if (done || t >= 2.10f) {
        icon_check(r, 336, 396, 26, C_ACCENT);
    } else {
        float dp = 0.5f + 0.5f * sinf(t * 6.f);
        circle(r, 348, 408, 5, with_alpha(C_ACCENT, (int)(120 + 135 * dp)));
    }
    text(r, g_ui.body, st, 374, 392, done ? C_TEXT : C_ACCENT);

    // data path: cartridge -> emulator core
    {
        const int py = 472;
        fill_rect(r, 336, py, 624, 1, with_alpha(C_DIM, 55));
        for (int i = 0; i < 6; i++) {
            float u = std::fmod(t * 0.14f + i * 0.17f, 1.f);
            int x = 336 + (int)(u * 608.f);
            fill_rect(r, x, py - 2, 14, 4, with_alpha(C_ACCENT, 110));
        }
        circle(r, 336, py, 3, with_alpha(C_ACCENT, 200));
        circle(r, 960, py, 3, with_alpha(C_ACCENT, 200));
        text(r, g_ui.small, "CART", 336, py + 8, with_alpha(C_DIM, 160));
        text(r, g_ui.small, "EMU CORE", 960, py + 8, with_alpha(C_DIM, 160),
             Align::Right);
    }

    // read activity + scan head
    {
        const int n = 52, step = 12, base = 656, x0 = 336;
        float head = x0 + fmodf(t * 0.75f, 1.f) * 600.f;
        for (int i = 0; i < n; i++) {
            float v = sinf(t * 5.2f + i * 0.55f) * sinf(t * 2.3f + i * 0.21f);
            int x = x0 + i * step;
            int hgt = 3 + (int)(fabsf(v) * 54.f * (0.25f + 0.75f * pp));
            float near = std::max(0.f, 1.f - fabsf(x - head) / 90.f);
            Uint8 a = (Uint8)(70 + 150 * near);
            fill_rect(r, x, base - hgt, 6, hgt, with_alpha(C_ACCENT, a));
            fill_rect(r, x, base, 6, hgt / 4, with_alpha(C_ACCENT, 26));
        }
        fill_rect(r, (int)head, 570, 2, 96, with_alpha(rgba(214, 255, 247, 255), 90));
        fill_rect(r, 336, base, 624, 1, with_alpha(C_DIM, 70));
    }
    char sec[48];
    snprintf(sec, sizeof sec, "SECTOR 0x%04X / 0x2000", (int)(pp * 0x2000));
    text(r, g_ui.small, sec, 336, 682, with_alpha(C_DIM, 190));
    char rate[32];
    snprintf(rate, sizeof rate, "READ %4.1f MB/s", 18.f + 7.f * breathe + 3.f * pp);
    text(r, g_ui.small, rate, 960, 682,
         with_alpha(done ? C_DIM : C_ACCENT, 200), Align::Right);

    if (out < 1.f) fill_rect(r, 0, 0, W, H, rgba(0, 0, 0, (Uint8)((1.f - out) * 255)));
}

// Runs before the emulator starts, on every console. Stops on black so the
// DRM hand-off to the emulator is invisible.
void launch_anim(const Console& c, const Game& g)
{
    if (!app.ren_top) return;

    const std::string cpath = launch_cover_path(c, g);
    SDL_Texture* cover_t = cpath.empty() ? nullptr : cover_tex(app.ren_top, cpath);
    SDL_Texture* cover_b = (cpath.empty() || !app.ren_bot)
                             ? nullptr : cover_tex(app.ren_bot, cpath);
    int idx = console_icon_index(c.id);
    SDL_Texture* icon_t = (idx >= 0) ? app.icons[idx] : nullptr;
    SDL_Texture* icon_b = (idx >= 0 && app.ren_bot) ? app.icons_bot[idx] : nullptr;
    SDL_Texture* card = build_launch_card(app.ren_top, c, g, cover_t, icon_t);

    const char* shotdir = getenv("GAMEOS_LAUNCH_SHOT");
    std::vector<float> shots;
    if (shotdir && *shotdir) {
        const char* e = getenv("GAMEOS_LAUNCH_AT");
        std::string ss = (e && *e) ? e : "0.35,0.7,0.9,1.25,1.9,2.3";
        size_t pos = 0;
        while (pos < ss.size()) {
            size_t nx = ss.find(',', pos);
            if (nx == std::string::npos) nx = ss.size();
            shots.push_back((float)atof(ss.substr(pos, nx - pos).c_str()));
            pos = nx + 1;
        }
    }
    size_t shot_i = 0;

    SDL_ShowCursor(SDL_DISABLE);
    Uint32 t0 = SDL_GetTicks();
    for (;;) {
        Uint32 now = SDL_GetTicks();
        float t = ((now - t0) / 1000.f) * LAUNCH_SPEED;
        draw_launch_top(app.ren_top, c, g, t, card, cover_t, icon_t);
        if (app.ren_bot)
            draw_launch_bot(app.ren_bot, c, g, t, cover_b, icon_b);
        while (shot_i < shots.size() && t >= shots[shot_i]) {
            char name[64];
            snprintf(name, sizeof name, "/top_%04d.png", (int)(shots[shot_i] * 1000));
            anim_save_panel(app.ren_top, std::string(shotdir) + name);
            if (app.ren_bot) {
                snprintf(name, sizeof name, "/bot_%04d.png", (int)(shots[shot_i] * 1000));
                anim_save_panel(app.ren_bot, std::string(shotdir) + name);
            }
            shot_i++;
        }
        SDL_RenderPresent(app.ren_top);
        if (app.ren_bot) SDL_RenderPresent(app.ren_bot);
        if (t >= LAUNCH_DUR) break;
        SDL_Delay(13);
    }
    if (card) SDL_DestroyTexture(card);
}

// Launch one game from any screen (console list, favorites, history, a
// recommendation). Records the session; the caller refreshes its own list.
static void launch_one(const Console& c, const Game& g)
{
    long long wall_start = (long long)time(nullptr);
    {
        char dbg[512];
        snprintf(dbg, sizeof(dbg), "gameos-ui: launch console=%s title=\"%.200s\" path=%s\n",
                 c.id, g.title.c_str(), g.path.c_str());
        ui_dbg(dbg);
    }
    // Every console, NDS included: play the animation and drop DRM master
    // BEFORE the emulator starts. The old dual-screen overlap ran RetroArch
    // while the UI still held master; its first modeset was denied, it
    // exited, and the automatic retry produced the black/white flash and a
    // wasted one to two seconds on every launch.
    launch_anim(c, g);                 // card-insert read animation
    shutdown_gfx();                    // release DRM master
    // CLOCK_MONOTONIC, not the wall clock: gameos-sleepd suspends to RAM on lid
    // close and CLOCK_REALTIME jumps forward by the sleep time on resume.
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = launch_game(c, g);
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg), "gameos-ui: emu returned rc=%d after %llds\n",
                 rc, (long long)(t1.tv_sec - t0.tv_sec));
        ui_dbg(dbg);
    }
    // Log the session BEFORE taking the panels back: if init_gfx() fails the UI
    // exits right here, and the hour just played would otherwise never be saved.
    if (rc != 127)
        activity_record(c.id, g.title, wall_start, (long long)(t1.tv_sec - t0.tv_sec));
    if (!init_gfx()) {                 // emulator exited — take panels back
        // Graphics re-init failed (DRM still held / mode broken). Exit non-zero
        // so the session supervisor backs off instead of treating it as a clean
        // exit and spinning in a tight restart loop.
        ui_dbg("gameos-ui: gfx reinit FAILED after emulator exit\n");
        app.running = false;
        app.exit_code = 1;
        return;
    }
    ui_dbg("gameos-ui: gfx reinit ok after emulator exit\n");
    // fresh scene textures after re-init: fade back in from black
    g_scene_cur ^= 1;
    g_trans_start = SDL_GetTicks();

    if (rc == 127) {
        sound_play(SND_ERROR);
        show_msg(tr(T_EMU_FAIL), tr(T_EMU_FAIL_BODY));
    } else if (rc != 0) {
        sound_play(SND_ERROR);
        show_msg(tr(T_EMU_CRASH_TITLE),
                 fmt(tr(T_EMU_CRASH_BODY), log_tail_line().c_str()));
    }
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
    case Screen::Msg:
        app.screen = Screen::Home;
        break;
    case Screen::Settings:
        // B first leaves adjust mode, a second press returns home
        if (app.set_adjust >= 0) { app.set_adjust = -1; break; }
        app.screen = Screen::Home;
        break;
    case Screen::Wifi:
        if (app.wifi_kb) { kb_cancel(); break; }
        app.screen = Screen::Settings;
        break;
    case Screen::Setup:
        // B goes back one step; an open keyboard is cancelled first
        if (app.wifi_kb) { kb_cancel(); break; }
        setup_prev();
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
    case Screen::Covers:
        // the download keeps running in the background if it was started
        app.screen = Screen::Settings;
        break;
    case Screen::Confirm:
        app.screen = (app.confirm == ConfirmAct::DeleteSave) ? Screen::Saves
                   : (app.confirm == ConfirmAct::Update)     ? Screen::Update
                   : (app.confirm == ConfirmAct::ForgetWifi) ? Screen::Wifi
                   : (app.confirm == ConfirmAct::ForgetBt)   ? Screen::Bluetooth
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
        if (app.set_sel <= 1) {   // A enters/leaves slider adjust mode
            app.set_adjust = (app.set_adjust == app.set_sel) ? -1 : app.set_sel;
            break;
        }
        app.set_adjust = -1;
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
        else if (app.set_sel == 12) covers_enter();
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
    case Screen::Covers:
        covers_toggle();
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
        } else if (app.confirm == ConfirmAct::ForgetWifi) {
            app.confirm = ConfirmAct::None;
            app.screen = Screen::Wifi;
            if (!app.wifi_del_ssid.empty()) {
                write_file("/run/gameos/wifi-arg", app.wifi_del_ssid + "\n");
                app.wifi_status = tr(T_FORGETTING);
                wifi_start("forget");
            }
            app.wifi_del_ssid.clear();
        } else if (app.confirm == ConfirmAct::ForgetBt) {
            app.confirm = ConfirmAct::None;
            app.screen = Screen::Bluetooth;
            if (bt_mac_valid(app.bt_del_mac)) {
                app.bt_status = tr(T_FORGETTING);
                bt_start("forget", app.bt_del_mac);
            }
            app.bt_del_mac.clear();
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
    if (app.wifi_kb) {
        sound_play(SND_BACK);
        kb_cancel();
    }
    else if (app.screen == Screen::Home) {
        sound_play(SND_CONFIRM);
        app.bt_on = (system("/usr/bin/gameos-bt status 2>/dev/null | grep -q '^on$'") == 0);
        wifi_probe_start();      // fills in the WiFi row when the probe lands
        update_refresh();        // and the update row
        app.screen = Screen::Settings;
        app.set_sel = 0;
        app.set_adjust = -1;
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
static void settings_mark_dirty();
static void settings_flush(bool force);

void navigate(int dx, int dy)
{
    if (dx || dy) {
        if (app.screen == Screen::Home && dx) sound_play(SND_SWITCH);
        else if (app.screen == Screen::Settings && app.set_adjust >= 0 && dx)
            sound_play(SND_ADJUST);
        else sound_play(SND_MOVE);
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
        if (app.set_adjust >= 0) {
            if (dx) {
                if (app.set_adjust == 0) {
                    app.settings.brightness =
                        std::clamp(app.settings.brightness + dx * 5, 10, 100);
                    apply_brightness(app.settings.brightness);
                } else {
                    app.settings.volume =
                        std::clamp(app.settings.volume + dx * 5, 0, 100);
                    apply_volume(app.settings.volume);
                }
                settings_mark_dirty();
                break;
            }
            if (dy) app.set_adjust = -1;   // leave adjust, then move
        }
        // two-column tile grid: up/down changes row, left/right changes column
        if (dy) app.set_sel = std::clamp(app.set_sel + dy * 2, 0, 12);
        if (dx) app.set_sel = std::clamp(app.set_sel + dx, 0, 12);
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
// bottom deck: the touch-first control surface
//
// The top panel shows one thing well (hero, detail, dashboard); the deck
// carries every control. Regions are registered while drawing and hit-tested
// against the last rendered frame, so touch always matches what is on screen.
// ---------------------------------------------------------------------------
SDL_Texture* g_deck_tex[2] = { nullptr, nullptr };
int g_deck_cur = 0;
Screen g_deck_last = Screen::Home;
int g_deck_last_step = -1;
bool g_deck_last_kb = false;
Uint32 g_deck_start = 0;

static SDL_Rect g_slider_rect = { 0, 0, 1, 1 };

void hit_add(SDL_Rect rc, int id, int a = 0, int b = 0)
{
    ui_hit_add(rc, id, a, b);
}

void deck_header(SDL_Renderer* r, const char* title, const char* right = nullptr)
{
    rrect(r, { W / 2 - 34, 12, 68, 4 }, 2, with_alpha(C_DIM, 85));
    if (title) {
        text(r, app.f_body, title, 48, 26, C_TEXT);
    } else if (app.logo_bot) {
        int tw = 0, th = 0;
        SDL_QueryTexture(app.logo_bot, nullptr, nullptr, &tw, &th);
        if (th > 0) {
            SDL_Rect dst = { 48, 26, tw * 28 / th, 28 };
            draw_tex(r, app.logo_bot, dst, 220);
        }
    } else {
        text(r, app.f_logo, "gameOS", 48, 20, C_ACCENT);
    }
    if (right && *right) {
        text(r, app.f_small, ellipsize(app.f_small, right, 300),
             W - 48, 32, with_alpha(C_DIM, 230), Align::Right);
    } else {
        text(r, app.f_small, clock_hm(), W - 48, 32, with_alpha(C_DIM, 220),
             Align::Right);
    }
}

void deck_hint(SDL_Renderer* r, const char* s)
{
    if (!s || !*s) return;
    text(r, app.f_small, ellipsize(app.f_small, s, W - 120),
         W / 2, H - 42, with_alpha(C_DIM, 160), Align::Center);
}

void deck_base(SDL_Renderer* r)
{
    fill_rect(r, 0, 0, W, H, C_BG);
    if (app.ambient) {
        SDL_SetTextureAlphaMod(app.ambient, 70);
        SDL_Rect dst = { 0, 0, W, H };
        draw_tex(r, app.ambient, dst, 255);
        SDL_SetTextureAlphaMod(app.ambient, 255);
    }
    fill_rect(r, 0, 0, W, H, { C_BG.r, C_BG.g, C_BG.b, 165 });
    glow(r, W - 90, H - 30, 360, g_accent, 8, 16);
    fill_rect(r, 0, 0, W, 2, with_alpha(C_ACCENT, 80));
}

void deck_volume_osd(SDL_Renderer* r)
{
    if (app.vol_osd < 0) return;
    Uint32 el = SDL_GetTicks() - app.vol_osd_at;
    if (el > 1600) return;
    int a = 255;
    if (el > 1100) a = (int)(255 * (1600 - el) / 500);
    SDL_Rect rc = { W / 2 - 180, 10, 360, 68 };
    rrect(r, rc, 34, rgba(C_PANEL.r, C_PANEL.g, C_PANEL.b, (Uint8)(235 * a / 255)));
    rrect_stroke(r, rc, 34, with_alpha(C_ACCENT, a / 2), 1);
    icon_speaker(r, rc.x + 22, rc.y + 20, 28, with_alpha(C_TEXT, a));
    SDL_Color fill = with_alpha(C_ACCENT, a);
    int w = 190;
    rrect(r, { rc.x + 66, rc.y + 31, w, 7 }, 3, with_alpha(C_TRACK, a));
    if (app.vol_osd > 0)
        rrect(r, { rc.x + 66, rc.y + 31, w * app.vol_osd / 100, 7 }, 3, fill);
    char b[16];
    snprintf(b, sizeof(b), "%d%%", app.vol_osd);
    text(r, app.f_small, b, rc.x + rc.w - 26, rc.y + 20, with_alpha(C_TEXT, a),
         Align::Right);
}

void volume_show(int v)
{
    app.vol_osd = std::clamp(v, 0, 100);
    app.vol_osd_at = SDL_GetTicks();
}

// gameos.ini lives on the exFAT data partition; writing it on every key repeat
// blocked the render thread long enough to feel like a stutter. Coalesce the
// writes and let the main loop flush once the adjustment settles.
static void settings_mark_dirty()
{
    app.settings_dirty = true;
    app.settings_dirty_at = SDL_GetTicks();
}

static void settings_flush(bool force)
{
    if (!app.settings_dirty) return;
    if (!force && SDL_GetTicks() - app.settings_dirty_at < 600) return;
    app.settings_dirty = false;
    save_settings(app.settings);
}

// Sets the level locally, pushes it to ALSA at a human rate, and persists on
// release. Shared by the deck slider, L2/R2 and the hardware keys' state file.
void volume_set(int v, bool persist)
{
    v = std::clamp(v, 0, 100);
    bool changed = v != app.settings.volume;
    app.settings.volume = v;
    Uint32 now = SDL_GetTicks();
    if (changed || persist) {
        if (persist || now - app.slider_at > 110) {
            app.slider_at = now;
            apply_volume(v);
        }
    }
    if (changed) sound_play(SND_ADJUST);
    volume_show(v);
    if (persist) settings_mark_dirty();
}

void brightness_set(int v, bool persist)
{
    v = std::clamp(v, 10, 100);
    bool changed = v != app.settings.brightness;
    app.settings.brightness = v;
    Uint32 now = SDL_GetTicks();
    if (persist || (changed && now - app.slider_at > 110)) {
        app.slider_at = now;
        apply_brightness(v);
    }
    if (persist) settings_mark_dirty();
}

static void refresh_recent()
{
    Uint32 now = SDL_GetTicks();
    if (app.recent_at && now - app.recent_at < 8000) return;
    app.recent_at = now;
    auto ses = activity_sessions();
    if (ses.empty()) { app.recent_ok = false; return; }
    QuickEntry e = resolve_entry(ses[0].sys, ses[0].title);
    e.seconds = ses[0].seconds;
    e.last = ses[0].start;
    app.recent = std::move(e);
    app.recent_ok = !app.recent.missing;
}

// A horizontal row of quiet info/control chips.
void deck_round(SDL_Renderer* r, SDL_Rect rc, bool focus = false)
{
    rrect(r, rc, rc.w / 2, rgba(255, 255, 255, focus ? 22 : 9));
    rrect_stroke(r, rc, rc.w / 2, rgba(255, 255, 255, focus ? 34 : 14), 1);
}

void deck_row(SDL_Renderer* r, SDL_Rect rc, bool sel, const std::string& title,
              const std::string& meta, const std::string& right,
              SDL_Color rightc)
{
    rrect(r, rc, 18, rgba(255, 255, 255, sel ? 17 : 7));
    rrect_stroke(r, rc, 18, rgba(255, 255, 255, sel ? 24 : 9), 1);
    if (sel) rrect(r, { rc.x + 16, rc.y + 18, 4, rc.h - 36 }, 2, C_ACCENT);
    int rw = right.empty() ? 0 : text_width(app.f_small, right) + 28;
    int tw = rc.w - 64 - rw;
    if (meta.empty()) {
        text(r, app.f_body, ellipsize(app.f_body, title, tw),
             rc.x + 34, rc.y + (rc.h - 38) / 2, sel ? C_TEXT : with_alpha(C_TEXT, 205));
    } else {
        text(r, app.f_body, ellipsize(app.f_body, title, tw),
             rc.x + 34, rc.y + 12, sel ? C_TEXT : with_alpha(C_TEXT, 205));
        text(r, app.f_small, ellipsize(app.f_small, meta, tw),
             rc.x + 34, rc.y + rc.h - 34, C_DIM);
    }
    if (!right.empty())
        text(r, app.f_small, right, rc.x + rc.w - 24, rc.y + (rc.h - 28) / 2,
             rightc, Align::Right);
}

// ---------------------------------------------------------------------------
// home deck
// ---------------------------------------------------------------------------
void deck_home(SDL_Renderer* r)
{
    deck_header(r, nullptr);
    refresh_recent();

    int y = 84;

    // continue card: always present, so the layout never jumps between boots
    SDL_Rect rc = { 48, y, 928, 136 };
    card(r, rc, 26, false, 224);
    if (app.recent_ok) {
        SDL_Texture* cv = cover_tex(r, cover_path(app.recent.sys,
                                                  app.recent.game.title));
        SDL_Rect art = { 68, y + 18, 100, 100 };
        rrect(r, art, 18, rgba(255, 255, 255, 10));
        if (cv) {
            draw_tex(r, cv, art);
        } else if (app.icons_bot[app.recent.con]) {
            SDL_Rect dst = { 82, y + 32, 72, 72 };
            draw_tex(r, app.icons_bot[app.recent.con], dst, 180);
        }
        text(r, app.f_small, tr(T_RECENT), 192, y + 22, C_ACCENT);
        text(r, app.f_body, ellipsize(app.f_body, app.recent.game.title, 620),
             192, y + 52, C_TEXT);
        std::string meta = console_badge(app.recent.sys);
        if (app.recent.seconds > 0)
            meta += " · " + fmt_duration(app.recent.seconds);
        if (app.recent.last > 0) meta += " · " + fmt_when(app.recent.last);
        text(r, app.f_small, ellipsize(app.f_small, meta, 620),
             192, y + 98, C_DIM);
        deck_round(r, { W - 128, y + 36, 64, 64 }, true);
        icon_play(r, W - 114, y + 50, 36, C_ACCENT);
    } else {
        deck_round(r, { 68, y + 36, 64, 64 });
        icon_clock(r, 82, y + 50, 36, with_alpha(C_DIM, 190));
        text(r, app.f_body, tr(T_ACT_EMPTY), 156, y + 34, with_alpha(C_TEXT, 210));
        text(r, app.f_small, tr(T_QUICK_EMPTY_HIST_HELP),
             156, y + 80, C_DIM);
    }
    hit_add(rc, HIT_RECENT);
    y += 160;

    // the five consoles: one tap opens, like the physical shortcut it is
    SDL_Rect swipe = { 48, y, 928, 212 };
    hit_add(swipe, HIT_SYS, -1);           // background horizontal swipe
    for (int i = 0; i < 5; i++) {
        SDL_Rect tile = { 48 + i * 188, y, 176, 212 };
        bool sel = app.carousel == i;
        if (sel) card(r, tile, 24, true, 238);
        else {
            rrect(r, tile, 24, rgba(255, 255, 255, 7));
            rrect_stroke(r, tile, 24, rgba(255, 255, 255, 10), 1);
        }
        if (app.icons_bot[i]) {
            SDL_Rect dst = { tile.x + 20, tile.y + 24, 56, 56 };
            draw_tex(r, app.icons_bot[i], dst, sel ? 255 : 200);
        } else {
            text(r, app.f_title, consoles()[i].name_en, tile.x + 20, tile.y + 26,
                 sel ? C_ACCENT : C_DIM);
        }
        char b[32];
        snprintf(b, sizeof(b), tr(T_LIST_COUNT_FMT), games_for(i).size());
        text(r, app.f_body, ellipsize(app.f_body, console_name(consoles()[i].id), 146),
             tile.x + 15, tile.y + 104, sel ? C_TEXT : with_alpha(C_TEXT, 200));
        text(r, app.f_small, b, tile.x + 15, tile.y + 158,
             sel ? with_alpha(C_ACCENT, 220) : C_DIM);
        hit_add(tile, HIT_SYS, i);
    }
    y += 236;

    // hub shortcuts
    for (int j = 0; j < 3; j++) {
        SDL_Rect tile = { 48 + j * 314, y, 296, 132 };
        rrect(r, tile, 22, rgba(255, 255, 255, 8));
        rrect_stroke(r, tile, 22, rgba(255, 255, 255, 12), 1);
        SDL_Texture* ico = j == 0 ? app.icon_activity_bot
                          : j == 1 ? app.icon_quick_bot : app.icon_settings_bot;
        if (ico) {
            SDL_Rect dst = { tile.x + 24, tile.y + 44, 44, 44 };
            draw_tex(r, ico, dst, 220);
        }
        const char* name = j == 0 ? tr(T_ACTIVITY) : j == 1 ? tr(T_QUICK)
                                                           : tr(T_SETTINGS);
        text(r, app.f_body, name, tile.x + 94, tile.y + 47, C_TEXT);
        icon_chevron(r, tile.x + tile.w - 44, tile.y + 52, 28,
                     with_alpha(C_DIM, 170), false);
        hit_add(tile, HIT_HUB, j);
    }

    deck_hint(r, tr(T_TOUCH_HOME));
}

// ---------------------------------------------------------------------------
// game list deck
// ---------------------------------------------------------------------------
void deck_list(SDL_Renderer* r)
{
    const Console& c = consoles()[app.carousel];
    char b[96];
    snprintf(b, sizeof(b), tr(T_LIST_COUNT_FMT), app.games.size());
    std::string cnt = b;
    if (app.fav_only) cnt += tr(T_FAV_SUFFIX);
    deck_header(r, console_name(c.id), cnt.c_str());

    const int top = 92, rowh = 88, vis = 6, listx = 48, listw = 928;
    if (app.games.empty()) {
        bool data_err = !app.fav_only && !userdata_mounted();
        SDL_Rect ec = { 112, 196, 800, 252 };
        card(r, ec, 30, false, 232);
        text(r, app.f_body, app.fav_only ? tr(T_NO_FAV)
             : data_err ? tr(T_DATA_ERROR) : tr(T_NO_GAMES),
             W / 2, 252, data_err ? C_AMBER : C_TEXT, Align::Center);
        std::string p = app.fav_only ? tr(T_FAV_HELP)
                       : data_err ? tr(T_DATA_ERROR_HELP)
                       : fmt(tr(T_COPY_ROMS_FMT), c.id);
        int ty = 312;
        for (const auto& l : wrap(app.f_small, p, 700)) {
            text(r, app.f_small, l, W / 2, ty, C_DIM, Align::Center);
            ty += 32;
        }
        deck_round(r, { 48, 640, 64, 64 });
        icon_chevron(r, 60, 652, 40, C_TEXT, true);
        hit_add({ 48, 640, 64, 64 }, HIT_BACK);
        return;
    }

    if (app.sel < app.scroll) app.scroll = app.sel;
    if (app.sel >= app.scroll + vis) app.scroll = app.sel - vis + 1;
    app.list_scroll_f = damp(app.list_scroll_f, (float)app.scroll);
    app.list_sel_f = damp(app.list_sel_f, (float)app.sel);

    SDL_Rect rows = { listx, top, listw, vis * rowh - 4 };
    hit_add(rows, HIT_LIST_ROWS);

    SDL_Rect clip = { listx - 4, top - 4, listw + 8, vis * rowh };
    set_clip(r, &clip);
    int first = (int)floorf(app.list_scroll_f);
    int fy = top + (int)((app.list_sel_f - app.list_scroll_f) * rowh);
    card(r, { listx, fy, listw, rowh - 8 }, 18, true, 235);
    rrect(r, { listx + 16, fy + 20, 4, rowh - 48 }, 2, C_ACCENT);
    for (int idx = first - 1; idx <= first + vis; idx++) {
        if (idx < 0 || idx >= (int)app.games.size()) continue;
        int ry = top + (int)((idx - app.list_scroll_f) * rowh);
        bool sel = idx == app.sel;
        if (!sel) {
            rrect(r, { listx, ry, listw, rowh - 8 }, 18, rgba(255, 255, 255, 7));
            rrect_stroke(r, { listx, ry, listw, rowh - 8 }, 18,
                         rgba(255, 255, 255, 9), 1);
        }
        const Game& g = app.games[idx];
        bool fav = fav_contains(c.id, g.title);
        bool tf2 = on_tf2(g.path);
        int maxw = listw - 84 - (fav ? 40 : 0) - (tf2 ? 82 : 0);
        text(r, app.f_body, ellipsize(app.f_body, g.title, maxw),
             listx + 34, ry + 22, sel ? C_TEXT : with_alpha(C_TEXT, 205));
        if (fav) icon_star(r, listx + listw - 94, ry + 26, 34, C_AMBER, true);
        if (tf2) {
            rrect(r, { listx + listw - 72, ry + 28, 44, 24 }, 12,
                  rgba(255, 255, 255, 16));
            text(r, app.f_small, "TF2", listx + listw - 50, ry + 24, C_DIM,
                 Align::Center);
        }
        hit_add({ listx, ry, listw, rowh - 8 }, HIT_LIST_ROW, idx);
    }
    set_clip(r, nullptr);

    // controls
    deck_round(r, { 48, 644, 64, 64 });
    icon_chevron(r, 60, 656, 40, C_TEXT, true);
    hit_add({ 48, 644, 64, 64 }, HIT_BACK);

    deck_round(r, { 124, 644, 64, 64 }, fav_contains(c.id, app.games[app.sel].title));
    icon_star(r, 138, 658, 36,
              fav_contains(c.id, app.games[app.sel].title) ? C_AMBER : C_TEXT,
              fav_contains(c.id, app.games[app.sel].title));
    hit_add({ 124, 644, 64, 64 }, HIT_FAV);

    deck_round(r, { 200, 644, 64, 64 });
    icon_refresh(r, 214, 658, 36, C_TEXT);
    hit_add({ 200, 644, 64, 64 }, HIT_REFRESH);

    SDL_Rect filt = { 300, 644, 176, 64 };
    rrect(r, filt, 32, app.fav_only ? with_alpha(C_ACCENT, 40)
                                    : rgba(255, 255, 255, 8));
    if (app.fav_only) rrect_stroke(r, filt, 32, with_alpha(C_ACCENT, 190), 1);
    icon_star(r, filt.x + 20, filt.y + 16, 32, app.fav_only ? C_AMBER : C_DIM,
              app.fav_only);
    text(r, app.f_body, tr(T_TAB_FAV), filt.x + 66, filt.y + 12,
         app.fav_only ? C_ACCENT : C_TEXT);
    hit_add(filt, HIT_FAV_FILTER);

    SDL_Rect play = { W - 48 - 300, 644, 300, 64 };
    button(r, play, tr(T_PLAY), true, false);
    hit_add(play, HIT_PLAY);

    deck_hint(r, tr(T_TOUCH_LIST));
}

// ---------------------------------------------------------------------------
// settings deck: 13 direct tiles, two columns
// ---------------------------------------------------------------------------
const char* settings_label(int i)
{
    const char* rows[13] = { tr(T_BRIGHTNESS), tr(T_VOLUME), tr(T_BT), tr(T_WIFI),
                             tr(T_THEME), tr(T_LANGUAGE), tr(T_UPDATE), tr(T_SAVES),
                             tr(T_POWEROFF), tr(T_REBOOT), tr(T_ABOUT), tr(T_SETUP),
                             tr(T_COVERS) };
    return rows[std::clamp(i, 0, 12)];
}

std::string settings_value(int i)
{
    char b[160];
    switch (i) {
    case 0: snprintf(b, sizeof(b), "%d%%", app.settings.brightness); return b;
    case 1: snprintf(b, sizeof(b), "%d%%", app.settings.volume); return b;
    case 2: return app.bt_on ? tr(T_ENABLED) : tr(T_DISABLED);
    case 3:
        if (app.wifi_state == "NOHW") return tr(T_WIFI_UNAVAILABLE);
        return app.wifi_link.empty() ? tr(T_WIFI_DISCONNECTED) : app.wifi_link;
    case 4: return theme_name(app.settings.theme);
    case 5: return lang_name(app.settings.language);
    case 6:
        if (app.upd_busy)
            return app.upd_op == "apply" ? tr(T_UPDATE_INSTALLING)
                                         : tr(T_UPDATE_CHECKING);
        if (app.upd_state == "available") return fmt(tr(T_UPDATE_AVAILABLE), app.upd_version.c_str());
        if (app.upd_state == "uptodate") return tr(T_UPDATE_UPTODATE);
        if (app.upd_state == "ready") return tr(T_UPDATE_REBOOT);
        if (app.upd_state == "nowifi") return tr(T_UPDATE_NOWIFI);
        if (app.upd_state == "failed")
            return fmt(tr(T_UPDATE_FAILED),
                       app.upd_msg.empty() ? "?" : app.upd_msg.c_str());
        return tr(T_UPD_CHECK);
    case 7: snprintf(b, sizeof(b), tr(T_SAVES_COUNT_FMT), app.saves.size()); return b;
    case 12:
        if (app.cov_state == "running" ||
            (app.setup_busy && app.setup_op == "covers"))
            return tr(T_COVER_RUNNING);
        if (app.cov_total > 0) {
            snprintf(b, sizeof(b), tr(T_COVER_QUEUE_FMT), app.cov_total);
            return b;
        }
        return "";
    default: return "";
    }
}

void settings_icon(SDL_Renderer* r, int idx, int x, int y, int size, SDL_Color c)
{
    switch (idx) {
    case 0: icon_sun(r, x, y, size, c); break;
    case 1: icon_speaker(r, x, y, size, c); break;
    case 2: icon_bt(r, x, y, size, c); break;
    case 3: icon_wifi(r, x, y, size, c); break;
    case 4: icon_image(r, x, y, size, c); break;
    case 5: icon_globe(r, x, y, size, c); break;
    case 6: icon_download(r, x, y, size, c); break;
    case 7: icon_clock(r, x, y, size, c); break;
    case 8: icon_power(r, x, y, size, c); break;
    case 9: icon_refresh(r, x, y, size, c); break;
    case 10: icon_gear(r, x, y, size, c); break;
    case 11: icon_gamepad(r, x, y, size, c); break;
    default: icon_image(r, x, y, size, c); break;
    }
}

void deck_settings(SDL_Renderer* r)
{
    deck_header(r, tr(T_SETTINGS));
    app.set_sel_f = damp(app.set_sel_f, (float)app.set_sel);

    const int tw = 452, th = 80, gx = 12, gy = 8;
    for (int i = 0; i < 13; i++) {
        int col = i % 2, row = i / 2;
        SDL_Rect tile = { 48 + col * (tw + gx), 84 + row * (th + gy), tw, th };
        bool sel = i == app.set_sel;
        SDL_Color vc = C_DIM;
        if (i == 2 || i == 3) vc = (i == 2 ? app.bt_on : !app.wifi_link.empty())
                                       ? C_GREEN : C_DIM;
        if (i == 4 || i == 5) vc = C_ACCENT;
        if (sel) card(r, tile, 20, true, 238);
        else {
            rrect(r, tile, 20, rgba(255, 255, 255, 7));
            rrect_stroke(r, tile, 20, rgba(255, 255, 255, 9), 1);
        }
        settings_icon(r, i, tile.x + 18, tile.y + 14, 26,
                      (i == 8) ? C_RED : sel ? C_ACCENT : C_DIM);
        text(r, app.f_body, settings_label(i), tile.x + 58, tile.y + 10,
             i == 8 ? C_RED : C_TEXT);
        std::string v = settings_value(i);
        if (!v.empty())
            text(r, app.f_small, ellipsize(app.f_small, v, tw - 80),
                 tile.x + 58, tile.y + 46, vc);
        // Tile first, slider second: ui_hit_at() walks the list backwards,
        // so the smaller track must be registered last to win the touch.
        hit_add(tile, HIT_SET_TILE, i);
        if (i == 0 || i == 1) {
            int pct = i == 0 ? app.settings.brightness : app.settings.volume;
            SDL_Rect track = { tile.x + tw - 190, tile.y + 46, 160, 24 };
            bool adj = app.set_adjust == i;
            if (adj) rrect_stroke(r, { track.x - 8, track.y - 4, track.w + 16, 32 },
                                  16, with_alpha(C_ACCENT, 210), 1);
            rrect(r, { track.x, track.y + 9, track.w, 6 }, 3, with_alpha(C_DIM, 60));
            rrect(r, { track.x, track.y + 9, track.w * pct / 100, 6 }, 3, C_ACCENT);
            circle(r, track.x + track.w * pct / 100, track.y + 12, adj ? 10 : 8,
                   C_TEXT);
            hit_add(track, HIT_SET_SLIDER, i);
        }
    }
    deck_hint(r, tr(app.set_adjust >= 0 ? T_SET_HINT_ADJUST : T_SET_HINT_GRID));
}

// ---------------------------------------------------------------------------
// shared on-screen keyboard
// ---------------------------------------------------------------------------
void deck_kb(SDL_Renderer* r)
{
    const std::string& buf = app.kb_generic ? *app.kb_buf : app.wifi_pw;
    std::string title = app.kb_generic ? app.kb_title : std::string(tr(T_WIFI_PASSWORD));
    deck_header(r, nullptr);
    text(r, app.f_title, ellipsize(app.f_title, title, W - 96), 48, 68, C_TEXT);
    if (!app.kb_generic && !app.wifi_target.empty())
        text(r, app.f_small, ellipsize(app.f_small, app.wifi_target, W - 96),
             48, 120, C_DIM);

    SDL_Rect field = { 48, 150, 928, 76 };
    rrect(r, field, 22, rgba(255, 255, 255, 12));
    rrect_stroke(r, field, 22, with_alpha(C_ACCENT, 90), 1);
    std::string disp = app.kb_mask ? std::string(buf.size(), '*') : buf;
    const int dispw = field.w - 48;
    while (!disp.empty() && text_width(app.f_body, disp + "_") > dispw) {
        size_t len = 1;
        unsigned char ch = (unsigned char)disp[0];
        if (ch >= 0xF0) len = 4; else if (ch >= 0xE0) len = 3;
        else if (ch >= 0xC0) len = 2;
        disp.erase(0, len);
    }
    text(r, app.f_body, disp, field.x + 22, field.y + 16, C_TEXT);
    int cx = field.x + 22 + text_width(app.f_body, disp);
    rrect(r, { cx + 2, field.y + 18, 3, 38 }, 1, C_ACCENT);

    const auto& rows = kb_rows();
    const int kh = 64, gap = 10, ky0 = 262;
    for (int ri = 0; ri < (int)rows.size(); ri++) {
        const auto& row = rows[ri];
        std::vector<int> widths;
        int total = 0;
        if (ri == (int)rows.size() - 1) {
            widths = { 170, 340, 140, 180, 78 };
        } else {
            widths.assign(row.size(), 84);
        }
        for (size_t k = 0; k < widths.size(); k++)
            total += widths[k] + (k ? gap : 0);
        int x = (W - total) / 2;
        int y = ky0 + ri * (kh + gap);
        for (int ci = 0; ci < (int)row.size(); ci++) {
            int w = ci < (int)widths.size() ? widths[ci] : 84;
            bool sel = app.kb_r == ri && app.kb_c == ci;
            rrect(r, { x, y, w, kh }, 16,
                  sel ? with_alpha(C_ACCENT, 200) : rgba(255, 255, 255, 10));
            if (sel) rrect_stroke(r, { x, y, w, kh }, 16, C_ACCENT, 2);
            std::string lab = row[ci];
            if (lab == "ABC") lab = app.kb_shift ? "abc" : "ABC";
            else if (lab == " ") lab = tr(T_KB_SPACE);
            else if (lab == "DEL") lab = tr(T_KB_DEL);
            else if (lab == "OK") lab = tr(T_KB_OK);
            else if (lab == "X") lab = tr(T_KB_CANCEL);
            else if (app.kb_shift && lab.size() == 1)
                lab[0] = (char)toupper((unsigned char)lab[0]);
            text(r, app.f_body, ellipsize(app.f_body, lab, w - 14),
                 x + w / 2, y + 14, sel ? C_TEXT : with_alpha(C_TEXT, 225),
                 Align::Center);
            hit_add({ x, y, w, kh }, HIT_KB_KEY, ri, ci);
            x += w + gap;
        }
    }
    deck_hint(r, tr(T_KB_DECK_HINT));
}

// ---------------------------------------------------------------------------
// wifi deck
// ---------------------------------------------------------------------------
void deck_wifi(SDL_Renderer* r)
{
    deck_header(r, tr(T_WIFI));

    const int top = 96, rowh = 84, vis = 6;
    int n = (int)app.wifi_nets.size();
    if (app.wifi_sel < app.wifi_scroll) app.wifi_scroll = app.wifi_sel;
    if (app.wifi_sel >= app.wifi_scroll + vis) app.wifi_scroll = app.wifi_sel - vis + 1;
    if (app.wifi_scroll < 0) app.wifi_scroll = 0;
    SDL_Rect rows = { 48, top, 928, vis * rowh - 6 };
    hit_add(rows, HIT_LIST_ROWS);

    for (int i = 0; i < vis; i++) {
        int idx = app.wifi_scroll + i;
        if (idx >= n) break;
        int y = top + i * rowh;
        const WifiNet& w = app.wifi_nets[idx];
        SDL_Rect rc = { 48, y, 928, rowh - 8 };
        bool sel = idx == app.wifi_sel;
        bool usable = w.saved && (!w.secure || w.keyed);
        deck_row(r, rc, sel, w.ssid,
                 usable ? tr(T_WIFI_SAVED) : !w.secure ? tr(T_WIFI_OPEN) : "",
                 "", C_DIM);
        int bars = w.signal >= -55 ? 4 : w.signal >= -67 ? 3 : w.signal >= -78 ? 2 : 1;
        for (int b = 0; b < 4; b++) {
            int h = 6 + b * 5;
            fill_rect(r, rc.x + rc.w - 62 + b * 13, rc.y + rc.h / 2 + 14 - h,
                      8, h, b < bars ? g_accent : with_alpha(C_DIM, 80));
        }
        if (usable) icon_check(r, rc.x + rc.w - 132, rc.y + 26, 26, C_GREEN);
        hit_add(rc, HIT_WIFI_NET, idx);
    }
    if (n == 0) {
        const char* msg = app.wifi_state == "NOHW" ? tr(T_WIFI_UNAVAILABLE)
                        : app.wifi_busy ? tr(T_WIFI_SCANNING)
                                        : tr(T_WIFI_EMPTY);
        text(r, app.f_body, msg, W / 2, top + 150, C_DIM, Align::Center);
    }

    std::string st = app.wifi_busy ? tr(T_PROCESSING) : app.wifi_status;
    if (!st.empty())
        text(r, app.f_small, ellipsize(app.f_small, st, W - 200),
             W / 2, 646, C_AMBER, Align::Center);

    deck_round(r, { 48, 672, 64, 64 });
    icon_chevron(r, 60, 684, 40, C_TEXT, true);
    hit_add({ 48, 672, 64, 64 }, HIT_BACK);

    if (app.wifi_sel >= 0 && app.wifi_sel < n && app.wifi_nets[app.wifi_sel].saved) {
        SDL_Rect fg = { 136, 672, 200, 64 };
        button(r, fg, tr(T_FORGET), false, false);
        hit_add(fg, HIT_WIFI_TEST);          // forget the selected network
    }

    SDL_Rect scan = { W - 48 - 200, 672, 200, 64 };
    button(r, scan, app.wifi_busy ? tr(T_SCANNING) : tr(T_SCAN), false, false);
    hit_add(scan, HIT_WIFI_SCAN);
    deck_hint(r, tr(T_TOUCH_WIFI));
}

// ---------------------------------------------------------------------------
// box-art deck (same layout as the wizard's cover step)
// ---------------------------------------------------------------------------
void deck_covers(SDL_Renderer* r)
{
    deck_header(r, tr(T_COVERS));

    bool running = app.cov_state == "running" ||
                   (app.setup_busy && app.setup_op == "covers");
    bool counting = app.setup_busy && app.setup_op == "covers-count";

    SDL_Rect ringrc = { 48, 100, 280, 280 };
    card(r, ringrc, 32, false, 230);
    float p = app.cov_total > 0 ? (float)app.cov_done / app.cov_total : 0;
    int cx = ringrc.x + ringrc.w / 2, cy = ringrc.y + 120;
    ring(r, cx, cy, 74, 10, with_alpha(C_DIM, 50));
    arc(r, cx, cy, 74, -90, -90 + 360 * p, 10, C_ACCENT);
    char b[256];
    snprintf(b, sizeof(b), "%d%%", (int)(p * 100));
    text(r, app.f_title, b, cx, cy - 24, C_TEXT, Align::Center);
    snprintf(b, sizeof(b), "%d / %d", app.cov_done, app.cov_total);
    text(r, app.f_small, b, cx, ringrc.y + 230, C_DIM, Align::Center);

    SDL_Rect info = { 356, 100, 620, 280 };
    rrect(r, info, 30, rgba(255, 255, 255, 7));
    rrect_stroke(r, info, 30, rgba(255, 255, 255, 10), 1);
    text(r, app.f_body, tr(T_COVERS), info.x + 32, info.y + 34, C_TEXT);
    int y = info.y + 90;
    for (const auto& l : wrap(app.f_small, tr(T_SETUP_COVER_HELP), info.w - 64)) {
        text(r, app.f_small, l, info.x + 32, y, C_DIM);
        y += 30;
    }
    if (app.cov_total > 0) {
        snprintf(b, sizeof(b), tr(T_COVER_PROGRESS_FMT),
                 app.cov_done, app.cov_total, app.cov_ok, app.cov_miss);
        text(r, app.f_small, ellipsize(app.f_small, b, info.w - 64),
             info.x + 32, info.y + info.h - 66, C_ACCENT);
    } else {
        snprintf(b, sizeof(b), tr(T_COVER_QUEUE_FMT), app.cov_total);
        text(r, app.f_small,
             counting ? tr(T_PROCESSING) : ellipsize(app.f_small, b, info.w - 64),
             info.x + 32, info.y + info.h - 66, C_DIM);
    }
    if (!app.cov_current.empty())
        text(r, app.f_small, ellipsize(app.f_small, app.cov_current, info.w - 64),
             info.x + 32, info.y + info.h - 36, with_alpha(C_DIM, 170));

    SDL_Rect act = { 48, 420, 452, 84 };
    button(r, act, running ? tr(T_COVER_STOP) : tr(T_COVER_START),
           true, !counting);
    hit_add(act, HIT_COVER_ACTION);

    if (!app.setup_status.empty())
        text(r, app.f_small, ellipsize(app.f_small, app.setup_status, W - 300),
             W / 2, 552, C_AMBER, Align::Center);

    deck_round(r, { 48, 672, 64, 64 });
    icon_chevron(r, 60, 684, 40, C_TEXT, true);
    hit_add({ 48, 672, 64, 64 }, HIT_BACK);
    deck_hint(r, tr(T_COVER_IDLE));
}

// ---------------------------------------------------------------------------
// bluetooth deck
// ---------------------------------------------------------------------------
void deck_bt(SDL_Renderer* r)
{
    deck_header(r, tr(T_BT));
    SDL_Rect sw = { 48, 88, 928, 84 };
    rrect(r, sw, 20, rgba(255, 255, 255, 9));
    rrect_stroke(r, sw, 20, rgba(255, 255, 255, 12), 1);
    icon_bt(r, sw.x + 22, sw.y + 22, 38, app.bt_on ? C_ACCENT : C_DIM);
    text(r, app.f_body, tr(T_BT_SWITCH), sw.x + 76, sw.y + 22, C_TEXT);
    text(r, app.f_small, app.bt_on ? tr(T_ENABLED) : tr(T_DISABLED),
         sw.x + 76, sw.y + 54, app.bt_on ? C_GREEN : C_DIM);
    toggle(r, { sw.x + sw.w - 92, sw.y + 24, 68, 36 }, app.bt_on ? 1.0f : 0.0f,
           C_ACCENT);
    hit_add(sw, HIT_BT_TOGGLE);

    if (!app.bt_on)
        text(r, app.f_small, ellipsize(app.f_small, tr(T_BT_OFF_HELP), W - 200),
             W / 2, 214, C_DIM, Align::Center);

    if (app.bt_on) {
        const int top = 196, rowh = 84, vis = 5;
        int n = (int)app.bt_devs.size();
        if (app.bt_sel - 1 < app.bt_scroll) app.bt_scroll = app.bt_sel - 1;
        if (app.bt_sel - 1 >= app.bt_scroll + vis) app.bt_scroll = app.bt_sel - vis;
        if (app.bt_scroll < 0) app.bt_scroll = 0;
        hit_add({ 48, top, 928, vis * rowh - 6 }, HIT_LIST_ROWS);
        for (int i = 0; i < vis; i++) {
            int idx = app.bt_scroll + i;
            if (idx >= n) break;
            const BtDev& d = app.bt_devs[idx];
            SDL_Rect rc = { 48, top + i * rowh, 928, rowh - 8 };
            std::string meta = bt_kind_name(d.kind);
            if (d.connected) meta += tr(T_CONN_SUFFIX);
            else if (d.paired) meta += tr(T_PAIRED_SUFFIX);
            deck_row(r, rc, app.bt_sel == idx + 1,
                     d.name.empty() ? d.mac : d.name,
                     meta, "", C_DIM);
            if (d.connected) icon_check(r, rc.x + rc.w - 56, rc.y + 26, 28, C_GREEN);
            hit_add(rc, HIT_BT_DEV, idx);
        }
        if (n == 0 && !app.bt_busy)
            text(r, app.f_body, tr(T_NO_DEVS), W / 2, top + 150, C_DIM, Align::Center);
    }

    std::string st = app.bt_busy ? tr(T_PROCESSING) : app.bt_status;
    if (!st.empty())
        text(r, app.f_small, ellipsize(app.f_small, st, W - 240),
             W / 2, 648, C_AMBER, Align::Center);

    deck_round(r, { 48, 672, 64, 64 });
    icon_chevron(r, 60, 684, 40, C_TEXT, true);
    hit_add({ 48, 672, 64, 64 }, HIT_BACK);
    if (app.bt_on && app.bt_sel > 0 && app.bt_sel <= (int)app.bt_devs.size() &&
        app.bt_devs[app.bt_sel - 1].paired) {
        SDL_Rect fg = { 136, 672, 200, 64 };
        button(r, fg, tr(T_FORGET), false, false);
        hit_add(fg, HIT_BT_FORGET);
    }
    if (app.bt_on) {
        SDL_Rect sc = { W - 48 - 200, 672, 200, 64 };
        button(r, sc, app.bt_busy ? tr(T_SCANNING) : tr(T_SCAN), false, false);
        hit_add(sc, HIT_BT_SCAN);
        deck_hint(r, tr(T_TOUCH_LIST));
    }
}

// ---------------------------------------------------------------------------
// saves deck
// ---------------------------------------------------------------------------
void deck_saves(SDL_Renderer* r)
{
    char b[64];
    snprintf(b, sizeof(b), tr(T_SAVE_COUNT_FMT), app.saves.size());
    deck_header(r, tr(T_SAVES), b);

    const int top = 88, rowh = 82, vis = 6;
    if (app.saves.empty()) {
        text(r, app.f_body, tr(T_NO_SAVES), W / 2, 240, C_TEXT, Align::Center);
        int y = 300;
        for (const auto& l : wrap(app.f_small, tr(T_SAVES_HELP), W - 240)) {
            text(r, app.f_small, l, W / 2, y, C_DIM, Align::Center);
            y += 32;
        }
    } else {
        if (app.save_sel < app.save_scroll) app.save_scroll = app.save_sel;
        if (app.save_sel >= app.save_scroll + vis) app.save_scroll = app.save_sel - vis + 1;
        hit_add({ 48, top, 928, vis * rowh - 6 }, HIT_LIST_ROWS);
        for (int i = 0; i < vis; i++) {
            int idx = app.save_scroll + i;
            if (idx >= (int)app.saves.size()) break;
            const SaveEntry& s = app.saves[idx];
            char kb[32];
            snprintf(kb, sizeof(kb), "%ld KB", s.size / 1024);
            SDL_Rect rc = { 48, top + i * rowh, 928, rowh - 8 };
            deck_row(r, rc, idx == app.save_sel, s.name, "", kb, C_DIM);
            hit_add(rc, HIT_SAVE_ROW, idx);
        }
    }

    deck_round(r, { 48, 640, 64, 64 });
    icon_chevron(r, 60, 652, 40, C_TEXT, true);
    hit_add({ 48, 640, 64, 64 }, HIT_BACK);
    if (!app.saves.empty()) {
        SDL_Rect del = { W - 48 - 240, 640, 240, 64 };
        rrect(r, del, 18, with_alpha(C_RED, 36));
        rrect_stroke(r, del, 18, with_alpha(C_RED, 160), 1);
        icon_trash(r, del.x + 26, del.y + 18, 28, C_RED);
        text(r, app.f_body, tr(T_DELETE_SAVE), del.x + 70, del.y + 12, C_RED);
        hit_add(del, HIT_SAVE_DEL);
        deck_hint(r, tr(T_TOUCH_LIST));
    }
}

// ---------------------------------------------------------------------------
// update deck
// ---------------------------------------------------------------------------
void deck_update(SDL_Renderer* r)
{
    deck_header(r, tr(T_UPDATE));

    std::string head, detail;
    SDL_Color tone = C_TEXT;
    char b[192];
    bool downloading = app.upd_busy && app.upd_op == "apply" &&
                       app.upd_state == "downloading";
    if (downloading) {
        snprintf(b, sizeof(b), tr(T_UPDATE_DOWNLOADING), app.upd_pct);
        head = b;
        tone = C_ACCENT;
    } else if (app.upd_busy && app.upd_op == "apply") {
        head = tr(T_UPDATE_INSTALLING);
        tone = C_AMBER;
    } else if (app.upd_busy) {
        head = tr(T_UPDATE_CHECKING);
        tone = C_DIM;
    } else if (app.upd_state == "available") {
        snprintf(b, sizeof(b), tr(T_UPDATE_AVAILABLE), app.upd_version.c_str());
        head = b;
        detail = tr(T_UPDATE_CONFIRM_BODY);
        tone = C_GREEN;
    } else if (app.upd_state == "uptodate") {
        head = tr(T_UPDATE_UPTODATE);
        tone = C_GREEN;
    } else if (app.upd_state == "ready") {
        head = tr(T_UPDATE_READY);
        tone = C_GREEN;
    } else if (app.upd_state == "nowifi") {
        head = tr(T_UPDATE_NOWIFI);
        tone = C_AMBER;
    } else if (app.upd_state == "failed") {
        snprintf(b, sizeof(b), tr(T_UPDATE_FAILED),
                 app.upd_msg.empty() ? "?" : app.upd_msg.c_str());
        head = b;
        tone = C_RED;
    } else {
        detail = tr(T_UPDATE_CONFIRM_BODY);
    }

    SDL_Rect info = { 48, 92, 928, 224 };
    card(r, info, 30, false, 232);
    rrect(r, { info.x + 36, info.y + 44, 112, 112 }, 34,
          with_alpha(C_ACCENT, 24));
    icon_download(r, info.x + 68, info.y + 76, 48,
                  app.upd_busy ? with_alpha(C_ACCENT, 160) : C_ACCENT);

    const int tx = info.x + 176, tw = info.w - 176 - 40;
    char vb[128] = { 0 };
    bool has_ver = !app.upd_cur.empty();
    if (has_ver)
        snprintf(vb, sizeof(vb), tr(T_UPDATE_CURRENT), app.upd_cur.c_str());
    int hy;
    if (head.empty() && has_ver) {
        text(r, app.f_title, vb, tx, info.y + 52, C_TEXT);
        hy = info.y + 118;
    } else {
        if (has_ver) text(r, app.f_small, vb, tx, info.y + 34, C_DIM);
        hy = info.y + 72;
        auto lines = wrap(app.f_body, head, tw);
        for (size_t i = 0; i < lines.size() && i < 2; i++) {
            text(r, app.f_body, ellipsize(app.f_body, lines[i], tw), tx, hy,
                 tone);
            hy += 42;
        }
    }
    if (!detail.empty() && hy + 34 < info.y + info.h - 12) {
        text(r, app.f_small, ellipsize(app.f_small, detail, tw),
             tx, hy + 2, with_alpha(C_DIM, 235));
    }
    if (downloading) {
        progress_bar(r, { info.x + 36, info.y + info.h - 32, info.w - 72, 10 },
                     app.upd_pct / 100.0f, C_ACCENT, C_TRACK);
    }

    bool install = !app.upd_busy && app.upd_state == "available";
    bool reboot = !app.upd_busy && app.upd_state == "ready";
    const char* label = app.upd_busy
        ? (app.upd_op == "apply" ? tr(T_UPDATE_INSTALLING)
                                 : tr(T_UPDATE_CHECKING))
        : reboot ? tr(T_UPDATE_REBOOT)
        : install ? tr(T_UPD_DOWNLOAD)
        : (app.upd_state == "failed" || app.upd_state == "nowifi" ||
           app.upd_state == "uptodate") ? tr(T_UPD_RECHECK)
                                        : tr(T_UPD_CHECK);
    SDL_Rect act = { 48, 360, 928, 84 };
    if (app.upd_busy) {
        rrect(r, act, 24, rgba(255, 255, 255, 8));
        rrect_stroke(r, act, 24, rgba(255, 255, 255, 20), 1);
        text(r, app.f_body, label, W / 2, act.y + 24, C_DIM, Align::Center);
    } else {
        button(r, act, label, true, false);
        hit_add(act, reboot ? HIT_UPD_REBOOT
                            : install ? HIT_UPD_APPLY : HIT_UPD_CHECK);
    }

    deck_round(r, { 48, 672, 64, 64 });
    icon_chevron(r, 60, 684, 40, C_TEXT, true);
    hit_add({ 48, 672, 64, 64 }, HIT_BACK);
}

// ---------------------------------------------------------------------------
// activity deck
// ---------------------------------------------------------------------------
void deck_activity(SDL_Renderer* r)
{
    deck_header(r, tr(T_ACTIVITY));
    const char* tabs[4] = { tr(T_TAB_OVERVIEW), tr(T_TAB_TIMELINE),
                            tr(T_TAB_TREND), tr(T_TAB_PICKS) };
    int x = 48;
    for (int i = 0; i < 4; i++) {
        int w = text_width(app.f_body, tabs[i]) + 44;
        SDL_Rect tab = { x, 82, w, 52 };
        rrect(r, tab, 26, i == app.act_tab ? with_alpha(C_ACCENT, 46)
                                           : rgba(255, 255, 255, 8));
        if (i == app.act_tab) rrect_stroke(r, tab, 26, with_alpha(C_ACCENT, 190), 1);
        text(r, app.f_small, tabs[i], x + w / 2, 92,
             i == app.act_tab ? C_ACCENT : C_DIM, Align::Center);
        hit_add(tab, HIT_ACT_TAB, i);
        x += w + 10;
    }

    const int top = 154, rowh = 88;
    int n = act_row_count();
    if (app.act_tab == 2) {
        long long per[5] = { 0, 0, 0, 0, 0 };
        for (const auto& g : app.act_games) {
            int ci = console_index(g.sys);
            if (ci < 5) per[ci] += g.seconds;
        }
        long long pmx = 0;
        for (long long v : per) pmx = std::max(pmx, v);
        if (pmx == 0) {
            text(r, app.f_body, tr(T_ACT_NO_TREND), W / 2, 330, C_TEXT, Align::Center);
        } else {
            for (int i = 0; i < 5; i++) {
                int y = 168 + i * 92;
                text(r, app.f_body, consoles()[i].name_en, 48, y, C_TEXT);
                text(r, app.f_body, fmt_duration(per[i]), W - 48, y, C_DIM,
                     Align::Right);
                rrect(r, { 48, y + 44, 928, 12 }, 6, with_alpha(C_DIM, 55));
                int w = (int)(928 * per[i] / pmx);
                if (w > 0) rrect(r, { 48, y + 44, w, 12 }, 6, C_ACCENT);
            }
        }
    } else if (n == 0) {
        text(r, app.f_body, app.act_tab == 3 ? tr(T_ACT_PICKS_EMPTY)
                                             : tr(T_ACT_EMPTY),
             W / 2, 300, C_TEXT, Align::Center);
        text(r, app.f_small, app.act_tab == 3 ? tr(T_ACT_PICKS_HELP)
                                              : tr(T_ACT_EMPTY_HELP),
             W / 2, 356, C_DIM, Align::Center);
    } else {
        const int vis = 5;
        if (app.act_sel < app.act_scroll) app.act_scroll = app.act_sel;
        if (app.act_sel >= app.act_scroll + vis) app.act_scroll = app.act_sel - vis + 1;
        hit_add({ 48, top, 928, vis * rowh - 6 }, HIT_ACT_ROWS);
        for (int i = 0; i < vis; i++) {
            int idx = app.act_scroll + i;
            if (idx >= n) break;
            std::string title, meta, right;
            SDL_Color rc2 = C_DIM;
            if (app.act_tab == 0) {
                const GameStat& g = app.act_games[idx];
                title = g.title;
                meta = console_badge(g.sys);
                char b[64];
                snprintf(b, sizeof(b), tr(T_ACT_SESSIONS_FMT), g.sessions);
                right = fmt_duration(g.seconds);
                if (idx == 0) meta += " · " + std::string(b);
            } else if (app.act_tab == 1) {
                const PlaySession& s = app.act_sessions[idx];
                title = s.title;
                meta = fmt_when(s.start) + " · " + console_badge(s.sys);
                right = fmt_duration(s.seconds);
                rc2 = g_accent;
            } else {
                const Pick& p = app.act_picks[idx];
                title = p.title;
                const char* reasons[] = { tr(T_PICK_FAV_NEW), tr(T_PICK_FAV_BACK),
                                          tr(T_PICK_NEW), tr(T_PICK_BACK),
                                          tr(T_PICK_REGULAR) };
                meta = std::string(reasons[p.reason]) + " · " + console_badge(p.sys);
                if (p.idle_days > 0) {
                    char b[64];
                    snprintf(b, sizeof(b), tr(T_PICK_IDLE_FMT), p.idle_days);
                    right = b;
                } else if (p.seconds > 0) right = fmt_duration(p.seconds);
                rc2 = C_AMBER;
            }
            SDL_Rect rc = { 48, top + i * rowh, 928, rowh - 8 };
            deck_row(r, rc, idx == app.act_sel, title, meta, right, rc2);
            hit_add(rc, HIT_ACT_ROW, idx);
        }
    }

    deck_round(r, { 48, 640, 64, 64 });
    icon_chevron(r, 60, 652, 40, C_TEXT, true);
    hit_add({ 48, 640, 64, 64 }, HIT_BACK);
    if (n > 0 && app.act_tab != 2) {
        SDL_Rect play = { W - 48 - 280, 640, 280, 64 };
        button(r, play, tr(T_PLAY), true, false);
        hit_add(play, HIT_PLAY);
        deck_hint(r, tr(T_TOUCH_LIST));
    }
}

// ---------------------------------------------------------------------------
// favorites / history deck
// ---------------------------------------------------------------------------
void deck_quick(SDL_Renderer* r)
{
    deck_header(r, tr(T_QUICK));
    const char* tabs[2] = { tr(T_TAB_FAV), tr(T_TAB_HISTORY) };
    int x = 48;
    for (int i = 0; i < 2; i++) {
        int w = text_width(app.f_body, tabs[i]) + 48;
        SDL_Rect tab = { x, 82, w, 52 };
        rrect(r, tab, 26, i == app.quick_tab ? with_alpha(C_ACCENT, 46)
                                             : rgba(255, 255, 255, 8));
        if (i == app.quick_tab) rrect_stroke(r, tab, 26, with_alpha(C_ACCENT, 190), 1);
        text(r, app.f_small, tabs[i], x + w / 2, 92,
             i == app.quick_tab ? C_ACCENT : C_DIM, Align::Center);
        hit_add(tab, HIT_QUICK_TAB, i);
        x += w + 10;
    }

    const int top = 154, rowh = 88, vis = 5;
    if (app.quick.empty()) {
        text(r, app.f_body,
             app.quick_tab == 0 ? tr(T_QUICK_EMPTY_FAV) : tr(T_QUICK_EMPTY_HIST),
             W / 2, 280, C_TEXT, Align::Center);
        int y = 340;
        for (const auto& l : wrap(app.f_small,
                 app.quick_tab == 0 ? tr(T_QUICK_EMPTY_FAV_HELP)
                                    : tr(T_QUICK_EMPTY_HIST_HELP), W - 300)) {
            text(r, app.f_small, l, W / 2, y, C_DIM, Align::Center);
            y += 32;
        }
    } else {
        if (app.quick_sel < app.quick_scroll) app.quick_scroll = app.quick_sel;
        if (app.quick_sel >= app.quick_scroll + vis) app.quick_scroll = app.quick_sel - vis + 1;
        hit_add({ 48, top, 928, vis * rowh - 6 }, HIT_QUICK_ROWS);
        for (int i = 0; i < vis; i++) {
            int idx = app.quick_scroll + i;
            if (idx >= (int)app.quick.size()) break;
            const QuickEntry& e = app.quick[idx];
            std::string meta = console_badge(e.sys);
            if (e.missing) meta += " · " + std::string(tr(T_QUICK_MISSING));
            else if (e.last > 0)
                meta += " · " + fmt_when(e.last) + " · " + fmt_duration(e.seconds);
            SDL_Rect rc = { 48, top + i * rowh, 928, rowh - 8 };
            deck_row(r, rc, idx == app.quick_sel,
                     e.missing ? e.game.title : e.game.title, meta, "",
                     C_DIM);
            if (fav_contains(e.sys, e.game.title))
                icon_star(r, rc.x + rc.w - 58, rc.y + 26, 30, C_AMBER, true);
            hit_add(rc, HIT_QUICK_ROW, idx);
        }
    }

    deck_round(r, { 48, 640, 64, 64 });
    icon_chevron(r, 60, 652, 40, C_TEXT, true);
    hit_add({ 48, 640, 64, 64 }, HIT_BACK);
    if (!app.quick.empty()) {
        bool fav = fav_contains(app.quick[app.quick_sel].sys,
                                app.quick[app.quick_sel].game.title);
        deck_round(r, { 124, 640, 64, 64 }, fav);
        icon_star(r, 138, 654, 36, fav ? C_AMBER : C_TEXT, fav);
        hit_add({ 124, 640, 64, 64 }, HIT_FAV);
        SDL_Rect play = { W - 48 - 280, 640, 280, 64 };
        button(r, play, tr(T_PLAY), true, false);
        hit_add(play, HIT_PLAY);
        deck_hint(r, tr(T_TOUCH_LIST));
    }
}

// ---------------------------------------------------------------------------
// wizard deck
// ---------------------------------------------------------------------------
void setup_deck_back(SDL_Renderer* r, int y = 668)
{
    SDL_Rect rc = { 48, y, 64, 64 };
    deck_round(r, rc);
    icon_chevron(r, rc.x + 12, rc.y + 12, 40, C_TEXT, true);
    hit_add(rc, HIT_SETUP_PREV);
}

void deck_setup(SDL_Renderer* r)
{
    const char* title = tr(T_SETUP_TITLE);
    switch (app.setup_step) {
    case 0: title = tr(T_LANGUAGE); break;
    case 1: title = tr(T_WIFI); break;
    case 2: title = tr(T_SETUP_TZ_TITLE); break;
    case 3: title = tr(T_SETUP_NAME_TITLE); break;
    case 4: title = tr(T_SETUP_RA_TITLE); break;
    case 5: title = tr(T_COVERS); break;
    default: title = tr(T_SETUP_DONE_TITLE); break;
    }
    char sb[64];
    int shown = std::min(app.setup_step + 1, SETUP_STEPS - 1);
    snprintf(sb, sizeof(sb), tr(T_SETUP_STEP_FMT), shown, SETUP_STEPS - 1);
    deck_header(r, title, sb);

    if (app.setup_step == 0) {
        const int tw = 296, th = 132;
        for (int i = 0; i < (int)LANG_COUNT; i++) {
            int col = i % 3, row = i / 3;
            SDL_Rect tile = { 48 + col * (tw + 20), 96 + row * (th + 16), tw, th };
            bool sel = i == app.settings.language;
            if (sel) card(r, tile, 24, true, 238);
            else {
                rrect(r, tile, 24, rgba(255, 255, 255, 7));
                rrect_stroke(r, tile, 24, rgba(255, 255, 255, 10), 1);
            }
            icon_globe(r, tile.x + 24, tile.y + 22, 32, sel ? C_ACCENT : C_DIM);
            text(r, app.f_title, lang_name(i), tile.x + 24, tile.y + 66,
                 sel ? C_TEXT : with_alpha(C_TEXT, 210));
            if (sel) icon_check(r, tile.x + tw - 48, tile.y + 24, 28, C_ACCENT);
            hit_add(tile, HIT_SETUP_OPT, i, 0);
        }
        deck_hint(r, tr(T_TOUCH_LANG));
        return;
    }

    if (app.setup_step == 1) {
        const int top = 92, rowh = 84, vis = 5;
        int nets = (int)app.wifi_nets.size();
        int n = nets + 2;
        setup_scroll_into_view(vis);
        hit_add({ 48, top, 928, vis * rowh - 6 }, HIT_LIST_ROWS);
        for (int i = 0; i < vis; i++) {
            int idx = app.setup_scroll + i;
            if (idx >= n) break;
            SDL_Rect rc = { 48, top + i * rowh, 928, rowh - 8 };
            bool sel = idx == app.setup_sel;
            if (idx < nets) {
                const WifiNet& w = app.wifi_nets[idx];
                bool usable = w.saved && (!w.secure || w.keyed);
                std::string meta = usable ? tr(T_WIFI_SAVED)
                                 : !w.secure ? tr(T_WIFI_OPEN) : "";
                deck_row(r, rc, sel, w.ssid, meta, "", C_DIM);
                int bars = w.signal >= -55 ? 4 : w.signal >= -67 ? 3
                         : w.signal >= -78 ? 2 : 1;
                for (int b = 0; b < 4; b++) {
                    int h = 6 + b * 5;
                    fill_rect(r, rc.x + rc.w - 62 + b * 13,
                              rc.y + rc.h / 2 + 14 - h, 8, h,
                              b < bars ? g_accent : with_alpha(C_DIM, 80));
                }
            } else {
                deck_row(r, rc, sel,
                         idx == nets ? tr(T_WIFI_TEST) : tr(T_NEXT),
                         "", "", C_DIM);
            }
            hit_add(rc, HIT_SETUP_OPT, idx, 0);
        }
        std::string link = tr(T_WIFI_DISCONNECTED);
        SDL_Color lc = C_DIM;
        if (app.wifi_state == "NOHW") link = tr(T_WIFI_UNAVAILABLE);
        else if (!app.wifi_ip.empty()) {
            link = fmt(tr(T_WIFI_CONNECTED_FMT), app.wifi_ip.c_str());
            lc = C_GREEN;
        }
        text(r, app.f_small, ellipsize(app.f_small, link, W - 240),
             W / 2, 604, lc, Align::Center);
        std::string st = app.wifi_busy ? tr(T_PROCESSING) : app.wifi_status;
        if (!st.empty())
            text(r, app.f_small, ellipsize(app.f_small, st, W - 240),
                 W / 2, 642, C_AMBER, Align::Center);
        if (nets == 0 && !app.wifi_busy && app.wifi_state != "NOHW")
            text(r, app.f_body, ellipsize(app.f_body, tr(T_WIFI_EMPTY), W - 200),
                 W / 2, 344, C_DIM, Align::Center);

        setup_deck_back(r);
        SDL_Rect scan = { W - 48 - 160, 668, 160, 64 };
        button(r, scan, app.wifi_busy ? tr(T_SCANNING) : tr(T_SCAN), false, false);
        hit_add(scan, HIT_WIFI_SCAN);
        return;
    }

    if (app.setup_step == 2) {
        const auto& regs = tz_regions();
        int x = 48;
        for (int i = 0; i < (int)regs.size(); i++) {
            const char* nm = tr(regs[i].name);
            int w = text_width(app.f_small, nm) + 36;
            SDL_Rect chip = { x, 92, w, 48 };
            bool cur = i == app.setup_region;
            rrect(r, chip, 24, cur ? with_alpha(C_ACCENT, 44)
                                   : rgba(255, 255, 255, 8));
            if (cur) rrect_stroke(r, chip, 24, with_alpha(C_ACCENT, 190), 1);
            text(r, app.f_small, nm, chip.x + w / 2, 100,
                 cur ? C_ACCENT : C_DIM, Align::Center);
            hit_add(chip, HIT_SETUP_OPT, i, 0);
            x += w + 10;
        }
        const auto& zs = regs[app.setup_region].zones;
        const int top = 168, rowh = 82, vis = 5;
        setup_scroll_into_view(vis);
        for (int i = 0; i < vis; i++) {
            int idx = app.setup_scroll + i;
            if (idx >= (int)zs.size()) break;
            SDL_Rect rc = { 48, top + i * rowh, 928, rowh - 8 };
            bool sel = app.setup_col == 1 && idx == app.setup_sel;
            deck_row(r, rc, sel, zs[idx], "", app.setup_tz == zs[idx]
                                                     ? tr(T_TEST_OK) : "", C_GREEN);
            hit_add(rc, HIT_SETUP_OPT, idx, 1);
        }
        std::string preview = zs[std::clamp(app.setup_sel, 0, (int)zs.size() - 1)];
        text(r, app.f_small,
             fmt(tr(T_TZ_NOW_FMT), zone_time(preview).c_str()),
             W / 2, 608, C_DIM, Align::Center);
        text(r, app.f_small, ellipsize(app.f_small, tr(T_TOUCH_TZ), W - 500),
             124, 684, with_alpha(C_DIM, 160));
        SDL_Rect next = { W - 48 - 160, 668, 160, 64 };
        button(r, next, tr(T_NEXT), false, false);
        hit_add(next, HIT_SETUP_NEXT);
        setup_deck_back(r);
        return;
    }

    if (app.setup_step == 3) {
        SDL_Rect field = { 48, 96, 928, 84 };
        rrect(r, field, 22, rgba(255, 255, 255, 10));
        rrect_stroke(r, field, 22, with_alpha(C_ACCENT, 100), 1);
        icon_keyboard(r, field.x + 24, field.y + 24, 34, C_ACCENT);
        text(r, app.f_body, app.setup_name.empty() ? tr(T_SETUP_NAME_TITLE)
                                                   : app.setup_name,
             field.x + 76, field.y + 22, app.setup_name.empty() ? C_DIM : C_TEXT);
        hit_add(field, HIT_SETUP_OPT, 0, 0);

        SDL_Rect next = { 48, 200, 928, 84 };
        button(r, next, tr(T_NEXT), true, true);
        hit_add(next, HIT_SETUP_NEXT);

        int y = 320;
        for (const auto& l : wrap(app.f_small, tr(T_SETUP_NAME_HELP), 880)) {
            text(r, app.f_small, l, 60, y, C_DIM);
            y += 32;
        }
        auto rl = wrap(app.f_small, tr(T_NAME_INVALID), 880);
        y += 16;
        for (size_t i = 0; i < rl.size() && i < 3; i++) {
            text(r, app.f_small, rl[i], 60, y, with_alpha(C_DIM, 180));
            y += 32;
        }
        if (!app.setup_status.empty())
            text(r, app.f_small, ellipsize(app.f_small, app.setup_status, W - 300),
                 W / 2, 624, C_AMBER, Align::Center);
        setup_deck_back(r);
        deck_hint(r, tr(T_TOUCH_NAME));
        return;
    }

    if (app.setup_step == 4) {
        std::string login = tr(T_RA_LOGIN);
        if (app.setup_ra_logged)
            login = fmt(tr(T_RA_LOGGED_FMT), app.setup_ra_name.c_str());
        std::string vals[6] = {
            app.setup_ra_user,
            std::string(app.setup_ra_pass.size(), '*'),
            app.setup_ra_hardcore ? tr(T_ON) : tr(T_OFF),
            login, tr(T_RA_LOGOUT), tr(T_NEXT)
        };
        const int rowh = 80;
        for (int i = 0; i < 6; i++) {
            SDL_Rect rc = { 48, 92 + i * rowh, 928, rowh - 8 };
            std::string label;
            switch (i) {
            case 0: label = tr(T_RA_USER); break;
            case 1: label = tr(T_RA_PASS); break;
            case 2: label = tr(T_RA_HARDCORE); break;
            case 3: label = login; break;
            case 4: label = tr(T_RA_LOGOUT); break;
            default: label = tr(T_NEXT); break;
            }
            bool sel = i == app.setup_sel;
            std::string right = (i == 0 || i == 1) ? ellipsize(app.f_small, vals[i], 340)
                              : i == 2 ? vals[2] : "";
            if (i == 5) button(r, rc, label, true, sel);
            else deck_row(r, rc, sel, label, "", right,
                          i == 3 ? C_GREEN : C_DIM);
            hit_add(rc, HIT_SETUP_OPT, i, 0);
        }
        int y = 584;
        for (const auto& l : wrap(app.f_small, tr(T_RA_HARDCORE_HELP), 880)) {
            text(r, app.f_small, l, 60, y, with_alpha(C_DIM, 190));
            y += 30;
        }
        if (!app.setup_status.empty())
            text(r, app.f_small, ellipsize(app.f_small, app.setup_status, W - 300),
                 W / 2, 648, C_AMBER, Align::Center);
        setup_deck_back(r, 672);
        deck_hint(r, tr(T_TOUCH_LIST));
        return;
    }

    if (app.setup_step == 5) {
        SDL_Rect ringrc = { 48, 100, 280, 280 };
        card(r, ringrc, 32, false, 230);
        float p = app.cov_total > 0 ? (float)app.cov_done / app.cov_total : 0;
        int cx = ringrc.x + ringrc.w / 2, cy = ringrc.y + 120;
        ring(r, cx, cy, 74, 10, with_alpha(C_DIM, 50));
        arc(r, cx, cy, 74, -90, -90 + 360 * p, 10, C_ACCENT);
        char b[32];
        snprintf(b, sizeof(b), "%d%%", (int)(p * 100));
        text(r, app.f_title, b, cx, cy - 24, C_TEXT, Align::Center);
        snprintf(b, sizeof(b), "%d / %d", app.cov_done, app.cov_total);
        text(r, app.f_small, b, cx, ringrc.y + 230, C_DIM, Align::Center);

        SDL_Rect info = { 356, 100, 620, 280 };
        rrect(r, info, 30, rgba(255, 255, 255, 7));
        rrect_stroke(r, info, 30, rgba(255, 255, 255, 10), 1);
        text(r, app.f_body, tr(T_COVER_START), info.x + 32, info.y + 34, C_TEXT);
        int y = info.y + 90;
        for (const auto& l : wrap(app.f_small, tr(T_SETUP_COVER_HELP), info.w - 64)) {
            text(r, app.f_small, l, info.x + 32, y, C_DIM);
            y += 30;
        }
        if (app.cov_total > 0)
            progress_bar(r, { info.x + 32, info.y + info.h - 44, info.w - 64, 12 },
                         p, C_ACCENT, C_TRACK);
        if (!app.cov_current.empty())
            text(r, app.f_small, ellipsize(app.f_small, app.cov_current, info.w - 64),
                 info.x + 32, info.y + info.h - 84, with_alpha(C_DIM, 170));

        bool running = app.setup_busy && app.setup_op == "covers";
        SDL_Rect act = { 48, 420, 452, 84 };
        button(r, act, running ? tr(T_COVER_STOP) : tr(T_COVER_START), true, false);
        hit_add(act, HIT_SETUP_ACTION, 0);
        SDL_Rect next = { 524, 420, 452, 84 };
        button(r, next, tr(T_NEXT), false, false);
        hit_add(next, HIT_SETUP_NEXT);

        if (!app.setup_status.empty())
            text(r, app.f_small, ellipsize(app.f_small, app.setup_status, W - 300),
                 W / 2, 552, C_AMBER, Align::Center);
        setup_deck_back(r);
        deck_hint(r, tr(T_TOUCH_COVER));
        return;
    }

    // done page
    text(r, app.f_hero, tr(T_SETUP_DONE_TITLE), W / 2, 200, g_accent, Align::Center);
    auto lines = wrap(app.f_body, tr(T_SETUP_DONE_BODY), W - 240);
    int y = 300;
    for (const auto& l : lines) {
        text(r, app.f_body, l, W / 2, y, C_TEXT, Align::Center);
        y += 44;
    }
    SDL_Rect go = { 232, 460, 560, 96 };
    button(r, go, tr(T_PLAY), true, true);
    hit_add(go, HIT_SETUP_ACTION, 1);
    setup_deck_back(r, 640);
}

// ---------------------------------------------------------------------------
// dialogs
// ---------------------------------------------------------------------------
void deck_msg(SDL_Renderer* r)
{
    deck_header(r, tr(T_EMU_FAIL));
    SDL_Rect body = { 112, 160, 800, 240 };
    card(r, body, 30, false, 232);
    text(r, app.f_title, ellipsize(app.f_title, app.msg_title, body.w - 80),
         W / 2, body.y + 36, C_AMBER, Align::Center);
    auto lines = wrap(app.f_body, app.msg_body, body.w - 90);
    int y = body.y + 110;
    for (size_t i = 0; i < lines.size() && i < 3; i++) {
        text(r, app.f_body, ellipsize(app.f_body, lines[i], body.w - 90),
             W / 2, y, C_TEXT, Align::Center);
        y += 42;
    }
    SDL_Rect ok = { 332, 450, 360, 84 };
    button(r, ok, tr(T_KB_OK), true, true);
    hit_add(ok, HIT_DLG_OK);
}

void deck_confirm(SDL_Renderer* r)
{
    deck_header(r, nullptr);
    const char* title = app.confirm == ConfirmAct::PowerOff ? tr(T_CONFIRM_OFF)
                      : app.confirm == ConfirmAct::Reboot ? tr(T_CONFIRM_REBOOT)
                      : app.confirm == ConfirmAct::Update ? tr(T_UPDATE)
                      : (app.confirm == ConfirmAct::ForgetWifi ||
                         app.confirm == ConfirmAct::ForgetBt) ? tr(T_CONFIRM_FORGET)
                                                             : tr(T_CONFIRM_DEL);
    text(r, app.f_title, ellipsize(app.f_title, title, W - 160),
         W / 2, 180, C_TEXT, Align::Center);
    if (app.confirm == ConfirmAct::Update && !app.upd_version.empty())
        text(r, app.f_body, fmt(tr(T_UPDATE_CONFIRM), app.upd_version.c_str()),
             W / 2, 260, C_AMBER, Align::Center);
    else if (app.confirm == ConfirmAct::Update)
        text(r, app.f_small, tr(T_UPDATE_CONFIRM_BODY), W / 2, 260, C_DIM,
             Align::Center);

    SDL_Rect cancel = { 112, 360, 380, 96 };
    button(r, cancel, tr(T_KB_CANCEL), false, false);
    hit_add(cancel, HIT_DLG_CANCEL);
    SDL_Rect ok = { 532, 360, 380, 96 };
    button(r, ok, tr(T_KB_OK), true, true);
    hit_add(ok, HIT_DLG_OK);
}

void draw_deck_content(SDL_Renderer* r)
{
    if (app.wifi_kb) { deck_kb(r); return; }
    switch (app.screen) {
    case Screen::Home:      deck_home(r); break;
    case Screen::List:      deck_list(r); break;
    case Screen::Settings:  deck_settings(r); break;
    case Screen::Wifi:      deck_wifi(r); break;
    case Screen::Bluetooth: deck_bt(r); break;
    case Screen::Saves:     deck_saves(r); break;
    case Screen::Update:    deck_update(r); break;
    case Screen::Setup:     deck_setup(r); break;
    case Screen::Covers:    deck_covers(r); break;
    case Screen::Activity:  deck_activity(r); break;
    case Screen::Quick:     deck_quick(r); break;
    case Screen::Msg:       deck_msg(r); break;
    case Screen::Confirm:   deck_confirm(r); break;
    }
}

void draw_deck()
{
    if (!app.ren_bot) return;
    SDL_Renderer* r = app.ren_bot;
    if (app.screen != g_deck_last || app.setup_step != g_deck_last_step ||
        app.wifi_kb != g_deck_last_kb) {
        g_deck_last = app.screen;
        g_deck_last_step = app.setup_step;
        g_deck_last_kb = app.wifi_kb;
        g_deck_cur ^= 1;
        g_deck_start = SDL_GetTicks();
    }
    if (!g_deck_tex[0]) {
        for (auto& t : g_deck_tex) {
            t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_TARGET, W, H);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        }
        g_deck_start = SDL_GetTicks() - 1000;
    }
    if (!g_deck_tex[g_deck_cur]) return;

    SDL_SetRenderTarget(r, g_deck_tex[g_deck_cur]);
    ui_hit_begin();
    if (drawcall::begin(r, g_deck_tex[g_deck_cur], W, H)) {
        deck_base(r);
        draw_deck_content(r);
        deck_volume_osd(r);
        drawcall::end(r);
        drawcall::replay(r);
        drawcall::flush();
    } else {
        deck_base(r);
        draw_deck_content(r);
        deck_volume_osd(r);
    }
    SDL_SetRenderTarget(r, nullptr);

    // the deck follows the top panel's fade/slide, at a shorter distance
    Uint32 te = SDL_GetTicks() - g_deck_start;
    SDL_Rect full = { 0, 0, W, H };
    if (te < 200 && g_deck_tex[g_deck_cur ^ 1]) {
        float p = ease_out(te / 200.0f);
        SDL_SetTextureAlphaMod(g_deck_tex[g_deck_cur ^ 1], 255);
        perf::note_draw();
        SDL_RenderCopy(r, g_deck_tex[g_deck_cur ^ 1], nullptr, &full);
        SDL_SetTextureAlphaMod(g_deck_tex[g_deck_cur], (Uint8)(255 * p));
        SDL_Rect dst = { 0, (int)((1 - p) * 18), W, H };
        perf::note_draw();
        SDL_RenderCopy(r, g_deck_tex[g_deck_cur], nullptr, &dst);
    } else {
        SDL_SetTextureAlphaMod(g_deck_tex[g_deck_cur], 255);
        perf::note_draw();
        SDL_RenderCopy(r, g_deck_tex[g_deck_cur], nullptr, &full);
    }
    SDL_RenderPresent(r);
}

// ---------------------------------------------------------------------------
// touch dispatch
// ---------------------------------------------------------------------------
void deck_activate(int id, int a, int b)
{
    switch (id) {
    case HIT_BACK:
        go_back();
        break;
    case HIT_HOME:
        if (app.screen == Screen::Setup) setup_finish();
        else app.screen = Screen::Home;
        break;
    case HIT_SYS:
        if (a >= 0 && a < 5) {
            app.carousel = a;
            enter_selection();
        }
        break;
    case HIT_HUB:
        if (a == 0) { rebuild_activity(); app.screen = Screen::Activity; }
        else if (a == 1) { rebuild_quick(); app.screen = Screen::Quick; }
        else { app.bt_on = (system("/usr/bin/gameos-bt status 2>/dev/null | grep -q '^on$'") == 0);
               wifi_probe_start(); update_refresh(); app.screen = Screen::Settings;
               app.set_sel = 0; app.set_adjust = -1; }
        break;
    case HIT_RECENT:
        if (app.recent_ok) launch_one(consoles()[app.recent.con], app.recent.game);
        else { rebuild_quick(); app.screen = Screen::Quick; app.quick_tab = 1; rebuild_quick(); }
        break;
    case HIT_LIST_ROW:
        if (a >= 0 && a < (int)app.games.size()) {
            if (app.sel == a) do_launch();
            else { app.sel = a; sound_play(SND_MOVE); }
        }
        break;
    case HIT_FAV:
        toggle_fav();
        break;
    case HIT_FAV_FILTER:
        toggle_fav_only();
        break;
    case HIT_REFRESH:
        refresh_games();
        break;
    case HIT_PLAY:
        confirm_btn();      // launches the focused row on list/quick/activity
        break;
    case HIT_SET_TILE:
        app.set_sel = std::clamp(a, 0, 12);
        if (a <= 1) {
            app.set_adjust = a;
            sound_play(SND_ADJUST);
            break;
        }
        app.set_adjust = -1;
        if (a == 2) bt_enter();
        else if (a == 3) wifi_enter();
        else if (a == 4) cycle_theme();
        else if (a == 5) cycle_lang();
        else if (a == 6) update_enter();
        else if (a == 7) {
            app.saves = scan_saves();
            app.save_sel = 0;
            app.save_scroll = 0;
            app.screen = Screen::Saves;
        } else if (a == 8) { app.confirm = ConfirmAct::PowerOff; app.screen = Screen::Confirm; }
        else if (a == 9) { app.confirm = ConfirmAct::Reboot; app.screen = Screen::Confirm; }
        else if (a == 10) show_msg(tr(T_ABOUT), tr(T_ABOUT_BODY));
        else if (a == 11) setup_enter();
        else if (a == 12) covers_enter();
        else sound_play(SND_MOVE);
        break;
    case HIT_COVER_ACTION:
        covers_toggle();
        break;
    case HIT_WIFI_NET:
        if (a >= 0 && a < (int)app.wifi_nets.size()) {
            app.wifi_sel = a;
            wifi_press_net();
        }
        break;
    case HIT_WIFI_SCAN:
        if (!app.wifi_busy) {
            sound_play(SND_SWITCH);
            wifi_scan_now();
        }
        break;
    case HIT_WIFI_TEST:
        if (!app.wifi_busy && app.wifi_sel >= 0 &&
            app.wifi_sel < (int)app.wifi_nets.size() &&
            app.wifi_nets[app.wifi_sel].saved) {
            sound_play(SND_BACK);
            app.wifi_del_ssid = app.wifi_nets[app.wifi_sel].ssid;
            app.confirm = ConfirmAct::ForgetWifi;
            app.screen = Screen::Confirm;
        }
        break;
    case HIT_BT_FORGET:
        if (!app.bt_busy && app.bt_sel > 0 && app.bt_sel <= (int)app.bt_devs.size()) {
            const BtDev& d = app.bt_devs[app.bt_sel - 1];
            if (d.paired && bt_mac_valid(d.mac)) {
                sound_play(SND_BACK);
                app.bt_del_mac = d.mac;
                app.confirm = ConfirmAct::ForgetBt;
                app.screen = Screen::Confirm;
            }
        }
        break;
    case HIT_BT_TOGGLE:
        if (!app.bt_busy) {
            app.bt_status = app.bt_on ? tr(T_TURNING_OFF) : tr(T_TURNING_ON);
            bt_start(app.bt_on ? "down" : "up");
            app.bt_sel = 0;
        }
        break;
    case HIT_BT_DEV:
        if (!app.bt_busy && a >= 0 && a < (int)app.bt_devs.size()) {
            app.bt_sel = a + 1;
            confirm_btn();
        }
        break;
    case HIT_BT_SCAN:
        if (app.bt_on && !app.bt_busy) {
            sound_play(SND_SWITCH);
            app.bt_status = tr(T_SCANNING);
            bt_start("scan");
        }
        break;
    case HIT_SAVE_ROW:
        if (a >= 0 && a < (int)app.saves.size()) {
            if (app.save_sel == a) {
                app.save_del_idx = a;
                app.confirm = ConfirmAct::DeleteSave;
                app.screen = Screen::Confirm;
            } else { app.save_sel = a; sound_play(SND_MOVE); }
        }
        break;
    case HIT_SAVE_DEL:
        if (!app.saves.empty()) {
            app.save_del_idx = app.save_sel;
            app.confirm = ConfirmAct::DeleteSave;
            app.screen = Screen::Confirm;
        }
        break;
    case HIT_UPD_CHECK:
        if (!app.upd_busy) { sound_play(SND_CONFIRM); update_start("check"); }
        break;
    case HIT_UPD_APPLY:
        if (!app.upd_busy && app.upd_state == "available") {
            app.confirm = ConfirmAct::Update;
            app.screen = Screen::Confirm;
        }
        break;
    case HIT_UPD_REBOOT:
        if (!app.upd_busy && app.upd_state == "ready") {
            sound_play(SND_CONFIRM);
            app.exit_code = 91;
            app.running = false;
        }
        break;
    case HIT_ACT_TAB:
        if (a >= 0 && a < 4) {
            app.act_tab = a;
            app.act_sel = 0;
            app.act_scroll = 0;
            sound_play(SND_SWITCH);
        }
        break;
    case HIT_ACT_ROW:
        if (app.act_sel == a) confirm_btn();
        else if (a >= 0 && a < act_row_count()) { app.act_sel = a; sound_play(SND_MOVE); }
        break;
    case HIT_QUICK_TAB:
        if (a >= 0 && a < 2) {
            app.quick_tab = a;
            app.quick_sel = 0;
            rebuild_quick();
            sound_play(SND_SWITCH);
        }
        break;
    case HIT_QUICK_ROW:
        if (a >= 0 && a < (int)app.quick.size()) {
            if (app.quick_sel == a) confirm_btn();
            else { app.quick_sel = a; sound_play(SND_MOVE); }
        }
        break;
    case HIT_SETUP_OPT:
        app.setup_sel = a;
        if (app.setup_step == 2) app.setup_col = b;
        setup_activate();
        break;
    case HIT_SETUP_NEXT:
        setup_next();
        break;
    case HIT_SETUP_PREV:
        setup_prev();
        break;
    case HIT_SETUP_ACTION:
        if (a == 1) setup_finish();
        else {
            app.setup_sel = 0;
            setup_activate();
        }
        break;
    case HIT_KB_KEY:
        app.kb_r = a;
        app.kb_c = b;
        kb_press();
        sound_play(SND_MOVE);
        break;
    case HIT_DLG_OK:
        confirm_btn();
        break;
    case HIT_DLG_CANCEL:
        go_back();
        break;
    default:
        break;
    }
}

void deck_touch(const TouchEvent& e)
{
    if (e.phase == TouchPhase::Down) {
        const UiHit* h = ui_hit_at(e.x, e.y);
        app.t_id = h ? h->id : HIT_NONE;
        app.t_a = h ? h->a : 0;
        app.t_b = h ? h->b : 0;
        app.t_x0 = e.x;
        app.t_y0 = e.y;
        app.t_ly = e.y;
        app.t_at = SDL_GetTicks();
        app.t_moved = false;
        app.t_acc = 0;
        app.slider_drag = false;
        if (h && h->id == HIT_SET_SLIDER) {
            app.slider_drag = true;
            app.set_sel = h->a;
            app.set_adjust = h->a;
            g_slider_rect = h->rect;
            int pct = std::clamp((int)((e.x - h->rect.x) * 100 / std::max(1, h->rect.w)), 0, 100);
            if (h->a == 0) brightness_set(pct, false);
            else volume_set(pct, false);
        }
        return;
    }
    if (e.phase == TouchPhase::Move) {
        if (app.t_id == HIT_NONE) return;
        if (std::abs(e.x - app.t_x0) > 10 || std::abs(e.y - app.t_y0) > 10)
            app.t_moved = true;
        if (app.slider_drag) {
            int pct = std::clamp((int)((e.x - g_slider_rect.x) * 100 /
                                 std::max(1, g_slider_rect.w)), 0, 100);
            if (app.t_id == HIT_SET_SLIDER && app.t_a == 0) brightness_set(pct, false);
            else volume_set(pct, false);
            return;
        }
        if (!app.t_moved) return;
        int dy = e.y - app.t_ly;
        int dx = e.x - app.t_x0;
        app.t_ly = e.y;
        if (app.t_id == HIT_LIST_ROWS || app.t_id == HIT_ACT_ROWS ||
            app.t_id == HIT_QUICK_ROWS) {
            app.t_acc += dy;
            while (app.t_acc >= 52) { navigate(0, -1); app.t_acc -= 52; }
            while (app.t_acc <= -52) { navigate(0, 1); app.t_acc += 52; }
        } else if (app.t_id == HIT_SYS && app.t_a == -1 && std::abs(dx) > 70) {
            navigate(dx < 0 ? 1 : -1, 0);
            app.t_x0 = e.x;
        }
        return;
    }
    // Up
    if (app.slider_drag) {
        if (app.t_id == HIT_SET_SLIDER && app.t_a == 0) brightness_set(app.settings.brightness, true);
        else volume_set(app.settings.volume, true);
        app.slider_drag = false;
    } else if (app.t_id != HIT_NONE && !app.t_moved) {
        const UiHit* h = ui_hit_at(e.x, e.y);
        if (h && h->id == app.t_id && h->a == app.t_a && h->b == app.t_b)
            deck_activate(h->id, h->a, h->b);
    }
    app.t_id = HIT_NONE;
}

void volume_poll()
{
    Uint32 now = SDL_GetTicks();
    if (now - app.vol_poll_at < 250) return;
    app.vol_poll_at = now;
    // The async hardware write trails the UI value; do not let a stale state
    // file yank the slider back while the user is still adjusting it.
    if (app.slider_drag || now - app.slider_at < 1500) return;
    std::string s = read_file("/run/gameos/volume.level");
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    if (s.empty()) return;
    int v = atoi(s.c_str());
    if (v >= 0 && v <= 100 && v != app.settings.volume) {
        app.settings.volume = v;
        volume_show(v);
        settings_mark_dirty();
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
            if (!app.wifi_busy && !app.wifi_kb) {
                sound_play(SND_SWITCH);
                wifi_scan_now();
            }
        } else if (app.screen == Screen::Update) {
            if (!app.upd_busy) { sound_play(SND_SWITCH); update_start("check"); }
        } else if (app.screen == Screen::Setup) {
            if (app.setup_step == 1 && !app.wifi_busy && !app.setup_busy &&
                !app.wifi_kb) {
                sound_play(SND_SWITCH);
                wifi_scan_now();
            }
        } else if (app.screen == Screen::Settings) {
            sound_play(SND_SWITCH);
            app.bt_on = (system("/usr/bin/gameos-bt status 2>/dev/null | grep -q '^on$'") == 0);
            wifi_probe_start();
            update_refresh();
        } else if (app.screen == Screen::Saves) {
            sound_play(SND_ADJUST);
            int keep = app.save_sel;
            app.saves = scan_saves();
            app.save_sel = std::clamp(keep, 0, std::max(0, (int)app.saves.size() - 1));
        } else refresh_games();
        break;
    case SDL_CONTROLLER_BUTTON_Y:   // physical X (top)
        if (app.screen == Screen::Bluetooth) {
            if (!app.bt_busy && app.bt_sel > 0 && app.bt_sel <= (int)app.bt_devs.size()) {
                const BtDev& d = app.bt_devs[app.bt_sel - 1];
                if (d.paired && bt_mac_valid(d.mac)) {
                    sound_play(SND_BACK);
                    app.bt_del_mac = d.mac;
                    app.confirm = ConfirmAct::ForgetBt;
                    app.screen = Screen::Confirm;
                }
            }
        } else if (app.screen == Screen::Wifi) {
            if (!app.wifi_busy && !app.wifi_kb) {
                if (app.wifi_sel >= 0 && app.wifi_sel < (int)app.wifi_nets.size() &&
                    app.wifi_nets[app.wifi_sel].saved) {
                    sound_play(SND_BACK);
                    app.wifi_del_ssid = app.wifi_nets[app.wifi_sel].ssid;
                    app.confirm = ConfirmAct::ForgetWifi;
                    app.screen = Screen::Confirm;
                }
            }
        } else if (app.screen == Screen::Update) {
            // nothing to forget here
        } else if (app.screen == Screen::Home) {
            sound_play(SND_CONFIRM);
            rebuild_quick();
            app.quick_tab = 0;
            app.screen = Screen::Quick;
        } else toggle_fav();
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: toggle_fav_only(); break; // physical R
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: go_back(); break;          // physical L
    case SDL_CONTROLLER_BUTTON_BACK:   // physical HOME (adc-keys-home -> BTN_BACK)
    case SDL_CONTROLLER_BUTTON_GUIDE:  // physical MENU (BTN_MODE)
        // typing first: a stray MENU/HOME press should not discard the input
        if (app.wifi_kb) { sound_play(SND_BACK); kb_cancel(); }
        // leaving the wizard by any deliberate exit means "do not ask again"
        else if (app.screen == Screen::Setup) setup_finish();
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
            if (!app.wifi_busy && !app.wifi_kb) {
                sound_play(SND_SWITCH);
                wifi_scan_now();
            }
        } else if (app.screen == Screen::Update) {
            if (!app.upd_busy) { sound_play(SND_SWITCH); update_start("check"); }
        } else if (app.screen == Screen::Setup) {
            if (app.setup_step == 1 && !app.wifi_busy && !app.setup_busy &&
                !app.wifi_kb) {
                sound_play(SND_SWITCH);
                wifi_scan_now();
            }
        } else if (app.screen == Screen::Settings) {
            sound_play(SND_SWITCH);
            app.bt_on = (system("/usr/bin/gameos-bt status 2>/dev/null | grep -q '^on$'") == 0);
            wifi_probe_start();
            update_refresh();
        } else if (app.screen == Screen::Saves) {
            sound_play(SND_ADJUST);
            int keep = app.save_sel;
            app.saves = scan_saves();
            app.save_sel = std::clamp(keep, 0, std::max(0, (int)app.saves.size() - 1));
        } else refresh_games();
        break;
    case SDLK_f:
        if (app.screen == Screen::Bluetooth) {
            if (!app.bt_busy && app.bt_sel > 0 && app.bt_sel <= (int)app.bt_devs.size()) {
                const BtDev& d = app.bt_devs[app.bt_sel - 1];
                if (d.paired && bt_mac_valid(d.mac)) {
                    sound_play(SND_BACK);
                    app.bt_del_mac = d.mac;
                    app.confirm = ConfirmAct::ForgetBt;
                    app.screen = Screen::Confirm;
                }
            }
        } else if (app.screen == Screen::Wifi) {
            if (!app.wifi_busy && !app.wifi_kb) {
                if (app.wifi_sel >= 0 && app.wifi_sel < (int)app.wifi_nets.size() &&
                    app.wifi_nets[app.wifi_sel].saved) {
                    sound_play(SND_BACK);
                    app.wifi_del_ssid = app.wifi_nets[app.wifi_sel].ssid;
                    app.confirm = ConfirmAct::ForgetWifi;
                    app.screen = Screen::Confirm;
                }
            }
        } else if (app.screen == Screen::Update) {
            // nothing to forget here
        } else if (app.screen == Screen::Home) {
            sound_play(SND_CONFIRM);
            rebuild_quick();
            app.quick_tab = 0;
            app.screen = Screen::Quick;
        } else toggle_fav();
        break;
    case SDLK_TAB: toggle_fav_only(); break;
    case SDLK_MINUS:
        volume_set(app.settings.volume - 5, true);
        break;
    case SDLK_EQUALS:
        volume_set(app.settings.volume + 5, true);
        break;
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
        // right stick scrolls lists / moves across carousel tiles
        Sint16 rx = SDL_GameControllerGetAxis(app.pad, SDL_CONTROLLER_AXIS_RIGHTX);
        Sint16 ry = SDL_GameControllerGetAxis(app.pad, SDL_CONTROLLER_AXIS_RIGHTY);
        if (!dx && rx < -16000) { dx = -1; from_btn = false; } else if (!dx && rx > 16000) { dx = 1; from_btn = false; }
        if (!dy && ry < -16000) { dy = -1; from_btn = false; } else if (!dy && ry > 16000) { dy = 1; from_btn = false; }

        // L2/R2 are analog triggers in SDL2: use them as volume keys with
        // hysteresis so holding one steps the volume exactly once.
        static bool lt_down = false, rt_down = false;
        Sint16 lt = SDL_GameControllerGetAxis(app.pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        Sint16 rt = SDL_GameControllerGetAxis(app.pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (!lt_down && lt > 16000) { volume_set(app.settings.volume - 5, true); lt_down = true; }
        else if (lt_down && lt < 8000) lt_down = false;
        if (!rt_down && rt > 16000) { volume_set(app.settings.volume + 5, true); rt_down = true; }
        else if (rt_down && rt < 8000) rt_down = false;
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

void draw_current(SDL_Renderer* r)
{
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
    case Screen::Covers:    draw_covers(r); break;
    case Screen::Msg:       draw_msg(r); break;
    case Screen::Confirm:   draw_confirm(r); break;
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
        perf::frame_begin();
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
        covers_poll();

        TouchEvent te;
        while (touch_poll(te)) deck_touch(te);
        volume_poll();
        settings_flush(false);

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
        Uint32 now = SDL_GetTicks();
        static Uint32 last_frame = now;
        g_dt = std::min(0.05f, (now - last_frame) / 1000.0f);
        last_frame = now;

        // screen (or wizard step) change: snap the motion state of the screen
        // being entered, then fade/slide the new scene over the old one
        // A missing texture pair means there is no previous scene to fade
        // FROM: first frame after boot, or the first frame after the emulator
        // exited and shutdown_gfx() destroyed the targets. Without this check
        // the transition below draws g_scene_tex[cur ^ 1] while it still holds
        // uninitialized GPU memory, which showed up as the boot splash
        // flashing back for 200 ms right before the home screen.
        const bool had_scene = g_scene_tex[0] != nullptr;
        if (app.screen != g_last_screen || app.setup_step != g_last_setup_step) {
            switch (app.screen) {
            case Screen::Home:     app.car_f = (float)app.carousel; break;
            case Screen::List:     app.list_sel_f = (float)app.sel;
                                   app.list_scroll_f = (float)app.scroll; break;
            case Screen::Settings: app.set_sel_f = (float)app.set_sel; break;
            case Screen::Saves:    app.save_sel_f = (float)app.save_sel;
                                   app.save_scroll_f = (float)app.save_scroll; break;
            case Screen::Quick:    app.quick_sel_f = (float)app.quick_sel;
                                   app.quick_scroll_f = (float)app.quick_scroll;
                                   app.quick_tab_f = (float)app.quick_tab; break;
            case Screen::Activity: app.act_sel_f = (float)app.act_sel;
                                   app.act_scroll_f = (float)app.act_scroll;
                                   app.act_tab_f = (float)app.act_tab; break;
            default: break;
            }
            g_last_screen = app.screen;
            g_last_setup_step = app.setup_step;
            g_scene_cur ^= 1;
            g_trans_start = now;
        }

        if (!g_scene_tex[0]) {
            for (auto& t : g_scene_tex)
                t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_TARGET, W, H);
            SDL_SetTextureBlendMode(g_scene_tex[0], SDL_BLENDMODE_BLEND);
            SDL_SetTextureBlendMode(g_scene_tex[1], SDL_BLENDMODE_BLEND);
        }
        SDL_SetRenderTarget(r, g_scene_tex[g_scene_cur]);
        if (drawcall::begin(r, g_scene_tex[g_scene_cur], W, H)) {
            draw_current(r);
            drawcall::end(r);
            drawcall::replay(r);
            drawcall::flush();
        } else {
            draw_current(r);
        }
        SDL_SetRenderTarget(r, nullptr);

        const bool dialog = app.screen == Screen::Msg || app.screen == Screen::Confirm;
        const Uint32 dur = dialog ? 160 : 200;
        Uint32 trans_elapsed = now - g_trans_start;
        SDL_Rect full = { 0, 0, W, H };
        if (had_scene && trans_elapsed < dur) {
            float p = ease_out(trans_elapsed / (float)dur);
            SDL_SetTextureAlphaMod(g_scene_tex[g_scene_cur ^ 1], 255);
            perf::note_draw();
            SDL_RenderCopy(r, g_scene_tex[g_scene_cur ^ 1], nullptr, &full);
            SDL_SetTextureAlphaMod(g_scene_tex[g_scene_cur], (Uint8)(255 * p));
            SDL_Rect dst;
            if (dialog) {
                int dw = (int)(W * (0.92f + 0.08f * p));
                int dh = (int)(H * (0.92f + 0.08f * p));
                dst = { (W - dw) / 2, (H - dh) / 2, dw, dh };
            } else {
                dst = { 0, (int)((1 - p) * 26), W, H };
            }
            perf::note_draw();
            SDL_RenderCopy(r, g_scene_tex[g_scene_cur], nullptr, &dst);
        } else {
            SDL_SetTextureAlphaMod(g_scene_tex[g_scene_cur], 255);
            perf::note_draw();
            SDL_RenderCopy(r, g_scene_tex[g_scene_cur], nullptr, &full);
        }
        SDL_RenderPresent(r);
        draw_deck();
        perf::frame_end(screen_key(app.screen));
        // cap at ~60 fps: uncapped rendering burned CPU and battery for no
        // visible gain on a 60 Hz panel
        Uint32 frame_ms = SDL_GetTicks() - now;
        if (frame_ms < 16) SDL_Delay(16 - frame_ms);
    }

    // Only a console tile is a "last console"; Activity/Quick/Settings would
    // otherwise be clamped onto psp and reopen there next boot.
    if (app.carousel < 5) app.settings.last_console = app.carousel;
    save_settings(app.settings);
    shutdown_gfx();
    perf::shutdown();
    return app.exit_code;
}