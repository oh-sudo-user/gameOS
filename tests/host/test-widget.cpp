// Host tests for widget layout, hit-testing and toast timing.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "widget.hpp"

#include <cmath>
#include <cstdio>

using namespace gameos::widget;

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

static void test_rows()
{
    SDL_Rect area = { 0, 100, 500, 300 };
    SDL_Rect r = row_rect(area, 2, 20);
    check(r.y == 192 && r.h == gameos::token::row_list && r.w == 500,
          "row rect follows scroll and token height");

    check(row_at(area, 20, 192, 3) == 2, "row hit test lands on row 2");
    check(row_at(area, 20, 99, 3) == -1, "row hit above area");
    check(row_at(area, 20, 400, 3) == -1, "row hit at area bottom");
    check(row_at(area, 20, 300, 3) == -1, "row index past count");
    check(row_at(area, 1000, 150, 3) == -1, "scrolled past count");
    check(row_at(area, 0, 150, 0) == -1, "empty list has no rows");
}

static void test_switch_slider()
{
    SDL_Rect sw = { 10, 20, 48, 28 };
    check(switch_knob_x(sw, 0.0f) == 12, "switch knob off");
    check(switch_knob_x(sw, 1.0f) == 32, "switch knob on");
    check(switch_knob_x(sw, 0.5f) == 22, "switch knob midway");
    check(switch_knob_x(sw, -1.0f) == 12, "switch knob clamps low");
    check(switch_knob_x(sw, 2.0f) == 32, "switch knob clamps high");

    SDL_Rect sl = { 0, 0, 200, 40 };
    check(slider_knob_x(sl, 0.0f) == 14, "slider knob start");
    check(slider_knob_x(sl, 1.0f) == 186, "slider knob end");
    check_near(slider_value_at(sl, 0), 0.0f, 0.001f, "slider value left");
    check_near(slider_value_at(sl, 100), 0.5f, 0.001f, "slider value middle");
    check_near(slider_value_at(sl, 260), 1.0f, 0.001f, "slider value clamps");
    check_near(slider_value_at(sl, -20), 0.0f, 0.001f, "slider value clamps low");
}

static void test_dialog_layout()
{
    SDL_Rect screen = { 0, 0, 1024, 768 };
    SDL_Rect p = dialog_panel_rect(screen, 640, 340);
    check(p.x == 192 && p.y == 214 && p.w == 640 && p.h == 340,
          "dialog panel centered");
    SDL_Rect wide = dialog_panel_rect(screen, 2000, 340);
    check(wide.w == 976 && wide.x == 24, "dialog panel clamps width");
    SDL_Rect narrow = dialog_panel_rect(screen, 0, 900);
    check(narrow.w == 0 && narrow.h == 720, "dialog panel clamps height");

    auto rects = dialog_button_rects(p, 2);
    check(rects.size() == 2, "dialog button count");
    check(rects[0].w == 132 && rects[1].w == 132, "dialog button width");
    check(rects[0].x == 532 && rects[1].x == 676, "dialog buttons right-aligned");
    check(rects[0].y == 482 && rects[0].h == gameos::token::button_h,
          "dialog button baseline");
    check(dialog_button_rects(p, 0).empty(), "no dialog buttons");
}

static void test_toast()
{
    ToastQueue q;
    check(!q.active(0), "empty toast inactive");
    check_near(q.alpha(0), 0.0f, 0.0f, "empty toast alpha");

    q.show("saved", ToastKind::Success, 1000, 2000);
    check(q.active(1000), "toast active at start");
    check_near(q.alpha(1000), 0.0f, 0.001f, "toast fades in from zero");
    check_near(q.alpha(1060), 0.5f, 0.01f, "toast half fade-in");
    check_near(q.alpha(2000), 1.0f, 0.001f, "toast fully visible");
    check_near(q.alpha(2920), 0.5f, 0.01f, "toast half fade-out");
    check(!q.active(3000), "toast expires at deadline");
    check_near(q.alpha(3100), 0.0f, 0.001f, "expired toast invisible");

    q.show("wifi failed", ToastKind::Danger, 5000, 500);
    check(q.current().text == "wifi failed", "new toast replaces text");
    check(q.current().kind == ToastKind::Danger, "new toast replaces kind");
    check(q.active(5499), "replacement resets timer");
    check(!q.active(5500), "replacement expires on its own timer");

    q.show("x", ToastKind::Info, 6000, 0);
    check(q.current().dur_ms == 1, "zero duration is clamped");
    q.clear();
    check(!q.active(6000), "clear hides toast");
}

int main()
{
    test_rows();
    test_switch_slider();
    test_dialog_layout();
    test_toast();
    std::printf("test-widget: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
