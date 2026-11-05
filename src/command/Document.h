#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace e2d {

// What the commands act on: the state of the editor that can be undone.
// The layers and their visibility (step 1c), the selection (step 2).
// The map itself (GDAL, styles) is not here: it lives in the render thread,
// which receives copies of this state with each request (notions §11).

// What a layer's bounding box is, without render2d: xmin, ymin, xmax, ymax.
using Extent = std::array<double, 4>;

struct LayerEntry {
    LayerEntry() = default;
    LayerEntry(std::string id_, std::string geometry_, bool visible_)
        : id(std::move(id_)), geometry(std::move(geometry_)), visible(visible_) {}

    std::string id;          // the GDAL name, made unique ("name#2"), never changed: notions §16, §38
    std::string geometry;    // "point", "line", "polygon", "raster" (§66) or "" (mixed, unknown)
    bool        visible = true;

    // Step 3a (§37, §38): what the threads need to open it, and what we know of it.
    std::string              source;       // "file.fgb", "file.gpkg::layer", "PG:..."
    std::string              style;        // YSLD file; "" = render2d's default style
    std::string              styleText;    // §41: the style as changed in the dialog (YSLD); "" = the file's
    int                      colour = 0;   // the default style's colour, given once (§38)
    std::string              title;        // from the project file; written back as it was (§52)
    std::vector<std::string> fields;       // its columns, for the Selection panel
    std::vector<bool>        numeric;      // per field: a number (integer, real), for rotation and size (§46)
    std::optional<Extent>    extent;       // for "Zoom to layer" (§39), in the LAYER's system
    std::string              crs;          // §69: its system (WKT, as GDAL gives it); "" = none declared
    std::string              crsDeclared;  // §69: crs: in the project ("EPSG:31300"): wins over `crs`
    int                      otherLayers = 0;   // more layers in its file (a GeoPackage): said once
    bool                     web = false;       // §76: a web basemap (WMS, WMTS, tiles): not in "zoom to all"
    std::string              attribution;       // §76: attribution: in the project, written back as it was
    bool                     legend = true;     // render2d §74: legend: false in the project, written back
};

// What the main thread learns by opening a source once (§38: the probe).
struct ProbedLayer {
    std::string              name;          // the GDAL layer name: the id, made unique
    std::string              geometry;      // as LayerEntry::geometry
    std::vector<std::string> fields;
    std::vector<bool>        numeric;       // as LayerEntry::numeric
    std::optional<Extent>    extent;
    std::string              crs;           // as LayerEntry::crs (§69)
    int                      otherLayers = 0;   // a GeoPackage with more: said in the status bar
    bool                     web = false;       // as LayerEntry::web (§76)
};

// Opens `source`, or returns nothing with the reason in `error`. Given to the
// Commander by the application (GDAL), by the tests (a fake): e2d_command
// does not know GDAL.
using LayerProbe = std::function<std::optional<ProbedLayer>(const std::string& source, std::string& error)>;

// Checks a YSLD text for layer.setStyle (§41): render2d's reader in the
// application, nothing in tests. False with the reason: refused whole.
using StyleCheck = std::function<bool(const std::string& ysld, std::string& error)>;

// The selection (notions §22): for each layer id, the FIDs selected. The
// flag of the SQLite engine, kept next to the data instead of in it.
using Selection = std::map<std::string, std::set<std::int64_t>>;

class Document {
public:
    std::vector<LayerEntry> layers;   // in DRAWING order: the first is at the bottom
    Selection               selection;
    long                    revision = 0;     // + 1 at each add, remove, move (§37)
    int                     nextColour = 0;   // the next default colour (§38)

    std::size_t selectionCount() const;

    // The index of the layer with this id, or -1.
    int findLayer(const std::string& id) const;
};

// Ids from GDAL names: the first "batiments" keeps its name, the next ones
// become "batiments#2", "batiments#3"... (notions §16).
std::vector<std::string> uniqueIds(const std::vector<std::string>& names);

// `name`, or "name#2", "name#3"... the first not used in `doc` (§38).
std::string uniqueId(const Document& doc, const std::string& name);

} // namespace e2d
