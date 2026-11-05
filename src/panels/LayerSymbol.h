#pragma once

#include "command/Document.h"
#include "style/style.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace e2d {

// The small picture in front of a layer's name (notions §68): what the layer
// looks like, in its style's own colours. No ImGui here: what to draw is
// decided here and tested; LayersPanel only draws it.
struct LayerSymbol {
    enum class Kind { None, Fill, Line, Mark, Text, Raster };
    Kind                      kind = Kind::None;
    std::optional<r2d::Color> fill;            // Fill, Mark; Text: the letters
    std::optional<r2d::Color> stroke;          // Fill: the outline; Line; Mark
    double                    strokeWidth = 1.0;   // pixels, as in the style (ratio 1)
    std::optional<r2d::GraphicFill> hatch;     // Fill: hatching, drawn over the fill
    r2d::MarkShape            shape = r2d::MarkShape::Circle;
    double                    size = 6.0;      // Mark: pixels, as in the style (a size in metres: 6)
    double                    opacity = 1.0;   // Raster
    std::string               image;           // Mark: a PNG or SVG (§78); shape, fill, stroke: its fallback
};

// §78: a symbol with an image, drawn by render2d as the map draws it, in a
// square of `side` pixels (the mark's size, kept between 40 % and 90 % of
// the square), on a transparent background. Its bytes B, G, R, A, rows of
// side x 4, NOT premultiplied (as ImGui blends). The fallback mark when the
// image cannot be read, as on the map.
std::vector<unsigned char> symbolPixels(const LayerSymbol& symbol, int side, float ratio);

// The style as the map draws it: the dialog's text if any, else the file,
// else render2d's default style for this geometry and colour (§38).
r2d::Style layerStyle(const LayerEntry& layer);

// The symbol of a style at a scale: the first rule that applies at this
// scale (the first rule of all when none does: the name is grey then), and
// in it the symbolizer that matches the geometry.
LayerSymbol symbolOf(const r2d::Style& style, const std::string& geometry, double scaleDenominator);

// The styles read once per layer, and read again only when what they come
// from changes (file, dialog text, colour, geometry). One per application.
class LayerSymbols {
public:
    LayerSymbol get(const LayerEntry& layer, double scaleDenominator);

private:
    struct Cached {
        std::string key;
        r2d::Style  style;
    };
    std::map<std::string, Cached> m_cache;   // per layer id
};

} // namespace e2d
