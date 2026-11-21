// gameOS front-end — emulator launch.
//
// The caller MUST tear down SDL (releasing DRM master) before calling
// launch_game(), and re-initialise afterwards. The child inherits the
// DRM device and becomes master itself.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"
#include "i18n.hpp"
#include "logutil.hpp"
#include "overrides.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace gameos {

// PPSSPP keeps its menu language in [General] Language of ppsspp.ini
// (locale string, e.g. "zh_CN"). Patch the key in place so the emulator
// menu follows the system language; create a minimal ini on first run.
static void apply_ppsspp_language()
{
    const std::string dir = "/userdata/system/ppsspp/.config/ppsspp/PSP/SYSTEM";
    const std::string path = dir + "/ppsspp.ini";
    const std::string val = std::string("Language = ") + ppsspp_locale();
    if (system(("mkdir -p " + dir).c_str()) != 0) return;

    std::istringstream ss(read_file(path));
    std::ostringstream out;
    std::string line;
    bool in_general = false, have_general = false, done = false;
    while (std::getline(ss, line)) {
        std::string t = line;
        size_t b = t.find_first_not_of(" \t\r");
        std::string trimmed = (b == std::string::npos) ? "" : t.substr(b);
        if (!trimmed.empty() && trimmed[0] == '[') {
            if (in_general && !done) { out << val << "\n"; done = true; }
            in_general = (trimmed.compare(0, 9, "[General]") == 0);
            if (in_general) have_general = true;
        } else if (in_general && !done &&
                   trimmed.compare(0, 8, "Language") == 0 &&
                   trimmed.size() > 8 &&
                   (trimmed[8] == ' ' || trimmed[8] == '\t' || trimmed[8] == '=')) {
            out << val << "\n";
            done = true;
            continue;
        }
        out << line << "\n";
    }
    if (in_general && !done) { out << val << "\n"; done = true; }
    if (!have_general) out << "[General]\n" << val << "\n";
    write_file(path, out.str());
}

// RG DS panels are 640x480, Plus panels 1024x768; the span framebuffer and
// the bottom-panel touch offset follow the physical panel height.
static int panel_height()
{
    std::string c = read_file("/proc/device-tree/compatible");
    return c.find("rg-ds-plus") != std::string::npos ? 768 : 480;
}

// ---------------------------------------------------------------------------
// Launch read-ahead
//
// The first launch after power-on otherwise pays for every byte the dynamic
// loader and the emulator touch: RetroArch itself, its GL/Mesa stack and the
// core. MELONDS maps a ROM lazily, but its boot code still has to come off
// the card. Ask the kernel for these files while the card-insert animation is
// on screen; the animation becomes the I/O window instead of dead time.
//
// posix_fadvise(WILLNEED) is a hint: it returns immediately and the kernel
// reads in the background. Files that do not exist (no state yet, no box art)
// are silently skipped. ROMs are capped: a 512 MiB NDS cartridge is mapped on
// demand by the core, and reading all of it would evict useful cache.
// ---------------------------------------------------------------------------
namespace {

constexpr long long kMB = 1024 * 1024;

void warm_path(const std::string& path, long long max_bytes)
{
    if (path.empty()) return;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    struct stat st = {};
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
        off_t len = st.st_size;
        if (max_bytes > 0 && len > max_bytes) len = (off_t)max_bytes;
#ifdef POSIX_FADV_WILLNEED
        (void)posix_fadvise(fd, 0, len, POSIX_FADV_WILLNEED);
#endif
    }
    close(fd);
}

void warm_dir(const std::string& dir, long long max_file_bytes)
{
    DIR* dp = opendir(dir.c_str());
    if (!dp) return;
    struct dirent* e;
    while ((e = readdir(dp))) {
        if (e->d_name[0] == '.') continue;
        warm_path(dir + "/" + e->d_name, max_file_bytes);
    }
    closedir(dp);
}

std::string lower_str(std::string s)
{
    for (char& ch : s)
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    return s;
}

std::string ext_of(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";
    return lower_str(path.substr(dot));
}

std::string dir_of(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

std::string join_path(const std::string& dir, const std::string& name)
{
    if (name.empty() || name[0] == '/') return name;
    if (dir.empty() || dir == ".") return name;
    return dir + "/" + name;
}

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// First FILE entry of a .cue sheet (quoted or bare), e.g.
//   FILE "Game (Track 1).bin" BINARY
std::string cue_first_file(const std::string& cue)
{
    std::string body = read_file(cue);
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        if (nl == std::string::npos) nl = body.size();
        std::string line = trim(body.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.size() < 5 || lower_str(line.substr(0, 4)) != "file")
            continue;
        size_t p = 4;
        if (p >= line.size() || (line[p] != ' ' && line[p] != '\t')) continue;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;
        if (p >= line.size()) break;
        if (line[p] == '"') {
            size_t q = line.find('"', p + 1);
            if (q != std::string::npos) return line.substr(p + 1, q - p - 1);
        } else {
            size_t q = line.find_first_of(" \t", p);
            return line.substr(p, q == std::string::npos ? std::string::npos : q - p);
        }
        break;
    }
    return "";
}

