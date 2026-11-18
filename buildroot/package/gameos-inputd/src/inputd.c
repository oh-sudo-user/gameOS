// gameos-inputd — merge the RG DS Plus split input devices into one gamepad.
//
// The RG DS Plus kernel exposes:
//   "gpio-keys-control" : all face/shoulder/menu buttons (EV_KEY only)
//   "adc-joystick"      : analog stick X/Y (EV_ABS only)
//   "gpio-keys-volume"  : volume up/down keys
//   "gpio-keys-hall"    : lid switch (SW_LID)
//
// SDL and RetroArch would treat the first two as separate (broken) joypads,
// so this daemon reads them directly and emits a single virtual uinput
// gamepad "gameOS Gamepad" with standard Linux gamepad codes. udev rules
// hide the raw devices from SDL/RetroArch (see 99-gameos-input.rules).
//
// Extras: volume keys drive ALSA directly; lid close blanks both panels
// (deliberately no suspend — suspend is unreliable on this hardware).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

#define PROG "gameos-inputd"

static volatile sig_atomic_t running = 1;
static void on_signal(int sig) { (void)sig; running = 0; }

static int open_named(const char *want)
{
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    int fd = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int f = open(path, O_RDONLY | O_NONBLOCK);
        if (f < 0) continue;
        char name[128] = {0};
        if (ioctl(f, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
            strcmp(name, want) == 0) {
            fd = f;
            break;
        }
        close(f);
    }
    closedir(d);
    return fd;
}

static void emit(int fd, int type, int code, int val)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = val;
    if (write(fd, &ev, sizeof(ev)) < 0) { /* device gone; ignore */ }
}

static void syn(int fd) { emit(fd, EV_SYN, SYN_REPORT, 0); }

static int uinput_open(void)
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror(PROG ": open /dev/uinput");
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    static const int buttons[] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
        BTN_SELECT, BTN_START, BTN_MODE, BTN_BACK,
        BTN_THUMBL, BTN_THUMBR,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
    };
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
        ioctl(fd, UI_SET_KEYBIT, buttons[i]);

    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);

    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    snprintf(us.name, UINPUT_MAX_NAME_SIZE, "gameOS Gamepad");
    us.id.bustype = BUS_VIRTUAL;
    us.id.vendor  = 0x1234;
    us.id.product = 0x5678;
    us.id.version = 1;

    struct uinput_abs_setup abs;
    memset(&abs, 0, sizeof(abs));
    abs.absinfo.minimum = -32768;
    abs.absinfo.maximum =  32767;
    abs.absinfo.fuzz    = 256;
    abs.absinfo.flat    = 512;
    abs.code = ABS_X;
    ioctl(fd, UI_ABS_SETUP, &abs);
    abs.code = ABS_Y;
    ioctl(fd, UI_ABS_SETUP, &abs);

    if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        perror(PROG ": uinput setup");
        close(fd);
        return -1;
    }
    return fd;
}

// normalize an axis reading using the device's own abs range
static int norm_axis(int fd, int code, int raw)
{
    struct input_absinfo ai;
    if (ioctl(fd, EVIOCGABS(code), &ai) < 0) return 0;
    long span = (long)ai.maximum - ai.minimum;
    if (span <= 0) return 0;
    long v = ((long)raw - ai.minimum) * 65535L / span - 32768L;
    if (v < -32768) v = -32768;
    if (v >  32767) v =  32767;
    return (int)v;
}

