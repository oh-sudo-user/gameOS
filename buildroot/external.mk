# gameOS external tree for Buildroot

include $(sort $(wildcard $(BR2_EXTERNAL_GAMEOS_PATH)/package/*/*.mk))

# NOTE: the rockchip-rkbin OVERRIDE_SRCDIR lives in local.mk (referenced by
# BR2_PACKAGE_OVERRIDE_FILE in the defconfig), NOT here: external.mk is
# included after the package recipes are evaluated, too late for overrides.

# ---------------------------------------------------------------------------
# Apply the vendored kernel patch stack in its pinned order:
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
# RG DS / RG DS Plus kernel integration, in the order proven on this SoC:
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
	# $$, not $: make expands $(...) inside a define before the shell sees
	# it, so a single $ would emit a dead "dtb- +=" line into the kernel
	# Makefile. Drop any previous append first so re-runs repair old trees.
	sed -i '/+= rk3568-anbernic-rg-ds-plus\.dtb$$/d' \
	      $(@D)/arch/arm64/boot/dts/rockchip/Makefile
	printf 'dtb-$$(CONFIG_ARCH_ROCKCHIP) += rk3568-anbernic-rg-ds-plus.dtb\n' \
	      >> $(@D)/arch/arm64/boot/dts/rockchip/Makefile
endef
LINUX_POST_PATCH_HOOKS += GAMEOS_LINUX_INSTALL_DTS