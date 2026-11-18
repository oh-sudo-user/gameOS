// gameos-sleepd — suspend-to-RAM when the lid closes.
//
// The order matters on this SoC: pause the emulator's
// process group first (a dynarec must not run while clocks drop), run the
// pre-sleep hooks (Wi-Fi driver out — rtw88 never resumes otherwise), sync,
// then echo mem. On wake: panels on, game continued, post hooks in the
// background. While charging there is no suspend (the PWM charging LED
// would die): dark and awake until the lid opens or the charger is pulled.
// A failed suspend falls back deep -> serial device suspend -> s2idle, and
// finally stays dark instead of retrying in a bag. An RTC alarm wakes every
// few hours to shut down cleanly before the battery runs flat asleep.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define GAME_PID   "/run/gameos/game.pid"
#define HOOK_DIR   "/etc/gameos/sleep.d"
#define POLL_MS    300
#define CHECK_SEC  (6 * 3600)
#define OFF_PCT    5

static int lid_fd = -1;
static int pwr_fd = -1;

// evdev bitmaps are arrays: KEY_POWER (116) does not fit one long
static int bit_set(const unsigned long* a, int nr)
{
    return (a[nr / (8 * sizeof(long))] >> (nr % (8 * sizeof(long)))) & 1;
}

static void read_str(const char* path, char* buf, size_t n)
{
    buf[0] = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    ssize_t r = read(fd, buf, n - 1);
    if (r > 0) buf[r] = 0;
    close(fd);
    buf[strcspn(buf, "\n")] = 0;
}

static int write_file(const char* path, const char* val)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t r = write(fd, val, strlen(val));
    int e = errno;
    close(fd);
    errno = e;
    return r < 0 ? -1 : 0;
}

// EVIOCGSW reads the switch state as the kernel has it now, so a lid edge
// that went by while hooks ran is never missed.
static int lid_closed(void)
{
    if (lid_fd < 0) return 0;
    unsigned long sw[4] = { 0 };
    if (ioctl(lid_fd, EVIOCGSW(sizeof sw), sw) < 0) return 0;
    return bit_set(sw, SW_LID);
}

static int power_released(void)
{
    if (pwr_fd < 0) return 0;
    struct input_event ev;
    int got = 0;
    while (read(pwr_fd, &ev, sizeof ev) == (ssize_t)sizeof ev)
        if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 0) got = 1;
    return got;
}

static void find_devices(void)
{
    DIR* d = opendir("/dev/input");
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[320];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long sw[4] = { 0 }, key[16] = { 0 };
        if (lid_fd < 0 && ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw) >= 0
            && bit_set(sw, SW_LID)) {
            lid_fd = fd;
            continue;
        }
        if (pwr_fd < 0 && ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key), key) >= 0
            && bit_set(key, KEY_POWER)) {
            pwr_fd = fd;
            continue;
        }
        close(fd);
    }
    closedir(d);
}

static int supply_is(const char* base, const char* type)
{
    char p[384], t[32];
    snprintf(p, sizeof p, "%s/type", base);
    read_str(p, t, sizeof t);
    return strcmp(t, type) == 0;
}

static int charger_online(void)
{
    DIR* d = opendir("/sys/class/power_supply");
    if (!d) return 0;
    struct dirent* e;
    int on = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char base[320], p[384], v[16];
        snprintf(base, sizeof base, "/sys/class/power_supply/%s", e->d_name);
        if (!supply_is(base, "Mains") && !supply_is(base, "USB")) continue;
        snprintf(p, sizeof p, "%s/online", base);
        read_str(p, v, sizeof v);
        if (strcmp(v, "1") == 0) on = 1;
    }
    closedir(d);
    return on;
}

static int battery_pct(void)
{
    DIR* d = opendir("/sys/class/power_supply");
    if (!d) return -1;
    struct dirent* e;
    int pct = -1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char base[320], p[384], v[16];
        snprintf(base, sizeof base, "/sys/class/power_supply/%s", e->d_name);
        if (!supply_is(base, "Battery")) continue;
        snprintf(p, sizeof p, "%s/capacity", base);
        read_str(p, v, sizeof v);
        pct = atoi(v);
        break;
    }
    closedir(d);
    return pct;
}

static pid_t game_pgid(void)
{
    char v[32];
    read_str(GAME_PID, v, sizeof v);
    pid_t p = atoi(v);
    if (p <= 0 || kill(p, 0) != 0) return 0;
    pid_t pg = getpgid(p);
    return pg > 0 ? pg : 0;
}

