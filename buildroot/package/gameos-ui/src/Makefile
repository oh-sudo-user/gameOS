CXX ?= g++
PKG_CONFIG ?= pkg-config
PKGS = sdl2 SDL2_ttf SDL2_image

CXXFLAGS += -O2 -std=c++17 -Wall -Wextra $(shell $(PKG_CONFIG) --cflags $(PKGS))
LIBS = $(shell $(PKG_CONFIG) --libs $(PKGS))

SRCS = main.cpp library.cpp launch.cpp i18n.cpp sound.cpp

all: gameos-ui gameos-splash

gameos-ui: $(SRCS) gameos.hpp i18n.hpp sound.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LIBS)

gameos-splash: splash.cpp
	$(CXX) $(CXXFLAGS) -o $@ splash.cpp $(LIBS)

clean:
	rm -f gameos-ui gameos-splash

.PHONY: all clean