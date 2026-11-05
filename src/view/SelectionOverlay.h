#pragma once

#include "command/Document.h"
#include "panels/Workspace.h"
#include "view/Navigator.h"

#include "core/attributes.hpp"
#include "core/geometry.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace e2d {

// A feature met in a query answer: its geometry and attributes, kept to draw
// the selection and show it in the panel (notions §23, §24).
struct KnownFeature {
    r2d::Geometry   geometry;
    r2d::Attributes attributes;
};

// Key: (layer id, FID), as in the Document's selection.
using FeatureCache = std::map<std::pair<std::string, std::int64_t>, KnownFeature>;

// The second drawing (§23): the selected features of the visible layers, in
// outline, over the map, their vertices converted from the ground to the
// screen with the WANTED view at every frame. `scale`: the screen's content
// scale (thicker lines at 150 %).
// `focus`: the feature designated in the Table or the Selection panel (§60),
// drawn last, stronger, in orange.
void drawSelection(const Document& doc, const FeatureCache& known, const Navigator& nav, const MapArea& area,
                   float scale, const std::pair<std::string, std::int64_t>& focus = {});

} // namespace e2d
