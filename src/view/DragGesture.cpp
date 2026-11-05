#include "view/DragGesture.h"

#include <cmath>

namespace e2d {

void DragGesture::press(double x, double y)
{
    m_pressed  = true;
    m_dragging = false;
    m_pressX = m_lastX = x;
    m_pressY = m_lastY = y;
}

DragGesture::Pan DragGesture::move(double x, double y)
{
    Pan pan;
    if (!m_pressed)
        return pan;
    if (!m_dragging) {
        // §20: sqrt(dx^2 + dy^2) <= 4: still a click, the map stays.
        if (std::hypot(x - m_pressX, y - m_pressY) <= kClickTolerance)
            return pan;
        m_dragging = true;   // crossed: catch up with the whole way from the press
        pan = {x - m_pressX, y - m_pressY};
    }
    else {
        pan = {x - m_lastX, y - m_lastY};
    }
    m_lastX = x;
    m_lastY = y;
    return pan;
}

bool DragGesture::release()
{
    const bool click = m_pressed && !m_dragging;
    m_pressed  = false;
    m_dragging = false;
    return click;
}

} // namespace e2d
