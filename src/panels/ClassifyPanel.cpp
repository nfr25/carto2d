#include "panels/ClassifyPanel.h"

#include "style/catalog.hpp"     // render2d: newSymbolizer
#include "worker/LayerProbe.h"   // summarizeSource

#include <imgui.h>

#include <algorithm>
#include <chrono>

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

void ClassifyPanel::reset()
{
    m_layer.clear();
    m_field.clear();
    m_summary.reset();
    m_error.clear();
}

void ClassifyPanel::read(const LayerEntry& layer, const std::string& field)
{
    m_layer = layer.id;
    m_field = field;
    m_summary.reset();
    m_error.clear();
    const auto t0 = std::chrono::steady_clock::now();
    m_summary = summarizeSource(layer.source, field, m_error);
    m_readMs  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

r2d::ClassifySettings ClassifyPanel::settings() const
{
    r2d::ClassifySettings s;
    s.graduate    = m_graduate;
    s.field       = m_field;
    s.classes     = m_classes;
    s.method      = m_method == 0 ? r2d::BreakMethod::EqualIntervals : r2d::BreakMethod::Quantiles;
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
    if (ImGui::BeginCombo("##field", m_field.empty() ? "(choose)" : m_field.c_str())) {
        for (std::size_t i = 0; i < layer.fields.size(); ++i) {
            if (m_graduate && !isNumeric(layer, i))
                continue;
            if (ImGui::Selectable(layer.fields[i].c_str(), layer.fields[i] == m_field))
                read(layer, layer.fields[i]);
        }
        ImGui::EndCombo();
    }

    if (m_graduate) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("classes");
        ImGui::SameLine(em * 7.0f);
        ImGui::SetNextItemWidth(em * 6.0f);
        ImGui::InputInt("##n", &m_classes);
        m_classes = std::clamp(m_classes, 2, 10);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 10.0f);
        const char* methods[] = {"equal intervals", "quantiles"};
        ImGui::Combo("##method", &m_method, methods, 2);
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
