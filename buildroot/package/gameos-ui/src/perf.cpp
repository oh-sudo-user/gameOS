// gameOS front-end — UI frame profiler. See perf.hpp for the contract.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "perf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace gameos {
namespace perf {

namespace {

constexpr const char* kMarker = "/userdata/system/ui-perf.on";
constexpr const char* kLog = "/userdata/system/ui-perf.log";
constexpr long kRotateBytes = 256 * 1024;
constexpr int kMinFlushFrames = 60;   // shorter windows are noise, not data

bool g_checked = false;
bool g_on = false;
bool g_have_prev = false;
bool g_header_written = false;
int g_draw_this_frame = 0;
struct timespec g_frame_start = {};
struct timespec g_cpu_start = {};
struct timespec g_prev_start = {};
Window g_window;

double ms_between(const struct timespec& a, const struct timespec& b)
{
    return (a.tv_sec - b.tv_sec) * 1000.0 + (a.tv_nsec - b.tv_nsec) / 1e6;
}

void stamp(char* out, size_t cap)
{
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tm);
}

void rotate_if_needed()
{
    struct stat st;
    if (stat(kLog, &st) != 0 || st.st_size <= kRotateBytes)
        return;
    const std::string old = std::string(kLog) + ".1";
    unlink(old.c_str());
    rename(kLog, old.c_str());
    g_header_written = false;
}

void flush_window()
{
    if (g_window.n < kMinFlushFrames) {
        g_window.reset();
        return;
    }

    rotate_if_needed();
    FILE* f = fopen(kLog, "a");
    if (!f) {
        // /userdata not mounted or read-only: keep running, drop the window.
        g_window.reset();
        return;
    }
    if (!g_header_written) {
        fputs("# gameOS ui-perf (wall/cpu in ms on the UI thread; draws = SDL\n"
              "# draw ops/frame; dropped = frame period > 20 ms; one line per\n"
              "# ~600 frames or screen change; wall excludes the 60 Hz sleep)\n",
              f);
        g_header_written = true;
    }

    const Stats w = g_window.wall();
    const Stats c = g_window.cpu();
    const Stats d = g_window.draw();
    char t[32];
    stamp(t, sizeof t);
    fprintf(f,
            "t=%s screen=%s frames=%d wall p50=%.2f p95=%.2f p99=%.2f max=%.2f "
            "cpu p50=%.2f p95=%.2f p99=%.2f max=%.2f "
            "draws p50=%.0f p95=%.0f max=%.0f dropped=%d\n",
            t, g_window.screen ? g_window.screen : "?",
            g_window.n,
            w.p50, w.p95, w.p99, w.max,
            c.p50, c.p95, c.p99, c.max,
            d.p50, d.p95, d.max,
            g_window.dropped);
    fclose(f);
    g_window.reset();
}

} // namespace

Stats summarize(const double* samples, int n)
{
    Stats s;
    if (!samples || n <= 0)
        return s;

    std::vector<double> v(samples, samples + n);
    std::sort(v.begin(), v.end());
    s.n = n;
    s.max = v[n - 1];
    const auto pick = [&](double pct) {
        int rank = (int)std::ceil(pct / 100.0 * n);
        rank = std::max(1, std::min(rank, n));
        return v[rank - 1];
    };
    s.p50 = pick(50.0);
    s.p95 = pick(95.0);
    s.p99 = pick(99.0);
    return s;
}

void Window::add(double wall, double cpu, double period, int draws)
{
    if (n >= kCap)
        return;
    wall_ms[n] = wall;
    cpu_ms[n] = cpu;
    period_ms[n] = period;
    draw_ops[n] = draws;
    if (period > 20.0)
        dropped++;
    n++;
}

Stats Window::draw() const
{
    if (n <= 0)
        return Stats{};
    std::vector<double> v((size_t)n);
    for (int i = 0; i < n; i++)
        v[(size_t)i] = (double)draw_ops[i];
    return summarize(v.data(), n);
}

void Window::reset()
{
    n = 0;
    dropped = 0;
    screen = nullptr;
}

bool enabled()
{
    if (!g_checked) {
        g_checked = true;
        const char* e = getenv("GAMEOS_UI_PERF");
        g_on = (e && *e && strcmp(e, "0") != 0) || access(kMarker, F_OK) == 0;
    }
    return g_on;
}

void frame_begin()
{
    if (!enabled())
        return;
    clock_gettime(CLOCK_MONOTONIC, &g_frame_start);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &g_cpu_start);
    g_draw_this_frame = 0;
}

void note_draw(int count)
{
    if (g_on)
        g_draw_this_frame += count;
}

void frame_end(const char* screen)
{
    if (!g_on)
        return;

    struct timespec frame_end_ts, cpu_end;
    clock_gettime(CLOCK_MONOTONIC, &frame_end_ts);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_end);

    const double wall = ms_between(frame_end_ts, g_frame_start);
    const double cpu = ms_between(cpu_end, g_cpu_start);
    const double period = g_have_prev ? ms_between(g_frame_start, g_prev_start) : wall;
    g_prev_start = g_frame_start;
    g_have_prev = true;

    if (!screen)
        screen = "?";

    // Attribute every line to one screen. A quick tab switch produces a
    // short burst; discard those rather than logging a meaningless window.
    if (g_window.screen && strcmp(g_window.screen, screen) != 0) {
        if (g_window.n >= kMinFlushFrames)
            flush_window();
        else
            g_window.reset();
        g_window.screen = screen;
    } else if (!g_window.screen) {
        g_window.screen = screen;
    }

    g_window.add(wall, cpu, period, g_draw_this_frame);
    if (g_window.full())
        flush_window();
}

void shutdown()
{
    if (g_on)
        flush_window();
}

} // namespace perf
} // namespace gameos
