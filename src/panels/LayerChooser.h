#pragma once

#include "backend/gdal/gdal_layer.hpp"   // render2d: LayerInfo

#include <deque>
#include <set>
#include <string>
#include <vector>

namespace e2d {

// §66: a dropped file with several layers (a GeoPackage) opens this window:
// its layers, to tick, as the tables of "Connect to database". Several such
// files dropped at once wait their turn.
class LayerChooser {
public:
    // A file and its layers (render2d's listLayers).
    void offer(const std::string& source, std::vector<r2d::LayerInfo> layers);

    // Every frame. Returns the sources to add: "file.gpkg::layer".
    std::vector<std::string> draw();

private:
    struct Offer {
        std::string                 source;
        std::vector<r2d::LayerInfo> layers;
    };
    std::deque<Offer>     m_offers;   // the first one is shown
    std::set<std::string> m_chosen;
};

} // namespace e2d
