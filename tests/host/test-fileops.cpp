// Host tests for fileops: path guard, copy/move/remove and ZIP safety.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileops.hpp"

#include <zlib.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace gameos::fsops;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::string rd(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static void wr(const std::string& p, const std::string& s)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(s.data(), (std::streamsize)s.size());
}

static bool exists(const std::string& p)
{
    struct stat st;
    return lstat(p.c_str(), &st) == 0;
}

static bool is_dir(const std::string& p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static void put16(std::vector<unsigned char>& v, unsigned x)
{
    v.push_back((unsigned char)(x & 255));
    v.push_back((unsigned char)((x >> 8) & 255));
}
static void put32(std::vector<unsigned char>& v, unsigned long x)
{
    for (int i = 0; i < 4; i++) v.push_back((unsigned char)((x >> (8 * i)) & 255));
}

struct ZEntry {
    std::string name;
    std::string data;
    unsigned method = 0;
    unsigned ext = 0100644u << 16;
    bool encrypt = false;
    std::string mode;           // "corrupt-crc", "unsupported", "truncate"

    ZEntry() = default;
    ZEntry(std::string n, std::string d, unsigned m = 0,
           unsigned e = 0100644u << 16, bool enc = false,
           std::string mo = "")
        : name(std::move(n)), data(std::move(d)), method(m), ext(e),
          encrypt(enc), mode(std::move(mo)) {}
};

static std::vector<unsigned char> deflate_raw(const std::string& in)
{
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                 Z_DEFAULT_STRATEGY);
    std::vector<unsigned char> out;
    std::vector<unsigned char> buf(8192);
    zs.next_in = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();
    int r;
    do {
        zs.next_out = buf.data();
        zs.avail_out = (uInt)buf.size();
        r = deflate(&zs, Z_FINISH);
        size_t n = buf.size() - zs.avail_out;
        out.insert(out.end(), buf.begin(), buf.begin() + (std::ptrdiff_t)n);
    } while (r == Z_OK);
    deflateEnd(&zs);
    return out;
}

static std::vector<unsigned char> make_zip(const std::vector<ZEntry>& entries)
{
    std::vector<unsigned char> out;
    struct Cen { unsigned off, crc, cs, us; std::string name; unsigned method, ext, flags; };
    std::vector<Cen> cen;
    for (const auto& e : entries) {
        unsigned crc = (unsigned)crc32(0, (const Bytef*)e.data.data(),
                                       (uInt)e.data.size());
        std::vector<unsigned char> body;
        unsigned method = e.method;
        if (e.mode == "unsupported") method = 99;
        if (method == 8) body = deflate_raw(e.data);
        else body.assign(e.data.begin(), e.data.end());
        if (e.mode == "corrupt-crc") crc ^= 0xFFFFFFFFu;
        unsigned flags = e.encrypt ? 1u : 0u;
        Cen c;
        c.off = (unsigned)out.size();
        c.crc = crc;
        c.cs = (unsigned)body.size();
        c.us = (unsigned)e.data.size();
        c.name = e.name;
        c.method = method;
        c.ext = e.ext;
        c.flags = flags;
        put32(out, 0x04034b50);
        put16(out, 20); put16(out, flags); put16(out, method);
        put16(out, 0); put16(out, 0x21);
        put32(out, crc); put32(out, c.cs); put32(out, c.us);
        put16(out, (unsigned)e.name.size()); put16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), body.begin(), body.end());
        cen.push_back(c);
    }
    unsigned cd_off = (unsigned)out.size();
    for (const auto& c : cen) {
        put32(out, 0x02014b50);
        put16(out, 0x031E); put16(out, 20); put16(out, c.flags); put16(out, c.method);
        put16(out, 0); put16(out, 0x21);
        put32(out, c.crc); put32(out, c.cs); put32(out, c.us);
        put16(out, (unsigned)c.name.size()); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0); put32(out, c.ext); put32(out, c.off);
        out.insert(out.end(), c.name.begin(), c.name.end());
    }
    unsigned cd_size = (unsigned)out.size() - cd_off;
    put32(out, 0x06054b50);
    put16(out, 0); put16(out, 0);
    put16(out, (unsigned)cen.size()); put16(out, (unsigned)cen.size());
    put32(out, cd_size); put32(out, cd_off); put16(out, 0);
    if (!entries.empty() && entries[0].mode == "truncate") out.resize(out.size() - 8);
    return out;
}

