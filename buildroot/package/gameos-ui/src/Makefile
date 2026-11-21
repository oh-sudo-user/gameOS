CXX ?= g++
PKG_CONFIG ?= pkg-config
PKGS = sdl2 SDL2_ttf SDL2_image libdrm libssh2 json-c zlib

CXXFLAGS += -O2 -std=c++17 -Wall -Wextra -pthread $(shell $(PKG_CONFIG) --cflags $(PKGS))
LIBS = $(shell $(PKG_CONFIG) --libs $(PKGS)) -pthread

SRCS = main.cpp library.cpp launch.cpp activity.cpp i18n.cpp sound.cpp displays.cpp ui.cpp touch.cpp perf.cpp drawcall.cpp motion.cpp widget.cpp netparse.cpp overrides.cpp fileops.cpp netsftp.cpp

all: gameos-ui gameos-splash

gameos-ui: $(SRCS) gameos.hpp i18n.hpp sound.hpp displays.hpp ui.hpp touch.hpp perf.hpp drawcall.hpp motion.hpp widget.hpp tokens.hpp netparse.hpp overrides.hpp fileops.hpp netsftp.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LIBS)

gameos-splash: splash.cpp i18n.cpp i18n.hpp displays.hpp displays.cpp
	$(CXX) $(CXXFLAGS) -o $@ splash.cpp i18n.cpp displays.cpp $(LIBS)

clean:
	rm -f gameos-ui gameos-splash

# Always relink: a binary left in the source tree by a host build must never be
# installed into the target rootfs (it would be x86-64 and fail the arch check).
.PHONY: all clean gameos-ui gameos-splash