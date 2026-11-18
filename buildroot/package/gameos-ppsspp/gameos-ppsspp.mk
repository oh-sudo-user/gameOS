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
#
# Run 14 compile fix (0001-sdlgl-guard-x11-kmsdrm.patch, verified against
# PPSSPP v1.20.4 SDL/SDLGLGraphicsContext.cpp):
#   ARM_DEVICE force-sets USING_EGL ON (CMakeLists line 88), so -DUSING_EGL=OFF
#   cannot take effect on aarch64. With USING_EGL the file's EGL_Open()/EGL_Close()
#   call XOpenDisplay/XCloseDisplay and read sysInfo.info.x11 unconditionally in
#   the !USING_FBDEV branch. Our SDL2 + mesa are KMS/DRM-only (no X11), so those
#   symbols are undeclared and the x11 union member does not exist -> compile
#   error. The patch guards every X11 reference with SDL_VIDEO_DRIVER_X11 (the
#   same convention the file already uses for DIRECTFB/WAYLAND/VIVANTE) and adds
#   a SDL_SYSWM_KMSDRM case using EGL_DEFAULT_DISPLAY with a null native window.
#   EGL_Init then cannot create a window surface, so Init() leaves useEGLSwap
#   false and renders/swaps through the SDL_GL context (SDL_GL_SwapWindow) that
#   SDL2's KMSDRM driver backs with GBM+EGL — the correct path on this device.
#   The patch lives in the package ROOT (not patches/): Buildroot only auto-globs
#   package/<pkg>/*.patch, never a patches/ subdirectory.
# CMAKE_POLICY_VERSION_MINIMUM=3.5: host cmake 4.4 removed < 3.5 compat.
# PPSSPP's own minimum is modern, but the bundled ext/* subprojects
# (libpng17, zstd, ffmpeg, snappy, ...) each declare their own old
# cmake_minimum_required and are add_subdirectory'd into the same
# configure; the policy floor guards them so generate does not abort.
GAMEOS_PPSSPP_CONF_OPTS = \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
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
# v1.20.4 keeps translations at assets/lang (there is NO top-level lang/),
# and PPSSPP's VFS registers /usr/share/ppsspp/assets as a root, so copying
# assets/ alone places lang where I18n expects it (assets/lang/<id>.ini).
define GAMEOS_PPSSPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/PPSSPPSDL $(TARGET_DIR)/usr/bin/PPSSPPSDL
	mkdir -p $(TARGET_DIR)/usr/share/ppsspp
	cp -dpfr $(@D)/assets $(TARGET_DIR)/usr/share/ppsspp/
endef

$(eval $(cmake-package))