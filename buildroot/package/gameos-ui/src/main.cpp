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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

using namespace gameos;

namespace {

constexpr int W = 1024;
constexpr int H = 768;

const SDL_Color C_TEXT   = { 232, 236, 247, 255 };
const SDL_Color C_DIM    = { 138, 147, 178, 255 };
const SDL_Color C_ACCENT = {  53, 208, 186, 255 };
const SDL_Color C_AMBER  = { 255, 180,  84, 255 };
const SDL_Color C_PANEL  = {  20,  27,  51, 235 };
const SDL_Color C_BG     = {  11,  16,  32, 255 };
const SDL_Color C_RED    = { 255,  99,  99, 255 };
const SDL_Color C_GREEN  = { 105, 220, 140, 255 };

enum class Screen { Home, List, Settings, Msg, Confirm };
enum class ConfirmAct { None, PowerOff, Reboot };

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
    SDL_Texture* icons[5] = {};
    SDL_Texture* icon_settings = nullptr;

    SDL_GameController* pad = nullptr;

    Settings settings;
    Screen screen = Screen::Home;
    int carousel = 0;              // 0..4 consoles, 5 = settings
    std::vector<Game> games;
    int sel = 0;
    int scroll = 0;
    int set_sel = 0;
    std::string msg_title, msg_body;
    ConfirmAct confirm = ConfirmAct::None;

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

void text(SDL_Renderer* r, TTF_Font* f, const std::string& s,
          int x, int y, SDL_Color c, Align align = Align::Left)
{
    if (!f || s.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surf) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surf);
    int tw = surf->w, th = surf->h;
    SDL_FreeSurface(surf);
    if (!t) return;
    int dx = x;
    if (align == Align::Center) dx = x - tw / 2;
    else if (align == Align::Right) dx = x - tw;
    SDL_Rect dst = { dx, y, tw, th };
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
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

std::string two(int v)
{
    char b[8];
    snprintf(b, sizeof(b), "%02d", v);
    return b;
}

// ---------------------------------------------------------------------------
// graphics init / teardown (also used around emulator launches)
// ---------------------------------------------------------------------------
SDL_Texture* load_tex(SDL_Renderer* r, const std::string& path)
{
    SDL_Texture* t = IMG_LoadTexture(r, path.c_str());
    return t; // nullptr is fine — fallbacks are drawn
}

void open_controller()
{
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        if (!SDL_IsGameController(i)) {
            // Register our virtual gamepad with SDL on the fly.
            const char* name = SDL_JoystickNameForIndex(i);
            if (name && strcmp(name, "gameOS Gamepad") == 0) {
                char guid[64];
                SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),
                                          guid, sizeof(guid));
                std::string map = std::string(guid) +
                    ",gameOS Gamepad,platform:Linux,"
                    "a:b0,b:b1,x:b3,y:b2,"
                    "back:b8,start:b9,guide:b10,"
                    "leftshoulder:b4,rightshoulder:b5,"
                    "lefttrigger:b6,righttrigger:b7,"
                    "leftstick:b12,rightstick:b13,"
                    "dpup:b14,dpdown:b15,dpleft:b16,dpright:b17,"
                    "leftx:a0,lefty:a1,";
                SDL_GameControllerAddMapping(map.c_str());
            }
        }
        if (SDL_IsGameController(i)) {
            app.pad = SDL_GameControllerOpen(i);
            if (app.pad) break;
        }
    }
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
    std::string assets = assets_dir();
    std::string noto = fonts + "/NotoSansSC-Regular.otf";
    std::string orbi = fonts + "/Orbitron.ttf";

    app.f_logo  = TTF_OpenFont(orbi.c_str(), 52);
    app.f_clock = TTF_OpenFont(orbi.c_str(), 96);
    app.f_title = TTF_OpenFont(noto.c_str(), 40);
    app.f_body  = TTF_OpenFont(noto.c_str(), 30);
    app.f_small = TTF_OpenFont(noto.c_str(), 22);
    if (!app.f_logo)  app.f_logo  = TTF_OpenFont(noto.c_str(), 48);
    if (!app.f_clock) app.f_clock = TTF_OpenFont(noto.c_str(), 90);
    if (!app.f_title) {
        fprintf(stderr, "gameos-ui: cannot open font %s\n", noto.c_str());
        return false;
    }

    int ndisp = SDL_GetNumVideoDisplays();
    app.win_top = SDL_CreateWindow("gameOS",
        SDL_WINDOWPOS_CENTERED_DISPLAY(0), SDL_WINDOWPOS_CENTERED_DISPLAY(0),
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

    if (ndisp > 1) {
        app.win_bot = SDL_CreateWindow("gameOS ambient",
            SDL_WINDOWPOS_CENTERED_DISPLAY(1), SDL_WINDOWPOS_CENTERED_DISPLAY(1),
            W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (app.win_bot) {
            app.ren_bot = SDL_CreateRenderer(app.win_bot, -1,
                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!app.ren_bot) {
                SDL_DestroyWindow(app.win_bot);
                app.win_bot = nullptr;
            }
        }
    }

    app.bg      = load_tex(app.ren_top, assets + "/home_bg.png");
    app.ambient = app.ren_bot ? load_tex(app.ren_bot, assets + "/ambient_bg.png") : nullptr;
    const char* ids[] = { "nds", "gba", "gbc", "ps1", "psp" };
    for (int i = 0; i < 5; i++)
        app.icons[i] = load_tex(app.ren_top,
            assets + "/console_" + ids[i] + ".png");
    app.icon_settings = load_tex(app.ren_top, assets + "/icon_settings.png");

    open_controller();
    return true;
}

void shutdown_gfx()
{
    if (app.pad) { SDL_GameControllerClose(app.pad); app.pad = nullptr; }
    for (auto& t : app.icons) { if (t) SDL_DestroyTexture(t); t = nullptr; }
    if (app.icon_settings) { SDL_DestroyTexture(app.icon_settings); app.icon_settings = nullptr; }
    if (app.bg)      { SDL_DestroyTexture(app.bg); app.bg = nullptr; }
    if (app.ambient) { SDL_DestroyTexture(app.ambient); app.ambient = nullptr; }
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
    text(r, app.f_logo, "gameOS", 32, 20, C_ACCENT);

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
    fill_rect(r, 0, H - 56, W, 56, { 8, 12, 26, 200 });
    text(r, app.f_small, s, W / 2, H - 42, C_DIM, Align::Center);
}

void draw_background(SDL_Renderer* r)
{
    if (app.bg) {
        SDL_Rect dst = { 0, 0, W, H };
        draw_tex(r, app.bg, dst);
    } else {
        fill_rect(r, 0, 0, W, H, C_BG);
        fill_rect(r, 0, 0, W, 6, C_ACCENT);
    }
}

// ---------------------------------------------------------------------------
// screens
// ---------------------------------------------------------------------------
void draw_home(SDL_Renderer* r)
{
    draw_background(r);
    draw_topbar(r);

    const auto& cons = consoles();
    int total = 6; // 5 consoles + settings

    for (int i = 0; i < total; i++) {
        int off = i - app.carousel;
        if (std::abs(off) > 2) continue;
        float scale = (off == 0) ? 1.0f : 0.68f;
        int size = (int)(250 * scale);
        int cx = W / 2 + off * 270;
        int cy = 330;
        Uint8 alpha = (off == 0) ? 255 : 130;

        SDL_Texture* icon = (i < 5) ? app.icons[i] : app.icon_settings;
        if (icon) {
            SDL_Rect dst = { cx - size / 2, cy - size / 2, size, size };
            draw_tex(r, icon, dst, alpha);
        } else {
            fill_rect(r, cx - size / 2, cy - size / 2, size, size, C_PANEL);
            text(r, app.f_title, (i < 5) ? cons[i].name_en : "SET",
                 cx, cy - 20, C_ACCENT, Align::Center);
        }
    }

    // selection ring / glow under the active icon
    fill_rect(r, W / 2 - 130, 470, 260, 4, C_ACCENT);

    std::string name, sub;
    if (app.carousel < 5) {
        const Console& c = cons[app.carousel];
        name = c.name_cn;
        const auto& games = games_for(app.carousel);
        char b[64];
        snprintf(b, sizeof(b), "%s · %zu 个游戏", c.name_en, games.size());
        sub = b;
    } else {
        name = "系统设置";
        sub = "亮度 · 音量 · 电源";
    }
    text(r, app.f_title, name, W / 2, 500, C_TEXT, Align::Center);
    text(r, app.f_small, sub, W / 2, 556, C_DIM, Align::Center);

    // carousel dots
    for (int i = 0; i < total; i++) {
        SDL_Color dc = (i == app.carousel) ? C_ACCENT : C_DIM;
        fill_rect(r, W / 2 - total * 12 + i * 24, 610, 10, 10, dc);
    }

    draw_hint(r, "← → 选择    Ⓐ 进入    START 设置");
}

void draw_list(SDL_Renderer* r)
{
    draw_background(r);
    const Console& c = consoles()[app.carousel];

    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, c.name_cn, 32, 26, C_TEXT);
    char b[64];
    snprintf(b, sizeof(b), "%zu 个游戏", app.games.size());
    text(r, app.f_body, b, W - 32, 34, C_DIM, Align::Right);
    fill_rect(r, 0, 96, W, 3, C_ACCENT);

    if (app.games.empty()) {
        text(r, app.f_body, "没有找到游戏", W / 2, 300, C_TEXT, Align::Center);
        std::string p = std::string("请把游戏文件复制到  /userdata/roms/") + c.id;
        text(r, app.f_small, p, W / 2, 360, C_DIM, Align::Center);
        text(r, app.f_small, "（把 TF 卡插到电脑上，打开 GAMEOS 盘符即可）",
             W / 2, 400, C_DIM, Align::Center);
    } else {
        const int rowh = 66, top = 116, vis = 9;
        if (app.sel < app.scroll) app.scroll = app.sel;
        if (app.sel >= app.scroll + vis) app.scroll = app.sel - vis + 1;
        for (int i = 0; i < vis; i++) {
            int idx = app.scroll + i;
            if (idx >= (int)app.games.size()) break;
            int y = top + i * rowh;
            if (idx == app.sel) {
                fill_rect(r, 24, y, W - 48, rowh - 6, C_PANEL);
                fill_rect(r, 24, y, 5, rowh - 6, C_ACCENT);
            }
            SDL_Color tc = (idx == app.sel) ? C_TEXT : C_DIM;
            text(r, app.f_body, app.games[idx].title, 48, y + 14, tc);
        }
        // scrollbar
        if ((int)app.games.size() > vis) {
            int trackh = vis * rowh;
            int barh = std::max(30, trackh * vis / (int)app.games.size());
            int bary = top + (trackh - barh) * app.scroll /
                       std::max(1, (int)app.games.size() - vis);
            fill_rect(r, W - 14, top, 4, trackh, { 60, 68, 96, 160 });
            fill_rect(r, W - 14, bary, 4, barh, C_ACCENT);
        }
    }
    draw_hint(r, "↑ ↓ 选择   Ⓐ 启动   Ⓑ 返回   Ⓨ 刷新");
}

