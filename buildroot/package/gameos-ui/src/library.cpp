// gameOS front-end — library scanning, settings, system state.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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

// Path without its extension — identifies one physical image regardless of
// how many files describe it (Game.bin / Game.cue share a stem).
static std::string stem_of(const std::string& p)
{
    auto dot = p.find_last_of('.');
    auto slash = p.find_last_of('/');
    // only strip the dot if it is in the basename (after the last slash)
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return p;
    return p.substr(0, dot);
}

// Preference when several files describe the same image: the launchable
// descriptor (.m3u playlist / .cue sheet / compressed .chd) wins over the raw
// .bin track, so a PS1 game is listed once and launches via its .cue/.m3u.
static int ext_rank(const std::string& name)
{
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return 0;
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".m3u") return 100;
    if (ext == ".cue") return 90;
    if (ext == ".chd") return 80;
    if (ext == ".pbp") return 70;
    if (ext == ".iso") return 60;
    if (ext == ".cso") return 55;
    if (ext == ".img") return 50;
    if (ext == ".bin") return 10;
    return 20; // single-file roms (.nds/.gba/.gbc/...)
}

// Recursive walk with a depth cap. lstat (not stat) so symlinked directories
// are never followed — avoids loops; the exFAT rom partition has no symlinks
// anyway. Subfolders let users organise roms by genre/series.
static void scan_dir(const std::string& dir, const Console& c,
                     int depth, std::vector<Game>& out)
{
    if (depth > 4) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;
        std::string full = dir + "/" + name;
        struct stat st;
        if (lstat(full.c_str(), &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir(full, c, depth + 1, out);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        if (!has_ext(name, c.exts)) continue;
        Game g;
        g.path = full;
        auto dot = name.find_last_of('.');
        g.title = (dot == std::string::npos) ? name : name.substr(0, dot);
        out.push_back(std::move(g));
    }
    closedir(d);
}

std::vector<Game> scan_games(const Console& c)
{
    std::vector<Game> out;
    std::string dir = std::string("/userdata/roms/") + c.id;
    scan_dir(dir, c, 0, out);

    // Group by stem, best extension first, then keep the first of each stem.
    // Sorting by stem (full path) rather than title makes same-image files
    // adjacent even when different folders share a base name, so the dedup
    // never drops a distinct game or keeps a duplicate .bin.
    std::sort(out.begin(), out.end(), [](const Game& a, const Game& b) {
        std::string sa = stem_of(a.path), sb = stem_of(b.path);
        if (sa != sb) return sa < sb;
        return ext_rank(a.path) > ext_rank(b.path);
    });
    std::vector<Game> dedup;
    std::string last_stem;
    for (const auto& g : out) {
        std::string s = stem_of(g.path);
        if (!dedup.empty() && s == last_stem) continue;
        dedup.push_back(g);
        last_stem = s;
    }

    std::sort(dedup.begin(), dedup.end(),
              [](const Game& a, const Game& b) { return a.title < b.title; });
    return dedup;
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

// Battery readings are cached and refreshed at most once every few seconds.
// draw_topbar() calls these every frame; re-reading sysfs 60x/sec is wasteful.
// The node path is re-resolved until found, because the power_supply driver
// may probe after the UI has already started (a one-shot static would then
// cache "no battery" forever).
namespace {
constexpr int BATTERY_REFRESH_SEC = 5;
std::string g_bat_node;
int    g_bat_percent  = -1;
bool   g_bat_charging = false;
time_t g_bat_last     = 0;
bool   g_bat_valid    = false;

void battery_refresh()
{
    time_t now = time(nullptr);
    if (g_bat_valid && (now - g_bat_last) < BATTERY_REFRESH_SEC)
        return;
    if (g_bat_node.empty())
        g_bat_node = battery_node();   // retry until the driver appears
    g_bat_last  = now;
    g_bat_valid = true;
    if (g_bat_node.empty()) {
        g_bat_percent  = -1;
        g_bat_charging = false;
        return;
    }
    std::string cap = read_file(g_bat_node + "/capacity");
    g_bat_percent = cap.empty() ? -1 : std::clamp(atoi(cap.c_str()), 0, 100);
    std::string st = read_file(g_bat_node + "/status");
    g_bat_charging = st.find("Charging") != std::string::npos;
}
} // namespace

int battery_percent()
{
    battery_refresh();
    return g_bat_percent;
}

bool battery_charging()
{
    battery_refresh();
    return g_bat_charging;
}

} // namespace gameos