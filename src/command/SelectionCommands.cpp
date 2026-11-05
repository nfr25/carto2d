#include "command/SelectionCommands.h"

namespace e2d {

namespace {

// Keeps the whole selection before and after: a few kilobytes even for
// thousands of features, and undo is then exact whatever the mode was.
class SetSelectionCommand : public Command {
public:
    SetSelectionCommand(std::string label, nlohmann::json json, Selection before, Selection after)
        : m_label(std::move(label)), m_json(std::move(json)), m_before(std::move(before)), m_after(std::move(after))
    {
    }

    void apply(CommandContext& ctx) override { set(ctx, m_after); }
    void revert(CommandContext& ctx) override { set(ctx, m_before); }
    std::string label() const override { return m_label; }
    nlohmann::json toJson() const override { return m_json; }

private:
    static void set(CommandContext& ctx, const Selection& s)
    {
        ctx.doc.selection = s;
        ctx.bus.publish({{"event", "selection.changed"},
                         {"count", ctx.doc.selectionCount()},
                         {"items", selectionItems(s)}});
    }

    std::string    m_label;
    nlohmann::json m_json;
    Selection      m_before, m_after;
};

std::string features(std::size_t n)
{
    return std::to_string(n) + (n == 1 ? " feature" : " features");
}

} // namespace

nlohmann::json selectionItems(const Selection& selection)
{
    nlohmann::json items = nlohmann::json::array();
    for (const auto& [layer, fids] : selection)
        for (std::int64_t fid : fids)
            items.push_back({{"layer", layer}, {"fid", fid}});
    return items;
}

void registerSelectionCommands(Registry& registry)
{
    registry.add({
        "selection.set",
        "Change the selection: replace it, add to it, remove from it, toggle features in it, or clear it.",
        {{"mode", "string", "replace, add, remove, toggle or clear"},
         {"items", "array", "[{\"layer\": layer id, \"fid\": feature id}, ...] (ignored by clear)"}},
        [](const nlohmann::json& j, const Document& doc, std::string& error) -> std::unique_ptr<Command> {
            const std::string mode = j["mode"];
            if (mode != "replace" && mode != "add" && mode != "remove" && mode != "toggle" && mode != "clear") {
                error = "selection.set: mode must be replace, add, remove, toggle or clear, not '" + mode + "'";
                return nullptr;
            }

            // Check every item before changing anything: refused whole, never half done (§16).
            std::vector<std::pair<std::string, std::int64_t>> items;
            if (mode != "clear") {
                for (const auto& item : j["items"]) {
                    if (!item.is_object() || !item.contains("layer") || !item["layer"].is_string() ||
                        !item.contains("fid") || !item["fid"].is_number_integer()) {
                        error = "selection.set: each item is {\"layer\": \"...\", \"fid\": integer}";
                        return nullptr;
                    }
                    const std::string layer = item["layer"];
                    if (doc.findLayer(layer) < 0) {
                        error = "selection.set: unknown layer '" + layer + "'";
                        return nullptr;
                    }
                    items.emplace_back(layer, item["fid"].get<std::int64_t>());
                }
            }

            Selection after = mode == "replace" || mode == "clear" ? Selection() : doc.selection;
            for (const auto& [layer, fid] : items) {
                auto& fids = after[layer];
                if (mode == "remove" || (mode == "toggle" && fids.count(fid)))
                    fids.erase(fid);   // §58: remove takes out, never adds
                else
                    fids.insert(fid);
            }
            for (auto it = after.begin(); it != after.end();)   // no empty layer left behind
                it = it->second.empty() ? after.erase(it) : std::next(it);

            if (after == doc.selection)
                return nullptr;   // nothing changes: no history entry

            std::string label;
            if (mode == "clear" || after.empty())
                label = "Clear selection";
            else if (mode == "replace")
                label = "Select " + features(items.size());
            else if (mode == "add")
                label = "Add " + features(items.size()) + " to selection";
            else if (mode == "remove")
                label = "Remove " + features(items.size()) + " from selection";
            else
                label = "Toggle " + features(items.size()) + " in selection";
            return std::make_unique<SetSelectionCommand>(label, j, doc.selection, std::move(after));
        },
    });
}

} // namespace e2d
