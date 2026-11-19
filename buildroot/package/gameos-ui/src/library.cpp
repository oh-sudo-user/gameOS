// gameOS front-end — library scanning, settings, system state.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

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

// Collapse "/./" and "/../" segments so a descriptor's relative references
// compare equal to the paths produced by scan_dir.
static std::string norm_path(const std::string& p)
{
    std::vector<std::string> parts;
    std::istringstream ss(p);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); continue; }
        parts.push_back(seg);
    }
    std::string out;
    for (const auto& s : parts) out += "/" + s;
    return out;
}

static std::string lower_ext(const std::string& name)
{
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

// A .m3u playlist / .cue sheet names the disc or track files it launches.
// Those payloads are one game with the descriptor, not separate entries —
// hide them even when their stem differs (Multi.m3u -> "Multi (Disc 1).cue").
static void collect_hidden(const std::vector<Game>& games, std::set<std::string>& hidden)
{
    for (const auto& g : games) {
        std::string ext = lower_ext(g.path);
        if (ext != ".m3u" && ext != ".cue") continue;
        std::string dir = g.path.substr(0, g.path.find_last_of('/'));
        std::ifstream f(g.path);
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            size_t beg = line.find_first_not_of(" \t");
            if (beg == std::string::npos) continue;
            line = line.substr(beg);
            std::string ref;
            if (ext == ".m3u") {
                if (line[0] == '#') continue;
                ref = line;
            } else {
                if (strncasecmp(line.c_str(), "FILE ", 5) != 0) continue;
                auto q1 = line.find('"');
                auto q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
                if (q2 == std::string::npos) continue;
                ref = line.substr(q1 + 1, q2 - q1 - 1);
            }
            if (ref.empty()) continue;
            hidden.insert(norm_path(dir + "/" + ref));
        }
    }
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

// ROM roots in priority order. TF1 (the GAMEOS data partition) comes first,
// then the second microSD slot, which gameos-tf2 mounts read-only at /mnt/tf2.
// Both the "roms/<system>" layout and console folders straight at the card
// root are accepted, so a card prepared for another firmware works as-is.
// exFAT/FAT lookups are case-insensitive; the ROMS variant covers ext4 cards.
static std::vector<std::string> rom_roots(const Console& c)
{
    std::vector<std::string> roots;
    roots.push_back(std::string("/userdata/roms/") + c.id);
    for (const char* base : { "/mnt/tf2/roms/", "/mnt/tf2/ROMS/", "/mnt/tf2/" })
        roots.push_back(std::string(base) + c.id);
    return roots;
}

static int root_index(const std::string& path,
                      const std::vector<std::string>& roots)
{
    for (size_t i = 0; i < roots.size(); i++)
        if (path.compare(0, roots[i].size(), roots[i]) == 0) return (int)i;
    return (int)roots.size();
}

bool on_tf2(const std::string& path)
{
    return path.compare(0, 9, "/mnt/tf2/") == 0;
}

std::vector<Game> scan_games(const Console& c)
{
    std::vector<Game> out;
    const std::vector<std::string> roots = rom_roots(c);
    for (const auto& dir : roots)
        scan_dir(dir, c, 0, out);

    std::set<std::string> hidden;
    collect_hidden(out, hidden);
    if (!hidden.empty())
        out.erase(std::remove_if(out.begin(), out.end(),
                  [&](const Game& g) { return hidden.count(g.path) > 0; }),
                  out.end());

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

    // The same game on both cards is still one entry: keep the TF1 copy (the
    // roots are in priority order) so saves and favourites stay on the
    // internal data partition. Titles are compared after the stem pass, which
    // reordered everything by path.
    std::sort(dedup.begin(), dedup.end(), [&](const Game& a, const Game& b) {
        int ra = root_index(a.path, roots), rb = root_index(b.path, roots);
        if (ra != rb) return ra < rb;
        return a.title < b.title;
    });
    // Global title set, NOT an adjacent comparison: the sort above groups by
    // root first, so the TF1 and TF2 copies of one title are separated by every
    // other TF1 game and would never be neighbours.
    std::vector<Game> uniq;
    std::set<std::string> seen_titles;
    for (const auto& g : dedup) {
        if (!seen_titles.insert(g.title).second) continue;
        uniq.push_back(g);
    }

    std::sort(uniq.begin(), uniq.end(),
              [](const Game& a, const Game& b) { return a.title < b.title; });
    return uniq;
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
        else if (k == "theme") s.theme = std::clamp(n, 0, 3);
        else if (k == "bluetooth") s.bluetooth = std::clamp(n, 0, 1);
        else if (k == "language") s.language = std::clamp(n, 0, LANG_COUNT - 1);
    }
    return s;
}

