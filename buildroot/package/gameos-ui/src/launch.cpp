// gameOS front-end — emulator launch.
//
// The caller MUST tear down SDL (releasing DRM master) before calling
// launch_game(), and re-initialise afterwards. The child inherits the
// DRM device and becomes master itself.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"
#include "i18n.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>
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

// Performance profile by console identity (never guessed from the ROM
// extension): NDS/PS1/PSP are heavy, GBA/GBC are light. gameos-perf maps these
// to governor preferences in /etc/gameos/perf.conf.
static const char* perf_profile(const Console& c)
{
    if (c.dual) return "heavy";                 // NDS spans both panels
    if (strcmp(c.id, "ps1") == 0) return "heavy";
    if (strcmp(c.id, "psp") == 0) return "heavy";
    return "light";                             // GBA / GBC
}

// Subsystem identity: our own launch/tuning layer around each engine.
static const char* subsystem_name(const Console& c)
{
    if (c.dual) return "game nds";
    if (strcmp(c.id, "gba") == 0) return "game gba";
    if (strcmp(c.id, "psp") == 0) return "game psp";
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

int launch_game(const Console& c, const Game& g)
{
    // Localise emulator menus to the system language before forking.
    // RetroArch: an extra appendconfig (applied last) sets user_language.
    write_file("/tmp/gameos-ra-lang.cfg",
               "user_language = " + std::to_string(retro_language()) + "\n");
    apply_ppsspp_language();

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // ---- child ----
        // Own process group: gameos-sleepd SIGSTOPs the emulator group on
        // lid close; sharing the UI's group would freeze the front-end too.
        setpgid(0, 0);
        setenv("SDL_VIDEODRIVER", "kmsdrm", 1);
        setenv("XDG_RUNTIME_DIR", "/tmp/gameos-runtime", 1);

        // Emulator output (core printf, exec errors) lands in one log on
        // /userdata so a failed launch can be diagnosed after the fact.
        mkdir("/userdata/system", 0755);
        const char* logmode = "a";
        struct stat st;
        if (stat("/userdata/system/emu.log", &st) == 0 && st.st_size > (1 << 20))
            logmode = "w";
        freopen("/userdata/system/emu.log", logmode, stdout);
        freopen("/userdata/system/emu.log", logmode, stderr);
        dprintf(1, "%s: launch %s\n", subsystem_name(c), g.path.c_str());

        if (c.dual) {
            // NDS: span both panels, map bottom-panel touch to y+panel_h.
            const std::string ph = std::to_string(panel_height());
            setenv("GAMEOS_DUALSCREEN", "1", 1);
            setenv("GAMEOS_TOUCH_Y_OFFSET", ph.c_str(), 1);
            setenv("GAMEOS_TOUCH_PANEL_H", ph.c_str(), 1);
        } else {
            unsetenv("GAMEOS_DUALSCREEN");
            unsetenv("GAMEOS_TOUCH_Y_OFFSET");
            unsetenv("GAMEOS_TOUCH_PANEL_H");
        }

        if (c.core) {
            std::string core = std::string("/usr/lib/libretro/") + c.core;
            std::string cfg =
                std::string("/usr/share/gameos/retroarch/") + c.appendcfg;
            std::vector<const char*> av = {
                "retroarch",
                "--log-file", "/userdata/system/retroarch.log",
                "-L", core.c_str(),
                "--appendconfig", cfg.c_str(),
                "--appendconfig", "/tmp/gameos-ra-lang.cfg",
            };
            // RetroAchievements credentials live on /userdata so they survive
            // an OTA. Appended last so they win over the per-console defaults;
            // config_save_on_exit is false, so RetroArch never rewrites them.
            if (access("/userdata/system/retroarch/cheevos.cfg", F_OK) == 0) {
                av.push_back("--appendconfig");
                av.push_back("/userdata/system/retroarch/cheevos.cfg");
            }
            av.push_back(g.path.c_str());
            av.push_back(nullptr);
            execvp("retroarch", (char* const*)av.data());
        } else {
            execlp("ppsspp-run", "ppsspp-run", g.path.c_str(), (char*)nullptr);
        }
        dprintf(2, "%s: exec failed (errno %d)\n", subsystem_name(c), errno);
        _exit(127); // exec failed
    }

    // ---- parent ----
    // The parent owns the performance profile switch (never the child): apply
    // the console's tier now and restore the UI tier on EVERY exit path,
    // including exec failure (child _exit(127)) and waitpid errors.
    apply_perf(perf_profile(c));
    // Publish the emulator pid for gameos-sleepd (lid-close pause/resume).
    mkdir("/run/gameos", 0755);
    write_file("/run/gameos/game.pid", std::to_string(pid));
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            unlink("/run/gameos/game.pid");
            apply_perf("ui");
            return -1;
        }
    }
    unlink("/run/gameos/game.pid");
    apply_perf("ui");
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

} // namespace gameos