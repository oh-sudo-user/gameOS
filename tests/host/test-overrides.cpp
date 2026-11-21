// Host tests for per-game overrides: schema, corruption, atomic writes and
// hot reload. SPDX-License-Identifier: GPL-3.0-or-later
#include "overrides.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace gameos;

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

static bool write_all(const std::string& path, const std::string& data)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

static std::string read_all(const std::string& path)
{
    std::string out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

static bool exists(const std::string& path)
{
    return access(path.c_str(), F_OK) == 0;
}

int main()
{
    char tmpl[] = "/tmp/gameos-ov-XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) {
        std::printf("SKIP: cannot create temp dir\n");
        return 0;
    }
    const std::string base = dir;
    const std::string path = base + "/game_overrides.json";
    const std::string rom = base + "/rom.gba";
    write_all(rom, "ROM!");

    // Missing file is a valid empty store.
    {
        OverrideStore st(path);
        std::vector<std::string> w;
        check(st.load(&w), "missing file loads");
        check(st.entries().empty(), "missing file yields no entries");
        check(w.empty(), "missing file has no warnings");
    }

    // Valid document with two good entries and three bad ones.
    write_all(path,
        "{\n"
        " \"schema\": 1,\n"
        " \"games\": {\n"
        "  \"" + override_key("gba", rom) + "\": {\n"
        "   \"title\": \"Custom\", \"perf\": \"heavy\",\n"
        "   \"video_scale_integer\": false, \"video_smooth\": true,\n"
        "   \"frameskip\": 2\n"
        "  },\n"
        "  \"nds:/tmp/other.nds|0\": { \"perf\": \"light\" },\n"
        "  \"not-an-object\": 5,\n"
        "  \"bad-perf\": { \"perf\": \"turbo\" },\n"
        "  \"bad-skip\": { \"frameskip\": 9 }\n"
        " }\n"
        "}\n");
    {
        OverrideStore st(path);
        std::vector<std::string> w;
        check(st.load(&w), "valid document loads");
        check(st.entries().size() == 2, "invalid entries are skipped");
        check(w.size() == 3, "each invalid entry warns");
        const GameOverride* ov = st.find("gba", rom);
        check(ov != nullptr, "entry found by system and path");
        if (ov) {
            check(ov->title == "Custom", "title parsed");
            check(ov->perf == 1, "heavy perf parsed");
            check(ov->scale_integer == 0, "false boolean parsed");
            check(ov->video_smooth == 1, "true boolean parsed");
            check(ov->frameskip == 2, "frameskip parsed");
            check(ov->any(), "override reports fields");
        }
        const GameOverride* light = st.find("nds:/tmp/other.nds|0");
        check(light && light->perf == 0, "entry found by raw key");

        GameOverride cfg_ov;
        cfg_ov.scale_integer = 0;
        cfg_ov.video_smooth = 1;
        cfg_ov.frameskip = 3;
        std::string cfg = override_cfg(cfg_ov, "gba");
        check(cfg.find("video_scale_integer = \"false\"") != std::string::npos,
              "scale maps to RetroArch config");
        check(cfg.find("video_smooth = \"true\"") != std::string::npos,
              "smooth maps to RetroArch config");
        check(cfg.find("gpsp_frameskip = \"3\"") != std::string::npos,
              "GBA frameskip maps to gpSP option");
        check(override_cfg(cfg_ov, "nds").find("frameskip") == std::string::npos,
              "frameskip does not leak to other systems");
        check(override_cfg(GameOverride(), "gba").empty(),
              "empty override produces no config");
    }

    // Saves are atomic, reload cleanly and empty overrides remove entries.
    {
        OverrideStore st(path);
        st.load();
        GameOverride ov;
        ov.title = "Saved";
        ov.perf = 0;
        check(st.put("gba:new|1", ov), "put saves");
        check(!exists(path + ".tmp"), "temporary file is renamed away");
        check(read_all(path).find("\"schema\"") != std::string::npos,
              "saved document carries the schema");

        OverrideStore st2(path);
        check(st2.load(), "saved document reloads");
        const GameOverride* got = st2.find("gba:new|1");
        check(got && got->title == "Saved" && got->perf == 0,
              "saved entry round-trips");
        check(st2.put("gba:new|1", GameOverride()), "empty put removes");
        OverrideStore st3(path);
        st3.load();
        check(st3.find("gba:new|1") == nullptr, "removed entry is gone on disk");
        check(st3.entries().size() == 2, "other entries survive the rewrite");
    }

    // Hot reload reacts to an external edit and stays quiet when unchanged.
    {
        OverrideStore st(path);
        st.load();
        write_all(path,
                  "{\"schema\":1,\"games\":{\"a|1\":{\"title\":\"Edited\"}}}\n");
        std::vector<std::string> w;
        check(st.reload_if_changed(&w), "external edit triggers reload");
        const GameOverride* got = st.find("a|1");
        check(got && got->title == "Edited", "reloaded title visible");
        check(!st.reload_if_changed(&w), "unchanged file does not reload");
    }

    // Corrupt documents are backed up and fall back to empty.
    {
        write_all(path, "{ this is not json\n");
        OverrideStore st(path);
        std::vector<std::string> w;
        check(!st.load(&w), "corrupt document reports failure");
        check(!exists(path), "corrupt file is moved away");
        check(read_all(path + ".bak").find("not json") != std::string::npos,
              "backup keeps the original bytes");
        check(st.entries().empty(), "corrupt document yields defaults");
        check(w.size() == 1, "corrupt document warns once");

        write_all(path, "{\"schema\":2,\"games\":{}}\n");
        OverrideStore st2(path);
        check(!st2.load(), "unknown schema is rejected");
        check(exists(path + ".bak"), "unknown schema is backed up");

        write_all(path, "{\"schema\":1,\"games\":{}} trailing\n");
        OverrideStore st3(path);
        check(!st3.load(), "trailing bytes reject the document");
        check(exists(path + ".bak"), "trailing garbage is backed up");
    }

    // Oversized files are backed up instead of parsed.
    {
        std::string huge(300 * 1024, ' ');
        write_all(path, huge);
        OverrideStore st(path);
        check(!st.load(), "oversized document is rejected");
        check(exists(path + ".bak"), "oversized document is backed up");
    }

    // Key includes the ROM size and tolerates a missing file.
    {
        std::string k1 = override_key("gba", rom);
        check(k1 == "gba:" + rom + "|4", "key carries system, path and size");
        check(override_key("gba", base + "/missing.gba") ==
              "gba:" + base + "/missing.gba|0", "missing ROM uses size 0");
    }

    // Cleanup.
    unlink(path.c_str());
    unlink((path + ".bak").c_str());
    unlink((path + ".tmp").c_str());
    unlink(rom.c_str());
    rmdir(base.c_str());

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
