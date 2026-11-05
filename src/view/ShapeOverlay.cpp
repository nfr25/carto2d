#include "view/ShapeOverlay.h"

#include <imgui.h>

namespace e2d {

namespace {

const ImU32 kHalo  = IM_COL32(0, 0, 0, 150);
const ImU32 kLine  = IM_COL32(255, 140, 0, 255);   // orange: not the selection's cyan
const ImU32 kPale  = IM_COL32(255, 140, 0, 110);   // the closing line, not drawn yet
const ImU32 kFill  = IM_COL32(255, 140, 0, 40);
const ImU32 kMeasure     = IM_COL32(186, 104, 255, 255);   // §72: violet, neither the selection nor a zone
const ImU32 kMeasurePale = IM_COL32(186, 104, 255, 110);
const ImU32 kMeasureFill = IM_COL32(186, 104, 255, 40);

// Ground -> screen (§23), for the overlays of this file.
struct ToScreen {
    const ViewState& v;
    const MapArea&   area;
    ImVec2           fb;
    ImVec2 operator()(const r2d::Point& p) const
    {
        return ImVec2(area.x + static_cast<float>((p.x - v.xmin) / v.res) / fb.x,
                      area.y + static_cast<float>((v.ymax - p.y) / v.res) / fb.y);
    }
};

// A text with a dark band behind it, readable on any map.
void label(ImDrawList* draw, ImVec2 at, const std::string& text, float scale)
{
    if (text.empty())
        return;
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 pad(4.0f * scale, 2.0f * scale);
    const ImVec2 lo(at.x + 12.0f * scale, at.y + 12.0f * scale);
    draw->AddRectFilled(lo, ImVec2(lo.x + size.x + 2 * pad.x, lo.y + size.y + 2 * pad.y), IM_COL32(20, 20, 24, 210),
                        3.0f * scale);
    draw->AddText(ImVec2(lo.x + pad.x, lo.y + pad.y), IM_COL32(255, 255, 255, 255), text.c_str());
}

} // namespace

void drawShape(const Tool& tool, const std::vector<r2d::Point>& waiting, const Navigator& nav,
               const MapArea& area, float scale, const std::string& text)
{
    const bool measuring = tool.kind() == ToolKind::MeasureLine || tool.kind() == ToolKind::MeasureArea;
    const ImU32 kLine = measuring ? kMeasure : ::e2d::kLine;
    const ImU32 kPale = measuring ? kMeasurePale : ::e2d::kPale;
    if (!area.valid())
        return;
    const ViewState& v = nav.state();
    const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
    auto toScreen = [&](const r2d::Point& p) {   // §23: ground -> screen
        return ImVec2(area.x + static_cast<float>((p.x - v.xmin) / v.res) / fb.x,
                      area.y + static_cast<float>((v.ymax - p.y) / v.res) / fb.y);
    };

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(area.x, area.y), ImVec2(area.x + area.width, area.y + area.height));
    const float thin = 2.0f * scale, thick = 4.0f * scale, dot = 3.5f * scale;

    std::vector<ImVec2> pts;
    auto polyline = [&](ImDrawFlags flags) {
        draw->AddPolyline(pts.data(), static_cast<int>(pts.size()), kHalo, flags, thick);
        draw->AddPolyline(pts.data(), static_cast<int>(pts.size()), kLine, flags, thin);
    };

    // A finished zone waiting for its answer (§30).
    if (waiting.size() >= 3) {
        pts.clear();
        for (const r2d::Point& p : waiting)
            pts.push_back(toScreen(p));
        polyline(ImDrawFlags_Closed);
    }

    // The rectangle being pulled (§28).
    if (tool.rectangleActive()) {
        const ImVec2 a = toScreen(tool.corner1()), b = toScreen(tool.corner2());
        const ImVec2 lo(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y);
        const ImVec2 hi(a.x < b.x ? b.x : a.x, a.y < b.y ? b.y : a.y);
        draw->AddRectFilled(lo, hi, kFill);
        draw->AddRect(lo, hi, kHalo, 0.0f, 0, thick);
        draw->AddRect(lo, hi, kLine, 0.0f, 0, thin);
    }

    // The polygon being clicked (§29).
    const auto& vertices = tool.vertices();
    if (!vertices.empty()) {
        pts.clear();
        for (const r2d::Point& p : vertices)
            pts.push_back(toScreen(p));
        const ImVec2 mouse = toScreen(tool.hover());
        pts.push_back(mouse);   // the elastic line
        polyline(ImDrawFlags_None);
        if (vertices.size() >= 2 && tool.closes())
            draw->AddLine(mouse, toScreen(vertices.front()), kPale, thin);   // back to the start
        for (const r2d::Point& p : vertices)
            draw->AddCircleFilled(toScreen(p), dot, kLine);
        // §72: on the first vertex, a click closes: it says so, larger, ringed.
        if (tool.onFirstVertex(v.res)) {
            draw->AddCircle(toScreen(vertices.front()), dot * 2.6f, kHalo, 0, thick);
            draw->AddCircle(toScreen(vertices.front()), dot * 2.6f, kLine, 0, thin);
        }
        label(draw, mouse, text, scale);
    }
    draw->PopClipRect();
}

void drawMeasured(const std::vector<r2d::Point>& points, bool closed, const std::string& text,
                  const Navigator& nav, const MapArea& area, float scale)
{
    if (!area.valid() || points.size() < 2)
        return;
    const ToScreen toScreen{nav.state(), area, ImGui::GetIO().DisplayFramebufferScale};
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(area.x, area.y), ImVec2(area.x + area.width, area.y + area.height));
    std::vector<ImVec2> pts;
    for (const r2d::Point& p : points)
        pts.push_back(toScreen(p));
    const float thin = 2.0f * scale, thick = 4.0f * scale, dot = 3.5f * scale;
    if (closed && pts.size() >= 3)
        draw->AddConcavePolyFilled(pts.data(), static_cast<int>(pts.size()), kMeasureFill);
    const ImDrawFlags flags = closed ? ImDrawFlags_Closed : ImDrawFlags_None;
    draw->AddPolyline(pts.data(), static_cast<int>(pts.size()), kHalo, flags, thick);
    draw->AddPolyline(pts.data(), static_cast<int>(pts.size()), kMeasure, flags, thin);
    for (const ImVec2& p : pts)
        draw->AddCircleFilled(p, dot, kMeasure);
    label(draw, pts.back(), text, scale);   // next to the last point clicked
    draw->PopClipRect();
}

void drawPageOutline(const r2d::Envelope& frame, const std::string& text, const Navigator& nav,
                     const MapArea& area, float scale)
{
    if (!area.valid())
        return;
    const ToScreen toScreen{nav.state(), area, ImGui::GetIO().DisplayFramebufferScale};
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(area.x, area.y), ImVec2(area.x + area.width, area.y + area.height));
    const ImVec2 a = toScreen({frame.xmin, frame.ymax}), b = toScreen({frame.xmax, frame.ymin});
    const ImU32 page = IM_COL32(30, 90, 200, 255), veil = IM_COL32(30, 90, 200, 28);
    draw->AddRectFilled(a, b, veil);
    draw->AddRect(a, b, kHalo, 0.0f, 0, 4.0f * scale);
    draw->AddRect(a, b, page, 0.0f, 0, 2.0f * scale);
    label(draw, a, text, scale);   // at the top-left corner
    draw->PopClipRect();
}

} // namespace e2d
