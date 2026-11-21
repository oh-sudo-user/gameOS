#!/bin/sh
# Host test for the NES/SNES/MD console registry and ROM scanner.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:-}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra $CXXFLAGS -I"$SRC" \
        -o "$TMP/test-platforms" "$ROOT/tests/host/test-platforms.cpp" \
        "$SRC/library.cpp"; then
    echo "FAIL compile test-platforms"
    exit 1
fi

"$TMP/test-platforms"
