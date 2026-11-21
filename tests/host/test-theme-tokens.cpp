// Host tests for the theme palette schema (src/i18n.cpp) and the four
// palette.conf files shipped in the rootfs overlay. No SDL, no display.
#include "i18n.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>

using gameos::ThemePal;

static int g_pass = 0;
static int g_fail = 0;

static void ok(const char* name) { g_pass++; std::printf("ok   %s\n", name); }
static void no(const char* name) { g_fail++; std::printf("FAIL %s\n", name); }

static bool same(const ThemePal& a, const ThemePal& b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}

static bool write_file(const std::string& path, const std::string& text)
{
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    bool okw = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return okw;
}

static const char* kValid =
    "# test palette\n"
    "version=1\n"
    "accent=#35d0ba\n"
    "text=#e8ecf7\n"
    "dim=#8a93b2\n"
    "panel=#141b33\n"
    "panel_a=235\n"
    "bg=#0b1020\n"
    "track=#1c263a\n"
    "sheen=#d6fff7\n"
    "tint_top=14\n"
    "tint_bot=26\n";

int main(int argc, char** argv)
{
    char dir[] = "/tmp/gameos-theme-XXXXXX";
    if (!mkdtemp(dir)) {
        std::printf("FAIL mkdtemp\n");
        return 1;
    }
    const std::string base = dir;
    const ThemePal sentinel = gameos::theme_palette(1);

    {
        ThemePal p = sentinel;
        if (!gameos::load_theme_palette(base + "/missing.conf", &p) &&
            same(p, sentinel))
            ok("missing file is rejected");
        else
            no("missing file is rejected");
    }
    {
        if (!gameos::load_theme_palette(base + "/ok.conf", nullptr))
            ok("null output is rejected");
        else
            no("null output is rejected");
    }
    if (write_file(base + "/ok.conf", kValid)) {
        ThemePal p = gameos::theme_palette(2);
        if (gameos::load_theme_palette(base + "/ok.conf", &p) &&
            p.accent.r == 0x35 && p.accent.g == 0xd0 && p.accent.b == 0xba &&
            p.text.r == 0xe8 && p.panel_a == 235 &&
            p.sheen.b == 0xf7 && p.tint_top == 14 && p.tint_bot == 26)
            ok("valid palette overrides every field");
        else
            no("valid palette overrides every field");
    } else {
        no("write ok.conf");
    }
    {
        std::string crlf;
        for (const char* s = kValid; *s; s++)
            crlf += (*s == '\n') ? "\r\n" : std::string(1, *s);
        std::string path = base + "/crlf.conf";
        ThemePal p = sentinel;
        if (write_file(path, crlf) &&
            gameos::load_theme_palette(path, &p) && p.accent.g == 0xd0)
            ok("CRLF line endings are accepted");
        else
            no("CRLF line endings are accepted");
    }

    // Every malformed file must be rejected as a whole and leave the
    // built-in palette untouched.
    auto rejects = [&](const char* name, const std::string& text) {
        std::string path = base + "/bad.conf";
        ThemePal p = sentinel;
        if (write_file(path, text) &&
            !gameos::load_theme_palette(path, &p) && same(p, sentinel))
            ok(name);
        else
            no(name);
    };

    auto without = [&](const char* line) {
        std::string t = kValid;
        std::string needle = std::string(line) + "\n";
        size_t at = t.find(needle);
        if (at != std::string::npos) t.erase(at, needle.size());
        return t;
    };

    rejects("missing version is rejected", without("version=1"));
    rejects("missing tint_bot is rejected", without("tint_bot=26"));
    rejects("bad hex length is rejected",
            std::string(kValid).replace(std::string(kValid).find("#35d0ba"), 7, "#35d0b"));
    rejects("bad hex digit is rejected",
            std::string(kValid).replace(std::string(kValid).find("#35d0ba"), 7, "#35d0bg"));
    rejects("unknown key is rejected", std::string(kValid) + "shadow=#000000\n");
    rejects("duplicate key is rejected", std::string(kValid) + "accent=#000000\n");
    rejects("byte above 255 is rejected",
            std::string(kValid).replace(std::string(kValid).find("panel_a=235"), 11,
                                        "panel_a=256"));
    rejects("negative byte is rejected",
            std::string(kValid).replace(std::string(kValid).find("tint_top=14"), 11,
                                        "tint_top=-1"));
    rejects("unsupported version is rejected",
            std::string(kValid).replace(std::string(kValid).find("version=1"), 9,
                                        "version=2"));
    rejects("line without '=' is rejected",
            std::string(kValid) + "accent #123456\n");

    if (argc > 1) {
        int bad = 0;
        for (int i = 0; i < 4; i++) {
            const std::string path = std::string(argv[1]) + "/" +
                                     gameos::theme_key(i) + "/palette.conf";
            ThemePal p = gameos::theme_palette((i + 1) % 4);
            if (!gameos::load_theme_palette(path, &p) ||
                !same(p, gameos::theme_palette(i))) {
                std::printf("     %s does not match built-in %s\n",
                            path.c_str(), gameos::theme_key(i));
                bad++;
            }
        }
        if (bad == 0)
            ok("shipped palettes match the built-in themes");
        else
            no("shipped palettes match the built-in themes");
    }

    char rm[512];
    std::snprintf(rm, sizeof rm, "rm -rf '%s'", dir);
    if (std::system(rm) != 0) { /* leftovers in /tmp are harmless */ }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