void save_settings(const Settings& s)
{
    std::ostringstream ss;
    ss << "brightness=" << s.brightness << "\n"
       << "volume=" << s.volume << "\n"
       << "last_console=" << s.last_console << "\n"
       << "theme=" << s.theme << "\n"
       << "bluetooth=" << s.bluetooth << "\n"
       << "language=" << s.language << "\n";
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
    // rk817 headphone exposes "Master"; aw88166 speakers expose "PCM" or
    // "Left PCM"/"Right PCM" (attenuation, no dB scale). Card probe order can
    // swap 0/1, so fall back Master->PCM->Left/Right PCM per card.
    char cmd[640];
    snprintf(cmd, sizeof(cmd),
             "amixer -c 0 sset Master %d%% >/dev/null 2>&1 || amixer -c 0 sset PCM %d%% >/dev/null 2>&1 || "
             "{ amixer -c 0 sset 'Left PCM' %d%% >/dev/null 2>&1; amixer -c 0 sset 'Right PCM' %d%% >/dev/null 2>&1; }; "
             "amixer -c 1 sset Master %d%% >/dev/null 2>&1 || amixer -c 1 sset PCM %d%% >/dev/null 2>&1 || "
             "{ amixer -c 1 sset 'Left PCM' %d%% >/dev/null 2>&1; amixer -c 1 sset 'Right PCM' %d%% >/dev/null 2>&1; }",
             pct, pct, pct, pct, pct, pct, pct, pct);
    if (system(cmd) != 0) { /* ignore */ }
    // Bluetooth headphones have their own volume (bluealsa per-device control);
    // the hw mixers above do not touch the A2DP stream.
    if (access("/run/gameos/bt-audio", F_OK) == 0) {
        char bt[96];
        snprintf(bt, sizeof(bt), "gameos-bt volume %d >/dev/null 2>&1", pct);
        if (system(bt) != 0) { /* ignore */ }
    }
}

// ---------------------------------------------------------------------------
// Favorites / saves / covers
// ---------------------------------------------------------------------------
static const char* FAV_PATH = "/userdata/system/favorites.ini";

static std::set<std::string>& fav_set()
{
    static std::set<std::string> s;
    static bool loaded = false;
    if (!loaded) {
        std::ifstream f(FAV_PATH);
        std::string line;
        while (std::getline(f, line))
            if (!line.empty()) s.insert(line);
        loaded = true;
    }
    return s;
}

static void fav_flush()
{
    std::ostringstream ss;
    for (const auto& k : fav_set()) ss << k << "\n";
    write_file(FAV_PATH, ss.str());
}

bool fav_contains(const std::string& sys, const std::string& title)
{
    return fav_set().count(sys + "/" + title) > 0;
}

void fav_toggle(const std::string& sys, const std::string& title)
{
    auto& s = fav_set();
    std::string k = sys + "/" + title;
    if (s.count(k)) s.erase(k); else s.insert(k);
    fav_flush();
}

std::vector<SaveEntry> scan_saves()
{
    std::vector<SaveEntry> out;
    DIR* d = opendir("/userdata/saves");
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;
        std::string full = std::string("/userdata/saves/") + name;
        struct stat st;
        if (stat(full.c_str(), &st) < 0 || !S_ISREG(st.st_mode)) continue;
        SaveEntry se;
        se.path = full;
        se.name = name;
        se.size = (long)st.st_size;
        out.push_back(std::move(se));
    }
    closedir(d);
    std::sort(out.begin(), out.end(),
              [](const SaveEntry& a, const SaveEntry& b) { return a.name < b.name; });
    return out;
}

bool delete_save(const std::string& path)
{
    return std::remove(path.c_str()) == 0;
}

std::string cover_path(const std::string& sys, const std::string& title)
{
    for (const char* ext : { ".png", ".jpg" }) {
        std::string p = "/userdata/covers/" + sys + "/" + title + ext;
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return p;
    }
    return {};
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