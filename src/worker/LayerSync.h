#pragma once

#include "map/map.hpp"

#include <map>
#include <string>
#include <vector>

namespace e2d {

// ============================================================================
// LayerSync — puts a thread's Map in line with the list of layers its request
// carries (notions §37). Each thread owns its Map: the main thread never
// touches it, it only sends the list, numbered by a revision.
//
//   1. remove what is no longer wanted (or wanted with another source/style)
//   2. open what is missing (warnings if it cannot be opened)
//   3. move into the wanted order, bottom first
//   4. a style changed in the dialog: given to the open layer (§41); back to
//      the file (""): the layer is reopened with its file
//   5. the visibility of each, by id
// Layers that stay are not reopened.
// ============================================================================

struct LayerState {
    std::string id;
    std::string source;
    std::string style;          // "" = render2d's default style
    bool        visible = true;
    int         colour  = 0;    // the default style's colour (§38)
    std::string styleText;      // §41: the style changed in the dialog; "" = the file's
    std::string crs;            // §69: its system declared in the project; "" = the file's
    std::string attribution;    // §76: attribution: in the project; "" = the source's own (OSM's)
    std::string title;          // §77: the project's title, for a page's legend; "" = the source's name
    bool        legend = true;  // §77: legend: false in the project
};

class LayerSync {
public:
    // `revision`: the Document's; the same as last time = only the visibility.
    // `crs`: the map's system decided by the main thread (§69, a WKT or
    // "EPSG:3857"); "" = not decided yet, the Map takes its first layer's.
    void apply(r2d::Map& map, long revision, const std::string& crs, const std::vector<LayerState>& wanted);

    // The warnings of the layers wanted now (a missing file, a bad style,
    // another CRS): those of a layer removed go with it.
    std::vector<std::string> warnings() const;

    // The id of each layer of the Map, in its order: the answers of the
    // threads speak of map indices, the main thread of ids.
    std::vector<std::string> ids() const;

private:
    std::vector<LayerState> m_current;   // what the Map holds, in its order
    long                    m_revision = -1;
    std::string             m_crs;                // §69: the system given to the Map last
    std::vector<std::string> m_crsWarnings;       // what that change could not do
    std::map<std::string, std::vector<std::string>> m_warnings;        // per layer id, when opened
    std::map<std::string, std::vector<std::string>> m_styleWarnings;   // per layer id, of its style text
};

} // namespace e2d
