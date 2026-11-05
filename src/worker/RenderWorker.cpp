#include "worker/RenderWorker.h"

#include "backend/cairo/cairo_image.hpp"
#include "backend/cairo/cairo_target.hpp"   // §77: the page's PDF

#include <cmath>

#include <chrono>
#include <cstring>

namespace e2d {

RenderWorker::~RenderWorker()
{
    stop();
}

void RenderWorker::start(double pixelRatio, std::function<void()> ring)
{
    m_ring   = std::move(ring);
    m_map    = std::make_unique<r2d::Map>(r2d::Map::open({}, pixelRatio));
    m_thread = std::thread(&RenderWorker::loop, this);
}

void RenderWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_one();
    if (m_thread.joinable())
        m_thread.join();
}

void RenderWorker::request(RenderRequest r)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = std::move(r);   // one place: the newest wins
    }
    m_wake.notify_one();
}

std::optional<RenderResult> RenderWorker::takeResult()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::optional<RenderResult> r = std::move(m_result);
    m_result.reset();
    return r;
}

void RenderWorker::print(PrintRequest r)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_printPending = std::move(r);
    }
    m_wake.notify_one();
}

std::optional<PrintResult> RenderWorker::takePrintResult()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::optional<PrintResult> r = std::move(m_printResult);
    m_printResult.reset();
    return r;
}

bool RenderWorker::busy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending.has_value() || m_drawing;
}

// The thread: sleeps until there is a request (no busy loop, §11), draws it,
// posts the result, and so on until stop().
void RenderWorker::loop()
{
    for (;;) {
        RenderRequest r;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || m_pending.has_value() || m_printPending.has_value(); });
            if (m_quit)
                return;
            if (m_printPending) {   // §77: a page first, then the views
                PrintRequest p = std::move(*m_printPending);
                m_printPending.reset();
                lock.unlock();
                PrintResult done = printPage(p);
                lock.lock();
                m_printResult = std::move(done);
                lock.unlock();
                if (m_ring)
                    m_ring();
                continue;
            }
            r = std::move(*m_pending);
            m_pending.reset();
            m_drawing = true;
        }

        RenderResult result = render(r);   // the long part, without the lock

        bool posted = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_drawing = false;
            // An abandoned render (a newer request came, or quitting) has no
            // pixels: nothing to post. Otherwise it replaces an untaken result.
            if (!result.pixels.empty()) {
                m_result = std::move(result);
                posted   = true;
            }
        }
        if (posted && m_ring)
            m_ring();   // §56: the main thread may be asleep
    }
}

// §77: the same page as render2d --page, with the map this thread holds.
PrintResult RenderWorker::printPage(const PrintRequest& r)
{
    PrintResult out;
    out.path = r.path;
    m_sync.apply(*m_map, r.revision, r.crs, r.layers);
    std::string error;
    const auto plan = r2d::planPage(*m_map, r.page, std::nullopt, error);
    if (!plan) {
        out.error = error;
        return out;
    }
    auto target = r2d::CairoTarget::create(
        r.path, r2d::ImageSize{static_cast<int>(std::lround(plan->width)), static_cast<int>(std::lround(plan->height))},
        error);
    if (!target) {
        out.error = error;
        return out;
    }
    const r2d::MapReport report = r2d::composePage(*m_map, r.page, *plan, target->canvas());
    if (!target->finish(error)) {
        out.error = error;
        return out;
    }
    char line[200];
    std::snprintf(line, sizeof line, "page: %s, %s, frame %.0f x %.0f mm", r2d::pageFormatText(r.page).c_str(),
                  r2d::scaleText(plan->scale).c_str(), plan->frameW * r2d::kPointMm, plan->frameH * r2d::kPointMm);
    out.lines.push_back(line);
    for (const auto& l : r2d::reportLines(report))
        out.lines.push_back(l);
    if (r.page.legend)
        out.lines.push_back("  legend: " + r2d::legendText(r2d::legendEntries(*m_map, plan->view, r2d::kPixelPerPoint, &report)));
    out.warnings = report.warnings;
    out.ok       = true;
    return out;
}

RenderResult RenderWorker::render(const RenderRequest& r)
{
    RenderResult out;
    out.id   = r.id;
    out.view = r.view;

    std::string error;
    auto image = r2d::CairoImage::create(r.view.size, error);
    if (!image) {
        out.warnings.push_back("cannot create the map image: " + error);
        return out;
    }
    image->canvas().clear(r2d::Color::fromHex(0xFFFFFF));   // §6: opaque background

    m_map->setPixelRatio(r.pixelRatio);
    m_sync.apply(*m_map, r.revision, r.crs, r.layers);   // §37: in line with the Document

    // §11: between two layers, give up if a newer view is waiting.
    auto newerWaiting = [this] {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_quit || m_pending.has_value();
    };

    const auto t0 = std::chrono::steady_clock::now();
    const r2d::MapReport report = m_map->render(image->canvas(), r.view, newerWaiting);
    out.renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (report.stopped)
        return out;   // no pixels: dropped by loop()

    // Copy the pixels out of Cairo (§11: about 1 ms for 4 MB): the image dies here.
    out.width  = r.view.size.width;
    out.height = r.view.size.height;
    out.stride = image->stride();
    const unsigned char* src = image->pixels();
    out.pixels.assign(src, src + static_cast<std::size_t>(out.stride) * out.height);

    // §18: report.layers are the VISIBLE layers, in order: give each its id.
    const std::vector<std::string> ids = m_sync.ids();
    std::size_t drawn = 0;
    for (int i = 0; i < m_map->layerCount() && drawn < report.layers.size(); ++i) {
        if (!m_map->visible(i))
            continue;
        out.outOfScale[ids[static_cast<std::size_t>(i)]] = report.layers[drawn].stats.passes == 0;
        ++drawn;
    }

    out.view        = report.view;   // pixelRatio included
    out.reportLines = r2d::reportLines(report);
    out.warnings    = m_sync.warnings();
    out.warnings.insert(out.warnings.end(), report.warnings.begin(), report.warnings.end());
    return out;
}

} // namespace e2d
