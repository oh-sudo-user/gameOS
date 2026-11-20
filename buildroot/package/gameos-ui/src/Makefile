CXX ?= g++
PKG_CONFIG ?= pkg-config
PKGS = sdl2 SDL2_ttf SDL2_image libdrm

CXXFLAGS += -O2 -std=c++17 -Wall -Wextra $(shell $(PKG_CONFIG) --cflags $(PKGS))
LIBS = $(shell $(PKG_CONFIG) --libs $(PKGS))

SRCS = main.cpp library.cpp launch.cpp activity.cpp i18n.cpp sound.cpp displays.cpp ui.cpp touch.cpp

all: gameos-ui gameos-splash

gameos-ui: $(SRCS) gameos.hpp i18n.hpp sound.hpp displays.hpp ui.hpp touch.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LIBS)

gameos-splash: splash.cpp i18n.cpp i18n.hpp displays.hpp displays.cpp
	$(CXX) $(CXXFLAGS) -o $@ splash.cpp i18n.cpp displays.cpp $(LIBS)

clean:
	rm -f gameos-ui gameos-splash

.PHONY: all clean