static void set_backlight(int percent_of_max)
{
    // percent_of_max < 0 means "restore saved values".
    // Scan /sys/class/backlight so we work with any panel naming.
    static int saved[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent *e;
    int idx = 0;
    while ((e = readdir(d)) && idx < 8) {
        if (e->d_name[0] == '.') continue;
        char p[PATH_MAX], mp[PATH_MAX];
        snprintf(p, sizeof(p), "/sys/class/backlight/%s/brightness", e->d_name);
        snprintf(mp, sizeof(mp), "/sys/class/backlight/%s/max_brightness", e->d_name);
        int f = open(p, O_RDWR);
        if (f < 0) continue;
        if (percent_of_max >= 0) {
            int max = 255;
            int mf = open(mp, O_RDONLY);
            if (mf >= 0) {
                char b[32] = {0};
                if (read(mf, b, sizeof(b) - 1) > 0) max = atoi(b);
                close(mf);
            }
            int cf = open(p, O_RDONLY);
            if (cf >= 0) {
                char b[32] = {0};
                if (read(cf, b, sizeof(b) - 1) > 0) saved[idx] = atoi(b);
                close(cf);
            }
            char out[32];
            int n = snprintf(out, sizeof(out), "%d", max * percent_of_max / 100);
            if (write(f, out, n) < 0) { /* ignore */ }
        } else if (saved[idx] >= 0) {
            char out[32];
            int n = snprintf(out, sizeof(out), "%d", saved[idx]);
            if (write(f, out, n) < 0) { /* ignore */ }
        }
        close(f);
        idx++;
    }
    closedir(d);
}

static void handle_volume(int code)
{
    const char *cmd = (code == KEY_VOLUMEUP)
        ? "amixer -c 0 sset Master 5%+ >/dev/null 2>&1; amixer -c 1 sset Master 5%+ >/dev/null 2>&1"
        : "amixer -c 0 sset Master 5%- >/dev/null 2>&1; amixer -c 1 sset Master 5%- >/dev/null 2>&1";
    if (system(cmd) != 0) { /* ignore */ }
}

int main(void)
{
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    int ufd = -1, bfd = -1, afd = -1, vfd = -1, hfd = -1;

    // wait for devices (udev may still be settling at boot)
    for (int tries = 0; tries < 60 && running; tries++) {
        if (ufd < 0) ufd = uinput_open();
        if (bfd < 0) bfd = open_named("gpio-keys-control");
        if (afd < 0) afd = open_named("adc-joystick");
        if (vfd < 0) vfd = open_named("gpio-keys-volume");
        if (hfd < 0) hfd = open_named("gpio-keys-hall");
        if (ufd >= 0 && bfd >= 0 && afd >= 0) break;
        usleep(500000);
    }
    if (ufd < 0 || bfd < 0 || afd < 0) {
        fprintf(stderr, PROG ": required input devices missing, exiting\n");
        return 1;
    }
    fprintf(stderr, PROG ": virtual gamepad up (buttons fd=%d axes fd=%d)\n", bfd, afd);

    // Live device fds, addressed by slot so reconnects (which change the fd
    // number) never confuse the source identification. Slots 0/1 (buttons,
    // axes) are required; 2/3 (volume, hall) are optional and may appear late.
    int *slots[4] = { &bfd, &afd, &vfd, &hfd };
    const char *names[4] = { "gpio-keys-control", "adc-joystick",
                             "gpio-keys-volume", "gpio-keys-hall" };
    time_t last_reconnect = 0;

    while (running) {
        // (Re)open any missing device at most once per second: recovers from
        // runtime disconnects and picks up optional devices that probe late.
        time_t now = time(NULL);
        if (now != last_reconnect) {
            last_reconnect = now;
            for (int i = 0; i < 4; i++) {
                if (*slots[i] < 0) {
                    int f = open_named(names[i]);
                    if (f >= 0) {
                        *slots[i] = f;
                        fprintf(stderr, PROG ": %s connected fd=%d\n", names[i], f);
                    }
                }
            }
            if (ufd < 0) ufd = uinput_open();
        }

        // Required device missing: wait briefly instead of busy-spinning.
        if (bfd < 0 || afd < 0 || ufd < 0) { usleep(200000); continue; }

        struct pollfd pf[4];
        int slot_of[4];
        int n = 0;
        for (int i = 0; i < 4; i++) {
            if (*slots[i] >= 0) {
                pf[n].fd = *slots[i];
                pf[n].events = POLLIN;
                pf[n].revents = 0;
                slot_of[n] = i;
                n++;
            }
        }
        int pr = poll(pf, n, 1000);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) continue;

        for (int i = 0; i < n; i++) {
            int slot = slot_of[i];
            int fd = pf[i].fd;

            // Hangup / error / invalid fd: the device is gone. Close it and
            // mark the slot empty so the reconnect block above reopens it.
            // Without this, poll returns POLLNVAL/HUP immediately forever and
            // the daemon busy-spins at 100% CPU with no input.
            if (pf[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                fprintf(stderr, PROG ": %s poll error (revents=0x%x); reconnecting\n",
                        names[slot], (unsigned)pf[i].revents);
                close(fd);
                *slots[slot] = -1;
                continue;
            }
            if (!(pf[i].revents & POLLIN)) continue;

            struct input_event ev[32];
            ssize_t rd = read(fd, ev, sizeof(ev));
            // EOF (0) or a real error (not EAGAIN/EINTR) means the device
            // disappeared mid-read: drop it and reconnect.
            if (rd == 0 || (rd < 0 && errno != EAGAIN && errno != EINTR)) {
                fprintf(stderr, PROG ": %s read EOF/error; reconnecting\n", names[slot]);
                close(fd);
                *slots[slot] = -1;
                continue;
            }
            if (rd < 0) continue;   // EAGAIN / EINTR: nothing to do

            int count = (int)(rd / sizeof(ev[0]));
            int need_syn = 0;
            for (int k = 0; k < count; k++) {
                int t = ev[k].type, c = ev[k].code, v = ev[k].value;
                if (slot == 0 && t == EV_KEY &&
                    ((c >= BTN_MISC && c < KEY_OK) ||
                     (c >= BTN_DPAD_UP && c <= BTN_DPAD_RIGHT))) {
                    emit(ufd, EV_KEY, c, v);
                    need_syn = 1;
                } else if (slot == 1 && t == EV_ABS && (c == ABS_X || c == ABS_Y)) {
                    emit(ufd, EV_ABS, c, norm_axis(fd, c, v));
                    need_syn = 1;
                } else if (slot == 2 && t == EV_KEY && v == 1 &&
                           (c == KEY_VOLUMEUP || c == KEY_VOLUMEDOWN)) {
                    handle_volume(c);
                } else if (slot == 3 && t == EV_SW && c == SW_LID) {
                    // v=1: lid closed -> blank panels; v=0: open -> restore
                    set_backlight(v ? 0 : -1);
                }
            }
            if (need_syn) syn(ufd);
        }
    }

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    return 0;
}