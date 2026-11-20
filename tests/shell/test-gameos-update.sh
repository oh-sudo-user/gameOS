#!/bin/sh
# Fault-injection tests for gameos-update (OTA updater).
#
# Runs on a POSIX host (dash / BusyBox ash) with NO hardware: block-device,
# mount and network I/O are stubbed with shell functions, while gzip, sha256sum
# and tar are the REAL tools so integrity checks are exercised for real.
#
# Coverage (audit doc 4.5):
#   - strict manifest validation matrix (missing/dup/reorder/type/range/CR/...)
#   - version comparison (no downgrade, no same-release rebuild, +g suffix)
#   - do_apply fault injection at EVERY stage; the invariant under test is that
#     /boot/state is switched LAST and is NEVER switched on any failure.
#
# The script is sourced as vars+functions only (top-level side effects and the
# command dispatcher are stripped) with absolute paths redirected into a temp
# sandbox. Logic is unchanged; only paths are redirected.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCRIPT="$ROOT/buildroot/board/gameos/rgds-plus/rootfs-overlay/usr/bin/gameos-update"
FIX="$HERE/../fixtures"

PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); printf 'ok   %s\n' "$1"; }
no(){ FAIL=$((FAIL+1)); printf 'FAIL %s\n' "$1"; }

SB=$(mktemp -d)
trap 'rm -rf "$SB"' EXIT INT TERM

# Publisher authentication is mandatory, so every apply test needs a real
# key pair and a real signature over the manifest it serves.
TESTKEY="$SB/ota-test-ed25519.pem"
openssl genpkey -algorithm ED25519 -out "$TESTKEY" 2>/dev/null \
  || { echo "SKIP: openssl with Ed25519 support is required"; exit 0; }
openssl pkey -in "$TESTKEY" -pubout -out "$SB/ota-pub.pem" 2>/dev/null

HEX64A="0000000000000000000000000000000000000000000000000000000000000000"

