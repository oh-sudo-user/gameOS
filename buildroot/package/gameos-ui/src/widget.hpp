// gameOS front-end — base widget library (UI spec C01, C04..C06, C09..C11).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tokens.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gameos {
namespace widget {

enum class State { Normal, Focused, Pressed, Disabled };

// ---------------------------------------------------------------------------
// Pure layout / state helpers (host-testable)
// ---------------------------------------------------------------------------
inline SDL_Rect row_rect(SDL_Rect area, int index, int scroll,
                         int row_h = token::row_list)
{
    return SDL_Rect{ area.x, area.y + index * row_h - scroll, area.w, row_h };
}

inline int row_at(SDL_Rect area, int scroll, int y, int count,
                  int row_h = token::row_list)
{
    if (count <= 0 || y < area.y || y >= area.y + area.h) return -1;
    int idx = (y - area.y + scroll) / row_h;
    if (y < area.y || idx < 0 || idx >= count) return -1;
    return idx;
}

inline int switch_knob_x(SDL_Rect rc, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    int knob = std::min(token::switch_knob, rc.h);
    return rc.x + 2 + (int)((rc.w - knob - 4) * t);
}

inline int slider_knob_x(SDL_Rect rc, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    int knob = std::min(token::slider_knob, rc.w);
    return rc.x + knob / 2 + (int)((rc.w - knob) * t);
}

inline float slider_value_at(SDL_Rect rc, int x)
{
    if (rc.w <= 0) return 0.0f;
    float p = (float)(x - rc.x) / (float)rc.w;
    return std::clamp(p, 0.0f, 1.0f);
}

inline SDL_Rect dialog_panel_rect(SDL_Rect screen, int w, int h)
{
    int maxw = std::max(0, screen.w - token::space_24 * 2);
    int maxh = std::max(0, screen.h - token::space_24 * 2);
    w = std::clamp(w, 0, maxw);
    h = std::clamp(h, 0, maxh);
    return SDL_Rect{ screen.x + (screen.w - w) / 2,
                     screen.y + (screen.h - h) / 2, w, h };
}

inline std::vector<SDL_Rect> dialog_button_rects(
    SDL_Rect panel, int count, int width = 132)
{
    std::vector<SDL_Rect> out;
    if (count <= 0 || panel.w <= 0) return out;
    int gap = token::space_12;
    int avail = panel.w - token::dialog_inset * 2 - gap * (count - 1);
    width = std::min(width, std::max(1, avail / count));
    int x = panel.x + panel.w - token::dialog_inset - width;
    int y = panel.y + panel.h - token::dialog_inset - token::button_h;
    out.resize(count);
    for (int i = count - 1; i >= 0; i--) {
        out[i] = SDL_Rect{ x, y, width, token::button_h };
        x -= width + gap;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
struct ListRow {
    std::string title;
    std::string detail;
    std::string value;
    bool chevron = false;
    bool selected = false;
    bool disabled = false;
    State state = State::Normal;
};

void list_row(SDL_Renderer* r, SDL_Rect rc, const ListRow& row);
std::string middle_ellipsis(TTF_Font* f, const std::string& s, int maxw);

enum class ButtonKind { Primary, Secondary, Danger };

struct Button {
    std::string label;
    ButtonKind kind = ButtonKind::Primary;
    State state = State::Normal;
};

void button(SDL_Renderer* r, SDL_Rect rc, const Button& b);
void switch_control(SDL_Renderer* r, SDL_Rect rc, float t, State st);
void slider_control(SDL_Renderer* r, SDL_Rect rc, float t, State st);

struct DialogButton {
    std::string label;
    ButtonKind kind = ButtonKind::Secondary;
};

struct Dialog {
    std::string title;
    std::string body;
    std::vector<DialogButton> buttons;
    int focus = 0;
    int pressed = -1;
};

void dialog(SDL_Renderer* r, SDL_Rect screen, const Dialog& d);

enum class ToastKind { Info, Success, Warning, Danger };

struct ToastState {
    std::string text;
    ToastKind kind = ToastKind::Info;
    uint64_t start_ms = 0;
    int dur_ms = 0;
    bool active = false;
};

class ToastQueue {
public:
    void show(std::string text, ToastKind kind, uint64_t now_ms,
              int dur_ms = token::toast_ms)
    {
        cur_.text = std::move(text);
        cur_.kind = kind;
        cur_.start_ms = now_ms;
        cur_.dur_ms = dur_ms > 0 ? dur_ms : 1;
        cur_.active = true;
    }

    void clear() { cur_.active = false; }

    const ToastState& current() const { return cur_; }

    bool active(uint64_t now_ms) const
    {
        if (!cur_.active || now_ms < cur_.start_ms) return false;
        return now_ms < cur_.start_ms + (uint64_t)cur_.dur_ms;
    }

    float alpha(uint64_t now_ms) const
    {
        if (!active(now_ms)) return 0.0f;
        uint64_t elapsed = now_ms - cur_.start_ms;
        uint64_t remain = cur_.start_ms + (uint64_t)cur_.dur_ms - now_ms;
        float a = 1.0f;
        if (elapsed < 120) a = (float)elapsed / 120.0f;
        if (remain < 160) a = std::min(a, (float)remain / 160.0f);
        return std::clamp(a, 0.0f, 1.0f);
    }

private:
    ToastState cur_;
};

void toast(SDL_Renderer* r, SDL_Rect screen, const ToastState& t, float alpha);

} // namespace widget
} // namespace gameos
