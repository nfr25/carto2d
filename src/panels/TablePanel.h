#pragma once

#include "command/Document.h"
#include "panels/SelectionPanel.h"   // SelectionPanelState: the feature designated
#include "view/SelectionOverlay.h"   // FeatureCache

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace e2d {

// What the table remembers between frames: the layer shown, the order of its
// rows (sorted only when needed, §60), and for which selection it was made.
struct TablePanelState {
    std::string               layer;     // its id; "" = the one with most selected
    std::vector<std::int64_t> rows;      // FIDs, in the order shown
    std::vector<std::int64_t> sortedFor; // the layer's selection when `rows` was made
    int                       sortColumn = 0;     // 0 = fid, 1 + field index
    bool                      ascending  = true;
    bool                      dirty      = true;  // sort again at the next frame
};

// ============================================================================
// The table of the selection (notions §60): one row per selected feature of
// one layer, one column per field, sortable. A click designates the feature
// (its attributes in the Selection panel, drawn stronger on the map); a double
// click asks to zoom on it; a right click on a cell copies its value. Only the
// visible rows are drawn (ImGuiListClipper). Returns the feature to zoom on.
// ============================================================================
std::optional<std::pair<std::string, std::int64_t>>
drawTablePanel(const Document& doc, const FeatureCache& known, SelectionPanelState& selection,
               TablePanelState& state);

} // namespace e2d
