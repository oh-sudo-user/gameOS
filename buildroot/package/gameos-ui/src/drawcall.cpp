// gameOS front-end — frame command buffer and damage tracking.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawcall.hpp"

#include "perf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>

namespace gameos {
namespace drawcall {

namespace {

constexpr int kMaxRects = 4;
constexpr size_t kMaxDamage = 256;
constexpr double kMaxArea = 0.55;
constexpr int kMergePad = 2;

struct State {
    SDL_Texture* target = nullptr;
    int w = 0;
    int h = 0;
    bool recording = false;
    bool have_shown = false;
    bool force_full = true;
    bool clip_on = false;
    SDL_Rect clip_rc{};
    SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
    Frame shown;
    Frame next;
    std::vector<SDL_Rect> dirty;
    Result res;
};

std::map<SDL_Renderer*, State>& states()
{
    static std::map<SDL_Renderer*, State> m;
    return m;
}

std::vector<SDL_Texture*>& graveyard()
{
    static std::vector<SDL_Texture*> v;
    return v;
}

int g_recording = 0;

State* find_state(SDL_Renderer* r)
{
    if (!r) return nullptr;
    auto it = states().find(r);
    return it == states().end() ? nullptr : &it->second;
}

State* rec_state(SDL_Renderer* r)
{
    if (g_recording == 0) return nullptr;
    State* s = find_state(r);
    return s && s->recording ? s : nullptr;
}

SDL_Rect screen_rect(int w, int h)
{
    return SDL_Rect{ 0, 0, w, h };
}

SDL_Rect clip_bounds(const State& s, SDL_Rect rc)
{
    rc = intersect_rect(rc, screen_rect(s.w, s.h));
    if (s.clip_on) rc = intersect_rect(rc, s.clip_rc);
    return rc;
}

uint64_t hash_u32(uint64_t h, Uint32 v)
{
    h ^= v;
    h *= 1099511628211ULL;
    return h;
}

uint64_t hash_float(uint64_t h, float f)
{
    Uint32 v = 0;
    memcpy(&v, &f, sizeof v);
    return hash_u32(h, v);
}

SDL_Rect geom_bounds(const SDL_Vertex* v, int n, const State& s)
{
    if (!v || n <= 0) return SDL_Rect{ 0, 0, 0, 0 };
    float x0 = v[0].position.x, x1 = x0;
    float y0 = v[0].position.y, y1 = y0;
    for (int i = 1; i < n; i++) {
        x0 = std::min(x0, v[i].position.x);
        x1 = std::max(x1, v[i].position.x);
        y0 = std::min(y0, v[i].position.y);
        y1 = std::max(y1, v[i].position.y);
    }
    int ix = (int)std::floor(x0) - 1;
    int iy = (int)std::floor(y0) - 1;
    int iw = (int)std::ceil(x1) - ix + 1;
    int ih = (int)std::ceil(y1) - iy + 1;
    return clip_bounds(s, SDL_Rect{ ix, iy, iw, ih });
}

bool cmd_same(const Frame& a, const Cmd& x, const Frame& b, const Cmd& y)
{
    if (x.kind != y.kind || x.alpha != y.alpha || x.tex != y.tex ||
        x.has_src != y.has_src || x.has_clip != y.has_clip ||
        x.blend != y.blend)
        return false;
    if (!rects_equal(x.dst, y.dst)) return false;
    if (x.has_src && !rects_equal(x.src, y.src)) return false;
    if (x.has_clip && !rects_equal(x.clip, y.clip)) return false;
    if (x.kind == Cmd::Kind::Fill)
        return x.color.r == y.color.r && x.color.g == y.color.g &&
               x.color.b == y.color.b && x.color.a == y.color.a;
    if (x.kind == Cmd::Kind::Geom) {
        if (x.sig != y.sig || x.v_n != y.v_n || x.i_n != y.i_n) return false;
        if (x.v_n && memcmp(&a.verts[x.v_off], &b.verts[y.v_off],
                            x.v_n * sizeof(SDL_Vertex)) != 0)
            return false;
        if (x.i_n && memcmp(&a.idx[x.i_off], &b.idx[y.i_off],
                            x.i_n * sizeof(int)) != 0)
            return false;
    }
    return true;
}

void draw_cmd(SDL_Renderer* r, const Frame& f, const Cmd& c)
{
    switch (c.kind) {
    case Cmd::Kind::Fill:
        SDL_SetRenderDrawBlendMode(r, c.blend);
        SDL_SetRenderDrawColor(r, c.color.r, c.color.g, c.color.b, c.color.a);
        SDL_RenderFillRect(r, &c.dst);
        break;
    case Cmd::Kind::Tex:
        if (!c.tex) return;
        SDL_SetTextureAlphaMod(c.tex, c.alpha);
        SDL_SetTextureBlendMode(c.tex, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(r, c.tex, c.has_src ? &c.src : nullptr, &c.dst);
        break;
    case Cmd::Kind::Geom:
        SDL_SetRenderDrawBlendMode(r, c.blend);
        SDL_RenderGeometry(r, c.tex,
                           &f.verts[c.v_off], (int)c.v_n,
                           c.i_n ? &f.idx[c.i_off] : nullptr, (int)c.i_n);
        break;
    }
}

} // namespace

SDL_Rect intersect_rect(const SDL_Rect& a, const SDL_Rect& b)
{
    int x0 = std::max(a.x, b.x);
    int y0 = std::max(a.y, b.y);
    int x1 = std::min(a.x + a.w, b.x + b.w);
    int y1 = std::min(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return SDL_Rect{ 0, 0, 0, 0 };
    return SDL_Rect{ x0, y0, x1 - x0, y1 - y0 };
}

bool rects_equal(const SDL_Rect& a, const SDL_Rect& b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

std::vector<SDL_Rect> merge_rects(std::vector<SDL_Rect> in, int w, int h,
                                  int max_rects, double max_area, bool* full)
{
    if (full) *full = false;
    const SDL_Rect screen = screen_rect(w, h);
    std::vector<SDL_Rect> rects;
    rects.reserve(in.size());
    for (SDL_Rect rc : in) {
        rc = intersect_rect(rc, screen);
        if (rc.w <= 0 || rc.h <= 0) continue;
        bool dup = false;
        for (const SDL_Rect& q : rects)
            if (rects_equal(q, rc)) { dup = true; break; }
        if (!dup) rects.push_back(rc);
    }
    if (rects.empty()) return {};
    const double panel = (double)w * (double)h;
    if (panel <= 0) {
        if (full) *full = true;
        return { screen };
    }
    if (rects.size() > kMaxDamage) {
        if (full) *full = true;
        return { screen };
    }

    std::vector<int> parent(rects.size());
    for (size_t i = 0; i < rects.size(); i++) parent[i] = (int)i;
    auto find = [&](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    for (size_t i = 0; i < rects.size(); i++) {
        for (size_t j = i + 1; j < rects.size(); j++) {
            SDL_Rect pad = { rects[i].x - kMergePad, rects[i].y - kMergePad,
                             rects[i].w + kMergePad * 2,
                             rects[i].h + kMergePad * 2 };
            SDL_Rect hit = intersect_rect(pad, rects[j]);
            if (hit.w > 0 && hit.h > 0) parent[find((int)i)] = find((int)j);
        }
    }

    std::vector<SDL_Rect> comps;
    std::vector<int> owner(rects.size(), -1);
    for (size_t i = 0; i < rects.size(); i++) {
        int root = find((int)i);
        if (owner[root] < 0) {
            owner[root] = (int)comps.size();
            comps.push_back(rects[i]);
        } else {
            SDL_Rect& c = comps[owner[root]];
            int x0 = std::min(c.x, rects[i].x);
            int y0 = std::min(c.y, rects[i].y);
            int x1 = std::max(c.x + c.w, rects[i].x + rects[i].w);
            int y1 = std::max(c.y + c.h, rects[i].y + rects[i].h);
            c = SDL_Rect{ x0, y0, x1 - x0, y1 - y0 };
        }
    }

    if ((int)comps.size() > max_rects) {
        SDL_Rect all = comps[0];
        for (size_t i = 1; i < comps.size(); i++) {
            int x0 = std::min(all.x, comps[i].x);
            int y0 = std::min(all.y, comps[i].y);
            int x1 = std::max(all.x + all.w, comps[i].x + comps[i].w);
            int y1 = std::max(all.y + all.h, comps[i].y + comps[i].h);
            all = SDL_Rect{ x0, y0, x1 - x0, y1 - y0 };
        }
        if ((double)all.w * all.h > max_area * panel) {
            if (full) *full = true;
            return { screen };
        }
        return { all };
    }

    long long area = 0;
    for (const SDL_Rect& c : comps) area += (long long)c.w * c.h;
    if ((double)area > max_area * panel) {
        if (full) *full = true;
        return { screen };
    }
    return comps;
}

uint64_t hash_geometry(const SDL_Vertex* v, int n, const int* idx, int m)
{
    uint64_t h = 1469598103934665603ULL;
    h = hash_u32(h, (Uint32)n);
    h = hash_u32(h, (Uint32)m);
    for (int i = 0; i < n; i++) {
        h = hash_float(h, v[i].position.x);
        h = hash_float(h, v[i].position.y);
        h = hash_u32(h, ((Uint32)v[i].color.a << 24) |
                        ((Uint32)v[i].color.b << 16) |
                        ((Uint32)v[i].color.g << 8) |
                        (Uint32)v[i].color.r);
        h = hash_float(h, v[i].tex_coord.x);
        h = hash_float(h, v[i].tex_coord.y);
    }
    for (int i = 0; i < m; i++) h = hash_u32(h, (Uint32)idx[i]);
    return h;
}

bool enabled()
{
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    const char* e = getenv("GAMEOS_UI_DIRTY");
    if (e && *e) {
        cached = (strcmp(e, "0") && strcmp(e, "off") &&
                  strcmp(e, "false") && strcmp(e, "no")) ? 1 : 0;
    } else {
        cached = access("/userdata/system/ui-dirty.on", F_OK) == 0 ? 1 : 0;
    }
    return cached != 0;
}

bool active(SDL_Renderer* r)
{
    return rec_state(r) != nullptr;
}

bool begin(SDL_Renderer* r, SDL_Texture* target, int w, int h)
{
    if (!r || w <= 0 || h <= 0 || !enabled()) return false;
    State& s = states()[r];
    if (s.target != target || s.w != w || s.h != h) {
        s.target = target;
        s.w = w;
        s.h = h;
        s.shown.clear();
        s.have_shown = false;
        s.force_full = true;
    }
    s.next.clear();
    s.dirty.clear();
    s.res = Result{};
    s.clip_on = false;
    s.clip_rc = SDL_Rect{ 0, 0, 0, 0 };
    SDL_GetRenderDrawBlendMode(r, &s.blend);
    if (!s.recording) {
        s.recording = true;
        g_recording++;
    }
    return true;
}

void end(SDL_Renderer* r)
{
    State* s = find_state(r);
    if (!s || !s->recording) return;
    s->recording = false;
    if (g_recording > 0) g_recording--;

    Result& out = s->res;
    out.ops = (int)s->next.cmds.size();
    const bool full = s->force_full || !s->have_shown;

    if (!full &&
        s->shown.cmds.size() == s->next.cmds.size()) {
        bool same = true;
        for (size_t i = 0; i < s->next.cmds.size(); i++) {
            if (!cmd_same(s->shown, s->shown.cmds[i],
                          s->next, s->next.cmds[i])) {
                same = false;
                break;
            }
        }
        if (same) {
            out.skipped = true;
            s->dirty.clear();
            s->next.clear();
            return;
        }
    }

    std::vector<SDL_Rect> damage;
    if (full) {
        damage.push_back(screen_rect(s->w, s->h));
    } else {
        size_t n = std::max(s->shown.cmds.size(), s->next.cmds.size());
        for (size_t i = 0; i < n; i++) {
            const Cmd* a = i < s->shown.cmds.size() ? &s->shown.cmds[i] : nullptr;
            const Cmd* b = i < s->next.cmds.size() ? &s->next.cmds[i] : nullptr;
            if (a && b && cmd_same(s->shown, *a, s->next, *b)) continue;
            if (a && a->bounds.w > 0 && a->bounds.h > 0)
                damage.push_back(a->bounds);
            if (b && b->bounds.w > 0 && b->bounds.h > 0)
                damage.push_back(b->bounds);
        }
    }

    bool whole = full;
    s->dirty = merge_rects(std::move(damage), s->w, s->h,
                           kMaxRects, kMaxArea, &whole);
    if (s->dirty.empty() && !whole) {
        // Commands changed but nothing visible did (fully clipped or empty).
        out.skipped = true;
    }
    out.full = whole;
    out.rects = whole ? 1 : (int)s->dirty.size();
    long long area = 0;
    for (const SDL_Rect& rc : s->dirty) area += (long long)rc.w * rc.h;
    out.dirty = (double)s->w * s->h > 0
                    ? (double)area / ((double)s->w * s->h) : 1.0;

    std::swap(s->shown, s->next);
    s->next.clear();
    s->have_shown = true;
    s->force_full = false;
}

void replay(SDL_Renderer* r)
{
    State* s = find_state(r);
    if (!s || s->recording || !s->have_shown) return;
    Result& out = s->res;
    if (out.skipped) return;

    const Frame& f = s->shown;
    int draws = 0;
    SDL_Rect cur_clip{ 0, 0, 0, 0 };
    bool have_clip = false;

    for (const SDL_Rect& region : s->dirty) {
        if (region.w <= 0 || region.h <= 0) continue;
        for (const Cmd& c : f.cmds) {
            if (c.bounds.w <= 0 || c.bounds.h <= 0) continue;
            SDL_Rect vis = intersect_rect(c.bounds, region);
            if (vis.w <= 0 || vis.h <= 0) continue;
            SDL_Rect cl = c.has_clip ? intersect_rect(c.clip, region) : region;
            if (cl.w <= 0 || cl.h <= 0) continue;
            if (!have_clip || !rects_equal(cur_clip, cl)) {
                SDL_RenderSetClipRect(r, &cl);
                cur_clip = cl;
                have_clip = true;
            }
            draw_cmd(r, f, c);
            perf::note_draw();
            draws++;
        }
    }
    SDL_RenderSetClipRect(r, nullptr);
    out.replays = draws;
}

bool replay_full(SDL_Renderer* r)
{
    State* s = find_state(r);
    if (!s || s->recording || !s->have_shown) return false;
    const Frame& f = s->shown;
    SDL_Rect cur_clip{ 0, 0, 0, 0 };
    bool have_clip = false;
    for (const Cmd& c : f.cmds) {
        if (c.has_clip) {
            if (!have_clip || !rects_equal(cur_clip, c.clip)) {
                SDL_RenderSetClipRect(r, &c.clip);
                cur_clip = c.clip;
                have_clip = true;
            }
        } else if (have_clip) {
            SDL_RenderSetClipRect(r, nullptr);
            have_clip = false;
        }
        draw_cmd(r, f, c);
    }
    SDL_RenderSetClipRect(r, nullptr);
    return true;
}

void clip(SDL_Renderer* r, const SDL_Rect* rc)
{
    State* s = rec_state(r);
    if (s) {
        s->clip_on = rc != nullptr;
        s->clip_rc = rc ? *rc : SDL_Rect{ 0, 0, 0, 0 };
        return;
    }
    SDL_RenderSetClipRect(r, rc);
}

void fill(SDL_Renderer* r, SDL_Rect rc, SDL_Color c)
{
    State* s = rec_state(r);
    if (!s) {
        perf::note_draw();
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(r, &rc);
        return;
    }
    // fill_rect always selects BLEND in the direct path; keep the real and
    // simulated renderer state in step so later geometry records the same
    // blend mode it would have used without the recorder.
    if (s->blend != SDL_BLENDMODE_BLEND) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        s->blend = SDL_BLENDMODE_BLEND;
    }
    Cmd cmd;
    cmd.kind = Cmd::Kind::Fill;
    cmd.dst = rc;
    cmd.color = c;
    cmd.alpha = c.a;
    cmd.has_clip = s->clip_on;
    cmd.clip = s->clip_rc;
    cmd.blend = SDL_BLENDMODE_BLEND;
    cmd.bounds = clip_bounds(*s, rc);
    s->next.cmds.push_back(cmd);
}

void tex(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src,
         SDL_Rect dst, Uint8 alpha)
{
    if (!t) return;
    State* s = rec_state(r);
    if (!s) {
        perf::note_draw();
        SDL_SetTextureAlphaMod(t, alpha);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(r, t, src, &dst);
        return;
    }
    Cmd cmd;
    cmd.kind = Cmd::Kind::Tex;
    cmd.dst = dst;
    if (src) {
        cmd.src = *src;
        cmd.has_src = true;
    }
    cmd.tex = t;
    cmd.alpha = alpha;
    cmd.has_clip = s->clip_on;
    cmd.clip = s->clip_rc;
    cmd.bounds = clip_bounds(*s, dst);
    s->next.cmds.push_back(cmd);
}

void geom(SDL_Renderer* r, const SDL_Vertex* v, int n,
          const int* idx, int m)
{
    if (!v || n <= 0) return;
    State* s = rec_state(r);
    if (!s) {
        perf::note_draw();
        SDL_RenderGeometry(r, nullptr, v, n, idx, m);
        return;
    }
    Cmd cmd;
    cmd.kind = Cmd::Kind::Geom;
    cmd.v_off = (Uint32)s->next.verts.size();
    cmd.v_n = (Uint32)n;
    s->next.verts.insert(s->next.verts.end(), v, v + n);
    cmd.i_off = (Uint32)s->next.idx.size();
    cmd.i_n = (Uint32)(m > 0 ? m : 0);
    if (m > 0 && idx) s->next.idx.insert(s->next.idx.end(), idx, idx + m);
    cmd.sig = hash_geometry(v, n, m > 0 ? idx : nullptr, m > 0 ? m : 0);
    cmd.has_clip = s->clip_on;
    cmd.clip = s->clip_rc;
    cmd.blend = s->blend;
    cmd.bounds = geom_bounds(v, n, *s);
    s->next.cmds.push_back(cmd);
}

void invalidate()
{
    for (auto& kv : states()) {
        State& s = kv.second;
        s.shown.clear();
        s.have_shown = false;
        s.force_full = true;
        s.dirty.clear();
    }
}

void destroy_texture(SDL_Texture* t)
{
    if (!t) return;
    bool recording = false;
    for (auto& kv : states()) {
        State& s = kv.second;
        if (s.recording) recording = true;
        s.shown.clear();
        s.have_shown = false;
        s.force_full = true;
        s.dirty.clear();
    }
    if (recording) graveyard().push_back(t);
    else SDL_DestroyTexture(t);
}

void flush()
{
    if (graveyard().empty()) return;
    for (SDL_Texture* t : graveyard()) SDL_DestroyTexture(t);
    graveyard().clear();
    // The frame that referenced them is gone from the diff history; the next
    // one starts from a known-full repaint.
    invalidate();
}

void shutdown()
{
    for (SDL_Texture* t : graveyard()) SDL_DestroyTexture(t);
    graveyard().clear();
    states().clear();
    g_recording = 0;
}

const Result& last(SDL_Renderer* r)
{
    static const Result empty;
    auto it = states().find(r);
    return it == states().end() ? empty : it->second.res;
}

} // namespace drawcall
} // namespace gameos
