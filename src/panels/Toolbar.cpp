#include "panels/Toolbar.h"

#include "panels/Icons.h"

#include <imgui.h>
#include <imgui_internal.h>   // BeginViewportSideBar

namespace e2d {

namespace {

void menus(const ToolbarState& s, ToolbarResult& r)
{
    // §53: the shortcuts are handled by the application, shown here.
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N"))
            r.file = FileAction::New;
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            r.file = FileAction::Open;
        if (ImGui::MenuItem("Save", "Ctrl+S"))
            r.file = FileAction::Save;
        if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S"))
            r.file = FileAction::SaveAs;
        ImGui::Separator();
        if (ImGui::MenuItem("Print...", "Ctrl+P", false, s.hasLayers))   // §77
            r.file = FileAction::Print;
        ImGui::Separator();
        if (ImGui::MenuItem("Connect to database..."))   // §65
            r.file = FileAction::Connect;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q"))
            r.file = FileAction::Quit;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, s.canUndo))
            r.undo = true;
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, s.canRedo))
            r.redo = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Selection")) {
        if (ImGui::MenuItem("Pointer", "Esc", s.tool == ToolKind::Pointer))
            r.tool = ToolKind::Pointer;
        if (ImGui::MenuItem("Rectangle", "R", s.tool == ToolKind::Rectangle))
            r.tool = ToolKind::Rectangle;
        if (ImGui::MenuItem("Polygon", "P", s.tool == ToolKind::Polygon))
            r.tool = ToolKind::Polygon;
        ImGui::Separator();
        if (ImGui::MenuItem("Select by attributes...", nullptr, false, s.hasLayers))
            r.selectByAttributes = true;
        if (ImGui::MenuItem("Zoom to selection", nullptr, false, s.hasSelection))
            r.zoomSelection = true;
        if (ImGui::MenuItem("Clear selection", nullptr, false, s.hasSelection))
            r.clearSelection = true;
        ImGui::EndMenu();
    }
    // §72: measuring on the ground.
    if (ImGui::BeginMenu("Measure")) {
        if (ImGui::MenuItem("Distance", "D", s.tool == ToolKind::MeasureLine))
            r.tool = ToolKind::MeasureLine;
        if (ImGui::MenuItem("Area", "A", s.tool == ToolKind::MeasureArea))
            r.tool = ToolKind::MeasureArea;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Zoom window", "W", s.tool == ToolKind::ZoomWindow))
            r.tool = ToolKind::ZoomWindow;
        if (ImGui::MenuItem("Full extent", nullptr, false, s.hasLayers))
            r.fullExtent = true;
        // §9: the keys work when the map has the focus (the last click was on it).
        if (ImGui::MenuItem("Zoom in", "+", false, s.hasLayers))
            r.zoom = 2.0;
        if (ImGui::MenuItem("Zoom out", "-", false, s.hasLayers))
            r.zoom = 0.5;
        ImGui::MenuItem("Pan", "arrows", false, false);
        ImGui::SetItemTooltip("the arrow keys move the map by a third of its size\n"
                              "(click on the map first, so that it has the keyboard)");
        // §69: the map's system. Layers in another are converted as they are read.
        if (ImGui::BeginMenu("Map system")) {
            struct Choice { const char* label; int epsg; };
            static const Choice choices[] = {
                {"Belgian Lambert 72", 31370},
                {"Belgian Lambert 2008", 3812},
                {"Web Mercator (OpenStreetMap)", 3857},
                {"UTM zone 31N", 32631},
            };
            for (const Choice& c : choices) {
                const std::string code = "EPSG:" + std::to_string(c.epsg);
                if (ImGui::MenuItem(c.label, code.c_str(), s.crsEpsg == c.epsg))
                    r.crs = code;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Other..."))   // §70: every system PROJ knows
                r.crsDialog = true;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // A panel lost behind others, or floating somewhere: brought back to the front.
        if (ImGui::BeginMenu("Panels")) {
            for (const char* name : {"Layers", "Selection", "History", "Report", "Events", "Table"})
                if (ImGui::MenuItem(name))
                    r.showPanel = name;
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset layout"))
            r.resetLayout = true;
        ImGui::SetItemTooltip("the panels back in their first places\n"
                              "(to move one: drag its tab; Shift while dragging: no docking)");
        ImGui::EndMenu();
    }
    // §71: the notes, and what was the Build panel.
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("carto2D notes"))
            r.notes = true;
        if (ImGui::MenuItem("render2d notes (online)"))
            r.render2dNotes = true;
        ImGui::Separator();
        if (ImGui::MenuItem("About carto2D..."))
            r.about = true;
        ImGui::EndMenu();
    }
}

void icons(const ToolbarState& s, ToolbarResult& r)
{
    auto gap = [] {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    };
    if (iconButton("##undo", Icon::Undo, false, s.canUndo, "Undo (Ctrl+Z)"))
        r.undo = true;
    ImGui::SameLine();
    if (iconButton("##redo", Icon::Redo, false, s.canRedo, "Redo (Ctrl+Y)"))
        r.redo = true;
    gap();
    if (iconButton("##pointer", Icon::Pointer, s.tool == ToolKind::Pointer, true, "Pointer: click a feature (Esc)"))
        r.tool = ToolKind::Pointer;
    ImGui::SameLine();
    if (iconButton("##rect", Icon::Rectangle, s.tool == ToolKind::Rectangle, true, "Rectangle selection (R)"))
        r.tool = ToolKind::Rectangle;
    ImGui::SameLine();
    if (iconButton("##poly", Icon::Polygon, s.tool == ToolKind::Polygon, true, "Polygon selection (P)"))
        r.tool = ToolKind::Polygon;
    gap();
    if (iconButton("##measureline", Icon::MeasureLine, s.tool == ToolKind::MeasureLine, true,
                   "Distance: click points, double click to finish (D)"))
        r.tool = ToolKind::MeasureLine;
    ImGui::SameLine();
    if (iconButton("##measurearea", Icon::MeasureArea, s.tool == ToolKind::MeasureArea, true,
                   "Area: click vertices, the first one again to close (A)"))
        r.tool = ToolKind::MeasureArea;
    gap();
    if (iconButton("##zoomwin", Icon::ZoomWindow, s.tool == ToolKind::ZoomWindow, true, "Zoom window (W)"))
        r.tool = ToolKind::ZoomWindow;
    ImGui::SameLine();
    if (iconButton("##zoomsel", Icon::ZoomSelection, false, s.hasSelection, "Zoom to selection"))
        r.zoomSelection = true;
    ImGui::SameLine();
    if (iconButton("##full", Icon::FullExtent, false, s.hasLayers, "Full extent: every layer"))
        r.fullExtent = true;
}

} // namespace

ToolbarResult drawToolbar(const ToolbarState& state)
{
    ToolbarResult result;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    // Side bars at the top: like the status bar (§19), they take their
    // height off the room the dockspace will have. First the menus...
    if (ImGui::BeginViewportSideBar("##menus", viewport, ImGuiDir_Up, ImGui::GetFrameHeight(),
                                    flags | ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            menus(state, result);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    // ...then the icons, under them (§61).
    const float height = ImGui::GetFrameHeight() * 1.25f + ImGui::GetStyle().WindowPadding.y * 2.0f;
    if (ImGui::BeginViewportSideBar("##icons", viewport, ImGuiDir_Up, height, flags))
        icons(state, result);
    ImGui::End();
    return result;
}

} // namespace e2d
