// gameOS front-end — emulator launch.
//
// The caller MUST tear down SDL (releasing DRM master) before calling
// launch_game(), and re-initialise afterwards. The child inherits the
// DRM device and becomes master itself.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"
#include "i18n.hpp"
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
// Persistent launch diagnostics
//
// The UI comes back to life after a black-screen emulator exit, so the card
// is the only reliable witness. Everything here is appended under
// /userdata/system/diag and survives reboots; the data partition is exFAT,
// so the logs can be read directly on a PC. Sampling never blocks the child.
// ---------------------------------------------------------------------------
namespace {

constexpr const char* DIAG_DIR = "/userdata/system/diag";
constexpr const char* SESSION_LOG = "/userdata/system/diag/emu-session.log";

bool userdata_mounted()
{
    struct stat a = {}, b = {};
    if (stat("/userdata", &a) != 0 || stat("/", &b) != 0) return false;
    return a.st_dev != b.st_dev;
}

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
        if (!userdata_mounted()) return;
        mkdir("/userdata/system", 0755);
        mkdir(DIAG_DIR, 0755);
        struct stat st = {};
        if (stat(SESSION_LOG, &st) == 0 && st.st_size > (4 << 20)) {
            unlink("/userdata/system/diag/emu-session.log.1");
            rename(SESSION_LOG, "/userdata/system/diag/emu-session.log.1");
        }
        f = fopen(SESSION_LOG, "a");
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
    if (!userdata_mounted()) return;
    FILE* f = fopen("/userdata/system/emu.log", "a");
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
        std::string from = "/userdata/system/retroarch.log";
        std::string to = std::string(DIAG_DIR) + "/retroarch-" + sid + "-a" +
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
                 "--log-file", "/userdata/system/retroarch.log",
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
        if (c.dual) args.push_back("--verbose");
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
    const char* logmode = "a";
    if (stat("/userdata/system/emu.log", &est) == 0 && est.st_size > (1 << 20))
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
        // /userdata so a failed launch can be diagnosed after the fact.
        mkdir("/userdata/system", 0755);
        freopen("/userdata/system/emu.log", logmode, stdout);
        freopen("/userdata/system/emu.log", logmode, stderr);
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
    d.tail("retroarch.log", "/userdata/system/retroarch.log", 120);
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
        summary += "retroarch_log=/userdata/system/retroarch.log\n";
        summary += "session_log=/userdata/system/diag/emu-session.log\n";
        write_file(std::string(DIAG_DIR) + "/last-result.txt", summary);
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
        if (stat("/userdata/system/retroarch.log", &st) == 0) {
            std::string prev = std::string(DIAG_DIR) + "/retroarch-prev.log";
            if (rename("/userdata/system/retroarch.log", prev.c_str()) == 0)
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