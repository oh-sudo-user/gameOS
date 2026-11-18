// gameOS front-end — emulator launch.
//
// The caller MUST tear down SDL (releasing DRM master) before calling
// launch_game(), and re-initialise afterwards. The child inherits the
// DRM device and becomes master itself.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace gameos {

int launch_game(const Console& c, const Game& g)
{
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
            execlp("retroarch", "retroarch",
                   "-L", core.c_str(),
                   "--appendconfig", cfg.c_str(),
                   g.path.c_str(), (char*)nullptr);
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