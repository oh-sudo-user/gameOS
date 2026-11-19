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

        if (c.dual) {
            // NDS: span both panels, map bottom-panel touch to y+768.
            setenv("GAMEOS_DUALSCREEN", "1", 1);
            setenv("GAMEOS_TOUCH_Y_OFFSET", "768", 1);
            setenv("GAMEOS_TOUCH_PANEL_H", "768", 1);
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
                "retroarch", "-L", core.c_str(),
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
        _exit(127); // exec failed
    }

    // ---- parent ----
    // Publish the emulator pid for gameos-sleepd (lid-close pause/resume).
    mkdir("/run/gameos", 0755);
    write_file("/run/gameos/game.pid", std::to_string(pid));
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            unlink("/run/gameos/game.pid");
            return -1;
        }
    }
    unlink("/run/gameos/game.pid");
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

} // namespace gameos