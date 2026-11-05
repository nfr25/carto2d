#pragma once

#include "backend/gdal/gdal_layer.hpp"   // render2d: LayerInfo

#include <set>
#include <string>
#include <vector>

namespace e2d {

// The list of a source's layers, to tick (§65, §66): name, type, features,
// CRS; "All", "None", "Add N layer(s)"; a double-click adds one layer at once.
// Shared by "Connect to database" and the chooser of a dropped GeoPackage.
struct LayerListResult {
    std::vector<std::string> add;            // the names to add, in the order of adding (orderForAdding)
    bool                     button = false; // by "Add N layer(s)" (else by a double-click)
};
LayerListResult drawLayerList(const std::vector<r2d::LayerInfo>& layers, std::set<std::string>& chosen);

} // namespace e2d
