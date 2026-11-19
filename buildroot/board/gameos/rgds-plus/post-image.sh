#!/bin/sh
# gameOS post-image: assemble the flashable SD card images (one per model)
# and the OTA release artifacts. Called by Buildroot with:
# $1 = BINARIES_DIR, $2 = BR2_CONFIG
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
TARGET_DIR="${TARGET_DIR:-$(dirname "$BR2_CONFIG")/target}"

VERSION="$(cat "$TARGET_DIR/etc/gameos-version" 2>/dev/null || echo unknown)"

echo "==> gameOS: staging boot partition (slots a and b, both dtbs)"
rm -rf "$BINARIES_DIR/boot-stage"
mkdir -p "$BINARIES_DIR/boot-stage/a" "$BINARIES_DIR/boot-stage/b" \
         "$BINARIES_DIR/boot-stage/extlinux"
for slot in a b; do
  cp "$BINARIES_DIR/Image" "$BINARIES_DIR/boot-stage/$slot/"
  cp "$BINARIES_DIR/rk3568-anbernic-rg-ds-plus.dtb" \
     "$BINARIES_DIR/rk3568-anbernic-rg-ds.dtb" \
     "$BINARIES_DIR/boot-stage/$slot/"
done

# Slot state read and rewritten by boot.scr: slot=a|b, fallback=a|b (last
# confirmed-good slot), n=unconfirmed boots. n>3 means the tried slot never
# reached userspace and U-Boot rolls back to the fallback slot.
printf 'slot=a\nfallback=a\nn=0\n' > "$BINARIES_DIR/boot-stage/state"

# Slot B starts empty; the OTA writes a full rootfs into it later.
echo "==> gameOS: zeroing slot B (1200 MiB)"
dd if=/dev/zero of="$BINARIES_DIR/rootfs_b.ext4" bs=1M count=1200 status=none

# One image per model: same kernel + rootfs, only the boot partition differs
# (dtb name in boot.scr/extlinux). Everything else is model-neutral: the
# rootfs is picked by PARTLABEL and userspace detects the model at runtime.
for model in rgds-plus rgds; do
  case "$model" in
    rgds-plus) DTB=rk3568-anbernic-rg-ds-plus.dtb ;;
    *)         DTB=rk3568-anbernic-rg-ds.dtb ;;
  esac
  echo "==> gameOS: [$model] boot partition (dtb $DTB)"

  # Fallback only (boot.scr runs first from CONFIG_BOOTCOMMAND): always slot A.
  # Console on the serial port ONLY: with console=tty0 the kernel and every
  # init script would print boot text on the game panels. The panels instead
  # show gameos-splash (S00splash) from early boot until the UI takes over.
  cat > "$BINARIES_DIR/boot-stage/extlinux/extlinux.conf" <<EOF
LABEL gameOS
  KERNEL /a/Image
  FDT /a/$DTB
  APPEND root=PARTLABEL=GAMEOS-ROOT-A rootwait rw console=ttyS2,1500000n8 loglevel=7 vt.global_cursor_default=0
EOF

  # boot.scr was generated once in post-build.sh (and shipped in the rootfs);
  # copy the matching model's script into this boot partition.
  cp "$TARGET_DIR/usr/share/gameos/boot/boot.scr.$model" "$BINARIES_DIR/boot.scr"

  dd if=/dev/zero of="$BINARIES_DIR/boot.vfat" bs=1M count=256 status=none
  "$HOST_DIR/sbin/mkfs.vfat" -n GAMEOS-BOOT "$BINARIES_DIR/boot.vfat" >/dev/null
  MTOOLS_SKIP_CHECK=1 "$HOST_DIR/bin/mcopy" -s -i "$BINARIES_DIR/boot.vfat" \
    "$BINARIES_DIR/boot-stage/a" \
    "$BINARIES_DIR/boot-stage/b" \
    "$BINARIES_DIR/boot-stage/extlinux" \
    "$BINARIES_DIR/boot-stage/state" \
    "$BINARIES_DIR/boot.scr" ::/

  echo "==> gameOS: [$model] genimage (GPT: boot + rootfs A/B)"
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

  echo "==> gameOS: [$model] packing idbloader (DDR init + SPL)"
  "$HOST_DIR/bin/mkimage" -n rk3568 -T rksd -C bzip2 \
    -d "$BINARIES_DIR/rk3568_ddr_1560MHz_v1.26.bin:$BINARIES_DIR/u-boot-spl.bin" \
    "$BINARIES_DIR/idbloader.img"

  echo "==> gameOS: [$model] writing bootloader areas (sector 64 / 16384)"
  dd if="$BINARIES_DIR/idbloader.img" of="$BINARIES_DIR/sdcard.img" \
     bs=512 seek=64 conv=notrunc,fsync status=none
  dd if="$BINARIES_DIR/u-boot.itb" of="$BINARIES_DIR/sdcard.img" \
     bs=512 seek=16384 conv=notrunc,fsync status=none

  mv "$BINARIES_DIR/sdcard.img" "$BINARIES_DIR/gameOS-$model.img"
  gzip -f "$BINARIES_DIR/gameOS-$model.img"
  ( cd "$BINARIES_DIR" && sha256sum "gameOS-$model.img.gz" > "gameOS-$model.img.gz.sha256" )
  if [ "$model" = rgds-plus ]; then
    PLUS_SHA=$(cut -d' ' -f1 "$BINARIES_DIR/gameOS-$model.img.gz.sha256")
    PLUS_SIZE=$(stat -c %s "$BINARIES_DIR/gameOS-$model.img.gz")
  else
    DS_SHA=$(cut -d' ' -f1 "$BINARIES_DIR/gameOS-$model.img.gz.sha256")
    DS_SIZE=$(stat -c %s "$BINARIES_DIR/gameOS-$model.img.gz")
  fi
