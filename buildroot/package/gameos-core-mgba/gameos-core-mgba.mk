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

# PGO: pgo/ holds counters from an instrumented in-source build at this
# same output/build path, profiled with 3600 frames of gb240p.gb plus
# 1800 frames of gbatris.gba (GB-weighted: mGBA is the default GB/GBC
# core here, while GBA defaults to game gba/gpSP and mGBA GBA is the
# fallback). Host bench: GB +1%, GBA +6% fps, both with identical
# video/audio hashes. LIBMGBA_ONLY disables mGBA's own BUILD_PGO path,
# so the flags go straight into CMAKE_{C,CXX}_FLAGS.
GAMEOS_CORE_MGBA_PGO_DIR := \
	$(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))/pgo

GAMEOS_CORE_MGBA_PGO_FLAGS = \
	-fprofile-use=$(GAMEOS_CORE_MGBA_PGO_DIR) -fprofile-correction

GAMEOS_CORE_MGBA_CONF_OPTS += \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) $(GAMEOS_CORE_MGBA_PGO_FLAGS)" \
	-DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) $(GAMEOS_CORE_MGBA_PGO_FLAGS)"

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
