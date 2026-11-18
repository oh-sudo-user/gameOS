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
GAMEOS_PPSSPP_DEPENDENCIES = sdl2 sdl2_ttf mesa3d zlib libpng freetype host-pkgconf
# Executable, not a library: skip the cmake staging install; the custom
# INSTALL_TARGET_CMDS below place the binary + assets into the rootfs.
GAMEOS_PPSSPP_INSTALL_STAGING = NO

# USE_SYSTEM_FREETYPE=ON: PPSSPP's bundled ext/freetype submodule trips
# freetype's "In-source builds are not permitted" CMake check under
# Buildroot (run 10). freetype is only linked by the AtlasTool font
# generator (ATLAS_TOOL=OFF here), so skipping the bundled copy via the
# system-freetype option is safe and avoids configuring it at all.
#
# Run 13 configure fixes (verified against PPSSPP v1.20.4 CMakeLists.txt):
#  * X11: VULKAN is force-set ON for UNIX (line 109) and the X11-Vulkan
#    branch does include_directories(${X11_Xlib_INCLUDE_PATH}) (line 239),
#    which leaks the NOTFOUND path into ext/zstd-build and fails generate.
#    We render on KMS/DRM (no X11): disable the X11 find entirely and turn
#    off USING_X11_VULKAN (the real cache option; USE_VULKAN is NOT one).
#  * SDL2_ttf: PPSSPP ships NO FindSDL2_ttf.cmake, so config-mode find is
#    unreliable and yielded "SDL2_ttf::SDL2_ttf target not found". Disable
#    find_package(SDL2_ttf) so it falls back to pkg_check_modules(IMPORTED_
#    TARGET SDL2_ttf) -> PkgConfig::SDL2_ttf_PKGCONFIG (always valid). The
#    sdl2_ttf dependency stages SDL2_ttf.pc (2.24.0 >= 2.0.18). Fontconfig
#    find is disabled too (same imported-target pitfall); SDL2_ttf renders
#    fonts directly without it.
#  * PNG: ARM64 requires libpng >= 1.7 (line 1290) but Buildroot has 1.6.x;
#    USE_SYSTEM_LIBPNG=OFF uses the bundled ext/libpng17 instead.
GAMEOS_PPSSPP_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DUSING_GLES2=ON \
	-DUSING_EGL=ON \
	-DUSING_QT_UI=OFF \
	-DHEADLESS=OFF \
	-DUNITTEST=OFF \
	-DATLAS_TOOL=OFF \
	-DLIBRETRO=OFF \
	-DUSING_X11_VULKAN=OFF \
	-DCMAKE_DISABLE_FIND_PACKAGE_X11=ON \
	-DCMAKE_DISABLE_FIND_PACKAGE_SDL2_ttf=ON \
	-DCMAKE_DISABLE_FIND_PACKAGE_Fontconfig=ON \
	-DUSE_SYSTEM_LIBPNG=OFF \
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