#include "panels/CrsDialog.h"

#include <imgui.h>
#include <imgui_stdlib.h>          // InputText on a std::string

namespace e2d {

namespace {

const ImVec4 kRed(1.0f, 0.45f, 0.40f, 1.0f);

std::string label(const r2d::CrsInfo& c)
{
    if (!c.known)
        return "not decided yet (no layer declares a system)";
    std::string code = c.epsg ? "EPSG:" + std::to_string(c.epsg) : c.code;
    return code.empty() ? c.name : code + "  " + c.name;
}

} // namespace

void CrsDialog::open()
{
    m_open  = true;
    m_dirty = true;
    m_error.clear();
    ImGui::SetWindowFocus("Map system");
}

void CrsDialog::search(const std::optional<r2d::Point>& centre)
{
    const bool nearWanted = m_near && centre.has_value();
    const bool sameCentre = m_searchedAt.has_value() == centre.has_value() &&
                            (!centre || (m_searchedAt->x == centre->x && m_searchedAt->y == centre->y));
    if (!m_dirty && m_words == m_searched && nearWanted == m_searchedNear && (!nearWanted || sameCentre))
        return;
    m_results      = r2d::searchCatalog(r2d::projectedCatalog(), m_words,
                                        nearWanted ? centre : std::optional<r2d::Point>());
    m_searched     = m_words;
    m_searchedNear = nearWanted;
    m_searchedAt   = centre;
    m_dirty        = false;
}

std::string CrsDialog::draw(const r2d::CrsInfo& mapCrs, const std::optional<r2d::Point>& centre,
                            const std::vector<CrsLayerRow>& layers)
{
    std::string chosen;
    if (!m_open)
        return chosen;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 46.0f, em * 34.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map system", &m_open)) {
        ImGui::End();
        return chosen;
    }

    // 1. The map's system now, and its area of use.
    ImGui::TextUnformatted("The map is drawn in");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "%s", label(mapCrs).c_str());
    if (const r2d::CrsEntry* e = r2d::catalogEntry(mapCrs)) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("made for: %s", e->area.c_str());
        // §70: Brussels in the Faroe TM is drawn, but turned by about 9 degrees.
        if (centre && !e->contains(centre->x, centre->y))
            ImGui::TextColored(kRed, "the map's centre is outside this area of use: shapes, angles and "
                                     "distances may be distorted");
        ImGui::PopTextWrapPos();
    }
    if (centre)
        ImGui::TextDisabled("the map's centre: %.4f %s, %.4f %s", std::abs(centre->x), centre->x < 0 ? "W" : "E",
                            std::abs(centre->y), centre->y < 0 ? "S" : "N");
    ImGui::Separator();

    // 2. The catalogue: words, and "near the map".
    ImGui::SetNextItemWidth(em * 18.0f);
    ImGui::InputTextWithHint("##words", "search: faroe, utm 29n, 31370...", &m_words);
    ImGui::SameLine();
    ImGui::BeginDisabled(!centre);
    ImGui::Checkbox("made for the map's place", &m_near);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("only the systems whose area of use contains the map's centre,\n"
                          "the most local first (notions §70)");
    search(centre);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu of %zu", m_results.size(), r2d::projectedCatalog().size());

    const float listH = ImGui::GetContentRegionAvail().y - em * 11.0f;
    if (ImGui::BeginTable("crs", 3,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable,
                          ImVec2(0.0f, std::max(listH, em * 6.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("code", ImGuiTableColumnFlags_WidthFixed, em * 6.5f);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("area of use", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;   // 5,000 rows: only those seen are drawn
        clipper.Begin(static_cast<int>(m_results.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const r2d::CrsEntry& e = *m_results[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                if (ImGui::Selectable(e.code.c_str(), m_selected == e.code,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_selected = e.code;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        chosen = e.code;
                }
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(e.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(e.area.c_str());
                if (ImGui::IsItemHovered() && e.area.size() > 40)
                    ImGui::SetTooltip("%s", e.area.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::BeginDisabled(m_selected.empty());
    if (ImGui::Button(m_selected.empty() ? "Use this system" : ("Use " + m_selected).c_str()))
        chosen = m_selected;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(or double-click a row)");

    // 3. A definition: a code, a WKT, a .prj file's path.
    ImGui::SetNextItemWidth(-em * 5.0f);
    ImGui::InputTextWithHint("##definition", "or a definition: EPSG:5316, a WKT, a .prj file", &m_definition);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_definition.empty());
    if (ImGui::Button("Use")) {
        std::string error;
        if (r2d::crsFromUser(m_definition, error)) {
            chosen = m_definition;
            m_error.clear();
        } else {
            m_error = error;
        }
    }
    ImGui::EndDisabled();
    if (!m_error.empty())
        ImGui::TextColored(kRed, "%s", m_error.c_str());

    // 4. Each layer: its own system, and how it comes to the map's.
    ImGui::Separator();
    ImGui::TextUnformatted("The layers");
    if (ImGui::BeginTable("layers", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                           ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, em * 7.0f))) {
        ImGui::TableSetupColumn("layer", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("its system", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("to the map's", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableHeadersRow();
        for (const CrsLayerRow& r : layers) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.layer.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.system.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.conversion.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", r.conversion.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
    return chosen;
}

} // namespace e2d