void draw_settings(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, 96, C_PANEL);
    text(r, app.f_title, "系统设置", 32, 26, C_TEXT);
    fill_rect(r, 0, 96, W, 3, C_ACCENT);

    const char* rows[] = { "屏幕亮度", "音量", "关机", "重启", "关于 gameOS" };
    const int rowh = 84, top = 140;
    for (int i = 0; i < 5; i++) {
        int y = top + i * rowh;
        if (i == app.set_sel) {
            fill_rect(r, 120, y, W - 240, rowh - 10, C_PANEL);
            fill_rect(r, 120, y, 5, rowh - 10, C_ACCENT);
        }
        SDL_Color tc = (i == app.set_sel) ? C_TEXT : C_DIM;
        text(r, app.f_body, rows[i], 150, y + 18, tc);
        if (i == 0 || i == 1) {
            int v = (i == 0) ? app.settings.brightness : app.settings.volume;
            // value bar
            fill_rect(r, 560, y + 26, 240, 12, { 60, 68, 96, 160 });
            fill_rect(r, 560, y + 26, 240 * v / 100, 12, C_ACCENT);
            char b[16];
            snprintf(b, sizeof(b), "%d%%", v);
            text(r, app.f_body, b, 860, y + 14, tc, Align::Right);
        }
    }
    draw_hint(r, "↑ ↓ 选择    ← → 调节    Ⓐ 执行    Ⓑ 返回");
}

