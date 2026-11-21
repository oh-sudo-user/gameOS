// gameOS front-end — SFTP client for the network transfer screen.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "netsftp.hpp"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace gameos {
namespace netsftp {
namespace {

constexpr int kConnectTimeoutMs = 10000;
constexpr int kSessionTimeoutMs = 15000;
constexpr uint64_t kSpaceHeadroom = 8ULL * 1024 * 1024;
constexpr size_t kChunk = 64 * 1024;

std::string env_or(const char* name, const char* fallback)
{
    const char* v = getenv(name);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

void set_err(std::string* err, const std::string& s)
{
    if (err) *err = s;
}

bool has_control(const std::string& s)
{
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return true;
    return false;
}

bool valid_remote_name(const std::string& name)
{
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos || name.find('\0') != std::string::npos)
        return false;
    if (has_control(name)) return false;
    return name.size() <= 255;
}

std::string b64(const unsigned char* data, size_t len)
{
    static const char* tab =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = (unsigned)data[i] << 16;
        if (i + 1 < len) v |= (unsigned)data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];
        out += tab[(v >> 18) & 63];
        out += tab[(v >> 12) & 63];
        out += (i + 1 < len) ? tab[(v >> 6) & 63] : '=';
        out += (i + 2 < len) ? tab[v & 63] : '=';
    }
    return out;
}

std::string known_hosts_path()
{
    return state_dir() + "/sftp_known_hosts";
}

std::string store_key(const std::string& host, int port)
{
    std::string h = host;
    if (h.find(':') != std::string::npos) h = "[" + h + "]";
    return h + ":" + std::to_string(port);
}

std::string session_error(LIBSSH2_SESSION* s)
{
    char* msg = nullptr;
    int len = 0;
    int code = libssh2_session_last_error(s, &msg, &len, 0);
    if (msg && len > 0) return std::string(msg, (size_t)len);
    return "SSH error " + std::to_string(code);
}

std::string sftp_error(LIBSSH2_SFTP* sftp, LIBSSH2_SESSION* ses)
{
    unsigned long c = sftp ? libssh2_sftp_last_error(sftp) : 0;
    const char* what = "SFTP error";
    switch (c) {
    case LIBSSH2_FX_NO_SUCH_FILE:      what = "no such file or directory"; break;
    case LIBSSH2_FX_PERMISSION_DENIED: what = "permission denied"; break;
    case LIBSSH2_FX_NO_SUCH_PATH:      what = "no such path"; break;
    case LIBSSH2_FX_FILE_ALREADY_EXISTS: what = "file already exists"; break;
    case LIBSSH2_FX_WRITE_PROTECT:     what = "write protected"; break;
    case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM: what = "no space on the server"; break;
    case LIBSSH2_FX_CONNECTION_LOST:   what = "connection lost"; break;
    case LIBSSH2_FX_OP_UNSUPPORTED:    what = "operation not supported"; break;
    default: break;
    }
    return std::string(what) + " (SFTP " + std::to_string(c) + ")" +
           (ses ? ": " + session_error(ses) : "");
}

bool write_all(int fd, const char* data, size_t len)
{
    while (len) {
        ssize_t n = ::write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        data += n;
        len -= (size_t)n;
    }
    return true;
}

bool run_global_init(std::string* err)
{
    static std::once_flag once;
    static int rc = 0;
    std::call_once(once, [] {
        signal(SIGPIPE, SIG_IGN);
        rc = libssh2_init(0);
    });
    if (rc != 0) {
        set_err(err, "libssh2 initialisation failed");
        return false;
    }
    return true;
}

struct Client {
    int fd = -1;
    LIBSSH2_SESSION* ses = nullptr;
    LIBSSH2_SFTP* sftp = nullptr;

