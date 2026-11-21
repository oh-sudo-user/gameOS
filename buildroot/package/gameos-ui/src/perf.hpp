// gameOS front-end — UI frame profiler (opt-in, no SDL dependency).
//
// The roadmap's M1 work needs a baseline before any renderer change: how
// long one frame takes on the UI thread, how much of it is CPU, how many
// draw operations are submitted, and how often a frame misses the 60 Hz
// cadence. This module records exactly those numbers in fixed-size windows
// and appends one line per window to <log dir>/ui-perf.log.
//
// Off by default. Enable with an empty file at /userdata/system/ui-perf.on
// (the card's system folder) or with GAMEOS_UI_PERF=1 in the environment.
// When off, the cost is one cached bool check per call — no file IO, no
// clocks, no allocations.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace gameos {
namespace perf {

struct Stats {
    int n = 0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

// Nearest-rank percentiles over a sorted copy of the samples. Null or an
// empty sample set returns an all-zero Stats, so callers never need to
// special-case the first window. Kept free of SDL so the host test can
// exercise it without a display.
Stats summarize(const double* samples, int n);

// Rolling window of frame samples, one entry per frame. kCap is about ten
// seconds at 60 Hz; a screen change flushes earlier so every line in the
// log can be attributed to a single screen.
struct Window {
    static constexpr int kCap = 600;
    double wall_ms[kCap] = {};    // event handling + draw + present
    double cpu_ms[kCap] = {};     // CLOCK_THREAD_CPUTIME_ID delta
    double period_ms[kCap] = {};  // start-to-start, includes the 60 Hz sleep
    int draw_ops[kCap] = {};      // SDL draw calls submitted for this frame
    int n = 0;
    int dropped = 0;              // periods above 20 ms (~one missed vblank)
    int dirty_rec = 0;            // command-buffer recordings
    int dirty_full = 0;           // recordings that repainted everything
    int dirty_skip = 0;           // identical frames not drawn at all
    double dirty_area = 0.0;      // summed damaged fraction (skips excluded)
    const char* screen = nullptr;

    void add(double wall, double cpu, double period, int draws);
    bool full() const { return n >= kCap; }
    Stats wall() const { return summarize(wall_ms, n); }
    Stats cpu() const { return summarize(cpu_ms, n); }
    Stats draw() const;
    void reset();
};

bool enabled();
void frame_begin();
void note_draw(int count = 1);
// Called once per command-buffer recording (top panel and deck separately).
// area is the damaged fraction of the screen, 0 for an identical-frame skip.
void note_dirty(double area, bool full, bool skipped);
void frame_end(const char* screen);
void shutdown();

} // namespace perf
} // namespace gameos
