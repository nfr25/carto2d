#include "view/Tool.h"

#include <algorithm>
#include <cmath>

namespace e2d {

namespace {

double distance(r2d::Point a, r2d::Point b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

} // namespace

const char* toolName(ToolKind kind)
{
    switch (kind) {
    case ToolKind::Pointer:    return "Pointer";
    case ToolKind::Rectangle:  return "Rectangle";
    case ToolKind::Polygon:    return "Polygon";
    case ToolKind::ZoomWindow: return "Zoom window";
    case ToolKind::MeasureLine: return "Distance";
    case ToolKind::MeasureArea: return "Area";
    }
    return "?";
}

void Tool::setKind(ToolKind kind)
{
    reset();
    m_kind = kind;
}

void Tool::reset()
{
    m_pressed = false;
    m_vertices.clear();
    m_ignoreRelease = false;
}

bool Tool::drawing() const
{
    return m_pressed || !m_vertices.empty();
}

bool Tool::clicksVertices() const
{
    return m_kind == ToolKind::Polygon || m_kind == ToolKind::MeasureLine || m_kind == ToolKind::MeasureArea;
}

bool Tool::closes() const
{
    return m_kind == ToolKind::Polygon || m_kind == ToolKind::MeasureArea;
}

bool Tool::onFirstVertex(double res) const
{
    return closes() && m_vertices.size() >= 3 && distance(m_vertices.front(), m_hover) < kClosePx * res;
}

void Tool::press(r2d::Point p)
{
    if (m_kind == ToolKind::Rectangle || m_kind == ToolKind::ZoomWindow) {
        m_pressed = true;
        m_corner1 = m_corner2 = p;
    }
}

void Tool::move(r2d::Point p)
{
    m_hover = p;
    if (m_pressed)
        m_corner2 = p;
}

ToolResult Tool::release(r2d::Point p, double res)
{
    ToolResult r;
    const double tolerance = kClickTolerancePx * res;   // 4 pixels, in metres now

    if (m_kind == ToolKind::Rectangle || m_kind == ToolKind::ZoomWindow) {
        if (!m_pressed)
            return r;
        m_pressed = false;
        m_corner2 = p;
        // §28: less than 4 pixels from the first corner: a click, not a rectangle.
        if (distance(m_corner1, m_corner2) < tolerance) {
            r.kind = ToolResult::Kind::Click;
            r.at   = p;
            return r;
        }
        const double x0 = std::min(m_corner1.x, m_corner2.x), x1 = std::max(m_corner1.x, m_corner2.x);
        const double y0 = std::min(m_corner1.y, m_corner2.y), y1 = std::max(m_corner1.y, m_corner2.y);
        r.kind = ToolResult::Kind::Zone;
        r.ring = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        return r;
    }

    if (clicksVertices()) {
        if (m_ignoreRelease) {   // the double-click has already finished the polygon
            m_ignoreRelease = false;
            return r;
        }
        // §72: on the first vertex (8 pixels), with 3 vertices at least: it closes.
        m_hover = p;
        if (onFirstVertex(res))
            return finishPolygon();
        // §29: a vertex, unless it falls on the previous one (the double-click's 2nd click).
        if (m_vertices.empty() || distance(m_vertices.back(), p) >= tolerance)
            m_vertices.push_back(p);
        m_hover = p;
    }
    return r;
}

ToolResult Tool::doubleClick()
{
    if (!clicksVertices() || m_vertices.empty())
        return {};
    // The double-click comes at the 2nd PRESS: its release must add nothing.
    m_ignoreRelease = true;
    return finishPolygon();
}

ToolResult Tool::enter()
{
    if (!clicksVertices())
        return {};
    return finishPolygon();
}

void Tool::backspace()
{
    if (clicksVertices() && !m_vertices.empty())
        m_vertices.pop_back();
}

bool Tool::escape()
{
    const bool had = drawing();
    const bool ignore = m_ignoreRelease;
    reset();
    m_ignoreRelease = ignore;
    return had;
}

// Fewer than 3 vertices: a zone without surface contains nothing (§29). A
// distance needs 2 points (§72).
ToolResult Tool::finishPolygon()
{
    ToolResult r;
    if (m_kind == ToolKind::Polygon && m_vertices.size() >= 3) {
        r.kind = ToolResult::Kind::Zone;
        r.ring = m_vertices;
    } else if (m_kind == ToolKind::MeasureArea && m_vertices.size() >= 3) {
        r.kind   = ToolResult::Kind::Measure;
        r.ring   = m_vertices;
        r.closed = true;
    } else if (m_kind == ToolKind::MeasureLine && m_vertices.size() >= 2) {
        r.kind = ToolResult::Kind::Measure;
        r.ring = m_vertices;
    }
    m_vertices.clear();
    return r;
}

} // namespace e2d
