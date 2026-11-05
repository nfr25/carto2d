#include "command/LayerCommands.h"

#include "command/SelectionCommands.h"   // selectionItems

#include <algorithm>
#include <set>

namespace e2d {

namespace {

// layer.setVisible: show or hide one layer.
//   {"name": "layer.setVisible", "layer": "Topo_points", "visible": false}
// The layer is kept by its ID, not its position (notions §16): the command
// stays right even if the list is reordered between do and undo.
class SetLayerVisibleCommand : public Command {
public:
    SetLayerVisibleCommand(std::string layer, bool visible) : m_layer(std::move(layer)), m_visible(visible) {}

    void apply(CommandContext& ctx) override { set(ctx, m_visible); }
    void revert(CommandContext& ctx) override { set(ctx, !m_visible); }   // created only when it changes something

    std::string label() const override { return std::string(m_visible ? "Show " : "Hide ") + m_layer; }

    nlohmann::json toJson() const override
    {
        return {{"name", "layer.setVisible"}, {"layer", m_layer}, {"visible", m_visible}};
    }

private:
    void set(CommandContext& ctx, bool visible)
    {
        const int i = ctx.doc.findLayer(m_layer);
        if (i < 0)
            return;
        ctx.doc.layers[static_cast<std::size_t>(i)].visible = visible;
        ctx.bus.publish({{"event", "layer.visibility"}, {"layer", m_layer}, {"visible", visible}});
    }

    std::string m_layer;
    bool        m_visible;
};

// What the events say about a layer, and the selection items of one layer.
nlohmann::json layerEvent(const char* name, const LayerEntry& l, int at)
{
    return {{"event", name}, {"layer", l.id}, {"at", at}, {"source", l.source}};
}

void publishSelection(CommandContext& ctx)
{
    ctx.bus.publish({{"event", "selection.changed"},
                     {"count", ctx.doc.selectionCount()},
                     {"items", selectionItems(ctx.doc.selection)}});
}

// layer.add: a new layer at `at` (§38). The probe has already opened it:
// the entry holds its id, type, fields and extent.
class AddLayerCommand : public Command {
public:
    AddLayerCommand(LayerEntry entry, int at, nlohmann::json json)
        : m_entry(std::move(entry)), m_at(at), m_json(std::move(json)) {}

    void apply(CommandContext& ctx) override
    {
        ctx.doc.layers.insert(ctx.doc.layers.begin() + m_at, m_entry);
        ++ctx.doc.revision;
        ctx.doc.nextColour = std::max(ctx.doc.nextColour, m_entry.colour + 1);   // never given twice
        ctx.bus.publish(layerEvent("layer.added", m_entry, m_at));
    }
    void revert(CommandContext& ctx) override
    {
        const int i = ctx.doc.findLayer(m_entry.id);
        if (i < 0)
            return;
        ctx.doc.layers.erase(ctx.doc.layers.begin() + i);
        ++ctx.doc.revision;
        ctx.bus.publish({{"event", "layer.removed"}, {"layer", m_entry.id}});
    }
    std::string label() const override { return "Add " + m_entry.id; }
    nlohmann::json toJson() const override { return m_json; }

private:
    LayerEntry     m_entry;
    int            m_at;
    nlohmann::json m_json;
};

// layer.remove: keeps the whole entry, its place and its selection (§38):
// Ctrl+Z puts it back exactly, selected features included.
class RemoveLayerCommand : public Command {
public:
    RemoveLayerCommand(LayerEntry entry, int at) : m_entry(std::move(entry)), m_at(at) {}

