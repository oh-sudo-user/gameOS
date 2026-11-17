################################################################################
#
# gameos-retroarch — RetroArch with gameOS dual-screen DRM patch
#
################################################################################

GAMEOS_RETROARCH_VERSION = 1.22.2
GAMEOS_RETROARCH_SITE = $(call github,libretro,RetroArch,v$(GAMEOS_RETROARCH_VERSION))
GAMEOS_RETROARCH_LICENSE = GPL-3.0+
GAMEOS_RETROARCH_LICENSE_FILES = COPYING
GAMEOS_RETROARCH_DEPENDENCIES = libdrm mesa3d alsa-lib eudev zlib freetype host-pkgconf

# KMS/DRM + EGL/GLES only: no X11, no Wayland, no SDL2, no desktop GL.
# The gameOS patch (0001) adds GAMEOS_DUALSCREEN=1 spanning mode for the
# RG DS Plus dual 1024x768 panels; patch 0002 offsets bottom-panel touch.
define GAMEOS_RETROARCH_CONFIGURE_CMDS
	cd $(@D) && $(TARGET_CONFIGURE_OPTS) $(TARGET_MAKE_ENV) \
	./configure \
		--prefix=/usr \
		--enable-kms \
		--enable-egl \
		--enable-opengles \
		--enable-opengles3 \
		--disable-opengl \
		--disable-opengl1 \
		--disable-vulkan \
		--disable-x11 \
		--disable-wayland \
		--enable-alsa \
		--disable-pulse \
		--disable-jack \
		--disable-oss \
		--enable-udev \
		--disable-libusb \
		--disable-ffmpeg \
		--disable-sdl2 \
		--disable-qt \
		--disable-cheevos \
		--disable-discord \
		--disable-cdrom \
		--disable-parport \
		--enable-neon \
		--enable-freetype
endef

# Do NOT pass TARGET_CONFIGURE_OPTS on the make command line here:
# RetroArch's Makefile includes config.mk (written by configure with
# all include paths/flags) and then appends via 'CFLAGS +='. A
# command-line CFLAGS would override all of that. configure already
# received our flags through the environment above.
define GAMEOS_RETROARCH_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

define GAMEOS_RETROARCH_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
	mkdir -p $(TARGET_DIR)/usr/share/retroarch/assets
	cp -dpfr $(@D)/assets/* $(TARGET_DIR)/usr/share/retroarch/assets/
	mkdir -p $(TARGET_DIR)/usr/share/retroarch/autoconfig
	cp -dpfr $(@D)/autoconfig/* $(TARGET_DIR)/usr/share/retroarch/autoconfig/
	mkdir -p $(TARGET_DIR)/usr/share/retroarch/info
	mkdir -p $(TARGET_DIR)/usr/lib/libretro
endef

$(eval $(generic-package))