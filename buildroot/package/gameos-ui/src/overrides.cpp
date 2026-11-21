// gameOS front-end — per-game overrides (schema 1).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "overrides.hpp"

#include <json-c/json.h>

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gameos {

namespace {

constexpr long long kMaxFile = 256 * 1024;
constexpr size_t kMaxTitle = 128;
constexpr size_t kMaxKey = 1024;

std::string strerror_s(int e)
{
    const char* s = std::strerror(e);
    return std::string(s ? s : "error");
}

bool read_small_file(const std::string& path, std::string& out, std::string& why)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        why = "open: " + strerror_s(errno);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        why = "seek failed";
        fclose(f);
        return false;
    }
    long len = ftell(f);
    if (len < 0 || len > kMaxFile) {
        why = "file too large";
        fclose(f);
        return false;
    }
    rewind(f);
    out.assign((size_t)len, '\0');
    size_t got = len ? fread(&out[0], 1, (size_t)len, f) : 0;
    fclose(f);
    if (got != (size_t)len) {
        why = "short read";
        return false;
    }
    return true;
}

bool parse_field(const json_object* obj, const char* name, json_type type,
                 json_object** out)
{
    if (!json_object_object_get_ex(obj, name, out)) return true; // absent is fine
    return json_object_is_type(*out, type) != 0;
}

void backup_bad(const std::string& path, std::vector<std::string>* warnings,
                const char* why)
{
    const std::string bak = path + ".bak";
    if (rename(path.c_str(), bak.c_str()) == 0) {
        if (warnings)
            warnings->push_back(path + ": " + why + "; moved to " + bak);
    } else if (warnings) {
        warnings->push_back(path + ": " + why + "; backup failed: " +
                            strerror_s(errno));
    }
}

} // namespace

std::string override_key(const std::string& sys, const std::string& path)
{
    struct stat st = {};
    long long size = stat(path.c_str(), &st) == 0 ? (long long)st.st_size : 0;
    return sys + ":" + path + "|" + std::to_string(size);
}

OverrideStore::OverrideStore(std::string path) : path_(std::move(path)) {}

bool OverrideStore::load(std::vector<std::string>* warnings)
{
    games_.clear();

    std::string data, why;
    if (!read_small_file(path_, data, why)) {
        if (access(path_.c_str(), F_OK) != 0) {
            stamp();
            return true;
        }
        backup_bad(path_, warnings, why.c_str());
        stamp();
        return false;
    }

    json_tokener* tok = json_tokener_new();
    json_object* root = json_tokener_parse_ex(tok, data.data(), (int)data.size());
    json_tokener_error err = json_tokener_get_error(tok);
    size_t end = json_tokener_get_parse_end(tok);
    json_tokener_free(tok);

    bool trailing = false;
    for (size_t i = end; i < data.size(); i++) {
        unsigned char c = (unsigned char)data[i];
        if (!isspace(c)) { trailing = true; break; }
    }

    bool bad = err != json_tokener_success || !root || trailing;
    json_object* schema = nullptr;
    json_object* games = nullptr;
    if (!bad) {
        bad = !json_object_is_type(root, json_type_object) ||
              !json_object_object_get_ex(root, "schema", &schema) ||
              !json_object_is_type(schema, json_type_int) ||
              json_object_get_int(schema) != 1 ||
              !json_object_object_get_ex(root, "games", &games) ||
              !json_object_is_type(games, json_type_object);
    }

    if (bad) {
        if (root) json_object_put(root);
        backup_bad(path_, warnings, "invalid document");
        stamp();
        return false;
    }

    json_object_object_foreach(games, key, val) {
        if (strlen(key) > kMaxKey) {
            if (warnings) warnings->push_back(std::string("override: key too long"));
            continue;
        }
        if (!json_object_is_type(val, json_type_object)) {
            if (warnings)
                warnings->push_back(std::string("override ") + key + ": not an object");
            continue;
        }
        GameOverride ov;
        json_object* j = nullptr;
        bool entry_bad = false;

        if (!parse_field(val, "title", json_type_string, &j)) {
            entry_bad = true;
        } else if (j) {
            std::string title = json_object_get_string(j);
            if (title.size() > kMaxTitle)
                entry_bad = true;
            else
                ov.title = title;
        }

        if (!parse_field(val, "perf", json_type_string, &j)) {
            entry_bad = true;
        } else if (j) {
            std::string p = json_object_get_string(j);
            if (p == "powersave") ov.perf = 0;
            else if (p == "balanced" || p == "light") ov.perf = 1;
            else if (p == "performance" || p == "heavy") ov.perf = 2;
            else entry_bad = true;
        }

        if (!parse_field(val, "video_scale_integer", json_type_boolean, &j)) {
            entry_bad = true;
        } else if (j) {
            ov.scale_integer = json_object_get_boolean(j) ? 1 : 0;
        }

        if (!parse_field(val, "video_smooth", json_type_boolean, &j)) {
            entry_bad = true;
        } else if (j) {
            ov.video_smooth = json_object_get_boolean(j) ? 1 : 0;
        }

        if (!parse_field(val, "frameskip", json_type_int, &j)) {
            entry_bad = true;
        } else if (j) {
            int n = json_object_get_int(j);
            if (n < 0 || n > 5) entry_bad = true;
            else ov.frameskip = n;
        }

        if (entry_bad) {
            if (warnings)
                warnings->push_back(std::string("override ") + key + ": ignored");
            continue;
        }
        games_[key] = std::move(ov);
    }

    json_object_put(root);
    stamp();
    return true;
}

