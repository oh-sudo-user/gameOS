// gameOS front-end — internationalisation (7 languages).
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
    LANG_KO,          // 한국어
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
    T_BIOS_TITLE, T_BIOS_MISSING_FMT, T_BIOS_BODY,
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
    T_COUNT
};

const char* tr(TKey k);

// Localised console display name (keyed by Console::id).
const char* console_name(const char* id);
// Localised theme name (0..3).
const char* theme_name(int idx);
// Language autonym — always in its own language ("简体中文", "English", ...).
const char* lang_name(int l);
// Full date line for the ambient panel, locale-aware (weekday/month names).
std::string format_date(const struct tm& t);

// Emulator menu localisation: RetroArch user_language value and PPSSPP
// [General] Language locale string for the active language.
int retro_language();
const char* ppsspp_locale();

} // namespace gameos