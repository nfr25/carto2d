#pragma once

#include "panels/Workspace.h"
#include "view/Navigator.h"
#include "view/Tool.h"

#include "core/point.hpp"

#include <string>
#include <vector>

namespace e2d {

// The shape being drawn (notions §28, §29), over the map and the selection,
// converted from the ground to the screen at every frame like the selection
// (§23): it follows the wheel and the middle button.
//   * a rectangle: outline and a translucent fill (always convex)
//   * a polygon: the vertices, the elastic line to the mouse, and a paler one
//     back to the first vertex; no fill (ImGui cannot fill a bow-tie)
// `waiting`: a zone already finished, its question not answered yet (§30):
// still drawn, until the selection arrives.
// §72: the measuring tools in violet, and `label` (the figures so far) next
// to the mouse; the first vertex drawn larger when a click there would close.
void drawShape(const Tool& tool, const std::vector<r2d::Point>& waiting, const Navigator& nav,
               const MapArea& area, float scale, const std::string& label = {});

// §77: the printed page's frame, on the ground, while the Print dialog is
// open: an outline and its label ("A4 landscape · 1:1 000") at its top.
void drawPageOutline(const r2d::Envelope& frame, const std::string& label, const Navigator& nav,
                     const MapArea& area, float scale);

// §72: a finished measure, kept on the map with its figures until Esc.
void drawMeasured(const std::vector<r2d::Point>& points, bool closed, const std::string& label,
                  const Navigator& nav, const MapArea& area, float scale);

} // namespace e2d