void draw_msg(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, H, { 5, 8, 18, 170 });
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
    text(r, app.f_small, "Ⓑ 返回", W / 2, py + ph - 52, C_DIM, Align::Center);
}

void draw_confirm(SDL_Renderer* r)
{
    draw_background(r);
    fill_rect(r, 0, 0, W, H, { 5, 8, 18, 170 });
    int pw = 640, ph = 300, px = (W - pw) / 2, py = (H - ph) / 2;
    fill_rect(r, px, py, pw, ph, C_PANEL);
    fill_rect(r, px, py, pw, 4, C_RED);
    const char* t = (app.confirm == ConfirmAct::PowerOff) ? "确认关机？" : "确认重启？";
    text(r, app.f_title, t, W / 2, py + 60, C_TEXT, Align::Center);
    text(r, app.f_body, "Ⓐ 确认        Ⓑ 取消", W / 2, py + 170, C_DIM, Align::Center);
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
        fill_rect(r, 0, H - 6, W, 6, C_ACCENT);
    }

    text(r, app.f_logo, "gameOS", W / 2, 170, C_ACCENT, Align::Center);

    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    std::string hhmm = two(tmv.tm_hour) + ":" + two(tmv.tm_min);
    text(r, app.f_clock, hhmm, W / 2, 260, C_TEXT, Align::Center);

    static const char* wd[] = { "日", "一", "二", "三", "四", "五", "六" };
    char date[96];
    snprintf(date, sizeof(date), "%d 年 %d 月 %d 日  星期%s",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, wd[tmv.tm_wday]);
    text(r, app.f_body, date, W / 2, 420, C_DIM, Align::Center);
    text(r, app.f_small, "为 Anbernic RG DS Plus 打造", W / 2, 500, C_DIM, Align::Center);
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

