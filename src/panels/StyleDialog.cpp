#include "panels/StyleDialog.h"
#include "panels/SymbolImage.h"
#include "window/FileDialog.h"
#include "view/SymbolTextures.h"

#include "render/symbolize.hpp"     // defaultStyle
#include "style/ysld_reader.hpp"
#include "style/ysld_writer.hpp"
#include "style/symbol_folders.hpp"   // §78: symbolUrl

#include <imgui.h>
#include <imgui_stdlib.h>          // InputText on a std::string

#include <algorithm>
#include <cfloat>   // FLT_MIN: the width "to the right edge" (ImGui)
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace e2d {

namespace {

namespace fs = std::filesystem;

const ImVec4 kRed(1.0f, 0.45f, 0.40f, 1.0f);

// §45: the symbolizers that draw something on this kind of layer.
std::vector<r2d::SymbolizerKind> kindsFor(const std::string& geometry)
{
    using K = r2d::SymbolizerKind;
    if (geometry == "raster")
        return {K::Raster};   // §66: its pixels, nothing else
    if (geometry == "point")
        return {K::Point, K::Text};
    if (geometry == "line")
        return {K::Line, K::Text};
    if (geometry == "polygon")
        return {K::Polygon, K::Line, K::Text};
    return {K::Polygon, K::Line, K::Point, K::Text};   // mixed or unknown: all
}

// The up and down arrows, then "x", on a tree node's line. Returns -1, +1, 2 (x) or 0.
int nodeButtons(std::size_t index, std::size_t count)
{
    int asked = 0;
    ImGui::SameLine();
    ImGui::BeginDisabled(index == 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        asked = -1;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("move up: drawn earlier");
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= count);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        asked = 1;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("move down: drawn later, on top");
    ImGui::SameLine();
    if (ImGui::SmallButton("x"))
        asked = 2;
    ImGui::SetItemTooltip("remove");
    return asked;
}

// §66: what render2d does without a style for a raster, as a style to edit.
r2d::Style rasterStyle()
{
    r2d::Rule rule;
    rule.name = "default";
    rule.symbolizers.push_back(r2d::RasterSymbolizer{});
    r2d::Style s;
    s.name = "default";
    s.featureStyles.push_back(r2d::FeatureTypeStyle{{rule}});
    return s;
}

r2d::GeomType typeOf(const std::string& geometry)
{
    if (geometry == "point")
        return r2d::GeomType::Point;
    if (geometry == "line")
        return r2d::GeomType::Line;
    return r2d::GeomType::Polygon;
}

// One row of the dialog: the label in a column on the left, then the widget
// (its own label hidden), as in the sketch of notions §42.
void label(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
}

// A colour, with its opacity (the alpha bar), as #RRGGBBAA. True when changed.
bool colourField(const char* text, r2d::Color& c)
{
    label(text);
    float v[4] = {static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), static_cast<float>(c.a)};
    ImGui::PushID(text);
    const bool edited = ImGui::ColorEdit4("##c", v, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex |
                                                        ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::PopID();
    if (edited)
        c = {v[0], v[1], v[2], v[3]};
    return edited;
}

// A length in pixels, dragged or typed (Ctrl+click to type).
bool pixels(const char* text, double& v, double max = 200.0)
{
    label(text);
    const double lo = 0.0;
    ImGui::PushID(text);
    const bool edited = ImGui::DragScalar("##v", ImGuiDataType_Double, &v, 0.05f, &lo, &max, "%.2f px");
    ImGui::PopID();
    return edited;
}

// An opacity from 0 to 1, shown in percent (§66).
bool opacity(const char* text, double& v)
{
    label(text);
    float percent = static_cast<float>(v * 100.0);
    ImGui::PushID(text);
    const bool edited = ImGui::SliderFloat("##v", &percent, 0.0f, 100.0f, "%.0f %%");
    ImGui::PopID();
    if (edited)
        v = std::clamp(static_cast<double>(percent) / 100.0, 0.0, 1.0);
    return edited;
}

// The scale range on one row: 1:[min] to 1:[max], 0 = no limit (YSLD's min, max).
bool scaleRow(r2d::ScaleRange& r)
{
    bool edited = false;
    auto bound = [&](const char* id, double& v, bool isMax) {
        double shown = (isMax && std::isinf(v)) ? 0.0 : v;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        if (ImGui::InputScalar(id, ImGuiDataType_Double, &shown, nullptr, nullptr, "%.0f")) {
            shown = std::max(0.0, shown);
            v = (isMax && shown == 0.0) ? std::numeric_limits<double>::infinity() : shown;
            edited = true;
        }
    };
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("scale");
    ImGui::SameLine(ImGui::GetFontSize() * 11.0f);
    ImGui::TextUnformatted("1:");
    ImGui::SameLine();
    bound("##min", r.min, false);
    ImGui::SameLine();
    ImGui::TextUnformatted("to 1:");
    ImGui::SameLine();
    bound("##max", r.max, true);
    ImGui::SameLine();
    ImGui::TextDisabled("(0 = no limit)");
    return edited;
}

bool textField(const char* text, std::string& value)
{
    label(text);
    ImGui::PushID(text);
    const bool edited = ImGui::InputText("##t", &value);
    ImGui::PopID();
    return edited;
}

bool checkbox(const char* text, bool& value)
{
    label(text);
    ImGui::PushID(text);
    const bool edited = ImGui::Checkbox("##b", &value);
    ImGui::PopID();
    return edited;
}

const char* kShapes[] = {"circle", "square", "triangle", "star", "cross", "x",
                         "shape://slash", "shape://backslash", "shape://horline",
                         "shape://vertline", "shape://times", "shape://dot"};

bool shapeField(const char* text, r2d::MarkShape& s)
{
    label(text);
    int i = static_cast<int>(s);
    ImGui::PushID(text);
    const bool edited = ImGui::Combo("##s", &i, kShapes, IM_ARRAYSIZE(kShapes));
    ImGui::PopID();
    if (!edited)
        return false;
    s = static_cast<r2d::MarkShape>(i);
    return true;
}

// "8 4" <-> {8, 4}.
std::string dashesText(const std::vector<double>& d)
{
    std::string s;
    for (double x : d) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", x);
        s += (s.empty() ? "" : " ") + std::string(buf);
    }
    return s;
}

} // namespace

