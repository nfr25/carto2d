#include "panels/Workspace.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder, BeginViewportSideBar

namespace e2d {

namespace {

// Frames to wait, after building the default layout, before showing the
// History and Report tabs: the windows must exist first (they are created
// by their first Begin, later in the frame).
int s_selectTabsIn = 0;
bool s_reset = false;   // §61: View > Reset layout

// One fixed id, the same at every run: imgui.ini finds its layout again.
ImGuiID dockspaceId()
{
    return ImHashStr("editor2d.dockspace");
}

// The first layout (notions §19), as ratios of the window:
//
//   ┌────────┬──────────────────────────────┐
//   │ Layers │                              │
//   │        │          the map             │
//   ├────────┤      (central zone)          │
//   │Selection│                             │
//   │History  ├──────────────────────────────┤
//   │        │  Report | Events | Table     │
//   └────────┴──────────────────────────────┘
void buildDefaultLayout(ImGuiID id, const ImGuiViewport* viewport)
{
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);   // before splitting

    ImGuiID left = 0, center = 0, leftBottom = 0, bottom = 0;
    ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.22f, &left, &center);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, &leftBottom, &left);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, &bottom, &center);

    ImGui::DockBuilderDockWindow("Layers", left);
    ImGui::DockBuilderDockWindow("History", leftBottom);
    ImGui::DockBuilderDockWindow("Selection", leftBottom);
    ImGui::DockBuilderDockWindow("Report", bottom);
    ImGui::DockBuilderDockWindow("Events", bottom);
    ImGui::DockBuilderDockWindow("Table", bottom);   // §60
    ImGui::DockBuilderFinish(id);
}

} // namespace

void resetLayout()
{
    s_reset = true;
}

void drawStatusBar(const std::string& text)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    // A "side bar" takes its height off the work area: the dockspace below
    // will not cover it.
    if (ImGui::BeginViewportSideBar("##status", viewport, ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::TextUnformatted(text.c_str());
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

MapArea dockspace()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID id = dockspaceId();
    if (s_reset) {   // §61: forget the layout; it is built again just below
        s_reset = false;
        ImGui::DockBuilderRemoveNode(id);
    }
    if (ImGui::DockBuilderGetNode(id) == nullptr) {   // no imgui.ini yet: the default layout
        buildDefaultLayout(id, viewport);
        s_selectTabsIn = 2;
    }
    // In a group of tabs, the one shown is the window focused last.
    if (s_selectTabsIn > 0 && --s_selectTabsIn == 0) {
        ImGui::SetWindowFocus("Report");
        ImGui::SetWindowFocus("Selection");
    }

    // PassthruCentralNode: the central zone has no background and takes no
    // mouse: the map drawn behind shows through, and gets the clicks (§19).
    ImGui::DockSpaceOverViewport(id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    MapArea area;
    if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(id)) {
        area.x      = central->Pos.x;
        area.y      = central->Pos.y;
        area.width  = central->Size.x;
        area.height = central->Size.y;
    }
    return area;
}

} // namespace e2d
