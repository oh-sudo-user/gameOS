// gameOS front-end — safe file management and ZIP extraction.
// SPDX-License-Identifier: GPL-3.0-or-later
#define _FILE_OFFSET_BITS 64
#include "fileops.hpp"

#include <zlib.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

namespace gameos {
namespace fsops {
namespace {

constexpr uint64_t kMaxZipTotal = 16ull << 30;   // uncompressed, per archive
constexpr uint64_t kMaxZipEntry = 8ull << 30;
constexpr int kMaxZipEntries = 4096;
constexpr uint64_t kMaxZipCd = 32ull << 20;
constexpr uint64_t kCopyChunk = 128 * 1024;

void set_err(std::string* err, const std::string& s)
{
    if (err) *err = s;
}

bool nocase_less(const std::string& a, const std::string& b)
{
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        int la = (ca >= 'A' && ca <= 'Z') ? ca + 32 : ca;
        int lb = (cb >= 'A' && cb <= 'Z') ? cb + 32 : cb;
        if (la != lb) return la < lb;
    }
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}

bool path_has_dotdot(const std::string& p)
{
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        if (j - i == 2 && p[i] == '.' && p[i + 1] == '.') return true;
        i = j + 1;
    }
    return false;
}

std::string dirname_of(const std::string& p)
{
    size_t s = p.find_last_of('/');
    if (s == std::string::npos) return ".";
    if (s == 0) return "/";
    return p.substr(0, s);
}

bool write_all(int fd, const void* buf, size_t n)
{
    const char* p = (const char*)buf;
    while (n) {
        ssize_t r = write(fd, p, n);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += r;
        n -= (size_t)r;
    }
    return true;
}

bool tree_size(const std::string& path, uint64_t* total, std::string* err)
{
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        set_err(err, path + ": " + std::strerror(errno));
        return false;
    }
    if (S_ISLNK(st.st_mode)) {
        set_err(err, path + ": symbolic links are not supported");
        return false;
    }
    if (S_ISREG(st.st_mode)) {
        *total += (uint64_t)st.st_size;
        return true;
    }
    if (!S_ISDIR(st.st_mode)) {
        set_err(err, path + ": unsupported file type");
        return false;
    }
    DIR* d = opendir(path.c_str());
    if (!d) {
        set_err(err, path + ": " + std::strerror(errno));
        return false;
    }
    struct dirent* e;
    bool ok = true;
    while ((e = readdir(d))) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        if (!tree_size(path + "/" + e->d_name, total, err)) { ok = false; break; }
    }
    closedir(d);
    return ok;
}