    void apply(CommandContext& ctx) override
    {
        const int i = ctx.doc.findLayer(m_entry.id);
        if (i < 0)
            return;
        ctx.doc.layers.erase(ctx.doc.layers.begin() + i);
        ++ctx.doc.revision;
        const auto it = ctx.doc.selection.find(m_entry.id);
        m_selected.clear();
        if (it != ctx.doc.selection.end()) {
            m_selected = it->second;
            ctx.doc.selection.erase(it);
        }
        ctx.bus.publish({{"event", "layer.removed"}, {"layer", m_entry.id}});
        if (!m_selected.empty())
            publishSelection(ctx);
    }
    void revert(CommandContext& ctx) override
    {
        ctx.doc.layers.insert(ctx.doc.layers.begin() + m_at, m_entry);
        ++ctx.doc.revision;
        ctx.bus.publish(layerEvent("layer.added", m_entry, m_at));
        if (!m_selected.empty()) {
            ctx.doc.selection[m_entry.id] = m_selected;
            publishSelection(ctx);
        }
    }
    std::string label() const override { return "Remove " + m_entry.id; }
    nlohmann::json toJson() const override { return {{"name", "layer.remove"}, {"layer", m_entry.id}}; }

private:
    LayerEntry             m_entry;
    int                    m_at;
    std::set<std::int64_t> m_selected;   // filled by apply
};

// layer.move: from one place to another in the drawing order (§38).
class MoveLayerCommand : public Command {
public:
    MoveLayerCommand(std::string layer, int from, int to) : m_layer(std::move(layer)), m_from(from), m_to(to) {}

    void apply(CommandContext& ctx) override { move(ctx, m_from, m_to); }
    void revert(CommandContext& ctx) override { move(ctx, m_to, m_from); }
    std::string label() const override { return "Move " + m_layer; }
    nlohmann::json toJson() const override { return {{"name", "layer.move"}, {"layer", m_layer}, {"to", m_to}}; }

private:
    void move(CommandContext& ctx, int from, int to)
    {
        auto& layers = ctx.doc.layers;
        LayerEntry l = std::move(layers[static_cast<std::size_t>(from)]);
        layers.erase(layers.begin() + from);
        layers.insert(layers.begin() + to, std::move(l));
        ++ctx.doc.revision;
        ctx.bus.publish({{"event", "layer.moved"}, {"layer", m_layer}, {"from", from}, {"to", to}});
    }

    std::string m_layer;
    int         m_from, m_to;
};

// layer.setStyle: the whole style as YSLD text (§41); "" = back to the
// style file (Revert, §43). Keeps the text before: undo is exact.
class SetStyleCommand : public Command {
public:
    SetStyleCommand(std::string layer, std::string before, std::string after)
        : m_layer(std::move(layer)), m_before(std::move(before)), m_after(std::move(after)) {}

    void apply(CommandContext& ctx) override { set(ctx, m_after); }
    void revert(CommandContext& ctx) override { set(ctx, m_before); }
    std::string label() const override { return (m_after.empty() ? "Revert style of " : "Style of ") + m_layer; }
    nlohmann::json toJson() const override { return {{"name", "layer.setStyle"}, {"layer", m_layer}, {"ysld", m_after}}; }

private:
    void set(CommandContext& ctx, const std::string& text)
    {
        const int i = ctx.doc.findLayer(m_layer);
        if (i < 0)
            return;
        ctx.doc.layers[static_cast<std::size_t>(i)].styleText = text;
        ++ctx.doc.revision;   // §37: the threads compare the list again
        ctx.bus.publish({{"event", "layer.style"}, {"layer", m_layer}});
    }

