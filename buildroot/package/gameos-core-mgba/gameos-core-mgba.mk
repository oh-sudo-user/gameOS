################################################################################
#
# gameos-core-mgba — mGBA libretro core (GBA / GBC)
#
################################################################################

GAMEOS_CORE_MGBA_VERSION = 0.10.5
GAMEOS_CORE_MGBA_SITE = $(call github,mgba-emu,mgba,$(GAMEOS_CORE_MGBA_VERSION))
GAMEOS_CORE_MGBA_LICENSE = MPL-2.0
GAMEOS_CORE_MGBA_LICENSE_FILES = LICENSE

define GAMEOS_CORE_MGBA_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) \
		-C $(@D)/src/platform/libretro
endef

define GAMEOS_CORE_MGBA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/src/platform/libretro/mgba_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/mgba_libretro.so
endef

$(eval $(generic-package))