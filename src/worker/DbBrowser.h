#pragma once

#include "backend/gdal/gdal_layer.hpp"   // render2d: LayerInfo, listLayers

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace e2d {

// What a database answered (§65).
struct DbListing {
    std::string                 source;   // the connection string asked
    std::vector<r2d::LayerInfo> tables;   // with a geometry column
    std::string                 error;    // GDAL's (libpq's) message, or empty
    double                      ms = 0.0;
};

// Asks a database for its tables in a thread of its own: a slow or silent
// server never freezes the window (§65). One question at a time; the answer
// is taken by the main thread, woken by `ring` (§56).
class DbBrowser {
public:
    explicit DbBrowser(std::function<void()> ring);
    ~DbBrowser();   // waits for a question under way (at most connect_timeout)

    DbBrowser(const DbBrowser&) = delete;
    DbBrowser& operator=(const DbBrowser&) = delete;

    void list(const std::string& source);   // ignored while busy
    bool busy() const { return m_busy; }
    std::optional<DbListing> take();         // the answer, once

private:
    std::function<void()>    m_ring;
    std::thread              m_thread;
    std::atomic<bool>        m_busy{false};
    std::mutex               m_mutex;
    std::optional<DbListing> m_answer;   // guarded by m_mutex
};

} // namespace e2d
