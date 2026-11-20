#!/bin/sh
# Fault-injection tests for the S20gameos first-boot data-partition block.
#
# THE INVARIANT (audit doc 3.3 / 0): an EXISTING p4 is NEVER auto-formatted.
# Formatting is allowed ONLY for a partition this boot just created, and only
# after every creation step succeeded AND a read-only verify mount passed.
#
# No hardware: block-device presence is simulated with a marker file, and
# sgdisk/parted/partx/mkfs.exfat/mount are stubbed with controllable exit codes.
# The decision logic under test is extracted verbatim from S20gameos; only the
# device-presence predicate and absolute paths are redirected into a sandbox.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCRIPT="$ROOT/buildroot/board/gameos/rgds-plus/rootfs-overlay/etc/init.d/S20gameos"

PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); printf 'ok   %s\n' "$1"; }
no(){ FAIL=$((FAIL+1)); printf 'FAIL %s\n' "$1"; }

SB=$(mktemp -d)
trap 'rm -rf "$SB"' EXIT INT TERM
mkdir -p "$SB/bin"
export SB
PATH="$SB/bin:$PATH"

# mkfs.exfat has a dot in its name (not a valid function name in every shell),
# so it is a PATH stub script driven by the exported MKFS_OK flag.
cat > "$SB/bin/mkfs.exfat" <<'STUB'
#!/bin/sh
echo "mkfs.exfat $*" >> "$SB/calls"
touch "$SB/mkfs-called"
[ "$MKFS_OK" = 1 ]
STUB
chmod +x "$SB/bin/mkfs.exfat"

# --- extract the first-boot block verbatim, wrap as a callable function ------
{
  echo 'firstboot_block(){'
  awk '/^FIRSTBOOT=0/{f=1} /^# --- mount userdata/{f=0} f{print}' "$SCRIPT" \
  | sed -e "s#/etc/gameos-firstboot#$SB/fb-marker#g" \
        -e "s#/tmp/gameos-data-probe#$SB/probe#g" \
        -e "s#\[ -b \${DEV}p4 \]#[ -f \"$SB/p4-present\" ]#g" \
        -e "s#\[ ! -b \${DEV}p4 \]#[ ! -f \"$SB/p4-present\" ]#g"
  echo '}'
} > "$SB/block.sh"
# shellcheck disable=SC1091
. "$SB/block.sh"

# --- stubs ------------------------------------------------------------------
log(){ echo "[gameos-setup] $*" >> "$SB/log"; }
mount(){ echo "mount $*" >> "$SB/calls"; [ "$MOUNT_RO_OK" = 1 ]; }
umount(){ :; }
sleep(){ :; }
blockdev(){ :; }
partx(){ :; }
sgdisk(){
  echo "sgdisk $*" >> "$SB/calls"
  case "$1" in
    -e) [ "$SGDISK_E_OK" = 1 ] ;;
    -n) if [ "$SGDISK_N_OK" = 1 ]; then
          [ "$NODE_APPEARS" = 1 ] && touch "$SB/p4-present"
          return 0
        fi
        return 1 ;;
    *) return 0 ;;
  esac
}
parted(){
  echo "parted $*" >> "$SB/calls"
  if [ "$PARTED_OK" = 1 ]; then
    [ "$NODE_APPEARS" = 1 ] && touch "$SB/p4-present"
    return 0
  fi
  return 1
}
# Control tool availability deterministically (the block uses `command -v`).
command(){
  if [ "${1:-}" = -v ]; then
    case "${2:-}" in
      sgdisk) [ "$HAVE_SGDISK" = 1 ] && { echo sgdisk; return 0; } ;;
      parted) [ "$HAVE_PARTED" = 1 ] && { echo parted; return 0; } ;;
      partx)  [ "$HAVE_PARTX" = 1 ] && { echo partx; return 0; } ;;
    esac
    return 1
  fi
  return 1
}

