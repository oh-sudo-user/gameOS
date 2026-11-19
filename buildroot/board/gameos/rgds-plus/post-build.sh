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
chmod +x "$TARGET_DIR/etc/gameos/wpa_action.sh" 2>/dev/null || true
chmod +x "$TARGET_DIR/etc/gameos/sleep.d/"* 2>/dev/null || true

# Bluetooth is off by default: gameos-bt starts bluetoothd on demand, so the
# package's boot-time init script must not run it behind our back.
rm -f "$TARGET_DIR/etc/init.d/S40bluetoothd"

# Machine-id for a stable identity
[ -f "$TARGET_DIR/etc/machine-id" ] || echo "gameos-rgdsplus-0001" > "$TARGET_DIR/etc/machine-id"

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
PRETTY_NAME="gameOS $BASE_VER (RG DS Plus)"
HOME_URL="https://github.com/oh-sudo-user/gameOS"
EOF

# Mark first boot (setup script creates the GAMEOS-DATA partition once)
touch "$TARGET_DIR/etc/gameos-firstboot"

# Strip docs we do not need on a 1.2G rootfs
rm -rf "$TARGET_DIR/usr/share/man" "$TARGET_DIR/usr/share/doc" "$TARGET_DIR/usr/share/gtk-doc"