void enter_selection()
{
    if (app.carousel == 5) {
        app.screen = Screen::Settings;
        app.set_sel = 0;
        return;
    }
    const Console& c = consoles()[app.carousel];
    if (c.dual && !nds_bios_ok()) {
        show_msg("需要 NDS BIOS 文件",
                 "缺少：" + bios_missing_list() + "\n"
                 "请把你自己的 NDS BIOS 文件复制到 TF 卡的\n"
                 "bios 文件夹（GAMEOS 盘符 → bios）。\n"
                 "需要 bios7.bin、bios9.bin、firmware.bin 三个文件。");
        return;
    }
    invalidate_games(app.carousel);   // entering a console: force a fresh scan
    app.games = games_for(app.carousel);
    app.sel = 0;
    app.scroll = 0;
    app.screen = Screen::List;
}

// Explicit refresh (Y button / R key): rescan the current console. On the
// list screen update app.games immediately; on home the next draw_home picks
// up the invalidated cache.
static void refresh_games()
{
    invalidate_games(app.carousel);
    if (app.screen == Screen::List) {
        app.games = games_for(app.carousel);
        app.sel = std::clamp(app.sel, 0, std::max(0, (int)app.games.size() - 1));
    }
}

void do_launch()
{
    if (app.games.empty()) return;
    const Console& c = consoles()[app.carousel];
    const Game& g = app.games[app.sel];

    shutdown_gfx();                    // release DRM master
    int rc = launch_game(c, g);
    if (!init_gfx()) {                 // emulator exited — take panels back
        // Graphics re-init failed (DRM still held / mode broken). Exit non-zero
        // so the session supervisor backs off instead of treating it as a clean
        // exit and spinning in a tight restart loop.
        app.running = false;
        app.exit_code = 1;
        return;
    }
    if (rc == 127)
        show_msg("无法启动模拟器", "模拟器程序缺失或损坏，请重新刷写系统。");
    else if (rc != 0)
        fprintf(stderr, "gameos-ui: emulator exited with %d\n", rc);

    // Back from the emulator: refresh the cached list and keep selection in
    // range (a ROM may have been added/removed, saves/screenshots changed).
    invalidate_games(app.carousel);
    app.games = games_for(app.carousel);
    app.sel = std::clamp(app.sel, 0, std::max(0, (int)app.games.size() - 1));
}

void go_back()
{
    switch (app.screen) {
    case Screen::List:
        app.screen = Screen::Home;
        break;
    case Screen::Settings:
    case Screen::Msg:
        app.screen = Screen::Home;
        break;
    case Screen::Confirm:
        app.confirm = ConfirmAct::None;
        app.screen = Screen::Settings;
        break;
    default:
        break;
    }
}

void confirm_btn()
{
    switch (app.screen) {
    case Screen::Home:
        enter_selection();
        break;
    case Screen::List:
        do_launch();
        break;
    case Screen::Settings:
        if (app.set_sel == 2) { app.confirm = ConfirmAct::PowerOff; app.screen = Screen::Confirm; }
        else if (app.set_sel == 3) { app.confirm = ConfirmAct::Reboot; app.screen = Screen::Confirm; }
        else if (app.set_sel == 4)
            show_msg("关于 gameOS",
                     "gameOS 1.0\n"
                     "为 Anbernic RG DS Plus 全新打造的开源游戏系统。\n"
                     "NDS · GBA · GBC · PS1 · PSP\n"
                     "github.com/oh-sudo-user/gameOS");
        break;
    case Screen::Confirm:
        app.exit_code = (app.confirm == ConfirmAct::PowerOff) ? 90 : 91;
        app.running = false;
        break;
    default:
        break;
    }
}