bool copy_file_data(const std::string& src, const std::string& dst,
                    uint64_t* done, uint64_t total,
                    const std::atomic<bool>* cancel,
                    const std::function<void(uint64_t, uint64_t)>& cb,
                    std::string* err)
{
    int in = open(src.c_str(), O_RDONLY);
    if (in < 0) {
        set_err(err, src + ": " + std::strerror(errno));
        return false;
    }
    std::string tmp = dst + ".gameos-part-" + std::to_string((long)getpid()) +
                      "-" + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
    int out = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (out < 0) {
        set_err(err, tmp + ": " + std::strerror(errno));
        close(in);
        return false;
    }
    std::vector<char> buf(kCopyChunk);
    bool ok = true;
    for (;;) {
        if (cancel && cancel->load()) {
            set_err(err, "cancelled");
            ok = false;
            break;
        }
        ssize_t r = read(in, buf.data(), buf.size());
        if (r < 0) {
            if (errno == EINTR) continue;
            set_err(err, src + ": " + std::strerror(errno));
            ok = false;
            break;
        }
        if (r == 0) break;
        if (!write_all(out, buf.data(), (size_t)r)) {
            set_err(err, tmp + ": " + std::strerror(errno));
            ok = false;
            break;
        }
        *done += (uint64_t)r;
        if (cb) cb(*done, total);
    }
    if (ok && fsync(out) != 0) {
        set_err(err, tmp + ": " + std::strerror(errno));
        ok = false;
    }
    if (close(out) != 0 && ok) {
        set_err(err, tmp + ": " + std::strerror(errno));
        ok = false;
    }
    close(in);
    if (!ok) {
        unlink(tmp.c_str());
        return false;
    }
    if (rename(tmp.c_str(), dst.c_str()) != 0) {
        set_err(err, dst + ": " + std::strerror(errno));
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool copy_tree_inner(const std::string& src, const std::string& dst,
                     uint64_t* done, uint64_t total,
                     const std::atomic<bool>* cancel,
                     const std::function<void(uint64_t, uint64_t)>& cb,
                     std::string* err)
{
    if (cancel && cancel->load()) {
        set_err(err, "cancelled");
        return false;
    }
    struct stat st;
    if (lstat(src.c_str(), &st) != 0) {
        set_err(err, src + ": " + std::strerror(errno));
        return false;
    }
    if (S_ISLNK(st.st_mode)) {
        set_err(err, src + ": symbolic links are not supported");
        return false;
    }
    if (S_ISREG(st.st_mode)) {
        if (!copy_file_data(src, dst, done, total, cancel, cb, err)) return false;
        return true;
    }
    if (!S_ISDIR(st.st_mode)) {
        set_err(err, src + ": unsupported file type");
        return false;
    }
    if (mkdir(dst.c_str(), 0755) != 0) {
        set_err(err, dst + ": " + std::strerror(errno));
        return false;
    }
    DIR* d = opendir(src.c_str());
    if (!d) {
        set_err(err, src + ": " + std::strerror(errno));
        return false;
    }
    struct dirent* e;
    bool ok = true;
    while ((e = readdir(d))) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        if (!copy_tree_inner(src + "/" + e->d_name, dst + "/" + e->d_name,
                             done, total, cancel, cb, err)) {
            ok = false;
            break;
        }
    }
    closedir(d);
    return ok;
}

// ---------------------------------------------------------------------------
// ZIP readers
// ---------------------------------------------------------------------------
uint16_t rd16(const unsigned char* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
uint32_t rd32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool file_range(FILE* f, uint64_t off, void* buf, size_t n)
{
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) return false;
    return fread(buf, 1, n, f) == n;
}

struct ZipEntry {
    std::string name;
    bool dir = false;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint64_t csize = 0;
    uint64_t usize = 0;
    uint64_t local = 0;
};

bool zip_name(const std::string& raw, bool raw_is_dir, std::string* out,
              std::string* err)
{
    std::string rel;
    if (!clean_rel(raw, &rel, err)) return false;
    if (rel.empty()) {
        set_err(err, "ZIP entry has an empty name");
        return false;
    }
    // A path ending in '/' is a directory even if its unix mode says regular.
    *out = rel;
    (void)raw_is_dir;
    return true;
}

bool parse_zip(FILE* f, uint64_t fsize, std::vector<ZipEntry>* out,
               std::string* err)
{
    if (fsize < 22) {
        set_err(err, "ZIP is too short");
        return false;
    }
    uint64_t tail_len = std::min<uint64_t>(fsize, 65557);
    std::vector<unsigned char> tail((size_t)tail_len);
    if (!file_range(f, fsize - tail_len, tail.data(), tail.size())) {
        set_err(err, "cannot read ZIP directory");
        return false;
    }
    long eocd = -1;
    for (long i = (long)tail.size() - 22; i >= 0; i--) {
        if (rd32(&tail[(size_t)i]) != 0x06054b50) continue;
        uint16_t clen = rd16(&tail[(size_t)i + 20]);
        if ((uint64_t)i + 22 + clen == tail.size()) { eocd = i; break; }
    }
    if (eocd < 0) {
        set_err(err, "ZIP end-of-directory not found");
        return false;
    }
    const unsigned char* p = &tail[(size_t)eocd];
    uint16_t disk = rd16(p + 4), cd_disk = rd16(p + 6);
    uint16_t n_disk = rd16(p + 8), n_total = rd16(p + 10);
    uint32_t cd_size = rd32(p + 12), cd_off = rd32(p + 16);
    if (disk != 0 || cd_disk != 0 || n_disk != n_total) {
        set_err(err, "multi-disk ZIP is not supported");
        return false;
    }
    if (n_total == 0xFFFF || cd_size == 0xFFFFFFFF || cd_off == 0xFFFFFFFF) {
        set_err(err, "ZIP64 is not supported");
        return false;
    }
    if (n_total > kMaxZipEntries) {
        set_err(err, "ZIP has too many entries");
        return false;
    }
    uint64_t eocd_pos = fsize - tail_len + (uint64_t)eocd;
    if ((uint64_t)cd_off + cd_size > eocd_pos || cd_size > kMaxZipCd) {
        set_err(err, "ZIP central directory is out of range");
        return false;
    }
    std::vector<unsigned char> cd(cd_size);
    if (cd_size && !file_range(f, cd_off, cd.data(), cd.size())) {
        set_err(err, "cannot read ZIP central directory");
        return false;
    }
    std::set<std::string> names;
    uint64_t total = 0;
    size_t at = 0;
    for (int i = 0; i < (int)n_total; i++) {
        if (at + 46 > cd.size() || rd32(&cd[at]) != 0x02014b50) {
            set_err(err, "ZIP central directory is corrupt");
            return false;
        }
        const unsigned char* q = &cd[at];
        uint16_t flags = rd16(q + 8);
        uint16_t method = rd16(q + 10);
        uint32_t crc = rd32(q + 16);
        uint32_t csize = rd32(q + 20), usize = rd32(q + 24);
        uint16_t nlen = rd16(q + 28), xlen = rd16(q + 30), clen = rd16(q + 32);
        uint16_t disk_start = rd16(q + 34);
        uint32_t ext = rd32(q + 38);
        uint32_t local = rd32(q + 42);
        if (at + 46 + nlen + xlen + clen > cd.size()) {
            set_err(err, "ZIP central directory is truncated");
            return false;
        }
        if (disk_start != 0) {
            set_err(err, "multi-disk ZIP is not supported");
            return false;
        }
        if (flags & 0x0041) {  // encrypted / strong encryption
            set_err(err, "encrypted ZIP is not supported");
            return false;
        }
        if (method != 0 && method != 8) {
            set_err(err, "unsupported ZIP compression method");
            return false;
        }
        if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF || local == 0xFFFFFFFF) {
            set_err(err, "ZIP64 is not supported");
            return false;
        }
        std::string raw((const char*)q + 46, nlen);
        bool is_dir = !raw.empty() && raw.back() == '/';
        std::string name;
        if (!zip_name(raw, is_dir, &name, err)) return false;
        uint16_t mode = (uint16_t)(ext >> 16);
        uint16_t type = mode & 0170000;
        if (type == 0120000) {
            set_err(err, "ZIP contains a symbolic link");
            return false;
        }
        if (type != 0 && type != 0100000 && type != 0040000) {
            set_err(err, "ZIP contains an unsupported special file");
            return false;
        }
        if (is_dir && usize != 0) {
            set_err(err, "ZIP directory entry has data");
            return false;
        }
        if (usize > kMaxZipEntry || total + usize > kMaxZipTotal) {
            set_err(err, "ZIP expands beyond the size limit");
            return false;
        }
        if (usize > (64ull << 20) && (csize == 0 || usize / csize > 400)) {
            set_err(err, "ZIP compression ratio is unsafe");
            return false;
        }
        if (!names.insert(name).second) {
            set_err(err, "ZIP has duplicate entries: " + name);
            return false;
        }
        total += usize;
        ZipEntry e;
        e.name = name;
        e.dir = is_dir;
        e.method = method;
        e.crc = crc;
        e.csize = csize;
        e.usize = usize;
        e.local = local;
        out->push_back(std::move(e));
        at += 46 + nlen + xlen + clen;
    }
    return true;
}

