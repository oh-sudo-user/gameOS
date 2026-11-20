// gameOS front-end — GT911 touch input for the lower panel.
//
// SDL's kmsdrm backend does not deliver touch events, so the panel is read
// straight from evdev. Multi-touch protocol B (ABS_MT_SLOT / TRACKING_ID) is
// tracked per slot; the first active contact drives the UI. Raw coordinates
// are mapped onto the 1024x768 design space (RG DS reports 640x480).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>

namespace gameos {
namespace {

constexpr int DW = 1024;
constexpr int DH = 768;
constexpr int MAX_SLOTS = 10;

struct Slot {
    bool active = false;
    int x = 0, y = 0;
};

struct TouchDev {
    int fd = -1;
    int minx = 0, maxx = 1023;
    int miny = 0, maxy = 767;
    int raw_x = 0, raw_y = 0;     // single-touch fallback (ABS_X/Y)
    int cur_slot = 0;
    Slot slots[MAX_SLOTS];
    bool down = false;
    int last_x = 0, last_y = 0;
    std::string name;
    Uint32 retry_at = 0;
};

TouchDev g_dev;
std::deque<TouchEvent> g_events;
bool g_have_mt = false;

int map_range(int v, int lo, int hi, int out)
{
    if (hi <= lo) return 0;
    v = std::clamp(v, lo, hi);
    return (v - lo) * out / (hi - lo);
}

bool has_bit(const unsigned long* bits, int bit)
{
    return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1UL;
}

bool probe(const char* path, int& fd, TouchDev& dev)
{
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;

    unsigned long absbits[ABS_MAX / (8 * sizeof(long)) + 2] = {};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0 ||
        !has_bit(absbits, ABS_MT_POSITION_X) ||
        !has_bit(absbits, ABS_MT_POSITION_Y)) {
        close(fd);
        fd = -1;
        return false;
    }

    struct input_absinfo xi = {}, yi = {};
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &xi) < 0 ||
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &yi) < 0) {
        close(fd);
        fd = -1;
        return false;
    }
    dev.minx = xi.minimum;
    dev.maxx = xi.maximum;
    dev.miny = yi.minimum;
    dev.maxy = yi.maximum;
    if (dev.maxx <= dev.minx || dev.maxy <= dev.miny) {
        close(fd);
        fd = -1;
        return false;
    }
    char name[128] = {};
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
    dev.name = name;
    return true;
}

void open_device()
{
    if (g_dev.fd >= 0) return;
    if (g_dev.retry_at && SDL_GetTicks() < g_dev.retry_at) return;

    std::string best;
    bool best_goodix = false;
    DIR* d = opendir("/dev/input");
    if (!d) {
        g_dev.retry_at = SDL_GetTicks() + 2000;
        return;
    }
    struct dirent* e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + e->d_name;
        int fd = -1;
        TouchDev tmp;
        if (!probe(path.c_str(), fd, tmp)) continue;
        close(fd);
        bool goodix = tmp.name.find("goodix") != std::string::npos ||
                      tmp.name.find("Goodix") != std::string::npos ||
                      tmp.name.find("GT911") != std::string::npos;
        if (best.empty() || (goodix && !best_goodix)) {
            best = path;
            best_goodix = goodix;
            g_dev.minx = tmp.minx;
            g_dev.maxx = tmp.maxx;
            g_dev.miny = tmp.miny;
            g_dev.maxy = tmp.maxy;
            g_dev.name = tmp.name;
        }
    }
    closedir(d);

    if (best.empty()) {
        g_dev.retry_at = SDL_GetTicks() + 2000;
        return;
    }
    int fd = open(best.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        g_dev.retry_at = SDL_GetTicks() + 2000;
        return;
    }
    g_dev.fd = fd;
    g_dev.retry_at = 0;
    fprintf(stderr, "gameos-ui: touch %s (%s) %d..%d x %d..%d\n",
            best.c_str(), g_dev.name.c_str(),
            g_dev.minx, g_dev.maxx, g_dev.miny, g_dev.maxy);
}

