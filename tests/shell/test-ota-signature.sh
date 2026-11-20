#!/bin/sh
# Unit tests for the OTA manifest signature gate (PR-02).
# Extracts verify_manifest_signature() verbatim from gameos-update and runs it
# against a real ephemeral Ed25519 key pair.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCRIPT="$ROOT/buildroot/board/gameos/rgds-plus/rootfs-overlay/usr/bin/gameos-update"
PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); printf 'ok   %s\n' "$1"; }
no(){ FAIL=$((FAIL+1)); printf 'FAIL %s\n' "$1"; }
SB=$(mktemp -d)
trap 'rm -rf "$SB"' EXIT INT TERM
command -v openssl >/dev/null 2>&1 || { echo "SKIP: openssl not found"; exit 0; }

awk '/^verify_manifest_signature[(][)] [{]/{f=1} f{print} f&&/^}/{exit}' \
    "$SCRIPT" > "$SB/fn.sh"
if [ ! -s "$SB/fn.sh" ]; then no "function extracted"; exit 1; fi
# shellcheck disable=SC1091
. "$SB/fn.sh"
PUBKEY="$SB/pub.pem"; SIG_LEN=64

expect(){ # <want> <desc> <manifest> <sig>
  verify_manifest_signature "$3" "$4"; r=$?
  if [ "$r" = "$1" ]; then ok "$2"; else no "$2 (got $r want $1)"; fi
}

openssl genpkey -algorithm ED25519 -out "$SB/key.pem" 2>/dev/null
openssl pkey -in "$SB/key.pem" -pubout -out "$PUBKEY" 2>/dev/null
openssl genpkey -algorithm ED25519 -out "$SB/other.pem" 2>/dev/null
printf '{"version":"1.0.1"}\n' > "$SB/m.json"
SIG="$SB/m.sig"

openssl pkeyutl -sign -inkey "$SB/key.pem" -rawin -in "$SB/m.json" -out "$SIG"
expect 0 "signature: valid manifest accepted" "$SB/m.json" "$SIG"

printf 'x' >> "$SB/m.json"
expect 1 "signature: one appended byte rejected" "$SB/m.json" "$SIG"

printf '{"version":"1.0.1"}\n' > "$SB/m.json"
openssl pkeyutl -sign -inkey "$SB/other.pem" -rawin -in "$SB/m.json" -out "$SIG"
expect 1 "signature: wrong key rejected" "$SB/m.json" "$SIG"

openssl pkeyutl -sign -inkey "$SB/key.pem" -rawin -in "$SB/m.json" -out "$SIG"
head -c 63 "$SIG" > "$SB/short.sig"
expect 1 "signature: 63-byte signature rejected" "$SB/m.json" "$SB/short.sig"

expect 1 "signature: missing signature rejected" "$SB/m.json" "$SB/nope.sig"

openssl pkeyutl -sign -inkey "$SB/key.pem" -rawin -in "$SB/m.json" -out "$SIG"
mv "$PUBKEY" "$SB/pub.hidden"
expect 1 "signature: missing public key rejected" "$SB/m.json" "$SIG"
mv "$SB/pub.hidden" "$PUBKEY"

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ]
