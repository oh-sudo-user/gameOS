################################################################################
#
# gameos-core-gpsp — gpSP libretro core (GBA)
#
################################################################################

GAMEOS_CORE_GPSP_VERSION = 5819380c2ffb0900219d700a382ee68c464ebb99
GAMEOS_CORE_GPSP_SITE = $(call github,libretro,gpsp,$(GAMEOS_CORE_GPSP_VERSION))
GAMEOS_CORE_GPSP_LICENSE = GPL-2.0+
GAMEOS_CORE_GPSP_LICENSE_FILES = COPYING

# PGO: pgo/ holds counters from an instrumented build at this same
# output/build path, profiled with gbatris.gba (1800 frames). Host bench:
# +4% fps (1395 -> 1450), identical video/audio hashes (the dynarec-generated code is
# most of the hot path and cannot be profiled at compile time).
# If regenerating these counters: this Makefile resets LDFLAGS with ':=',
# so the -fprofile-generate link flag must be passed on the command line
# (CFLAGS/CXXFLAGS still work from the environment).
GAMEOS_CORE_GPSP_PGO_DIR := \
	$(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))/pgo

GAMEOS_CORE_GPSP_PGO_FLAGS = \
	-fprofile-use=$(GAMEOS_CORE_GPSP_PGO_DIR) -fprofile-correction

# platform=arm64 selects CPU_ARCH=arm64 + HAVE_DYNAREC + MMAP_JIT_CACHE.
# The x86 auto-detection in the Makefile only runs for platform=unix, so
# the arm64 platform must be named explicitly when cross-compiling.
#
# Like melonDS: pass flags through the ENVIRONMENT (the Makefile uses
# 'CFLAGS +='), and only toolchain binaries on the command line.
define GAMEOS_CORE_GPSP_BUILD_CMDS
	$(TARGET_MAKE_ENV) \
	CFLAGS="$(TARGET_CFLAGS) $(GAMEOS_CORE_GPSP_PGO_FLAGS)" \
	CXXFLAGS="$(TARGET_CXXFLAGS) $(GAMEOS_CORE_GPSP_PGO_FLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	$(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		platform=arm64
endef

define GAMEOS_CORE_GPSP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gpsp_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/gpsp_libretro.so
endef

$(eval $(generic-package))
