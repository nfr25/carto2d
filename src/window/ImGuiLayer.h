#pragma once

struct GLFWwindow;

namespace e2d {

// Initialises Dear ImGui and wraps its per-frame begin/end calls.
// Copied from editor3d (ui/ImGuiLayer); differences in DIVERGENCES.md.
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // `scale`: the window's content scale (1.5 at 150 %). Fonts and spacing
    // are enlarged by it, so that the panels have the size Windows expects.
    bool init(GLFWwindow* window, float scale);
    void shutdown();

    void beginFrame();
    void endFrame();

    // True when ImGui is using the mouse/keyboard (hovering a panel, typing...).
    bool wantsMouse() const;
    bool wantsKeyboard() const;

private:
    bool m_initialized = false;
};

} // namespace e2d
