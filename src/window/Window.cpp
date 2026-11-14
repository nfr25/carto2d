#include "window/Window.h"

#ifndef _WIN32
#include "window/IconPixels.h"   // §71
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <string>

namespace e2d {

namespace {

std::string g_lastGlfwError;   // the last thing GLFW said: for the message when the window cannot open

void glfwErrorCallback(int code, const char* description)
{
    std::fprintf(stderr, "[GLFW] error %d: %s\n", code, description);
    g_lastGlfwError = description ? description : "";
}

// What the graphics of this computer offer, when the 4.5 context was refused:
// a small hidden window with whatever OpenGL the driver gives, asked its
// version and its card. "3.1.0 - Build 9.17.10.4459 (Intel(R) HD Graphics
// 3000)"; "" when even that fails.
std::string graphicsOffered()
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* probe = glfwCreateWindow(64, 64, "probe", nullptr, nullptr);
    if (!probe)
        return {};
    glfwMakeContextCurrent(probe);
    using GetString = const GLubyte*(GLAD_API_PTR*)(GLenum);
    std::string text;
    if (const auto getString = reinterpret_cast<GetString>(glfwGetProcAddress("glGetString"))) {
        const GLubyte* version  = getString(GL_VERSION);
        const GLubyte* renderer = getString(GL_RENDERER);
        if (version)
            text = reinterpret_cast<const char*>(version);
        if (renderer)
            text += std::string(" (") + reinterpret_cast<const char*>(renderer) + ")";
    }
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(probe);
    return text;
}

// The message for a window that could not open: what is missing, what this
// computer has, what can be done (notions §82).
std::string needsOpenGl(const std::string& offered)
{
    std::string text = "carto2D needs OpenGL 4.5";
    if (offered.empty())
        text += ", and no OpenGL window could be opened" +
                (g_lastGlfwError.empty() ? std::string(".") : " (" + g_lastGlfwError + ").");
    else
        text += ". This computer's graphics offer OpenGL " + offered + ".";
    text += "\n\nA newer driver from the maker of the graphics card (Intel, AMD, NVIDIA) often brings OpenGL 4.5. "
            "render2d, the command line installed beside carto2D, does not need OpenGL: it still makes maps "
            "as PNG and PDF.";
#ifndef _WIN32
    text += "\n\nOn Linux, LIBGL_ALWAYS_SOFTWARE=1 carto2d draws without the graphics card (Mesa's llvmpipe, slower).";
#endif
    return text;
}

} // namespace

Window::~Window()
{
    shutdown();
}

bool Window::init(int width, int height, const char* title)
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        m_failure = "The window system could not be started (" + g_lastGlfwError + ").";
#ifndef _WIN32
        m_failure += " Is there a display (DISPLAY or WAYLAND_DISPLAY)?";
#endif
        return false;
    }

    // Hints must be set before the window is created.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
    // On Windows, sizes are in screen points: without this, 1600 x 900 would
    // be a small window on a 4K screen at 150 %. With it: 2400 x 1350.
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    m_handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_handle) {
        m_failure = needsOpenGl(graphicsOffered());   // §82: an old card or driver says so
        glfwTerminate();
        return false;
    }

#ifndef _WIN32
    // §71: the window's icon. On Windows it comes from the executable's
    // resource (GLFW_ICON); elsewhere it is given as pixels. (Wayland ignores it.)
    GLFWimage icon{kIconSize, kIconSize, const_cast<unsigned char*>(kIconPixels)};
    glfwSetWindowIcon(m_handle, 1, &icon);
