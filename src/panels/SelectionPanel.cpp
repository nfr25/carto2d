#include "panels/SelectionPanel.h"

#include "panels/ValueText.h"
#include "style/expression.hpp"   // render2d: the filters' parser (§58)

#include <imgui.h>
#include <imgui_stdlib.h>          // InputText on a std::string

#include <variant>

namespace e2d {

namespace {

// §58: the three lines of the selection by attributes.
void drawWhere(const Document& doc, SelectionPanelState& state)
{
    // The vector layers only: a raster has no attribute (§66).
    auto isVector = [&](int i) { return doc.layers[static_cast<std::size_t>(i)].geometry != "raster"; };
    int top = static_cast<int>(doc.layers.size()) - 1;
    while (top >= 0 && !isVector(top))
        --top;
    if (top < 0)
        return;
    const float em = ImGui::GetFontSize();
    // §64: a feature designated in another layer (a click, the table): the
    // question goes to that layer. Once: the combo can still choose another.
    if (state.layer != state.whereFollowed) {
        state.whereFollowed = state.layer;
        if (const int i = doc.findLayer(state.layer); i >= 0 && isVector(i))
            state.whereLayer = state.layer;
    }
    // The layer: kept while it exists, else the top one.
    if (const int i = doc.findLayer(state.whereLayer); i < 0 || !isVector(i))
        state.whereLayer = doc.layers[static_cast<std::size_t>(top)].id;
    const LayerEntry& layer = doc.layers[static_cast<std::size_t>(doc.findLayer(state.whereLayer))];

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("layer");
    ImGui::SameLine(em * 4.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##whereLayer", layer.id.c_str())) {
        for (int i = static_cast<int>(doc.layers.size()) - 1; i >= 0; --i) {   // top first, as the Layers panel
            if (!isVector(i))
                continue;
            const std::string& id = doc.layers[static_cast<std::size_t>(i)].id;
            if (ImGui::Selectable(id.c_str(), id == state.whereLayer))
                state.whereLayer = id;
        }
        ImGui::EndCombo();
    }

    // The expression, read by render2d's parser as it is typed (§42).
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("where");
    ImGui::SameLine(em * 4.0f);
    const bool bad = !state.whereError.empty();
    if (bad)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
    ImGui::SetNextItemWidth(-em * 4.5f);
    const bool enter = ImGui::InputText("##where", &state.whereText, ImGuiInputTextFlags_EnterReturnsTrue);
    if (bad)
        ImGui::PopStyleColor();
    if (ImGui::IsItemEdited() || enter) {
        state.whereError.clear();
        if (!state.whereText.empty())
            r2d::Expression::parseCondition(state.whereText, state.whereError);
    }
    // §46: a field, added at the end of the text.
    ImGui::SameLine();
    if (ImGui::SmallButton("fields"))
        ImGui::OpenPopup("whereFields");
    if (ImGui::BeginPopup("whereFields")) {
        for (const std::string& f : layer.fields)
            if (ImGui::MenuItem(f.c_str())) {
                if (!state.whereText.empty() && state.whereText.back() != ' ')
                    state.whereText += ' ';
                state.whereText += f + ' ';
            }
        if (layer.fields.empty())
            ImGui::TextDisabled("no field");
        ImGui::EndPopup();
    }
    if (bad)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "%s", state.whereError.c_str());

    ImGui::RadioButton("new", &state.whereMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("add", &state.whereMode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("remove", &state.whereMode, 2);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.whereText.empty() || bad || state.whereBusy);
    if (ImGui::SmallButton("Select") || (enter && !bad && !state.whereText.empty() && !state.whereBusy))
        state.whereAsked = true;
    ImGui::EndDisabled();
    // On its own line: the panel is often narrow (§50's lesson).
    if (state.whereBusy)
        ImGui::TextDisabled("reading the whole layer...");
    else if (!state.whereResult.empty())
        ImGui::TextDisabled("%s", state.whereResult.c_str());
    ImGui::Separator();
}

} // namespace

bool drawSelectionPanel(const Document& doc, const FeatureCache& known, Commander& commander,
                        SelectionPanelState& state)
{
    bool zoom = false;
    ImGui::Begin("Selection");   // docked on the left by default (Workspace)
    drawWhere(doc, state);       // §58

    const std::size_t n = doc.selectionCount();
    ImGui::Text("%zu %s", n, n == 1 ? "feature" : "features");
    ImGui::SameLine();
    ImGui::BeginDisabled(n == 0);
    if (ImGui::SmallButton("Clear")) {
        std::string error;
        commander.run({{"name", "selection.set"}, {"mode", "clear"}, {"items", nlohmann::json::array()}}, error);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Zoom to selection"))
        zoom = true;
    ImGui::EndDisabled();
    if (n == 0)
        ImGui::TextDisabled("click a feature on the map (Shift: add, Ctrl: toggle, Esc: clear)");

    // The features, grouped by layer; the one clicked shows its attributes.
    // Still selected? Asked of the document, not of the open tree nodes: a
    // folded layer, or a row chosen in the Table (§60), keeps its feature.
    const auto chosenLayer = doc.selection.find(state.layer);
    const bool stateStillSelected = chosenLayer != doc.selection.end() && chosenLayer->second.count(state.fid);
    for (const auto& [layer, fids] : doc.selection) {
        const int li = doc.findLayer(layer);
        const bool hidden = li >= 0 && !doc.layers[static_cast<std::size_t>(li)].visible;
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        const std::string header = layer + " (" + std::to_string(fids.size()) + ")" + (hidden ? "  hidden" : "");
        if (!ImGui::TreeNode(layer.c_str(), "%s", header.c_str()))
            continue;
        // Only the visible lines are drawn (§60: 5 878 selected, 30 seen).
        const std::vector<std::int64_t> list(fids.begin(), fids.end());
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(list.size()));
        while (clipper.Step())
            for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
                const std::int64_t fid = list[static_cast<std::size_t>(k)];
                const bool chosen = state.layer == layer && state.fid == fid;
                const std::string label = "fid " + std::to_string(fid) + "##" + layer;
                if (ImGui::Selectable(label.c_str(), chosen)) {
                    state.layer = layer;
                    state.fid   = fid;
                }
            }
        ImGui::TreePop();
    }
    // A single feature: show it without asking for a click in the list.
    if (!stateStillSelected) {
        state.layer = {};
        state.fid   = -1;
        if (n == 1) {
            state.layer = doc.selection.begin()->first;
            state.fid   = *doc.selection.begin()->second.begin();
        }
    }

    // The attributes: every field, as the question read them (§24).
    const auto it = known.find({state.layer, state.fid});
    const int li  = doc.findLayer(state.layer);
    if (it != known.end() && li >= 0) {
        ImGui::SeparatorText(("fid " + std::to_string(state.fid)).c_str());
        const auto& names = doc.layers[static_cast<std::size_t>(li)].fields;
        if (ImGui::BeginTable("attributes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            for (std::size_t c = 0; c < names.size() && c < it->second.attributes.size(); ++c) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", names[c].c_str());
                ImGui::TableNextColumn();
                const std::string value = valueText(it->second.attributes[c]);
                ImGui::TextUnformatted(value.c_str());
                // §26: a long value is cut by the panel; the tooltip shows it all,
                // a right click copies it. No command: the document does not change.
                ImGui::SetItemTooltip("%s\n%s\n(right click: copy)", names[c].c_str(), value.c_str());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    ImGui::SetClipboardText(value.c_str());
                    state.copiedField = names[c];
                    state.copiedAt    = ImGui::GetTime();
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    return zoom;
}

} // namespace e2d
