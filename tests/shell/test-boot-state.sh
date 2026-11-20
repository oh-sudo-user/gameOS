#!/bin/sh
# Tests for the Linux-side boot-state logic in S99update (audit doc 2.3).
#
# confirm()          -- a confirmed UI records slot=fallback=<current>, n=0.
# reconcile_bootscr()-- atomically deploys this rootfs's model-matched boot.scr
#                       on the first confirmed boot (fixes old-updater devices).
# start gate         -- confirm runs ONLY after /run/gameos/ui-ok exists; a UI
#                       that never comes up leaves n climbing (U-Boot rolls back).
#
# The U-Boot-side n>3 rollback itself runs in boot.scr (HUSH, not POSIX sh) and
# is covered by the U-Boot sandbox plan in tests/integration/test-boot-state.md.
# Here we test the POSIX-sh functions with stubbed mount I/O and sandbox paths.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCRIPT="$ROOT/buildroot/board/gameos/rgds-plus/rootfs-overlay/etc/init.d/S99update"

PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); printf 'ok   %s\n' "$1"; }
no(){ FAIL=$((FAIL+1)); printf 'FAIL %s\n' "$1"; }

SB=$(mktemp -d)
trap 'rm -rf "$SB"' EXIT INT TERM
mkdir -p "$SB/bin"
export SB
PATH="$SB/bin:$PATH"
printf '#!/bin/sh\nexit 0\n' > "$SB/bin/gameos-update"; chmod +x "$SB/bin/gameos-update"

pathsed(){
  sed -e "s#/usr/share/gameos/boot#$SB/share#g" \
      -e "s#/boot/#$SB/boot/#g" \
      -e "s#/boot\([ ;]\)#$SB/boot\1#g" \
      -e "s#/proc/cmdline#$SB/cmdline#g" \
      -e "s#/proc/device-tree/compatible#$SB/compatible#g" \
      -e "s#/dev/disk/by-partlabel#$SB/by-partlabel#g" \
      -e "s#/run/gameos#$SB/run#g" \
      -e "s#/tmp/gameos-setup.log#$SB/setup.log#g"
}

# Part A sourceable copy: confirm() + reconcile_bootscr() only.
awk '/^confirm[(][)] [{]/{f=1} /^case /{f=0} f{print}' "$SCRIPT" \
  | pathsed > "$SB/funcs.sh"
# shellcheck disable=SC1091
. "$SB/funcs.sh"

# Part B sourceable copy: full script, background subshell made synchronous.
pathsed < "$SCRIPT" | sed 's#) &#)#' > "$SB/full.sh"

# --- stubs ------------------------------------------------------------------
sync(){ :; }
sleep(){ :; }
ip(){ echo "default via 192.168.1.1 dev wlan0"; }
mountpoint(){ [ "$BOOT_MOUNTED" = 1 ]; }
mount(){ [ "$MOUNT_OK" = 1 ] && { BOOT_MOUNTED=1; return 0; }; return 1; }

reset(){
  rm -rf "$SB/boot" "$SB/share" "$SB/run" "$SB/by-partlabel" "$SB/notblock" "$SB/setup.log"
  mkdir -p "$SB/boot" "$SB/share" "$SB/run" "$SB/by-partlabel"
  printf 'slot=b\nfallback=a\nn=3\n' > "$SB/boot/state"
  printf 'old-boot-scr\n' > "$SB/boot/boot.scr"
  printf 'NEW-PLUS-SCR\n' > "$SB/share/boot.scr.rgds-plus"
  printf 'NEW-DS-SCR\n' > "$SB/share/boot.scr.rgds"
  printf 'BOOT_IMAGE=/a/Image root=PARTLABEL=GAMEOS-ROOT-A rw\n' > "$SB/cmdline"
  printf 'anbernic,rg-ds-plus\000rockchip,rk3568\000' > "$SB/compatible"
  : > "$SB/notblock"; ln -sf "$SB/notblock" "$SB/by-partlabel/GAMEOS-BOOT"
  BOOT_MOUNTED=1; MOUNT_OK=1
}
field(){ sed -n "s/^$1=//p" "$SB/boot/state" | head -n1; }
expect_field(){ # <key> <want> <desc>
  got=$(field "$1")
  if [ "$got" = "$2" ]; then ok "$3"; else no "$3 ($1=$got want $2)"; fi
}

