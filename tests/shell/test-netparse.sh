#!/bin/sh
# Host test for the gameos-bt / gameos-wifi output parsers.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
        -o "$TMP/test-netparse" "$ROOT/tests/host/test-netparse.cpp" \
        "$SRC/netparse.cpp"; then
    echo "FAIL compile test-netparse"
    exit 1
fi

"$TMP/test-netparse"
