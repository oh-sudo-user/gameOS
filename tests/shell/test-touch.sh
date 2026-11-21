#!/bin/sh
# Host test for touch gesture constants and transition gating (header-only).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
        -o "$TMP/test-touch" "$ROOT/tests/host/test-touch.cpp"; then
    echo "FAIL compile test-touch"
    exit 1
fi

"$TMP/test-touch"
