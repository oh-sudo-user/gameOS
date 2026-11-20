#!/bin/sh
# gameOS post-build: final rootfs tweaks before the ext4 image is created.
set -e

TARGET_DIR="$1"

# Make sure init scripts and helpers are executable
chmod +x "$TARGET_DIR/etc/init.d/"S* 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-session" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/ppsspp-run" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-bt" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-wifi" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-update" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-tf2" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-bootstage" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-setup" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-covers" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-perf" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-inputd-supervisor" 2>/dev/null || true
chmod +x "$TARGET_DIR/etc/gameos/wpa_action.sh" 2>/dev/null || true
chmod +x "$TARGET_DIR/etc/gameos/sleep.d/"* 2>/dev/null || true

# Refuse to ship files that still carry CRLF (a Windows checkout used to
# arrive this way): /bin/sh^M is "not found" on the device, inittab and
# kconfig silently mis-parse, and boot.cmd would bake CRs into boot.scr.
# Fail the build with an actionable message instead of flashing a dead image.
if grep -rlI "$(printf '\r')" \
        "$TARGET_DIR/etc/init.d" "$TARGET_DIR/etc/gameos" "$TARGET_DIR/etc/inittab" \
        "$TARGET_DIR/etc/udev/rules.d" "$TARGET_DIR/etc/modprobe.d" \
        "$TARGET_DIR/usr/bin" "$TARGET_DIR/usr/share/gameos" 2>/dev/null | grep -q .; then
  echo "ERROR: CRLF line endings in shipped files; run 'git checkout -- .' and rebuild" >&2
  exit 1
fi

# Bluetooth is off by default: gameos-bt starts bluetoothd on demand, so the
# package's boot-time init script must not run it behind our back.
rm -f "$TARGET_DIR/etc/init.d/S40bluetoothd"

# Machine-id for a stable identity
[ -f "$TARGET_DIR/etc/machine-id" ] || echo "gameos-rgds-0001" > "$TARGET_DIR/etc/machine-id"

# Build version, shown by Settings > System update and compared against the
# release manifest by gameos-update. The release tag is bumped by editing
# board/gameos/rgds-plus/version; the git suffix identifies the exact build.
BOARD_DIR="$(cd "$(dirname "$0")" && pwd)"
VER="$(cat "$BOARD_DIR/version" 2>/dev/null | head -n 1)"
[ -n "$VER" ] || VER="0.0.0"
GIT_SHA="$(git -C "$BOARD_DIR/../../.." rev-parse --short HEAD 2>/dev/null || true)"
if [ -n "$GIT_SHA" ]; then
  case "$VER" in
    *"+g"*) ;;
    *) VER="$VER+g$GIT_SHA" ;;
  esac
fi
echo "$VER" > "$TARGET_DIR/etc/gameos-version"

# os-release (same version string as /etc/gameos-version, minus the git suffix)
BASE_VER="${VER%%+g*}"
cat > "$TARGET_DIR/usr/lib/os-release" <<EOF
NAME="gameOS"
VERSION="$VER"
ID=gameos
VERSION_ID="$BASE_VER"
PRETTY_NAME="gameOS $BASE_VER (RG DS / RG DS Plus)"
HOME_URL="https://github.com/oh-sudo-user/gameOS"
EOF

# Boot slot selectors for BOTH models, carried inside the rootfs. A confirmed
# boot reconciles /boot/boot.scr against these (S99update), so a device upgraded
# by an OLD updater that never replaced the boot script still gets the fixed
# slot selector on its first confirmed boot into the new rootfs. post-image.sh
# copies these same files into each model's boot partition and the OTA boot
# tar -- one generation point. host-uboot-tools is built before target finalize
# (uboot depends on it), so mkimage is present here.
HOST_DIR="$(dirname "$TARGET_DIR")/host"
BOOTSCR_DIR="$TARGET_DIR/usr/share/gameos/boot"
mkdir -p "$BOOTSCR_DIR"
for model in rgds-plus rgds; do
  case "$model" in
    rgds-plus) dtb=rk3568-anbernic-rg-ds-plus.dtb ;;
    *)         dtb=rk3568-anbernic-rg-ds.dtb ;;
  esac
  sed "s/@DTB@/$dtb/g" "$BOARD_DIR/boot.cmd" > "$BOOTSCR_DIR/boot.$model.cmd"
  "$HOST_DIR/bin/mkimage" -A arm64 -O linux -T script -C none \
    -n "gameOS slot selector ($model)" -d "$BOOTSCR_DIR/boot.$model.cmd" \
    "$BOOTSCR_DIR/boot.scr.$model" >/dev/null
  rm -f "$BOOTSCR_DIR/boot.$model.cmd"
done

# Mark first boot (setup script creates the GAMEOS-DATA partition once)
touch "$TARGET_DIR/etc/gameos-firstboot"

# Strip docs we do not need on a 1.2G rootfs
rm -rf "$TARGET_DIR/usr/share/man" "$TARGET_DIR/usr/share/doc" "$TARGET_DIR/usr/share/gtk-doc"