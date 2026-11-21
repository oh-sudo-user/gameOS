#!/bin/sh
# Host test for the UI command buffer / damage tracker (src/drawcall.cpp).
# Only the pure rectangle and hashing helpers run here; the recorder needs a
# renderer and is exercised by the pixel-diff harness during development.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}

if ! pkg-config --exists sdl2; then
    echo "SKIP test-ui-drawcall (SDL2 development files not installed)"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
        $(pkg-config --cflags sdl2) \
        -o "$TMP/test-drawcall" \
        "$ROOT/tests/host/test-drawcall.cpp" "$SRC/drawcall.cpp" \
        "$SRC/perf.cpp" $(pkg-config --libs sdl2); then
    echo "FAIL compile test-drawcall"
    exit 1
fi

"$TMP/test-drawcall"