void start_btn()
{
    if (app.screen == Screen::Home) { app.screen = Screen::Settings; app.set_sel = 0; }
    else if (app.screen != Screen::Confirm) app.screen = Screen::Home;
}

// horizontal / vertical navigation with key repeat
void navigate(int dx, int dy)
{
    switch (app.screen) {
    case Screen::Home:
        if (dx) app.carousel = (app.carousel + dx + 6) % 6;
        break;
    case Screen::List:
        if (dy && !app.games.empty())
            app.sel = std::clamp(app.sel + dy, 0, (int)app.games.size() - 1);
        break;
    case Screen::Settings:
        if (dy) app.set_sel = std::clamp(app.set_sel + dy, 0, 4);
        if (dx) {
            if (app.set_sel == 0) {
                app.settings.brightness = std::clamp(app.settings.brightness + dx * 5, 10, 100);
                apply_brightness(app.settings.brightness);
                save_settings(app.settings);
            } else if (app.set_sel == 1) {
                app.settings.volume = std::clamp(app.settings.volume + dx * 5, 0, 100);
                apply_volume(app.settings.volume);
                save_settings(app.settings);
            }
        }
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
    case SDL_CONTROLLER_BUTTON_X: refresh_games(); break;  // physical Y (top)
    case SDL_CONTROLLER_BUTTON_BACK:
        if (app.screen != Screen::Confirm) app.screen = Screen::Home;
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
    case SDLK_r: refresh_games(); break;
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
    if (app.pad) {
        if (SDL_GameControllerGetButton(app.pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  dx = -1;
        if (SDL_GameControllerGetButton(app.pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) dx = 1;
        if (SDL_GameControllerGetButton(app.pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    dy = -1;
        if (SDL_GameControllerGetButton(app.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  dy = 1;
        Sint16 ax = SDL_GameControllerGetAxis(app.pad, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ay = SDL_GameControllerGetAxis(app.pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -16000) dx = -1; else if (ax > 16000) dx = 1;
        if (ay < -16000) dy = -1; else if (ay > 16000) dy = 1;
    }
    int dir = dy ? dy * 2 : dx; // encode: vertical takes priority
    Uint32 now = SDL_GetTicks();
    if (dir != app.hold_dir) {
        app.hold_dir = dir;
        app.hold_since = now;
        app.last_repeat = now;
        return; // first press already handled by button event
    }
    if (dir == 0) return;
    Uint32 delay = (now - app.hold_since < 400) ? 400 : 130;
    if (now - app.last_repeat >= delay) {
        app.last_repeat = now;
        if (dir == -2) navigate(0, -1);
        else if (dir == 2) navigate(0, 1);
        else navigate(dir, 0);
    }
}

} // namespace

int main(int, char**)
{
    setenv("SDL_VIDEODRIVER", "kmsdrm", 1);

    app.settings = load_settings();
    app.carousel = app.settings.last_console;
    apply_brightness(app.settings.brightness);
    // Restore the user's saved volume on every startup. S20gameos now only
    // sets a first-boot default, so without this the mixer would stay at the
    // hardware default and ignore the saved setting.
    apply_volume(app.settings.volume);

    if (!init_gfx()) {
        shutdown_gfx();   // release any partial resources / DRM master
        return 1;
    }

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
            case SDL_CONTROLLERDEVICEREMOVED:
                if (app.pad) { SDL_GameControllerClose(app.pad); app.pad = nullptr; }
                open_controller();
                break;
            default:
                break;
            }
        }

        poll_repeat();

        SDL_Renderer* r = app.ren_top;
        switch (app.screen) {
        case Screen::Home:     draw_home(r); break;
        case Screen::List:     draw_list(r); break;
        case Screen::Settings: draw_settings(r); break;
        case Screen::Msg:      draw_msg(r); break;
        case Screen::Confirm:  draw_confirm(r); break;
        }
        SDL_RenderPresent(r);
        draw_ambient();
        SDL_Delay(1);
    }

    app.settings.last_console = std::clamp(app.carousel, 0, 4);
    save_settings(app.settings);
    shutdown_gfx();
    return app.exit_code;
}