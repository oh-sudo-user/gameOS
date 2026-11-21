// gameOS front-end — safe file management and ZIP extraction.
//
// All paths handled here are lexical paths under an explicit set of roots
// (/userdata and /mnt/tf2). Symlinks are never followed for writes, ZIP
// entries are validated before a single byte is written, and archives are
// unpacked into a staging directory that is renamed into place only after
// every member passed its CRC check.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gameos {
namespace fsops {

struct Entry {
    std::string name;       // basename, "." / ".." never included
    bool dir = false;
    bool symlink = false;
    long long size = 0;
    long long mtime = 0;
};

std::vector<std::string> default_roots();

// Lexical relative path validation: no absolute path, no drive letter, no
// "..", no NUL. Backslashes are normalised to '/'. Empty means the root.
bool clean_rel(const std::string& in, std::string* out, std::string* err);

// Resolves a validated relative path under root (root must be absolute).
bool resolve_under(const std::string& root, const std::string& rel,
                   std::string* out, std::string* err);

// True only for the roots themselves or paths strictly below one of them.
// The check is lexical; callers resolve paths through resolve_under().
bool within_roots(const std::vector<std::string>& roots,
                  const std::string& path);

// Directory listing: directories first, then files, case-insensitive name
// order. Symlinks are reported (and refused by every mutating operation).
std::vector<Entry> list_dir(const std::string& dir, std::string* err);

bool make_dir(const std::string& path, std::string* err);
bool make_dirs(const std::string& path, std::string* err);
bool rename_path(const std::string& from, const std::string& to,
                 std::string* err);

// Recursive copy / remove with cancellation. `done`/`total` are bytes for
// copy. copy_path does not follow symlinks and never overwrites unless asked.
bool copy_path(const std::string& src, const std::string& dst, bool overwrite,
               const std::atomic<bool>* cancel,
               const std::function<void(uint64_t, uint64_t)>& cb,
               std::string* err);
bool remove_path(const std::string& path, std::string* err);

// ---------------------------------------------------------------------------
// ZIP extraction
// ---------------------------------------------------------------------------
// Supported: stored and deflate members, no encryption, no ZIP64. Names that
// are absolute, contain "..", contain NUL or carry a symlink/device mode are
// rejected. Duplicate names are rejected. `dest` must not exist; the archive
// is unpacked into "<parent>/.gameos-unzip-*" and atomically renamed to
// `dest` after all CRCs pass. On failure or cancellation the staging
// directory is removed and `dest` is never created.
struct ZipProgress {
    int entries_done = 0;
    int entries_total = 0;
    uint64_t bytes_done = 0;
    uint64_t bytes_total = 0;
};
bool extract_zip(const std::string& zip, const std::string& dest,
                 const std::atomic<bool>* cancel,
                 const std::function<void(const ZipProgress&)>& cb,
                 std::string* err);

// ---------------------------------------------------------------------------
// Sequential background queue
// ---------------------------------------------------------------------------
enum class Job { None, Copy, Move, Remove, Unzip };

struct Progress {
    uint64_t id = 0;            // changes for every job
    Job job = Job::None;
    std::string src, dst;
    bool running = false;
    bool ok = false;
    bool cancelled = false;
    int pending = 0;            // jobs waiting behind the running one
    uint64_t done = 0, total = 0;
    int entries_done = 0, entries_total = 0;
    uint64_t bytes_done = 0;
    double speed = 0;           // bytes per second
    double eta = -1;            // seconds, negative when unknown
    std::string error;
};

class TaskQueue {
public:
    explicit TaskQueue(std::vector<std::string> roots);
    ~TaskQueue();
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    const std::vector<std::string>& roots() const { return roots_; }
    bool busy() const;          // running or queued
    Progress snapshot() const;

    bool enqueue_copy(const std::string& src, const std::string& dst,
                      bool overwrite, std::string* err);
    bool enqueue_move(const std::string& src, const std::string& dst,
                      bool overwrite, std::string* err);
    bool enqueue_remove(const std::string& path, std::string* err);
    bool enqueue_unzip(const std::string& zip, const std::string& dest,
                       std::string* err);

    void cancel();
    void wait_idle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> roots_;
};

} // namespace fsops
} // namespace gameos