bool OverrideStore::reload_if_changed(std::vector<std::string>* warnings)
{
    if (!changed()) return false;
    return load(warnings);
}

const GameOverride* OverrideStore::find(const std::string& key) const
{
    auto it = games_.find(key);
    return it == games_.end() ? nullptr : &it->second;
}

const GameOverride* OverrideStore::find(const std::string& sys,
                                        const std::string& rom_path) const
{
    return find(override_key(sys, rom_path));
}

bool OverrideStore::put(const std::string& key, const GameOverride& ov)
{
    auto it = games_.find(key);
    const bool had = it != games_.end();
    GameOverride old;
    if (had) old = it->second;

    if (ov.any()) games_[key] = ov;
    else games_.erase(key);

    if (save()) return true;

    if (had) games_[key] = old;
    else games_.erase(key);
    return false;
}

bool OverrideStore::save()
{
    json_object* root = json_object_new_object();
    if (!root) return false;
    json_object_object_add(root, "schema", json_object_new_int(1));
    json_object* games = json_object_new_object();
    if (!games) {
        json_object_put(root);
        return false;
    }
    for (const auto& kv : games_) {
        const GameOverride& ov = kv.second;
        json_object* e = json_object_new_object();
        if (!e) {
            json_object_put(games);
            json_object_put(root);
            return false;
        }
        if (!ov.title.empty())
            json_object_object_add(e, "title", json_object_new_string(ov.title.c_str()));
        if (ov.perf >= 0) {
            const char* pn = ov.perf == 0 ? "powersave"
                           : ov.perf == 1 ? "balanced" : "performance";
            json_object_object_add(e, "perf", json_object_new_string(pn));
        }
        if (ov.scale_integer >= 0)
            json_object_object_add(e, "video_scale_integer",
                                   json_object_new_boolean(ov.scale_integer != 0));
        if (ov.video_smooth >= 0)
            json_object_object_add(e, "video_smooth",
                                   json_object_new_boolean(ov.video_smooth != 0));
        if (ov.frameskip >= 0)
            json_object_object_add(e, "frameskip", json_object_new_int(ov.frameskip));
        json_object_object_add(games, kv.first.c_str(), e);
    }
    json_object_object_add(root, "games", games);

    std::string text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    text += "\n";
    json_object_put(root);

    const size_t slash = path_.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        std::string dir = path_.substr(0, slash);
        if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }

    const std::string tmp = path_ + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    size_t off = 0;
    bool ok = true;
    while (off < text.size()) {
        ssize_t n = write(fd, text.data() + off, text.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        off += (size_t)n;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (!ok || rename(tmp.c_str(), path_.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    stamp();
    return true;
}

void OverrideStore::stamp()
{
    struct stat st = {};
    have_stamp_ = true;
    if (stat(path_.c_str(), &st) == 0) {
        mtime_sec_ = (long long)st.st_mtim.tv_sec;
        mtime_nsec_ = (long long)st.st_mtim.tv_nsec;
        size_ = (long long)st.st_size;
    } else {
        mtime_sec_ = 0;
        mtime_nsec_ = 0;
        size_ = -1;
    }
}

bool OverrideStore::changed() const
{
    if (!have_stamp_) return true;
    struct stat st = {};
    if (stat(path_.c_str(), &st) != 0) return size_ >= 0;
    if (size_ < 0) return true;
    return (long long)st.st_size != size_ ||
           (long long)st.st_mtim.tv_sec != mtime_sec_ ||
           (long long)st.st_mtim.tv_nsec != mtime_nsec_;
}

OverrideStore& overrides()
{
    static OverrideStore store("/userdata/system/game_overrides.json");
    return store;
}

std::string override_cfg(const GameOverride& ov, const std::string& sys)
{
    std::string cfg;
    if (ov.scale_integer >= 0)
        cfg += std::string("video_scale_integer = \"") +
               (ov.scale_integer ? "true" : "false") + "\"\n";
    if (ov.video_smooth >= 0)
        cfg += std::string("video_smooth = \"") +
               (ov.video_smooth ? "true" : "false") + "\"\n";
    if (ov.frameskip >= 0 && sys == "gba")
        cfg += "gpsp_frameskip = \"" + std::to_string(ov.frameskip) + "\"\n";
    return cfg;
}

} // namespace gameos
