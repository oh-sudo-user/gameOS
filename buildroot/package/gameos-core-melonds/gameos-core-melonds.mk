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
# HAVE_THREADS=1 builds the threaded software renderer, so GPU rendering
# runs on a worker core next to the JIT CPU thread (melonds_threaded_renderer).
#
# IMPORTANT: the libretro Makefile appends its own include paths and
# -D__LIBRETRO__ via 'CFLAGS +='. Passing CFLAGS on the make COMMAND
# LINE (as TARGET_CONFIGURE_OPTS does) overrides those appends and the
# build fails on missing headers. Pass our flags through the
# ENVIRONMENT instead (makefile '+=' then appends to them) and only the
# toolchain binaries on the command line. LD must be the C++ compiler
# driver ($(TARGET_CXX)): the Makefile links with $(LD) and its own
# default is LD = $(CXX); linking C++ objects with gcc leaves libstdc++
# symbols undefined (run 9: operator new / __cxxabiv1 vtables).
define GAMEOS_CORE_MELONDS_BUILD_CMDS
	$(TARGET_MAKE_ENV) \
	CFLAGS="$(TARGET_CFLAGS)" \
	CXXFLAGS="$(TARGET_CXXFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	$(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" LD="$(TARGET_CXX)" \
		platform=unix ARCH=arm64 HAVE_WIFI=0 HAVE_NEON=1 HAVE_THREADS=1
endef

define GAMEOS_CORE_MELONDS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/melonds_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/melonds_libretro.so
endef

$(eval $(generic-package))