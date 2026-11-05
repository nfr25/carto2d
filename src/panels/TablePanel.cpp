#include "panels/TablePanel.h"

#include "panels/ValueText.h"

#include <imgui.h>

#include <algorithm>
#include <variant>

namespace e2d {

namespace {

// §60: numbers as numbers, texts alphabetically, NULL (and "not read") always last.
// Returns <0, 0, >0 like strcmp; `missing` values are put last by the caller.
int compareValues(const r2d::Value& a, const r2d::Value& b)
{
    auto number = [](const r2d::Value& v, double& out) {
        if (const auto* i = std::get_if<long long>(&v)) { out = static_cast<double>(*i); return true; }
        if (const auto* d = std::get_if<double>(&v)) { out = *d; return true; }
        return false;
    };
    double x = 0.0, y = 0.0;
    const bool nx = number(a, x), ny = number(b, y);
    if (nx && ny)
        return x < y ? -1 : x > y ? 1 : 0;
    if (nx != ny)
        return nx ? -1 : 1;   // numbers before texts
    return std::get<std::string>(a).compare(std::get<std::string>(b));
}

void sortRows(const std::string& layer, const FeatureCache& known, TablePanelState& state)
{
    const int column = state.sortColumn;
    const bool up = state.ascending;
    std::stable_sort(state.rows.begin(), state.rows.end(), [&](std::int64_t a, std::int64_t b) {
        if (column == 0)
            return up ? a < b : a > b;
        const std::size_t c = static_cast<std::size_t>(column - 1);
        auto valueOf = [&](std::int64_t fid) -> const r2d::Value* {
            const auto it = known.find({layer, fid});
            if (it == known.end() || c >= it->second.attributes.size())
                return nullptr;
            const r2d::Value& v = it->second.attributes[c];
            return std::holds_alternative<std::monostate>(v) ? nullptr : &v;
        };
        const r2d::Value* va = valueOf(a);
        const r2d::Value* vb = valueOf(b);
        if (!va || !vb)
            return va && !vb;   // NULL last, in both directions
        const int k = compareValues(*va, *vb);
        return up ? k < 0 : k > 0;
    });
}

} // namespace

std::optional<std::pair<std::string, std::int64_t>>
drawTablePanel(const Document& doc, const FeatureCache& known, SelectionPanelState& selection,
               TablePanelState& state)
{
    std::optional<std::pair<std::string, std::int64_t>> zoom;
    ImGui::Begin("Table");   // docked with Report and Events by default (Workspace)

    if (doc.selection.empty()) {
        ImGui::TextDisabled("nothing selected: click, draw a zone, or select by attributes (Selection panel)");
        ImGui::End();
        return zoom;
    }
    // The layer: kept while it has a selection, else the one with most.
    if (!doc.selection.count(state.layer)) {
        std::size_t most = 0;
        for (const auto& [layer, fids] : doc.selection)
            if (fids.size() > most) {
                most        = fids.size();
                state.layer = layer;
            }
        state.dirty = true;
    }
    const std::set<std::int64_t>& selected = doc.selection.at(state.layer);
    const int li = doc.findLayer(state.layer);
    static const std::vector<std::string> kNoField;
    const std::vector<std::string>& fields = li >= 0 ? doc.layers[static_cast<std::size_t>(li)].fields : kNoField;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("layer");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
    if (ImGui::BeginCombo("##tableLayer", state.layer.c_str())) {
        for (const auto& [layer, fids] : doc.selection)
            if (ImGui::Selectable((layer + " (" + std::to_string(fids.size()) + ")").c_str(), layer == state.layer)) {
                state.layer = layer;
                state.dirty = true;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu row(s)", selected.size());   // nfr, 23 October: "le s entre ()"

    // The selection of this layer changed (a click, Ctrl+Z...): the rows again.
    if (state.dirty || state.sortedFor.size() != selected.size() ||
        !std::equal(state.sortedFor.begin(), state.sortedFor.end(), selected.begin())) {
        state.sortedFor.assign(selected.begin(), selected.end());
        state.rows = state.sortedFor;
        sortRows(state.layer, known, state);
        state.dirty = false;
    }

    const int columns = 1 + static_cast<int>(fields.size());
    const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    // §64: one table per layer (its own columns, fitted to their contents);
    // no widths kept in imgui.ini, where another layer's would come back.
    ImGui::PushID(state.layer.c_str());
    if (columns <= 64 && ImGui::BeginTable("rows", columns, flags)) {
        ImGui::TableSetupScrollFreeze(1, 1);   // the fid column and the headers stay
        ImGui::TableSetupColumn("fid", ImGuiTableColumnFlags_DefaultSort);
        for (const std::string& f : fields)
            ImGui::TableSetupColumn(f.c_str());
        ImGui::TableHeadersRow();

        // §60: sort only when a header was clicked.
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                state.sortColumn = specs->Specs[0].ColumnIndex;
                state.ascending  = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                sortRows(state.layer, known, state);
            }
            specs->SpecsDirty = false;
        }

        // §60: only the visible rows are drawn.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.rows.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const std::int64_t fid = state.rows[static_cast<std::size_t>(r)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool chosen = selection.layer == state.layer && selection.fid == fid;
                const std::string label = std::to_string(fid) + "##row";
                ImGui::PushID(r);
                if (ImGui::Selectable(label.c_str(), chosen,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selection.layer = state.layer;   // designated: attributes, stronger on the map
                    selection.fid   = fid;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        zoom = std::make_pair(state.layer, fid);
                }
                const auto it = known.find({state.layer, fid});
                for (std::size_t c = 0; c < fields.size(); ++c) {
                    ImGui::TableNextColumn();
                    if (it == known.end()) {
                        ImGui::TextDisabled("(not read)");
                        continue;
                    }
                    const std::string value =
                        c < it->second.attributes.size() ? valueText(it->second.attributes[c]) : std::string();
                    ImGui::TextUnformatted(value.c_str());
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {   // §26: copy
                        ImGui::SetClipboardText(value.c_str());
                        selection.copiedField = fields[c];
                        selection.copiedAt    = ImGui::GetTime();
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::End();
    return zoom;
}

} // namespace e2d
