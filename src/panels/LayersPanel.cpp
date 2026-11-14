#include "panels/LayersPanel.h"
#include "view/SymbolTextures.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace e2d {

namespace {

constexpr const char* kPayload = "E2D_LAYER";   // the drag-and-drop payload: a layer index
constexpr float       kPi      = 3.14159265f;

ImU32 colourOf(const r2d::Color& c, float alpha)
{
    return ImGui::GetColorU32(ImVec4(float(c.r), float(c.g), float(c.b), float(c.a) * alpha));
}

// The points of a closed mark, in a unit frame: centre (0, 0), radius 1,
// y down (the screen). Empty for the marks made of strokes only.
std::vector<ImVec2> markOutline(r2d::MarkShape shape)
{
    std::vector<ImVec2> p;
    switch (shape) {
    case r2d::MarkShape::Circle:
    case r2d::MarkShape::Dot:
        for (int k = 0; k < 20; ++k)
            p.push_back({std::cos(k * 2 * kPi / 20), std::sin(k * 2 * kPi / 20)});
        break;
    case r2d::MarkShape::Square:
        p = {{-0.8f, -0.8f}, {0.8f, -0.8f}, {0.8f, 0.8f}, {-0.8f, 0.8f}};
        break;
    case r2d::MarkShape::Triangle:   // its tip to the north, as on the map (§41)
        p = {{0.0f, -1.0f}, {0.87f, 0.5f}, {-0.87f, 0.5f}};
        break;
    case r2d::MarkShape::Star:       // 5 points, the inner ones at 0.38
        for (int k = 0; k < 10; ++k) {
            const float r = k % 2 ? 0.38f : 1.0f, a = -kPi / 2 + k * kPi / 5;
            p.push_back({r * std::cos(a), r * std::sin(a)});
        }
        break;
    default:
        break;
    }
    return p;
}

// The symbol (notions §68), in the square [corner, corner + size]. `ratio`:
// the screen's pixel ratio, as for the map (§7); `alpha`: < 1 for a layer
// out of scale, grey like its name.
void drawSymbol(ImDrawList* draw, ImVec2 corner, float size, const LayerSymbol& s, float ratio, float alpha)
{
    auto at = [&](float x, float y) { return ImVec2(corner.x + x * size, corner.y + y * size); };
    // A width from the style, kept readable: at least 1 px, at most a fifth of the square.
    auto width = [&](double w) { return std::clamp(float(w) * ratio, 1.0f, size * 0.2f); };

    switch (s.kind) {
    case LayerSymbol::Kind::None:
        break;

    case LayerSymbol::Kind::Fill: {
        const ImVec2 a = at(0.12f, 0.2f), b = at(0.88f, 0.8f);
        if (s.fill)
            draw->AddRectFilled(a, b, colourOf(*s.fill, alpha));
        if (s.hatch) {   // a few strokes of the hatching, in its colour, inside the rectangle
            const r2d::Color c = s.hatch->stroke ? s.hatch->stroke->color
                               : s.hatch->fill   ? s.hatch->fill->color : r2d::Color{};
            draw->PushClipRect(a, b, true);
            const bool back = s.hatch->shape == r2d::MarkShape::Backslash;
            for (int k = -2; k <= 3; ++k) {
                const float x = a.x + k * size * 0.25f;
                if (back)
                    draw->AddLine({x, a.y}, {x + (b.y - a.y), b.y}, colourOf(c, alpha), 1.0f);
                else
                    draw->AddLine({x, b.y}, {x + (b.y - a.y), a.y}, colourOf(c, alpha), 1.0f);
            }
            draw->PopClipRect();
        }
        if (s.stroke)
            draw->AddRect(a, b, colourOf(*s.stroke, alpha), 0.0f, 0, width(s.strokeWidth));
        break;
    }

    case LayerSymbol::Kind::Line: {
        // A broken line, so a thin grey line is not mistaken for an underline.
        const ImVec2 p[] = {at(0.08f, 0.72f), at(0.38f, 0.32f), at(0.62f, 0.68f), at(0.92f, 0.28f)};
        if (s.casing)   // §83: the wider line under it (the grey border of an OSM road)
            draw->AddPolyline(p, 4, colourOf(*s.casing, alpha), 0,
                              std::max(width(s.casingWidth), width(s.strokeWidth) + 2.0f));
        if (s.stroke)
            draw->AddPolyline(p, 4, colourOf(*s.stroke, alpha), 0, width(s.strokeWidth));
        break;
    }

    case LayerSymbol::Kind::Mark: {
        const ImVec2 c = at(0.5f, 0.5f);
        // Its size on the map, kept between 40 % and 84 % of the square.
        const float r = std::clamp(float(s.size) * ratio / 2.0f, size * 0.2f, size * 0.42f);
        const std::vector<ImVec2> unit = markOutline(s.shape);
        if (!unit.empty()) {
            std::vector<ImVec2> p;
            for (const ImVec2& u : unit)
                p.push_back({c.x + u.x * r, c.y + u.y * r});
            if (s.fill)
                draw->AddConcavePolyFilled(p.data(), int(p.size()), colourOf(*s.fill, alpha));
            if (s.stroke)
                draw->AddPolyline(p.data(), int(p.size()), colourOf(*s.stroke, alpha), ImDrawFlags_Closed,
                                  width(s.strokeWidth));
        } else {   // cross, x, lines: strokes, in the outline's colour (else the fill's)
            const std::optional<r2d::Color> k = s.stroke ? s.stroke : s.fill;
            if (!k)
                break;
            const ImU32 col = colourOf(*k, alpha);
            const float w = std::max(width(s.strokeWidth), 1.5f);
            auto seg = [&](float x0, float y0, float x1, float y1) {
                draw->AddLine({c.x + x0 * r, c.y + y0 * r}, {c.x + x1 * r, c.y + y1 * r}, col, w);
            };
            switch (s.shape) {
            case r2d::MarkShape::Cross:     seg(-1, 0, 1, 0); seg(0, -1, 0, 1); break;
            case r2d::MarkShape::HorLine:   seg(-1, 0, 1, 0); break;
            case r2d::MarkShape::VertLine:  seg(0, -1, 0, 1); break;
            case r2d::MarkShape::Slash:     seg(-0.7f, 0.7f, 0.7f, -0.7f); break;
            case r2d::MarkShape::Backslash: seg(-0.7f, -0.7f, 0.7f, 0.7f); break;
            default:                        seg(-0.7f, -0.7f, 0.7f, 0.7f); seg(-0.7f, 0.7f, 0.7f, -0.7f); break;   // X, Times
            }
        }
        break;
    }

    case LayerSymbol::Kind::Text: {   // labels only: an "a" in their colour
        const r2d::Color k = s.fill ? *s.fill : r2d::Color{};
        ImFont* font = ImGui::GetFont();
        const float h = size * 0.9f;
        const ImVec2 t = font->CalcTextSizeA(h, 1e9f, 0.0f, "a");
        draw->AddText(font, h, {corner.x + (size - t.x) / 2, corner.y + (size - t.y) / 2}, colourOf(k, alpha), "a");
        break;
    }

    case LayerSymbol::Kind::Raster: {   // a grey ramp, faded by the opacity
        const float o = float(s.opacity) * alpha;
        const ImU32 dark  = ImGui::GetColorU32(ImVec4(0.25f, 0.25f, 0.25f, o));
        const ImU32 light = ImGui::GetColorU32(ImVec4(0.85f, 0.85f, 0.85f, o));
        draw->AddRectFilledMultiColor(at(0.12f, 0.2f), at(0.88f, 0.8f), dark, light, light, dark);
        break;
    }
    }
}

// A symbol in the square at `corner`: an image (SVG, PNG) drawn by render2d
// as on the map (§78), else by drawSymbol. Then the square is taken.
void symbolCell(const LayerSymbol& symbol, float side, float pixelRatio, float alpha, SymbolTextures* textures)
{
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const unsigned int texture =
        symbol.image.empty() || !textures ? 0u : textures->get(symbol, static_cast<int>(std::lround(side)), pixelRatio);
    if (texture != 0)
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(texture), corner,
                                             ImVec2(corner.x + side, corner.y + side), ImVec2(0, 0), ImVec2(1, 1),
                                             ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)));
    else
        drawSymbol(ImGui::GetWindowDrawList(), corner, side, symbol, pixelRatio, alpha);
    ImGui::Dummy(ImVec2(side, side));
}

