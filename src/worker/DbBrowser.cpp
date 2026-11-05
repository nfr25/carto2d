#include "worker/DbBrowser.h"

#include <chrono>

namespace e2d {

DbBrowser::DbBrowser(std::function<void()> ring) : m_ring(std::move(ring)) {}

DbBrowser::~DbBrowser()
{
    if (m_thread.joinable())
        m_thread.join();
}

void DbBrowser::list(const std::string& source)
{
    if (m_busy)
        return;
    if (m_thread.joinable())
        m_thread.join();   // the previous question, finished
    m_busy = true;
    m_thread = std::thread([this, source] {
        const auto t0 = std::chrono::steady_clock::now();
        DbListing answer;
        answer.source = source;
        answer.tables = r2d::listLayers(source, answer.error);
        answer.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_answer = std::move(answer);
        }
        m_busy = false;
        if (m_ring)
            m_ring();   // wake the loop: there is something to show
    });
}

std::optional<DbListing> DbBrowser::take()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::optional<DbListing> out = std::move(m_answer);
    m_answer.reset();
    return out;
}

} // namespace e2d
