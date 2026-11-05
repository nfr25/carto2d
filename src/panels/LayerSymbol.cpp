#include "panels/LayerSymbol.h"

#include "backend/cairo/cairo_image.hpp"   // §78: an image symbol drawn in memory
#include "render/symbolize.hpp"   // defaultStyle, drawSymbolizer
#include "style/ysld_reader.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace e2d {

r2d::Style layerStyle(const LayerEntry& layer)
{
    std::optional<r2d::Style> s;
    if (!layer.styleText.empty())
        s = r2d::parseYsld(layer.styleText).style;
    else if (!layer.style.empty())
        s = r2d::readYsldFile(layer.style).style;
    if (s)
        return *s;
    if (layer.geometry == "raster")
        return {};   // no rule: drawn as it is, opacity 1 (§62)
    const r2d::GeomType type = layer.geometry == "polygon" ? r2d::GeomType::Polygon
                             : layer.geometry == "line"    ? r2d::GeomType::Line
                                                           : r2d::GeomType::Point;
    return r2d::defaultStyle(type, layer.colour);
}

LayerSymbol symbolOf(const r2d::Style& style, const std::string& geometry, double scaleDenominator)
{
    LayerSymbol out;

    // The rule: the first one active at this scale, else the very first.
    const r2d::Rule* rule = nullptr;
    for (const r2d::FeatureTypeStyle& fts : style.featureStyles) {
        for (const r2d::Rule& r : fts.rules) {
            if (!r.invalid && r.scale.contains(scaleDenominator)) {
                rule = &r;
                break;
            }
        }
        if (rule)
            break;
    }
    if (!rule) {
        for (const r2d::FeatureTypeStyle& fts : style.featureStyles) {
            if (!fts.rules.empty()) {
                rule = &fts.rules.front();
                break;
            }
        }
    }

    if (geometry == "raster") {
        out.kind = LayerSymbol::Kind::Raster;
        if (rule)
            for (const r2d::Symbolizer& s : rule->symbolizers)
                if (const auto* r = std::get_if<r2d::RasterSymbolizer>(&s)) {
                    out.opacity = r->opacity;
                    break;
                }
        return out;
    }
    if (!rule)
        return out;

    // The first symbolizer of each kind in the rule.
    const r2d::PolygonSymbolizer* poly = nullptr;
    const r2d::LineSymbolizer*    line = nullptr;
    const r2d::PointSymbolizer*   point = nullptr;
    const r2d::TextSymbolizer*    text = nullptr;
    for (const r2d::Symbolizer& s : rule->symbolizers) {
        std::visit([&](const auto& sym) {
            using T = std::decay_t<decltype(sym)>;
            if constexpr (std::is_same_v<T, r2d::PolygonSymbolizer>) { if (!poly)  poly = &sym; }
            if constexpr (std::is_same_v<T, r2d::LineSymbolizer>)    { if (!line)  line = &sym; }
            if constexpr (std::is_same_v<T, r2d::PointSymbolizer>)   { if (!point) point = &sym; }
            if constexpr (std::is_same_v<T, r2d::TextSymbolizer>)    { if (!text)  text = &sym; }
        }, s);
    }

    auto asFill = [&]() {
        out.kind = LayerSymbol::Kind::Fill;
        if (poly->fill)
            out.fill = poly->fill->color;
        out.hatch = poly->graphicFill;
        if (poly->stroke) {
            out.stroke      = poly->stroke->color;
            out.strokeWidth = poly->stroke->width;
        } else if (line) {   // a polygon outlined by a line symbolizer (SLD allows it)
            out.stroke      = line->stroke.color;
            out.strokeWidth = line->stroke.width;
        }
    };
    auto asLine = [&]() {
        out.kind        = LayerSymbol::Kind::Line;
        out.stroke      = line->stroke.color;
        out.strokeWidth = line->stroke.width;
    };
    auto asMark = [&]() {
        out.kind  = LayerSymbol::Kind::Mark;
        out.shape = point->shape;
        out.size  = point->metres || point->sizeExpr || point->size <= 0.0 ? 6.0 : point->size;
        if (point->fill)
            out.fill = point->fill->color;
        if (point->stroke) {
            out.stroke      = point->stroke->color;
            out.strokeWidth = point->stroke->width;
        }
        out.image = point->image;   // §78: drawn by render2d in the panel
    };

    // The symbolizer of the layer's own geometry first; then any drawing one.
    if (geometry == "polygon" && poly)       asFill();
    else if (geometry == "line" && line)     asLine();
    else if (geometry == "point" && point)   asMark();
    else if (poly)                           asFill();
    else if (line)                           asLine();
    else if (point)                          asMark();
    else if (text) {
        out.kind = LayerSymbol::Kind::Text;
        out.fill = text->fill;
    }
    return out;
}

std::vector<unsigned char> symbolPixels(const LayerSymbol& symbol, int side, float ratio)
{
    std::vector<unsigned char> out;
    if (side <= 0)
        return out;
    std::string error;
    auto image = r2d::CairoImage::create({side, side}, error);   // transparent at creation
    if (!image)
        return out;

    // The point symbolizer again, without what the panel does not show
    // (rotation, a size from an attribute): the image, and its fallback mark.
    r2d::PointSymbolizer p;
    p.image = symbol.image;
    p.shape = symbol.shape;
    if (symbol.fill)
        p.fill = r2d::FillStyle{*symbol.fill};
    else
        p.fill.reset();
    if (symbol.stroke) {
        r2d::StrokeStyle st;
        st.color = *symbol.stroke;
        st.width = symbol.strokeWidth * ratio;
        p.stroke = st;
    } else {
        p.stroke.reset();
    }
    p.size = std::clamp(symbol.size * ratio, side * 0.4, side * 0.9);

    r2d::Geometry g;
    g.type   = r2d::GeomType::Point;
    g.points = {{side / 2.0, side / 2.0}};
    r2d::drawSymbolizer(image->canvas(), r2d::Affine{}, g, p);

    // Cairo's bytes are premultiplied (render2d §48): an edge pixel half
    // covered by green is stored (0, 64, 0, 128). ImGui blends straight
    // colours: back to (0, 128, 0, 128), each colour divided by the alpha.
    const unsigned char* px = image->pixels();
    const int stride = image->stride();
    out.resize(static_cast<std::size_t>(side) * side * 4);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const unsigned char* in = px + y * stride + x * 4;
            unsigned char* o = out.data() + (static_cast<std::size_t>(y) * side + x) * 4;
            const int a = in[3];
            for (int c = 0; c < 3; ++c)
                o[c] = a == 0 ? 0 : static_cast<unsigned char>(std::min(255, (in[c] * 255 + a / 2) / a));
            o[3] = static_cast<unsigned char>(a);
        }
    return out;
}

LayerSymbol LayerSymbols::get(const LayerEntry& layer, double scaleDenominator)
{
    const std::string key = layer.style + '\x1f' + layer.styleText + '\x1f' + std::to_string(layer.colour)
                          + '\x1f' + layer.geometry;
    Cached& c = m_cache[layer.id];
    if (c.key != key) {   // first time, or what the style comes from has changed
        c.key   = key;
        c.style = layerStyle(layer);
    }
    return symbolOf(c.style, layer.geometry, scaleDenominator);
}

} // namespace e2d