    ~Client() { close(); }
    Client() = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void close()
    {
        if (sftp) { libssh2_sftp_shutdown(sftp); sftp = nullptr; }
        if (ses) {
            libssh2_session_disconnect(ses, "gameOS transfer finished");
            libssh2_session_free(ses);
            ses = nullptr;
        }
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

    bool open_socket(const Config& cfg, const std::atomic<bool>* cancel,
                     std::string* err)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        std::string port = std::to_string(cfg.port);
        int gai = getaddrinfo(cfg.host.c_str(), port.c_str(), &hints, &res);
        if (gai != 0) {
            set_err(err, std::string("cannot resolve host: ") + gai_strerror(gai));
            return false;
        }
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(kConnectTimeoutMs);
        bool ok = false;
        for (addrinfo* a = res; a && !ok; a = a->ai_next) {
            int s = ::socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC,
                             a->ai_protocol);
            if (s < 0) continue;
            int fl = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, fl | O_NONBLOCK);
            int r = ::connect(s, a->ai_addr, a->ai_addrlen);
            if (r != 0 && errno != EINPROGRESS) {
                ::close(s);
                continue;
            }
            if (r != 0) {
                bool ready = false;
                while (std::chrono::steady_clock::now() < deadline) {
                    if (cancel && cancel->load()) {
                        set_err(err, "cancelled");
                        ::close(s);
                        freeaddrinfo(res);
                        return false;
                    }
                    pollfd pfd{ s, POLLOUT, 0 };
                    int pr = ::poll(&pfd, 1, 100);
                    if (pr > 0) { ready = true; break; }
                    if (pr < 0 && errno != EINTR) break;
                }
                if (!ready) { ::close(s); continue; }
                int so = 0;
                socklen_t sl = sizeof so;
                if (getsockopt(s, SOL_SOCKET, SO_ERROR, &so, &sl) != 0 || so != 0) {
                    ::close(s);
                    continue;
                }
            }
            fcntl(s, F_SETFL, fl & ~O_NONBLOCK);
            fd = s;
            ok = true;
        }
        freeaddrinfo(res);
        if (!ok) set_err(err, "connection failed");
        return ok;
    }

    bool connect(const Config& cfg, const std::string& password,
                 Progress* p, std::string* err,
                 const std::atomic<bool>* cancel)
    {
        if (!run_global_init(err)) return false;
        if (cancel && cancel->load()) { set_err(err, "cancelled"); return false; }
        if (!open_socket(cfg, cancel, err)) return false;
        ses = libssh2_session_init();
        if (!ses) { set_err(err, "cannot create SSH session"); return false; }
        libssh2_session_set_timeout(ses, kSessionTimeoutMs);
        if (libssh2_session_handshake(ses, fd) != 0) {
            set_err(err, "SSH handshake failed: " + session_error(ses));
            return false;
        }
        size_t key_len = 0;
        int key_type = 0;
        if (!libssh2_session_hostkey(ses, &key_len, &key_type)) {
            set_err(err, "server did not send a host key");
            return false;
        }
        const char* hash = libssh2_hostkey_hash(ses, LIBSSH2_HOSTKEY_HASH_SHA256);
        if (!hash) { set_err(err, "server has no SHA256 host key"); return false; }
        std::string fp = "SHA256:" +
                         b64(reinterpret_cast<const unsigned char*>(hash), 32);
        bool first = false;
        if (!host_key_check(cfg.host, cfg.port, fp, &first, err)) return false;
        if (p) {
            p->first_use = first;
            p->fingerprint = fp;
        }
        if (cancel && cancel->load()) { set_err(err, "cancelled"); return false; }
        if (libssh2_userauth_password(ses, cfg.user.c_str(),
                                      password.c_str()) != 0) {
            set_err(err, "authentication failed: " + session_error(ses));
            return false;
        }
        sftp = libssh2_sftp_init(ses);
        if (!sftp) {
            set_err(err, "cannot start SFTP: " + session_error(ses));
            return false;
        }
        return true;
    }

    bool list(const std::string& path, std::vector<Entry>* out,
              std::string* err)
    {
        LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open_ex(
            sftp, path.c_str(), (unsigned)path.size(), 0, 0,
            LIBSSH2_SFTP_OPENDIR);
        if (!h) {
            set_err(err, "cannot open " + path + ": " + sftp_error(sftp, ses));
            return false;
        }
        std::vector<Entry> ents;
        char name[512];
        char longentry[512];
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        for (;;) {
            int n = libssh2_sftp_readdir_ex(h, name, sizeof name,
                                            longentry, sizeof longentry, &attrs);
            if (n == 0) break;
            if (n < 0) {
                set_err(err, "cannot read " + path + ": " +
                             sftp_error(sftp, ses));
                libssh2_sftp_closedir(h);
                return false;
            }
            std::string nm(name, strnlen(name, sizeof name));
            if (nm == "." || nm == "..") continue;
            if (!valid_remote_name(nm)) continue;
            Entry e;
            e.name = nm;
            e.dir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
                    S_ISDIR(attrs.permissions);
            if (!e.dir && (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE))
                e.size = attrs.filesize > (libssh2_uint64_t)LLONG_MAX
                             ? -1 : (long long)attrs.filesize;
            else if (e.dir)
                e.size = 0;
            else
                e.size = -1;
            ents.push_back(std::move(e));
        }
        libssh2_sftp_closedir(h);
        std::sort(ents.begin(), ents.end(), [](const Entry& a, const Entry& b) {
            if (a.dir != b.dir) return a.dir;
            return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        *out = std::move(ents);
        return true;
    }

    bool download(const std::string& remote,
                  const std::function<void(uint64_t, uint64_t, double, double)>&
                      progress_cb,
                  const std::atomic<bool>* cancel, std::string* local_out,
                  std::string* err)
    {
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        if (libssh2_sftp_stat(sftp, remote.c_str(), &attrs) != 0) {
            set_err(err, "cannot stat " + remote + ": " + sftp_error(sftp, ses));
            return false;
        }
        if (!(attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ||
            !S_ISREG(attrs.permissions)) {
            set_err(err, "not a regular file");
            return false;
        }
        if (!(attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)) {
            set_err(err, "the server did not report the file size");
            return false;
        }
        if (attrs.filesize > (libssh2_uint64_t)LLONG_MAX) {
            set_err(err, "file is too large");
            return false;
        }
        uint64_t size = (uint64_t)attrs.filesize;

        std::string leaf;
        if (!sanitize_leaf(remote.substr(remote.rfind('/') + 1), &leaf)) {
            set_err(err, "remote file name is not safe for local storage");
            return false;
        }
        std::string dest = dest_dir();
        if (mkdir(dest.c_str(), 0755) != 0 && errno != EEXIST) {
            set_err(err, dest + ": " + strerror(errno));
            return false;
        }
        std::string final;
        if (!unique_local_path(dest, leaf, &final, err)) return false;
        std::string part = final + ".part";

        uint64_t offset = 0;
        struct stat st{};
        if (stat(part.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
            offset = (uint64_t)st.st_size;
        if (offset > size) offset = 0;

        uint64_t freeb = 0;
        if (!free_bytes(dest, &freeb, err)) return false;
        if (freeb < size - offset + kSpaceHeadroom) {
            char b[160];
            snprintf(b, sizeof b,
                     "not enough space: need %llu MiB, %llu MiB free",
                     (unsigned long long)((size - offset + kSpaceHeadroom +
                                          1024 * 1024 - 1) / (1024 * 1024)),
                     (unsigned long long)(freeb / (1024 * 1024)));
            set_err(err, b);
            return false;
        }

        if (cancel && cancel->load()) { set_err(err, "cancelled"); return false; }
        LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open_ex(
            sftp, remote.c_str(), (unsigned)remote.size(), LIBSSH2_FXF_READ, 0,
            LIBSSH2_SFTP_OPENFILE);
        if (!h) {
            set_err(err, "cannot open " + remote + ": " + sftp_error(sftp, ses));
            return false;
        }
        if (offset) libssh2_sftp_seek64(h, offset);
        int out = ::open(part.c_str(), O_WRONLY | O_CREAT |
                         (offset ? 0 : O_TRUNC), 0644);
        if (out < 0) {
            set_err(err, part + ": " + strerror(errno));
            libssh2_sftp_close(h);
            return false;
        }
        if (offset && lseek(out, (off_t)offset, SEEK_SET) != (off_t)offset) {
            set_err(err, part + ": " + strerror(errno));
            ::close(out);
            libssh2_sftp_close(h);
            return false;
        }

        auto started = std::chrono::steady_clock::now();
        uint64_t resume_at = offset;
        if (progress_cb) progress_cb(offset, size, 0, -1);
        std::vector<char> buf(kChunk);
        bool ok = true;
        bool cancelled = false;
        while (ok) {
            if (cancel && cancel->load()) { cancelled = true; ok = false; break; }
            ssize_t n = libssh2_sftp_read(h, buf.data(), buf.size());
            if (n < 0) {
                set_err(err, "read failed: " + sftp_error(sftp, ses));
                ok = false;
                break;
            }
            if (n == 0) break;
            if (!write_all(out, buf.data(), (size_t)n)) {
                set_err(err, part + ": " + strerror(errno));
                ok = false;
                break;
            }
            offset += (uint64_t)n;
            if (offset > size) {
                set_err(err, "remote file grew during the transfer");
                ok = false;
                break;
            }
            if (progress_cb) {
                auto now = std::chrono::steady_clock::now();
                double sec = std::chrono::duration<double>(now - started).count();
                double speed = sec > 0.01
                    ? (double)(offset - resume_at) / sec : 0;
                double eta = speed > 1
                    ? (double)(size - offset) / speed : -1;
                progress_cb(offset, size, speed, eta);
            }
        }
        if (ok && offset != size) {
            set_err(err, "remote file size changed during the transfer");
            ok = false;
        }
        if (ok && fsync(out) != 0) {
            set_err(err, part + ": " + strerror(errno));
            ok = false;
        }
        if (::close(out) != 0 && ok) {
            set_err(err, part + ": " + strerror(errno));
            ok = false;
        }
        libssh2_sftp_close(h);
        if (!ok) {
            if (cancelled) set_err(err, "cancelled");
            return false;             // .part stays: the next try resumes it
        }
        if (rename(part.c_str(), final.c_str()) != 0) {
            set_err(err, final + ": " + strerror(errno));
            return false;
        }
        if (progress_cb) progress_cb(size, size, 0, 0);
        if (local_out) *local_out = final;
        return true;
    }
};

} // namespace

std::string state_dir()
{
    return env_or("GAMEOS_NET_ROOT", "/userdata/system");
}

std::string dest_dir()
{
    return env_or("GAMEOS_NET_DEST", "/userdata/network");
}

bool clean_remote_path(const std::string& in, std::string* out,
                       std::string* err)
{
    if (err) err->clear();
    if (in.empty() || in[0] != '/') {
        set_err(err, "remote path must be absolute");
        return false;
    }
    if (in.find('\0') != std::string::npos || has_control(in)) {
        set_err(err, "remote path contains control characters");
        return false;
    }
    if (in.size() > 1024) {
        set_err(err, "remote path is too long");
        return false;
    }
    std::string norm = "/";
    size_t i = 0;
    while (i < in.size()) {
        while (i < in.size() && in[i] == '/') i++;
        size_t j = in.find('/', i);
        if (j == std::string::npos) j = in.size();
        if (i < j) {
            std::string seg = in.substr(i, j - i);
            if (seg == "." || seg == "..") {
                set_err(err, "remote path may not contain . or ..");
                return false;
            }
            if (norm.size() > 1) norm += '/';
            norm += seg;
        }
        i = j;
    }
    if (out) *out = norm;
    return true;
}

bool join_remote(const std::string& dir, const std::string& name,
                 std::string* out, std::string* err)
{
    std::string base;
    if (!clean_remote_path(dir, &base, err)) return false;
    if (!valid_remote_name(name)) {
        set_err(err, "invalid remote file name");
        return false;
    }
    std::string p = base == "/" ? "/" + name : base + "/" + name;
    if (out) *out = p;
    return true;
}

std::string parent_remote(const std::string& path)
{
    std::string p;
    std::string err;
    if (!clean_remote_path(path, &p, &err) || p == "/") return "/";
    size_t slash = p.rfind('/');
    if (slash == 0 || slash == std::string::npos) return "/";
    return p.substr(0, slash);
}

bool sanitize_leaf(const std::string& in, std::string* out)
{
    if (in.empty() || in == "." || in == "..") return false;
    if (in.size() > 200) return false;
    if (in.find('/') != std::string::npos ||
        in.find('\\') != std::string::npos ||
        in.find('\0') != std::string::npos) return false;
    if (has_control(in)) return false;
    if (out) *out = in;
    return true;
}

bool host_key_check(const std::string& host, int port,
                    const std::string& fingerprint, bool* first_use,
                    std::string* err)
{
    if (err) err->clear();
    std::string key = store_key(host, port);
    std::string path = known_hosts_path();
    std::string data;
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        bool bad = ferror(f) != 0;
        fclose(f);
        if (bad) {
            set_err(err, "cannot read " + path);
            return false;
        }
    } else if (errno != ENOENT) {
        set_err(err, "cannot read " + path + ": " + strerror(errno));
        return false;
    }
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        std::string line = data.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos) continue;
        std::string k = line.substr(0, sp);
        if (k != key) continue;
        size_t fp0 = line.find_first_not_of(" \t", sp);
        std::string stored = fp0 == std::string::npos ? "" : line.substr(fp0);
        if (stored != fingerprint) {
            set_err(err, "host key changed for " + key +
                         " (expected " + stored + ", got " + fingerprint + ")");
            return false;
        }
        if (first_use) *first_use = false;
        return true;
    }
    std::string line = key + " " + fingerprint + "\n";
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        set_err(err, "cannot remember host key in " + path + ": " +
                     strerror(errno));
        return false;
    }
    bool ok = write_all(fd, line.data(), line.size()) && fsync(fd) == 0;
    int saved = errno;
    ::close(fd);
    if (!ok) {
        set_err(err, "cannot write " + path + ": " + strerror(saved));
        return false;
    }
    if (first_use) *first_use = true;
    return true;
}