void StyleDialog::open(const std::string& layerId)
{
    m_open   = true;
    m_layer  = layerId;
    m_loaded = false;   // read again from the layer
    ImGui::SetWindowFocus("###style");
}

void StyleDialog::reset()
{
    m_classify.reset();
    m_open    = false;
    m_loaded  = false;
    m_pending = false;
    m_layer.clear();
    m_typed.clear();
    m_errors.clear();
    m_saved.clear();
    m_backedUp.clear();
}

// The copy to edit: the text the dialog sent last (if any), else the file,
// else render2d's default style for this geometry and colour (§38).
void StyleDialog::load(const LayerEntry& layer)
{
    m_typed.clear();
    m_errors.clear();
    m_pending = false;
    std::optional<r2d::Style> s;
    if (!layer.styleText.empty()) {
        r2d::YsldResult r = r2d::parseYsld(layer.styleText);
        s = std::move(r.style);
    } else if (!layer.style.empty()) {
        r2d::YsldResult r = r2d::readYsldFile(layer.style);
        s = std::move(r.style);
    }
    m_style = s ? *s : layer.geometry == "raster" ? rasterStyle() : r2d::defaultStyle(typeOf(layer.geometry), layer.colour);
    m_loadedText = layer.styleText;
    m_loaded     = true;
}

bool StyleDialog::unsaved(const LayerEntry& layer) const
{
    if (layer.styleText.empty())
        return false;   // the file's style
    const auto saved = m_saved.find(layer.id);
    return saved == m_saved.end() || saved->second != layer.styleText;
}