static void backlights(int on)
{
    DIR* d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[320];
        snprintf(p, sizeof p, "/sys/class/backlight/%s/bl_power", e->d_name);
        write_file(p, on ? "0" : "4");
    }
    closedir(d);
}

static void hooks(const char* phase, int background)
{
    struct dirent** list = NULL;
    int n = scandir(HOOK_DIR, &list, NULL, alphasort);
    for (int i = 0; i < n; i++) {
        char path[320];
        snprintf(path, sizeof path, "%s/%s", HOOK_DIR, list[i]->d_name);
        struct stat st;
        if (list[i]->d_name[0] == '.' || stat(path, &st) < 0
            || !S_ISREG(st.st_mode) || access(path, X_OK) != 0) {
            free(list[i]);
            continue;
        }
        pid_t p = fork();
        if (p == 0) {
            if (background) {
                // orphan the hook so init reaps it, not this daemon
                pid_t g = fork();
                if (g == 0) {
                    execl(path, path, phase, (char*)NULL);
                    _exit(127);
                }
                _exit(0);
            }
            execl(path, path, phase, (char*)NULL);
            _exit(127);
        }
        if (p > 0) waitpid(p, NULL, 0);   // background: only the short-lived middle child
        free(list[i]);
    }
    free(list);
}

static void sleep_mode(int fallback)
{
    char modes[128];
    read_str("/sys/power/mem_sleep", modes, sizeof modes);
    const char* want = fallback >= 2 ? "s2idle" : "deep";
    char chosen[32];
    snprintf(chosen, sizeof chosen, "[%s]", want);
    if (strstr(modes, want) && !strstr(modes, chosen))
        write_file("/sys/power/mem_sleep", want);
    // serial device suspend: the xhci controller on this SoC fails the
    // parallel path now and then ("Clearing Run/Stop bit failed")
    write_file("/sys/power/pm_async", "0");
}

static void rtc_alarm(int seconds)
{
    write_file("/sys/class/rtc/rtc0/wakealarm", "0");
    if (seconds <= 0) return;
    char v[32];
    snprintf(v, sizeof v, "+%d", seconds);
    write_file("/sys/class/rtc/rtc0/wakealarm", v);
}

static void suspend_now(void)
{
    pid_t pg = game_pgid();
    if (pg > 0) kill(-pg, SIGSTOP);
    backlights(0);

    int fallback = 0, failures = 0;
    for (;;) {
        // charging: suspend would kill the PWM charging LED, so stay dark
        // and awake (game paused) until the lid opens or power is pressed
        while (charger_online()) {
            if (!lid_closed() || power_released()) goto wake;
            usleep(POLL_MS * 1000);
        }
        hooks("pre", 0);
        sync();
        sleep_mode(fallback);
        if (!lid_closed()) goto wake;   // opened again while hooks ran
        rtc_alarm(CHECK_SEC);
        time_t alarm_at = time(NULL) + CHECK_SEC;
        int r = write_file("/sys/power/state", "mem");
        // read before clearing: a fired alarm reads empty, a lid wake
        // still shows the pending epoch — clearing first would make every
        // wake look like the battery check and sleep again with the lid open
        char pending[32];
        read_str("/sys/class/rtc/rtc0/wakealarm", pending, sizeof pending);
        rtc_alarm(0);
        if (r != 0) {
            failures++;
            if (failures <= 2 && fallback < 2) {
                fallback++;
                usleep(200000);
                continue;
            }
            // cannot suspend at all: dark until the lid opens, no retry
            // loop with the lid shut in a bag
            while (lid_closed() && !power_released()) usleep(POLL_MS * 1000);
            goto wake;
        }
        failures = 0;
        fallback = 0;
        if (pending[0] == 0 || time(NULL) >= alarm_at) {
            int pct = battery_pct();
            if (pct >= 0 && pct <= OFF_PCT) {
                if (pg > 0) {
                    kill(-pg, SIGCONT);   // let the emulator write its saves
                    kill(-pg, SIGTERM);
                }
                sync();
                if (system("poweroff") != 0) { /* init handles it */ }
                exit(0);
            }
            continue;   // battery fine: straight back to sleep
        }
        if (lid_closed()) continue;   // power-button wake with lid shut
        break;
    }
wake:
    backlights(!lid_closed());
    if (pg > 0) kill(-pg, SIGCONT);
    hooks("post", 1);
}

int main(void)
{
    int last = 0;
    for (;;) {
        if (lid_fd < 0 || pwr_fd < 0) find_devices();
        int cur = lid_closed();
        if (cur && !last) suspend_now();
        else if (!cur && last) backlights(1);   // lid open: panels back on
        last = lid_closed();
        usleep(POLL_MS * 1000);
    }
    return 0;
}