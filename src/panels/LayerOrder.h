#pragma once

#include "backend/gdal/gdal_layer.hpp"   // render2d: LayerInfo

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace e2d {

// §66: the order in which the layers ticked in a list are added. Each
// layer.add goes on top: the first added ends at the bottom. Surfaces first,
// then lines, then points, so that nothing hides the small things; in a
// group, the order of the list. Mixed or unknown types go with the surfaces.
inline std::vector<std::string> orderForAdding(const std::vector<r2d::LayerInfo>& layers,
                                               const std::set<std::string>& chosen)
{
    auto rank = [](const r2d::LayerInfo& l) {
        if (!l.type || *l.type == r2d::GeomType::Polygon)
            return 0;
        return *l.type == r2d::GeomType::Line ? 1 : 2;
    };
    std::vector<const r2d::LayerInfo*> picked;
    for (const r2d::LayerInfo& l : layers)
        if (chosen.count(l.name))
            picked.push_back(&l);
    std::stable_sort(picked.begin(), picked.end(),
                     [&](const r2d::LayerInfo* a, const r2d::LayerInfo* b) { return rank(*a) < rank(*b); });
    std::vector<std::string> names;
    for (const r2d::LayerInfo* l : picked)
        names.push_back(l->name);
    return names;
}

} // namespace e2d
