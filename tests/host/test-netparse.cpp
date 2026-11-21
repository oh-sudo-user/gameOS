// Host tests for the gameos-bt / gameos-wifi output parsers.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "netparse.hpp"

#include <cstdio>

using namespace gameos;

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* what)
{
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        std::printf("FAIL %s\n", what);
    }
}

static void test_mac()
{
    check(bt_mac_valid("AA:BB:CC:DD:EE:FF"), "uppercase MAC is valid");
    check(bt_mac_valid("aa:bb:cc:dd:ee:ff"), "lowercase MAC is valid");
    check(!bt_mac_valid(""), "empty MAC is invalid");
    check(!bt_mac_valid("AA:BB:CC:DD:EE"), "short MAC is invalid");
    check(!bt_mac_valid("AA:BB:CC:DD:EE:FFF"), "long MAC is invalid");
    check(!bt_mac_valid("AA-BB-CC-DD-EE-FF"), "dash MAC is invalid");
    check(!bt_mac_valid("GG:BB:CC:DD:EE:FF"), "non-hex MAC is invalid");
}

static void test_bt_devs()
{
    const std::string out =
        "AA:BB:CC:DD:EE:01\t1\t1\tkeyboard\tKeyboard\r\n"
        "not-a-mac\t0\t0\tmouse\tBad\n"
        "AA:BB:CC:DD:EE:02\t0\t0\taudio\tHead\tphones\n"
        "missing\tfields\n"
        "\n";
    auto devs = parse_bt_devs(out);
    check(devs.size() == 2, "BT parser keeps two valid lines");
    check(devs[0].mac == "AA:BB:CC:DD:EE:01", "BT MAC parsed");
    check(devs[0].paired && devs[0].connected, "BT pairing flags parsed");
    check(devs[0].kind == "keyboard", "BT kind parsed");
    check(devs[0].name == "Keyboard", "BT name parsed and CR stripped");
    check(!devs[1].paired && !devs[1].connected, "BT defaults are false");
    check(devs[1].name == "Head\tphones", "extra tabs stay in the name");
    check(parse_bt_devs("").empty(), "empty BT output yields no devices");
}

static void test_last_line()
{
    check(last_nonempty_line("a\n\nb\n") == "b", "last non-empty line");
    check(last_nonempty_line("a\n\r\n") == "a", "bare CR lines are skipped");
    check(last_nonempty_line("a\r\n") == "a\r",
          "last line keeps its CR for the caller");
    check(last_nonempty_line("") == "", "empty output has no last line");
}

static void test_known()
{
    auto known = parse_wifi_known("Home\t1\r\nOpen\t0\nbare\n\n");
    check(known.size() == 3, "known list drops empty lines and CRs");
    check(known[0] == "Home\t1", "known keeps the keyed form");
    check(known[2] == "bare", "known keeps the bare form");

    check(wifi_known_saved(known, "Home"), "keyed network is saved");
    check(wifi_known_saved(known, "Open"), "open network is saved");
    check(wifi_known_saved(known, "bare"), "bare network is saved");
    check(!wifi_known_saved(known, "Hom"), "saved check does not prefix-match");
    check(!wifi_known_saved(known, "Home2"), "saved check does not suffix-match");

    check(wifi_known_keyed(known, "Home"), "keyed network is flagged");
    check(!wifi_known_keyed(known, "Open"), "open saved network is not keyed");
    check(!wifi_known_keyed(known, "bare"), "bare network is not keyed");
}

static void test_status()
{
    WifiStatus st = parse_wifi_status(
        "state=COMPLETED\r\nssid=Home WiFi\nip=192.168.1.5\njunk\n");
    check(st.state == "COMPLETED", "wifi state parsed");
    check(st.link == "Home WiFi", "wifi SSID parsed");
    check(st.ip == "192.168.1.5", "wifi IP parsed");

    WifiStatus empty = parse_wifi_status("no equals here\n");
    check(empty.state.empty() && empty.link.empty() && empty.ip.empty(),
          "malformed status yields empty fields");
}

static void test_nets()
{
    const std::vector<std::string> known = {"Home\t1", "Open"};
    auto nets = parse_wifi_nets(
        "-70\t0\tOpen\r\n"
        "-40\t1\tHome\n"
        "-55\t1\tHome\n"
        "-65\t1\tCafe\n"
        "-80\t1\t\n"
        "garbage\n", known);

    check(nets.size() == 3, "duplicate SSIDs collapse");
    check(nets[0].ssid == "Home" && nets[0].signal == -40,
          "strongest duplicate survives and sorting is descending");
    check(nets[0].saved && nets[0].keyed, "keyed network flags detected");
    check(nets[1].ssid == "Cafe" && !nets[1].saved && !nets[1].keyed,
          "unknown network is not saved");
    check(nets[2].ssid == "Open" && nets[2].saved && !nets[2].keyed,
          "open saved network detected");
    check(!nets[2].secure, "open network is not marked secure");
    check(nets[0].secure, "secure flag parsed");

    auto none = parse_wifi_nets("", known);
    check(none.empty(), "empty scan output yields no networks");

    mark_wifi_saved(nets, {"Cafe\t1"});
    check(nets[1].saved && nets[1].keyed, "mark_wifi_saved refreshes flags");
    check(!nets[0].saved, "mark_wifi_saved clears stale flags");
}

int main()
{
    test_mac();
    test_bt_devs();
    test_last_line();
    test_known();
    test_status();
    test_nets();

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
