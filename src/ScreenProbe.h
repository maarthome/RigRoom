#pragma once

// Size in pixels of the X11 screen RigRoom will open on, read before Qt
// starts (Qt's interface scale has to be chosen before QApplication exists).
// Returns false when no X display can be opened.
bool probeX11ScreenSize(int& width, int& height);
