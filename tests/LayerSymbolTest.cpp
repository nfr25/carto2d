// The symbol in front of a layer's name (notions §68): which rule, which
// symbolizer, which colours. No window: what ImGui draws is decided here.

#include "panels/LayerSymbol.h"

#include "style/ysld_reader.hpp"

#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

bool near(double a, double b) { return std::fabs(a - b) <= 1e-6; }

// The buildings of render2d's styles/batiments.yaml (notions §68).
const char* kBuildings = R"(name: batiments
feature-styles:
- rules:
  - name: proche
    scale: [min, 15000]
    symbolizers:
    - polygon:
        fill-color: '#C98B6B'
        stroke-color: '#7A4A35'
        stroke-width: 0.8
  - name: loin
    scale: [15000, max]
    symbolizers:
    - polygon:
        fill-color: '#8E7B6B'
)";

r2d::Style parse(const char* text)
{
    return *r2d::parseYsld(text).style;
}

void testScaleBands()
{
    const r2d::Style s = parse(kBuildings);
    const e2d::LayerSymbol near5000 = e2d::symbolOf(s, "polygon", 5000);
    check(near5000.kind == e2d::LayerSymbol::Kind::Fill, "1:5,000: a filled rectangle");
    check(near5000.fill && near(near5000.fill->r, 0xC9 / 255.0), "1:5,000: the 'proche' fill, #C98B6B");
    check(near5000.stroke && near(near5000.stroke->r, 0x7A / 255.0) && near(near5000.strokeWidth, 0.8),
          "1:5,000: its outline, #7A4A35, 0.8 px");

    const e2d::LayerSymbol far = e2d::symbolOf(s, "polygon", 20000);
    check(far.fill && near(far.fill->r, 0x8E / 255.0), "1:20,000: the 'loin' fill, #8E7B6B");
    check(!far.stroke, "1:20,000: no outline");

    const e2d::LayerSymbol edge = e2d::symbolOf(s, "polygon", 15000);
    check(edge.fill && near(edge.fill->r, 0x8E / 255.0), "1:15,000 exactly: 'loin' (min <= d < max)");
}

void testOutOfScale()
{
    const r2d::Style s = parse(R"(feature-styles:
- rules:
  - scale: [min, 2500]
    symbolizers:
    - point:
        size: 8
        symbols:
        - mark:
            shape: triangle
            fill-color: '#FF0000'
)");
    const e2d::LayerSymbol m = e2d::symbolOf(s, "point", 10000);
    check(m.kind == e2d::LayerSymbol::Kind::Mark && m.shape == r2d::MarkShape::Triangle,
          "out of scale: still the first rule's mark (the name is grey)");
    check(near(m.size, 8.0) && m.fill && near(m.fill->r, 1.0), "its size 8, its red fill");
}

void testGeometryPicks()
{
    // A line layer whose rule has a text first, then the line.
    const r2d::Style s = parse(R"(feature-styles:
- rules:
  - symbolizers:
    - text:
        label: ${NAME}
    - line:
        stroke-color: '#3366CC'
        stroke-width: 2
)");
    const e2d::LayerSymbol l = e2d::symbolOf(s, "line", 5000);
    check(l.kind == e2d::LayerSymbol::Kind::Line && near(l.strokeWidth, 2.0), "the line, not the label before it");

    const r2d::Style labels = parse(R"(feature-styles:
- rules:
  - symbolizers:
    - text:
        label: ${POLICENUM}
)");
    check(e2d::symbolOf(labels, "point", 1000).kind == e2d::LayerSymbol::Kind::Text, "labels only: an 'a'");
}

void testDefaultAndRaster()
{
    e2d::LayerEntry layer("parcels", "polygon", true);
    layer.colour = 2;
    const r2d::Style d = e2d::layerStyle(layer);
    const e2d::LayerSymbol s = e2d::symbolOf(d, "polygon", 5000);
    check(s.kind == e2d::LayerSymbol::Kind::Fill && s.fill, "no style: render2d's default style, filled");

    e2d::LayerEntry other = layer;
    other.colour = 3;
    const e2d::LayerSymbol t = e2d::symbolOf(e2d::layerStyle(other), "polygon", 5000);
    check(t.fill && (!near(t.fill->r, s.fill->r) || !near(t.fill->g, s.fill->g) || !near(t.fill->b, s.fill->b)),
          "another colour index, another colour (§38)");

    e2d::LayerEntry ortho("ortho", "raster", true);
    check(e2d::symbolOf(e2d::layerStyle(ortho), "raster", 5000).kind == e2d::LayerSymbol::Kind::Raster,
          "a raster without style: the grey ramp");
    const r2d::Style faded = parse(R"(feature-styles:
- rules:
  - symbolizers:
    - raster:
        opacity: 0.6
)");
    check(near(e2d::symbolOf(faded, "raster", 5000).opacity, 0.6), "a raster at 0.6: the ramp at 0.6");
}