bool extract_member(FILE* f, uint64_t fsize, const ZipEntry& e,
                    const std::string& dst, uint64_t* done,
                    const std::atomic<bool>* cancel,
                    const std::function<void(const ZipProgress&)>& cb,
                    ZipProgress* zp, std::string* err)
{
    unsigned char lh[30];
    if (e.local + 30 > fsize || !file_range(f, e.local, lh, sizeof(lh)) ||
        rd32(lh) != 0x04034b50) {
        set_err(err, "ZIP local header is corrupt");
        return false;
    }
    uint16_t nlen = rd16(lh + 26), xlen = rd16(lh + 28);
    uint64_t data_off = e.local + 30 + nlen + xlen;
    if (data_off > fsize || e.csize > fsize - data_off) {
        set_err(err, "ZIP member is truncated");
        return false;
    }
    int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        set_err(err, dst + ": " + std::strerror(errno));
        return false;
    }
    uint32_t crc = crc32(0L, Z_NULL, 0);
    uint64_t produced = 0;
    bool ok = true;
    if (e.method == 0) {
        if (e.csize != e.usize) {
            set_err(err, "ZIP stored member has inconsistent sizes");
            ok = false;
        }
        std::vector<unsigned char> buf(64 * 1024);
        uint64_t left = e.csize, off = data_off;
        while (ok && left) {
            if (cancel && cancel->load()) { set_err(err, "cancelled"); ok = false; break; }
            size_t n = (size_t)std::min<uint64_t>(left, buf.size());
            if (!file_range(f, off, buf.data(), n)) {
                set_err(err, "cannot read ZIP member");
                ok = false;
                break;
            }
            if (!write_all(out, buf.data(), n)) {
                set_err(err, dst + ": " + std::strerror(errno));
                ok = false;
                break;
            }
            crc = crc32(crc, buf.data(), (uInt)n);
            off += n;
            left -= n;
            produced += n;
            *done += n;
            zp->bytes_done = *done;
            if (cb) cb(*zp);
        }
    } else {
        z_stream zs;
        std::memset(&zs, 0, sizeof(zs));
        bool inited = inflateInit2(&zs, -MAX_WBITS) == Z_OK;
        if (!inited) {
            set_err(err, "cannot initialise deflate decoder");
            ok = false;
        }
        std::vector<unsigned char> in(64 * 1024), outbuf(64 * 1024);
        uint64_t left = e.csize, off = data_off;
        int zr = Z_OK;
        while (ok && zr != Z_STREAM_END) {
            if (cancel && cancel->load()) { set_err(err, "cancelled"); ok = false; break; }
            if (zs.avail_in == 0) {
                if (left == 0) {
                    set_err(err, "ZIP deflate stream is truncated");
                    ok = false;
                    break;
                }
                size_t n = (size_t)std::min<uint64_t>(left, in.size());
                if (!file_range(f, off, in.data(), n)) {
                    set_err(err, "cannot read ZIP member");
                    ok = false;
                    break;
                }
                off += n;
                left -= n;
                zs.next_in = in.data();
                zs.avail_in = (uInt)n;
            }
            zs.next_out = outbuf.data();
            zs.avail_out = (uInt)outbuf.size();
            zr = inflate(&zs, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END) {
                set_err(err, "ZIP deflate stream is corrupt");
                ok = false;
                break;
            }
            size_t n = outbuf.size() - zs.avail_out;
            if (n) {
                produced += n;
                if (produced > e.usize) {
                    set_err(err, "ZIP member expands beyond its declared size");
                    ok = false;
                    break;
                }
                if (!write_all(out, outbuf.data(), n)) {
                    set_err(err, dst + ": " + std::strerror(errno));
                    ok = false;
                    break;
                }
                crc = crc32(crc, outbuf.data(), (uInt)n);
                *done += n;
                zp->bytes_done = *done;
                if (cb) cb(*zp);
            }
        }
        if (inited) inflateEnd(&zs);
    }
    if (close(out) != 0 && ok) {
        set_err(err, dst + ": " + std::strerror(errno));
        ok = false;
    }
    if (!ok) return false;
    if (produced != e.usize || crc != e.crc) {
        set_err(err, "ZIP member failed CRC/size check: " + e.name);
        return false;
    }
    return true;
}

} // namespace

