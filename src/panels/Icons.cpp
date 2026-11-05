#include "panels/Icons.h"

#include <cmath>
#include <initializer_list>
#include <vector>

namespace e2d {

namespace {

constexpr float kPi = 3.14159265f;

} // namespace

// Every icon is drawn in a unit square: (0, 0) top left, (1, 1) bottom right,
// with a margin of 0.12 kept free around it (§61).
void drawIcon(ImDrawList* draw, Icon icon, ImVec2 corner, float size, ImU32 colour)
{
    auto at = [&](float x, float y) { return ImVec2(corner.x + x * size, corner.y + y * size); };
    const float line = std::max(1.0f, size * 0.07f);   // 2.2 px for a 32 px icon
    auto stroke = [&](std::initializer_list<ImVec2> unit, bool closed) {
        std::vector<ImVec2> p;
        for (const ImVec2& u : unit)
            p.push_back(at(u.x, u.y));
        draw->AddPolyline(p.data(), static_cast<int>(p.size()), colour, closed ? ImDrawFlags_Closed : 0, line);
    };
    // A curved arrow: an arc of radius 0.24 around (0.5, 0.58), then its head.
    auto turn = [&](bool left) {
        const ImVec2 c = at(0.5f, 0.58f);
        draw->PathArcTo(c, 0.24f * size, left ? 0.0f : -kPi, left ? -kPi : 0.0f, 16);   // over the top
        draw->PathStroke(colour, 0, line);
        const float x = left ? 0.26f : 0.74f;   // the end of the arc
        draw->AddTriangleFilled(at(x - 0.13f, 0.50f), at(x + 0.13f, 0.50f), at(x, 0.72f), colour);
    };
    // A magnifying glass: a ring and a handle.
    auto lens = [&]() {
        draw->AddCircle(at(0.42f, 0.42f), 0.24f * size, colour, 24, line);
        draw->AddLine(at(0.60f, 0.60f), at(0.86f, 0.86f), colour, line * 1.8f);
    };

    switch (icon) {
    case Icon::Undo:
        turn(true);
        break;
    case Icon::Redo:
        turn(false);
        break;
    case Icon::Pointer: {
        // The classic arrow, as two simple shapes that stay clear at 32 px:
        // a head (a triangle, its tip at the top left) and a tail.
        draw->AddTriangleFilled(at(0.26f, 0.10f), at(0.26f, 0.74f), at(0.74f, 0.58f), colour);
        draw->AddLine(at(0.44f, 0.58f), at(0.64f, 0.92f), colour, line * 2.2f);
        break;
    }
    case Icon::Rectangle: {
        // A dashed rectangle: 3 dashes per side.
        const float x0 = 0.14f, y0 = 0.24f, x1 = 0.86f, y1 = 0.76f;
        auto dashes = [&](float ax, float ay, float bx, float by) {
            for (int k = 0; k < 3; ++k) {
                const float t0 = (k * 2.0f) / 5.0f, t1 = (k * 2.0f + 1.0f) / 5.0f;   // dash, gap, dash...
                draw->AddLine(at(ax + (bx - ax) * t0, ay + (by - ay) * t0), at(ax + (bx - ax) * t1, ay + (by - ay) * t1),
                              colour, line);
            }
        };
        dashes(x0, y0, x1, y0);
        dashes(x1, y0, x1, y1);
        dashes(x1, y1, x0, y1);
        dashes(x0, y1, x0, y0);
        break;
    }
    case Icon::Polygon: {
        // An irregular outline, its vertices marked.
        const ImVec2 v[] = {{0.20f, 0.30f}, {0.55f, 0.16f}, {0.85f, 0.42f}, {0.68f, 0.84f}, {0.24f, 0.72f}};
        stroke({v[0], v[1], v[2], v[3], v[4]}, true);
        const float s = 0.06f;
        for (const ImVec2& u : v)
            draw->AddRectFilled(at(u.x - s, u.y - s), at(u.x + s, u.y + s), colour);
        break;
    }
    case Icon::ZoomWindow:
        lens();
        // A small rectangle in the lens: zoom to a window.
        draw->AddRect(at(0.31f, 0.33f), at(0.53f, 0.51f), colour, 0.0f, 0, line * 0.8f);
        break;
    case Icon::ZoomSelection:
        lens();
        draw->AddCircleFilled(at(0.42f, 0.42f), 0.09f * size, IM_COL32(0, 229, 255, 255));   // the selection's cyan
        break;
    case Icon::FullExtent:
        // Four corners pointing out: everything.
        stroke({{0.14f, 0.36f}, {0.14f, 0.14f}, {0.36f, 0.14f}}, false);
        stroke({{0.64f, 0.14f}, {0.86f, 0.14f}, {0.86f, 0.36f}}, false);
        stroke({{0.86f, 0.64f}, {0.86f, 0.86f}, {0.64f, 0.86f}}, false);
        stroke({{0.36f, 0.86f}, {0.14f, 0.86f}, {0.14f, 0.64f}}, false);
        draw->AddRect(at(0.34f, 0.34f), at(0.66f, 0.66f), colour, 0.0f, 0, line);
        break;
    case Icon::MeasureLine: {
        // §72: a ruler, laid across, with its graduations.
        stroke({{0.10f, 0.62f}, {0.62f, 0.10f}, {0.90f, 0.38f}, {0.38f, 0.90f}}, true);
        for (int k = 1; k <= 4; ++k) {
            const float t = k / 5.0f;
            const float x = 0.10f + t * 0.52f, y = 0.62f - t * 0.52f;
            const float len = k % 2 ? 0.10f : 0.17f;
            stroke({{x, y}, {x + len * 0.71f, y + len * 0.71f}}, false);
        }
        break;
    }
    case Icon::MeasureArea:
        // §72: a surface, its corners marked.
        draw->AddQuadFilled(at(0.16f, 0.30f), at(0.70f, 0.16f), at(0.86f, 0.70f), at(0.30f, 0.84f),
                            (colour & 0x00FFFFFFu) | 0x50000000u);
        stroke({{0.16f, 0.30f}, {0.70f, 0.16f}, {0.86f, 0.70f}, {0.30f, 0.84f}}, true);
        for (const ImVec2& p : {ImVec2(0.16f, 0.30f), ImVec2(0.70f, 0.16f), ImVec2(0.86f, 0.70f), ImVec2(0.30f, 0.84f)})
            draw->AddCircleFilled(at(p.x, p.y), line * 1.4f, colour);
        break;
    }
}

bool iconButton(const char* id, Icon icon, bool active, bool enabled, const char* tooltip)
{
    const float size = ImGui::GetFrameHeight() * 1.25f;
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    const ImVec2 corner = ImGui::GetItemRectMin();
    ImDrawList* draw    = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    if (active || (hovered && enabled))
        draw->AddRectFilled(corner, ImVec2(corner.x + size, corner.y + size),
                            ImGui::GetColorU32(active ? style.Colors[ImGuiCol_ButtonActive] : style.Colors[ImGuiCol_ButtonHovered]),
                            style.FrameRounding);
    const ImU32 colour = ImGui::GetColorU32(enabled ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled]);
    drawIcon(draw, icon, corner, size, colour);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return clicked && enabled;
}

} // namespace e2d
