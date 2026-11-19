// gameOS front-end — shared declarations.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include <string>
#include <utility>
#include <vector>

namespace gameos {

// ---------------------------------------------------------------------------
// Supported systems
// ---------------------------------------------------------------------------
struct Console {
    const char* id;         // directory name under /userdata/roms
    const char* name_cn;    // Chinese display name
    const char* name_en;    // short badge text
    const char* core;       // libretro core (nullptr = standalone PPSSPP)
    const char* appendcfg;  // per-system RetroArch config
    bool dual;              // span both panels (NDS only)
    std::vector<std::string> exts;
};

const std::vector<Console>& consoles();

struct Game {
    std::string path;
    std::string title;
};

// Scans /userdata/roms/<id> (TF1) and the second microSD slot mounted at
// /mnt/tf2 (roms/<id> or <id> at the card root). Same title on both cards is
// listed once, preferring the TF1 copy.
std::vector<Game> scan_games(const Console& c);

// True when the game file lives on the second card (/mnt/tf2). Whether a card
// is mounted there is tracked by the UI from /run/gameos/tf2 (see tf2_poll).
bool on_tf2(const std::string& path);

// NDS needs bios7.bin / bios9.bin / firmware.bin in /userdata/bios.
bool nds_bios_ok();
std::string bios_missing_list();

// ---------------------------------------------------------------------------
// Settings & system state
// ---------------------------------------------------------------------------
struct Settings {
    int brightness = 70;
    int volume = 70;
    int last_console = 0;
    int theme = 0;
    int bluetooth = 0;   // 1 = stack was on; restored at session start
    int language = 0;    // enum Lang (i18n.hpp); 0 = 简体中文
};

Settings load_settings();
void save_settings(const Settings& s);
void apply_brightness(int pct);
void apply_volume(int pct);

// Favorites (persisted across sessions), keyed "<system>/<title>".
bool fav_contains(const std::string& sys, const std::string& title);
void fav_toggle(const std::string& sys, const std::string& title);
// Every favorite key, split into system id and title, in stored order.
std::vector<std::pair<std::string, std::string>> fav_list();

// ---------------------------------------------------------------------------
// Play-time statistics (/userdata/system/activity.log)
// ---------------------------------------------------------------------------
struct PlaySession {
    long long start = 0;      // wall-clock epoch seconds when the game started
    long long seconds = 0;    // measured play time, excluding system suspend
    std::string sys, title;
};

struct GameStat {
    std::string sys, title;
    long long seconds = 0;
    int sessions = 0;
    long long last = 0;       // wall-clock epoch of the most recent session
};

// Why a game was suggested. The UI turns this into a localised sentence.
enum PickReason {
    PICK_FAV_NEW = 0,   // favorited, never played
    PICK_FAV_BACK,      // favorited, not played for a week
    PICK_NEW,           // never played
    PICK_BACK,          // not played for a fortnight
    PICK_REGULAR        // played recently and often
};

struct Pick {
    std::string sys, title;
    int reason = PICK_NEW;
    long long seconds = 0;
    int idle_days = 0;
    long long score = 0;
};

struct LibEntry {
    std::string sys, title;
};

// Append one finished session. Sessions under 5 s are dropped: a failed exec
// exits immediately and would otherwise fill the log with non-play.
void activity_record(const std::string& sys, const std::string& title,
                     long long wall_start, long long seconds);

std::vector<PlaySession> activity_sessions();   // newest first
std::vector<GameStat> activity_games();         // most played first
long long activity_total();                     // sum of all play seconds
// Totals for the last `days` local days, oldest first (last element = today).
std::vector<long long> activity_daily(int days);
// Deterministic "what to play next" ranking over the whole scanned library.
std::vector<Pick> activity_recommend(const std::vector<LibEntry>& lib, int limit);

// Save-state / save-file management under /userdata/saves.
struct SaveEntry {
    std::string path;
    std::string name;
    long size = 0;
};
std::vector<SaveEntry> scan_saves();
bool delete_save(const std::string& path);

// Box-art path for a game, or "" if the user supplied no cover.
std::string cover_path(const std::string& sys, const std::string& title);

int battery_percent();      // -1 = unknown
bool battery_charging();

// ---------------------------------------------------------------------------
// Emulator launch (blocks until the emulator exits; caller must release
// SDL/DRM resources first). Returns the child exit status.
// ---------------------------------------------------------------------------
int launch_game(const Console& c, const Game& g);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
std::string assets_dir();
std::string fonts_dir();
std::string read_file(const std::string& path);
bool write_file(const std::string& path, const std::string& data);

} // namespace gameos