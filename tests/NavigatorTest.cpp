// The wanted view and the placement of an image (notions §10), with the very
// numbers of the notions. No window, no OpenGL.

#include "view/Navigator.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// The image of the notions: xmin 149 000, ymax 171 000, 0.5 m/px, 1200 x 900.
e2d::Navigator start()
{
    e2d::Navigator nav;
    nav.fit({149000.0, 171000.0 - 900 * 0.5, 149000.0 + 1200 * 0.5, 171000.0}, 1200, 900);
    return nav;
}

void testFit()
{
    const e2d::Navigator nav = start();
    check(near(nav.state().xmin, 149000.0) && near(nav.state().ymax, 171000.0) && near(nav.state().res, 0.5),
          "fit: corner 149000 / 171000, 0.5 m/px");
    const r2d::View v = nav.view();
    check(v.size.width == 1200 && v.size.height == 900 && near(v.resolution, 0.5) && !v.widened,
          "view(): same size, same resolution, not widened");
}

// §10, example 1.
void testPan()
{
    e2d::Navigator nav = start();
    const e2d::ViewState image = nav.state();
    nav.pan(120.0, 40.0);
    check(near(nav.state().xmin, 148940.0), "pan: xmin 149000 - 60 = 148940");
    check(near(nav.state().ymax, 171020.0), "pan: ymax 171000 + 20 = 171020");
    const e2d::Placement p = nav.placement(image);
    check(near(p.k, 1.0) && near(p.x0, 120.0) && near(p.y0, 40.0), "pan: image drawn at (120, 40), k = 1");
}

// §10, example 2.
void testZoom()
{
    e2d::Navigator nav = start();
    const e2d::ViewState image = nav.state();
    nav.zoomAt(300.0, 200.0, 2.0);
    check(near(nav.state().res, 0.25), "zoom x2: 0.25 m/px");
    check(near(nav.state().xmin, 149075.0), "zoom: xmin 149150 - 75 = 149075");
    check(near(nav.state().ymax, 170950.0), "zoom: ymax 170900 + 50 = 170950");
    const e2d::Placement p = nav.placement(image);
    check(near(p.k, 2.0) && near(p.x0, -300.0) && near(p.y0, -200.0), "zoom: image at (-300, -200), k = 2");
    check(near(p.x0 + 300.0 * p.k, 300.0), "zoom: image pixel 300 still under the mouse");

    const r2d::Point under = nav.toWorld(300.0, 200.0);
    check(near(under.x, 149150.0) && near(under.y, 170900.0), "zoom: (149150, 170900) still under the mouse");
}

// §12: the centre of pixel (640, 372) of the wanted view.
void testMouse()
{
    const e2d::Navigator nav = start();
    const r2d::Point p = nav.toWorld(640.5, 372.5);
    check(near(p.x, 149320.25) && near(p.y, 170813.75), "mouse: (149320.25, 170813.75)");
}

void testLimitsAndResize()
{
    e2d::Navigator nav = start();
    nav.zoomAt(0.0, 0.0, 1e9);
    check(near(nav.state().res, e2d::Navigator::kMinRes), "zoom in: stops at 1 cm per pixel");

    e2d::Navigator grow = start();
    grow.resize(1600, 1000);
    check(near(grow.state().xmin, 149000.0) && near(grow.state().res, 0.5) && grow.view().size.width == 1600,
          "resize: corner and scale kept, more map shown");
}

} // namespace

// §28: the zoom window, with the numbers of the notions.
void testFitRect()
{
    e2d::Navigator nav = start();
    nav.resize(1870, 982);
    nav.fitRect({149500.0, 170600.0, 149800.0, 170700.0});
    check(near(nav.state().res, 300.0 / 1870.0), "fitRect: the larger resolution, 300 / 1870 = 0.16043 m/px");
    check(near(nav.state().xmin, 149500.0), "fitRect: xmin 149650 - 935 x 0.16043 = 149500.00");
    check(near(nav.state().ymax, 170728.77, 0.005), "fitRect: ymax 170650 + 491 x 0.16043 = 170728.77");
    const double before = nav.state().res;
    nav.fitRect({149500.0, 170600.0, 149500.0, 170700.0});   // no width: nothing
    check(near(nav.state().res, before), "fitRect: a rectangle without width changes nothing");
    nav.fitRect({0.0, 0.0, 0.001, 0.001});
    check(near(nav.state().res, e2d::Navigator::kMinRes), "fitRect: never closer than 1 cm per pixel");
}

// §9: the keyboard. The arrows move by a third of the map, + and - zoom by 2
// around its centre; the very numbers of the notions.
void testKeys()
{
    e2d::Navigator nav = start();
    nav.pan(-1200 / 3.0, 0.0);   // right arrow: look further east
    check(near(nav.state().xmin, 149200.0) && near(nav.state().ymax, 171000.0), "right arrow: xmin 149200");
    nav = start();
    nav.pan(0.0, 900 / 3.0);     // up arrow: look further north
    check(near(nav.state().ymax, 171150.0) && near(nav.state().xmin, 149000.0), "up arrow: ymax 171150");
    nav = start();
    nav.zoomAt(600.0, 450.0, 2.0);   // +
    check(near(nav.state().res, 0.25) && near(nav.state().xmin, 149150.0) && near(nav.state().ymax, 170887.5),
          "+: 0.25 m/px, xmin 149150, ymax 170887.5, the centre stays");
    nav.zoomAt(600.0, 450.0, 0.5);   // - : back to the start
    check(near(nav.state().res, 0.5) && near(nav.state().xmin, 149000.0) && near(nav.state().ymax, 171000.0),
          "-: back to 0.5 m/px and the same corner");
}

int main()
{
    testFit();
    testPan();
    testZoom();
    testMouse();
    testLimitsAndResize();
    testFitRect();
    testKeys();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
