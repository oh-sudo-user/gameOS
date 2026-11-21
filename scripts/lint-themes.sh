#!/bin/sh
# Validate every shipped theme palette against schema version 1.
# A theme ships with the rootfs overlay, so a typo here would only surface
# on the device; fail the test suite instead. The UI's loader (i18n.cpp) is
# deliberately stricter than "best effort": any bad file is ignored whole.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
THEMES="$ROOT/buildroot/board/gameos/rgds-plus/rootfs-overlay/usr/share/gameos/assets/themes"
fail=0

for key in neon pixel fresh ink; do
    f="$THEMES/$key/palette.conf"
    if [ ! -f "$f" ]; then
        echo "FAIL theme $key: missing $f"
        fail=1
        continue
    fi
    if awk '
        function fail(msg) { printf "FAIL %s: %s\n", FILENAME, msg; bad = 1 }
        BEGIN {
            n = split("version accent text dim panel panel_a bg track sheen tint_top tint_bot", req, " ")
            for (i = 1; i <= n; i++) want[req[i]] = 1
        }
        { sub(/\r$/, "") }
        /^[ \t]*#/ { next }
        /^[ \t]*$/ { next }
        {
            eq = index($0, "=")
            if (!eq) { fail("line without ="); next }
            k = substr($0, 1, eq - 1)
            v = substr($0, eq + 1)
            gsub(/^[ \t]+|[ \t]+$/, "", k)
            gsub(/^[ \t]+|[ \t]+$/, "", v)
            if (!(k in want)) { fail("unknown key " k); next }
            if (k in seen) { fail("duplicate key " k); next }
            seen[k] = 1
            if (k == "version") {
                if (v != "1") fail("version must be 1")
                next
            }
            if (k == "accent" || k == "text" || k == "dim" || k == "panel" ||
                k == "bg" || k == "track" || k == "sheen") {
                if (v !~ /^#[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]$/)
                    fail("bad colour " k "=" v)
                next
            }
            if (v !~ /^[0-9][0-9]?[0-9]?$/ || v + 0 > 255)
                fail("bad byte " k "=" v)
        }
        END {
            for (i = 1; i <= n; i++)
                if (!(req[i] in seen)) fail("missing key " req[i])
            exit bad
        }
    ' "$f"; then
        echo "ok   theme $key"
    else
        fail=1
    fi
done

[ "$fail" -eq 0 ] || exit 1
echo "theme palettes: all valid"
