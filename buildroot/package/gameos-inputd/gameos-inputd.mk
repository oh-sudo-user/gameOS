################################################################################
#
# gameos-inputd
#
################################################################################

GAMEOS_INPUTD_VERSION = 1.0
GAMEOS_INPUTD_SITE = $(BR2_EXTERNAL_GAMEOS_PATH)/package/gameos-inputd/src
GAMEOS_INPUTD_SITE_METHOD = local
GAMEOS_INPUTD_LICENSE = GPL-3.0+

define GAMEOS_INPUTD_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -Wall -Wextra \
		-o $(@D)/gameos-inputd $(@D)/inputd.c
endef

define GAMEOS_INPUTD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gameos-inputd $(TARGET_DIR)/usr/bin/gameos-inputd
endef

$(eval $(generic-package))