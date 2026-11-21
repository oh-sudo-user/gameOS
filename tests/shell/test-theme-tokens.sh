#!/bin/sh
# Host test for theme palette files: schema parser plus the shipped files.
# The parser must reject a bad file as a whole, and each shipped palette
# must still describe exactly the built-in defaults (no silent recolour).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
THEMES="$ROOT/buildroot/board/gameos/rgds-plus/rootfs-overlay/usr/share/gameos/assets/themes"
CXX=${CXX:-g++}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
        -o "$TMP/test-theme-tokens" \
        "$ROOT/tests/host/test-theme-tokens.cpp" "$SRC/i18n.cpp"; then
    echo "FAIL compile test-theme-tokens"
    exit 1
fi

"$TMP/test-theme-tokens" "$THEMES" || exit 1
sh "$ROOT/scripts/lint-themes.sh"