bool free_bytes(const std::string& dir, uint64_t* out, std::string* err)
{
    if (err) err->clear();
    struct statvfs sv{};
    if (statvfs(dir.c_str(), &sv) != 0) {
        set_err(err, dir + ": " + strerror(errno));
        return false;
    }
    uint64_t avail = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
    if (sv.f_frsize && avail / sv.f_frsize != sv.f_bavail) avail = UINT64_MAX;
    if (out) *out = avail;
    return true;
}

bool unique_local_path(const std::string& dir, const std::string& leaf,
                       std::string* out, std::string* err)
{
    if (err) err->clear();
    std::string safe;
    if (!sanitize_leaf(leaf, &safe)) {
        set_err(err, "invalid local file name");
        return false;
    }
    size_t slash = dir.rfind('/');
    if (dir.empty() || dir[0] != '/' || slash == dir.size() - 1) {
        set_err(err, "invalid destination directory");
        return false;
    }
    size_t ext_at = safe.find_last_of('.');
    std::string stem = safe, ext;
    if (ext_at != std::string::npos && ext_at > 0) {
        stem = safe.substr(0, ext_at);
        ext = safe.substr(ext_at);
    }
    for (int n = 0; n < 1000; n++) {
        std::string name = n == 0 ? safe
            : stem + " (" + std::to_string(n) + ")" + ext;
        std::string candidate = dir + "/" + name;
        if (candidate.size() > 4096) {
            set_err(err, "local path is too long");
            return false;
        }
        if (::access(candidate.c_str(), F_OK) != 0) {
            if (errno != ENOENT) {
                set_err(err, candidate + ": " + strerror(errno));
                return false;
            }
            if (out) *out = candidate;
            return true;
        }
    }
    set_err(err, "too many files with that name");
    return false;
}