std::vector<std::string> default_roots()
{
    return { "/userdata", "/mnt/tf2" };
}

bool clean_rel(const std::string& in, std::string* out, std::string* err)
{
    if (err) err->clear();
    if (in.find('\0') != std::string::npos) {
        set_err(err, "path contains NUL");
        return false;
    }
    std::string s = in;
    std::replace(s.begin(), s.end(), '\\', '/');
    if (!s.empty() && s[0] == '/') {
        set_err(err, "absolute path is not allowed");
        return false;
    }
    if (s.size() >= 2 && s[1] == ':') {
        set_err(err, "drive letter is not allowed");
        return false;
    }
    if (s.size() > 1024) {
        set_err(err, "path is too long");
        return false;
    }
    std::string clean;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        std::string part = s.substr(i, j - i);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                set_err(err, "path traversal is not allowed");
                return false;
            }
            if (part.size() > 255) {
                set_err(err, "path component is too long");
                return false;
            }
            if (!clean.empty()) clean += '/';
            clean += part;
        }
        i = j + 1;
    }
    if (out) *out = clean;
    return true;
}

bool resolve_under(const std::string& root, const std::string& rel,
                   std::string* out, std::string* err)
{
    if (root.empty() || root[0] != '/') {
        set_err(err, "root is not absolute");
        return false;
    }
    std::string inner;
    if (!clean_rel(rel, &inner, err)) return false;
    std::string base = root;
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    if (out) *out = inner.empty() ? base : base + "/" + inner;
    return true;
}

