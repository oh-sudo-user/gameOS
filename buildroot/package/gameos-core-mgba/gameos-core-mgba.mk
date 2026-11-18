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
#
# CMAKE_POLICY_VERSION_MINIMUM=3.5: mgba 0.10.5 declares
# cmake_minimum_required(VERSION 3.1); Buildroot 2026.08 ships host
# cmake 4.4, which removed compatibility with < 3.5 and aborts configure
# (local run: "Compatibility with CMake < 3.5 has been removed"). The
# policy floor lets the old project configure under cmake 4.x.
GAMEOS_CORE_MGBA_CONF_OPTS = \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
	-DLIBMGBA_ONLY=ON \
	-DBUILD_LIBRETRO=ON \
	-DM_CORE_GBA=ON \
	-DM_CORE_GB=ON

# cmake-package defaults to IN-SOURCE build (pkg-cmake.mk:
# SUPPORTS_IN_SOURCE_BUILD ?= YES -> BUILDDIR = $(@D)); buildroot-build
# is only used when that is set to NO (like llvm). Run 12 confirmed
# "Build files have been written to: .../gameos-core-mgba-0.10.5" and the
# core links at the source root, so install from $(@D), NOT buildroot-build.
define GAMEOS_CORE_MGBA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mgba_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/mgba_libretro.so
endef

$(eval $(cmake-package))
