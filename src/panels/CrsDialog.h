#pragma once

#include "backend/gdal/gdal_reproject.hpp"   // render2d: CrsInfo, the catalogue

#include <optional>
#include <string>
#include <vector>

namespace e2d {

// One layer, as the dialog shows it (notions §70).
struct CrsLayerRow {
    std::string layer;        // its id
    std::string system;       // "EPSG:4326 (WGS 84)", "none declared"...
    std::string conversion;   // "the map's", or the way PROJ takes, with its accuracy
};

// View > Map system > Other... (notions §70): every projected system PROJ
// knows, searched by words, or only those made for the place the map shows;
// a definition typed or pasted (a code, a WKT, a .prj); and each layer's own
// system, with the way it is converted.
class CrsDialog {
public:
    void open();

    // Every frame. `centre`: the middle of the map, in degrees (nullopt:
    // unknown). Returns the system chosen ("EPSG:5316", a WKT...), or "".
    std::string draw(const r2d::CrsInfo& mapCrs, const std::optional<r2d::Point>& centre,
                     const std::vector<CrsLayerRow>& layers);

    bool isOpen() const { return m_open; }

private:
    void search(const std::optional<r2d::Point>& centre);

    bool        m_open = false;
    std::string m_words;                 // the search field
    bool        m_near = true;           // only the systems made for the map's place
    std::string m_selected;              // a code, clicked in the list
    std::string m_definition;            // typed or pasted
    std::string m_error;                 // why the definition was refused

    std::vector<const r2d::CrsEntry*> m_results;   // the list shown
    std::string                       m_searched;  // what it was computed for
    bool                              m_searchedNear = false;
    std::optional<r2d::Point>         m_searchedAt;
    bool                              m_dirty = true;
};

} // namespace e2d