// "1,234": a count, its thousands apart.
std::string thousands(std::size_t n)
{
    std::string digits = std::to_string(n), out;
    for (std::size_t k = 0; k < digits.size(); ++k) {
        if (k > 0 && (digits.size() - k) % 3 == 0)
            out += ',';
        out += digits[k];
    }
    return out;
}

} // namespace

LayersPanelResult drawLayersPanel(const Document& doc, const std::map<std::string, bool>& outOfScale,
                                  const std::set<std::string>& unsaved, LayerSymbols& symbols,
                                  double scaleDenominator, float pixelRatio, Commander& commander,
                                  SymbolTextures* textures, const std::map<std::string, LayerCounts>* counts)
{
    LayersPanelResult result;
    ImGui::Begin("Layers");   // docked on the left by default (Workspace)

    const int n = static_cast<int>(doc.layers.size());
    if (n == 0)
        ImGui::TextDisabled("no layer: drop files on the window (§39)");

    // §26, §39: the name takes what is left; the type keeps the width of
    // "polygon", so a long name never cuts it.
    if (n > 0 && ImGui::BeginTable("layers", 4, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("visible", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("symbol", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());   // §68
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("polygon").x);

        // §18: row i shows layer n - 1 - i (the one drawn last, on top, first).
        bool moved = false;   // a layer.move: the rest of this frame's rows are stale
        for (int row = 0; row < n; ++row) {
            const int i = n - 1 - row;
            const LayerEntry& layer = doc.layers[static_cast<std::size_t>(i)];
            ImGui::PushID(layer.id.c_str());
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            bool visible = layer.visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                // Not layer.visible = ...: a command, undoable, the same as the API's.
                commander.run({{"name", "layer.setVisible"}, {"layer", layer.id}, {"visible", visible}},
                              result.error);
            }

            const auto it = outOfScale.find(layer.id);
            const bool out = layer.visible && it != outOfScale.end() && it->second;
            // §83: what the image shown counted for it (a visible layer, once drawn).
            const LayerCounts* counted = nullptr;
            if (counts && layer.visible)
                if (const auto c = counts->find(layer.id); c != counts->end())
                    counted = &c->second;
            const std::vector<LegendRow> classes = symbols.legend(layer, scaleDenominator);
            const bool unfolds = classes.size() >= 2;

            // §68: the symbol, in the style's colours; faded with the name.
            ImGui::TableNextColumn();
            const float side = ImGui::GetFrameHeight();
            symbolCell(symbols.get(layer, scaleDenominator), side, pixelRatio, out ? 0.35f : 1.0f, textures);

            // The name: grey when visible but drawn by no rule at this scale.
            // §83: an arrow in front when the style has classes; a click on
            // the arrow unfolds them, the name stays for dragging and the menu.
            ImGui::TableNextColumn();
            if (out)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            // "###name": the same item with or without its "*" (§45).
            const std::string shown = layer.id + (unsaved.count(layer.id) ? " *" : "") + "###name";
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (!unfolds)
                flags |= ImGuiTreeNodeFlags_Leaf;   // no arrow, the names stay aligned
            const bool open = ImGui::TreeNodeEx(shown.c_str(), flags) && unfolds;
            if (out)
                ImGui::PopStyleColor();
            const std::string drawnText =
                counted ? "\n" + thousands(counted->drawn) + (counted->drawn == 1 ? " feature" : " features") +
                              " in the view"
                        : std::string();
            ImGui::SetItemTooltip("%s%s%s%s\n(drag to reorder; right click: style, zoom, order, remove)",
                                  layer.id.c_str(), layer.source.empty() ? "" : "\n", layer.source.c_str(),
                                  drawnText.c_str());
            if (out)
                ImGui::SetItemTooltip("%s\nout of scale: no rule of its style at this scale", layer.id.c_str());

            // §39: drag a row onto another: the layer takes that place.
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(kPayload, &i, sizeof i);
                ImGui::TextUnformatted(layer.id.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayload)) {
                    const int from = *static_cast<const int*>(p->Data);
                    if (from != i && from >= 0 && from < n)
                        commander.run({{"name", "layer.move"},
                                       {"layer", doc.layers[static_cast<std::size_t>(from)].id},
                                       {"to", i}},
                                      result.error);
                }
                ImGui::EndDragDropTarget();
            }

            // Right click on the name.
            if (ImGui::BeginPopupContextItem("menu")) {
                if (ImGui::MenuItem("Style..."))
                    result.styleOf = layer.id;
                if (ImGui::MenuItem("Zoom to layer"))
                    result.zoomTo = layer.id;
                // §39: the order, without dragging. Drawing order: 0 at the
                // bottom, n - 1 on top. Each choice is one layer.move.
                if (ImGui::BeginMenu("Order", n > 1)) {
                    int to = -1;
                    if (ImGui::MenuItem("To top", nullptr, false, i < n - 1))
                        to = n - 1;
                    if (ImGui::MenuItem("Up", nullptr, false, i < n - 1))
                        to = i + 1;
                    if (ImGui::MenuItem("Down", nullptr, false, i > 0))
                        to = i - 1;
                    if (ImGui::MenuItem("To bottom", nullptr, false, i > 0))
                        to = 0;
                    if (to >= 0) {
                        commander.run({{"name", "layer.move"}, {"layer", layer.id}, {"to", to}}, result.error);
                        moved = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Remove"))
                    commander.run({{"name", "layer.remove"}, {"layer", layer.id}}, result.error);
                ImGui::EndPopup();
            }

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", layer.geometry.c_str());

            // §83: the classes, one row each: its symbol under the layer's,
            // its name under the layer's name. Grey when the image shown has
            // none of it; the count in the tooltip.
            if (open && !moved && static_cast<int>(doc.layers.size()) == n) {
                for (std::size_t k = 0; k < classes.size(); ++k) {
                    const LegendRow& c = classes[k];
                    ImGui::PushID(static_cast<int>(k));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const std::size_t drawn = counted && counted->filtered ? rowCount(c, counted->rules) : 1;
                    const bool none = out || (counted && counted->filtered && drawn == 0);
                    ImGui::TableNextColumn();
                    symbolCell(c.symbol, side, pixelRatio, none ? 0.35f : 1.0f, textures);
                    ImGui::TableNextColumn();
                    ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                    const std::string& label = c.label.empty() ? layer.id : c.label;
                    ImGui::AlignTextToFramePadding();
                    if (none)
                        ImGui::TextDisabled("%s", label.c_str());
                    else
                        ImGui::TextUnformatted(label.c_str());
                    if (out)
                        ImGui::SetItemTooltip("%s\nout of scale", label.c_str());
                    else if (!layer.visible)
                        ImGui::SetItemTooltip("%s\nthe layer is hidden: nothing counted", label.c_str());
                    else if (counted && counted->filtered)
                        ImGui::SetItemTooltip("%s\n%s", label.c_str(),
                                              drawn == 0 ? "none in the view"
                                                         : (thousands(drawn) + (drawn == 1 ? " feature" : " features") +
                                                            " drawn in the view").c_str());
                    else
                        ImGui::SetItemTooltip("%s", label.c_str());
                    ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                    ImGui::TableNextColumn();
                    ImGui::PopID();
                }
            }

            ImGui::PopID();
            if (moved || static_cast<int>(doc.layers.size()) != n)
                break;   // a command changed the list: the rest of this frame's rows are stale
        }
        ImGui::EndTable();
    }
    ImGui::End();
    return result;
}

} // namespace e2d
