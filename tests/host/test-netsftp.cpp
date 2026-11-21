// Host test: SFTP path/name validation, config storage and TOFU host keys.
// No network is required except one connection to a closed local port, which
// checks the error path without depending on an SSH server.
#include "netsftp.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace gameos::netsftp;

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

static std::string read_all(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int main()
{
    char tmpl[] = "/tmp/gameos-netsftp-XXXXXX";
    char* tmp = mkdtemp(tmpl);
    if (!tmp) {
        std::cerr << "FAIL mkdtemp\n";
        return 1;
    }
    fs::path root = fs::path(tmp) / "system";
    fs::path dest = fs::path(tmp) / "network";
    fs::create_directories(root);
    fs::create_directories(dest);
    setenv("GAMEOS_NET_ROOT", root.c_str(), 1);
    setenv("GAMEOS_NET_DEST", dest.c_str(), 1);

    std::string out, err;
    check(clean_remote_path("/", &out, &err) && out == "/", "root path");
    check(clean_remote_path("/roms/", &out, &err) && out == "/roms",
          "trailing slash");
    check(clean_remote_path("//a//b/", &out, &err) && out == "/a/b",
          "duplicate slashes");
    check(!clean_remote_path("roms", &out, &err), "relative path rejected");
    check(!clean_remote_path("/a/../b", &out, &err), "dot-dot rejected");
    check(!clean_remote_path(std::string("/a/\x01"), &out, &err),
          "control byte rejected");
    check(parent_remote("/a/b/c") == "/a/b", "parent nested");
    check(parent_remote("/a") == "/", "parent of top level");
    check(parent_remote("/") == "/", "parent of root");
    check(join_remote("/", "x.nes", &out, &err) && out == "/x.nes",
          "join at root");
    check(join_remote("/roms/", "x.nes", &out, &err) && out == "/roms/x.nes",
          "join nested");
    check(!join_remote("/roms", "..", &out, &err), "join rejects dot-dot");
    check(!join_remote("/roms", "a/b", &out, &err), "join rejects slash");

    check(sanitize_leaf("Mario (USA).nes", &out) && out == "Mario (USA).nes",
          "leaf keeps spaces");
    check(sanitize_leaf("游戏 1.zip", &out), "leaf keeps UTF-8");
    check(!sanitize_leaf("", &out), "empty leaf rejected");
    check(!sanitize_leaf(".", &out), "dot leaf rejected");
    check(!sanitize_leaf("..", &out), "dot-dot leaf rejected");
    check(!sanitize_leaf("a/b", &out), "slash leaf rejected");
    check(!sanitize_leaf("a\\b", &out), "backslash leaf rejected");
    check(!sanitize_leaf(std::string("a\x01"), &out), "control leaf rejected");
    check(!sanitize_leaf(std::string(201, 'a'), &out), "long leaf rejected");

    Config cfg;
    cfg.host = "192.168.1.10";
    cfg.port = 22;
    cfg.user = "gameos";
    cfg.path = "/roms";
    check(valid_config(cfg, &err), "valid config");
    Config bad = cfg;
    bad.host = "two words";
    check(!valid_config(bad, &err), "host with space rejected");
    bad = cfg;
    bad.user.clear();
    check(!valid_config(bad, &err), "empty user rejected");
    bad = cfg;
    bad.port = 0;
    check(!valid_config(bad, &err), "port zero rejected");
    bad = cfg;
    bad.path = "relative";
    check(!valid_config(bad, &err), "relative path rejected in config");

    check(save_config(cfg), "save config");
    Config loaded;
    check(load_config(&loaded), "load config");
    check(loaded.host == cfg.host && loaded.port == cfg.port &&
          loaded.user == cfg.user && loaded.path == cfg.path,
          "config roundtrip");
    fs::path ini = root / "sftp.ini";
    check(fs::exists(ini), "config file exists");
    struct stat st{};
    check(stat(ini.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600,
          "config is mode 0600");
    check(read_all(ini).find("secret") == std::string::npos,
          "config has no password field");

    bool first = false;
    check(host_key_check("192.168.1.10", 22, "SHA256:AAAA", &first, &err),
          "first host key accepted");
    check(first, "first use reported");
    check(host_key_check("192.168.1.10", 22, "SHA256:AAAA", &first, &err),
          "same key accepted");
    check(!first, "second use is not first");
    check(!host_key_check("192.168.1.10", 22, "SHA256:BBBB", &first, &err),
          "changed host key rejected");
    check(!err.empty(), "changed key error explains");
    check(host_key_check("192.168.1.11", 2222, "SHA256:CCCC", &first, &err),
          "different host accepted");
    check(host_key_check("fe80::1", 22, "SHA256:DDDD", &first, &err),
          "IPv6 host accepted");
    std::string hosts = read_all(root / "sftp_known_hosts");
    check(hosts.find("192.168.1.10:22 SHA256:AAAA") != std::string::npos,
          "known host line stored");
    check(hosts.find("[fe80::1]:22 SHA256:DDDD") != std::string::npos,
          "IPv6 known host bracketed");

    fs::create_directories(dest / "Game.nes");
    std::ofstream(dest / "Game (1).nes") << "x";
    check(unique_local_path(dest.string(), "Game.nes", &out, &err) &&
          out == (dest / "Game (2).nes").string(),
          "unique local suffix");
    check(unique_local_path(dest.string(), "Other.tar.gz", &out, &err) &&
          out == (dest / "Other.tar.gz").string(), "unique keeps name");
    std::ofstream(dest / "Other.tar.gz.part") << "partial";
    check(unique_local_path(dest.string(), "Other.tar.gz", &out, &err) &&
          out == (dest / "Other.tar.gz").string(),
          "existing .part resumes same name");
    check(!unique_local_path(dest.string(), "..", &out, &err),
          "unique rejects bad leaf");

    uint64_t fb = 0;
    check(free_bytes(dest.string(), &fb, &err) && fb > 0, "free bytes works");
    check(!free_bytes((dest / "missing").string(), &fb, &err),
          "free bytes missing dir fails");

    Config refused = cfg;
    refused.host = "127.0.0.1";
    refused.port = 1;
    Task task;
    check(task.start_list(refused, "pw", "/"), "start refused connect");
    task.wait();
    auto p = task.snapshot();
    check(!p.running, "task finished");
    check(!p.ok, "refused connect failed");
    check(!p.error.empty(), "refused connect has an error");
    check(task.start_download(refused, "pw", "/x.bin"),
          "download path accepted");
    task.cancel();
    task.wait();
    auto p2 = task.snapshot();
    check(!p2.ok, "cancelled download not ok");

    fs::remove_all(tmp);
    if (failures) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "test-netsftp: " << checks << " checks passed\n";
    return 0;
}
