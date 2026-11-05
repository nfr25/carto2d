#pragma once

#include "core/envelope.hpp"
#include "core/point.hpp"
#include "core/view.hpp"

namespace e2d {

// Where the map is: the top-left corner in metres and the metres per pixel.
// Enough to place any image: x = xmin + px * res, y = ymax - py * res.
struct ViewState {
    double xmin = 0.0;
    double ymax = 0.0;
    double res  = 1.0;   // metres per image pixel
};

// Where an image drawn for one ViewState falls on a screen showing another:
// a scale and a shift, nothing else (notions §10).
struct Placement {
    double x0 = 0.0, y0 = 0.0;   // the image's top-left corner on the screen, pixels
    double k  = 1.0;             // image pixels -> screen pixels
};

// The WANTED view (notions §10): changed at once by every gesture, it decides
// the coordinates under the mouse, the scale shown and the next render.
// Pure maths, no OpenGL, no ImGui: tested with the numbers of the notions.
// All positions are in screen pixels (one image pixel per screen point, §7).
class Navigator {
public:
    // The whole `bbox` in a width x height screen, widened to its shape
    // (render2d's makeView, §4). The start of the session.
    void fit(const r2d::Envelope& bbox, int width, int height);

    // The screen changed size: the top-left corner and the scale stay, the
    // map simply shows more or less around them.
    void resize(int width, int height);

    // The map follows the hand: dragging (dx, dy) pixels to the right and down.
    void pan(double dx, double dy);

    // Zoom by `factor` (> 1 = closer), the point under (px, py) staying there.
    void zoomAt(double px, double py, double factor);

    // Zoom window (§28): the whole rectangle in the screen, centred; the
    // larger of the two resolutions, so that both dimensions fit.
    void fitRect(const r2d::Envelope& rect);

    const ViewState& state() const { return m_state; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    // The r2d::View to render the wanted view at the screen's size.
    r2d::View view() const;

    // A screen position (pixels, continuous) -> metres.
    r2d::Point toWorld(double px, double py) const;

    // Where to draw an image rendered for `image` in the wanted view (§10).
    Placement placement(const ViewState& image) const;

    // Resolution limits: 1 cm to 1 km per pixel.
    static constexpr double kMinRes = 0.01;
    static constexpr double kMaxRes = 1000.0;

private:
    ViewState m_state;
    int m_width = 1, m_height = 1;
};

// The ViewState of a rendered r2d::View.
ViewState stateOf(const r2d::View& view);

} // namespace e2d
