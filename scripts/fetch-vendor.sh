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

# Fonts are pinned to exact upstream commits, not moving branches, and the
# SHA-256 below is checked on every run (cached or freshly downloaded): a
# changed/compromised font fails the build instead of silently shipping.
NOTO_PIN=f8d157532fbfaeda587e826d4cd5b21a49186f7c
ORBITRON_PIN=06bfe0d62a98375cb09c942fa18e68bd32cff057
NOTO_URL="https://raw.githubusercontent.com/notofonts/noto-cjk/$NOTO_PIN/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
ORBITRON_URL="https://raw.githubusercontent.com/google/fonts/$ORBITRON_PIN/ofl/orbitron/Orbitron%5Bwght%5D.ttf"
NOTO_SHA256=faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9
ORBITRON_SHA256=f42db2dd16e642258e35782916eceb1dcdbea06fb958d77ad71dc5963587e8fd

# rkbin blobs are pinned by commit above AND by hash here: a swapped blob at
# the same URL must fail the build, not just a missing file.
RKBIN_DDR_SHA256=acb4e054298f0f0c8471dd3d45de07a1c77b67e687ec6e77ede365a087f1d95a
RKBIN_BL31_SHA256=c81ac7e8e1fd727cf7f0db62a9aaea760bde2b270e34d98eb264a264b86df749

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
  case "$f" in
    rk3568_ddr_1560MHz_v1.26.bin) want="$RKBIN_DDR_SHA256" ;;
    rk3568_bl31_v1.46.elf)        want="$RKBIN_BL31_SHA256" ;;
    *) echo "ERROR: no pinned hash for $f"; exit 1 ;;
  esac
  got=$(sha256sum "$ROOT/vendor/rkbin/bin/rk35/$f" | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "ERROR: sha256 mismatch for $f (got $got want $want)"; exit 1; }
done
echo "    DDR init + BL31 verified (pinned hashes)"

echo "==> Fetching fonts (Noto Sans SC for CJK, Orbitron for display)"
FONTS="$BOARD/rootfs-overlay/usr/share/gameos/fonts"
mkdir -p "$FONTS"
fetch_font() {  # fetch_font <url> <destination> <sha256>
  if [ ! -s "$2" ]; then
    echo "    downloading $(basename "$2")"
    curl -fL --retry 3 -o "$2.part" "$1" || { rm -f "$2.part"; exit 1; }
    mv -f "$2.part" "$2"
  fi
  echo "$3  $2" | sha256sum -c - >/dev/null 2>&1 \
    || { echo "ERROR: sha256 mismatch for $2 (expected $3)"; exit 1; }
}
fetch_font "$NOTO_URL" "$FONTS/NotoSansSC-Regular.otf" "$NOTO_SHA256"
fetch_font "$ORBITRON_URL" "$FONTS/Orbitron.ttf" "$ORBITRON_SHA256"
echo "    font hashes verified"

echo "==> Vendor fetch complete."