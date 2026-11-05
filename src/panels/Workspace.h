#pragma once

#include <string>

namespace e2d {

// The rectangle left to the map in the middle of the dockspace (notions §19),
// in ImGui units (= window units, = screen pixels on Windows).
struct MapArea {
    float x = 0.0f, y = 0.0f;
    float width = 0.0f, height = 0.0f;
    bool  valid() const { return width >= 1.0f && height >= 1.0f; }
};

// The status bar, a thin band at the bottom of the window, outside the
// dockspace. Call it BEFORE dockspace(): it takes its height from the room
// the dockspace will have.
void drawStatusBar(const std::string& text);

// The dockspace over the whole window, its central zone transparent: the
// panels dock around, the map shows through the middle. At the very first
// run (no imgui.ini yet), a default layout is built: Layers, then History and
// Build on the left; Report and Events at the bottom.
// Returns the central zone: where the map goes.
MapArea dockspace();

// §61: the panels back in their first places, at the next frame (View > Reset layout).
void resetLayout();

} // namespace e2d