void close_device()
{
    if (g_dev.fd >= 0) close(g_dev.fd);
    g_dev.fd = -1;
    g_dev.down = false;
    for (auto& s : g_dev.slots) s = Slot{};
    g_dev.retry_at = SDL_GetTicks() + 1000;
}

void push(TouchPhase p, int rx, int ry)
{
    TouchEvent ev;
    ev.phase = p;
    ev.x = (float)map_range(rx, g_dev.minx, g_dev.maxx, DW - 1);
    ev.y = (float)map_range(ry, g_dev.miny, g_dev.maxy, DH - 1);
    if (g_events.size() > 32) g_events.clear();
    g_events.push_back(ev);
}

const Slot* primary_slot()
{
    for (const auto& s : g_dev.slots)
        if (s.active) return &s;
    return nullptr;
}

void handle_syn()
{
    const Slot* p = primary_slot();
    int rx = 0, ry = 0;
    bool active = false;
    if (p) {
        rx = p->x;
        ry = p->y;
        active = true;
        g_have_mt = true;
    } else if (!g_have_mt) {
        rx = g_dev.raw_x;
        ry = g_dev.raw_y;
        // ABS_X/ABS_Y drivers report before the first MT slot only; keep the
        // fallback for them, but never fabricate a hover.
        active = g_dev.down;
    }

    if (active && !g_dev.down) {
        g_dev.down = true;
        g_dev.last_x = rx;
        g_dev.last_y = ry;
        push(TouchPhase::Down, rx, ry);
    } else if (active && g_dev.down) {
        if (rx != g_dev.last_x || ry != g_dev.last_y) {
            g_dev.last_x = rx;
            g_dev.last_y = ry;
            push(TouchPhase::Move, rx, ry);
        }
    } else if (!active && g_dev.down) {
        g_dev.down = false;
        push(TouchPhase::Up, g_dev.last_x, g_dev.last_y);
    }
}

void read_events()
{
    struct input_event ev[32];
    for (;;) {
        ssize_t n = read(g_dev.fd, ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            close_device();
            return;
        }
        if (n == 0) return;
        int cnt = (int)(n / sizeof(struct input_event));
        for (int i = 0; i < cnt; i++) {
            if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT) {
                handle_syn();
            } else if (ev[i].type == EV_ABS) {
                int code = ev[i].code;
                int val = ev[i].value;
                if (code == ABS_MT_SLOT) {
                    if (val >= 0 && val < MAX_SLOTS) g_dev.cur_slot = val;
                } else if (code == ABS_MT_TRACKING_ID) {
                    Slot& s = g_dev.slots[g_dev.cur_slot];
                    if (val >= 0) {
                        s.active = true;
                        s.x = g_dev.raw_x;
                        s.y = g_dev.raw_y;
                    } else {
                        s.active = false;
                    }
                } else if (code == ABS_MT_POSITION_X) {
                    g_dev.slots[g_dev.cur_slot].x = val;
                } else if (code == ABS_MT_POSITION_Y) {
                    g_dev.slots[g_dev.cur_slot].y = val;
                } else if (code == ABS_X) {
                    g_dev.raw_x = val;
                } else if (code == ABS_Y) {
                    g_dev.raw_y = val;
                }
            }
        }
    }
}

} // namespace

void touch_init()
{
    open_device();
}

bool touch_available()
{
    return g_dev.fd >= 0;
}

bool touch_poll(TouchEvent& ev)
{
    if (g_dev.fd < 0) open_device();
    if (g_dev.fd >= 0) read_events();
    if (g_events.empty()) return false;
    ev = g_events.front();
    g_events.pop_front();
    return true;
}

void touch_shutdown()
{
    close_device();
    g_events.clear();
}

} // namespace gameos
