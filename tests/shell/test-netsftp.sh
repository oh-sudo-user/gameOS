#!/bin/sh
# Host test for the SFTP path/config/host-key layer (no SSH server needed).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:-}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! pkg-config --exists libssh2 2>/dev/null; then
    echo "SKIP test-netsftp: libssh2 development files not installed"
    exit 0
fi

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra $CXXFLAGS -I"$SRC" \
        $(pkg-config --cflags libssh2) \
        -o "$TMP/test-netsftp" "$ROOT/tests/host/test-netsftp.cpp" \
        "$SRC/netsftp.cpp" $(pkg-config --libs libssh2) -pthread; then
    echo "FAIL compile test-netsftp"
    exit 1
fi

"$TMP/test-netsftp"
