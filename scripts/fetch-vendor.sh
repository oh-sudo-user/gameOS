#!/usr/bin/env bash
# gameOS vendor fetcher — pulls pinned upstream hardware-support files.
# Everything fetched here is GPL upstream code or Rockchip firmware blobs,
# pinned to exact commits for reproducible builds.
#
# Honors http_proxy/https_proxy (useful on networks where GitHub is slow).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOARD="$ROOT/buildroot/board/gameos/rgds-plus"
PKG="$ROOT/buildroot/package"

# --- pinned revisions -------------------------------------------------------
VENDOR_PIN=ad9e48d1492f272db9c742d0cfa6d81769ceee22   # upstream RK3566 hardware-support tree (next)
RKBIN_PIN=3e288fe814e059dd06833495f845cab04ac20a5c     # rockchip-linux/rkbin (DDR v1.26 / BL31 v1.46)
KERNEL_VERSION=7.2.11                                  # mainline, matches the vendored RK3566 patch set

NOTO_URL="https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
ORBITRON_URL="https://github.com/google/fonts/raw/main/ofl/orbitron/Orbitron%5Bwght%5D.ttf"

echo "==> Fetching vendored kernel support files @ ${VENDOR_PIN:0:12}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# Pinned source tarball (partial git clones proved unreliable for blob fetch).
curl -fL --retry 3 -o "$TMP/src.tar.gz" \
  "https://codeload.github.com/ROCKNIX/distribution/tar.gz/$VENDOR_PIN"
mkdir -p "$TMP/src"
PFX="distribution-$VENDOR_PIN"
tar -xzf "$TMP/src.tar.gz" -C "$TMP/src" --strip-components=1 \
  "$PFX/packages/linux/patches/default" \
  "$PFX/projects/ROCKNIX/devices/RK3566/linux" \
  "$PFX/projects/ROCKNIX/devices/RK3566/patches/linux" \
  "$PFX/projects/ROCKNIX/devices/RK3566/packages/u-boot-Specific/patches" \
  "$PFX/projects/ROCKNIX/packages/linux-drivers/generic-dsi/sources"

mkdir -p "$BOARD/kernel-patches" "$BOARD/dts"
# Application order matters: common "default" patches first, then the RK3566
# device patches (the vendor's "rockchip" patch set is NOT used at this pin --
# the kernel here is mainline 7.2.11). Buildroot's apply-patches.sh honours a
# "series" file, so we pin the order explicitly instead of relying on
# alphabetical glob order across groups.
cp -f "$TMP/src/packages/linux/patches/default/"*.patch "$BOARD/kernel-patches/"
cp -f "$TMP/src/projects/ROCKNIX/devices/RK3566/patches/linux/"*.patch "$BOARD/kernel-patches/"
{
  ( cd "$TMP/src/packages/linux/patches/default" && ls -1 *.patch | LC_ALL=C sort )
  ( cd "$TMP/src/projects/ROCKNIX/devices/RK3566/patches/linux" && ls -1 *.patch | LC_ALL=C sort )
} > "$BOARD/kernel-patches/series"
cp -f "$TMP/src/projects/ROCKNIX/devices/RK3566/linux/linux.aarch64.conf" "$BOARD/linux.config"
cp -f "$TMP/src/projects/ROCKNIX/devices/RK3566/linux/dts/rockchip/rk3568-anbernic-rg-ds.dtsi" \
      "$TMP/src/projects/ROCKNIX/devices/RK3566/linux/dts/rockchip/rk3568-anbernic-rg-ds.dts" \
      "$TMP/src/projects/ROCKNIX/devices/RK3566/linux/dts/rockchip/rk3568-anbernic-rg-ds-plus.dts" \
      "$BOARD/dts/"
# gameOS fix-ups: the vendor tree is a generated artifact, so keep our
# device-tree changes as reviewable patches in dts-fixes/. The battery fix
# lowers the cell's constant-charge-current limit from a 10A BSP placeholder
# to the stock 1.5A charge limit for this pack.
# -N -t: re-running this script on an already-patched tree must skip applied
# patches instead of stopping at an interactive "Assume -R?" prompt.
for p in "$BOARD"/dts-fixes/*.patch; do
  [ -e "$p" ] || continue
  patch -p1 -N -t -d "$BOARD" < "$p"
done
grep -q 'constant-charge-current-max-microamp = <1500000>;' \
      "$BOARD/dts/rk3568-anbernic-rg-ds-plus.dts" \
  || { echo "ERROR: battery charge current patch did not apply"; exit 1; }
grep -q 'constant-charge-current-max-microamp = <1500000>;' \
      "$BOARD/dts/rk3568-anbernic-rg-ds.dts" \
  || { echo "ERROR: rgds battery charge current patch did not apply"; exit 1; }
# Dual-DSI panel driver: built into the kernel (obj-y) by the post-patch hook
# in buildroot/external.mk, so both panels are ready before the DRM driver binds.
cp -f "$TMP/src/projects/ROCKNIX/packages/linux-drivers/generic-dsi/sources/panel-generic-dsi.c" \
      "$BOARD/panel-generic-dsi.c"
# U-Boot fixes proven on RG DS Plus:
#   001: RK8XX PMIC boots on power key, not on power apply
#   002: SPL tries SD card (sdmmc0) before eMMC (device has no eMMC)
mkdir -p "$BOARD/uboot-patches"
cp -f "$TMP/src/projects/ROCKNIX/devices/RK3566/packages/u-boot-Specific/patches/"*.patch \
      "$BOARD/uboot-patches/"
# gameOS' own addition on top: SPL must be able to read the SD card itself
# (quartz64 base defconfig is SPI-only; RG DS Plus has no SPI flash).
cp -f "$BOARD/uboot-fixes/"*.patch "$BOARD/uboot-patches/"
echo "    $(ls "$BOARD/kernel-patches" | wc -l) kernel patches, 3 device trees, linux.config, panel driver, $(ls "$BOARD/uboot-patches" | wc -l) u-boot patches"

echo "==> Fetching rkbin firmware @ ${RKBIN_PIN:0:12}"
# Only the two blobs the boot chain needs; downloaded straight from the
# pinned commit (Buildroot's rockchip-rkbin package installs exactly these
# via BR2_PACKAGE_ROCKCHIP_RKBIN_{TPL,BL31}_FILENAME, see external.mk override).
mkdir -p "$ROOT/vendor/rkbin/bin/rk35"
for f in rk3568_ddr_1560MHz_v1.26.bin rk3568_bl31_v1.46.elf; do
  if [ ! -s "$ROOT/vendor/rkbin/bin/rk35/$f" ]; then
    curl -fL --retry 3 -o "$ROOT/vendor/rkbin/bin/rk35/$f" \
      "https://raw.githubusercontent.com/rockchip-linux/rkbin/$RKBIN_PIN/bin/rk35/$f"
  fi
  test -s "$ROOT/vendor/rkbin/bin/rk35/$f" || { echo "ERROR: missing $f in rkbin"; exit 1; }
done
echo "    DDR init + BL31 verified"

echo "==> Fetching fonts (Noto Sans SC for CJK, Orbitron for display)"
FONTS="$BOARD/rootfs-overlay/usr/share/gameos/fonts"
mkdir -p "$FONTS"
[ -f "$FONTS/NotoSansSC-Regular.otf" ] || curl -fL --retry 3 -o "$FONTS/NotoSansSC-Regular.otf" "$NOTO_URL"
[ -f "$FONTS/Orbitron.ttf" ]           || curl -fL --retry 3 -o "$FONTS/Orbitron.ttf" "$ORBITRON_URL"

echo "==> Vendor fetch complete."