done

# --- OTA release artifacts ---------------------------------------------------
# The updater downloads these instead of the whole card image: only the idle
# slot's rootfs and the boot assets are written, never the running system.
# The boot tar carries both dtbs AND both models' boot.scr so an OTA can also
# replace the slot selector (/boot/boot.scr) -- the updater installs only the
# script matching the running model. manifest keys are one-per-line on purpose:
# the updater is a POSIX sh script and reads them with a strict anchored parse.
echo "==> gameOS: OTA release artifacts ($VERSION)"
gzip -9 -c "$BINARIES_DIR/rootfs.ext4" > "$BINARIES_DIR/gameOS-rootfs.ext4.gz"
mkdir -p "$BINARIES_DIR/ota-boot"
cp "$BINARIES_DIR/boot-stage/a/Image" \
   "$BINARIES_DIR/boot-stage/a/rk3568-anbernic-rg-ds-plus.dtb" \
   "$BINARIES_DIR/boot-stage/a/rk3568-anbernic-rg-ds.dtb" \
   "$TARGET_DIR/usr/share/gameos/boot/boot.scr.rgds-plus" \
   "$TARGET_DIR/usr/share/gameos/boot/boot.scr.rgds" \
   "$BINARIES_DIR/ota-boot/"
tar -czf "$BINARIES_DIR/gameOS-boot.tar.gz" -C "$BINARIES_DIR/ota-boot" \
  Image rk3568-anbernic-rg-ds-plus.dtb rk3568-anbernic-rg-ds.dtb \
  boot.scr.rgds-plus boot.scr.rgds

# images/rootfs.ext4 is a symlink to rootfs.ext2 in this Buildroot, so -L is
# required: without it stat reports the 11-byte link name and the updater's
# size gate rejects every download.
ROOTFS_SIZE=$(stat -L -c %s "$BINARIES_DIR/rootfs.ext4")
ROOTFS_GZ=$(stat -c %s "$BINARIES_DIR/gameOS-rootfs.ext4.gz")
ROOTFS_SHA=$(sha256sum "$BINARIES_DIR/gameOS-rootfs.ext4.gz" | cut -d' ' -f1)
BOOT_SHA=$(sha256sum "$BINARIES_DIR/gameOS-boot.tar.gz" | cut -d' ' -f1)
BOOT_SIZE=$(stat -c %s "$BINARIES_DIR/gameOS-boot.tar.gz")

cat > "$BINARIES_DIR/manifest.json" <<EOF
{
  "version": "$VERSION",
  "rootfs": { "file": "gameOS-rootfs.ext4.gz", "sha256": "$ROOTFS_SHA", "size": $ROOTFS_SIZE, "gz": $ROOTFS_GZ },
  "boot": { "file": "gameOS-boot.tar.gz", "sha256": "$BOOT_SHA", "size": $BOOT_SIZE },
  "image_rgds_plus": { "file": "gameOS-rgds-plus.img.gz", "sha256": "$PLUS_SHA", "size": $PLUS_SIZE },
  "image_rgds": { "file": "gameOS-rgds.img.gz", "sha256": "$DS_SHA", "size": $DS_SIZE }
}
EOF

echo "==> done: $BINARIES_DIR/gameOS-rgds-plus.img.gz + gameOS-rgds.img.gz"
echo "==> OTA:   $BINARIES_DIR/manifest.json"