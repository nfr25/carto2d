#include "panels/ClassifyPanel.h"

#include "style/catalog.hpp"     // render2d: newSymbolizer
#include "worker/LayerProbe.h"   // summarizeSource

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace e2d {

namespace {

constexpr int kMaxDistinct = 50;   // §48: beyond, a legend no longer reads

const ImVec4 kYellow(1.0f, 0.85f, 0.35f, 1.0f);
const ImVec4 kRed(1.0f, 0.45f, 0.40f, 1.0f);

r2d::Color colourOf(const float c[3])
{
    return {c[0], c[1], c[2], 1.0};
}

bool isNumeric(const LayerEntry& layer, std::size_t i)
{
    return i < layer.numeric.size() && layer.numeric[i];
}

} // namespace

ClassifyPanel::~ClassifyPanel()
{
    if (m_thread.joinable())
        m_thread.join();
}

void ClassifyPanel::reset()
{
    m_layer.clear();
    m_field.clear();
    m_summary.reset();
    m_error.clear();
}

// §82: the read goes to a thread; the panel draws a progress bar meanwhile,
// and takes the answer when it comes (collect). The previous thread, finished
// (the list waits while one reads), is joined first.
void ClassifyPanel::read(const LayerEntry& layer, const std::string& field)
{
    if (m_busy)
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_layer = layer.id;
    m_field = field;
    m_summary.reset();
    m_error.clear();
    m_readingLayer = layer.id;
    m_readingField = field;
    m_readSince    = ImGui::GetTime();
    m_done  = 0;
    m_total = -1;
    m_busy  = true;
    m_thread = std::thread([this, source = layer.source, id = layer.id, field]() {
        Answer a{id, field, std::nullopt, {}, 0.0};
        const auto t0 = std::chrono::steady_clock::now();
        a.summary = summarizeSource(source, field, a.error, &m_done, &m_total);
        a.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_answer = std::move(a);
        }
        m_busy = false;
    });
}

void ClassifyPanel::collect()
{
    std::optional<Answer> a;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        a.swap(m_answer);
    }
    if (!a || a->layer != m_layer || a->field != m_field)
        return;   // the dialog went to another layer meanwhile: an answer for nobody
    m_summary = std::move(a->summary);
    m_error   = std::move(a->error);
    m_readMs  = a->ms;
}

// "reading SUPERFICIE: 84,000 of 200,000 features, 2.1 s", over a bar. GDAL's
// count unknown (-1, some sources): a bar that moves without a fraction.
void ClassifyPanel::drawProgress()
{
    auto thousands = [](long long n) {
        std::string digits = std::to_string(n), out;
        for (std::size_t k = 0; k < digits.size(); ++k) {
            if (k > 0 && (digits.size() - k) % 3 == 0)
                out += ',';
            out += digits[k];
        }
        return out;
    };
    const long long done = m_done, total = m_total;
    const double seconds = ImGui::GetTime() - m_readSince;
    ImGui::Text("reading %s of %s...", m_readingField.c_str(), m_readingLayer.c_str());
    char overlay[96];
    if (done == 0) {   // still opening: a GeoJSON is read whole by GDAL before its first feature
        std::snprintf(overlay, sizeof overlay, "opening the source, %.1f s", seconds);
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-FLT_MIN, 0), overlay);
    } else if (total > 0) {
        std::snprintf(overlay, sizeof overlay, "%s of %s features, %.1f s", thousands(done).c_str(),
                      thousands(total).c_str(), seconds);
        ImGui::ProgressBar(std::min(1.0f, static_cast<float>(done) / static_cast<float>(total)), ImVec2(-FLT_MIN, 0),
                           overlay);
    } else {
        std::snprintf(overlay, sizeof overlay, "%s features, %.1f s", thousands(done).c_str(), seconds);
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-FLT_MIN, 0), overlay);
    }
}

r2d::ClassifySettings ClassifyPanel::settings() const
{
    r2d::ClassifySettings s;
    s.graduate    = m_graduate;
    s.field       = m_field;
    s.classes     = m_classes;
    s.method      = m_method == 1   ? r2d::BreakMethod::Quantiles
                  : m_method == 2   ? r2d::BreakMethod::NaturalBreaks   // render2d §77
                                    : r2d::BreakMethod::EqualIntervals;
    s.from        = colourOf(m_from);
    s.to          = colourOf(m_to);
    s.maxDistinct = kMaxDistinct;
    return s;
}

