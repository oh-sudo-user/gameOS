# gameOS package overrides (loaded via BR2_PACKAGE_OVERRIDE_FILE).
#
# IMPORTANT: this file is included by Buildroot's top Makefile BEFORE the
# package recipes are evaluated, which is required for <pkg>_OVERRIDE_SRCDIR
# to take effect. Putting the override in external.mk does NOT work:
# external.mk is included after package/*/*.mk, so the download step has
# already been decided by then.
#
# Rockchip firmware blobs: the exact rkbin revision ROCKNIX uses for RK3566
# (DDR v1.26 + BL31 v1.46), fetched into vendor/rkbin by scripts/fetch-vendor.sh.
ROCKCHIP_RKBIN_OVERRIDE_SRCDIR = $(BR2_EXTERNAL_GAMEOS_PATH)/../vendor/rkbin
