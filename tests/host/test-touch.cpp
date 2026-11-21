// Host tests for touch gesture constants and transition gating.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch.hpp"

#include <cstdio>

using namespace gameos;

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what)
{
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        std::printf("FAIL %s\n", what);
    }
}

int main()
{
    check(kTouchSlop == 12.0f, "touch slop follows the 12 DP spec");

    check(!touch_blocked(999, 1000, 220), "touch before a transition starts");
    check(touch_blocked(1000, 1000, 220), "touch at the transition start");
    check(touch_blocked(1219, 1000, 220), "touch on the last blocked millisecond");
    check(!touch_blocked(1220, 1000, 220), "touch at the transition end");
    check(!touch_blocked(5000, 0, 220), "stale boot timestamp does not lock touch");
    check(!touch_blocked(0, 50, 220), "timestamp before the start is not blocked");
    check(touch_blocked(100, 0, 220), "boot transition locks the first frames");
    check(!touch_blocked(1000, 1000, 0), "zero duration never locks");

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
