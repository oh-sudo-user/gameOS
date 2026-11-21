#!/bin/sh
# Host-side CI gate: every shell test, theme lint, script syntax and a check
# that all package sources are pinned to tags or commits (no floating refs).
# Run from a clean checkout; the image job calls verify-image.sh separately.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail=0

note_fail() {
    echo "FAIL: $1"
    fail=1
}

echo "== host tests"
for t in "$ROOT"/tests/shell/test-*.sh; do
    name=${t#"$ROOT"/}
    echo "-- $name"
    if ! sh "$t"; then
        note_fail "$name"
    fi
done

echo "== theme lint"
if ! sh "$ROOT/scripts/lint-themes.sh"; then
    note_fail "scripts/lint-themes.sh"
fi

echo "== shell syntax"
syntax_bad=0
for d in "$ROOT/scripts" "$ROOT/tests" "$ROOT/buildroot/board"; do
    [ -d "$d" ] || continue
    find "$d" -name '*.sh' -type f | while read -r f; do
        if ! sh -n "$f"; then
            echo "FAIL syntax: ${f#"$ROOT"/}"
            exit 1
        fi
    done || syntax_bad=1
done
[ "$syntax_bad" = 0 ] || note_fail "shell syntax"

echo "== package pins"
pins_bad=0
for mk in "$ROOT"/buildroot/package/*/*.mk; do
    [ -f "$mk" ] || continue
    prefix=$(basename "$mk" .mk)
    key=$(echo "$prefix" | tr '[:lower:]-' '[:upper:]_')
    method=$(sed -n "s/^${key}_SITE_METHOD[[:space:]]*=[[:space:]]*//p" "$mk" | head -1)
    case "$method" in
        git)
            ver=$(sed -n "s/^${key}_VERSION[[:space:]]*=[[:space:]]*//p" "$mk" | head -1)
            case "$ver" in
                ""|main|master|HEAD|origin/main|origin/master)
                    echo "FAIL floating git ref: ${mk#"$ROOT"/} ($key = '$ver')"
                    pins_bad=1
                    ;;
            esac
            ;;
    esac
done
[ "$pins_bad" = 0 ] || note_fail "package pins"

if [ -n "${GAMEOS_IMAGES:-}" ]; then
    echo "== image artifacts"
    if ! sh "$ROOT/scripts/verify-image.sh" "$GAMEOS_IMAGES"; then
        note_fail "image artifacts"
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "CI: FAILED"
    exit 1
fi
echo "CI: PASSED"
