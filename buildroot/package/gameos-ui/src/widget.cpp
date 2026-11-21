// gameOS front-end — base widget library.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "widget.hpp"

#include "ui.hpp"

#include <cmath>

namespace gameos {
namespace widget {

namespace {

SDL_Color toast_accent(ToastKind kind)
{
    switch (kind) {
    case ToastKind::Success: return C_GREEN;
    case ToastKind::Warning: return C_AMBER;
    case ToastKind::Danger:  return C_RED;
    default:                 return C_ACCENT;
    }
}

SDL_Color on_accent()
{
    int lum = (C_ACCENT.r * 299 + C_ACCENT.g * 587 + C_ACCENT.b * 114) / 1000;
    return lum > 150 ? rgba(10, 16, 28, 255) : rgba(255, 255, 255, 255);
}

size_t utf8_len(unsigned char c)
{
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}

} // namespace

std::string middle_ellipsis(TTF_Font* f, const std::string& s, int maxw)
{
    if (!f || maxw <= 0 || text_width(f, s) <= maxw) return s;

    std::vector<std::pair<size_t, size_t>> parts;
    for (size_t i = 0; i < s.size();) {
        size_t n = utf8_len((unsigned char)s[i]);
        if (i + n > s.size()) break;
        parts.push_back({ i, n });
        i += n;
    }

    const std::string ell = "…";
    int budget = std::max(0, maxw - text_width(f, ell));
    int headw = budget * 60 / 100;
    int tailw = budget * 25 / 100;

    size_t head_n = 0;
    std::string head;
    for (size_t i = 0; i < parts.size(); i++) {
        std::string piece = s.substr(parts[i].first, parts[i].second);
        if (text_width(f, head + piece) > headw) break;
        head += piece;
        head_n = i + 1;
    }

    std::string tail;
    size_t tail_from = parts.size();
    for (size_t i = parts.size(); i > 0; i--) {
        std::string piece = s.substr(parts[i - 1].first, parts[i - 1].second);
        if (text_width(f, piece + tail) > tailw) break;
        tail = piece + tail;
        tail_from = i - 1;
    }
    if (tail_from < head_n) tail.clear();
    return head + ell + tail;
}

void list_row(SDL_Renderer* r, SDL_Rect rc, const ListRow& row)
{
    const bool focused = row.state == State::Focused ||
                         row.state == State::Pressed;
    if (row.chrome) {
        SDL_Color bg = row.selected ? with_alpha(C_ACCENT, 28)
                                    : with_alpha(C_PANEL, 200);
        if (focused) bg = with_alpha(C_ACCENT, 42);
        rrect(r, rc, token::radius_card, bg);
        if (focused) {
            rrect_stroke(r, rc, token::radius_card,
                         with_alpha(C_ACCENT, 190), 2);
            fill_rect(r, rc.x + token::space_4, rc.y + token::space_12,
                      3, rc.h - token::space_24, C_ACCENT);
        }
    }

    int x = rc.x + token::space_32;
    int right = rc.x + rc.w - token::space_16;
    if (row.chevron) {
        icon_chevron(r, right - 16, rc.y + (rc.h - 16) / 2, 16,
                     with_alpha(C_DIM, row.disabled ? 110 : 200), false);
        right -= token::space_24;
    }
    if (!row.badge.empty()) {
        int bw = std::max(40, text_width(g_ui.small, row.badge) + 8);
        SDL_Rect pill = { right - bw, rc.y + (rc.h - 24) / 2, bw, 24 };
        rrect(r, pill, 12, with_alpha(C_TEXT, 30));
        text(r, g_ui.small, row.badge, pill.x + pill.w / 2,
             pill.y + (24 - 30) / 2, C_DIM, Align::Center);
        right -= bw + token::space_8;
    }
    if (row.star) {
        icon_star(r, right - 32, rc.y + (rc.h - 32) / 2, 32, C_AMBER, true);
        right -= 32 + token::space_8;
    }
    if (!row.value.empty()) {
        int vw = text_width(g_ui.small, row.value);
        SDL_Color vc = row.value_color.a ? row.value_color
                                         : with_alpha(C_TEXT, 220);
        if (row.disabled) vc = with_alpha(vc, 110);
        text(r, g_ui.small, row.value, right - vw,
             rc.y + (rc.h - 30) / 2, vc);
        right -= vw + token::space_12;
    }

    Uint8 fg = row.disabled ? 110 : (row.dim ? 205 : 255);
    int maxw = std::max(0, right - x - token::space_8);
    // Current fonts are larger than the spec's DP sizes, so a two-line row
    // needs at least 72 px; shorter rows keep the single-line title only.
    const bool two_lines = !row.detail.empty() && rc.h >= 72;
    if (!two_lines) {
        text(r, g_ui.body,
             ellipsize(g_ui.body, row.title, maxw),
             x, rc.y + (rc.h - 38) / 2,
             with_alpha(focused ? C_ACCENT : C_TEXT, fg));
    } else {
        text(r, g_ui.body,
             middle_ellipsis(g_ui.body, row.title, maxw),
             x, rc.y + 8,
             with_alpha(focused ? C_ACCENT : C_TEXT, fg));
        text(r, g_ui.small,
             ellipsize(g_ui.small, row.detail, maxw),
             x, rc.y + rc.h - 36, with_alpha(C_DIM, fg));
    }
}

void button(SDL_Renderer* r, SDL_Rect rc, const Button& b)
{
    const bool pressed = b.state == State::Pressed;
    const bool focused = b.state == State::Focused || pressed;
    const bool disabled = b.state == State::Disabled;
    SDL_Rect box = rc;
    if (pressed) { box.x += 2; box.y += 2; box.w -= 4; box.h -= 4; }

    SDL_Color fg = C_TEXT;
    if (b.kind == ButtonKind::Primary) {
        SDL_Color top = lighten(C_ACCENT, 0.12f);
        SDL_Color bot = C_ACCENT;
        if (pressed) {
            top = mix_color(top, C_BG, 0.08f);
            bot = mix_color(bot, C_BG, 0.08f);
        }
        if (disabled) {
            top = with_alpha(top, 110);
            bot = with_alpha(bot, 110);
        }
        rrect_grad(r, box, token::radius_button, top, bot);
        fg = on_accent();
        if (disabled) fg = with_alpha(fg, 150);
    } else if (b.kind == ButtonKind::Danger) {
        SDL_Color base = disabled ? with_alpha(C_RED, 110) : C_RED;
        rrect_grad(r, box, token::radius_button, lighten(base, 0.08f), base);
        fg = rgba(255, 255, 255, disabled ? 150 : 255);
    } else {
        SDL_Color fill = C_PANEL;
        fill.a = disabled ? 120 : (pressed ? 255 : 235);
        rrect(r, box, token::radius_button, fill);
        rrect_stroke(r, box, token::radius_button,
                     focused ? with_alpha(C_ACCENT, 200)
                             : with_alpha(C_DIM, 38),
                     focused ? 2 : 1);
        fg = focused ? C_ACCENT
                     : with_alpha(C_TEXT, disabled ? 110 : 255);
    }
    if (focused) focus_ring(r, box, token::radius_button, 120);
    text(r, g_ui.body, b.label, box.x + box.w / 2, box.y + (box.h - 38) / 2,
         fg, Align::Center);
}

void switch_control(SDL_Renderer* r, SDL_Rect rc, float t, State st)
{
    const bool focused = st == State::Focused || st == State::Pressed;
    const bool disabled = st == State::Disabled;
    t = std::clamp(t, 0.0f, 1.0f);
    SDL_Color track = mix_color(C_TRACK, C_ACCENT, t);
    if (disabled) track = with_alpha(track, 110);
    rrect(r, rc, rc.h / 2, track);
    int knob = std::min(token::switch_knob, rc.h);
    int x = switch_knob_x(rc, t);
    circle(r, x + knob / 2, rc.y + rc.h / 2, knob / 2,
           rgba(255, 255, 255, disabled ? 150 : 245));
    if (focused) {
        SDL_Rect ring_rc = { rc.x - token::space_4, rc.y - token::space_4,
                             rc.w + token::space_8, rc.h + token::space_8 };
        focus_ring(r, ring_rc, ring_rc.h / 2, 120);
    }
}

void slider_control(SDL_Renderer* r, SDL_Rect rc, float t, State st)
{
    const bool focused = st == State::Focused || st == State::Pressed;
    const bool disabled = st == State::Disabled;
    t = std::clamp(t, 0.0f, 1.0f);
    SDL_Rect track = { rc.x, rc.y + (rc.h - token::slider_track_h) / 2,
                       rc.w, token::slider_track_h };
    rrect(r, track, track.h / 2,
          disabled ? with_alpha(C_TRACK, 120) : C_TRACK);
    int w = (int)(track.w * t);
    if (w > 2) rrect(r, { track.x, track.y, w, track.h }, track.h / 2,
                     disabled ? with_alpha(C_ACCENT, 120) : C_ACCENT);
    int cx = slider_knob_x(rc, t);
    int cy = rc.y + rc.h / 2;
    if (focused) glow(r, cx, cy, token::slider_knob, C_ACCENT, 8, 70);
    circle(r, cx, cy, token::slider_knob / 2,
           disabled ? with_alpha(C_TEXT, 120) : lighten(C_TEXT, 0.2f));
    circle(r, cx, cy, token::slider_knob / 2 - 4,
           disabled ? with_alpha(C_ACCENT, 120) : C_ACCENT);
}

void dialog(SDL_Renderer* r, SDL_Rect screen, const Dialog& d)
{
    fill_rect(r, screen.x, screen.y, screen.w, screen.h,
              { C_BG.r, C_BG.g, C_BG.b, 185 });
    SDL_Rect panel = dialog_panel_rect(screen, token::dialog_max_w, 340);
    glow(r, panel.x + panel.w / 2, panel.y + 48, 300, C_ACCENT, 10, 14);
    card(r, panel, token::radius_overlay, false, 246);

    int inset = token::dialog_inset;
    int textw = panel.w - inset * 2;
    text(r, g_ui.title, ellipsize(g_ui.title, d.title, textw),
         panel.x + inset, panel.y + inset, C_TEXT);
    if (!d.body.empty()) {
        auto lines = wrap(g_ui.small, d.body, textw);
        int y = panel.y + inset + 62;
        int limit = panel.y + panel.h - inset - token::button_h -
                    token::space_12;
        for (const std::string& l : lines) {
            if (y + 30 > limit) break;
            text(r, g_ui.small, l, panel.x + inset, y, C_TEXT);
            y += 30;
        }
    }

    auto rects = dialog_button_rects(panel, (int)d.buttons.size());
    for (size_t i = 0; i < d.buttons.size() && i < rects.size(); i++) {
        Button b;
        b.label = d.buttons[i].label;
        b.kind = d.buttons[i].kind;
        b.state = d.pressed == (int)i ? State::Pressed
                : d.focus == (int)i ? State::Focused
                                    : State::Normal;
        button(r, rects[i], b);
    }
}

void toast(SDL_Renderer* r, SDL_Rect screen, const ToastState& t, float alpha)
{
    if (!t.active || alpha <= 0.0f || t.text.empty()) return;
    SDL_Color ac = toast_accent(t.kind);
    int maxw = std::max(0, screen.w - token::space_24 * 2);
    std::string label = ellipsize(g_ui.small, t.text, maxw - 88);
    int w = std::min(maxw, text_width(g_ui.small, label) + 88);
    SDL_Rect rc = { screen.x + (screen.w - w) / 2,
                    screen.y + screen.h - token::space_24 - token::toast_h,
                    w, token::toast_h };
    rrect(r, rc, rc.h / 2, with_alpha(C_PANEL, (Uint8)(242 * alpha)));
    rrect_stroke(r, rc, rc.h / 2, with_alpha(ac, (Uint8)(200 * alpha)), 1);
    circle(r, rc.x + token::space_24, rc.y + rc.h / 2, 10,
           with_alpha(ac, (Uint8)(255 * alpha)));
    text(r, g_ui.small, label, rc.x + 44, rc.y + (rc.h - 30) / 2,
         with_alpha(C_TEXT, (Uint8)(235 * alpha)));
}

} // namespace widget
} // namespace gameos
