################################################################################
#
# gameos-ppsspp — standalone PPSSPP (PSP), SDL2 + GLES2/EGL on KMS/DRM
#
################################################################################

GAMEOS_PPSSPP_VERSION = v1.20.4
GAMEOS_PPSSPP_SITE = https://github.com/hrydgard/ppsspp.git
GAMEOS_PPSSPP_SITE_METHOD = git
GAMEOS_PPSSPP_GIT_SUBMODULES = YES
GAMEOS_PPSSPP_LICENSE = GPL-2.0+
GAMEOS_PPSSPP_LICENSE_FILES = LICENSE.TXT
GAMEOS_PPSSPP_DEPENDENCIES = sdl2 mesa3d zlib libpng freetype host-pkgconf
# Executable, not a library: skip the cmake staging install; the custom
# INSTALL_TARGET_CMDS below place the binary + assets into the rootfs.
GAMEOS_PPSSPP_INSTALL_STAGING = NO

# USE_SYSTEM_FREETYPE=ON: PPSSPP's bundled ext/freetype submodule trips
# freetype's "In-source builds are not permitted" CMake check under
# Buildroot (run 10). freetype is only linked by the AtlasTool font
# generator (ATLAS_TOOL=OFF here), so skipping the bundled copy via the
# system-freetype option is safe and avoids configuring it at all.
GAMEOS_PPSSPP_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DUSING_GLES2=ON \
	-DUSING_EGL=ON \
	-DUSING_QT_UI=OFF \
	-DHEADLESS=OFF \
	-DUNITTEST=OFF \
	-DATLAS_TOOL=OFF \
	-DLIBRETRO=OFF \
	-DUSE_VULKAN=OFF \
	-DUSE_FFMPEG=ON \
	-DUSE_SYSTEM_FFMPEG=OFF \
	-DUSE_SYSTEM_FREETYPE=ON \
	-DUSE_SYSTEM_LIBSDL2=ON \
	-DUSE_DISCORD=OFF \
	-DUSE_MINIUPNPC=OFF \
	-DUSE_WAYLAND_WSI=OFF

# PPSSPP locates its assets relative to the working directory; the
# ppsspp-run wrapper (rootfs overlay) cd's into /usr/share/ppsspp.
# cmake-package defaults to IN-SOURCE build (SUPPORTS_IN_SOURCE_BUILD ?=
# YES -> BUILDDIR = $(@D)); PPSSPPSDL has no custom output dir, so the
# binary lands at the source root $(@D)/PPSSPPSDL, NOT buildroot-build.
define GAMEOS_PPSSPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/PPSSPPSDL $(TARGET_DIR)/usr/bin/PPSSPPSDL
	mkdir -p $(TARGET_DIR)/usr/share/ppsspp
	cp -dpfr $(@D)/assets $(TARGET_DIR)/usr/share/ppsspp/
	cp -dpfr $(@D)/lang $(TARGET_DIR)/usr/share/ppsspp/
endef

$(eval $(cmake-package))