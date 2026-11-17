################################################################################
#
# gameos-core-melonds — melonDS libretro core (Nintendo DS)
#
################################################################################

GAMEOS_CORE_MELONDS_VERSION = 4081e8358e66f1ea11879560be6b813dfeaab75a
GAMEOS_CORE_MELONDS_SITE = $(call github,libretro,melonDS,$(GAMEOS_CORE_MELONDS_VERSION))
GAMEOS_CORE_MELONDS_LICENSE = GPL-3.0+
GAMEOS_CORE_MELONDS_LICENSE_FILES = LICENSE

# ARCH=arm64 enables the aarch64 JIT recompiler.
# HAVE_WIFI=0 drops the libpcap dependency (local play only).
define GAMEOS_CORE_MELONDS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) \
		platform=unix ARCH=arm64 HAVE_WIFI=0 HAVE_NEON=1
endef

define GAMEOS_CORE_MELONDS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/melonds_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/melonds_libretro.so
endef

$(eval $(generic-package))