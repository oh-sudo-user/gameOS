// Host tests for the UI frame profiler's pure math (perf.cpp needs no SDL
// and no display). The file I/O side is exercised on-device: create
// /userdata/system/ui-perf.on, play with the UI, read ui-perf.log.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "perf.hpp"

#include <cstdio>

using gameos::perf::Stats;
using gameos::perf::Window;

static int g_pass = 0;
static int g_fail = 0;

static void ok(const char* name) { g_pass++; std::printf("ok   %s\n", name); }
static void no(const char* name)
{
    g_fail++;
    std::printf("FAIL %s\n", name);
}

static bool near(double a, double b) { return (a - b) < 1e-9 && (b - a) < 1e-9; }

static void check_stats(const char* name, const Stats& s, int n,
                        double p50, double p95, double p99, double max)
{
    if (s.n == n && near(s.p50, p50) && near(s.p95, p95) &&
        near(s.p99, p99) && near(s.max, max))
        ok(name);
    else
        no(name);
}

int main()
{
    // ---- summarize ------------------------------------------------------
    {
        Stats s = gameos::perf::summarize(nullptr, 0);
        check_stats("summarize(null) is all zero", s, 0, 0, 0, 0, 0);
    }
    {
        double one[1] = { 7.5 };
        check_stats("summarize(single)", gameos::perf::summarize(one, 1),
                    1, 7.5, 7.5, 7.5, 7.5);
    }
    {
        // Nearest rank: p50 -> ceil(0.50*2)=1 -> smallest; p95 -> 2 -> largest.
        double two[2] = { 20, 10 };
        check_stats("summarize(two, nearest rank)",
                    gameos::perf::summarize(two, 2), 2, 10, 20, 20, 20);
    }
    {
        double v[100];
        for (int i = 0; i < 100; i++) v[i] = (double)(i + 1);  // 1..100
        check_stats("summarize(1..100)",
                    gameos::perf::summarize(v, 100), 100, 50, 95, 99, 100);
    }

    // ---- Window ---------------------------------------------------------
    {
        Window w;
        for (int i = 0; i < Window::kCap; i++)
            w.add(10.0, 5.0, (i % 100 == 0) ? 25.0 : 16.0, 40);
        if (w.full() && w.n == Window::kCap)
            ok("window fills to kCap");
        else
            no("window fills to kCap");

        // Exactly 20.0 ms is not a drop; only period > 20 ms is.
        const int expected_drops = Window::kCap / 100;  // i=0,100,...,500
        if (w.dropped == expected_drops)
            ok("window counts dropped frames");
        else {
            std::printf("     dropped=%d expected=%d\n", w.dropped, expected_drops);
            no("window counts dropped frames");
        }

        check_stats("window wall stats", w.wall(), Window::kCap, 10, 10, 10, 10);
        check_stats("window cpu stats", w.cpu(), Window::kCap, 5, 5, 5, 5);
        check_stats("window draw stats", w.draw(), Window::kCap, 40, 40, 40, 40);

        w.add(999.0, 999.0, 999.0, 999);  // past the cap: ignored
        if (w.n == Window::kCap && near(w.wall().max, 10.0))
            ok("window ignores samples past kCap");
        else
            no("window ignores samples past kCap");

        w.reset();
        if (w.n == 0 && w.dropped == 0 && w.screen == nullptr)
            ok("window reset clears state");
        else
            no("window reset clears state");
    }
    {
        Window w;
        w.add(1, 1, 16, 10);
        w.add(2, 2, 21, 30);
        Stats d = w.draw();
        check_stats("window draws p50/p95/max", d, 2, 10, 30, 30, 30);
    }

    // ---- off by default, and the off path is a no-op --------------------
    {
        // No marker and no env in the test environment: frame_end must not
        // create /userdata (which does not exist on a host).
        if (!gameos::perf::enabled())
            ok("profiler disabled without marker/env");
        else
            no("profiler disabled without marker/env");
        gameos::perf::frame_begin();
        gameos::perf::note_draw();
        gameos::perf::frame_end("home");
        gameos::perf::shutdown();
        ok("off path runs without side effects");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
