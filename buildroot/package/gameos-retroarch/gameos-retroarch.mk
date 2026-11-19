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
#
# CROSS_COMPILE must be non-empty. RetroArch's qb/config.libs.sh injects the
# HOST lib dir (-L/usr/lib64, /opt/local/lib) whenever CROSS_COMPILE is unset,
# i.e. when it believes this is a native build. Buildroot's paranoid cross-gcc
# wrapper aborts on that path ("unsafe header/library path used in
# cross-compilation"), which fails the EGL/egl.h probe and kills configure.
# TARGET_CONFIGURE_OPTS supplies CC/CXX/AR but NOT CROSS_COMPILE, and the
# configure script never uses CROSS_COMPILE to prefix tools, so setting it to
# the real target prefix only suppresses the host-dir injection.
#
# NEON is disabled: RetroArch's HAVE_NEON path is armv7-only. Its configure
# probes the predefined macro __ARM_NEON__ (which aarch64 GCC does NOT define
# — it defines __ARM_NEON), and when forced on it adds '-mfpu=neon -marm',
# flags aarch64 GCC rejects. AArch64 Advanced SIMD is baseline ISA and
# RetroArch uses it through __ARM_NEON/__aarch64__ guarded intrinsics that do
# not depend on HAVE_NEON, so disabling the armv7 assembly path loses nothing.
#
# Force PKG_CONF_PATH to Buildroot's pkg-config wrapper. RetroArch's
# qb.comp.sh honors only PKG_CONF_PATH (not PKG_CONFIG); without it,
# auto-detection looks for a cross-prefixed pkgconf Buildroot does not
# provide, leaving PKG_CONF_PATH=none. check_pkgconf then fails and check_val
# falls back to check_lib, which probes the HOST filesystem: /usr/include/alsa
# exists in WSL (libsdl2-dev pulled libasound2-dev), so it records the host
# path -I/usr/include/alsa in config.mk and the paranoid cross-gcc rejects it
# at build time. The wrapper resolves sysroot .pc files and emits correct
# cflags, so check_pkgconf succeeds and the host-dir fallback never runs.
#
# RetroAchievements is ON: the first-boot wizard logs the account in and writes
# /userdata/system/retroarch/cheevos.cfg, which launch.cpp appends last. cheevos
# only needs networking + SSL (both already enabled, builtin mbedtls) and
# deps/rcheevos, which ships in the tarball — no extra Buildroot dependency.
define GAMEOS_RETROARCH_CONFIGURE_CMDS
	cd $(@D) && $(TARGET_CONFIGURE_OPTS) $(TARGET_MAKE_ENV) \
	CROSS_COMPILE="$(TARGET_CROSS)" \
	PKG_CONF_PATH="$(HOST_DIR)/bin/pkg-config" \
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
		--enable-cheevos \
		--disable-discord \
		--disable-cdrom \
		--disable-parport \
		--disable-neon \
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

# The RetroArch source tarball ships NEITHER menu assets (media/assets) nor
# joypad autoconfig — they live in separate libretro repos and are absent
# here, and make install's asset block is gated on HAVE_ASSETS (unset).
# Copying $(@D)/assets/* or $(@D)/autoconfig/* therefore fails the install.
# autoconfig is supplied by the rootfs overlay (autoconfig/udev/gameOS
# Gamepad.cfg); the in-emulator menu uses rgui (built-in, asset-free) until
# an ozone asset pack is added as a visual follow-up.
define GAMEOS_RETROARCH_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
	mkdir -p $(TARGET_DIR)/usr/share/retroarch/assets
	mkdir -p $(TARGET_DIR)/usr/share/retroarch/info
	mkdir -p $(TARGET_DIR)/usr/lib/libretro
endef

$(eval $(generic-package))