StyleDialogResult StyleDialog::draw(const Document& doc, Commander& commander, const std::string& configPath,
                                    GLFWwindow* window, SymbolTextures* textures)
{
    StyleDialogResult out;
    if (!m_open)
        return out;
    const int li = doc.findLayer(m_layer);
    if (li < 0) {   // the layer was removed
        m_open = false;
        return out;
    }
    const LayerEntry& layer = doc.layers[static_cast<std::size_t>(li)];
    // Changed elsewhere (Ctrl+Z, a script): the copy follows, unless we are editing it.
    if (!m_loaded || (layer.styleText != m_loadedText && !m_pending))
        load(layer);
    m_geometry = layer.geometry;
    m_colour   = layer.colour;
    m_fields   = layer.fields;
    m_numeric  = layer.numeric;
    m_numeric.resize(m_fields.size(), false);
    m_window       = window;
    m_textures     = textures;
    m_imageBase    = imageBase(layer.style, configPath);
    m_styleHasFile = !layer.style.empty();

    // In proportion to the text: wide enough at 150 % (§7) for the scale row.
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 46.0f, em * 34.0f), ImGuiCond_FirstUseEver);
    const std::string title = "Style - " + m_layer + "###style";
    if (!ImGui::Begin(title.c_str(), &m_open)) {
        ImGui::End();
        return out;
    }

    // The tree of the style, in a zone that scrolls: a long style (a
    // categorized one, 30 rules) does not push Save and Revert out of sight.
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    m_edit = {};
    bool classified = false;
    // §50: two tabs on the same copy of the style; Save and Revert below, common.
    if (ImGui::BeginTabBar("tabs")) {
        const ImGuiTabItemFlags rulesFlags = m_showRules ? ImGuiTabItemFlags_SetSelected : 0;
        m_showRules = false;
        if (ImGui::BeginTabItem("Rules", nullptr, rulesFlags)) {
    ImGui::BeginChild("tree", ImVec2(0.0f, -footer));
    if (ImGui::SmallButton("+ rule"))
        m_edit.kind = Edit::AddRule;
    ImGui::SetItemTooltip("a new rule, drawn last (on top), in the layer's colour");
    const bool severalPasses = m_style.featureStyles.size() > 1;
    for (std::size_t p = 0; p < m_style.featureStyles.size(); ++p) {
        ImGui::PushID(static_cast<int>(p));
        if (severalPasses)
            ImGui::SeparatorText(("pass " + std::to_string(p + 1)).c_str());
        const std::size_t n = m_style.featureStyles[p].rules.size();
        for (std::size_t r = 0; r < n; ++r)
            drawRule(p, r, n);
        ImGui::PopID();
    }
    ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (layer.geometry != "raster" && ImGui::BeginTabItem("Classify")) {   // a raster has no attribute
            ImGui::BeginChild("classify", ImVec2(0.0f, -footer));
            classified = m_classify.draw(layer, m_style);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    // §50: Apply replaced the rules: one gesture, then the Rules tab shows them.
    if (classified) {
        m_typed.clear();
        m_errors.clear();
        changed();   // sent at the next frame where nothing is active (§42)
        m_showRules = true;
    }
    // §45: the shape changes after the tree is drawn, never while it is.
    if (m_edit.kind != Edit::None) {
        apply(m_edit);
        m_typed.clear();    // the fields are keyed by position: positions moved
        m_errors.clear();
        changed();
    }

    if (!m_notice.empty()) {
        out.notice = m_notice;
        m_notice.clear();
    }

    // §42: one command per gesture: when nothing is being dragged or typed any more.
    if (m_pending && !ImGui::IsAnyItemActive())
        commit(layer, commander, out);

    // §43: Save, Revert, and where the style lives.
    ImGui::Separator();
    if (ImGui::Button("Save")) {
        if (m_pending)
            commit(layer, commander, out);   // the last change first
        save(layer, configPath, out);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(layer.styleText.empty());
    if (ImGui::Button("Revert")) {   // back to the file: a command too, undoable
        std::string error;
        commander.run({{"name", "layer.setStyle"}, {"layer", m_layer}, {"ysld", ""}}, error);
        if (!error.empty())
            out.notice = error;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const std::string file = layer.style.empty() ? std::string("(default style, no file)")
                                                 : fs::path(layer.style).filename().string();
    ImGui::TextDisabled("%s%s", file.c_str(), unsaved(layer) ? "  (changes not saved)" : "");

    ImGui::End();
    return out;
}

void StyleDialog::drawRule(std::size_t pass, std::size_t r, std::size_t count)
{
    r2d::Rule& rule = m_style.featureStyles[pass].rules[r];
    const std::string key = "p" + std::to_string(pass) + "r" + std::to_string(r);
    m_at = {};
    m_at.pass = pass;
    m_at.rule = r;
    ImGui::PushID(key.c_str());
    const std::string head = "rule '" + (rule.name.empty() ? std::string("-") : rule.name) + "'###rule";
    const bool open = ImGui::TreeNodeEx(head.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    // §45: arrows, x, and the "+" of the rule.
    if (const int b = nodeButtons(r, count); b != 0) {
        m_edit = m_at;
        m_edit.kind  = b == 2 ? Edit::RemoveRule : Edit::MoveRule;
        m_edit.delta = b == 2 ? 0 : b;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+"))
        ImGui::OpenPopup("add");
    ImGui::SetItemTooltip("add a filter, else, a symbolizer");
    if (ImGui::BeginPopup("add")) {
        const bool free = !rule.filter && !rule.isElse && !rule.invalid;
        if (ImGui::MenuItem("filter", nullptr, false, free && !m_fields.empty())) {
            m_edit = m_at;
            m_edit.kind = Edit::AddFilter;
        }
        if (ImGui::MenuItem("else", nullptr, false, free)) {
            m_edit = m_at;
            m_edit.kind = Edit::AddElse;
        }
        ImGui::Separator();
        for (r2d::SymbolizerKind k : kindsFor(m_geometry))
            if (ImGui::MenuItem(r2d::kindName(k))) {
                m_edit = m_at;
                m_edit.kind    = Edit::AddSymbolizer;
                m_edit.symKind = k;
            }
        ImGui::EndPopup();
    }

    if (open) {
        if (textField("name", rule.name))
            changed();
        // Scales: 1:min to 1:max, 0 = no limit (§24 of render2d).
        if (scaleRow(rule.scale))
            changed();
        if (rule.isElse) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("else: the features that no other rule draws");
            ImGui::SameLine();
            if (ImGui::SmallButton("x##else")) {
                m_edit = m_at;
                m_edit.kind = Edit::RemoveElse;
            }
        } else if (rule.filter || rule.invalid) {
            exprField("filter", key + ".filter", rule.filter, true, false, Fields::All, "filter");
        }
        if (rule.invalid && !rule.filter)
            ImGui::TextColored(kRed, "this filter does not read: the rule is never applied");

        const std::size_t n = rule.symbolizers.size();
        for (std::size_t s = 0; s < n; ++s)
            drawSymbolizer(pass, r, s, n);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void StyleDialog::removeButton(const std::string& property)
{
    ImGui::SameLine();
    if (ImGui::SmallButton(("x##" + property).c_str())) {
        m_edit = m_at;
        m_edit.kind = Edit::RemoveProperty;
        m_edit.key  = property;
    }
    ImGui::SetItemTooltip("remove");
}

void StyleDialog::drawSymbolizer(std::size_t pass, std::size_t r, std::size_t si, std::size_t count)
{
    r2d::Symbolizer& sym = m_style.featureStyles[pass].rules[r].symbolizers[si];
    const std::string key = "p" + std::to_string(pass) + "r" + std::to_string(r) + "s" + std::to_string(si);
    m_at.sym = si;
    ImGui::PushID(key.c_str());
    const r2d::SymbolizerKind kind = r2d::kindOf(sym);
    const bool open = ImGui::TreeNodeEx("##sym", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap,
                                        "%s", r2d::kindName(kind));

    // §45: arrows, x, and the "+" of the properties render2d's catalogue offers.
    if (const int b = nodeButtons(si, count); b != 0) {
        m_edit = m_at;
        m_edit.kind  = b == 2 ? Edit::RemoveSymbolizer : Edit::MoveSymbolizer;
        m_edit.delta = b == 2 ? 0 : b;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+"))
        ImGui::OpenPopup("add");
    ImGui::SetItemTooltip("add a property");
    if (ImGui::BeginPopup("add")) {
        bool any = false;
        for (const r2d::PropertySpec& p : r2d::optionalProperties(kind)) {
            if (!r2d::canAddProperty(sym, p.key))
                continue;
            any = true;
            if (ImGui::MenuItem(p.label.c_str())) {
                m_edit = m_at;
                m_edit.kind = Edit::AddProperty;
                m_edit.key  = p.key;
            }
        }
        if (!any)
            ImGui::TextDisabled("nothing more to add");
        ImGui::EndPopup();
    }
    if (!open) {
        ImGui::PopID();
        return;
    }

    // A stroke's rows. `removable`: its "x" (a polygon's outline); the dashes have theirs.
    // `name`: "stroke", or "hatch line" for the lines of a hatch (not the outline).
    auto stroke = [&](r2d::StrokeStyle& s, const std::string& name, bool removable, bool dashesRemovable) {
        ImGui::PushID(name.c_str());
        if (colourField(name.c_str(), s.color))
            changed();
        if (removable)
            removeButton("stroke");
        if (pixels((name + " width").c_str(), s.width, 50.0))
            changed();
        std::string dashes = dashesText(s.dashes);
        if (!s.dashes.empty()) {
            if (textField("dashes (px)", dashes)) {
                std::vector<double> d;
                std::istringstream in(dashes);
                for (double x; in >> x;)
                    if (x > 0.0)
                        d.push_back(x);
                if (!d.empty()) {
                    s.dashes = d;
                    changed();
                }
            }
            if (dashesRemovable)
                removeButton("dashes");
        }
        ImGui::PopID();
    };

    std::visit(
        [&](auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, r2d::PolygonSymbolizer>) {
                if (s.fill) {
                    if (colourField("fill", s.fill->color))
                        changed();
                    removeButton("fill");
                }
                if (s.graphicFill) {
                    ImGui::PushID("hatch");
                    if (shapeField("hatch", s.graphicFill->shape))
                        changed();
                    ImGui::PopID();
                    removeButton("hatch");
                    ImGui::PushID("hatch");
                    if (pixels("hatch tile", s.graphicFill->size, 100.0))
                        changed();
                    if (s.graphicFill->stroke)
                        stroke(*s.graphicFill->stroke, "hatch line", false, false);
                    ImGui::PopID();
                }
                if (s.stroke)
                    stroke(*s.stroke, "stroke", true, true);
            } else if constexpr (std::is_same_v<T, r2d::LineSymbolizer>) {
                stroke(s.stroke, "stroke", false, true);
            } else if constexpr (std::is_same_v<T, r2d::PointSymbolizer>) {
                if (s.sizeExpr) {
                    exprField("size (expression)", key + ".size", s.sizeExpr, false, false, Fields::Numbers,
                              "sizeField");
                } else {
                    if (pixels(s.metres ? "size (m)" : "size", s.size, 500.0))
                        changed();
                }
                if (s.metres)
                    removeButton("metres");
                if (s.rotation)
                    exprField("rotation", key + ".rotation", s.rotation, false, false, Fields::Numbers, "rotation");
                // §78: the image first, the mark under it: SLD draws the first
                // symbol it can, so the mark is what shows when the image cannot.
                imageRow(key, s.image);
                // With an image, the mark is only its fallback (drawn when the
                // image cannot be read): folded, closed at first, so that its
                // colours are not taken for the image's (§78, 0.6.11).
                const bool markShown = s.image.empty() ||
                                       ImGui::TreeNodeEx("fallback mark", ImGuiTreeNodeFlags_None);
                if (!s.image.empty())
                    ImGui::SetItemTooltip("drawn when the image cannot be read (a map sent without its symbols)");
                if (markShown) {
                    if (shapeField("shape", s.shape))
                        changed();
                    if (s.fill && colourField("fill", s.fill->color))
                        changed();
                    if (s.stroke)
                        stroke(*s.stroke, "stroke", false, false);
                    if (!s.image.empty())
                        ImGui::TreePop();
                }
            } else if constexpr (std::is_same_v<T, r2d::TextSymbolizer>) {
                exprField("label", key + ".label", s.label, false, true, Fields::All, "");
                if (textField("font", s.font.family))
                    changed();
                if (pixels("font size", s.font.size, 100.0))
                    changed();
                if (checkbox("bold", s.font.bold))
                    changed();
                if (checkbox("italic", s.font.italic))
                    changed();
                if (colourField("text", s.fill))
                    changed();
                if (s.halo) {
                    if (colourField("halo", s.halo->color))
                        changed();
                    removeButton("halo");
                    if (pixels("halo radius", s.halo->radius, 20.0))
                        changed();
                }
                if (s.rotation)
                    exprField("rotation", key + ".rotation", s.rotation, false, false, Fields::Numbers, "rotation");
            } else if constexpr (std::is_same_v<T, r2d::RasterSymbolizer>) {
                if (opacity("opacity", s.opacity))
                    changed();
            }
        },
        sym);

    ImGui::TreePop();
    ImGui::PopID();
}

// §45: a text gets a label at once, the first field; else the catalogue's look.
r2d::Symbolizer StyleDialog::newSymbolizer(r2d::SymbolizerKind kind) const
{
    r2d::Symbolizer s = r2d::newSymbolizer(kind, m_colour);
    if (auto* t = std::get_if<r2d::TextSymbolizer>(&s)) {
        std::string error;
        if (!m_fields.empty())
            t->label = r2d::Expression::parseValue(m_fields.front(), error);
        if (!t->label)
            t->label = r2d::Expression::literalText("label");
    }
    return s;
}

void StyleDialog::apply(const Edit& e)
{
    auto& passes = m_style.featureStyles;
    if (e.kind == Edit::AddRule) {
        if (passes.empty())
            passes.emplace_back();
        auto& rules = passes.back().rules;
        r2d::Rule rule;
        rule.name = "rule " + std::to_string(rules.size() + 1);
        rule.symbolizers.push_back(newSymbolizer(kindsFor(m_geometry).front()));
        rules.push_back(std::move(rule));
        return;
    }
    if (e.pass >= passes.size() || e.rule >= passes[e.pass].rules.size())
        return;
    auto& rules = passes[e.pass].rules;
    r2d::Rule& rule = rules[e.rule];
    auto& syms = rule.symbolizers;
    // Take the element out, put it back one place further. Not std::swap:
    // GCC 16 warns about swapping variants (a false "may be used uninitialized").
    auto move = [](auto& list, std::size_t i, int delta) {
        const long j = static_cast<long>(i) + delta;
        if (j < 0 || j >= static_cast<long>(list.size()))
            return;
        auto item = std::move(list[i]);
        list.erase(list.begin() + static_cast<long>(i));
        list.insert(list.begin() + j, std::move(item));
    };
    switch (e.kind) {
    case Edit::RemoveRule:
        rules.erase(rules.begin() + static_cast<long>(e.rule));
        break;
    case Edit::MoveRule:
        move(rules, e.rule, e.delta);
        break;
    case Edit::AddSymbolizer:
        syms.push_back(newSymbolizer(e.symKind));
        break;
    case Edit::AddFilter: {
        // A filter that keeps every feature with a value: true at once, adjusted afterwards.
        std::string error;
        rule.filter = r2d::Expression::parseCondition(m_fields.front() + " IS NOT NULL", error);
        break;
    }
    case Edit::RemoveFilter:
        rule.filter.reset();
        rule.invalid = false;
        rule.invalidFilter.clear();
        break;
    case Edit::AddElse:
        rule.isElse = true;
        break;
    case Edit::RemoveElse:
        rule.isElse = false;
        break;
    default:
        if (e.sym >= syms.size())
            return;
        if (e.kind == Edit::RemoveSymbolizer)
            syms.erase(syms.begin() + static_cast<long>(e.sym));
        else if (e.kind == Edit::MoveSymbolizer)
            move(syms, e.sym, e.delta);
        else if (e.kind == Edit::AddProperty)
            r2d::addProperty(syms[e.sym], e.key);
        else if (e.kind == Edit::RemoveProperty)
            r2d::removeProperty(syms[e.sym], e.key);
        break;
    }
}

// §42: the text is checked by render2d's parser as it is typed; the model
// changes only when it reads. §46: a list of the fields that fit.
void StyleDialog::exprField(const char* label, const std::string& key, std::optional<r2d::Expression>& target,
                            bool condition, bool literal, Fields fields, const std::string& remove)
{
    auto it = m_typed.find(key);
    if (it == m_typed.end()) {
        std::string shown;
        if (target)
            shown = (literal && !target->isLiteralText()) ? "${" + target->text() + "}" : target->text();
        it = m_typed.emplace(key, shown).first;
    }
    const bool bad = m_errors.count(key) > 0;
    if (bad)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
    e2d::label(label);
    ImGui::PushID(key.c_str());
    bool edited = ImGui::InputText("##e", &it->second);
    if (bad)
        ImGui::PopStyleColor();

    // §46: pick a field: it replaces the text (then complete by hand: 90 - CARTOANGLE).
    if (fields != Fields::None) {
        ImGui::SameLine();
        if (ImGui::SmallButton("fields"))
            ImGui::OpenPopup("fields");
        ImGui::SetItemTooltip(fields == Fields::Numbers ? "the numeric fields of the layer" : "the fields of the layer");
        if (ImGui::BeginPopup("fields")) {
            bool any = false;
            for (std::size_t f = 0; f < m_fields.size(); ++f) {
                if (fields == Fields::Numbers && !m_numeric[f])
                    continue;
                any = true;
                if (ImGui::MenuItem(m_fields[f].c_str())) {
                    it->second = literal ? "${" + m_fields[f] + "}"
                               : condition ? m_fields[f] + " IS NOT NULL" : m_fields[f];
                    edited = true;
                }
            }
            if (!any)
                ImGui::TextDisabled(fields == Fields::Numbers ? "no numeric field" : "no field");
            ImGui::EndPopup();
        }
    }
    ImGui::PopID();
    if (!remove.empty()) {
        if (remove == "filter") {
            ImGui::SameLine();
            if (ImGui::SmallButton("x##filter")) {
                m_edit = m_at;
                m_edit.kind = Edit::RemoveFilter;
            }
            ImGui::SetItemTooltip("remove the filter: every feature");
        } else {
            removeButton(remove);
        }
    }

    if (edited) {
        std::string text = it->second, error;
        std::optional<r2d::Expression> e;
        const bool braces = text.size() >= 3 && text.compare(0, 2, "${") == 0 && text.back() == '}';
        if (braces)
            text = text.substr(2, text.size() - 3);
        if (literal && !braces)
            e = r2d::Expression::literalText(text);   // a fixed text: Evere
        else
            e = condition ? r2d::Expression::parseCondition(text, error) : r2d::Expression::parseValue(text, error);
        if (e) {
            target = std::move(e);
            m_errors.erase(key);
            changed();
        } else {
            m_errors[key] = error;
        }
    }
    if (const auto err = m_errors.find(key); err != m_errors.end())
        ImGui::TextColored(kRed, "%s", err->second.c_str());
}

// §78: one row: the path as the style will hold it, typed (Enter) or chosen
// with "..." (Windows' Open window); "x" removes it. A path that does not
// lead to a PNG or an SVG render2d can read is refused, in red, the style
// unchanged.
void StyleDialog::imageRow(const std::string& key, std::string& image)
{
    const std::string id = key + ".image";
    auto typed = m_typed.find(id);
    if (typed == m_typed.end())
        typed = m_typed.emplace(id, imageShown(image, m_imageBase)).first;

    std::optional<std::string> wanted;   // a path to check: typed or chosen
    // The path on its own row, as wide as the panel allows; the buttons on
    // the row below, under it: a narrow docked panel cuts nothing (0.6.11).
    label("image");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID("image");
    ImGui::InputTextWithHint("##path", "none: the mark", &typed->second);
    const float fieldX = ImGui::GetItemRectMin().x;
    if (ImGui::IsItemDeactivatedAfterEdit())
        wanted = typed->second;
    ImGui::SetItemTooltip("%s", image.empty() ? "a .svg or .png file: beside the style (icons/tree.svg)\n"
                                                "or a symbol library's name (nature/park.svg)"
                                              : image.c_str());
    // §78: the library's symbols, as thumbnails.
    ImGui::SetCursorScreenPos(ImVec2(fieldX, ImGui::GetCursorScreenPos().y));
    if (ImGui::SmallButton("Symbols...")) {
        m_families = libraryFamilies();
        ImGui::OpenPopup("symbols");
    }
    ImGui::SetItemTooltip("choose in the symbol library");
    if (const auto picked = symbolPicker(image))
        wanted = *picked;
    if (fileDialogAvailable()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("..."))
            if (const auto chosen = chooseImageFile(m_window, imageStartFolder(image, m_imageBase)))
                wanted = *chosen;
        ImGui::SetItemTooltip("choose an SVG or PNG file");
    }
    bool removed = false;
    if (!image.empty()) {
        ImGui::SameLine();
        removed = ImGui::SmallButton("x");
        ImGui::SetItemTooltip("no image: the mark");
    }
    ImGui::PopID();

    if (removed || (wanted && wanted->empty())) {
        image.clear();
        typed->second.clear();
        m_errors.erase(id);
        changed();
    } else if (wanted) {
        const ImageCheck c = checkImage(*wanted, m_imageBase, m_styleHasFile);
        if (c.error.empty()) {
            image         = c.path;
            typed->second = imageShown(image, m_imageBase);
            m_errors.erase(id);
            if (c.outside)
                m_notice = "the image is neither beside the style nor in a symbol library: the style will hold its whole path";
            changed();
        } else {
            m_errors[id] = c.error;
        }
    }
    if (const auto err = m_errors.find(id); err != m_errors.end())
        ImGui::TextColored(kRed, "%s", err->second.c_str());
}

std::optional<std::string> StyleDialog::symbolPicker(const std::string& current)
{
    std::optional<std::string> picked;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 30.0f, em * 22.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("symbols"))
        return picked;
    if (m_families.empty()) {
        ImGui::TextDisabled("no symbol library found: symbols/ beside the program (render2d notes §76)");
    } else if (ImGui::BeginTabBar("families", ImGuiTabBarFlags_FittingPolicyScroll)) {
        // The thumbnails: 2 em (32 px at 150 %), drawn by render2d once and kept.
        const float side = std::round(em * 2.0f);
        const std::string currentPath = current.empty() ? std::string() : fs::path(current).lexically_normal().generic_string();
        for (const SymbolFamily& family : m_families) {
            // The current symbol's family opens first.
            const bool here = std::any_of(family.files.begin(), family.files.end(),
                                          [&](const std::string& f) { return f == currentPath; });
            if (!ImGui::BeginTabItem(family.name.c_str(), nullptr, here && ImGui::IsWindowAppearing()
                                                                         ? ImGuiTabItemFlags_SetSelected : 0))
                continue;
            ImGui::BeginChild("grid");
            const float cell = side + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
            const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
            for (std::size_t i = 0; i < family.files.size(); ++i) {
                const std::string& file = family.files[i];
                LayerSymbol s;
                s.kind  = LayerSymbol::Kind::Mark;
                s.image = file;
                s.size  = 1e6;   // as large as the square allows (90 %)
                const unsigned int texture = m_textures ? m_textures->get(s, static_cast<int>(side), 1.0f) : 0u;
                if (i % static_cast<std::size_t>(columns) != 0)
                    ImGui::SameLine();
                ImGui::PushID(static_cast<int>(i));
                const bool chosen = file == currentPath;
                if (chosen)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                const bool clicked = texture != 0
                                       ? ImGui::ImageButton("##s", static_cast<ImTextureID>(texture), ImVec2(side, side))
                                       : ImGui::Button(fs::path(file).stem().string().c_str(), ImVec2(side, side));
                if (chosen)
                    ImGui::PopStyleColor();
                // The name the style will hold: "nature/tree.svg".
                ImGui::SetItemTooltip("%s", r2d::symbolUrl(file, "").c_str());
                if (clicked) {
                    picked = file;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndPopup();
    return picked;
}

void StyleDialog::commit(const LayerEntry& layer, Commander& commander, StyleDialogResult& out)
{
    m_pending = false;
    // Image paths stay absolute in the text: the threads read it with no folder (§41).
    const std::string text = r2d::writeYsld(m_style);
    m_loadedText = text;
    std::string error;
    if (!commander.run({{"name", "layer.setStyle"}, {"layer", layer.id}, {"ysld", text}}, error)) {
        out.notice = error;
        m_loaded   = false;   // refused: back to what the layer has
    }
}

// §43: the layer's style file, .bak first; a layer without one gets
// styles/<id>.yaml next to viewer.yaml (or next to its data). The dialog's
// layer is written from the copy; another one from its text.
bool StyleDialog::save(const LayerEntry& layer, const std::string& configPath, StyleDialogResult& out)
{
    r2d::Style style;
    if (layer.id == m_layer && m_loaded) {
        style = m_style;
    } else {
        r2d::YsldResult r = r2d::parseYsld(layer.styleText);
        if (!r.style) {
            out.notice = "cannot save " + layer.id + ": " + r.error;
            return false;
        }
        style = std::move(*r.style);
    }
    fs::path path = layer.style;
    if (path.empty()) {
        const fs::path base = !configPath.empty() ? fs::path(configPath).parent_path()
                                                  : fs::path(layer.source.substr(0, layer.source.rfind("::"))).parent_path();
        std::string name = layer.id;
        for (char& c : name)
            if (c == '#' || c == ':' || c == '/' || c == '\\')
                c = '_';
        path = base / "styles" / (name + ".yaml");
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (fs::exists(path) && !m_backedUp.count(path.string())) {
        fs::copy_file(path, fs::path(path.string() + ".bak"), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            out.notice = "cannot back up " + path.string() + ": " + ec.message() + " (not saved)";
            return false;
        }
        m_backedUp.insert(path.string());
    }
    std::ofstream file(path, std::ios::binary);
    file << r2d::writeYsld(style, path.parent_path().string());
    if (!file) {
        out.notice = "cannot write " + path.string();
        return false;
    }
    m_saved[layer.id] = layer.styleText;
    out.notice = "saved " + path.generic_string();
    if (layer.style.empty())
        out.savedFiles.emplace_back(layer.id, path.generic_string());
    return true;
}

bool StyleDialog::saveAll(const Document& doc, Commander& commander, const std::string& configPath,
                          StyleDialogResult& out)
{
    if (m_pending)
        if (const int li = doc.findLayer(m_layer); li >= 0)
            commit(doc.layers[static_cast<std::size_t>(li)], commander, out);
    bool ok = true;
    for (const LayerEntry& layer : doc.layers)
        if (unsaved(layer))
            ok = save(layer, configPath, out) && ok;
    return ok;
}

} // namespace e2d
