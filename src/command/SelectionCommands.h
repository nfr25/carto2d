#pragma once

#include "command/Registry.h"

namespace e2d {

// selection.set (notions §22):
//   {"name": "selection.set", "mode": "replace",
//    "items": [{"layer": "UrbISBuildings_04000_Buildings", "fid": 1823}]}
// mode: "replace" (a click), "add" (Shift), "toggle" (Ctrl), "clear" (items ignored).
void registerSelectionCommands(Registry& registry);

// The selection as the JSON of events and commands: [{"layer": ..., "fid": ...}, ...].
nlohmann::json selectionItems(const Selection& selection);

} // namespace e2d
