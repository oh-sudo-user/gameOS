// gameOS front-end — parsers for the gameos-bt / gameos-wifi helper output.
// The helpers print line-oriented text; keeping the parsing free of UI state
// makes it testable on the host and keeps main.cpp focused on interaction.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace gameos {

struct BtDev {
    std::string mac, name, kind;
    bool paired = false, connected = false;
};

struct WifiNet {
    std::string ssid;
    int  signal = 0;      // dBm
    bool secure = false;
    bool saved = false;
    bool keyed = false;   // saved with a non-empty passphrase
};

// "AA:BB:CC:DD:EE:FF"
bool bt_mac_valid(const std::string& mac);

// One device per line: MAC \t paired \t connected \t kind \t name.
// Lines with a bad MAC or missing fields are skipped.
std::vector<BtDev> parse_bt_devs(const std::string& out);

// The last non-empty line, with a trailing CR removed. Helper errors are
// reported on the last line; empty lines in between are ignored.
std::string last_nonempty_line(const std::string& out);

// "SSID<TAB>1" when a passphrase is stored, "SSID<TAB>0" for an open network.
// The bare-SSID form is accepted too so an older helper still works.
std::vector<std::string> parse_wifi_known(const std::string& out);
bool wifi_known_saved(const std::vector<std::string>& known,
                      const std::string& ssid);
bool wifi_known_keyed(const std::vector<std::string>& known,
                      const std::string& ssid);

struct WifiStatus {
    std::string state;   // wpa_state / NOHW
    std::string link;    // associated SSID
    std::string ip;
};
WifiStatus parse_wifi_status(const std::string& out);

// One network per line: signal \t secure \t SSID. Duplicates keep the
// stronger entry and the result is sorted from strongest to weakest.
std::vector<WifiNet> parse_wifi_nets(const std::string& out,
                                     const std::vector<std::string>& known);

// Refresh saved/keyed flags after the known list changes.
void mark_wifi_saved(std::vector<WifiNet>& nets,
                     const std::vector<std::string>& known);

} // namespace gameos
