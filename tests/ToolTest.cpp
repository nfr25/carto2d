// The tools (notions §27 to §29): rectangle, zoom window, polygon, Esc.
// Positions in metres, at 0.5 m per pixel: 4 pixels = 2 m.

#include "view/MeasureText.h"
#include "view/Tool.h"

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

constexpr double kRes = 0.5;   // m per pixel

using e2d::ToolKind;
using Kind = e2d::ToolResult::Kind;

void testRectangle()
{
    e2d::Tool t;
    t.setKind(ToolKind::Rectangle);
    t.press({100, 200});
    t.move({160, 170});
    check(t.drawing() && t.rectangleActive(), "rectangle: drawing while the button is held");
    const auto r = t.release({160, 170}, kRes);
    check(r.kind == Kind::Zone && r.ring.size() == 4, "rectangle: released -> a zone of 4 points");
    check(r.ring.size() == 4 && r.ring[0].x == 100 && r.ring[0].y == 170 && r.ring[2].x == 160 &&
              r.ring[2].y == 200,
          "rectangle: corners put in order, (100;170) to (160;200)");
    check(!t.drawing() && t.kind() == ToolKind::Rectangle, "rectangle: finished, the tool stays (§27)");
}

void testRectangleClick()
{
    e2d::Tool t;
    t.setKind(ToolKind::ZoomWindow);
    t.press({100, 200});
    const auto r = t.release({101, 201}, kRes);   // 1.4 m = 2.8 px
    check(r.kind == Kind::Click && r.at.x == 101, "zoom window: under 4 px it is a click (§28)");
}

void testPolygon()
{
    e2d::Tool t;
    t.setKind(ToolKind::Polygon);
    t.press({0, 0});
    t.release({0, 0}, kRes);
    t.press({10, 0});
    t.release({10, 0}, kRes);
    t.press({10, 10});
    t.release({10, 10}, kRes);
    check(t.vertices().size() == 3, "polygon: a vertex per click");
    t.backspace();
    check(t.vertices().size() == 2, "polygon: Backspace removes the last vertex");
    t.press({10, 10});
    t.release({10, 10}, kRes);
    // The double-click: its 1st click adds (0;10), its 2nd press finishes,
    // its 2nd release adds nothing.
    t.press({0, 10});
    t.release({0, 10}, kRes);
    t.press({0, 10});
    const auto r = t.doubleClick();
    check(r.kind == Kind::Zone && r.ring.size() == 4, "polygon: double-click -> a zone of 4 vertices");
    t.release({0, 10}, kRes);
    check(!t.drawing() && t.vertices().empty(), "polygon: the double-click's release starts nothing");
}

void testPolygonDuplicates()
{
    e2d::Tool t;
    t.setKind(ToolKind::Polygon);
    t.release({0, 0}, kRes);
    t.release({1, 1}, kRes);   // 1.4 m < 2 m: the same vertex
    t.release({10, 0}, kRes);
    check(t.vertices().size() == 2, "polygon: a click within 4 px of the last vertex adds nothing");
    const auto r = t.enter();
    check(r.kind == Kind::None && !t.drawing(), "polygon: Enter with 2 vertices -> nothing, abandoned (§29)");
}

void testEscape()
{
    e2d::Tool t;
    t.setKind(ToolKind::Polygon);
    t.release({0, 0}, kRes);
    check(t.escape(), "Esc: a shape in progress is abandoned -> true");
    check(t.kind() == ToolKind::Polygon && !t.drawing(), "Esc: the tool stays");
    check(!t.escape(), "Esc again: nothing to abandon -> false (the caller goes back to the pointer)");
}

void testPointer()
{
    e2d::Tool t;
    t.press({0, 0});
    const auto r = t.release({50, 50}, kRes);
    check(r.kind == Kind::None && !t.drawing(), "pointer: the tool produces nothing (DragGesture does it)");
}

} // namespace

// §72: a click on the first vertex (within 8 px = 4 m at 0.5 m/px) closes.
void testCloseOnFirstVertex()
{
    e2d::Tool t;
    t.setKind(ToolKind::Polygon);
    for (const r2d::Point p : {r2d::Point{0, 0}, r2d::Point{100, 0}, r2d::Point{100, 100}}) {
        t.press(p);
        t.move(p);
        t.release(p, kRes);
    }
    t.move({3, 2});   // 3.6 m = 7.2 px from the first vertex
    check(t.onFirstVertex(kRes), "polygon: the mouse within 8 px of the first vertex: it will close");
    t.press({3, 2});
    const auto r = t.release({3, 2}, kRes);
    check(r.kind == Kind::Zone && r.ring.size() == 3 && !t.drawing(), "polygon: clicked there, closed, 3 vertices");

    // Two vertices only: near the first one, it is a vertex, not a close.
    t.setKind(ToolKind::Polygon);
    for (const r2d::Point p : {r2d::Point{0, 0}, r2d::Point{100, 0}, r2d::Point{3, 2}}) {
        t.press(p);
        t.move(p);
        t.release(p, kRes);
    }
    check(t.vertices().size() == 3, "polygon: 2 vertices, a click near the first adds a 3rd");
}

// §72: the measuring tools finish as a polygon does.
void testMeasure()
{
    e2d::Tool t;
    t.setKind(ToolKind::MeasureLine);
    for (const r2d::Point p : {r2d::Point{0, 0}, r2d::Point{300, 400}}) {
        t.press(p);
        t.move(p);
        t.release(p, kRes);
    }
    auto r = t.enter();
    check(r.kind == Kind::Measure && r.ring.size() == 2 && !r.closed, "distance: 2 points, Enter: a measure, open");

    t.setKind(ToolKind::MeasureArea);
    for (const r2d::Point p : {r2d::Point{0, 0}, r2d::Point{100, 0}, r2d::Point{100, 100}, r2d::Point{1, 1}}) {
        t.press(p);
        t.move(p);
        r = t.release(p, kRes);
    }
    check(r.kind == Kind::Measure && r.closed && r.ring.size() == 3, "area: closed on its first vertex, 3 points");

    check(e2d::lengthText(812.4712) == "812.47 m", "812.47 m");
    check(e2d::lengthText(3251.0) == "3.251 km", "3.251 km");
    check(e2d::lengthText(1000.041) == "1.000 km", "1.000 km");
    check(e2d::areaText(652.44) == "652.4 m²", "652.4 m²");
    check(e2d::areaText(10000.83) == "1.00 ha", "1.00 ha (the square of the notes)");
    check(e2d::areaText(42700.0) == "4.27 ha", "4.27 ha");
    check(e2d::areaText(12600000.0) == "12.6 km²", "12.6 km²");
    check(e2d::groupedNumber(1234567.89, 1) == "1,234,567.9", "1,234,567.9");
}

int main()
{
    testRectangle();
    testRectangleClick();
    testPolygon();
    testPolygonDuplicates();
    testEscape();
    testPointer();
    testCloseOnFirstVertex();
    testMeasure();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
