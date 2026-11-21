################################################################################
#
# gameos-core-fceumm — FCEUmm libretro core (NES / Famicom)
#
################################################################################

GAMEOS_CORE_FCEUMM_VERSION = 77468e568d9fe9c48d684ca911f1a3fe7259fdf7
GAMEOS_CORE_FCEUMM_SITE = $(call github,libretro,libretro-fceumm,$(GAMEOS_CORE_FCEUMM_VERSION))
GAMEOS_CORE_FCEUMM_LICENSE = GPL-2.0+
GAMEOS_CORE_FCEUMM_LICENSE_FILES = Copying

# Same libretro Makefile caveat as the other gameOS cores: the core appends
# its own includes/defines to CFLAGS, so flags go through the environment.
define GAMEOS_CORE_FCEUMM_BUILD_CMDS
	$(TARGET_MAKE_ENV) \
	CFLAGS="$(TARGET_CFLAGS)" \
	CXXFLAGS="$(TARGET_CXXFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	$(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		platform=unix
endef

define GAMEOS_CORE_FCEUMM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/fceumm_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/fceumm_libretro.so
endef

$(eval $(generic-package))
