// Host tests for the UI command buffer's pure damage math (drawcall.cpp).
// The recorder itself needs a renderer; the pixel harness exercises that.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawcall.hpp"

#include <cstdio>
#include <cstring>

using namespace gameos::drawcall;

static int g_pass = 0;
static int g_fail = 0;

static void ok(const char* name) { g_pass++; std::printf("ok   %s\n", name); }
static void no(const char* name)
{
    g_fail++;
    std::printf("FAIL %s\n", name);
}

static bool eq(const SDL_Rect& a, int x, int y, int w, int h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

static void check_merge(const char* name, std::vector<SDL_Rect> in,
                        int w, int h, size_t want_n, bool want_full,
                        const SDL_Rect* want0 = nullptr)
{
    bool full = false;
    auto out = merge_rects(std::move(in), w, h, 4, 0.55, &full);
    if (out.size() != want_n || full != want_full ||
        (want0 && !rects_equal(out[0], *want0))) {
        no(name);
        std::printf("     got n=%zu full=%d [%d,%d %dx%d]\n",
                    out.size(), (int)full,
                    out.empty() ? 0 : out[0].x, out.empty() ? 0 : out[0].y,
                    out.empty() ? 0 : out[0].w, out.empty() ? 0 : out[0].h);
    } else {
        ok(name);
    }
}

int main()
{
    // ---- rectangle intersection ----------------------------------------
    {
        SDL_Rect r = intersect_rect({ 0, 0, 10, 10 }, { 5, 5, 10, 10 });
        if (eq(r, 5, 5, 5, 5)) ok("intersect overlaps");
        else no("intersect overlaps");
    }
    {
        SDL_Rect r = intersect_rect({ 0, 0, 10, 10 }, { 10, 0, 5, 5 });
        if (r.w == 0 && r.h == 0) ok("intersect touching is empty");
        else no("intersect touching is empty");
    }
    {
        SDL_Rect r = intersect_rect({ -5, -5, 10, 10 }, { 0, 0, 100, 100 });
        if (eq(r, 0, 0, 5, 5)) ok("intersect clamps");
        else no("intersect clamps");
    }

    // ---- merging ---------------------------------------------------------
    {
        SDL_Rect want{ 0, 0, 15, 15 };
        check_merge("overlapping rects merge", { { 0, 0, 10, 10 }, { 5, 5, 10, 10 } },
                    100, 100, 1, false, &want);
    }
    {
        SDL_Rect want{ 0, 0, 10, 10 };
        check_merge("duplicate rects collapse", { { 0, 0, 10, 10 }, { 0, 0, 10, 10 } },
                    100, 100, 1, false, &want);
    }
    check_merge("disjoint rects stay separate",
                { { 0, 0, 10, 10 }, { 50, 50, 10, 10 } }, 100, 100, 2, false);
    check_merge("offscreen damage is clipped",
                { { -20, -20, 10, 10 } }, 100, 100, 0, false);
    {
        SDL_Rect want{ 90, 90, 10, 10 };
        check_merge("offscreen tail is clipped",
                    { { 90, 90, 30, 30 } }, 100, 100, 1, false, &want);
    }
    {
        bool full = false;
        auto out = merge_rects({ { 0, 0, 100, 100 } }, 100, 100, 4, 0.55, &full);
        if (full && out.size() == 1 && eq(out[0], 0, 0, 100, 100))
            ok("area above threshold forces full");
        else
            no("area above threshold forces full");
    }
    {
        // Four small corners are under the area threshold and fit the limit.
        check_merge("four corners stay four rects",
                    { { 0, 0, 10, 10 }, { 90, 0, 10, 10 },
                      { 0, 90, 10, 10 }, { 90, 90, 10, 10 } },
                    100, 100, 4, false);
    }
    {
        // Five scattered rects exceed the rect budget; their bounding box is
        // still cheap, so it is used instead of a full repaint.
        SDL_Rect want{ 0, 0, 50, 50 };
        check_merge("too many rects collapse to a bounding box",
                    { { 0, 0, 10, 10 }, { 20, 0, 10, 10 }, { 40, 0, 10, 10 },
                      { 0, 20, 10, 10 }, { 20, 40, 10, 10 } },
                    100, 100, 1, false, &want);
    }

    // ---- geometry hashing ------------------------------------------------
    {
        SDL_Vertex v[3] = {
            { { 0, 0 }, { 255, 0, 0, 255 }, { 0, 0 } },
            { { 10, 0 }, { 0, 255, 0, 255 }, { 0, 0 } },
            { { 0, 10 }, { 0, 0, 255, 255 }, { 0, 0 } },
        };
        int idx[3] = { 0, 1, 2 };
        uint64_t a = hash_geometry(v, 3, idx, 3);
        uint64_t b = hash_geometry(v, 3, idx, 3);
        if (a == b) ok("geometry hash is stable");
        else no("geometry hash is stable");

        SDL_Vertex moved[3];
        memcpy(moved, v, sizeof v);
        moved[2].position.x += 1;
        if (hash_geometry(moved, 3, idx, 3) != a) ok("geometry hash sees position");
        else no("geometry hash sees position");

        SDL_Vertex recolored[3];
        memcpy(recolored, v, sizeof v);
        recolored[0].color.g = 1;
        if (hash_geometry(recolored, 3, idx, 3) != a) ok("geometry hash sees colour");
        else no("geometry hash sees colour");

        int flipped[3] = { 2, 1, 0 };
        if (hash_geometry(v, 3, flipped, 3) != a) ok("geometry hash sees index order");
        else no("geometry hash sees index order");
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
