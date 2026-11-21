// Host tests for motion tokens, easing, animator and the degrade ladder.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "motion.hpp"

#include <cmath>
#include <cstdio>

using namespace gameos::motion;

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

static void check_near(float got, float want, float tol, const char* what)
{
    if (std::fabs(got - want) <= tol) {
        g_pass++;
    } else {
        g_fail++;
        std::printf("FAIL %s: got %.3f want %.3f\n", what, got, want);
    }
}

static void test_easing()
{
    static const float enter[] = { 0.00f, 0.16f, 0.50f, 0.69f, 0.80f,
                                   0.88f, 0.93f, 0.96f, 0.98f, 1.00f, 1.00f };
    static const float exit[] = { 0.00f, 0.02f, 0.07f, 0.14f, 0.22f,
                                  0.33f, 0.44f, 0.56f, 0.70f, 0.84f, 1.00f };
    static const float move[] = { 0.00f, 0.03f, 0.16f, 0.38f, 0.60f,
                                  0.76f, 0.86f, 0.93f, 0.97f, 0.99f, 1.00f };
    static const float spring[] = { 0.00f, 0.34f, 0.60f, 0.79f, 0.92f,
                                    0.99f, 1.02f, 1.03f, 1.02f, 1.01f, 1.00f };
    static const float linear[] = { 0.00f, 0.10f, 0.20f, 0.30f, 0.40f,
                                    0.50f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f };

    struct Row {
        Ease e;
        const float* samples;
        const char* name;
    } rows[] = {
        { Ease::Enter, enter, "ease.enter" },
        { Ease::Exit, exit, "ease.exit" },
        { Ease::Move, move, "ease.move" },
        { Ease::Spring, spring, "ease.spring" },
        { Ease::Linear, linear, "linear" },
    };
    for (const Row& row : rows) {
        for (int i = 0; i <= 10; i++) {
            char what[64];
            std::snprintf(what, sizeof what, "%s @%d%%", row.name, i * 10);
            check_near(ease(row.e, (float)i / 10.0f), row.samples[i], 0.03f,
                       what);
        }
    }
    check_near(ease(Ease::Enter, -1.0f), 0.0f, 0.0f, "ease clamps low");
    check_near(ease(Ease::Spring, 2.0f), 1.0f, 0.0f, "ease clamps high");
    check(ease(Ease::Spring, 0.7f) > 1.0f, "spring overshoots");
}

static void test_anim()
{
    Anim a;
    check(!a.running(0), "idle anim not running");
    a.start(0.0f, 100.0f, 220, Ease::Linear, 1000);
    check(a.running(1000), "start is running");
    check_near(a.at(1000), 0.0f, 0.01f, "anim start value");
    check_near(a.at(1110), 50.0f, 0.01f, "anim midpoint");
    check_near(a.at(1220), 100.0f, 0.01f, "anim end value");
    check(!a.running(1220), "anim done at duration");
    check_near(a.at(1300), 100.0f, 0.01f, "anim holds end value");

    a.start(0.0f, 100.0f, 220, Ease::Linear, 2000);
    a.retarget(0.0f, 220, Ease::Linear, 2110);
    check_near(a.at(2110), 50.0f, 0.01f, "retarget starts at current value");
    check_near(a.at(2220), 25.0f, 0.01f, "retarget midpoint");
    check_near(a.at(2330), 0.0f, 0.01f, "retarget end");

    a.start(5.0f, -5.0f, 0, Ease::Enter, 3000);
    check(!a.running(3000), "zero duration is instant");
    check_near(a.at(3000), -5.0f, 0.0f, "zero duration snaps");

    a.start(0.0f, 1.0f, 180, Ease::Enter, 4000);
    a.stop(0.25f);
    check_near(a.at(4100), 0.25f, 0.0f, "stop sets value");

    a.start(0.0f, 100.0f, 100, Ease::Linear, 5000);
    check_near(a.at(4999), 0.0f, 0.01f, "before start holds from");
}

static void test_degrade()
{
    Degrade d;
    for (int i = 0; i < 29; i++) d.sample(3.0);
    check(d.level() == Degrade::kL0, "calm stays L0");

    for (int i = 0; i < 30; i++) d.sample(5.0);
    check(d.level() == Degrade::kL1, "first over-budget window -> L1");
    for (int i = 0; i < 30; i++) d.sample(5.0);
    check(d.level() == Degrade::kL2, "second over-budget window -> L2");
    for (int i = 0; i < 30; i++) d.sample(5.0);
    check(d.level() == Degrade::kL3, "third over-budget window -> L3");
    for (int i = 0; i < 60; i++) d.sample(9.0);
    check(d.level() == Degrade::kL3, "L3 is the floor");

    for (int i = 0; i < 800; i++) d.sample(1.0);
    check(d.level() == Degrade::kL0, "calm recovers down to L0");

    Degrade e;
    for (int i = 0; i < 30; i++) e.sample(3.0);
    e.sample(30.0);
    for (int i = 0; i < 29; i++) e.sample(3.0);
    check(e.level() == Degrade::kL0, "a single spike stays under P95");

    Degrade f;
    for (int i = 0; i < 200; i++) f.sample(3.99);
    check(f.level() == Degrade::kL0, "just under budget stays L0");
}

int main()
{
    test_easing();
    test_anim();
    test_degrade();
    std::printf("test-motion: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
