#!/bin/sh
# Host test for the file manager core and ZIP safety (needs zlib development files).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}

if ! pkg-config --exists zlib 2>/dev/null; then
    echo "SKIP test-fileops (zlib development files not installed)"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -pthread -I"$SRC" \
        $(pkg-config --cflags zlib) \
        -o "$TMP/test-fileops" "$ROOT/tests/host/test-fileops.cpp" \
        "$SRC/fileops.cpp" $(pkg-config --libs zlib); then
    echo "FAIL compile test-fileops"
    exit 1
fi

"$TMP/test-fileops"
