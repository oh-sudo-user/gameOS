#!/bin/sh
# Host test for the per-game override store (needs json-c development files).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}

if ! pkg-config --exists json-c 2>/dev/null; then
    echo "SKIP test-overrides (json-c development files not installed)"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
        $(pkg-config --cflags json-c) \
        -o "$TMP/test-overrides" "$ROOT/tests/host/test-overrides.cpp" \
        "$SRC/overrides.cpp" $(pkg-config --libs json-c); then
    echo "FAIL compile test-overrides"
    exit 1
fi

"$TMP/test-overrides"
