// Shared log locations. Every component writes below one folder on the TF
// card (GAMEOS-DATA) so a failure can be diagnosed by reading the card on a
// PC. Header-only: the UI, the splash and the profiler each include it.
#pragma once

#include <cstdlib>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace gameos {

inline bool log_writable(const std::string& dir)
{
    struct stat st = {};
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
           access(dir.c_str(), W_OK) == 0;
}

inline bool data_mounted()
{
    struct stat a = {}, b = {};
    if (stat("/userdata", &a) != 0 || stat("/", &b) != 0) return false;
    return a.st_dev != b.st_dev;
}

// /userdata/system is only reachable once the data partition is mounted
// (compare devices with / so a stale directory on the rootfs cannot capture
// logs). /boot is the same card and survives reboots; /tmp is the last
// resort (RAM) so logging never blocks.
inline const std::string& log_dir()
{
    static std::string dir;
    if (!dir.empty()) return dir;
    if (const char* e = getenv("GAMEOS_LOG_DIR")) {
        if (*e) {
            mkdir(e, 0755);
            dir = e;
            return dir;
        }
    }
    const char* cand[3];
    int n = 0;
    if (data_mounted()) cand[n++] = "/userdata/system/logs";
    cand[n++] = "/boot/logs";
    cand[n++] = "/tmp/gameos-logs";
    for (int i = 0; i < n; i++) {
        mkdir(cand[i], 0755);
        if (log_writable(cand[i])) { dir = cand[i]; return dir; }
    }
    dir = "/tmp";
    return dir;
}

inline std::string log_file(const char* name)
{
    return log_dir() + "/" + name;
}

// Keep one previous generation. Called once per process (or at boot) right
// before a log is opened, so the stat cost is negligible.
inline void log_rotate(const std::string& path, long max_bytes)
{
    struct stat st = {};
    if (stat(path.c_str(), &st) == 0 && st.st_size > max_bytes)
        rename(path.c_str(), (path + ".1").c_str());
}

} // namespace gameos
