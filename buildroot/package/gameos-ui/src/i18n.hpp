// gameOS front-end — internationalisation (6 languages).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>
#include <string>

namespace gameos {

enum Lang {
    LANG_ZH_CN = 0,   // 简体中文
    LANG_ZH_TW,       // 繁體中文
    LANG_JA,          // 日本語
    LANG_EN,          // English
    LANG_RU,          // Русский
    LANG_ES,          // Español
    LANG_COUNT
};

// Active language; set from Settings.language at startup and on change.
extern Lang g_lang;

// UI string keys. Every key has a translation for each Lang; tr() falls
// back to English (then "") if a cell is ever left empty.
enum TKey {
    // home / carousel
    T_SETTINGS, T_SETTINGS_SUB, T_HOME_HINT, T_GAMES_FMT,
    // game list
    T_LIST_COUNT_FMT, T_FAV_SUFFIX, T_NO_FAV, T_NO_GAMES, T_FAV_HELP,
    T_COPY_ROMS_FMT, T_TF_HELP, T_COVER, T_LIST_HINT, T_TF2_ON,
    // bluetooth
    T_BT, T_ENABLED, T_DISABLED, T_BT_SWITCH, T_ON, T_OFF,
    T_KIND_AUDIO, T_KIND_PAD, T_KIND_KBD, T_KIND_MOUSE, T_KIND_DEV,
    T_CONN_SUFFIX, T_PAIRED_SUFFIX, T_NO_DEVS, T_PROCESSING, T_BT_HINT,
    T_BT_TIMEOUT, T_SCAN_DONE, T_BT_ON_OK, T_BT_ON_FAIL, T_BT_OFF_OK,
    T_PAIR_OK, T_CONNECTED, T_DISCONNECTED, T_FORGOT, T_OP_FAIL,
    T_TURNING_OFF, T_TURNING_ON, T_PAIRING, T_CONNECTING, T_DISCONNECTING,
    T_SCANNING, T_FORGETTING,
    // settings rows
    T_BRIGHTNESS, T_VOLUME, T_THEME, T_LANGUAGE, T_SAVES, T_POWEROFF,
    T_REBOOT, T_ABOUT,
    T_SAVES_COUNT_FMT, T_SETTINGS_HINT,
    // saves screen
    T_SAVE_COUNT_FMT, T_NO_SAVES, T_SAVES_HELP, T_SAVES_HINT,
    // message / confirm dialogs
    T_BACK, T_CONFIRM_OFF, T_CONFIRM_REBOOT, T_CONFIRM_DEL, T_CONFIRM_HINT,
    T_EMU_FAIL, T_EMU_FAIL_BODY, T_ABOUT_BODY,
    // ambient panel
    T_TAGLINE,
    // wifi
    T_WIFI, T_WIFI_HINT, T_WIFI_NO_NETS, T_WIFI_SCANNING, T_WIFI_PASSWORD,
    T_WIFI_CONNECTING, T_WIFI_CONNECTED_FMT, T_WIFI_CONNECT_FAIL,
    T_WIFI_DISCONNECTED, T_WIFI_UNAVAILABLE, T_WIFI_SAVED, T_WIFI_OPEN,
    T_WIFI_FORGET_DONE, T_KB_SPACE, T_KB_DEL, T_KB_OK, T_KB_CANCEL,
    // system update
    T_UPDATE, T_UPDATE_HINT, T_UPDATE_CHECKING, T_UPDATE_UPTODATE,
    T_UPDATE_AVAILABLE, T_UPDATE_DOWNLOADING, T_UPDATE_INSTALLING,
    T_UPDATE_READY, T_UPDATE_FAILED, T_UPDATE_NOWIFI, T_UPDATE_REBOOT,
    T_UPDATE_CURRENT, T_UPDATE_IDLE, T_UPDATE_CONFIRM, T_UPDATE_CONFIRM_BODY,
    // boot splash stages (gameos-splash reads /run/gameos/boot-stage)
    T_BOOT_DISPLAY, T_BOOT_STORAGE, T_BOOT_DATA, T_BOOT_NETWORK,
    T_BOOT_AUDIO, T_BOOT_INPUT, T_BOOT_READY,
    // first-boot wizard frame
    T_SETUP, T_SETUP_TITLE, T_SETUP_STEP_FMT, T_SETUP_HINT,
    T_SETUP_DONE_TITLE, T_SETUP_DONE_BODY, T_SETUP_NEED_NET, T_SETUP_SKIP_ALL,
    // connectivity test
    T_WIFI_TEST, T_WIFI_TESTING, T_TEST_LINK, T_TEST_ROUTE, T_TEST_DNS,
    T_TEST_NET, T_TEST_OK, T_TEST_FAIL, T_TEST_WAIT,
    // timezone
    T_SETUP_TZ_TITLE, T_SETUP_TZ_HINT, T_TZ_ASIA, T_TZ_EUROPE, T_TZ_AMERICA,
    T_TZ_AFRICA, T_TZ_OCEANIA, T_TZ_UTC, T_TZ_NOW_FMT,
    // machine name
    T_SETUP_NAME_TITLE, T_SETUP_NAME_HINT, T_SETUP_NAME_HELP,
    T_NAME_INVALID, T_NAME_APPLIED_FMT,
    // retroachievements
    T_SETUP_RA_TITLE, T_SETUP_RA_HELP, T_RA_USER, T_RA_PASS, T_RA_HARDCORE,
    T_RA_HARDCORE_HELP, T_RA_LOGIN, T_RA_LOGGING_IN, T_RA_OK_FMT,
    T_RA_FAIL_FMT, T_RA_LOGGED_FMT, T_RA_LOGOUT,
    // covers
    T_SETUP_COVER_HELP, T_COVER_START, T_COVER_RUNNING, T_COVER_PROGRESS_FMT,
    T_COVER_DONE_FMT, T_COVER_STOP, T_COVER_NONE, T_COVERS,
    T_COVER_IDLE, T_COVER_QUEUE_FMT,
    // wizard: advance without acting on the current row
    T_NEXT,
    // activity statistics + favorites/history hub
    T_ACTIVITY, T_ACTIVITY_SUB, T_QUICK, T_QUICK_SUB,
    T_TAB_OVERVIEW, T_TAB_TIMELINE, T_TAB_TREND, T_TAB_PICKS,
    T_ACT_TOTAL, T_ACT_SESSIONS, T_ACT_GAMES_PLAYED, T_ACT_TODAY, T_ACT_WEEK,
    T_ACT_MOST, T_ACT_EMPTY, T_ACT_EMPTY_HELP, T_ACT_SESSIONS_FMT, T_ACT_GAMES_FMT,
    T_DUR_HM_FMT, T_DUR_M_FMT, T_DUR_S_FMT,
    T_ACT_TREND_TITLE, T_ACT_BY_CONSOLE, T_ACT_NO_TREND,
    T_PICK_FAV_NEW, T_PICK_FAV_BACK, T_PICK_NEW, T_PICK_BACK, T_PICK_REGULAR,
    T_ACT_PICKS_EMPTY, T_ACT_PICKS_HELP, T_PICK_IDLE_FMT,
    T_TAB_FAV, T_TAB_HISTORY, T_QUICK_EMPTY_FAV, T_QUICK_EMPTY_FAV_HELP,
    T_QUICK_EMPTY_HIST, T_QUICK_EMPTY_HIST_HELP, T_QUICK_MISSING, T_QUICK_COUNT_FMT,
    T_ACTIVITY_HINT, T_QUICK_HINT,
    // data partition could not be mounted (never auto-formatted)
    T_DATA_ERROR, T_DATA_ERROR_HELP,
    // 2.0 touch-first deck
    T_PLAY, T_SKIP, T_RECENT,
    T_TOUCH_HOME, T_TOUCH_LIST, T_TOUCH_SETTINGS, T_TOUCH_WIFI,
    // update deck actions
    T_UPD_CHECK, T_UPD_RECHECK, T_UPD_DOWNLOAD,
    // touch-first wifi deck
    T_SCAN, T_FORGET, T_WIFI_EMPTY,
    // touch hints for wizard steps and the deck keyboard
    T_TOUCH_LANG, T_TOUCH_NAME, T_TOUCH_COVER, T_KB_DECK_HINT, T_TOUCH_TZ,
    T_DELETE_SAVE, T_BT_OFF_HELP,
    T_SET_HINT_GRID, T_SET_HINT_ADJUST,
    T_WIFI_SCAN_FAIL, T_WIFI_SUPPLICANT_FAIL,
    T_CONFIRM_FORGET,
    T_EMU_CRASH_TITLE, T_EMU_CRASH_BODY,
    // launch animation status lines (bottom panel)
    T_LOAD_CARD, T_LOAD_VERIFY, T_LOAD_START,
    T_COUNT
};

const char* tr(TKey k);

// Localised console display name (keyed by Console::id).
const char* console_name(const char* id);
// Localised theme name (0..3).
const char* theme_name(int idx);
// Asset directory key for a theme: themes/<key>/ under the assets root.
const char* theme_key(int idx);
// Full palette of a theme, shared by the UI and the boot splash so both
// panels and the handoff stay on the same colours.
struct ThemeRGB { unsigned char r, g, b; };
struct ThemePal {
    ThemeRGB accent, text, dim, panel, bg, track, sheen;
    unsigned char panel_a;              // panel translucency
    unsigned char tint_top, tint_bot;   // accent wash over each panel
};
ThemePal theme_palette(int idx);
// Overlay themes/<key>/palette.conf onto a built-in palette. The file is a
// strict key=value schema (version, seven #rrggbb colours, three 0..255
// bytes). Any missing, duplicate or malformed field rejects the whole file
// and leaves *out untouched, so a bad token file can never ship a half
// themed UI; scripts/lint-themes.sh checks the files before packaging.
bool load_theme_palette(const std::string& path, ThemePal* out);
// Language autonym — always in its own language ("简体中文", "English", ...).
const char* lang_name(int l);
// Full date line for the ambient panel, locale-aware (weekday/month names).
std::string format_date(const struct tm& t);

// Emulator menu localisation: RetroArch user_language value and PPSSPP
// [General] Language locale string for the active language.
int retro_language();
const char* ppsspp_locale();

} // namespace gameos