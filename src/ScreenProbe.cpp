#include "ScreenProbe.h"

// Kept apart from any Qt code: Xlib defines macros (Bool, None, Status...)
// that clash with Qt's headers.
#include <X11/Xlib.h>

bool probeX11ScreenSize(int& width, int& height) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) return false;
    const int screen = DefaultScreen(display);
    width = DisplayWidth(display, screen);
    height = DisplayHeight(display, screen);
    XCloseDisplay(display);
    return width > 0 && height > 0;
}