# --- sourceable copy: var block + function block, paths redirected to $SB ----
awk '
  /^REPO=/ {v=1}
  /^mkdir -p/ {v=0}
  v {print}
  /^state[(][)] [{]/ {f=1}
  f && /^case [$]1 in/ {f=0}
  f {print}
' "$SCRIPT" \
| sed -e "s#/userdata/system/update#$SB/app#g" \
      -e "s#/run/gameos#$SB/run#g" \
      -e "s#/userdata#$SB/userdata#g" \
      -e "s#/boot/#$SB/boot/#g" \
      -e "s#/boot\([ ;]\)#$SB/boot\1#g" \
      -e "s#/etc/gameos-version#$SB/gameos-version#g" \
      -e "s#/etc/gameos/ota-pub.pem#$SB/ota-pub.pem#g" \
      -e "s#/etc/ssl/certs/ca-certificates.crt#$SB/cacert#g" \
      -e "s#/proc/self/mounts#$SB/mounts#g" \
      -e "s#/proc/cmdline#$SB/cmdline#g" \
      -e "s#/proc/device-tree/compatible#$SB/compatible#g" \
      -e "s#/sys/class/block#$SB/sysblock#g" \
      -e "s#/dev/disk/by-partlabel#$SB/by-partlabel#g" \
  > "$SB/funcs.sh"
# shellcheck disable=SC1091
. "$SB/funcs.sh"
PUBKEY="$SB/ota-pub.pem"

# ===========================================================================
# Part A -- pure-function validation (real coreutils, no stubs yet)
# ===========================================================================
mkdir -p "$SB/app" "$SB/run"

expect_manifest(){ # <file> <want 0|1> <desc>
  [ "$1" = "$MANIFEST" ] || cp "$1" "$MANIFEST"
  validate_manifest; r=$?
  if [ "$r" = "$2" ]; then ok "$3"; else no "$3 (got $r want $2)"; fi
}
expect_manifest "$FIX/manifest-valid.json"        0 "manifest: valid accepted"
expect_manifest "$FIX/manifest-invalid-hash.json" 1 "manifest: non-hex sha rejected"
expect_manifest "$FIX/manifest-invalid-size.json" 1 "manifest: zero size rejected"

# duplicate version line
sed 's/^  "version": "1.0.1",/  "version": "1.0.1",\n  "version": "9.9.9",/' \
  "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: duplicate version rejected"
# reordered rootfs keys (gz before size) -> canonical mismatch
sed 's/"size": 1048576, "gz": 524288/"gz": 524288, "size": 1048576/' \
  "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: reordered keys rejected"
# extra key in rootfs section
sed 's/"gz": 524288 }/"gz": 524288, "path": "..\/evil" }/' \
  "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: extra key rejected"
# wrong rootfs filename
sed 's/gameOS-rootfs.ext4.gz/evil.gz/' "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: wrong rootfs filename rejected"
# missing boot section
grep -v '"boot"' "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: missing boot section rejected"
# bad version (two components)
sed 's/"version": "1.0.1"/"version": "1.0"/' "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: bad version rejected"
# oversize rootfs (> MAX_ROOTFS)
sed 's/"size": 1048576/"size": 2000000000/' "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: oversize rootfs rejected"
# size as a string (wrong type)
sed 's/"size": 1048576/"size": "1048576"/' "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: string size rejected"
# CRLF line endings
sed 's/$/\r/' "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: CRLF rejected"
# rootfs section split across lines
sed 's/"rootfs": { "file"/"rootfs": {\n    "file"/' "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 1 "manifest: multiline section rejected"
# valid with +g build suffix
sed 's/"version": "1.0.1"/"version": "1.0.1+gabc1234"/' "$FIX/manifest-valid.json" > "$MANIFEST"
expect_manifest "$MANIFEST" 0 "manifest: +g suffix accepted"
# missing manifest file
rm -f "$MANIFEST"
validate_manifest; r=$?
if [ "$r" = 1 ]; then ok "manifest: absent file rejected"; else no "manifest: absent file rejected"; fi

expect_cmp(){ # <a> <b> <want> <desc>
  got=$(ver_cmp "$1" "$2")
  if [ "$got" = "$3" ]; then ok "$4"; else no "$4 (got $got want $3)"; fi
}
expect_cmp 1.0.1 1.0.0 1  "ver_cmp: newer patch"
expect_cmp 1.0.0 1.0.1 -1 "ver_cmp: older patch"
expect_cmp 1.0.0 1.0.0 0  "ver_cmp: same"
expect_cmp 1.1.0 1.0.9 1  "ver_cmp: newer minor"
expect_cmp 2.0.0 1.9.9 1  "ver_cmp: newer major"
expect_cmp 1.0.1+gabc123 1.0.1 0 "ver_cmp: +g suffix ignored (no rebuild offer)"
expect_cmp 1.0.0 1.0.0+gdeadbeef 0 "ver_cmp: suffix on current ignored"
expect_cmp 1.0.1 "" 1 "ver_cmp: unknown current treated as 0.0.0"
expect_cmp 1.0.10 1.0.9 1 "ver_cmp: numeric not lexical (10 > 9)"

is_hex64 "$HEX64A"; r=$?
if [ "$r" = 0 ]; then ok "is_hex64: 64 hex accepted"; else no "is_hex64: 64 hex accepted"; fi
is_hex64 "${HEX64A%?}"; r=$?
if [ "$r" = 1 ]; then ok "is_hex64: 63 chars rejected"; else no "is_hex64: 63 chars rejected"; fi
is_uint "12345"; r=$?
if [ "$r" = 0 ]; then ok "is_uint: digits accepted"; else no "is_uint: digits accepted"; fi
is_uint "12a45"; r=$?
if [ "$r" = 1 ]; then ok "is_uint: non-digit rejected"; else no "is_uint: non-digit rejected"; fi
is_uint ""; r=$?
if [ "$r" = 1 ]; then ok "is_uint: empty rejected"; else no "is_uint: empty rejected"; fi

# ===========================================================================
# Part B -- do_apply fault injection (stub block/mount/network I/O)
# ===========================================================================
ONLINE=1; DD_FAIL=0; MOUNT_FAIL=0; CURL_FAIL=none; NO_SLOT=0
DF_AVAIL_KB=2000000; BLOCKDEV_SIZE=1258291200
ROOTFS_FIX="$SB/def-rootfs.gz"; BOOT_FIX="$SB/def-boot.tar.gz"

online(){ [ "$ONLINE" = 1 ]; }
part_dev(){
  [ "$NO_SLOT" = 1 ] && return 0
  case "$1" in
    GAMEOS-ROOT-B) echo "$SB/rootB" ;;
    GAMEOS-ROOT-A) echo "$SB/rootA" ;;
  esac
}
sync(){ :; }
blockdev(){ echo "$BLOCKDEV_SIZE"; }
df(){
  printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\n'
  printf '/dev/root 1000000 0 %s 0%% /userdata\n' "$DF_AVAIL_KB"
}
mountpoint(){
  p=""; for a in "$@"; do case "$a" in -*) ;; *) p="$a" ;; esac; done
  case "$p" in
    "$SB/userdata") [ -f "$SB/mp-userdata" ] && return 0 ;;
    "$SB/boot")     [ -f "$SB/mp-boot" ] && return 0 ;;
  esac
  return 1
}
mount(){ [ "$MOUNT_FAIL" = 1 ] && return 1; mkdir -p "$SB/boot"; touch "$SB/mp-boot"; return 0; }
dd(){
  [ "$DD_FAIL" = 1 ] && return 1
  inf=""; outf=""
  for a in "$@"; do case "$a" in if=*) inf="${a#if=}" ;; of=*) outf="${a#of=}" ;; esac; done
  cp "$inf" "$outf"
}
curl(){
  url=""; out=""
  for a in "$@"; do url="$a"; done
  while [ $# -gt 0 ]; do
    if [ "$1" = -o ]; then out="$2"; shift 2; continue; fi
    shift
  done
  case "$url" in
    */manifest.json) asset=manifest ;;
    */gameOS-rootfs.ext4.gz) asset=rootfs ;;
    */gameOS-boot.tar.gz) asset=boot ;;
    *) asset=other ;;
  esac
  [ "$CURL_FAIL" = "$asset" ] && return 1
  case "$asset" in
    rootfs) cp "$ROOTFS_FIX" "$out" ;;
    boot)   cp "$BOOT_FIX" "$out" ;;
    *)      return 1 ;;
  esac
}

