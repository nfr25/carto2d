#include "panels/LayerChooser.h"

#include "panels/LayerList.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace e2d {

void LayerChooser::offer(const std::string& source, std::vector<r2d::LayerInfo> layers)
{
    m_offers.push_back({source, std::move(layers)});
}

std::vector<std::string> LayerChooser::draw()
{
    std::vector<std::string> add;
    if (m_offers.empty())
        return add;
    const Offer& offer = m_offers.front();

    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 34.0f, em * 22.0f), ImGuiCond_FirstUseEver);
    // One window, its title follows the file: "###chooser" keeps its place and size.
    const std::string title = "Layers of " + std::filesystem::path(offer.source).filename().string() + "###chooser";
    bool open = true;
    bool added = false;
    if (ImGui::Begin(title.c_str(), &open)) {
        const auto n = std::count_if(offer.layers.begin(), offer.layers.end(),
                                     [](const r2d::LayerInfo& l) { return l.hasGeometry; });
        ImGui::TextDisabled("%d layers; tick, or double-click one", static_cast<int>(n));
        if (m_offers.size() > 1)
            ImGui::TextDisabled("then %zu more file(s)", m_offers.size() - 1);
        const LayerListResult r = drawLayerList(offer.layers, m_chosen);
        for (const std::string& name : r.add)
            add.push_back(offer.source + "::" + name);
        added = r.button;
    }
    ImGui::End();
    // Closed by its cross, or layers added with the button: the next file.
    // (A double-click adds one layer and leaves the list open for more.)
    if (!open || added) {
        m_offers.pop_front();
        m_chosen.clear();
    }
    return add;
}

} // namespace e2d
