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

define GAMEOS_CORE_PCSX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) \
		-f Makefile.libretro platform=unix ARCH=arm64
endef

define GAMEOS_CORE_PCSX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pcsx_rearmed_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/pcsx_rearmed_libretro.so
endef

$(eval $(generic-package))