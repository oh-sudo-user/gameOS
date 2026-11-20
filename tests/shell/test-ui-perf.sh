#!/bin/sh
# Host test for the UI frame profiler (src/perf.cpp).
#
# perf.cpp deliberately has no SDL dependency, so its percentile and window
# logic can be compiled and run on any host with a C++17 compiler. The file
# I/O side (ui-perf.on / ui-perf.log) is a device-only check, documented in
# docs/TROUBLESHOOTING.md.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC="$ROOT/buildroot/package/gameos-ui/src"
CXX=${CXX:-g++}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -I"$SRC" \
        -o "$TMP/test-ui-perf" \
        "$ROOT/tests/host/test-ui-perf.cpp" "$SRC/perf.cpp"; then
    echo "FAIL compile test-ui-perf"
    exit 1
fi

"$TMP/test-ui-perf"