reset(){
  rm -rf "$SB/probe" "$SB/p4-present" "$SB/mkfs-called" "$SB/calls" "$SB/log" "$SB/fb-marker"
  : > "$SB/log"; : > "$SB/calls"
  touch "$SB/fb-marker"
  DEV="$SB/disk"
  LOG="$SB/log"
  HAVE_SGDISK=1; HAVE_PARTED=1; HAVE_PARTX=1
  SGDISK_E_OK=1; SGDISK_N_OK=1; PARTED_OK=1; NODE_APPEARS=1
  MKFS_OK=1; MOUNT_RO_OK=1
  export MKFS_OK
}
assert_marker(){ # <gone|kept> <desc>
  if [ "$1" = gone ]; then
    if [ ! -f "$SB/fb-marker" ]; then ok "$2"; else no "$2 (marker still present)"; fi
  else
    if [ -f "$SB/fb-marker" ]; then ok "$2"; else no "$2 (marker wrongly cleared)"; fi
  fi
}
assert_mkfs(){ # <called|never> <desc>
  if [ "$1" = never ]; then
    if [ ! -f "$SB/mkfs-called" ]; then ok "$2"; else no "$2 (mkfs WAS called)"; fi
  else
    if [ -f "$SB/mkfs-called" ]; then ok "$2"; else no "$2 (mkfs not called)"; fi
  fi
}

# 1. new card: p4 absent, create+format+verify all succeed
reset
firstboot_block
assert_marker gone "new card: marker cleared after create+format+verify"
assert_mkfs called "new card: mkfs ran on the just-created partition"

# 2. existing exFAT p4: preserved, NEVER formatted
reset; touch "$SB/p4-present"; MOUNT_RO_OK=1
firstboot_block
assert_marker gone "existing exFAT: marker cleared (recognised)"
assert_mkfs never  "existing exFAT: NEVER formatted"

# 3. existing corrupt/foreign p4: preserved untouched, marker kept for retry
reset; touch "$SB/p4-present"; MOUNT_RO_OK=0
firstboot_block
assert_marker kept "existing corrupt p4: marker kept"
assert_mkfs never  "existing corrupt p4: NEVER formatted (data preserved)"

# 4. p4 absent, sgdisk -e (relocate backup GPT) fails -> no create, no format
reset; SGDISK_E_OK=0
firstboot_block
assert_marker kept "sgdisk -e fail: marker kept"
assert_mkfs never  "sgdisk -e fail: NEVER formatted"

# 5. p4 absent, sgdisk -n (create) fails -> no format
reset; SGDISK_N_OK=0
firstboot_block
assert_marker kept "sgdisk -n fail: marker kept"
assert_mkfs never  "sgdisk -n fail: NEVER formatted"

# 6. p4 absent, created but kernel node never appears (timeout) -> no format
reset; NODE_APPEARS=0
firstboot_block
assert_marker kept "node timeout: marker kept"
assert_mkfs never  "node timeout: NEVER formatted"

# 7. p4 absent, created, node appears, mkfs fails -> marker kept
reset; MKFS_OK=0
firstboot_block
assert_marker kept "mkfs fail: marker kept"
assert_mkfs called "mkfs fail: mkfs attempted on created partition"

# 8. p4 absent, created, formatted, but ro verify mount fails -> marker kept
reset; MOUNT_RO_OK=0
firstboot_block
assert_marker kept "verify-mount fail: marker kept"
assert_mkfs called "verify-mount fail: mkfs ran but marker not cleared"

# 9. p4 absent, no sgdisk, parted path succeeds
reset; HAVE_SGDISK=0; PARTED_OK=1
firstboot_block
assert_marker gone "parted path: marker cleared"
assert_mkfs called "parted path: mkfs ran on created partition"

# 10. p4 absent, neither sgdisk nor parted -> cannot create, no format
reset; HAVE_SGDISK=0; HAVE_PARTED=0
firstboot_block
assert_marker kept "no partition tool: marker kept"
assert_mkfs never  "no partition tool: NEVER formatted"

# 11. not first boot (no marker): block skipped entirely
reset; rm -f "$SB/fb-marker"; touch "$SB/p4-present"
firstboot_block
assert_mkfs never "not first boot: NEVER formatted"

# 12. partx absent is non-fatal (node still appears via sgdisk)
reset; HAVE_PARTX=0
firstboot_block
assert_marker gone "partx absent: still completes"
assert_mkfs called "partx absent: mkfs ran"

# 13. existing p4 must NOT fall through to the create branch even if ro fails
reset; touch "$SB/p4-present"; MOUNT_RO_OK=0; SGDISK_N_OK=1; NODE_APPEARS=1
firstboot_block
assert_mkfs never "existing p4 + ro fail: no create/format fallthrough"
if ! grep -q 'sgdisk -n' "$SB/calls"; then
  ok "existing p4 + ro fail: create branch not entered"
else no "existing p4 + ro fail: create branch WAS entered"; fi

printf '\n== firstboot-data: %s passed, %s failed ==\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ] || exit 1
exit 0