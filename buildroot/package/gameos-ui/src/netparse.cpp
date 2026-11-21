// gameOS front-end — parsers for the gameos-bt / gameos-wifi helper output.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "netparse.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace gameos {

bool bt_mac_valid(const std::string& m)
{
    if (m.size() != 17) return false;
    for (int i = 0; i < 17; i++) {
        char c = m[i];
        if (i % 3 == 2) { if (c != ':') return false; }
        else if (!isxdigit((unsigned char)c)) return false;
    }
    return true;
}

std::vector<BtDev> parse_bt_devs(const std::string& out)
{
    std::vector<BtDev> devs;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        // MAC \t paired \t connected \t kind \t name
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        size_t t3 = line.find('\t', t2 + 1);
        if (t3 == std::string::npos) continue;
        size_t t4 = line.find('\t', t3 + 1);
        if (t4 == std::string::npos) continue;
        BtDev d;
        d.mac = line.substr(0, t1);
        if (!bt_mac_valid(d.mac)) continue;
        d.paired = line[t1 + 1] == '1';
        d.connected = line[t2 + 1] == '1';
        d.kind = line.substr(t3 + 1, t4 - t3 - 1);
        d.name = line.substr(t4 + 1);
        if (!d.name.empty() && d.name.back() == '\r') d.name.pop_back();
        devs.push_back(std::move(d));
    }
    return devs;
}

std::string last_nonempty_line(const std::string& out)
{
    std::string last, line;
    std::istringstream ss(out);
    while (std::getline(ss, line))
        if (!line.empty() && line != "\r") last = line;
    return last;
}

std::vector<std::string> parse_wifi_known(const std::string& out)
{
    std::vector<std::string> known;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) known.push_back(line);
    }
    return known;
}

bool wifi_known_saved(const std::vector<std::string>& known,
                      const std::string& ssid)
{
    for (const auto& k : known) {
        if (k == ssid) return true;
        if (k.size() > ssid.size() + 1 &&
            k.compare(0, ssid.size(), ssid) == 0 && k[ssid.size()] == '\t')
            return true;
    }
    return false;
}

bool wifi_known_keyed(const std::vector<std::string>& known,
                      const std::string& ssid)
{
    const size_t n = ssid.size();
    for (const auto& k : known) {
        if (k.size() == n + 2 && k.compare(0, n, ssid) == 0 &&
            k[n] == '\t' && k[n + 1] == '1')
            return true;
    }
    return false;
}

WifiStatus parse_wifi_status(const std::string& out)
{
    WifiStatus st;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "state") st.state = v;
        else if (k == "ssid") st.link = v;
        else if (k == "ip") st.ip = v;
    }
    return st;
}

std::vector<WifiNet> parse_wifi_nets(const std::string& out,
                                     const std::vector<std::string>& known)
{
    std::vector<WifiNet> nets;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        WifiNet n;
        n.signal = atoi(line.substr(0, t1).c_str());
        n.secure = line[t1 + 1] == '1';
        n.ssid = line.substr(t2 + 1);
        if (n.ssid.empty()) continue;
        bool dup = false;
        for (auto& e : nets)
            if (e.ssid == n.ssid) { dup = true; if (n.signal > e.signal) e = n; break; }
        if (!dup) nets.push_back(std::move(n));
    }
    std::sort(nets.begin(), nets.end(),
              [](const WifiNet& a, const WifiNet& b) { return a.signal > b.signal; });
    mark_wifi_saved(nets, known);
    return nets;
}

void mark_wifi_saved(std::vector<WifiNet>& nets,
                     const std::vector<std::string>& known)
{
    for (auto& n : nets) {
        n.saved = wifi_known_saved(known, n.ssid);
        n.keyed = wifi_known_keyed(known, n.ssid);
    }
}

} // namespace gameos
