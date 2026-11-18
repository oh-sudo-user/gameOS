################################################################################
#
# gameos-ui — the gameOS front-end (local source)
#
################################################################################

GAMEOS_UI_VERSION = 1.0
GAMEOS_UI_SITE = $(BR2_EXTERNAL_GAMEOS_PATH)/package/gameos-ui/src
GAMEOS_UI_SITE_METHOD = local
GAMEOS_UI_LICENSE = GPL-3.0+
GAMEOS_UI_DEPENDENCIES = sdl2 sdl2_ttf sdl2_image host-pkgconf

define GAMEOS_UI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(TARGET_CONFIGURE_OPTS) \
	PKG_CONFIG="$(HOST_DIR)/bin/pkg-config" \
	PKG_CONFIG_SYSROOT_DIR="$(STAGING_DIR)" \
	PKG_CONFIG_LIBDIR="$(STAGING_DIR)/usr/lib/pkgconfig:$(STAGING_DIR)/usr/share/pkgconfig" \
	$(MAKE) -C $(@D) CXX="$(TARGET_CXX)"
endef

define GAMEOS_UI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gameos-ui $(TARGET_DIR)/usr/bin/gameos-ui
	$(INSTALL) -D -m 0755 $(@D)/gameos-splash $(TARGET_DIR)/usr/bin/gameos-splash
endef

$(eval $(generic-package))