write_manifest(){ # <ver> <rsha> <rsize> <rgz> <bsha> <bsize>
  cat > "$MANIFEST" <<EOF
{
  "version": "$1",
  "rootfs": { "file": "gameOS-rootfs.ext4.gz", "sha256": "$2", "size": $3, "gz": $4 },
  "boot": { "file": "gameOS-boot.tar.gz", "sha256": "$5", "size": $6 },
  "image_rgds_plus": { "file": "gameOS-rgds-plus.img.gz", "sha256": "$2", "size": 1000 },
  "image_rgds": { "file": "gameOS-rgds.img.gz", "sha256": "$2", "size": 1000 }
}
EOF
  openssl pkeyutl -sign -inkey "$TESTKEY" -rawin \
    -in "$MANIFEST" -out "$SIG" 2>/dev/null
}

make_default_fixtures(){
  mkdir -p "$SB/src/boot"
  head -c 65536 /dev/zero | tr '\0' 'A' > "$SB/src/rootfs.ext4"
  gzip -9 -c "$SB/src/rootfs.ext4" > "$ROOTFS_FIX"
  for m in Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus boot.scr.rgds; do
    printf 'content-of-%s\n' "$m" > "$SB/src/boot/$m"
  done
  tar -czf "$BOOT_FIX" -C "$SB/src/boot" \
    Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus boot.scr.rgds
}

make_manifest(){
  RSIZE=$(wc -c < "$SB/src/rootfs.ext4" | tr -d ' ')
  RGZ=$(wc -c < "$ROOTFS_FIX" | tr -d ' ')
  RSHA=$(sha256sum "$ROOTFS_FIX" | cut -d' ' -f1)
  BSIZE=$(wc -c < "$BOOT_FIX" | tr -d ' ')
  BSHA=$(sha256sum "$BOOT_FIX" | cut -d' ' -f1)
  write_manifest 1.0.1 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
}

reset(){
  rm -rf "$SB/app" "$SB/run" "$SB/boot" "$SB/src" "$SB/rootA" "$SB/rootB"
  mkdir -p "$SB/app" "$SB/run" "$SB/boot" "$SB/userdata"
  : > "$SB/rootA"; : > "$SB/rootB"
  printf 'slot=a\nfallback=a\nn=0\n' > "$SB/boot/state"
  printf 'BOOT_IMAGE=/a/Image root=PARTLABEL=GAMEOS-ROOT-A rw\n' > "$SB/cmdline"
  printf 'anbernic,rg-ds-plus\000rockchip,rk3568\000' > "$SB/compatible"
  printf '/dev/root / ext4 rw 0 0\n' > "$SB/mounts"
  echo 1.0.0 > "$SB/gameos-version"
  touch "$SB/mp-userdata"; rm -f "$SB/mp-boot"
  ONLINE=1; DD_FAIL=0; MOUNT_FAIL=0; CURL_FAIL=none; NO_SLOT=0
  DF_AVAIL_KB=2000000; BLOCKDEV_SIZE=1258291200
}
setup(){ reset; make_default_fixtures; make_manifest; }

