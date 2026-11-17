# gameOS external tree for Buildroot

include $(sort $(wildcard $(BR2_EXTERNAL_GAMEOS_PATH)/package/*/*.mk))

# ---------------------------------------------------------------------------
# Rockchip firmware blobs: the exact rkbin revision ROCKNIX uses for RK3566
# (DDR v1.26 + BL31 v1.46). OVERRIDE_SRCDIR points at the local vendor
# directory created by scripts/fetch-vendor.sh, bypassing download/hash checks.
# ---------------------------------------------------------------------------
ROCKCHIP_RKBIN_OVERRIDE_SRCDIR = $(BR2_EXTERNAL_GAMEOS_PATH)/../vendor/rkbin

# ---------------------------------------------------------------------------
# Apply the vendored kernel patch stack exactly like ROCKNIX does:
# series order (default -> RK3566 device) and plain `patch -p1` semantics
# (fuzz 2) on mainline 7.2.11. Buildroot's built-in patch step uses -F0
# (zero fuzz) and rejects upstream patches that need fuzz, so the patch
# dir is NOT passed to Buildroot; we apply here in post-extract instead.
# ---------------------------------------------------------------------------
define GAMEOS_LINUX_APPLY_PATCHES
	while read p; do \
	  echo "gameOS: applying kernel patch $$p"; \
	  patch -p1 -N -t -d $(@D) \
	    < $(BR2_EXTERNAL_GAMEOS_PATH)/board/gameos/rgds-plus/kernel-patches/$$p \
	    || exit 1; \
	done < $(BR2_EXTERNAL_GAMEOS_PATH)/board/gameos/rgds-plus/kernel-patches/series
endef
LINUX_POST_EXTRACT_HOOKS += GAMEOS_LINUX_APPLY_PATCHES

# ---------------------------------------------------------------------------
# RG DS / RG DS Plus kernel integration, mirroring ROCKNIX's proven order
# (projects/ROCKNIX/packages/linux/package.mk post_patch):
#   1. all kernel patches are applied first (GAMEOS_LINUX_APPLY_PATCHES,
#      ordered by kernel-patches/series: default -> RK3566 device)
#   2. THEN the vendored device trees are copied over the in-tree ones
#   3. THEN panel-generic-dsi.c is added to the DRM panel directory and
#      built IN (obj-y), not as a module, so both DSI panels are available
#      before the DRM driver binds — no modprobe race at boot.
# ---------------------------------------------------------------------------
define GAMEOS_LINUX_INSTALL_DTS
	cp -f $(BR2_EXTERNAL_GAMEOS_PATH)/board/gameos/rgds-plus/dts/rk3568-anbernic-rg-ds.dtsi \
	      $(BR2_EXTERNAL_GAMEOS_PATH)/board/gameos/rgds-plus/dts/rk3568-anbernic-rg-ds.dts \
	      $(BR2_EXTERNAL_GAMEOS_PATH)/board/gameos/rgds-plus/dts/rk3568-anbernic-rg-ds-plus.dts \
	      $(@D)/arch/arm64/boot/dts/rockchip/
	cp -f $(BR2_EXTERNAL_GAMEOS_PATH)/board/gameos/rgds-plus/panel-generic-dsi.c \
	      $(@D)/drivers/gpu/drm/panel/
	grep -q 'panel-generic-dsi.o' $(@D)/drivers/gpu/drm/panel/Makefile || \
	      echo 'obj-y += panel-generic-dsi.o' >> $(@D)/drivers/gpu/drm/panel/Makefile
endef
LINUX_POST_PATCH_HOOKS += GAMEOS_LINUX_INSTALL_DTS