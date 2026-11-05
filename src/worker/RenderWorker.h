#pragma once

#include "map/map.hpp"
#include "map/page.hpp"
#include "worker/LayerSync.h"

#include <map>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace e2d {

// ============================================================================
// RenderWorker — the render thread (notions §11).
//
// It OWNS the map: the GDAL layers, the styles, the Cairo image never leave
// this thread. The main thread only talks to it through two one-place
// mailboxes, with copies:
//
//   main ── request(#8) ──► [ one place ] ──► thread: Map::render
//                            (#8 replaces #7 if #7 was not taken yet)
//   main ◄── takeResult() ◄── [ one place ] ◄── pixels + View + report
//
// Between two layers, the thread asks "is a newer request waiting?"; if so
// it drops the view nobody looks at any more (Map::render's `stop`).
// ============================================================================

struct RenderRequest {
    long              id = 0;          // numbered: older results are ignored
    r2d::View         view;            // the WANTED view, at the screen's size
    double            pixelRatio = 1.0;
    std::vector<LayerState> layers;    // the Document's list, a copy (§37)
    long              revision = 0;    // the Document's revision (§37)
    std::string       crs;             // §69: the map's system; "" = the first layer's
};

struct RenderResult {
    long                       id = 0;
    r2d::View                  view;       // the view these pixels were drawn for
    int                        width = 0, height = 0, stride = 0;
    std::vector<unsigned char> pixels;     // copied out of Cairo: B, G, R, A rows
    std::vector<std::string>   reportLines;
    std::vector<std::string>   warnings;   // the layers opened so far, then this render's
    std::map<std::string, bool> outOfScale; // per layer id: visible, but no rule at this scale (§18)
    double                     renderMs = 0.0;
};

// §77: File > Print: a page written by render2d's composePage, in this
// thread, with the map it holds. Its own mailbox: a view asked meanwhile
// does not replace it.
struct PrintRequest {
    r2d::PageSpec           page;       // centre and scale filled in
    std::string             path;       // the PDF (or SVG) to write
    std::vector<LayerState> layers;     // as for a view
    long                    revision = 0;
    std::string             crs;
};

struct PrintResult {
    bool                     ok = false;
    std::string              path;
    std::string              error;
    std::vector<std::string> lines;      // the page's line, the map's report, the legend
    std::vector<std::string> warnings;
};

class RenderWorker {
public:
    RenderWorker() = default;
    ~RenderWorker();

    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    // Starts the thread with an empty map: the layers come with the requests
    // (§37). `pixelRatio`: the screen's, the styles are scaled once for it.
    // `ring`: called by the thread when a result is posted, to wake the main
    // thread asleep in waitEvents (§56: the dining room's bell).
    void start(double pixelRatio, std::function<void()> ring = {});

    // Stops between two layers at most, and waits for the thread.
    void stop();

    // Puts the request in the mailbox, replacing one not yet taken.
    void request(RenderRequest r);

    // The newest finished result, if any (each result is handed out once).
    std::optional<RenderResult> takeResult();

    bool busy() const;   // a request is waiting or being drawn

    // §77: a page to write; its result once, as for the views.
    void print(PrintRequest r);
    std::optional<PrintResult> takePrintResult();

private:
    void loop();
    RenderResult render(const RenderRequest& r);
    PrintResult  printPage(const PrintRequest& r);

    std::unique_ptr<r2d::Map> m_map;   // used by the thread only, once started
    LayerSync                 m_sync;  // what m_map holds (§37)
    std::thread               m_thread;

    mutable std::mutex          m_mutex;   // protects everything below
    std::condition_variable     m_wake;
    std::optional<RenderRequest> m_pending;
    std::optional<RenderResult>  m_result;
    std::optional<PrintRequest>  m_printPending;   // §77
    std::optional<PrintResult>   m_printResult;
    bool                         m_drawing = false;
    std::function<void()>        m_ring;
    bool                         m_quit    = false;
};

} // namespace e2d
