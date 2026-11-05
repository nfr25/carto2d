#pragma once

namespace e2d {

// ============================================================================
// DragGesture — one press of the left button: a click, or a drag? (§20)
//
// Within 4 pixels of the press, the map does not move: a hand that clicks
// trembles a little. Beyond, it catches up at once with the way covered,
// then follows the hand. At the release: a click if it never went beyond.
// Pure arithmetic: tested without a window.
// ============================================================================
class DragGesture {
public:
    static constexpr double kClickTolerance = 4.0;   // pixels, as editor3d

    struct Pan {
        double dx = 0.0, dy = 0.0;
    };

    void press(double x, double y);

    // The pan to apply now: nothing while within the tolerance; the whole way
    // since the press when crossing it; then the movement since the last call.
    Pan move(double x, double y);

    // True: it was a click (the map never moved).
    bool release();

    bool pressed() const { return m_pressed; }
    bool dragging() const { return m_dragging; }

private:
    bool   m_pressed = false;
    bool   m_dragging = false;
    double m_pressX = 0.0, m_pressY = 0.0;
    double m_lastX = 0.0, m_lastY = 0.0;
};

} // namespace e2d
