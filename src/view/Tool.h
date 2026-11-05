#pragma once

#include "core/point.hpp"

#include <vector>

namespace e2d {

// ============================================================================
// Tool — what the left button does (notions §27 to §29).
//
// One tool at a time. A tool PRODUCES A SHAPE; what is done with it (a
// question, a new view) is the caller's business: the "end" of §27.
// Every position is in terrain coordinates, given with the metres per screen
// pixel at that moment (`res`): the shape follows the wheel and the middle
// button, and the 4-pixel thresholds still hold after a zoom.
// Pure: no ImGui, no OpenGL, tested without a window.
// ============================================================================

enum class ToolKind { Pointer, Rectangle, Polygon, ZoomWindow, MeasureLine, MeasureArea };   // §72: the last two

const char* toolName(ToolKind kind);   // "Pointer", "Rectangle", ...

// What an event produced.
struct ToolResult {
    enum class Kind { None, Click, Zone, Measure };
    Kind                    kind = Kind::None;
    r2d::Point              at;     // Click: where
    std::vector<r2d::Point> ring;   // Zone: the shape, not closed (4 points for a rectangle); Measure: the points
    bool                    closed = false;   // Measure: an area (a polygon), else a length
};

class Tool {
public:
    static constexpr double kClickTolerancePx = 4.0;   // §20
    static constexpr double kClosePx = 8.0;            // §72: a click this near the first vertex closes the polygon

    ToolKind kind() const { return m_kind; }
    void     setKind(ToolKind kind);   // abandons a shape in progress

    // The left button. Pointer: nothing here (DragGesture does it, §20).
    void       press(r2d::Point p);
    void       move(r2d::Point p);                 // the mouse, button held or not
    ToolResult release(r2d::Point p, double res);

    ToolResult doubleClick();   // Polygon: finish (§29)
    ToolResult enter();         // Polygon: finish
    void       backspace();     // Polygon: the last vertex off

    // §27: abandons the shape in progress. True if there was one.
    bool escape();

    bool drawing() const;   // a shape in progress

    // §29, §72: the tools that click vertices one by one (a polygon, a
    // measure), and those whose shape closes (a polygon, an area).
    bool clicksVertices() const;
    bool closes() const;
    // §72: the mouse is on the first vertex: a click there closes. `res`:
    // metres per screen pixel now.
    bool onFirstVertex(double res) const;

    // For the drawing (§28, §29).
    // Rectangle, Zoom window: the two corners while the button is held.
    bool       rectangleActive() const { return m_pressed; }
    r2d::Point corner1() const { return m_corner1; }
    r2d::Point corner2() const { return m_corner2; }
    // Polygon: the vertices so far, and the mouse (the elastic line).
    const std::vector<r2d::Point>& vertices() const { return m_vertices; }
    r2d::Point                     hover() const { return m_hover; }

private:
    ToolResult finishPolygon();
    void       reset();

    ToolKind m_kind = ToolKind::Pointer;

    // Rectangle, Zoom window.
    bool       m_pressed = false;
    r2d::Point m_corner1, m_corner2;

    // Polygon.
    std::vector<r2d::Point> m_vertices;
    r2d::Point              m_hover;
    bool                    m_ignoreRelease = false;   // the release of the double-click's 2nd click
};

} // namespace e2d
