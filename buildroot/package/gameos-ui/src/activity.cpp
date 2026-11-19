// gameOS front-end — play-time statistics.
//
// One line per finished session in /userdata/system/activity.log:
//     <wall_start>\t<seconds>\t<system>\t<title>
// The title is the LAST field and is split off after the first three tabs, so a
// title that itself contains a tab still round-trips. Newlines are folded to
// spaces because they are the record separator.
//
// WHY <seconds> is stored instead of an end timestamp: gameos-sleepd suspends
// the whole system to RAM when the lid closes. CLOCK_REALTIME jumps forward by
// the sleep duration on resume, CLOCK_MONOTONIC does not — so the caller
// measures with CLOCK_MONOTONIC and a closed lid is never counted as playing.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameos.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

namespace gameos {

static const char* ACT_PATH = "/userdata/system/activity.log";
constexpr long long MIN_SESSION = 5;

namespace {

std::vector<PlaySession> g_sessions;   // newest first
bool g_loaded = false;

std::string one_line(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        if (c == '\n' || c == '\r') c = ' ';
    return out;
}

std::string key_of(const std::string& sys, const std::string& title)
{
    return sys + "\t" + title;
}

void load()
{
    if (g_loaded) return;
    g_loaded = true;
    g_sessions.clear();
    std::ifstream f(ACT_PATH);
    std::string line;
    while (std::getline(f, line)) {
        size_t p1 = line.find('\t');
        if (p1 == std::string::npos) continue;
        size_t p2 = line.find('\t', p1 + 1);
        if (p2 == std::string::npos) continue;
        size_t p3 = line.find('\t', p2 + 1);
        if (p3 == std::string::npos) continue;
        PlaySession s;
        s.start   = atoll(line.substr(0, p1).c_str());
        s.seconds = atoll(line.substr(p1 + 1, p2 - p1 - 1).c_str());
        s.sys     = line.substr(p2 + 1, p3 - p2 - 1);
        s.title   = line.substr(p3 + 1);
        if (s.sys.empty() || s.title.empty() || s.seconds <= 0) continue;
        g_sessions.push_back(std::move(s));
    }
    std::stable_sort(g_sessions.begin(), g_sessions.end(),
                     [](const PlaySession& a, const PlaySession& b) {
                         return a.start > b.start;
                     });
}

// Local midnight of an epoch second, so day buckets follow the user's timezone
// (set by the first-boot wizard) rather than UTC.
time_t local_midnight(time_t t)
{
    struct tm tm;
    localtime_r(&t, &tm);
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    return mktime(&tm);
}

} // namespace

void activity_record(const std::string& sys, const std::string& title,
                     long long wall_start, long long seconds)
{
    if (seconds < MIN_SESSION || sys.empty() || title.empty()) return;
    std::ofstream f(ACT_PATH, std::ios::app);
    if (!f) return;
    f << wall_start << '\t' << seconds << '\t' << sys << '\t'
      << one_line(title) << '\n';
    if (!f.good()) return;
    g_loaded = false;
}

std::vector<PlaySession> activity_sessions()
{
    load();
    return g_sessions;
}

long long activity_total()
{
    load();
    long long t = 0;
    for (const auto& s : g_sessions) t += s.seconds;
    return t;
}

std::vector<GameStat> activity_games()
{
    load();
    std::map<std::string, GameStat> by_key;
    // newest first, so the first hit of a key is that game's most recent play
    for (const auto& s : g_sessions) {
        GameStat& g = by_key[key_of(s.sys, s.title)];
        if (g.sessions == 0) {
            g.sys = s.sys;
            g.title = s.title;
            g.last = s.start;
        }
        g.seconds += s.seconds;
        g.sessions++;
    }
    std::vector<GameStat> out;
    out.reserve(by_key.size());
    for (auto& kv : by_key) out.push_back(std::move(kv.second));
    std::sort(out.begin(), out.end(), [](const GameStat& a, const GameStat& b) {
        if (a.seconds != b.seconds) return a.seconds > b.seconds;
        if (a.sys != b.sys) return a.sys < b.sys;
        return a.title < b.title;
    });
    return out;
}

std::vector<long long> activity_daily(int days)
{
    load();
    if (days < 1) days = 1;
    std::vector<long long> out(days, 0);
    time_t today0 = local_midnight(time(nullptr));
    for (const auto& s : g_sessions) {
        time_t day0 = local_midnight((time_t)s.start);
        // Reject future days before rounding: C++ division truncates toward
        // zero, so (today0 - tomorrow0 + 43200) / 86400 is 0, not -1, and a
        // clock that stepped backwards would land tomorrow's play in today.
        if (day0 > today0) continue;
        // round to whole days: a DST day is 23 h or 25 h, not 86400 s
        long long back = (long long)((today0 - day0 + 43200) / 86400);
        if (back >= days) continue;
        out[days - 1 - (int)back] += s.seconds;
    }
    return out;
}

std::vector<Pick> activity_recommend(const std::vector<LibEntry>& lib, int limit)
{
    std::map<std::string, GameStat> by_key;
    for (auto& g : activity_games()) by_key[key_of(g.sys, g.title)] = std::move(g);

    time_t now = time(nullptr);
    std::vector<Pick> out;
    out.reserve(lib.size());
    for (const auto& e : lib) {
        auto it = by_key.find(key_of(e.sys, e.title));
        long long secs = (it == by_key.end()) ? 0 : it->second.seconds;
        int sess       = (it == by_key.end()) ? 0 : it->second.sessions;
        long long idle = 0;
        if (it != by_key.end()) {
            idle = (now - it->second.last) / 86400;
            if (idle < 0) idle = 0;   // clock stepped backwards
        }
        bool fav = fav_contains(e.sys, e.title);

        // Favorites dominate, then discovery, then rediscovery of something
        // untouched for a while. A game barely started ranks above one already
        // played for hours; a current regular still gets a small nudge.
        long long score = fav ? 100 : 0;
        if (sess == 0) {
            score += 60;
        } else {
            score += std::min(idle, 30LL) * 3;
            if (secs < 3600) score += 25;
            else if (idle < 3) score += 10;
        }

        Pick p;
        p.sys = e.sys;
        p.title = e.title;
        p.seconds = secs;
        p.idle_days = (int)idle;
        p.score = score;
        if (fav && sess == 0)      p.reason = PICK_FAV_NEW;
        else if (fav && idle >= 7) p.reason = PICK_FAV_BACK;
        else if (sess == 0)        p.reason = PICK_NEW;
        else if (idle >= 14)       p.reason = PICK_BACK;
        else                       p.reason = PICK_REGULAR;
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const Pick& a, const Pick& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.seconds != b.seconds) return a.seconds < b.seconds;
        if (a.sys != b.sys) return a.sys < b.sys;
        return a.title < b.title;
    });
    if (limit > 0 && (int)out.size() > limit) out.resize(limit);
    return out;
}

} // namespace gameos