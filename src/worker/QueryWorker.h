#pragma once

#include "map/map.hpp"
#include "worker/LayerSync.h"

#include <condition_variable>
#include <functional>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace e2d {

// ============================================================================
// QueryWorker — the thread for questions (notions §21).
//
// Its own Map: the same sources opened a second time, so that a click never
// waits behind a render of 1.5 s. The same one-place mailboxes as the render
// thread, numbered questions, copies only.
// ============================================================================

struct QueryRequest {
    long              id = 0;
    r2d::View         view;            // the WANTED view, the one the mouse shows
    r2d::Point        pixel;           // a click: in that view's image pixels
    double            tolerancePx = 4.0;
    std::vector<r2d::Point> ring;      // a zone (§30), terrain coordinates: then `pixel` is unused
    std::size_t       maxHits = 0;     // a zone: the cap (0 = none)
    double            pixelRatio = 1.0;
    std::vector<LayerState> layers;    // the Document's list, a copy (§37): only what one sees
    long              revision = 0;
    std::string       crs;             // §69: the map's system, as for the render
    std::string       mode;            // what to do with the answer: replace, add, remove, toggle
    std::string       whereLayer;      // §58: a selection by attributes on this layer (its id)...
    std::string       where;           // ...with this expression; then view, pixel and ring are unused
};

struct QueryAnswer {
    long                       id = 0;
    std::string                mode;
    std::vector<r2d::QueryHit> hits;   // top first (§52)
    std::vector<std::string>   layerIds;   // hit.layer -> its id (the Map's order when asked)
    bool                       zone = false;        // the answer to a zone
    bool                       truncated = false;   // a zone: more than maxHits
    std::string                error;               // a zone refused by render2d
    bool                       where = false;       // §58: the answer to an expression
    std::size_t                matched = 0;         // §58: how many matched in all (beyond the cap too)
    double                     ms = 0.0;
};

class QueryWorker {
public:
    QueryWorker() = default;
    ~QueryWorker();

    QueryWorker(const QueryWorker&) = delete;
    QueryWorker& operator=(const QueryWorker&) = delete;

    // An empty map: the layers come with the questions (§37). `ring`: as RenderWorker's (§56).
    void start(double pixelRatio, std::function<void()> ring = {});
    void stop();

    void ask(QueryRequest q);                    // replaces a question not yet taken
    std::optional<QueryAnswer> takeAnswer();     // each answer handed out once

private:
    void loop();

    std::unique_ptr<r2d::Map> m_map;   // used by the thread only, once started
    LayerSync                 m_sync;
    std::thread               m_thread;

    std::mutex                  m_mutex;
    std::condition_variable     m_wake;
    std::optional<QueryRequest> m_pending;
    std::function<void()>       m_ring;
    std::optional<QueryAnswer>  m_answer;
    bool                        m_quit = false;
};

} // namespace e2d
