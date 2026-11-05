#include "worker/LayerSync.h"

#include "backend/gdal/gdal_reproject.hpp"   // crsFromText (§69)

#include <algorithm>
#include <iterator>

namespace e2d {

namespace {

// The same layer: same id, and the same source and style (not reopened).
bool same(const LayerState& a, const LayerState& b)
{
    return a.id == b.id && a.source == b.source && a.style == b.style && a.colour == b.colour && a.crs == b.crs &&
           a.attribution == b.attribution && a.title == b.title && a.legend == b.legend;
}

const LayerState* findId(const std::vector<LayerState>& list, const std::string& id)
{
    for (const LayerState& l : list)
        if (l.id == id)
            return &l;
    return nullptr;
}

} // namespace

void LayerSync::apply(r2d::Map& map, long revision, const std::string& crs, const std::vector<LayerState>& wanted)
{
    // §69: the map's system first, so that the layers opened below are bound to it.
    if (!crs.empty() && crs != m_crs) {
        m_crsWarnings.clear();
        std::string error;
        if (const auto info = r2d::crsFromText(crs, error))
            map.setCrs(*info, m_crsWarnings);
        else
            m_crsWarnings.push_back("map system: " + error);
        m_crs = crs;
    }
    if (revision != m_revision) {
        // The warnings of layers no longer wanted go.
        for (auto it = m_warnings.begin(); it != m_warnings.end();)
            it = findId(wanted, it->first) ? std::next(it) : m_warnings.erase(it);
        // 1. Remove, from the top so that the indices below stay right.
        for (int i = static_cast<int>(m_current.size()) - 1; i >= 0; --i) {
            const LayerState& cur = m_current[static_cast<std::size_t>(i)];
            const LayerState* w = findId(wanted, cur.id);
            // Back to the file's style: reopened, the simplest way to read the file again.
            const bool backToFile = w && w->styleText.empty() && !cur.styleText.empty();
            if (!w || !same(*w, cur) || backToFile) {
                map.removeLayer(i);
                m_current.erase(m_current.begin() + i);
            }
        }
        // 2. Open what is missing, on top for now.
        for (const LayerState& w : wanted) {
            if (findId(m_current, w.id))
                continue;
            r2d::LayerSpec spec(w.source);
            if (!w.style.empty())
                spec.style = w.style;
            spec.colour = w.colour;
            spec.crs    = w.crs;   // §69
            spec.attribution = w.attribution;   // §76
            spec.title  = w.title;    // §77: the legend's names
            spec.legend = w.legend;
            std::vector<std::string> said;   // tried again at the next revision if it failed
            if (map.addLayer(spec, -1, said)) {
                LayerState opened = w;
                opened.styleText.clear();   // the file's style, for now: step 4 below
                m_current.push_back(opened);
            }
            m_warnings[w.id] = std::move(said);
            m_styleWarnings.erase(w.id);
        }
        // 3. The wanted order, bottom first: position by position.
        int pos = 0;
        for (const LayerState& w : wanted) {
            const auto it = std::find_if(m_current.begin(), m_current.end(),
                                         [&](const LayerState& c) { return c.id == w.id; });
            if (it == m_current.end())
                continue;   // could not be opened: not in the Map
            const int cur = static_cast<int>(it - m_current.begin());
            if (cur != pos) {
                map.moveLayer(cur, pos);
                LayerState moved = *it;
                m_current.erase(it);
                m_current.insert(m_current.begin() + pos, moved);
            }
            ++pos;
        }
        m_revision = revision;
    }
    // 4. A style changed in the dialog (§41): the layer stays open.
    for (int i = 0; i < static_cast<int>(m_current.size()); ++i) {
        LayerState& cur = m_current[static_cast<std::size_t>(i)];
        const LayerState* w = findId(wanted, cur.id);
        if (!w || w->styleText == cur.styleText || w->styleText.empty())
            continue;
        std::vector<std::string> said;
        map.setLayerStyle(i, w->styleText, said);
        m_styleWarnings[cur.id] = std::move(said);
        cur.styleText = w->styleText;
    }
    // 5. The visibility, at every request (a checkbox does not change the revision).
    for (int i = 0; i < static_cast<int>(m_current.size()); ++i) {
        const LayerState* w = findId(wanted, m_current[static_cast<std::size_t>(i)].id);
        map.setVisible(i, w && w->visible);
    }
}

std::vector<std::string> LayerSync::warnings() const
{
    std::vector<std::string> out = m_crsWarnings;
    for (const auto& [id, list] : m_warnings)
        out.insert(out.end(), list.begin(), list.end());
    for (const auto& [id, list] : m_styleWarnings)
        if (findId(m_current, id))
            out.insert(out.end(), list.begin(), list.end());
    return out;
}

std::vector<std::string> LayerSync::ids() const
{
    std::vector<std::string> out;
    for (const LayerState& l : m_current)
        out.push_back(l.id);
    return out;
}

} // namespace e2d
