#!/bin/sh
# wpa_supplicant action hook: lease an address when the link comes up and drop
# it when the link goes away. wpa_supplicant calls this as
#   wpa_action.sh <interface> <CONNECTED|DISCONNECTED|...>
IFACE="$1"
EVENT="$2"
PID="/run/udhcpc.$IFACE.pid"
kill_lease() {
    p=$(cat "$PID" 2>/dev/null)
    [ -n "$p" ] && kill "$p" 2>/dev/null
    rm -f "$PID"
}
case "$EVENT" in
CONNECTED)
    kill_lease
    udhcpc -i "$IFACE" -b -R -p "$PID" >/dev/null 2>&1 &
    ;;
DISCONNECTED)
    kill_lease
    ;;
esac
exit 0
