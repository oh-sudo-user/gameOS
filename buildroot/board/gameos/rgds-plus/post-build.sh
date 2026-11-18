#!/bin/sh
# gameOS post-build: final rootfs tweaks before the ext4 image is created.
set -e

TARGET_DIR="$1"

# Make sure init scripts and helpers are executable
chmod +x "$TARGET_DIR/etc/init.d/"S* 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/gameos-session" 2>/dev/null || true
chmod +x "$TARGET_DIR/usr/bin/ppsspp-run" 2>/dev/null || true
chmod +x "$TARGET_DIR/etc/gameos/sleep.d/"* 2>/dev/null || true

# Machine-id for a stable identity
[ -f "$TARGET_DIR/etc/machine-id" ] || echo "gameos-rgdsplus-0001" > "$TARGET_DIR/etc/machine-id"

# os-release
cat > "$TARGET_DIR/usr/lib/os-release" <<EOF
NAME="gameOS"
VERSION="1.0"
ID=gameos
VERSION_ID=1.0
PRETTY_NAME="gameOS 1.0 (RG DS Plus)"
HOME_URL="https://github.com/oh-sudo-user/gameOS"
EOF

# Mark first boot (setup script creates the GAMEOS-DATA partition once)
touch "$TARGET_DIR/etc/gameos-firstboot"

# Strip docs we do not need on a 1.2G rootfs
rm -rf "$TARGET_DIR/usr/share/man" "$TARGET_DIR/usr/share/doc" "$TARGET_DIR/usr/share/gtk-doc"