run_case(){ # <desc> <want_state> <want_slot>
  do_apply > "$SB/apply.out" 2>&1
  st=$(sed -n 's/^state=//p' "$STATE" | head -n1)
  sl=$(sed -n 's/^slot=//p' "$SB/boot/state" | head -n1)
  if [ "$st" = "$2" ] && [ "$sl" = "$3" ]; then
    ok "$1"
  else
    no "$1 (state=$st slot=$sl want $2/$3)"; sed 's/^/    /' "$SB/apply.out"
  fi
}

# --- success path: state switched LAST, rootfs written, boot assets installed
setup
run_case "apply: success switches slot a->b" ready b
if grep -qx 'fallback=a' "$SB/boot/state" && grep -qx 'n=0' "$SB/boot/state"; then
  ok "apply: success sets fallback=a n=0"
else no "apply: success sets fallback=a n=0"; fi
rsz=$(wc -c < "$SB/rootB" | tr -d ' ')
if [ "$rsz" = "$RSIZE" ]; then ok "apply: rootfs written to idle slot"; else no "apply: rootfs written to idle slot ($rsz != $RSIZE)"; fi
if [ -f "$SB/boot/b/Image" ] && [ -f "$SB/boot/boot.scr" ]; then
  ok "apply: boot assets + model boot.scr installed"
else no "apply: boot assets + model boot.scr installed"; fi
if [ ! -f "$ROOTFS" ] && [ ! -f "$BOOT" ] && [ ! -d "$STAGE" ]; then
  ok "apply: downloads + staging cleaned up"
else no "apply: downloads + staging cleaned up"; fi

# --- every failure must leave /boot/state on slot=a (never switched) ---------
setup; CURL_FAIL=rootfs
run_case "apply: rootfs download fail -> no switch" failed a
setup; CURL_FAIL=boot
run_case "apply: boot download fail -> no switch" failed a
setup; write_manifest 1.0.1 "$HEX64A" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: rootfs checksum mismatch -> no switch" failed a
setup; write_manifest 1.0.1 "$RSHA" "$RSIZE" "$RGZ" "$HEX64A" "$BSIZE"
run_case "apply: boot checksum mismatch -> no switch" failed a

# publisher authentication: a manifest changed after signing, a missing
# signature and a truncated signature must all stop before any block write
setup; printf 'x' >> "$MANIFEST"
run_case "apply: tampered manifest -> no switch" failed a

setup; rm -f "$SIG"
run_case "apply: missing manifest signature -> no switch" failed a

setup; head -c 63 "$SIG" > "$SB/sig.t"; mv "$SB/sig.t" "$SIG"
run_case "apply: truncated manifest signature -> no switch" failed a

# truncated gzip (sha recomputed so checksum passes, gzip -t fails)
setup
sz=$(wc -c < "$ROOTFS_FIX" | tr -d ' '); head -c "$((sz / 2))" "$ROOTFS_FIX" > "$SB/t.gz"
mv "$SB/t.gz" "$ROOTFS_FIX"
RGZ=$(wc -c < "$ROOTFS_FIX" | tr -d ' '); RSHA=$(sha256sum "$ROOTFS_FIX" | cut -d' ' -f1)
write_manifest 1.0.1 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: corrupt gzip -> no switch" failed a

# uncompressed size mismatch (valid gzip, wrong declared rsize)
setup; write_manifest 1.0.1 "$RSHA" "$((RSIZE - 1))" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: rootfs size mismatch -> no switch" failed a

# insufficient space to decompress+verify
setup; DF_AVAIL_KB=100
run_case "apply: not enough space -> no switch" failed a

# block write failure
setup; DD_FAIL=1
run_case "apply: dd write fail -> no switch" failed a

# idle slot too small for the rootfs
setup; BLOCKDEV_SIZE=1000
run_case "apply: slot too small -> no switch" failed a