r2d::Symbolizer ClassifyPanel::fallback(const LayerEntry& layer) const
{
    const r2d::SymbolizerKind kind = layer.geometry == "point" ? r2d::SymbolizerKind::Point
                                   : layer.geometry == "line"  ? r2d::SymbolizerKind::Line
                                                               : r2d::SymbolizerKind::Polygon;
    return r2d::newSymbolizer(kind, layer.colour);
}

bool ClassifyPanel::draw(const LayerEntry& layer, r2d::Style& style)
{
    collect();   // §82: an answer from the thread
    if (m_layer != layer.id) {   // the dialog moved to another layer: start again
        reset();
        m_layer = layer.id;
    }
    const float em = ImGui::GetFontSize();

    // Categorize or graduate; graduate takes the numeric fields only (§46).
    if (ImGui::RadioButton("categorize", !m_graduate))
        m_graduate = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("graduate", m_graduate))
        m_graduate = true;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("field");
    ImGui::SameLine(em * 7.0f);
    ImGui::SetNextItemWidth(em * 14.0f);
    // §82: one read at a time. The disabled block holds the whole combo: its
    // list is a window of its own, opened and closed inside it.
    ImGui::BeginDisabled(m_busy);
    if (ImGui::BeginCombo("##field", m_field.empty() ? "(choose)" : m_field.c_str())) {
        for (std::size_t i = 0; i < layer.fields.size(); ++i) {
            if (m_graduate && !isNumeric(layer, i))
                continue;
            if (ImGui::Selectable(layer.fields[i].c_str(), layer.fields[i] == m_field))
                read(layer, layer.fields[i]);
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (m_graduate) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("classes");
        ImGui::SameLine(em * 7.0f);
        ImGui::SetNextItemWidth(em * 6.0f);
        ImGui::InputInt("##n", &m_classes);
        m_classes = std::clamp(m_classes, 2, 10);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 14.0f);   // "natural breaks (Jenks)" whole
        const char* methods[] = {"equal intervals", "quantiles", "natural breaks (Jenks)"};
        ImGui::Combo("##method", &m_method, methods, 3);
        ImGui::SetItemTooltip("equal intervals: the range cut in equal parts\n"
                              "quantiles: as many features in each class\n"
                              "natural breaks: the cuts in the gaps of the values");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("colours");
        ImGui::SameLine(em * 7.0f);
        ImGui::ColorEdit3("##from", m_from, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::TextUnformatted("->");
        ImGui::SameLine();
        ImGui::ColorEdit3("##to", m_to, ImGuiColorEditFlags_NoInputs);
    }

    // What was read: each fact on its own line (§50).
    ImGui::Separator();
    if (m_busy) {   // §82: the bar, and nothing else until the values are in
        drawProgress();
        return false;
    }
    if (!m_error.empty())
        ImGui::TextColored(kRed, "%s", m_error.c_str());
    if (!m_summary) {
        ImGui::TextDisabled(m_field.empty() ? "choose a field" : "");
        return false;
    }
    ImGui::TextDisabled("%d features, %d without value", m_summary->total, m_summary->nulls);
    if (!m_graduate)
        ImGui::TextDisabled("%zu%s distinct values", m_summary->distinct.size(), m_summary->tooMany ? "+" : "");
    ImGui::TextDisabled("read in %.0f ms", m_readMs);

    // render2d's classify(): the same recipe as `render2d --graduate` (its §63).
    const r2d::Classified c = r2d::classify(style, fallback(layer), *m_summary, settings());
    if (!c.ok) {
        ImGui::TextColored(kYellow, "%s", c.error.c_str());
        return false;
    }
    if (!m_graduate && m_summary->distinct.size() > 12)
        ImGui::TextDisabled("more than 12 values: the colours repeat");

    // The preview: the legend to come.
    if (ImGui::BeginTable("preview", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupColumn("rule", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, em * 6.0f);
        ImGui::TableSetupColumn("colour", ImGuiTableColumnFlags_WidthFixed, em * 3.0f);
        ImGui::TableHeadersRow();
        for (const r2d::LegendEntry& e : c.legend) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.count);
            ImGui::TableNextColumn();
            ImGui::ColorButton("##c", ImVec4(static_cast<float>(e.colour.r), static_cast<float>(e.colour.g),
                                             static_cast<float>(e.colour.b), 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(em * 2.0f, em * 0.9f));
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Apply")) {
        style = c.style;
        return true;
    }
    return false;
}

} // namespace e2d