#endif

    // A current context is required before loading OpenGL functions.
    glfwMakeContextCurrent(m_handle);

    const int version = gladLoadGL(glfwGetProcAddress);
    if (version == 0) {
        std::fprintf(stderr, "Failed to load OpenGL functions\n");
        m_failure = needsOpenGl({});
        shutdown();
        return false;
    }
    std::printf("OpenGL %d.%d loaded\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    glfwSwapInterval(1); // vsync

    // The static callbacks find this object through the window's user pointer.
    // ImGui is initialised AFTER this and chains to our callbacks.
    glfwSetWindowUserPointer(m_handle, this);
    glfwSetScrollCallback(m_handle, scrollCallback);
    glfwSetMouseButtonCallback(m_handle, mouseButtonCallback);
    glfwSetDropCallback(m_handle, dropCallback);   // §39: files dropped on the window
    // §56: any other input only counts, to know there is something to draw.
    glfwSetCursorPosCallback(m_handle, [](GLFWwindow* w, double, double) { touch(w); });
    glfwSetCursorEnterCallback(m_handle, [](GLFWwindow* w, int) { touch(w); });
    glfwSetKeyCallback(m_handle, [](GLFWwindow* w, int, int, int, int) { touch(w); });
    glfwSetCharCallback(m_handle, [](GLFWwindow* w, unsigned int) { touch(w); });
    glfwSetWindowFocusCallback(m_handle, [](GLFWwindow* w, int) { touch(w); });
    glfwSetFramebufferSizeCallback(m_handle, [](GLFWwindow* w, int, int) { touch(w); });
    glfwSetWindowRefreshCallback(m_handle, [](GLFWwindow* w) { touch(w); });
    return true;
}

void Window::shutdown()
{
    if (!m_handle)
        return;

    glfwDestroyWindow(m_handle);
    m_handle = nullptr;
    glfwTerminate();
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_handle);
}

void Window::setShouldClose(bool close)
{
    glfwSetWindowShouldClose(m_handle, close ? GLFW_TRUE : GLFW_FALSE);
}

void Window::setTitle(const std::string& title)
{
    glfwSetWindowTitle(m_handle, title.c_str());
}

void Window::waitEvents(double seconds)
{
    glfwWaitEventsTimeout(seconds);
}

void Window::ring()
{
    glfwPostEmptyEvent();   // thread-safe (GLFW documentation)
}

void Window::pollEvents()
{
    glfwPollEvents();
}

void Window::swapBuffers()
{
    glfwSwapBuffers(m_handle);
}

void Window::framebufferSize(int& width, int& height) const
{
    glfwGetFramebufferSize(m_handle, &width, &height);
}

void Window::windowSize(int& width, int& height) const
{
    glfwGetWindowSize(m_handle, &width, &height);
}

float Window::contentScale() const
{
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(m_handle, &sx, &sy);
    return sx; // square pixels: x and y are the same
}

void Window::touch(GLFWwindow* window)
{
    if (auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window)))
        self->m_input = true;
}

bool Window::takeInput()
{
    const bool had = m_input;
    m_input = false;
    return had;
}

void Window::scrollCallback(GLFWwindow* window, double /*xOffset*/, double yOffset)
{
    touch(window);
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self)
        self->m_scrollAccumulator += static_cast<float>(yOffset);
}

void Window::mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/)
{
    touch(window);
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self)
        return;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS)
        self->m_middlePressed = true;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    if (action == GLFW_PRESS)
        self->m_leftPressed = true;
    else if (action == GLFW_RELEASE)
        self->m_leftReleased = true;
}

void Window::dropCallback(GLFWwindow* window, int count, const char** paths)
{
    touch(window);
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self)
        return;
    for (int i = 0; i < count; ++i)
        self->m_drops.emplace_back(paths[i]);
}

std::vector<std::string> Window::takeDrops()
{
    std::vector<std::string> out;
    out.swap(m_drops);
    return out;
}

MouseState Window::consumeMouse()
{
    MouseState m;
    glfwGetCursorPos(m_handle, &m.x, &m.y);
    m.left   = glfwGetMouseButton(m_handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    m.middle = glfwGetMouseButton(m_handle, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    m.right  = glfwGetMouseButton(m_handle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    m.scroll = m_scrollAccumulator;
    m_scrollAccumulator = 0.0f;
    m.leftPressed  = m_leftPressed;
    m.leftReleased = m_leftReleased;
    m.middlePressed = m_middlePressed;
    m_leftPressed  = false;
    m_leftReleased = false;
    m_middlePressed = false;
    return m;
}

double Window::time() const
{
    return glfwGetTime();
}

} // namespace e2d
