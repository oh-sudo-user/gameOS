################################################################################
#
# gameos-core-mgba — mGBA libretro core (GBA / GBC)
#
################################################################################

GAMEOS_CORE_MGBA_VERSION = 0.10.5
GAMEOS_CORE_MGBA_SITE = $(call github,mgba-emu,mgba,$(GAMEOS_CORE_MGBA_VERSION))
GAMEOS_CORE_MGBA_LICENSE = MPL-2.0
GAMEOS_CORE_MGBA_LICENSE_FILES = LICENSE
GAMEOS_CORE_MGBA_INSTALL_STAGING = NO
# INSTALL_TARGET must stay YES (default): = NO would skip the whole
# install-target step INCLUDING the custom CMDS below, silently leaving
# mgba_libretro.so out of the rootfs (run 10 log showed no install step).
# The custom CMDS override cmake's default 'cmake --install' (ifndef guard
# in pkg-cmake.mk), copying just the libretro core.

# mGBA 0.10.5 has NO Makefile in src/platform/libretro — the core is a
# CMake target. LIBMGBA_ONLY builds the minimal static core with no
# external dependencies (bundled sources), exactly what a libretro
# core needs; BUILD_LIBRETRO then produces mgba_libretro.so.
GAMEOS_CORE_MGBA_CONF_OPTS = \
	-DLIBMGBA_ONLY=ON \
	-DBUILD_LIBRETRO=ON \
	-DM_CORE_GBA=ON \
	-DM_CORE_GB=ON

define GAMEOS_CORE_MGBA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/buildroot-build/mgba_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/mgba_libretro.so
endef

$(eval $(cmake-package))
