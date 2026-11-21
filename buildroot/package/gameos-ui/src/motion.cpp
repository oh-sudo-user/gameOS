// gameOS front-end — motion tokens, easing, animator and frame budget.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <time.h>

namespace gameos {
namespace motion {

namespace {

struct Curve {
    double x1, y1, x2, y2;
};

// ease.enter / ease.exit / ease.move / ease.spring from the UI spec.
constexpr Curve kCurves[4] = {
    { 0.20, 0.00, 0.00, 1.00 },
    { 0.40, 0.00, 1.00, 1.00 },
    { 0.35, 0.00, 0.25, 1.00 },
    { 0.34, 1.30, 0.64, 1.00 },
};

constexpr int kLut = 60;

double bez(double p1, double p2, double t)
{
    double mt = 1.0 - t;
    return 3.0 * mt * mt * t * p1 + 3.0 * mt * t * t * p2 + t * t * t;
}

double bez_deriv(double p1, double p2, double t)
{
    double mt = 1.0 - t;
    return 3.0 * mt * mt * p1 + 6.0 * mt * t * (p2 - p1) +
           3.0 * t * t * (1.0 - p2);
}

double curve_at(const Curve& c, double t)
{
    return bez(c.y1, c.y2, t);
}

double solve_t(const Curve& c, double x)
{
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    double t = x;
    for (int i = 0; i < 8; i++) {
        double xt = bez(c.x1, c.x2, t);
        double d = bez_deriv(c.x1, c.x2, t);
        if (std::fabs(d) < 1e-6) break;
        double step = (xt - x) / d;
        t -= step;
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;
        if (std::fabs(step) < 1e-7) break;
    }
    if (std::fabs(bez(c.x1, c.x2, t) - x) > 1e-4) {
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 20; i++) {
            double mid = (lo + hi) * 0.5;
            if (bez(c.x1, c.x2, mid) < x) lo = mid;
            else hi = mid;
        }
        t = (lo + hi) * 0.5;
    }
    return t;
}

struct Table {
    float v[kLut];
};

Table make_table(const Curve& c)
{
    Table t;
    for (int i = 0; i < kLut; i++) {
        double x = (double)i / (double)(kLut - 1);
        t.v[i] = (float)curve_at(c, solve_t(c, x));
    }
    t.v[0] = 0.0f;
    t.v[kLut - 1] = 1.0f;
    return t;
}

const Table& table_for(Ease e)
{
    static const Table tables[4] = {
        make_table(kCurves[0]),
        make_table(kCurves[1]),
        make_table(kCurves[2]),
        make_table(kCurves[3]),
    };
    return tables[(int)e];
}

} // namespace

float ease(Ease e, float x)
{
    if (e == Ease::Linear) {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;
        return x;
    }
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    const Table& t = table_for(e);
    float xf = x * (float)(kLut - 1);
    int i = (int)xf;
    float f = xf - (float)i;
    if (i >= kLut - 1) return t.v[kLut - 1];
    return t.v[i] + (t.v[i + 1] - t.v[i]) * f;
}

uint64_t cpu_now_ns()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void Anim::start(float from, float to, int dur_ms, Ease e, uint64_t now_ms)
{
    from_ = from;
    to_ = to;
    t0_ = now_ms;
    dur_ = dur_ms > 0 ? dur_ms : 0;
    ease_ = e;
    running_ = dur_ > 0;
    if (!running_) from_ = to_;
}

void Anim::retarget(float to, int dur_ms, Ease e, uint64_t now_ms)
{
    start(at(now_ms), to, dur_ms, e, now_ms);
}

void Anim::stop(float value)
{
    from_ = value;
    to_ = value;
    running_ = false;
}

float Anim::at(uint64_t now_ms) const
{
    if (!running_) return to_;
    uint64_t elapsed = now_ms > t0_ ? now_ms - t0_ : 0;
    if (elapsed >= (uint64_t)dur_) return to_;
    float p = ease(ease_, (float)((double)elapsed / (double)dur_));
    return from_ + (to_ - from_) * p;
}

bool Anim::running(uint64_t now_ms) const
{
    if (!running_) return false;
    uint64_t elapsed = now_ms > t0_ ? now_ms - t0_ : 0;
    return elapsed < (uint64_t)dur_;
}

void Degrade::sample(double cpu_ms)
{
    if (!(cpu_ms > 0.0)) cpu_ms = 0.0;
    win_[pos_] = cpu_ms;
    pos_ = (pos_ + 1) % kWindow;
    if (n_ < kWindow) n_++;
    since_change_++;
    if (cooldown_ > 0) cooldown_--;
    if (n_ < kWindow) return;

    double sorted[kWindow];
    memcpy(sorted, win_, sizeof sorted);
    std::sort(sorted, sorted + kWindow);
    int idx = (int)std::ceil(0.95 * kWindow) - 1;
    if (idx < 0) idx = 0;
    double p95 = sorted[idx];

    if (p95 > kBudgetMs) {
        calm_ = 0;
        if (level_ < kL3 && since_change_ >= kWindow && cooldown_ == 0) {
            level_ = (Level)(level_ + 1);
            since_change_ = 0;
        }
    } else if (p95 < kBudgetMs * 0.5) {
        calm_++;
        if (calm_ >= 120 && level_ > kL0 && cooldown_ == 0) {
            level_ = (Level)(level_ - 1);
            calm_ = 0;
            since_change_ = 0;
            cooldown_ = 300;
        }
    } else {
        calm_ = 0;
    }
}

} // namespace motion
} // namespace gameos
