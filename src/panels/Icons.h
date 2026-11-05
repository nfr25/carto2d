#pragma once

#include <imgui.h>

namespace e2d {

// The toolbar's icons (notions §61), drawn with ImGui's draw list: lines,
// arcs and polygons in a unit square, scaled to the button. No font, no
// image: crisp at any screen scale.
enum class Icon { Undo, Redo, Pointer, Rectangle, Polygon, ZoomWindow, FullExtent, ZoomSelection,
                  MeasureLine, MeasureArea };   // §72

void drawIcon(ImDrawList* draw, Icon icon, ImVec2 corner, float size, ImU32 colour);

// A square button with an icon. `active`: the current tool, highlighted.
// Disabled: drawn faint, never clicked. The tooltip says its name and key.
bool iconButton(const char* id, Icon icon, bool active, bool enabled, const char* tooltip);

} // namespace e2d
