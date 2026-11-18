#!/bin/sh
# gameOS post-image: assemble the flashable SD card image.
# Called by Buildroot with: $1 = BINARIES_DIR, $2 = BR2_CONFIG
set -e

BINARIES_DIR="$1"
BR2_CONFIG="${2:-}"
BOARD_DIR="$(cd "$(dirname "$0")" && pwd)"

# Buildroot exports HOST_DIR/BUILD_DIR via EXTRA_ENV; derive fallbacks anyway.
if [ -z "${HOST_DIR:-}" ] && [ -n "$BR2_CONFIG" ]; then
  HOST_DIR="$(dirname "$BR2_CONFIG")/host"
fi
if [ -z "${BUILD_DIR:-}" ] && [ -n "$BR2_CONFIG" ]; then
  BUILD_DIR="$(dirname "$BR2_CONFIG")/build"
fi

echo "==> gameOS: staging boot partition"
rm -rf "$BINARIES_DIR/boot-stage"
mkdir -p "$BINARIES_DIR/boot-stage/extlinux"
cp "$BINARIES_DIR/Image" "$BINARIES_DIR/boot-stage/"
cp "$BINARIES_DIR/rk3568-anbernic-rg-ds-plus.dtb" \
   "$BINARIES_DIR/rk3568-anbernic-rg-ds.dtb" \
   "$BINARIES_DIR/boot-stage/"

cat > "$BINARIES_DIR/boot-stage/extlinux/extlinux.conf" <<'EOF'
LABEL gameOS
  KERNEL /Image
  FDT /rk3568-anbernic-rg-ds-plus.dtb
  APPEND root=PARTLABEL=GAMEOS-ROOT rootwait rw console=ttyS2,1500000n8 console=tty0 quiet loglevel=3 vt.global_cursor_default=0
EOF

echo "==> gameOS: creating boot.vfat"
dd if=/dev/zero of="$BINARIES_DIR/boot.vfat" bs=1M count=256 status=none
"$HOST_DIR/sbin/mkfs.vfat" -n GAMEOS-BOOT "$BINARIES_DIR/boot.vfat" >/dev/null
MTOOLS_SKIP_CHECK=1 "$HOST_DIR/bin/mcopy" -s -i "$BINARIES_DIR/boot.vfat" \
  "$BINARIES_DIR/boot-stage/Image" \
  "$BINARIES_DIR/boot-stage/rk3568-anbernic-rg-ds-plus.dtb" \
  "$BINARIES_DIR/boot-stage/rk3568-anbernic-rg-ds.dtb" \
  "$BINARIES_DIR/boot-stage/extlinux" ::/

echo "==> gameOS: genimage (GPT: boot + rootfs)"
# genimage refuses a non-empty tmppath. A previous run interrupted during
# genimage can leave residue in this project-specific tmp dir, so clear it
# before each call (genimage normally cleans it on success).
rm -rf "$BUILD_DIR/genimage.tmp"
mkdir -p "$BUILD_DIR/genimage.root" "$BUILD_DIR/genimage.tmp"
"$HOST_DIR/bin/genimage" \
  --config "$BOARD_DIR/genimage.cfg" \
  --rootpath "$BUILD_DIR/genimage.root" \
  --tmppath "$BUILD_DIR/genimage.tmp" \
  --inputpath "$BINARIES_DIR" \
  --outputpath "$BINARIES_DIR"

echo "==> gameOS: packing idbloader (DDR init + SPL)"
"$HOST_DIR/bin/mkimage" -n rk3568 -T rksd -C bzip2 \
  -d "$BINARIES_DIR/rk3568_ddr_1560MHz_v1.26.bin:$BINARIES_DIR/u-boot-spl.bin" \
  "$BINARIES_DIR/idbloader.img"

echo "==> gameOS: writing bootloader areas (sector 64 / 16384)"
dd if="$BINARIES_DIR/idbloader.img" of="$BINARIES_DIR/sdcard.img" \
   bs=512 seek=64 conv=notrunc,fsync status=none
dd if="$BINARIES_DIR/u-boot.itb" of="$BINARIES_DIR/sdcard.img" \
   bs=512 seek=16384 conv=notrunc,fsync status=none

echo "==> gameOS: final image"
mv "$BINARIES_DIR/sdcard.img" "$BINARIES_DIR/gameOS-rgds-plus.img"
gzip -f "$BINARIES_DIR/gameOS-rgds-plus.img"
( cd "$BINARIES_DIR" && sha256sum gameOS-rgds-plus.img.gz > gameOS-rgds-plus.img.gz.sha256 )
echo "==> done: $BINARIES_DIR/gameOS-rgds-plus.img.gz"