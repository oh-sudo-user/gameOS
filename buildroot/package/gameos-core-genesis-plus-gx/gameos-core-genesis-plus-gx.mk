################################################################################
#
# gameos-core-genesis-plus-gx — Genesis Plus GX libretro core (Mega Drive)
#
################################################################################

GAMEOS_CORE_GENESIS_PLUS_GX_VERSION = 4fd32efe1fec3537d11f7aaa24cb961c6ff57fdd
GAMEOS_CORE_GENESIS_PLUS_GX_SITE = $(call github,libretro,Genesis-Plus-GX,$(GAMEOS_CORE_GENESIS_PLUS_GX_VERSION))
GAMEOS_CORE_GENESIS_PLUS_GX_LICENSE = Genesis-Plus-GX
GAMEOS_CORE_GENESIS_PLUS_GX_LICENSE_FILES = LICENSE.txt

# HAVE_CDROM=0 is deliberate: the Unix branch enables it when uname says
# Linux, which pulls in SCSI CD-ROM ioctls that are useless on this handheld
# (ROMs are launched from the SD card, not an optical drive). Command-line
# assignment wins over the Makefile's uname check.
define GAMEOS_CORE_GENESIS_PLUS_GX_BUILD_CMDS
	$(TARGET_MAKE_ENV) \
	CFLAGS="$(TARGET_CFLAGS)" \
	CXXFLAGS="$(TARGET_CXXFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	$(MAKE) -C $(@D) -f Makefile.libretro \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		platform=unix HAVE_CDROM=0
endef

define GAMEOS_CORE_GENESIS_PLUS_GX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/genesis_plus_gx_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/genesis_plus_gx_libretro.so
endef

$(eval $(generic-package))