# idle slot already mounted (must never write the running slot)
setup; printf '%s / ext4 rw 0 0\n' "$SB/rootB" >> "$SB/mounts"
run_case "apply: idle slot in use -> no switch" failed a

# no idle slot device
setup; NO_SLOT=1
run_case "apply: no idle slot -> no switch" failed a

# boot partition cannot be mounted (late failure, still no state switch)
setup; MOUNT_FAIL=1
run_case "apply: boot mount fail -> no switch" failed a

# no network
setup; ONLINE=0
run_case "apply: offline -> nowifi, no switch" nowifi a

# unknown device model
setup; printf 'some,other-board\000' > "$SB/compatible"
run_case "apply: unknown model -> no switch" failed a

# not newer than running (no downgrade / no same-release reinstall)
setup; write_manifest 1.0.0 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: same version -> uptodate, no switch" uptodate a
setup; write_manifest 0.9.9 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: older version -> uptodate, no switch" uptodate a

# --- malicious boot archives ------------------------------------------------
make_traversal_tar(){
  mkdir -p "$SB/src/trav"
  for m in Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus; do
    printf 'x\n' > "$SB/src/trav/$m"
  done
  printf 'evil\n' > "$SB/src/evil"
  tar -czPf "$BOOT_FIX" -C "$SB/src/trav" \
    Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus ../evil
}
setup; make_traversal_tar
BSIZE=$(wc -c < "$BOOT_FIX" | tr -d ' '); BSHA=$(sha256sum "$BOOT_FIX" | cut -d' ' -f1)
write_manifest 1.0.1 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: boot tar path traversal -> no switch" failed a
if [ ! -e "$SB/evil" ] && [ ! -e "$SB/src/evil.installed" ]; then
  ok "apply: traversal did not escape staging"
else no "apply: traversal escaped staging"; fi

make_symlink_tar(){
  mkdir -p "$SB/src/sym"
  for m in rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus boot.scr.rgds; do
    printf 'x\n' > "$SB/src/sym/$m"
  done
  ln -sf /etc/passwd "$SB/src/sym/Image"
  tar -czf "$BOOT_FIX" -C "$SB/src/sym" \
    Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus boot.scr.rgds
}
setup; make_symlink_tar
BSIZE=$(wc -c < "$BOOT_FIX" | tr -d ' '); BSHA=$(sha256sum "$BOOT_FIX" | cut -d' ' -f1)
write_manifest 1.0.1 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: boot tar symlink member -> no switch" failed a

make_short_tar(){
  mkdir -p "$SB/src/short"
  for m in Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus; do
    printf 'x\n' > "$SB/src/short/$m"
  done
  tar -czf "$BOOT_FIX" -C "$SB/src/short" \
    Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus
}
setup; make_short_tar
BSIZE=$(wc -c < "$BOOT_FIX" | tr -d ' '); BSHA=$(sha256sum "$BOOT_FIX" | cut -d' ' -f1)
write_manifest 1.0.1 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: boot tar missing member (old release) -> no switch" failed a

make_extra_tar(){
  mkdir -p "$SB/src/extra"
  for m in Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus boot.scr.rgds; do
    printf 'x\n' > "$SB/src/extra/$m"
  done
  printf 'x\n' > "$SB/src/extra/evil"
  tar -czf "$BOOT_FIX" -C "$SB/src/extra" \
    Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus boot.scr.rgds evil
}
setup; make_extra_tar
BSIZE=$(wc -c < "$BOOT_FIX" | tr -d ' '); BSHA=$(sha256sum "$BOOT_FIX" | cut -d' ' -f1)
write_manifest 1.0.1 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: boot tar extra member -> no switch" failed a

# empty boot.scr member (size 0 rejected)
setup
: > "$SB/src/boot/boot.scr.rgds"
tar -czf "$BOOT_FIX" -C "$SB/src/boot" \
  Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb boot.scr.rgds-plus boot.scr.rgds
BSIZE=$(wc -c < "$BOOT_FIX" | tr -d ' '); BSHA=$(sha256sum "$BOOT_FIX" | cut -d' ' -f1)
write_manifest 1.0.1 "$RSHA" "$RSIZE" "$RGZ" "$BSHA" "$BSIZE"
run_case "apply: empty boot.scr member -> no switch" failed a

# ===========================================================================
printf '\n== gameos-update: %s passed, %s failed ==\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ] || exit 1
exit 0