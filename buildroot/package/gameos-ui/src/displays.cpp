// gameOS front-end — dual-panel display mapping.
//
// SDL's kmsdrm backend names displays "0", "1", ... after their index, never
// after the DRM connector, so looking for "DSI-2" in SDL_GetDisplayName()
// always fails and silently left the whole UI on display 0 (the bottom panel;
// DSI-2 is the physical top on both RG DS models). Walk the kernel connector
// list in the order kmsdrm does: connected connectors with modes, one SDL
// display each.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "displays.hpp"

#include <SDL2/SDL.h>

#include <cstdlib>

#include <fcntl.h>
#include <unistd.h>

#ifdef __linux__
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

namespace gameos {

namespace {

#ifdef __linux__
// Fills dsi1/dsi2 with the SDL display index of the DSI-1/DSI-2 connector.
// Returns false when the driver is not kmsdrm or DRM cannot be inspected;
// callers then use SDL's enumeration order.
bool find_dsi_panels(int displays, int* dsi1, int* dsi2)
{
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || SDL_strcmp(driver, "kmsdrm") != 0) return false;

    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;

    drmModeRes* res = drmModeGetResources(fd);
    if (!res) {
        close(fd);
        return false;
    }

    int index = 0;
    for (int i = 0; i < res->count_connectors && index < displays; i++) {
        drmModeConnector* c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c) continue;
        bool usable = c->connection == DRM_MODE_CONNECTED && c->count_modes > 0;
        bool is_dsi1 = c->connector_type == DRM_MODE_CONNECTOR_DSI &&
                       c->connector_type_id == 1;
        bool is_dsi2 = c->connector_type == DRM_MODE_CONNECTOR_DSI &&
                       c->connector_type_id == 2;
        drmModeFreeConnector(c);
        if (!usable) continue;
        if (is_dsi1) *dsi1 = index;
        else if (is_dsi2) *dsi2 = index;
        index++;
    }

    drmModeFreeResources(res);
    close(fd);
    return true;
}
#endif

} // namespace

Panels panel_displays()
{
    Panels p;
    const int n = SDL_GetNumVideoDisplays();
    if (n <= 0) return p;

    int dsi1 = -1, dsi2 = -1;
#ifdef __linux__
    find_dsi_panels(n, &dsi1, &dsi2);
#endif

    const char* env = getenv("GAMEOS_TOP_DISPLAY");
    if (env && *env) {
        int v = atoi(env);
        if (v >= 0 && v < n) p.top = v;
    } else if (dsi2 >= 0) {
        p.top = dsi2;          // DSI-2 is the lid panel on both models
    } else if (dsi1 >= 0) {
        p.top = dsi1;          // only the bottom panel probed: still usable
    } else if (n > 1) {
        p.top = 1;             // kmsdrm enumerates DSI-1 first
    }

    if (dsi1 >= 0 && dsi1 != p.top) {
        p.bottom = dsi1;
    } else if (n > 1) {
        p.bottom = (p.top == 0) ? 1 : 0;
    }
    return p;
}

} // namespace gameos