# --- confirm() --------------------------------------------------------------
reset; confirm
expect_field slot a "confirm: running slot A recorded"
expect_field fallback a "confirm: fallback = confirmed slot"
expect_field n 0 "confirm: boot counter reset to 0"

reset; printf 'root=PARTLABEL=GAMEOS-ROOT-B rw\n' > "$SB/cmdline"; confirm
expect_field slot b "confirm: running slot B recorded"
expect_field fallback b "confirm: slot B becomes fallback"

reset; confirm
expect_field n 0 "confirm: old n=3 cleared on confirmation"

reset; printf 'no root label here\n' > "$SB/cmdline"; confirm
expect_field slot a "confirm: missing label defaults to A"

reset; BOOT_MOUNTED=0; MOUNT_OK=0
if confirm; then r=0; else r=1; fi
if [ "$r" = 1 ]; then ok "confirm: returns 1 when /boot unmountable"; else no "confirm: should fail when /boot unmountable"; fi
expect_field slot b "confirm: unmountable /boot leaves state untouched"
expect_field n 3 "confirm: unmountable /boot does NOT clear n"

# --- reconcile_bootscr() ----------------------------------------------------
reset; reconcile_bootscr
if [ "$(cat "$SB/boot/boot.scr")" = "NEW-PLUS-SCR" ]; then
  ok "reconcile: plus model boot.scr deployed"
else no "reconcile: plus model boot.scr deployed"; fi
if [ ! -f "$SB/boot/boot.scr.new" ]; then ok "reconcile: atomic (no .new left)"; else no "reconcile: .new residue"; fi

reset; printf 'NEW-PLUS-SCR\n' > "$SB/boot/boot.scr"; reconcile_bootscr
if [ "$(cat "$SB/boot/boot.scr")" = "NEW-PLUS-SCR" ]; then
  ok "reconcile: identical hash -> no rewrite"
else no "reconcile: identical hash changed content"; fi

reset; rm -f "$SB/share/boot.scr.rgds-plus"; reconcile_bootscr
if [ "$(cat "$SB/boot/boot.scr")" = "old-boot-scr" ]; then
  ok "reconcile: missing want -> no change"
else no "reconcile: missing want should not change boot.scr"; fi

reset; printf 'anbernic,rg-ds\000rockchip,rk3568\000' > "$SB/compatible"; reconcile_bootscr
if [ "$(cat "$SB/boot/boot.scr")" = "NEW-DS-SCR" ]; then
  ok "reconcile: rgds model picks boot.scr.rgds"
else no "reconcile: rgds model picks boot.scr.rgds"; fi

reset; BOOT_MOUNTED=0; reconcile_bootscr
if [ "$(cat "$SB/boot/boot.scr")" = "old-boot-scr" ]; then
  ok "reconcile: /boot unmounted -> no change"
else no "reconcile: should not touch boot.scr when /boot unmounted"; fi

# --- start gate: confirm only after ui-ok -----------------------------------
reset; rm -f "$SB/run/ui-ok"
( set -- start; . "$SB/full.sh" )
expect_field n 3 "gate: no ui-ok -> n NOT cleared (rollback preserved)"
expect_field slot b "gate: no ui-ok -> slot NOT confirmed"

reset; touch "$SB/run/ui-ok"
( set -- start; . "$SB/full.sh" )
expect_field n 0 "gate: ui-ok present -> confirm clears n"
expect_field slot a "gate: ui-ok present -> slot confirmed"

printf '\n== boot-state: %s passed, %s failed ==\n' "$PASS" "$FAIL"
[ "$FAIL" = 0 ] || exit 1
exit 0