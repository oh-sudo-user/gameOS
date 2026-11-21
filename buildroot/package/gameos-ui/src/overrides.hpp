// gameOS front-end — per-game overrides (schema 1).
//
// Stored on the data partition as game_overrides.json, so it survives an OTA
// and can be edited on a PC. Keys are "<system>:<absolute path>|<size>"; the
// ROM header Game Code is not used yet, and moving a file changes its key.
// The store validates the whole document: a broken file is renamed to .bak and
// ignored, while a single bad entry is skipped with a warning.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <vector>

namespace gameos {

struct GameOverride {
    std::string title;      // custom display title (empty = use the file name)
    int perf = -1;          // -1 unset, 0 light, 1 heavy
    int scale_integer = -1; // -1 unset, 0 false, 1 true (video_scale_integer)
    int video_smooth = -1;  // -1 unset, 0 false, 1 true
    int frameskip = -1;     // -1 unset, 0..5 (gpSP only)

    bool any() const
    {
        return !title.empty() || perf >= 0 || scale_integer >= 0 ||
               video_smooth >= 0 || frameskip >= 0;
    }
};

// "<system>:<path>|<size>". A missing/unreadable ROM uses size 0.
std::string override_key(const std::string& sys, const std::string& path);

class OverrideStore {
public:
    explicit OverrideStore(std::string path);

    const std::string& path() const { return path_; }

    // Loads the file. A missing file is not an error. A corrupt document is
    // moved to "<path>.bak" and the store starts empty (returns false).
    // Per-entry problems are appended to warnings and the entry is skipped.
    bool load(std::vector<std::string>* warnings = nullptr);

    // Re-reads only when the file's size or mtime changed. Missing after a
    // present load clears the store (the file was deleted).
    bool reload_if_changed(std::vector<std::string>* warnings = nullptr);

    const GameOverride* find(const std::string& key) const;
    const GameOverride* find(const std::string& sys,
                             const std::string& rom_path) const;

    // Stores the entry and writes the whole document atomically. An empty
    // override removes the entry. On a write failure the in-memory map is
    // restored and false is returned.
    bool put(const std::string& key, const GameOverride& ov);

    const std::map<std::string, GameOverride>& entries() const { return games_; }

private:
    bool save();
    void stamp();
    bool changed() const;

    std::string path_;
    std::map<std::string, GameOverride> games_;
    bool have_stamp_ = false;
    long long mtime_sec_ = 0;
    long long mtime_nsec_ = 0;
    long long size_ = -1;
};

// Process-wide store at /userdata/system/game_overrides.json.
OverrideStore& overrides();

// RetroArch appendconfig lines for the fields that map to core options.
// Returns "" when nothing applies. Callers append it last so it wins.
std::string override_cfg(const GameOverride& ov, const std::string& sys);

} // namespace gameos