bool within_roots(const std::vector<std::string>& roots,
                  const std::string& path)
{
    if (path.empty() || path[0] != '/' || path_has_dotdot(path)) return false;
    for (const auto& r : roots) {
        if (r.empty() || r[0] != '/') continue;
        std::string base = r;
        while (base.size() > 1 && base.back() == '/') base.pop_back();
        if (path == base) return true;
        if (path.size() > base.size() && path.compare(0, base.size(), base) == 0 &&
            path[base.size()] == '/')
            return true;
    }
    return false;
}

std::vector<Entry> list_dir(const std::string& dir, std::string* err)
{
    std::vector<Entry> out;
    if (err) err->clear();
    DIR* d = opendir(dir.c_str());
    if (!d) {
        set_err(err, dir + ": " + std::strerror(errno));
        return out;
    }
    struct dirent* e;
    while ((e = readdir(d))) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        if (std::strncmp(e->d_name, ".gameos-unzip-", 14) == 0) continue;
        std::string full = dir + "/" + e->d_name;
        struct stat st;
        if (lstat(full.c_str(), &st) != 0) continue;
        Entry ent;
        ent.name = e->d_name;
        ent.dir = S_ISDIR(st.st_mode);
        ent.symlink = S_ISLNK(st.st_mode);
        ent.size = (long long)st.st_size;
        ent.mtime = (long long)st.st_mtime;
        out.push_back(std::move(ent));
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        return nocase_less(a.name, b.name);
    });
    return out;
}

bool make_dir(const std::string& path, std::string* err)
{
    if (mkdir(path.c_str(), 0755) != 0) {
        set_err(err, path + ": " + std::strerror(errno));
        return false;
    }
    return true;
}

bool make_dirs(const std::string& path, std::string* err)
{
    if (path.empty() || path[0] != '/') {
        set_err(err, "path is not absolute");
        return false;
    }
    if (path == "/") return true;
    struct stat st;
    if (lstat(path.c_str(), &st) == 0) {
        if (S_ISLNK(st.st_mode)) {
            set_err(err, path + ": symbolic links are not supported");
            return false;
        }
        if (S_ISDIR(st.st_mode)) return true;
        set_err(err, path + ": not a directory");
        return false;
    }
    std::string parent = dirname_of(path);
    if (parent != path && !make_dirs(parent, err)) return false;
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        set_err(err, path + ": " + std::strerror(errno));
        return false;
    }
    return true;
}

bool rename_path(const std::string& from, const std::string& to,
                 std::string* err)
{
    struct stat st;
    if (lstat(from.c_str(), &st) != 0) {
        set_err(err, from + ": " + std::strerror(errno));
        return false;
    }
    if (S_ISLNK(st.st_mode)) {
        set_err(err, "symbolic links are not supported");
        return false;
    }
    struct stat dst;
    if (lstat(to.c_str(), &dst) == 0) {
        set_err(err, to + ": target already exists");
        return false;
    }
    if (rename(from.c_str(), to.c_str()) != 0) {
        set_err(err, to + ": " + std::strerror(errno));
        return false;
    }
    return true;
}

