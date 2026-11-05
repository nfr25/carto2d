#include "panels/LayerList.h"

#include "panels/LayerOrder.h"

#include <imgui.h>

#include <algorithm>

namespace e2d {

namespace {

std::string typeText(const r2d::LayerInfo& t)
{
    if (!t.type)
        return "mixed";
    return *t.type == r2d::GeomType::Point ? "point" : *t.type == r2d::GeomType::Line ? "line" : "polygon";
}

std::string crsText(const r2d::CrsInfo& c)
{
    if (!c.known)
        return "none";
    return c.epsg != 0 ? "EPSG:" + std::to_string(c.epsg) : c.name;
}

} // namespace

LayerListResult drawLayerList(const std::vector<r2d::LayerInfo>& layers, std::set<std::string>& chosen)
{
    LayerListResult r;
    // §66: the tables of attributes only (a GeoPackage's lists of values) are not layers to draw.
    const auto tables = std::count_if(layers.begin(), layers.end(), [](const r2d::LayerInfo& l) { return !l.hasGeometry; });
    if (tables > 0)
        ImGui::TextDisabled("%d table(s) without geometry, not shown", static_cast<int>(tables));
    const float footer = ImGui::GetFrameHeightWithSpacing();
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("layers", 4, flags, ImVec2(0.0f, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("layer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("type");
        ImGui::TableSetupColumn("features");
        ImGui::TableSetupColumn("CRS");
        ImGui::TableHeadersRow();
        for (const r2d::LayerInfo& t : layers) {
            if (!t.hasGeometry)
                continue;   // a table of attributes: nothing to draw (§66)
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(t.name.c_str());
            bool ticked = chosen.count(t.name) > 0;
            if (ImGui::Checkbox("##tick", &ticked)) {
                if (ticked)
                    chosen.insert(t.name);
                else
                    chosen.erase(t.name);
            }
            ImGui::SameLine();
            if (ImGui::Selectable(t.name.c_str(), ticked,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                r.add.push_back(t.name);   // one layer at once
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(typeText(t).c_str());
            ImGui::TableNextColumn();
            if (t.featureCount >= 0)
                ImGui::Text("%lld", t.featureCount);
            else
                ImGui::TextDisabled("?");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(crsText(t.crs).c_str());
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("All"))
        for (const r2d::LayerInfo& t : layers)
            if (t.hasGeometry)
                chosen.insert(t.name);
    ImGui::SameLine();
    if (ImGui::SmallButton("None"))
        chosen.clear();
    ImGui::SameLine();
    ImGui::BeginDisabled(chosen.empty());
    const std::string button = "Add " + std::to_string(chosen.size()) + " layer(s)";
    if (ImGui::Button(button.c_str())) {
        r.add    = orderForAdding(layers, chosen);   // §66: surfaces at the bottom, points on top
        r.button = true;
        chosen.clear();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("surfaces first, then lines, then points: the points end on top");
    return r;
}

} // namespace e2d