// First non-comment line of an .m3u playlist.
std::string m3u_first_entry(const std::string& m3u)
{
    std::string body = read_file(m3u);
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        if (nl == std::string::npos) nl = body.size();
        std::string line = trim(body.substr(pos, nl - pos));
        pos = nl + 1;
        if (!line.empty() && line[0] != '#') return line;
    }
    return "";
}

void warm_rom(const Console& c, const Game& g)
{
    const std::string& path = g.path;
    const std::string ext = ext_of(path);
    long long cap = 0;                       // 0 = whole file
    if (strcmp(c.id, "nds") == 0)      cap = 8 * kMB;
    else if (strcmp(c.id, "ps1") == 0) cap = 16 * kMB;
    else if (strcmp(c.id, "psp") == 0) cap = 16 * kMB;

    if (ext == ".cue") {
        warm_path(path, 0);                  // the tiny sheet itself
        std::string bin = cue_first_file(path);
        if (!bin.empty())
            warm_path(join_path(dir_of(path), bin), 16 * kMB);
        return;
    }
    if (ext == ".m3u") {
        warm_path(path, 0);
        std::string first = m3u_first_entry(path);
        std::string target = join_path(dir_of(path), first);
        if (ext_of(target) == ".cue") {
            warm_path(target, 0);
            std::string bin = cue_first_file(target);
            if (!bin.empty())
                warm_path(join_path(dir_of(target), bin), 16 * kMB);
        } else {
            warm_path(target, 8 * kMB);
        }
        return;
    }
    warm_path(path, cap);
}

void warm_retroarch(const Console& c)
{
    warm_path("/usr/bin/retroarch", 0);
    if (c.core)
        warm_path(std::string("/usr/lib/libretro/") + c.core, 0);
    warm_path("/etc/retroarch.cfg", 0);
    if (c.appendcfg && *c.appendcfg)
        warm_path(std::string("/usr/share/gameos/retroarch/") + c.appendcfg, 0);
    warm_path("/tmp/gameos-ra-lang.cfg", 0);
    warm_path("/userdata/system/retroarch/cheevos.cfg", 0);

    // The KMS/GL stack RetroArch loads before it can show a frame. libgallium
    // carries Panfrost; libLLVM is a NEEDED dependency of it but only its
    // relocation/rodata head is touched at load (the code pages are for
    // llvmpipe and are left cold on purpose).
    static const char* libs[] = {
        "/usr/lib/libEGL.so.1", "/usr/lib/libGLESv2.so.2",
        "/usr/lib/libgbm.so.1", "/usr/lib/libdrm.so.2",
        "/usr/lib/libexpat.so.1", "/usr/lib/libz.so.1",
        "/usr/lib/libasound.so.2", "/usr/lib/libfreetype.so.6",
        "/usr/lib/libudev.so.1", "/usr/lib/libstdc++.so.6",
        "/lib/libgcc_s.so.1", "/lib/libm.so.6", "/lib/libc.so.6",
        "/lib/ld-linux-aarch64.so.1",
    };
    for (const char* p : libs) warm_path(p, 0);

    glob_t gl = {};
    if (glob("/usr/lib/libgallium-*.so", GLOB_NOSORT, nullptr, &gl) == 0) {
        for (size_t i = 0; i < gl.gl_pathc; i++) warm_path(gl.gl_pathv[i], 0);
        globfree(&gl);
    }
    glob_t llvm = {};
    if (glob("/usr/lib/libLLVM.so.*", GLOB_NOSORT, nullptr, &llvm) == 0) {
        for (size_t i = 0; i < llvm.gl_pathc; i++)
            warm_path(llvm.gl_pathv[i], 8 * kMB);
        globfree(&llvm);
    }

    // BIOS files are small and read during core load.
    warm_dir("/userdata/bios", 4 * kMB);
}