bool load_config(Config* out)
{
    if (!out) return false;
    Config cfg;
    std::string path = state_dir() + "/sftp.ini";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return true;             // first run: defaults
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "host") cfg.host = v;
        else if (k == "user") cfg.user = v;
        else if (k == "path") cfg.path = v;
        else if (k == "port") {
            int p = atoi(v.c_str());
            if (p >= 1 && p <= 65535) cfg.port = p;
        }
    }
    fclose(f);
    std::string norm;
    if (clean_remote_path(cfg.path, &norm, nullptr)) cfg.path = norm;
    if (cfg.port < 1 || cfg.port > 65535) cfg.port = 22;
    *out = cfg;
    return true;
}

bool save_config(const Config& cfg)
{
    std::string path = state_dir() + "/sftp.ini";
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fprintf(f, "host=%s\nport=%d\nuser=%s\npath=%s\n",
                      cfg.host.c_str(), cfg.port, cfg.user.c_str(),
                      cfg.path.c_str()) > 0;
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        unlink(tmp.c_str());
        return false;
    }
    chmod(tmp.c_str(), 0600);
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool valid_config(const Config& cfg, std::string* err)
{
    if (err) err->clear();
    if (cfg.host.empty() || cfg.host.size() > 255 ||
        has_control(cfg.host) || cfg.host.find(' ') != std::string::npos) {
        set_err(err, "invalid host");
        return false;
    }
    if (cfg.port < 1 || cfg.port > 65535) {
        set_err(err, "invalid port");
        return false;
    }
    if (cfg.user.empty() || cfg.user.size() > 64 ||
        has_control(cfg.user) || cfg.user.find(' ') != std::string::npos) {
        set_err(err, "invalid user name");
        return false;
    }
    std::string norm;
    if (!clean_remote_path(cfg.path, &norm, err)) return false;
    return true;
}

