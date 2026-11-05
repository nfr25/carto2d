// render2d seen from editor2d: its headers are found, its libraries link,
// and a View gives the numbers of notions §7 (a 1200 x 900 panel at 0.5 m/px).
// No window, no OpenGL.

#include <cmath>
#include <cstdio>
#include <string>

#include "app/version.hpp"
#include "backend/cairo/cairo_target.hpp"
#include "backend/gdal/gdal_layer.hpp"
#include "core/view.hpp"
#include "panels/ValueText.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

} // namespace

// §64: a number shown whole, as short as possible, never rounded.
void testValueText()
{
    check(e2d::valueText(5000070.0) == "5000070", "5000070.0 -> 5000070 (not 5.00007e+06)");
    check(e2d::valueText(8000100.25) == "8000100.25", "8000100.25 kept whole");
    check(e2d::valueText(412.6) == "412.6", "412.6");
    check(e2d::valueText(0.1) == "0.1", "0.1 (not 0.1000000000000000055)");
    check(e2d::valueText(r2d::Value{}) == "NULL", "NULL");
    check(e2d::valueText(r2d::Value{21009LL}) == "21009", "an integer");
}

int main()
{
    testValueText();
    std::printf("render2d %s, GDAL %s, Cairo %s\n", r2d::kVersion, r2d::gdalVersion().c_str(),
                r2d::cairoVersion().c_str());
    check(std::string(r2d::kVersion).size() > 0, "render2d version reachable");
    check(!r2d::gdalVersion().empty(), "r2d_gdal linked");
    check(!r2d::cairoVersion().empty(), "r2d_cairo linked");

    // notions §7: 1200 x 900 pixels over 600 x 450 m -> 0.5 m/px.
    const r2d::Envelope area{149000.0, 170550.0, 149600.0, 171000.0};
    auto view = r2d::makeView(area, r2d::ImageSize{1200, 900});
    check(view.has_value(), "makeView");
    if (view) {
        view->pixelRatio = 1.5;
        check(near(view->resolution, 0.5), "resolution 0.5 m/px");
        // 0.5 x 1.5 / 0.00028 = 2678.57...
        check(near(view->scaleDenominator(), 0.75 / 0.00028), "scale 1:2679 at pixel ratio 1.5");
        // notions §12: centre of pixel (640, 372) -> (149320.25, 170813.75).
        const r2d::Point p = view->imageToWorld.apply({640.5, 372.5});
        check(near(p.x, 149320.25, 1e-6) && near(p.y, 170813.75, 1e-6), "mouse at (640, 372)");
    }

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
