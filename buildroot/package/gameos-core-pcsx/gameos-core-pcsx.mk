################################################################################
#
# gameos-core-pcsx — PCSX-ReARMed libretro core (PlayStation 1)
#
################################################################################

GAMEOS_CORE_PCSX_VERSION = 7b0e8f8fadfbce3d170d195b9d9eecaabfa08c8e
GAMEOS_CORE_PCSX_SITE = $(call github,libretro,pcsx_rearmed,$(GAMEOS_CORE_PCSX_VERSION))
GAMEOS_CORE_PCSX_LICENSE = GPL-2.0+
GAMEOS_CORE_PCSX_LICENSE_FILES = COPYING
GAMEOS_CORE_PCSX_DEPENDENCIES = zlib libpng

# Same libretro Makefile caveat as gameos-core-melonds: flags must go
# through the environment, not the make command line, or the core's
# own 'CFLAGS +=' include paths/defines are overridden. CC/CXX on the
# command line let Makefile.libretro auto-detect aarch64 (ari64 dynarec
# + neon GPU); final linking uses CC_LINK=$(CC).
define GAMEOS_CORE_PCSX_BUILD_CMDS
	$(TARGET_MAKE_ENV) \
	CFLAGS="$(TARGET_CFLAGS)" \
	CXXFLAGS="$(TARGET_CXXFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	$(MAKE) -C $(@D) -f Makefile.libretro \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" LD="$(TARGET_LD)" \
		platform=unix ARCH=arm64
endef

define GAMEOS_CORE_PCSX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pcsx_rearmed_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/pcsx_rearmed_libretro.so
endef

$(eval $(generic-package))