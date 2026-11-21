// gameOS front-end — SFTP client for the network transfer screen.
//
// Password authentication only. The password lives in memory for one
// transfer and is never written to disk or to a log. The server host key is
// pinned on first use (TOFU) under /userdata/system; a later mismatch aborts
// the connection instead of trusting the new key.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gameos {
namespace netsftp {

struct Config {
    std::string host;
    int port = 22;
    std::string user;
    std::string path = "/";
};

struct Entry {
    std::string name;       // basename, "." / ".." never included
    bool dir = false;
    long long size = 0;     // -1 when the server did not report it
};

// /userdata/system, or GAMEOS_NET_ROOT for host tests.
std::string state_dir();
// /userdata/network, or GAMEOS_NET_DEST for host tests.
std::string dest_dir();

bool load_config(Config* out);
bool save_config(const Config& cfg);
bool valid_config(const Config& cfg, std::string* err);

// Lexical validation: absolute path, no NUL/control bytes, no "." or ".."
// segments. Returns the normalised path (single slashes, no trailing slash
// except for the root).
bool clean_remote_path(const std::string& in, std::string* out,
                       std::string* err);
bool join_remote(const std::string& dir, const std::string& name,
                 std::string* out, std::string* err);
std::string parent_remote(const std::string& path);

// A single remote name is reduced to a safe local leaf: no separators, no
// control bytes, no "." / "..", length capped for ext4. Returns false for
// anything that cannot be represented safely on the local filesystem.
bool sanitize_leaf(const std::string& in, std::string* out);

// First-use host-key pinning. `fingerprint` is "SHA256:<base64>".
// Fails closed when the store cannot be read or written.
bool host_key_check(const std::string& host, int port,
                    const std::string& fingerprint, bool* first_use,
                    std::string* err);

// Byte counter for the destination filesystem.
bool free_bytes(const std::string& dir, uint64_t* out, std::string* err);

// "name.ext" -> "name (1).ext" when the final or its .part already exists.
// The returned path is absolute and inside dir.
bool unique_local_path(const std::string& dir, const std::string& leaf,
                       std::string* out, std::string* err);

enum class Op { None, List, Download };

struct Progress {
    Op op = Op::None;
    bool running = false;
    bool ok = false;
    bool cancelled = false;
    bool first_use = false;
    std::string fingerprint;
    std::string error;
    std::string remote_path;        // List: directory; Download: file
    std::string local_path;         // Download: final file
    std::vector<Entry> entries;
    uint64_t done = 0;
    uint64_t total = 0;             // 0 when the size is unknown
    double speed = 0;               // bytes per second, average
    double eta = -1;                // seconds, negative when unknown
};

// One background operation at a time. Starting while busy() returns false.
class Task {
public:
    Task();
    ~Task();
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool start_list(const Config& cfg, const std::string& password,
                    const std::string& path);
    bool start_download(const Config& cfg, const std::string& password,
                        const std::string& remote);
    void cancel();
    bool busy() const;
    Progress snapshot() const;
    void wait();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace netsftp
} // namespace gameos