// Auto-resume state (RetroArch names it after the ROM without extension).
// Ordered after the ROM: the core needs its boot code first, the state only
// once the game has been initialised.
void warm_save_state(const Game& g)
{
    std::string base = g.path.substr(g.path.find_last_of('/') + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.resize(dot);
    warm_path("/userdata/saves/" + base + ".state.auto", 0);
    warm_path("/userdata/saves/" + base + ".state", 0);
    warm_path("/userdata/saves/" + base + ".srm", 0);
}

void warm_ppsspp()
{
    warm_path("/usr/bin/PPSSPPSDL", 0);
    static const char* libs[] = {
        "/usr/lib/libSDL2-2.0.so.0", "/usr/lib/libSDL2_ttf-2.0.so.0",
        "/usr/lib/libcpu_features.so", "/usr/lib/libimgui.so",
        "/usr/lib/libglslang.so.12", "/usr/lib/libSPIRV.so.12",
    };
    for (const char* p : libs) warm_path(p, 0);
    warm_path("/userdata/system/ppsspp/.config/ppsspp/PSP/SYSTEM/ppsspp.ini", 0);

    // Assets PPSSPP touches before the first frame: the UI atlas, controller
    // database, per-language strings and the VFPU lookup tables (loaded on
    // first use, so a 3D game hits them during boot). Flash0 fonts are left
    // cold: games load them later, on demand.
    static const char* assets[] = {
        "/usr/share/ppsspp/assets/font_atlas.zim",
        "/usr/share/ppsspp/assets/ppge_atlas.zim",
        "/usr/share/ppsspp/assets/gamecontrollerdb.txt",
        "/usr/share/ppsspp/assets/langregion.ini",
    };
    for (const char* p : assets) warm_path(p, 0);
    warm_path(std::string("/usr/share/ppsspp/assets/lang/") +
              ppsspp_locale() + ".ini", 0);
    warm_dir("/usr/share/ppsspp/assets/vfpu", 1 * kMB);
}

} // namespace

void warm_launch_files(const Console& c, const Game& g)
{
    if (c.core)
        warm_retroarch(c);
    else
        warm_ppsspp();
    warm_rom(c, g);
    if (c.core)
        warm_save_state(g);
}