void testCache()
{
    e2d::LayerSymbols symbols;
    e2d::LayerEntry layer("buildings", "polygon", true);
    layer.styleText = kBuildings;
    check(symbols.get(layer, 5000).stroke.has_value(), "cache: read once");
    layer.styleText = R"(feature-styles:
- rules:
  - symbolizers:
    - polygon:
        fill-color: '#00FF00'
)";
    const e2d::LayerSymbol s = symbols.get(layer, 5000);
    check(s.fill && near(s.fill->g, 1.0) && !s.stroke, "cache: the dialog's new text is read again");
}

// §78: a symbol with an image, drawn by render2d in a small square for the
// Layers panel: its colours straight (not premultiplied), transparent around;
// the fallback mark when the image cannot be read.
void testImageSymbol()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path();
    const std::string svg = (dir / "e2d_green.svg").generic_string();
    std::ofstream(svg) << R"(<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"><circle cx="5" cy="5" r="5" fill="#00FF00"/></svg>)";
    const std::string text = std::string(R"(feature-styles:
- rules:
  - symbolizers:
    - point:
        size: 16
        symbols:
        - external: {url: ')") + svg + R"(', format: image/svg+xml}
        - mark: {shape: square, fill-color: '#FF0000'}
)";
    const r2d::Style style = parse(text.c_str());
    const e2d::LayerSymbol s = e2d::symbolOf(style, "point", 1000);
    // The same file, whatever the separators: render2d gives Windows' "\\", we wrote "/".
    check(s.kind == e2d::LayerSymbol::Kind::Mark &&
              fs::path(s.image).lexically_normal() == fs::path(svg).lexically_normal(),
          "an image symbol: its path kept");

    // 24 px square at a ratio of 1: the circle is 16 px (between 9.6 and 21.6).
    const std::vector<unsigned char> px = e2d::symbolPixels(s, 24, 1.0f);
    check(px.size() == 24 * 24 * 4, "24 x 24, 4 bytes each");
    if (px.size() == 24 * 24 * 4) {
        auto at = [&](int x, int y, int c) { return int(px[(std::size_t(y) * 24 + x) * 4 + c]); };   // B G R A
        check(at(12, 12, 1) == 255 && at(12, 12, 2) == 0 && at(12, 12, 3) == 255, "the centre: green, opaque");
        check(at(1, 1, 3) == 0, "a corner: transparent");
        // An edge pixel, partly covered: its green stays 255 (straight), only its alpha is partial.
        int edges = 0, straight = 0;
        for (int x = 0; x < 24; ++x) {
            const int a = at(x, 12, 3);
            if (a > 0 && a < 255) {
                ++edges;
                if (at(x, 12, 1) >= 250)
                    ++straight;
            }
        }
        check(edges >= 2 && straight == edges, "the edges: partial alpha, green not darkened");
    }

    // The image unreadable: the fallback mark, a red square.
    e2d::LayerSymbol missing = s;
    missing.image = (dir / "e2d_no_such.svg").generic_string();
    const std::vector<unsigned char> fb = e2d::symbolPixels(missing, 24, 1.0f);
    check(fb.size() == 24 * 24 * 4 && fb[(12 * 24 + 12) * 4 + 2] == 255 && fb[(12 * 24 + 12) * 4 + 1] == 0,
          "unreadable: the fallback mark, red");
    fs::remove(svg);
}

} // namespace

int main()
{
    testScaleBands();
    testOutOfScale();
    testGeometryPicks();
    testDefaultAndRaster();
    testCache();
    testImageSymbol();
    std::printf("%s\n", g_failures ? "FAILURES" : "all ok");
    return g_failures ? 1 : 0;
}