    std::string m_layer, m_before, m_after;
};

} // namespace

void registerLayerCommands(Registry& registry, const LayerProbe& probe, const StyleCheck& styleCheck)
{
    registry.add({
        "layer.setVisible",
        "Show or hide a layer.",
        {{"layer", "string", "the layer id (its GDAL name, '#2' for a second one)"},
         {"visible", "boolean", "true to show it, false to hide it"}},
        [](const nlohmann::json& j, const Document& doc, std::string& error) -> std::unique_ptr<Command> {
            const std::string layer = j["layer"];
            const bool visible      = j["visible"];
            const int i = doc.findLayer(layer);
            if (i < 0) {
                error = "layer.setVisible: unknown layer '" + layer + "'";
                return nullptr;
            }
            if (doc.layers[static_cast<std::size_t>(i)].visible == visible)
                return nullptr;   // already so: nothing to do, nothing to undo
            return std::make_unique<SetLayerVisibleCommand>(layer, visible);
        },
    });

    registry.add({
        "layer.add",
        "Add a layer: a file, a file::layer or a PG: connection, with an optional style.",
        {{"source", "string", "the file or connection, as GDAL opens it"},
         {"style", "string", "a YSLD file; absent: render2d's default style", true},
         {"at", "number", "the place in the drawing order, 0 = bottom; absent: on top (a raster: under the vector layers)", true}},
        [&probe](const nlohmann::json& j, const Document& doc, std::string& error) -> std::unique_ptr<Command> {
            const std::string source = j["source"];
            if (!probe) {
                error = "layer.add: no way to open a source here";
                return nullptr;
            }
            // §38: open it once now; what cannot be read is refused whole.
            std::string why;
            const std::optional<ProbedLayer> p = probe(source, why);
            if (!p) {
                error = "layer.add: " + why;
                return nullptr;
            }
            const int n = static_cast<int>(doc.layers.size());
            int at = n;
            if (p->geometry == "raster") {   // §66: a photo on top would hide the map: above the rasters, under the rest
                at = 0;
                while (at < n && doc.layers[static_cast<std::size_t>(at)].geometry == "raster")
                    ++at;
            }
            if (j.contains("at")) {
                if (!j["at"].is_number_integer() || j["at"].get<int>() < 0 || j["at"].get<int>() > n) {
                    error = "layer.add: 'at' must be an integer from 0 to " + std::to_string(n);
                    return nullptr;
                }
                at = j["at"].get<int>();
            }
            LayerEntry e(uniqueId(doc, p->name), p->geometry, true);
            e.source = source;
            e.style  = j.contains("style") ? j["style"].get<std::string>() : std::string();
            e.colour = doc.nextColour;
            e.fields = p->fields;
            e.numeric = p->numeric;
            e.extent = p->extent;
            e.crs    = p->crs;   // §69
            e.otherLayers = p->otherLayers;
            e.web    = p->web;   // §76
            nlohmann::json json = j;
            json["at"] = at;   // the journal says where it went
            return std::make_unique<AddLayerCommand>(std::move(e), at, std::move(json));
        },
    });

    registry.add({
        "layer.remove",
        "Remove a layer (Ctrl+Z puts it back, with its selection).",
        {{"layer", "string", "the layer id"}},
        [](const nlohmann::json& j, const Document& doc, std::string& error) -> std::unique_ptr<Command> {
            const std::string layer = j["layer"];
            const int i = doc.findLayer(layer);
            if (i < 0) {
                error = "layer.remove: unknown layer '" + layer + "'";
                return nullptr;
            }
            return std::make_unique<RemoveLayerCommand>(doc.layers[static_cast<std::size_t>(i)], i);
        },
    });

    registry.add({
        "layer.move",
        "Move a layer in the drawing order.",
        {{"layer", "string", "the layer id"}, {"to", "number", "its new place, 0 = bottom"}},
        [](const nlohmann::json& j, const Document& doc, std::string& error) -> std::unique_ptr<Command> {
            const std::string layer = j["layer"];
            const int i = doc.findLayer(layer);
            if (i < 0) {
                error = "layer.move: unknown layer '" + layer + "'";
                return nullptr;
            }
            const int n = static_cast<int>(doc.layers.size());
            if (!j["to"].is_number_integer() || j["to"].get<int>() < 0 || j["to"].get<int>() >= n) {
                error = "layer.move: 'to' must be an integer from 0 to " + std::to_string(n - 1);
                return nullptr;
            }
            const int to = j["to"].get<int>();
            if (to == i)
                return nullptr;   // already there
            return std::make_unique<MoveLayerCommand>(layer, i, to);
        },
    });

    registry.add({
        "layer.setStyle",
        "Give a layer a new style, as YSLD text; an empty text goes back to its style file.",
        {{"layer", "string", "the layer id"},
         {"ysld", "string", "the whole style, as in a .yaml file; \"\" = the file's style"}},
        [&styleCheck](const nlohmann::json& j, const Document& doc, std::string& error) -> std::unique_ptr<Command> {
            const std::string layer = j["layer"];
            const std::string ysld  = j["ysld"];
            const int i = doc.findLayer(layer);
            if (i < 0) {
                error = "layer.setStyle: unknown layer '" + layer + "'";
                return nullptr;
            }
            const std::string& before = doc.layers[static_cast<std::size_t>(i)].styleText;
            if (ysld == before)
                return nullptr;   // the same style: nothing to do
            std::string why;
            if (!ysld.empty() && styleCheck && !styleCheck(ysld, why)) {
                error = "layer.setStyle: " + why;
                return nullptr;
            }
            return std::make_unique<SetStyleCommand>(layer, before, ysld);
        },
    });
}

} // namespace e2d
