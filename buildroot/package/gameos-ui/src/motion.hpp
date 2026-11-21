// gameOS front-end — motion tokens, easing, animator and frame budget.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace gameos {
namespace motion {

// Duration tokens, in milliseconds.
constexpr int kPressMs = 80;
constexpr int kMicroMs = 120;
constexpr int kExitMs = 160;
constexpr int kEnterMs = 180;
constexpr int kNormalMs = 220;
constexpr int kLongMs = 320;
constexpr int kInstantMs = 0;

enum class Ease { Enter, Exit, Move, Spring, Linear };

// Per-theme motion language: the same tokens, a different rhythm. Theme 1
// (pixel) steps its transitions like a low frame-rate console; theme 2
// (fresh) floats with a soft spring; theme 3 (ink) is slow and fade-only.
struct ThemeProfile {
    float speed;      // multiplier applied to the duration tokens
    Ease scene;       // incoming-screen curve
    float slide_px;   // vertical travel of the incoming scene (0 = pure fade)
    float carousel;   // damp rate for the console carousel
    float scroll;     // damp rate for lists and selectors
    int steps;        // >1 quantises transition progress (retro stepping)
};

ThemeProfile theme_profile(int theme);
int theme_ms(int ms, int theme);

// Cubic bezier easing sampled from a 60-entry lookup table.
float ease(Ease e, float x);

// Monotonic CPU clock for one frame's UI work (thread CPU time).
uint64_t cpu_now_ns();

// One animated scalar. Interrupting retargets from the current visual value.
class Anim {
public:
    void start(float from, float to, int dur_ms, Ease e, uint64_t now_ms);
    void retarget(float to, int dur_ms, Ease e, uint64_t now_ms);
    void stop(float value);
    float at(uint64_t now_ms) const;
    bool running(uint64_t now_ms) const;

private:
    float from_ = 0.0f;
    float to_ = 0.0f;
    uint64_t t0_ = 0;
    int dur_ = 0;
    Ease ease_ = Ease::Linear;
    bool running_ = false;
};

// Frame-time sampler and L0..L3 degradation ladder.
//
// A 30-frame window whose P95 UI CPU time exceeds the budget raises the level
// by one (at most once per window); 120 calm windows lower it by one, followed
// by a 300-frame hold-off before any further change. L3 means hard cuts.
class Degrade {
public:
    enum Level { kL0 = 0, kL1, kL2, kL3 };

    void sample(double cpu_ms);
    int level() const { return (int)level_; }

private:
    static constexpr int kWindow = 30;
    static constexpr double kBudgetMs = 4.0;
    double win_[kWindow] = {};
    int n_ = 0;
    int pos_ = 0;
    Level level_ = kL0;
    int since_change_ = 0;
    int calm_ = 0;
    int cooldown_ = 0;
};

} // namespace motion
} // namespace gameos
