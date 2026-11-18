// gameOS front-end — shared declarations.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#include <string>
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

std::vector<Game> scan_games(const Console& c);

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
};

Settings load_settings();
void save_settings(const Settings& s);
void apply_brightness(int pct);
void apply_volume(int pct);

// Favorites (persisted across sessions), keyed "<system>/<title>".
bool fav_contains(const std::string& sys, const std::string& title);
void fav_toggle(const std::string& sys, const std::string& title);

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