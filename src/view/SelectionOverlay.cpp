#include "view/SelectionOverlay.h"

#include <imgui.h>

#include <vector>

namespace e2d {

namespace {

const ImU32 kHalo  = IM_COL32(0, 0, 0, 150);       // under the colour: visible on any map
const ImU32 kColor = IM_COL32(0, 229, 255, 255);   // cyan: no map style uses it
const ImU32 kFocus = IM_COL32(255, 140, 0, 255);   // §60: the designated one, orange

} // namespace

void drawSelection(const Document& doc, const FeatureCache& known, const Navigator& nav, const MapArea& area,
                   float scale, const std::pair<std::string, std::int64_t>& focus)
{
    if (doc.selection.empty() || !area.valid())
        return;

    // §23: ground -> screen pixel with the wanted view, then -> ImGui units,
    // from the corner of the central zone. The same path as §12, backwards.
    const ViewState& v = nav.state();
    const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
    auto toScreen = [&](const r2d::Point& p) {
        return ImVec2(area.x + static_cast<float>((p.x - v.xmin) / v.res) / fb.x,
                      area.y + static_cast<float>((v.ymax - p.y) / v.res) / fb.y);
    };

    ImDrawList* draw = ImGui::GetBackgroundDrawList();   // over the map, under the panels
    draw->PushClipRect(ImVec2(area.x, area.y), ImVec2(area.x + area.width, area.y + area.height));
    const float thin = 2.5f * scale, thick = 5.0f * scale, radius = 7.0f * scale;

    std::vector<ImVec2> pts;
    // One feature: a halo, then its colour; `wide` for the designated one.
    auto drawOne = [&](const r2d::Geometry& g, ImU32 colour, bool wide) {
        const float w1 = wide ? thin * 1.8f : thin, w0 = wide ? thick * 1.6f : thick;
        if (g.type == r2d::GeomType::Point) {
            for (const r2d::Point& p : g.points) {
                draw->AddCircle(toScreen(p), wide ? radius * 1.4f : radius, kHalo, 0, w0);
                draw->AddCircle(toScreen(p), wide ? radius * 1.4f : radius, colour, 0, w1);
            }
            return;
        }
        const ImDrawFlags flags = g.type == r2d::GeomType::Polygon ? ImDrawFlags_Closed : ImDrawFlags_None;
        for (const r2d::Part& part : g.parts) {
            pts.clear();
            for (std::size_t k = 0; k < part.count; ++k)
                pts.push_back(toScreen(g.points[part.start + k]));
            draw->AddPolyline(pts.data(), static_cast<int>(pts.size()), kHalo, flags, w0);
            draw->AddPolyline(pts.data(), static_cast<int>(pts.size()), colour, flags, w1);
        }
    };
    const r2d::Geometry* focused = nullptr;
    for (const auto& [layer, fids] : doc.selection) {
        const int i = doc.findLayer(layer);
        if (i < 0 || !doc.layers[static_cast<std::size_t>(i)].visible)
            continue;   // a hidden layer keeps its selection, not drawn
        for (std::int64_t fid : fids) {
            const auto it = known.find({layer, fid});
            if (it == known.end())
                continue;
            if (layer == focus.first && fid == focus.second)
                focused = &it->second.geometry;   // drawn last, on top
            else
                drawOne(it->second.geometry, kColor, false);
        }
    }
    if (focused)
        drawOne(*focused, kFocus, true);
    draw->PopClipRect();
}

} // namespace e2d
