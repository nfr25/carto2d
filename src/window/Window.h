#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

namespace e2d {

// Snapshot of the mouse for one frame.
struct MouseState {
    double x = 0.0;      // cursor position in WINDOW coordinates, origin top-left,
    double y = 0.0;      // y pointing DOWN (on Windows: real screen points, notions §7)
    bool left   = false; // buttons currently held
    bool middle = false;
    bool right  = false;
    float scroll = 0.0f; // wheel notches since the previous frame (+ = away from you)

    // EDGES of the left button since the previous frame, from events: a quick
    // click can fit between two frames, and polling alone would never see it.
    bool leftPressed  = false;
    bool leftReleased = false;
    bool middlePressed = false;   // the middle button: pans with every tool (§27)
};

// Owns the GLFW window and its OpenGL 4.5 core context.
// Copied from editor3d (app/Window); differences in DIVERGENCES.md.
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // width x height in LOGICAL pixels: the window is enlarged by the
    // monitor's scale (150 % -> 1.5 times larger), as other applications are.
    bool init(int width, int height, const char* title);
    void shutdown();

    bool shouldClose() const;
    void setShouldClose(bool close);   // false: the close is cancelled (a question first, §45)
    void setTitle(const std::string& title);
    void pollEvents();
    // §56: sleeps until an event (mouse, keyboard, size, a dropped file, the
    // bell of a worker) or `seconds`.
    void waitEvents(double seconds);
    static void ring();   // wakes waitEvents; may be called from any thread

    // §56: true if the mouse, the keyboard, the window... did anything since
    // the last call (a worker's bell is not input: its result says itself).
    bool takeInput();
    void swapBuffers();

    // Size in pixels of the drawable area.
    void framebufferSize(int& width, int& height) const;

    // Size in window coordinates: the units of the mouse position.
    // On Windows, the same numbers as framebufferSize (notions §7).
    void windowSize(int& width, int& height) const;

    // The enlargement Windows asks for: 1.5 on a screen set to 150 %.
    // This is the pixel ratio given to render2d, and ImGui's scale. notions §7.
    float contentScale() const;

    // Mouse position and buttons are POLLED; the wheel and the button edges
    // only come through callbacks, so they are accumulated and handed out
    // here, then reset ("consume").
    MouseState consumeMouse();

    // The files dropped on the window since the last call (UTF-8 paths, notions §39).
    std::vector<std::string> takeDrops();

    double time() const;

    GLFWwindow* handle() const { return m_handle; }

private:
    static void scrollCallback(GLFWwindow* window, double xOffset, double yOffset);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void dropCallback(GLFWwindow* window, int count, const char** paths);
    static void touch(GLFWwindow* window);   // §56: some input arrived

    GLFWwindow* m_handle = nullptr;
    float m_scrollAccumulator = 0.0f;
    bool  m_leftPressed  = false; // accumulated by the callback, reset by consumeMouse()
    bool  m_leftReleased = false;
    bool  m_middlePressed = false;
    bool  m_input = true;              // §56: the first frames are drawn
    std::vector<std::string> m_drops;   // filled by dropCallback, emptied by takeDrops()
};

} // namespace e2d
