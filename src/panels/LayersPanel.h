#pragma once

#include "command/Commander.h"
#include "command/Document.h"
#include "panels/LayerSymbol.h"

#include <map>
#include <set>
#include <string>

namespace e2d {

class SymbolTextures;

// What the panel asks the application to do.
struct LayersPanelResult {
    std::string error;    // a refused command, or ""
    std::string zoomTo;   // "Zoom to layer" chosen: its id
    std::string styleOf;  // "Style..." chosen: its id (§42)
};

// The layers (notions §18, §39): the top layer at the top of the list.
//   * a checkbox that MAKES A COMMAND (layer.setVisible)
//   * the name: dragged onto another row, a layer.move; right click: "Zoom
//     to layer", "Remove" (layer.remove); in grey when the last render found
//     no active rule for it ("out of scale" in the tooltip)
//   * in front of the name, its symbol in the style's colours (§68), read
//     through `symbols` at `scaleDenominator`; `pixelRatio` as for the map
//   * the geometry type, in a column of fixed width: never cut by a long name
// `outOfScale`: per layer id (may miss a layer: unknown yet).
// `unsaved`: the layers whose style was changed and not saved: a "*" after the name (§45).
LayersPanelResult drawLayersPanel(const Document& doc, const std::map<std::string, bool>& outOfScale,
                                  const std::set<std::string>& unsaved, LayerSymbols& symbols,
                                  double scaleDenominator, float pixelRatio, Commander& commander,
                                  SymbolTextures* textures = nullptr);   // §78: image symbols

} // namespace e2d
