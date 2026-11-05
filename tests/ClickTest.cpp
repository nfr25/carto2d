// A click or a drag (notions §20), with its numbers.

#include "view/DragGesture.h"

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

void testClick()
{
    e2d::DragGesture g;
    g.press(640, 410);
    const auto pan = g.move(642, 413);   // sqrt(4 + 9) = 3.6 px
    check(pan.dx == 0 && pan.dy == 0 && !g.dragging(), "3.6 px: the map does not move");
    check(g.release(), "released: a click");
}

void testDrag()
{
    e2d::DragGesture g;
    g.press(640, 410);
    auto pan = g.move(700, 430);   // sqrt(3600 + 400) = 63 px
    check(g.dragging() && pan.dx == 60 && pan.dy == 20, "63 px: a drag, catching up with (60, 20) at once");
    pan = g.move(710, 430);
    check(pan.dx == 10 && pan.dy == 0, "then the movement since the last call: (10, 0)");
    check(!g.release(), "released: not a click");
}

void testBackToStart()
{
    // Once a drag, always a drag: coming back to the press point is not a click.
    e2d::DragGesture g;
    g.press(100, 100);
    g.move(120, 100);
    g.move(100, 100);
    check(!g.release(), "a drag that comes back: still not a click");
}

} // namespace

int main()
{
    testClick();
    testDrag();
    testBackToStart();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
