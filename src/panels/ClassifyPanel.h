#pragma once

#include "command/Document.h"

#include "style/classify.hpp"   // render2d: summaries, classify() (its §62, §63)
#include "style/style.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace e2d {

// ============================================================================
// ClassifyPanel — the assistants Categorize and Graduate (notions §48 to §50):
// the "Classify" tab of the legend dialog.
//
// A field is chosen; its values are read once (render2d's summarizeField, in a
// thread of its own since 0.9.1, §82: a PostGIS table of 200,000 parcels takes
// seconds, and the window must not freeze); the preview shows the legend to come,
// with the count of each class. Apply replaces the rules of the dialog's copy
// of the style with render2d's classify() — the same recipe as
// `render2d --graduate` (its §63): the classes repeated in each scale band.
// ============================================================================
class ClassifyPanel {
public:
    ClassifyPanel() = default;
    ~ClassifyPanel();   // waits for a read under way
    ClassifyPanel(const ClassifyPanel&) = delete;
    ClassifyPanel& operator=(const ClassifyPanel&) = delete;

    void reset();   // §53: a new project
    bool busy() const { return m_busy; }   // §82: a field being read: frames go on (§56)

    // Draws the tab's content. True when Apply was pressed: `style` was replaced.
    bool draw(const LayerEntry& layer, r2d::Style& style);

private:
    void read(const LayerEntry& layer, const std::string& field);   // starts reading a field, in the thread
    void collect();                                                   // its answer, once
    void drawProgress();                                              // §82: while it reads
    r2d::ClassifySettings settings() const;
    r2d::Symbolizer fallback(const LayerEntry& layer) const;          // no symbol in the style

    std::string m_layer;                         // the summary is of this layer...
    std::string m_field;                         // ...and of this field
    std::optional<r2d::FieldSummary> m_summary;
    std::string m_error;
    double      m_readMs = 0.0;                  // §50: measured, to see if it is too slow

    // §82: the read, in a thread. One at a time: the field list waits meanwhile.
    struct Answer {
        std::string layer, field;
        std::optional<r2d::FieldSummary> summary;
        std::string error;
        double      ms = 0.0;
    };
    std::thread             m_thread;
    std::atomic<bool>       m_busy{false};
    std::atomic<long long>  m_done{0};           // features read so far
    std::atomic<long long>  m_total{-1};         // GDAL's count (-1: unknown)
    std::mutex              m_mutex;
    std::optional<Answer>   m_answer;            // guarded by m_mutex
    std::string             m_readingLayer, m_readingField;
    double                  m_readSince = 0.0;   // ImGui's clock
    bool        m_graduate = false;
    int         m_classes  = 5;
    int         m_method   = 0;                  // 0 equal intervals, 1 quantiles, 2 natural breaks
    float       m_from[3] = {1.0f, 245.0f / 255.0f, 235.0f / 255.0f};   // #FFF5EB (§49)
    float       m_to[3]   = {140.0f / 255.0f, 45.0f / 255.0f, 4.0f / 255.0f};   // #8C2D04
};

} // namespace e2d