struct Task::Impl {
    mutable std::mutex mx;
    Progress progress;
    std::atomic<bool> cancel{false};
    std::atomic<bool> busy{false};
    std::thread worker;

    ~Impl() { shutdown(); }

    void shutdown()
    {
        cancel.store(true);
        if (worker.joinable()) worker.join();
        busy.store(false);
    }

    bool start(Op op, const Config& cfg, const std::string& password,
               const std::string& path)
    {
        if (busy.load()) return false;
        if (worker.joinable()) worker.join();
        cancel.store(false);
        {
            std::lock_guard<std::mutex> lk(mx);
            progress = Progress{};
            progress.op = op;
            progress.running = true;
            progress.remote_path = path;
            progress.total = 0;
        }
        busy.store(true);
        worker = std::thread([this, op, cfg, password, path] {
            Progress local;
            local.op = op;
            local.remote_path = path;
            std::string err;
            bool ok = false;
            {
                Client c;
                if (c.connect(cfg, password, &local, &err, &cancel)) {
                    if (op == Op::List) {
                        ok = c.list(path, &local.entries, &err);
                    } else {
                        auto update = [this](uint64_t done, uint64_t total,
                                             double speed, double eta) {
                            std::lock_guard<std::mutex> lk(mx);
                            progress.done = done;
                            progress.total = total;
                            progress.speed = speed;
                            progress.eta = eta;
                        };
                        ok = c.download(path, update, &cancel,
                                        &local.local_path, &err);
                    }
                }
            }
            std::lock_guard<std::mutex> lk(mx);
            local.running = false;
            local.ok = ok;
            local.cancelled = !ok && cancel.load();
            if (!ok) local.error = err;
            progress = std::move(local);
            busy.store(false);
        });
        return true;
    }
};

Task::Task() : impl_(new Impl) {}
Task::~Task() = default;

bool Task::start_list(const Config& cfg, const std::string& password,
                      const std::string& path)
{
    std::string norm, err;
    if (!clean_remote_path(path, &norm, &err)) norm = "/";
    return impl_->start(Op::List, cfg, password, norm);
}

bool Task::start_download(const Config& cfg, const std::string& password,
                          const std::string& remote)
{
    std::string norm, err;
    if (!clean_remote_path(remote, &norm, &err) || norm == "/") return false;
    return impl_->start(Op::Download, cfg, password, norm);
}

void Task::cancel()
{
    impl_->cancel.store(true);
}

bool Task::busy() const
{
    return impl_->busy.load();
}

Progress Task::snapshot() const
{
    std::lock_guard<std::mutex> lk(impl_->mx);
    Progress p = impl_->progress;
    p.running = impl_->busy.load();
    return p;
}

void Task::wait()
{
    if (impl_->worker.joinable()) impl_->worker.join();
}

} // namespace netsftp
} // namespace gameos
