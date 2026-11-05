#include "worker/QueryWorker.h"

#include <algorithm>
#include <chrono>

namespace e2d {

QueryWorker::~QueryWorker()
{
    stop();
}

void QueryWorker::start(double pixelRatio, std::function<void()> ring)
{
    m_ring = std::move(ring);
    m_map    = std::make_unique<r2d::Map>(r2d::Map::open({}, pixelRatio));
    m_thread = std::thread(&QueryWorker::loop, this);
}

void QueryWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_one();
    if (m_thread.joinable())
        m_thread.join();
}

void QueryWorker::ask(QueryRequest q)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = std::move(q);
    }
    m_wake.notify_one();
}

std::optional<QueryAnswer> QueryWorker::takeAnswer()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::optional<QueryAnswer> a = std::move(m_answer);
    m_answer.reset();
    return a;
}

void QueryWorker::loop()
{
    for (;;) {
        QueryRequest q;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || m_pending.has_value(); });
            if (m_quit)
                return;
            q = std::move(*m_pending);
            m_pending.reset();
        }

        // The same state as the render: pixel ratio (the styles' sizes) and
        // the visible layers.
        m_map->setPixelRatio(q.pixelRatio);
        m_sync.apply(*m_map, q.revision, q.crs, q.layers);   // §37

        const auto t0 = std::chrono::steady_clock::now();
        QueryAnswer a;
        a.id   = q.id;
        a.mode = q.mode;
        a.layerIds = m_sync.ids();
        if (!q.where.empty()) {   // §58: the whole layer, whatever the scale
            const std::vector<std::string> ids = m_sync.ids();
            const auto it = std::find(ids.begin(), ids.end(), q.whereLayer);
            a.where = a.zone = true;
            if (it == ids.end()) {
                a.error = "the layer " + q.whereLayer + " could not be opened";
            } else {
                r2d::AreaQuery z = m_map->queryWhere(static_cast<int>(it - ids.begin()), q.where, q.maxHits);
                a.hits      = std::move(z.hits);
                a.truncated = z.truncated;
                a.matched   = z.matched;
                a.error     = std::move(z.error);
            }
        } else if (q.ring.empty()) {
            a.hits = m_map->queryAt(q.view, q.pixel, q.tolerancePx);
        } else {   // §30: a zone, with the cap
            r2d::AreaQuery z = m_map->queryArea(q.view, q.ring, q.maxHits);
            a.zone      = true;
            a.hits      = std::move(z.hits);
            a.truncated = z.truncated;
            a.error     = std::move(z.error);
        }
        a.ms   = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_answer = std::move(a);
        }
        if (m_ring)
            m_ring();   // §56: the main thread may be asleep
    }
}

} // namespace e2d
