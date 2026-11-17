// gameOS front-end — library scanning, settings, system state.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace gameos {

const std::vector<Console>& consoles()
{
    static const std::vector<Console> list = {
        { "nds", "任天堂 DS", "NDS",
          "melonds_libretro.so", "nds.cfg", true,
          { ".nds", ".dsi", ".ids", ".srl" } },
        { "gba", "GBA 掌机", "GBA",
          "mgba_libretro.so", "gba.cfg", false,
          { ".gba", ".agb", ".mb" } },
        { "gbc", "GB / GBC", "GBC",
          "mgba_libretro.so", "gbc.cfg", false,
          { ".gbc", ".gb", ".sgb" } },
        { "ps1", "索尼 PS1", "PS1",
          "pcsx_rearmed_libretro.so", "ps1.cfg", false,
          { ".bin", ".cue", ".iso", ".img", ".chd", ".pbp", ".m3u" } },
        { "psp", "索尼 PSP", "PSP",
          nullptr, nullptr, false,
          { ".iso", ".cso", ".pbp", ".elf" } },
    };
    return list;
}

std::string assets_dir()
{
    const char* e = getenv("GAMEOS_ASSETS");
    return e ? e : "/usr/share/gameos/assets";
}

std::string fonts_dir()
{
    const char* e = getenv("GAMEOS_FONTS");
    return e ? e : "/usr/share/gameos/fonts";
}

std::string read_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& data)
{
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << data;
    return f.good();
}

static bool has_ext(const std::string& name, const std::vector<std::string>& exts)
{
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    for (const auto& e : exts)
        if (ext == e) return true;
    return false;
}

std::vector<Game> scan_games(const Console& c)
{
    std::vector<Game> out;
    std::string dir = std::string("/userdata/roms/") + c.id;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;
        std::string full = dir + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) < 0 || !S_ISREG(st.st_mode)) continue;
        if (!has_ext(name, c.exts)) continue;
        Game g;
        g.path = full;
        auto dot = name.find_last_of('.');
        g.title = (dot == std::string::npos) ? name : name.substr(0, dot);
        out.push_back(std::move(g));
    }
    closedir(d);
    std::sort(out.begin(), out.end(),
              [](const Game& a, const Game& b) { return a.title < b.title; });
    return out;
}

bool nds_bios_ok()
{
    for (const char* f : { "bios7.bin", "bios9.bin", "firmware.bin" }) {
        std::string p = std::string("/userdata/bios/") + f;
        struct stat st;
        if (stat(p.c_str(), &st) < 0 || st.st_size == 0) return false;
    }
    return true;
}

std::string bios_missing_list()
{
    std::string out;
    for (const char* f : { "bios7.bin", "bios9.bin", "firmware.bin" }) {
        std::string p = std::string("/userdata/bios/") + f;
        struct stat st;
        if (stat(p.c_str(), &st) < 0 || st.st_size == 0) {
            if (!out.empty()) out += "、";
            out += f;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
static const char* SETTINGS_PATH = "/userdata/system/gameos.ini";

Settings load_settings()
{
    Settings s;
    std::ifstream f(SETTINGS_PATH);
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        int n = atoi(v.c_str());
        if (k == "brightness") s.brightness = std::clamp(n, 10, 100);
        else if (k == "volume") s.volume = std::clamp(n, 0, 100);
        else if (k == "last_console") s.last_console = std::clamp(n, 0, 4);
    }
    return s;
}

void save_settings(const Settings& s)
{
    std::ostringstream ss;
    ss << "brightness=" << s.brightness << "\n"
       << "volume=" << s.volume << "\n"
       << "last_console=" << s.last_console << "\n";
    write_file(SETTINGS_PATH, ss.str());
}

void apply_brightness(int pct)
{
    pct = std::clamp(pct, 5, 100);
    DIR* d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        std::string base = std::string("/sys/class/backlight/") + e->d_name;
        std::string maxs = read_file(base + "/max_brightness");
        int max = maxs.empty() ? 255 : atoi(maxs.c_str());
        if (max <= 0) max = 255;
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "echo %d > '%s/brightness' 2>/dev/null",
                 max * pct / 100, base.c_str());
        if (system(cmd) != 0) { /* ignore */ }
    }
    closedir(d);
}

void apply_volume(int pct)
{
    pct = std::clamp(pct, 0, 100);
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "amixer -c 0 sset Master %d%% >/dev/null 2>&1; "
             "amixer -c 1 sset Master %d%% >/dev/null 2>&1", pct, pct);
    if (system(cmd) != 0) { /* ignore */ }
}

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
static std::string battery_node()
{
    DIR* d = opendir("/sys/class/power_supply");
    if (!d) return {};
    struct dirent* e;
    std::string found;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        std::string base = std::string("/sys/class/power_supply/") + e->d_name;
        std::string type = read_file(base + "/type");
        if (type.find("Battery") != std::string::npos) {
            found = base;
            break;
        }
    }
    closedir(d);
    return found;
}

int battery_percent()
{
    static std::string node = battery_node();
    if (node.empty()) return -1;
    std::string cap = read_file(node + "/capacity");
    if (cap.empty()) return -1;
    return std::clamp(atoi(cap.c_str()), 0, 100);
}

bool battery_charging()
{
    static std::string node = battery_node();
    if (node.empty()) return false;
    std::string st = read_file(node + "/status");
    return st.find("Charging") != std::string::npos;
}

} // namespace gameos