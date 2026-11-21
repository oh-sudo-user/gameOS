#!/bin/sh
# Host test for motion tokens, easing curves, the animator and the L0..L3
# degradation ladder (30-frame P95 budget, calm recovery, cooldown).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
        -o "$TMP/test-motion" \
        "$ROOT/tests/host/test-motion.cpp" "$SRC/motion.cpp"; then
    echo "FAIL compile test-motion"
    exit 1
fi

"$TMP/test-motion"