static std::string write_zip(const std::string& path,
                             const std::vector<ZEntry>& entries)
{
    auto z = make_zip(entries);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write((const char*)z.data(), (std::streamsize)z.size());
    return path;
}

static int staging_left(const std::string& dir)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;
    int n = 0;
    struct dirent* e;
    while ((e = readdir(d)))
        if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..") &&
            std::strncmp(e->d_name, ".gameos-unzip-", 14) == 0)
            n++;
    closedir(d);
    return n;
}

int main()
{
    char tmpl[] = "/tmp/gameos-fileops-XXXXXX";
    char* rootp = mkdtemp(tmpl);
    if (!rootp) { std::perror("mkdtemp"); return 2; }
    std::string root = rootp;
    std::string r = root + "/r";
    make_dirs(r, nullptr);

    // ---------------------------------------------------------------- paths
    std::string out, err;
    CHECK(!clean_rel("../x", &out, &err));
    CHECK(!clean_rel("a/../../b", &out, &err));
    CHECK(!clean_rel("/abs", &out, &err));
    CHECK(!clean_rel("C:/x", &out, &err));
    CHECK(!clean_rel("a\\..\\b", &out, &err));
    CHECK(clean_rel("roms/gba/", &out, &err) && out == "roms/gba");
    CHECK(clean_rel("", &out, &err) && out.empty());
    CHECK(resolve_under(r, "a/b", &out, nullptr) && out == r + "/a/b");
    CHECK(within_roots({ r }, r) && within_roots({ r }, r + "/a"));
    CHECK(!within_roots({ r }, root + "/outside"));
    CHECK(!within_roots({ r }, r + "/../etc"));

    // ---------------------------------------------------------------- list
    make_dirs(r + "/zeta", nullptr);
    make_dirs(r + "/Alpha", nullptr);
    wr(r + "/beta.txt", "beta");
    wr(r + "/Afile.txt", "a");
    CHECK(symlink("beta.txt", (r + "/link").c_str()) == 0);
    auto list = list_dir(r, &err);
    CHECK(err.empty());
    CHECK(list.size() == 5);
    if (list.size() == 5) {
        CHECK(list[0].name == "Alpha" && list[0].dir);
        CHECK(list[1].name == "zeta" && list[1].dir);
        CHECK(list[2].name == "Afile.txt" && !list[2].dir);
        CHECK(list[4].name == "link" && list[4].symlink);
    }

    // ---------------------------------------------------------------- fs ops
    CHECK(make_dir(r + "/newdir", &err));
    CHECK(!make_dir(r + "/newdir", &err));
    CHECK(make_dirs(r + "/deep/a/b", &err) && is_dir(r + "/deep/a/b"));
    CHECK(rename_path(r + "/newdir", r + "/renamed", &err));
    CHECK(!rename_path(r + "/renamed", r + "/zeta", &err));

    wr(r + "/src.bin", "source-data");
    CHECK(copy_path(r + "/src.bin", r + "/copy.bin", false, nullptr, nullptr, &err));
    CHECK(rd(r + "/copy.bin") == "source-data");
    CHECK(!copy_path(r + "/src.bin", r + "/copy.bin", false, nullptr, nullptr, &err));
    wr(r + "/src.bin", "new-data");
    CHECK(copy_path(r + "/src.bin", r + "/copy.bin", true, nullptr, nullptr, &err));
    CHECK(rd(r + "/copy.bin") == "new-data");
    CHECK(!copy_path(r + "/link", r + "/linkcopy", false, nullptr, nullptr, &err));

    make_dirs(r + "/tree/sub", nullptr);
    wr(r + "/tree/a.txt", "aa");
    wr(r + "/tree/sub/b.txt", "bbb");
    CHECK(copy_path(r + "/tree", r + "/treecopy", false, nullptr, nullptr, &err));
    CHECK(rd(r + "/treecopy/sub/b.txt") == "bbb");
    CHECK(remove_path(r + "/treecopy", &err) && !exists(r + "/treecopy"));

    std::atomic<bool> cancel{ true };
    CHECK(!copy_path(r + "/src.bin", r + "/cancelled.bin", false, &cancel, nullptr, &err));
    CHECK(!exists(r + "/cancelled.bin"));

    CHECK(rename_path(r + "/copy.bin", r + "/moved.bin", &err));
    CHECK(!exists(r + "/copy.bin") && rd(r + "/moved.bin") == "new-data");

    // ---------------------------------------------------------------- zip ok
    std::string z = write_zip(r + "/good.zip", {
        { "Game.nds", "NDS-DATA", 0 },
        { "sub/readme.txt", "hello zip", 0 },
        { "sub/deflated.txt", std::string(2000, 'x'), 8 },
        { "empty/", "", 0, 0040755u << 16 },
    });
    CHECK(staging_left(r) == 0);
    CHECK(extract_zip(z, r + "/unz", nullptr, nullptr, &err));
    CHECK(rd(r + "/unz/Game.nds") == "NDS-DATA");
    CHECK(rd(r + "/unz/sub/readme.txt") == "hello zip");
    CHECK(rd(r + "/unz/sub/deflated.txt") == std::string(2000, 'x'));
    CHECK(is_dir(r + "/unz/empty"));
    CHECK(!extract_zip(z, r + "/unz", nullptr, nullptr, &err));
    CHECK(!err.empty());

    // ------------------------------------------------------------ zip rejects
    auto bad = [&](const std::string& name, std::vector<ZEntry> es) {
        std::string p = write_zip(r + "/" + name + ".zip", es);
        std::string e;
        bool ok = extract_zip(p, r + "/bad-" + name, nullptr, nullptr, &e);
        if (ok) std::fprintf(stderr, "unexpected success: %s\n", name.c_str());
        CHECK(!ok);
        CHECK(!exists(r + "/bad-" + name));
        CHECK(staging_left(r) == 0);
    };
    bad("trav", { { "../evil.txt", "x", 0 } });
    bad("abs", { { "/etc/evil.txt", "x", 0 } });
    bad("symlink", { { "link", "target", 0, 0120777u << 16 } });
    bad("dupe", { { "a.txt", "1", 0 }, { "a.txt", "2", 0 } });
    bad("crc", { { "a.txt", "payload", 0, 0100644u << 16, false, "corrupt-crc" } });
    bad("enc", { { "a.txt", "payload", 0, 0100644u << 16, true } });
    bad("method", { { "a.txt", "payload", 0, 0100644u << 16, false, "unsupported" } });
    bad("trunc", { { "a.txt", "payload", 0, 0100644u << 16, false, "truncate" } });
    {
        std::atomic<bool> cz{ true };
        std::string p = write_zip(r + "/cancel.zip", { { "a.txt", "abc", 0 } });
        CHECK(!extract_zip(p, r + "/cancel-out", &cz, nullptr, &err));
        CHECK(!exists(r + "/cancel-out"));
        CHECK(staging_left(r) == 0);
    }

    // ---------------------------------------------------------------- queue
    TaskQueue q({ r });
    CHECK(!q.enqueue_copy("/etc/passwd", r + "/nope", false, &err));
    CHECK(q.enqueue_copy(r + "/moved.bin", r + "/qcopy.bin", false, &err));
    q.wait_idle();
    Progress p = q.snapshot();
    CHECK(p.ok && p.job == Job::Copy && !q.busy());
    CHECK(rd(r + "/qcopy.bin") == "new-data");

    CHECK(q.enqueue_move(r + "/qcopy.bin", r + "/qmove.bin", false, &err));
    q.wait_idle();
    CHECK(!exists(r + "/qcopy.bin") && rd(r + "/qmove.bin") == "new-data");

    CHECK(q.enqueue_unzip(z, r + "/qunz", &err));
    q.wait_idle();
    CHECK(rd(r + "/qunz/Game.nds") == "NDS-DATA");
    CHECK(q.snapshot().job == Job::Unzip);

    CHECK(q.enqueue_remove(r + "/qunz", &err));
    q.wait_idle();
    CHECK(!exists(r + "/qunz"));

    CHECK(remove_path(root, &err));
    if (fails) {
        std::fprintf(stderr, "%d/%d checks failed\n", fails, checks);
        return 1;
    }
    std::printf("test-fileops: %d checks passed\n", checks);
    return 0;
}