bool copy_path(const std::string& src, const std::string& dst, bool overwrite,
               const std::atomic<bool>* cancel,
               const std::function<void(uint64_t, uint64_t)>& cb,
               std::string* err)
{
    if (err) err->clear();
    if (src.empty() || dst.empty() || src == dst) {
        set_err(err, "invalid copy paths");
        return false;
    }
    struct stat sst;
    if (lstat(src.c_str(), &sst) != 0) {
        set_err(err, src + ": " + std::strerror(errno));
        return false;
    }
    if (S_ISLNK(sst.st_mode)) {
        set_err(err, "symbolic links are not supported");
        return false;
    }
    struct stat dst_st;
    bool exists = lstat(dst.c_str(), &dst_st) == 0;
    if (S_ISDIR(sst.st_mode)) {
        if (exists) {
            set_err(err, dst + ": target already exists");
            return false;
        }
        uint64_t total = 0;
        if (!tree_size(src, &total, err)) return false;
        uint64_t done = 0;
        if (cb) cb(0, total);
        if (!copy_tree_inner(src, dst, &done, total, cancel, cb, err)) {
            remove_path(dst, nullptr);
            return false;
        }
        return true;
    }
    if (!S_ISREG(sst.st_mode)) {
        set_err(err, "unsupported file type");
        return false;
    }
    if (exists) {
        if (S_ISDIR(dst_st.st_mode)) {
            set_err(err, dst + ": target is a directory");
            return false;
        }
        if (!overwrite) {
            set_err(err, dst + ": target already exists");
            return false;
        }
    }
    uint64_t total = (uint64_t)sst.st_size;
    uint64_t done = 0;
    if (cb) cb(0, total);
    if (!copy_file_data(src, dst, &done, total, cancel, cb, err)) return false;
    return true;
}

bool remove_path(const std::string& path, std::string* err)
{
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        set_err(err, path + ": " + std::strerror(errno));
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (!d) {
            set_err(err, path + ": " + std::strerror(errno));
            return false;
        }
        struct dirent* e;
        bool ok = true;
        while ((e = readdir(d))) {
            if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
            if (!remove_path(path + "/" + e->d_name, err)) { ok = false; break; }
        }
        closedir(d);
        if (!ok) return false;
        if (rmdir(path.c_str()) != 0) {
            set_err(err, path + ": " + std::strerror(errno));
            return false;
        }
        return true;
    }
    if (unlink(path.c_str()) != 0) {
        set_err(err, path + ": " + std::strerror(errno));
        return false;
    }
    return true;
}

