// Host test: console registry and ROM scanning for the NES/SNES/MD expansion.
// The scanner accepts GAMEOS_ROM_ROOT as a temporary first-card root, so this
// test never touches /userdata or a real SD card.
#include "gameos.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace gameos;

static int checks;
static int failures;

static void check(bool ok, const std::string& what)
{
    checks++;
    if (!ok) {
        failures++;
        std::cerr << "FAIL " << what << "\n";
    }
}

static const Console* find_console(const char* id)
{
    for (const auto& c : consoles())
        if (std::string(c.id) == id) return &c;
    return nullptr;
}

static void write_file(const fs::path& path)
{
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f << "rom";
}

static bool has_title(const std::vector<Game>& games, const std::string& title)
{
    return std::any_of(games.begin(), games.end(),
                       [&](const Game& g) { return g.title == title; });
}

int main()
{
    const auto& cons = consoles();
    check(cons.size() == 8, "eight consoles are registered");
    check(find_console("nes") != nullptr, "NES is registered");
    check(find_console("snes") != nullptr, "SNES is registered");
    check(find_console("md") != nullptr, "Mega Drive is registered");
    check(find_console("psp") && find_console("psp")->core == nullptr,
          "PSP remains the standalone core");

    const Console* nes = find_console("nes");
    const Console* snes = find_console("snes");
    const Console* md = find_console("md");
    if (!nes || !snes || !md) return 1;

    check(std::string(nes->core) == "fceumm_libretro.so",
          "NES uses the pinned FCEUmm core");
    check(std::string(snes->core) == "snes9x_libretro.so",
          "SNES uses the pinned Snes9x core");
    check(std::string(md->core) == "genesis_plus_gx_libretro.so",
          "MD uses the pinned Genesis Plus GX core");
    check(std::string(nes->appendcfg) == "nes.cfg", "NES cfg name");
    check(std::string(snes->appendcfg) == "snes.cfg", "SNES cfg name");
    check(std::string(md->appendcfg) == "md.cfg", "MD cfg name");

    for (size_t i = 0; i < cons.size(); i++) {
        check(cons[i].id && *cons[i].id, "console id is non-empty");
        check(!cons[i].exts.empty(), std::string(cons[i].id) + " has extensions");
        if (cons[i].core) {
            check(cons[i].appendcfg && *cons[i].appendcfg,
                  std::string(cons[i].id) + " has a RetroArch config");
        }
        for (size_t j = i + 1; j < cons.size(); j++)
            check(std::string(cons[i].id) != cons[j].id,
                  "console ids are unique");
    }

    const fs::path root = fs::temp_directory_path() /
                          ("gameos-platforms-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "roms");

    // One nested and one flat file per new platform, plus a wrong-extension
    // decoy that must not appear in that console's library.
    write_file(root / "roms/nes/Alpha.nes");
    write_file(root / "roms/nes/Deep/Beta.fds");
    write_file(root / "roms/nes/decoy.txt");
    write_file(root / "roms/snes/Gamma.sfc");
    write_file(root / "roms/snes/Deep/Delta.smc");
    write_file(root / "roms/snes/decoy.txt");
    write_file(root / "roms/md/Epsilon.md");
    write_file(root / "roms/md/Deep/Zeta.gen");
    write_file(root / "roms/md/decoy.txt");
    // Same physical MD image described by two extensions: one library row.
    write_file(root / "roms/md/Sonic.md");
    write_file(root / "roms/md/Sonic.bin");

    setenv("GAMEOS_ROM_ROOT", root.c_str(), 1);
    auto nes_games = scan_games(*nes);
    auto snes_games = scan_games(*snes);
    auto md_games = scan_games(*md);
    unsetenv("GAMEOS_ROM_ROOT");

    check(nes_games.size() == 2, "NES scan returns two games");
    check(has_title(nes_games, "Alpha"), "NES flat ROM found");
    check(has_title(nes_games, "Beta"), "NES nested ROM found");
    check(!has_title(nes_games, "decoy"), "NES ignores a wrong extension");

    check(snes_games.size() == 2, "SNES scan returns two games");
    check(has_title(snes_games, "Gamma"), "SNES SFC found");
    check(has_title(snes_games, "Delta"), "SNES SMC found");
    check(!has_title(snes_games, "decoy"), "SNES ignores a wrong extension");

    check(md_games.size() == 3, "MD scan deduplicates same-stem images");
    check(has_title(md_games, "Epsilon"), "MD MD found");
    check(has_title(md_games, "Zeta"), "MD GEN found");
    check(has_title(md_games, "Sonic"), "MD same-stem image kept once");

    fs::remove_all(root);

    std::cout << "test-platforms: " << checks << " checks, "
              << failures << " failures\n";
    return failures ? 1 : 0;
}
