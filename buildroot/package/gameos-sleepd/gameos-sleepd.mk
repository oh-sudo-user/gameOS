################################################################################
#
# gameos-sleepd — lid-close suspend daemon (local source)
#
################################################################################

GAMEOS_SLEEPD_VERSION = 1.0
GAMEOS_SLEEPD_SITE = $(BR2_EXTERNAL_GAMEOS_PATH)/package/gameos-sleepd/src
GAMEOS_SLEEPD_SITE_METHOD = local
GAMEOS_SLEEPD_LICENSE = GPL-3.0+

define GAMEOS_SLEEPD_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/gameos-sleepd $(@D)/main.c
endef

define GAMEOS_SLEEPD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gameos-sleepd $(TARGET_DIR)/usr/bin/gameos-sleepd
endef

$(eval $(generic-package))