bool extract_zip(const std::string& zip, const std::string& dest,
                 const std::atomic<bool>* cancel,
                 const std::function<void(const ZipProgress&)>& cb,
                 std::string* err)
{
    if (err) err->clear();
    struct stat zst;
    if (stat(zip.c_str(), &zst) != 0 || !S_ISREG(zst.st_mode)) {
        set_err(err, zip + ": not a regular file");
        return false;
    }
    if (dest.empty() || dest[0] != '/') {
        set_err(err, "destination is not absolute");
        return false;
    }
    std::string parent = dirname_of(dest);
    struct stat pst;
    if (lstat(parent.c_str(), &pst) != 0 || S_ISLNK(pst.st_mode) ||
        !S_ISDIR(pst.st_mode)) {
        set_err(err, parent + ": destination parent is missing");
        return false;
    }
    struct stat dst_st;
    if (lstat(dest.c_str(), &dst_st) == 0) {
        set_err(err, dest + ": target already exists");
        return false;
    }
    FILE* f = fopen(zip.c_str(), "rb");
    if (!f) {
        set_err(err, zip + ": " + std::strerror(errno));
        return false;
    }
    if (fseeko(f, 0, SEEK_END) != 0) {
        set_err(err, zip + ": seek failed");
        fclose(f);
        return false;
    }
    uint64_t fsize = (uint64_t)ftello(f);
    std::vector<ZipEntry> entries;
    if (!parse_zip(f, fsize, &entries, err)) {
        fclose(f);
        return false;
    }
    uint64_t total = 0;
    for (const auto& e : entries) total += e.usize;
    std::string stage = parent + "/.gameos-unzip-" + std::to_string((long)getpid()) +
                        "-" + std::to_string((unsigned long long)
                        std::chrono::steady_clock::now().time_since_epoch().count());
    if (mkdir(stage.c_str(), 0700) != 0) {
        set_err(err, stage + ": " + std::strerror(errno));
        fclose(f);
        return false;
    }
    ZipProgress zp;
    zp.entries_total = (int)entries.size();
    zp.bytes_total = total;
    if (cb) cb(zp);
    uint64_t done = 0;
    bool ok = true;
    for (const auto& e : entries) {
        if (cancel && cancel->load()) { set_err(err, "cancelled"); ok = false; break; }
        std::string target = stage + "/" + e.name;
        if (e.dir) {
            if (!make_dirs(target, err)) { ok = false; break; }
        } else {
            if (!make_dirs(dirname_of(target), err)) { ok = false; break; }
            if (!extract_member(f, fsize, e, target, &done, cancel, cb,
                                &zp, err)) {
                ok = false;
                break;
            }
        }
        zp.entries_done++;
        if (cb) cb(zp);
    }
    fclose(f);
    if (ok) {
        struct stat now;
        if (lstat(dest.c_str(), &now) == 0) {
            set_err(err, dest + ": target already exists");
            ok = false;
        } else if (rename(stage.c_str(), dest.c_str()) != 0) {
            set_err(err, dest + ": " + std::strerror(errno));
            ok = false;
        }
    }
    if (!ok) {
        std::string ignored;
        remove_path(stage, &ignored);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// TaskQueue
// ---------------------------------------------------------------------------
struct TaskQueue::Impl {
    struct JobDef {
        uint64_t id = 0;
        Job kind = Job::None;
        std::string src, dst;
        bool overwrite = false;
    };

    explicit Impl(std::vector<std::string> r) : roots(std::move(r)) {}

    std::vector<std::string> roots;
    mutable std::mutex mu;
    std::condition_variable cv;
    std::deque<JobDef> q;
    std::thread th;
    bool worker = false;
    bool running = false;
    bool stop = false;
    uint64_t seq = 0;
    std::atomic<bool> cancel{ false };
    Progress last;
    std::chrono::steady_clock::time_point started;

    void publish(const std::function<void(Progress&)>& f)
    {
        std::lock_guard<std::mutex> lk(mu);
        f(last);
    }

    void execute(const JobDef& j)
    {
        std::string err;
        auto t0 = std::chrono::steady_clock::now();
        auto report_bytes = [&](uint64_t done, uint64_t total) {
            auto now = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(now - t0).count();
            publish([&](Progress& p) {
                p.done = done;
                p.total = total;
                p.bytes_done = done;
                p.speed = sec > 0.05 ? (double)done / sec : 0;
                p.eta = (p.speed > 1 && total >= done) ? (total - done) / p.speed : -1;
            });
        };
        auto report_zip = [&](const ZipProgress& z) {
            auto now = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(now - t0).count();
            publish([&](Progress& p) {
                p.entries_done = z.entries_done;
                p.entries_total = z.entries_total;
                p.done = (uint64_t)z.entries_done;
                p.total = (uint64_t)z.entries_total;
                p.bytes_done = z.bytes_done;
                p.speed = sec > 0.05 ? (double)z.bytes_done / sec : 0;
                p.eta = (p.speed > 1 && z.bytes_total >= z.bytes_done)
                            ? (z.bytes_total - z.bytes_done) / p.speed : -1;
            });
        };
        bool ok = false;
        switch (j.kind) {
        case Job::Copy:
            ok = copy_path(j.src, j.dst, j.overwrite, &cancel, report_bytes, &err);
            break;
        case Job::Move: {
            struct stat st;
            if (lstat(j.src.c_str(), &st) != 0) {
                err = j.src + ": " + std::strerror(errno);
                break;
            }
            struct stat dstat;
            bool exists = lstat(j.dst.c_str(), &dstat) == 0;
            if (!exists) {
                if (rename(j.src.c_str(), j.dst.c_str()) == 0) {
                    uint64_t total = 0;
                    if (tree_size(j.dst, &total, nullptr)) report_bytes(total, total);
                    ok = true;
                } else if (errno == EXDEV) {
                    if (copy_path(j.src, j.dst, false, &cancel, report_bytes, &err))
                        ok = remove_path(j.src, &err);
                } else {
                    err = j.dst + ": " + std::strerror(errno);
                }
            } else if (copy_path(j.src, j.dst, j.overwrite, &cancel,
                                 report_bytes, &err)) {
                ok = remove_path(j.src, &err);
            }
            break;
        }
        case Job::Remove:
            ok = remove_path(j.src, &err);
            if (ok) report_bytes(1, 1);
            break;
        case Job::Unzip:
            ok = extract_zip(j.src, j.dst, &cancel, report_zip, &err);
            break;
        default:
            err = "unknown job";
            break;
        }
        std::lock_guard<std::mutex> lk(mu);
        last.running = false;
        last.ok = ok;
        last.cancelled = (!ok && err == "cancelled");
        last.error = ok ? "" : (err.empty() ? "operation failed" : err);
        last.pending = (int)q.size();
        if (ok && last.total) last.done = last.total;
        running = false;
        if (!ok && last.cancelled) last.error.clear();
        cv.notify_all();
    }

    void run()
    {
        std::unique_lock<std::mutex> lk(mu);
        for (;;) {
            if (stop) break;
            if (q.empty()) {
                if (!cv.wait_for(lk, std::chrono::seconds(2),
                                 [&] { return stop || !q.empty(); })) {
                    worker = false;
                    return;
                }
                if (stop) break;
            }
            JobDef j = q.front();
            q.pop_front();
            running = true;
            cancel = false;
            started = std::chrono::steady_clock::now();
            last = Progress{};
            last.id = j.id;
            last.job = j.kind;
            last.src = j.src;
            last.dst = j.dst;
            last.running = true;
            last.pending = (int)q.size();
            lk.unlock();
            execute(j);
            lk.lock();
        }
        worker = false;
    }

    bool enqueue(Job k, const std::string& src, const std::string& dst,
                 bool overwrite, std::string* err)
    {
        if (!within_roots(roots, src)) {
            set_err(err, src + ": outside the allowed roots");
            return false;
        }
        if (!dst.empty() && !within_roots(roots, dst)) {
            set_err(err, dst + ": outside the allowed roots");
            return false;
        }
        for (const auto& r : roots)
            if (src == r) {
                set_err(err, "refusing to operate on the root itself");
                return false;
            }
        std::lock_guard<std::mutex> lk(mu);
        if (!worker) {
            if (th.joinable()) th.join();
            worker = true;
            stop = false;
            th = std::thread([this] { run(); });
        }
        JobDef j;
        j.id = ++seq;
        j.kind = k;
        j.src = src;
        j.dst = dst;
        j.overwrite = overwrite;
        q.push_back(j);
        if (!running) {
            last = Progress{};
            last.id = j.id;
            last.job = k;
            last.src = src;
            last.dst = dst;
            last.pending = (int)q.size();
        }
        cv.notify_all();
        return true;
    }
};

TaskQueue::TaskQueue(std::vector<std::string> roots)
    : impl_(new Impl(std::move(roots)))
{
    roots_ = impl_->roots;
}

TaskQueue::~TaskQueue()
{
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->stop = true;
        impl_->cancel = true;
        impl_->q.clear();
        impl_->cv.notify_all();
    }
    if (impl_->th.joinable()) impl_->th.join();
}

bool TaskQueue::busy() const
{
    std::lock_guard<std::mutex> lk(impl_->mu);
    return impl_->running || !impl_->q.empty();
}

Progress TaskQueue::snapshot() const
{
    std::lock_guard<std::mutex> lk(impl_->mu);
    Progress p = impl_->last;
    p.pending = (int)impl_->q.size();
    return p;
}

bool TaskQueue::enqueue_copy(const std::string& src, const std::string& dst,
                             bool overwrite, std::string* err)
{
    return impl_->enqueue(Job::Copy, src, dst, overwrite, err);
}

bool TaskQueue::enqueue_move(const std::string& src, const std::string& dst,
                             bool overwrite, std::string* err)
{
    return impl_->enqueue(Job::Move, src, dst, overwrite, err);
}

bool TaskQueue::enqueue_remove(const std::string& path, std::string* err)
{
    return impl_->enqueue(Job::Remove, path, "", false, err);
}

bool TaskQueue::enqueue_unzip(const std::string& zip, const std::string& dest,
                              std::string* err)
{
    return impl_->enqueue(Job::Unzip, zip, dest, false, err);
}

void TaskQueue::cancel()
{
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->cancel = true;
    impl_->q.clear();
    impl_->cv.notify_all();
}

void TaskQueue::wait_idle()
{
    std::unique_lock<std::mutex> lk(impl_->mu);
    impl_->cv.wait(lk, [&] { return !impl_->running && impl_->q.empty(); });
}

} // namespace fsops
} // namespace gameos
