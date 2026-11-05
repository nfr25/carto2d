#pragma once

#include "view/Tool.h"

#include <optional>
#include <string>

namespace e2d {

// The File menu (§53).
enum class FileAction { None, New, Open, Save, SaveAs, Connect, Print, Quit };   // Connect: §65; Print: §77

// What the menus and the icons asked for, this frame (§53, §61).
struct ToolbarResult {
    std::optional<ToolKind> tool;            // a tool chosen
    FileAction              file = FileAction::None;
    bool undo = false, redo = false;         // Edit
    bool clearSelection = false;             // Selection
    bool zoomSelection  = false;
    bool selectByAttributes = false;         // shows the Selection panel (§58)
    bool fullExtent  = false;                // View
    double zoom = 0.0;                       // View > Zoom in (2) / Zoom out (0.5); 0 = none
    bool resetLayout = false;
    std::string showPanel;                   // View > a panel's name: brought to the front
    std::string crs;                         // §69: View > Map system: "EPSG:3857"...; "" = none chosen
    bool crsDialog = false;                  // §70: View > Map system > Other...
    bool about = false;                      // §71: Help > About editor2d
    bool notes = false;                      // §71: Help > editor2d notes
    bool render2dNotes = false;              // §71: Help > render2d notes (online)
};

// What the menus need to know to grey out what cannot be done.
struct ToolbarState {
    ToolKind tool = ToolKind::Pointer;
    bool     canUndo = false, canRedo = false;
    bool     hasSelection = false;
    bool     hasLayers = false;
    int      crsEpsg = 0;                    // §69: the map's system, ticked in View > Map system
};

// Two thin bands at the top of the window, outside the dockspace, as the
// status bar: the menus (File, Edit, Selection, View), then the toolbar, its
// icons drawn by hand (§61). Call it BEFORE dockspace().
ToolbarResult drawToolbar(const ToolbarState& state);

} // namespace e2d