// ---------------------------------------------------------------------------
// Persistent launch diagnostics
//
// The UI comes back to life after a black-screen emulator exit, so the card
// is the only reliable witness. Everything here is appended under
// /userdata/system/logs and survives reboots; the data partition is exFAT,
// so the logs can be read directly on a PC. Sampling never blocks the child.
// ---------------------------------------------------------------------------
namespace {

bool userdata_mounted()
{
    struct stat a = {}, b = {};
    if (stat("/userdata", &a) != 0 || stat("/", &b) != 0) return false;
    return a.st_dev != b.st_dev;
}

std::string emu_log() { return gameos::log_file("emu.log"); }
std::string ra_log()  { return gameos::log_file("retroarch.log"); }

std::string clean_line(std::string s)
{
    for (char& ch : s)
        if (ch == '\n' || ch == '\r' || ch == '\0') ch = ' ';
    size_t a = s.find_first_not_of(" \t");
    size_t b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string first_line(const std::string& path)
{
    return clean_line(read_file(path));
}

double uptime_seconds()
{
    std::string s = read_file("/proc/uptime");
    return s.empty() ? 0.0 : atof(s.c_str());
}

std::string wall_clock()
{
    time_t t = time(nullptr);
    struct tm tmv = {};
    localtime_r(&t, &tmv);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

std::string session_id()
{
    time_t t = time(nullptr);
    struct tm tmv = {};
    localtime_r(&t, &tmv);
    char date[32];
    strftime(date, sizeof(date), "%Y%m%d-%H%M%S", &tmv);
    char out[96];
    snprintf(out, sizeof(out), "%s-%d", date, (int)getpid());
    return out;
}

struct Diag {
    FILE* f = nullptr;

    Diag()
    {
        const std::string path = gameos::log_file("emu-session.log");
        gameos::log_rotate(path, 4 << 20);
        f = fopen(path.c_str(), "a");
        if (f) {
            setvbuf(f, nullptr, _IOLBF, 0);
            fcntl(fileno(f), F_SETFD, FD_CLOEXEC);
        }
    }
    ~Diag()
    {
        if (!f) return;
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }

    void line(const char* fmt, ...)
    {
        if (!f) return;
        char msg[4096];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        fprintf(f, "[%s +%.1fs] %s\n", wall_clock().c_str(), uptime_seconds(), msg);
        fflush(f);
    }

    void text(const char* label, const std::string& body, size_t max = 8192)
    {
        line("%s (%zu bytes):", label, body.size());
        size_t off = 0;
        while (off < body.size() && off < max) {
            size_t nl = body.find('\n', off);
            if (nl == std::string::npos) nl = body.size();
            line("  | %s", body.substr(off, nl - off).c_str());
            off = nl + 1;
        }
        if (off < body.size()) line("  | ... truncated ...");
    }

    void stat_info(const char* label, const std::string& path)
    {
        struct stat st = {};
        if (stat(path.c_str(), &st) != 0) {
            line("%s: %s MISSING (errno=%d)", label, path.c_str(), errno);
            return;
        }
        line("%s: %s size=%lld mtime=%lld", label, path.c_str(),
             (long long)st.st_size, (long long)st.st_mtime);
    }

    void file_text(const char* label, const std::string& path, size_t max = 8192)
    {
        struct stat st = {};
        if (stat(path.c_str(), &st) != 0) {
            line("%s: %s missing (errno=%d)", label, path.c_str(), errno);
            return;
        }
        std::string head = std::string(label) + " " + path;
        text(head.c_str(), read_file(path), max);
    }

    void tail(const char* label, const std::string& path, int lines = 100)
    {
        FILE* in = fopen(path.c_str(), "rb");
        if (!in) {
            line("%s: %s missing", label, path.c_str());
            return;
        }
        long sz = -1;
        if (fseek(in, 0, SEEK_END) == 0) sz = ftell(in);
        if (sz < 0) {
            fclose(in);
            line("%s: cannot size %s", label, path.c_str());
            return;
        }
        long start = sz > (96 << 10) ? sz - (96 << 10) : 0;
        fseek(in, start, SEEK_SET);
        std::string buf;
        char tmp[4096];
        size_t n;
        while ((n = fread(tmp, 1, sizeof(tmp), in)) > 0) buf.append(tmp, n);
        fclose(in);

        std::vector<std::string> keep;
        size_t off = 0;
        while (off <= buf.size()) {
            size_t nl = buf.find('\n', off);
            if (nl == std::string::npos) nl = buf.size();
            if (nl > off) keep.push_back(buf.substr(off, nl - off));
            if (nl == buf.size()) break;
            off = nl + 1;
        }
        size_t first = keep.size() > (size_t)lines ? keep.size() - lines : 0;
        line("--- %s: %s (last %zu lines) ---", label, path.c_str(), keep.size() - first);
        for (size_t i = first; i < keep.size(); i++) line("  | %s", keep[i].c_str());
    }
};

void dump_mem(Diag& d, const char* tag)
{
    std::istringstream ss(read_file("/proc/meminfo"));
    std::string l;
    int n = 0;
    while (std::getline(ss, l) && n < 4) {
        d.line("%s meminfo: %s", tag, l.c_str());
        n++;
    }
    std::string psi = read_file("/proc/pressure/memory");
    if (!psi.empty()) d.line("%s psi: %s", tag, clean_line(psi).c_str());
}

void dump_drm(Diag& d, const char* tag)
{
    glob_t g = {};
    if (glob("/sys/class/drm/card*-*", GLOB_NOSORT, nullptr, &g) != 0) {
        d.line("%s drm: no connectors", tag);
        return;
    }
    for (size_t i = 0; i < g.gl_pathc; i++) {
        std::string p = g.gl_pathv[i];
        std::string name = p.substr(p.find_last_of('/') + 1);
        d.line("%s drm %s status=%s enabled=%s modes=%s", tag, name.c_str(),
               first_line(p + "/status").c_str(),
               first_line(p + "/enabled").c_str(),
               first_line(p + "/modes").c_str());
    }
    globfree(&g);
}

void dump_backlight(Diag& d, const char* tag)
{
    glob_t g = {};
    if (glob("/sys/class/backlight/*", GLOB_NOSORT, nullptr, &g) != 0) {
        d.line("%s backlight: none", tag);
        return;
    }
    for (size_t i = 0; i < g.gl_pathc; i++) {
        std::string p = g.gl_pathv[i];
        d.line("%s backlight %s brightness=%s actual=%s max=%s", tag,
               p.substr(p.find_last_of('/') + 1).c_str(),
               first_line(p + "/brightness").c_str(),
               first_line(p + "/actual_brightness").c_str(),
               first_line(p + "/max_brightness").c_str());
    }
    globfree(&g);
}

void dump_dmesg(Diag& d, const char* tag)
{
    FILE* p = popen("dmesg 2>/dev/null | tail -n 100", "r");
    if (!p) {
        d.line("%s dmesg: unavailable", tag);
        return;
    }
    char buf[2048];
    int n = 0;
    while (fgets(buf, sizeof(buf), p)) {
        d.line("%s dmesg: %s", tag, clean_line(buf).c_str());
        n++;
    }
    pclose(p);
    if (n == 0) d.line("%s dmesg: empty", tag);
}

void dump_fds(Diag& d, pid_t pid, const char* tag)
{
    std::string dir = "/proc/" + std::to_string((long)pid) + "/fd";
    DIR* dp = opendir(dir.c_str());
    if (!dp) {
        d.line("%s fds: unavailable", tag);
        return;
    }
    std::vector<std::string> targets;
    struct dirent* e;
    while ((e = readdir(dp))) {
        if (e->d_name[0] == '.') continue;
        char buf[512];
        ssize_t n = readlink((dir + "/" + e->d_name).c_str(), buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        std::string t = buf;
        if (t.find("/dev/dri") != std::string::npos ||
            t.find("/dev/fb") != std::string::npos ||
            t.find("/dev/input") != std::string::npos ||
            t.find("/dev/snd") != std::string::npos)
            targets.push_back(t);
    }
    closedir(dp);
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    std::string joined;
    for (const auto& t : targets) {
        if (!joined.empty()) joined += ", ";
        joined += t;
    }
    d.line("%s fds: %s", tag, joined.empty() ? "(none)" : joined.c_str());
}

struct ProcInfo {
    char state = '?';
    int threads = 0;
    long rss_kb = 0;
    unsigned long long ticks = 0;
    std::string wchan;
};

bool read_proc(pid_t pid, ProcInfo& out)
{
    std::string stat = read_file("/proc/" + std::to_string((long)pid) + "/stat");
    size_t rp = stat.rfind(')');
    if (rp == std::string::npos) return false;
    std::istringstream is(stat.substr(rp + 2));
    std::vector<std::string> tok;
    std::string t;
    while (is >> t) tok.push_back(t);
    if (tok.size() < 22 || tok[0].empty()) return false;
    out.state = tok[0][0];
    out.ticks = strtoull(tok[11].c_str(), nullptr, 10) +
                strtoull(tok[12].c_str(), nullptr, 10);
    out.threads = atoi(tok[17].c_str());
    out.rss_kb = (long)(strtoll(tok[21].c_str(), nullptr, 10) *
                        (sysconf(_SC_PAGESIZE) / 1024));
    out.wchan = clean_line(read_file("/proc/" + std::to_string((long)pid) + "/wchan"));
    return true;
}

void append_marker(const char* fmt, ...)
{
    FILE* f = fopen(emu_log().c_str(), "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

std::string join_args(const std::vector<std::string>& args)
{
    std::string s;
    for (size_t i = 0; i < args.size(); i++) {
        if (i) s += ' ';
        s += args[i];
    }
    return s;
}

void dump_session_header(Diag& d, const Console& c, const Game& g,
                         const std::string& safe_cfg)
{
    d.line("================= LAUNCH SESSION =================");
    d.line("gameos_version=%s diag_build=%s kernel=%s",
           first_line("/etc/gameos-version").c_str(),
           first_line("/etc/gameos-diag-build").c_str(),
           first_line("/proc/sys/kernel/osrelease").c_str());
    d.line("model=%s compatible=%s boot_id=%s",
           first_line("/proc/device-tree/model").c_str(),
           first_line("/proc/device-tree/compatible").c_str(),
           first_line("/proc/sys/kernel/random/boot_id").c_str());
    d.line("console=%s title=%s dual=%d", c.id, g.title.c_str(), (int)c.dual);
    d.line("rom=%s", g.path.c_str());
    d.stat_info("rom", g.path);
    if (c.core) {
        d.stat_info("core", std::string("/usr/lib/libretro/") + c.core);
    }
    d.stat_info("retroarch", "/usr/bin/retroarch");
    d.stat_info("cheevos", "/userdata/system/retroarch/cheevos.cfg");
    d.line("child env: SDL_VIDEODRIVER=kmsdrm XDG_RUNTIME_DIR=/tmp/gameos-runtime "
           "GAMEOS_DUALSCREEN=%s GAMEOS_TOUCH_Y_OFFSET=%d LIBGL_DEBUG=%s",
           c.dual ? "1" : "(unset)", c.dual ? panel_height() : 0,
           c.dual ? "verbose" : "(unset)");
    d.text("nds-safe.cfg", safe_cfg.empty() ? "(empty)" : safe_cfg);
    if (c.core && c.appendcfg && *c.appendcfg)
        d.file_text("per-console cfg",
                    std::string("/usr/share/gameos/retroarch/") + c.appendcfg);
    d.file_text("lang cfg", "/tmp/gameos-ra-lang.cfg");
    d.file_text("retroarch.cfg", "/etc/retroarch.cfg", 16384);
    dump_mem(d, "boot");
    dump_drm(d, "boot");
    dump_backlight(d, "boot");
    dump_dmesg(d, "boot");
}

} // namespace

// Performance profile by console identity (never guessed from the ROM
// extension): NDS/PS1/PSP are heavy; GBA/GBC/NES/SNES/MD are two-chip
// systems that fit in the balanced tier. gameos-perf maps these strategy
// names to governor preferences in /etc/gameos/perf.conf.
static const char* perf_profile(const Console& c)
{
    if (c.dual) return "performance";           // NDS spans both panels
    if (strcmp(c.id, "ps1") == 0) return "performance";
    if (strcmp(c.id, "psp") == 0) return "performance";
    return "balanced";
}

// Per-game override: 0 powersave, 1 balanced, 2 performance.
static const char* perf_name(int p)
{
    switch (p) {
    case 0: return "powersave";
    case 1: return "balanced";
    default: return "performance";
    }
}

// Subsystem identity: our own launch/tuning layer around each engine.
static const char* subsystem_name(const Console& c)
{
    if (c.dual) return "game nds";
    if (strcmp(c.id, "gba") == 0) return "game gba";
    if (strcmp(c.id, "gbc") == 0) return "game gbc";
    if (strcmp(c.id, "ps1") == 0) return "game ps1";
    if (strcmp(c.id, "psp") == 0) return "game psp";
    if (strcmp(c.id, "nes") == 0) return "game nes";
    if (strcmp(c.id, "snes") == 0) return "game snes";
    if (strcmp(c.id, "md") == 0) return "game md";
    return "gameos";
}

// Best-effort governor switch. A failure here is logged by gameos-perf and
// must never block game launch, so the return value is deliberately ignored.
// The profile argument is a fixed literal, not user input.
static void apply_perf(const char* profile)
{
    std::string cmd = std::string("/usr/bin/gameos-perf ") + profile;
    (void)system(cmd.c_str());
}

static int run_emu(const Console& c, const Game& g,
                   const std::string& override_cfg, const std::string& perf,
                   long& seconds, Diag& d, int attempt, const std::string& sid)
{
    d.line("----- attempt %d begin -----", attempt);
    if (attempt > 1) {
        std::string from = ra_log();
        std::string to = gameos::log_dir() + "/retroarch-" + sid + "-a" +
                         std::to_string(attempt - 1) + ".log";
        if (rename(from.c_str(), to.c_str()) == 0)
            d.line("archived previous retroarch.log -> %s", to.c_str());
    }
    if (!override_cfg.empty()) {
        write_file("/tmp/gameos-ra-override.cfg", override_cfg);
        d.text("override.cfg", override_cfg);
    } else {
        d.line("override.cfg: (none)");
    }

    // Build argv before fork so the child only has to exec it. The full
    // command is logged here, where std::string work cannot deadlock a
    // post-fork process that inherits the UI's worker thread.
    std::string core_path, cfg_path, ph;
    std::vector<std::string> args;
    if (c.core) {
        core_path = std::string("/usr/lib/libretro/") + c.core;
        cfg_path = std::string("/usr/share/gameos/retroarch/") + c.appendcfg;
        args = { "retroarch",
                 "--log-file", ra_log(),
                 "-L", core_path,
                 "--appendconfig", cfg_path,
                 "--appendconfig", "/tmp/gameos-ra-lang.cfg" };
        // RetroAchievements credentials live on /userdata so they survive an
        // OTA. Appended last so they win over the per-console defaults;
        // config_save_on_exit is false, so RetroArch never rewrites them.
        if (access("/userdata/system/retroarch/cheevos.cfg", F_OK) == 0) {
            args.push_back("--appendconfig");
            args.push_back("/userdata/system/retroarch/cheevos.cfg");
        }
        // Last so it wins over the per-console defaults: a crash fallback.
        if (!override_cfg.empty()) {
            args.push_back("--appendconfig");
            args.push_back("/tmp/gameos-ra-override.cfg");
        }
        // NDS used to run with --verbose unconditionally. That writes a few
        // hundred extra lines to the exFAT log during the very seconds we are
        // trying to save. Keep it available for diagnosis: create an empty
        // /userdata/system/ra-verbose and the next launch logs verbosely.
        if (c.dual && access("/userdata/system/ra-verbose", F_OK) == 0)
            args.push_back("--verbose");
        args.push_back(g.path);
    } else {
        args = { "ppsspp-run", g.path };
    }
    d.line("exec argv: %s", join_args(args).c_str());
    std::vector<char*> av;
    for (auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);

    if (c.dual) ph = std::to_string(panel_height());

    struct stat est = {};
    const std::string emu_path = emu_log();
    const char* logmode = "a";
    if (stat(emu_path.c_str(), &est) == 0 && est.st_size > (1 << 20))
        logmode = "w";
    append_marker("\n===== gameos attempt %d at %s (%s) =====\n",
                  attempt, wall_clock().c_str(), g.path.c_str());

    pid_t pid = fork();
    if (pid < 0) {
        d.line("fork failed errno=%d", errno);
        return -1;
    }

    if (pid == 0) {
        // ---- child ----
        // Own process group: gameos-sleepd SIGSTOPs the emulator group on
        // lid close; sharing the UI's group would freeze the front-end too.
        // The parent repeats this call (one wins, the other gets EACCES), so a
        // pause request can never race the fork and hit the UI's group.
        if (setpgid(0, 0) != 0)
            dprintf(2, "%s: child setpgid failed (errno %d)\n",
                    subsystem_name(c), errno);
        setenv("SDL_VIDEODRIVER", "kmsdrm", 1);
        setenv("XDG_RUNTIME_DIR", "/tmp/gameos-runtime", 1);

        // Emulator output (core printf, exec errors) lands in one log on
        // the card so a failed launch can be diagnosed after the fact.
        freopen(emu_path.c_str(), logmode, stdout);
        freopen(emu_path.c_str(), logmode, stderr);
        dprintf(1, "%s: launch %s\n", subsystem_name(c), g.path.c_str());

        if (c.dual) {
            // NDS: span both panels, map bottom-panel touch to y+panel_h.
            setenv("GAMEOS_DUALSCREEN", "1", 1);
            setenv("GAMEOS_TOUCH_Y_OFFSET", ph.c_str(), 1);
            setenv("GAMEOS_TOUCH_PANEL_H", ph.c_str(), 1);
            setenv("LIBGL_DEBUG", "verbose", 1);
        } else {
            unsetenv("GAMEOS_DUALSCREEN");
            unsetenv("GAMEOS_TOUCH_Y_OFFSET");
            unsetenv("GAMEOS_TOUCH_PANEL_H");
            unsetenv("LIBGL_DEBUG");
        }

        execvp(av[0], av.data());
        dprintf(2, "%s: exec failed (errno %d)\n", subsystem_name(c), errno);
        _exit(127); // exec failed
    }

    // ---- parent ----
    d.line("forked child pid=%d", (int)pid);
    // Mirror the child's setpgid. Whichever call lands first wins; the loser
    // gets EACCES/ESRCH once the child has exec'd. Doing it on both sides
    // closes the fork window in which gameos-sleepd could still see the
    // emulator inside the UI's process group.
    if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH)
        d.line("parent setpgid(%d) failed errno=%d", (int)pid, errno);
    // The parent owns the performance profile switch (never the child): apply
    // the console's tier now and restore the UI tier on EVERY exit path,
    // including exec failure (child _exit(127)) and waitpid errors.
    apply_perf(perf.c_str());
    // Publish game.pid only once the child is visibly its own group leader; a
    // pause request that raced the fork would otherwise SIGSTOP the UI. A
    // short retry covers the instant between fork and the child's setpgid.
    bool pg_ok = false;
    for (int i = 0; i < 50; i++) {
        if (getpgid(pid) == pid) { pg_ok = true; break; }
        usleep(1000);
    }
    if (pg_ok) {
        mkdir("/run/gameos", 0755);
        const std::string tmp = "/run/gameos/game.pid.tmp";
        if (write_file(tmp, std::to_string(pid)) &&
            rename(tmp.c_str(), "/run/gameos/game.pid") == 0) {
            d.line("published game.pid pid=%d", (int)pid);
        } else {
            unlink(tmp.c_str());
            d.line("ERROR: cannot publish game.pid errno=%d", errno);
        }
    } else {
        d.line("ERROR: child %d never became a group leader; game.pid not published",
               (int)pid);
    }

    struct timespec ts0 = {}, ts1 = {};
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    auto elapsed_ms = [&ts0]() {
        struct timespec now = {};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (long)((now.tv_sec - ts0.tv_sec) * 1000 +
                      (now.tv_nsec - ts0.tv_nsec) / 1000000);
    };
    long tick = sysconf(_SC_CLK_TCK);
    if (tick <= 0) tick = 100;

    int status = 0;
    bool reaped = false;
    long next_ms = 0;
    long sample = 0;
    long last_ms = 0;
    unsigned long long last_ticks = 0;
    bool have_last = false;

    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            reaped = true;
            break;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            d.line("waitpid failed errno=%d", errno);
            break;
        }
        long ms = elapsed_ms();
        if (ms >= next_ms) {
            ProcInfo pi;
            if (read_proc(pid, pi)) {
                if (have_last && ms > last_ms) {
                    double cpu = 100.0 * (double)(pi.ticks - last_ticks) /
                                 (double)tick / ((double)(ms - last_ms) / 1000.0);
                    d.line("sample t=%ldms state=%c threads=%d rss=%ldkB cpu=%.0f%% wchan=%s",
                           ms, pi.state, pi.threads, pi.rss_kb, cpu, pi.wchan.c_str());
                } else {
                    d.line("sample t=%ldms state=%c threads=%d rss=%ldkB cpu=? wchan=%s",
                           ms, pi.state, pi.threads, pi.rss_kb, pi.wchan.c_str());
                }
                last_ticks = pi.ticks;
                have_last = true;
            } else {
                d.line("sample t=%ldms process-not-readable (pre-exec or gone)", ms);
            }
            last_ms = ms;
            // DRM/memory every 10 s; open DRM/input/ALSA fds every ~11 s.
            if (sample % 10 == 0) {
                dump_mem(d, "sample");
                dump_drm(d, "sample");
            }
            if (sample % 10 == 5) dump_fds(d, pid, "sample");
            sample++;
            next_ms = ms < 30000 ? ms + 1000
                     : (ms < 300000 ? ms + 5000 : ms + 30000);
        }
        usleep(200000);
    }

    clock_gettime(CLOCK_MONOTONIC, &ts1);
    double run_s = (double)(ts1.tv_sec - ts0.tv_sec) +
                   (double)(ts1.tv_nsec - ts0.tv_nsec) / 1e9;
    if (run_s < 0) run_s = 0;
    seconds = (long)run_s;

    int rc = -1;
    int sig = 0;
    if (reaped) {
        if (WIFEXITED(status)) rc = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) sig = WTERMSIG(status);
    }
    d.line("attempt %d end: reaped=%d rc=%d signal=%d runtime=%.1fs",
           attempt, (int)reaped, rc, sig, run_s);
    d.tail("retroarch.log", ra_log(), 120);
    if (rc != 0 || run_s < 30) dump_dmesg(d, "exit");
    dump_mem(d, "exit");
    dump_drm(d, "exit");
    dump_backlight(d, "exit");

    if (userdata_mounted()) {
        std::string summary;
        summary += "time=" + wall_clock() + "\n";
        summary += "gameos_version=" + first_line("/etc/gameos-version") + "\n";
        summary += "diag_build=" + first_line("/etc/gameos-diag-build") + "\n";
        summary += "console=" + std::string(c.id) + "\n";
        summary += "title=" + g.title + "\n";
        summary += "rom=" + g.path + "\n";
        summary += "attempt=" + std::to_string(attempt) + "\n";
        summary += "rc=" + std::to_string(rc) + "\n";
        summary += "signal=" + std::to_string(sig) + "\n";
        summary += "runtime_s=" + std::to_string(seconds) + "\n";
        summary += "retroarch_log=" + ra_log() + "\n";
        summary += "session_log=" + gameos::log_file("emu-session.log") + "\n";
        write_file(gameos::log_dir() + "/last-result.txt", summary);
    }

    unlink("/run/gameos/game.pid");
    apply_perf("ui");
    return reaped && sig == 0 ? rc : -1;
}

// Some NDS titles (and, on a few units, melonDS itself) crash early with the
// threaded software renderer or the JIT. When the first attempt dies quickly,
// retry with safer settings for this launch only. The choice is never
// persisted: a saved "JIT off" made every later launch run in slow motion.
// A crash after a real session is not retried — that would replay the game
// file and hide the failure.
static const char* NDS_SAFE_CFG = "/userdata/system/nds-safe.cfg";
static const char* NDS_SAFE1 =
    "melonds_threaded_renderer = \"disabled\"\n";
static const char* NDS_SAFE2 =
    "melonds_threaded_renderer = \"disabled\"\n"
    "melonds_jit_enable = \"disabled\"\n";

int launch_game(const Console& c, const Game& g)
{
    // Localise emulator menus to the system language before forking.
    // RetroArch: an extra appendconfig (applied last) sets user_language.
    write_file("/tmp/gameos-ra-lang.cfg",
               "user_language = " + std::to_string(retro_language()) + "\n");
    apply_ppsspp_language();

    Diag d;
    std::string sid = session_id();
    if (userdata_mounted()) {
        // Start every launch with a fresh RetroArch log (the emulator may
        // append); the previous one is kept for comparison.
        struct stat st = {};
        const std::string path = ra_log();
        if (stat(path.c_str(), &st) == 0) {
            std::string prev = gameos::log_dir() + "/retroarch-prev.log";
            if (rename(path.c_str(), prev.c_str()) == 0)
                d.line("archived previous retroarch.log -> %s", prev.c_str());
        }
    }

    // A safe cfg persisted by an older build must not apply any more: it may
    // have JIT/threads disabled, which reads as "game runs in slow motion".
    unlink(NDS_SAFE_CFG);

    std::vector<std::string> ov_warn;
    overrides().reload_if_changed(&ov_warn);
    for (const auto& w : ov_warn) d.line("override: %s", w.c_str());
    const GameOverride* ov = overrides().find(c.id, g.path);
    const std::string base_cfg = ov ? override_cfg(*ov, c.id) : "";
    const std::string perf = (ov && ov->perf >= 0)
                             ? perf_name(ov->perf)
                             : perf_profile(c);
    d.line("override key=%s perf=%s title=%s",
           override_key(c.id, g.path).c_str(), perf.c_str(),
           ov && !ov->title.empty() ? ov->title.c_str() : "(file name)");
    if (!base_cfg.empty()) d.text("per-game cfg", base_cfg);

    std::string cfg = base_cfg;
    dump_session_header(d, c, g, cfg);
    if (!c.core) d.stat_info("ppsspp-run", "/usr/bin/ppsspp-run");

    long secs = 0;
    int rc = run_emu(c, g, cfg, perf, secs, d, 1, sid);
    if (!c.dual || rc == 0 || rc == 127 || secs >= 30) {
        d.line("launch finished rc=%d runtime=%lds (no retry)", rc, secs);
        return rc;
    }

    d.line("early NDS failure rc=%d runtime=%lds -> retry without threaded renderer",
           rc, secs);
    cfg = std::string(NDS_SAFE1) + base_cfg;
    rc = run_emu(c, g, cfg, perf, secs, d, 2, sid);
    if (rc == 0) {
        d.line("retry with threaded renderer off worked (not persisted)");
        return 0;
    }
    if (secs >= 30) {
        d.line("retry still failed after %lds; giving up", secs);
        return rc;
    }

    d.line("second failure rc=%d runtime=%lds -> retry with JIT disabled too",
           rc, secs);
    cfg = std::string(NDS_SAFE2) + base_cfg;
    rc = run_emu(c, g, cfg, perf, secs, d, 3, sid);
    if (rc == 0)
        d.line("retry with threaded renderer + JIT off worked (not persisted)");
    return rc;
}

} // namespace gameos