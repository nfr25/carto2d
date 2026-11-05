#include "view/Navigator.h"

#include <algorithm>

namespace e2d {

ViewState stateOf(const r2d::View& view)
{
    return {view.shown.xmin, view.shown.ymax, view.resolution};
}

void Navigator::fit(const r2d::Envelope& bbox, int width, int height)
{
    m_width  = std::max(width, 1);
    m_height = std::max(height, 1);
    if (const auto v = r2d::makeView(bbox, r2d::ImageSize{m_width, m_height}))
        m_state = stateOf(*v);
}

void Navigator::resize(int width, int height)
{
    m_width  = std::max(width, 1);
    m_height = std::max(height, 1);
}

// §10, example 1: dragging 120 px right and 40 px down at 0.5 m/px moves the
// corner 60 m west and 20 m north.
void Navigator::pan(double dx, double dy)
{
    m_state.xmin -= dx * m_state.res;
    m_state.ymax += dy * m_state.res;
}

// §10, example 2: the point under the mouse is computed with the old
// resolution, then the corner is put back so that it falls under the mouse
// with the new one.
// §28: 300 m x 100 m in 1870 x 982 px -> max(0.16043, 0.10183) = 0.16043 m/px,
// centred on (149650, 170650): xmin = 149650 - 935 x 0.16043 = 149500.
void Navigator::fitRect(const r2d::Envelope& rect)
{
    const double w = rect.xmax - rect.xmin, h = rect.ymax - rect.ymin;
    if (!(w > 0.0) || !(h > 0.0))
        return;
    const double res = std::clamp(std::max(w / m_width, h / m_height), kMinRes, kMaxRes);
    const double cx = (rect.xmin + rect.xmax) / 2.0, cy = (rect.ymin + rect.ymax) / 2.0;
    m_state.res  = res;
    m_state.xmin = cx - m_width / 2.0 * res;
    m_state.ymax = cy + m_height / 2.0 * res;
}

void Navigator::zoomAt(double px, double py, double factor)
{
    if (factor <= 0.0)
        return;
    const double newRes = std::clamp(m_state.res / factor, kMinRes, kMaxRes);
    const r2d::Point p  = toWorld(px, py);
    m_state.res  = newRes;
    m_state.xmin = p.x - px * newRes;
    m_state.ymax = p.y + py * newRes;
}

r2d::View Navigator::view() const
{
    const double w = m_width * m_state.res, h = m_height * m_state.res;
    const r2d::Envelope e{m_state.xmin, m_state.ymax - h, m_state.xmin + w, m_state.ymax};
    // Same shape as the screen: makeView keeps this resolution (no widening).
    return *r2d::makeView(e, r2d::ImageSize{m_width, m_height});
}

r2d::Point Navigator::toWorld(double px, double py) const
{
    return {m_state.xmin + px * m_state.res, m_state.ymax - py * m_state.res};
}

// §10: k = res_image / res_wanted ; x0 = (xmin_image - xmin_wanted) / res_wanted ;
//      y0 = (ymax_wanted - ymax_image) / res_wanted.
Placement Navigator::placement(const ViewState& image) const
{
    Placement p;
    p.k  = image.res / m_state.res;
    p.x0 = (image.xmin - m_state.xmin) / m_state.res;
    p.y0 = (m_state.ymax - image.ymax) / m_state.res;
    return p;
}

} // namespace e2d
