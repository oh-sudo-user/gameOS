#!/bin/sh
# Host test for widget layout, hit-testing and toast timing. The pure helpers
# are header-only, so no SDL library is linked; SDL headers are required.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}

if ! pkg-config --exists sdl2 SDL2_ttf 2>/dev/null; then
    echo "SKIP test-widget (SDL2 development files not installed)"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
        $(pkg-config --cflags sdl2 SDL2_ttf) \
        -o "$TMP/test-widget" "$ROOT/tests/host/test-widget.cpp"; then
    echo "FAIL compile test-widget"
    exit 1
fi

"$TMP/test-widget"
