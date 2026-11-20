#!/bin/sh
# Sign a release manifest with the gameOS OTA Ed25519 key.
# usage: scripts/sign-ota-manifest.sh <manifest.json> [private-key]
# The private key defaults to $GAMEOS_OTA_SIGNING_KEY or
# ~/.gameos-ota-signing/ed25519.pem and must never enter the repo.
set -eu
m=${1:?usage: $0 manifest.json [private-key]}
k=${2:-${GAMEOS_OTA_SIGNING_KEY:-$HOME/.gameos-ota-signing/ed25519.pem}}
[ -f "$m" ] || { echo "missing manifest: $m" >&2; exit 1; }
[ -f "$k" ] || { echo "missing signing key: $k" >&2; exit 1; }
openssl pkeyutl -sign -inkey "$k" -rawin -in "$m" -out "$m.sig"
[ "$(wc -c < "$m.sig" | tr -d ' ')" = 64 ] || {
    rm -f "$m.sig"; echo "signature is not 64 bytes" >&2; exit 1; }
echo "signed: $m.sig"
