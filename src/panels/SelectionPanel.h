#pragma once

#include "command/Commander.h"
#include "command/Document.h"
#include "view/SelectionOverlay.h"

#include <cstdint>
#include <string>
#include <vector>

namespace e2d {

// The feature whose attributes are shown (and the last copy, §26).
struct SelectionPanelState {
    std::string  layer;
    std::int64_t fid = -1;
    // The last value copied by a right click (§26), for the status bar.
    std::string  copiedField;
    double       copiedAt = -1.0;   // ImGui::GetTime() of the copy

    // §58: selecting by attributes. The panel fills the request; the
    // application sends it to the thread for questions and writes the result.
    std::string whereLayer;          // the layer's id
    std::string whereFollowed;       // §64: the layer last designated, followed once
    std::string whereText;           // AREA > 500
    std::string whereError;          // the parser's, while typing
    int         whereMode = 0;       // 0 new selection, 1 add, 2 remove
    bool        whereAsked = false;  // Select clicked: for the application
    bool        whereBusy  = false;  // asked, not answered yet
    std::string whereResult;         // "1 284 features in 410 ms", or why not
};

// The selection (notions §24). At the top, selecting by attributes (§58):
// a layer, an expression checked as it is typed, a mode, Select. Then the
// count, a Clear button (a command), the
// features grouped by layer, and the attributes of the one clicked in the list.
// A value: its whole text in a tooltip, copied by a right click (§26).
// The field names come from each layer's entry. Returns true when "Zoom to
// selection" was clicked (§28, §39).
bool drawSelectionPanel(const Document& doc, const FeatureCache& known, Commander& commander,
                        SelectionPanelState& state);

} // namespace e2d
