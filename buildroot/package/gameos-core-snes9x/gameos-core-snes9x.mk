################################################################################
#
# gameos-core-snes9x — Snes9x libretro core (SNES)
#
################################################################################

GAMEOS_CORE_SNES9X_VERSION = 204ea1c0b851f960cbf223075eb1c25641cb483c
GAMEOS_CORE_SNES9X_SITE = $(call github,libretro,snes9x,$(GAMEOS_CORE_SNES9X_VERSION))
GAMEOS_CORE_SNES9X_LICENSE = Snes9x
GAMEOS_CORE_SNES9X_LICENSE_FILES = LICENSE

# The libretro frontend lives one level down; CORE_DIR is relative to it.
# Do not enable LTO: upstream explicitly leaves it opt-in because it has
# miscompiled this core before.
define GAMEOS_CORE_SNES9X_BUILD_CMDS
	$(TARGET_MAKE_ENV) \
	CFLAGS="$(TARGET_CFLAGS)" \
	CXXFLAGS="$(TARGET_CXXFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	$(MAKE) -C $(@D)/libretro \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		platform=unix
endef

define GAMEOS_CORE_SNES9X_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libretro/snes9x_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/snes9x_libretro.so
endef

$(eval $(generic-package))
