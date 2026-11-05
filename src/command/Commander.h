#pragma once

#include "command/History.h"
#include "command/Registry.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <string>
#include <vector>

namespace e2d {

// ============================================================================
// Commander — the single door for every change (notions §15 to §17).
//
//   run(json)     : now. The layers panel, Ctrl+Z, later the server.
//   submit(json)  : later, at the next processQueue(). For event subscribers,
//                   who must not run a command while being told (§17).
//
// Besides the registered commands, two built-in names: "history.undo" and
// "history.redo". After every change: {"event": "history.changed", ...}.
// ============================================================================
class Commander {
public:
    Commander(Document& doc, EventBus& bus);

    // Executes the JSON command. False with `error` when it was refused.
    // True when it was executed, or when there was nothing to do.
    bool run(const nlohmann::json& j, std::string& error);

    void submit(nlohmann::json j);

    // Runs the queued commands, in order. Returns their errors.
    std::vector<std::string> processQueue();

    bool undo();
    bool redo();

    Registry&      registry() { return m_registry; }

    // How layer.add opens a source (§38): GDAL in the application, a fake in tests.
    void setLayerProbe(LayerProbe probe) { m_probe = std::move(probe); }
    // How layer.setStyle checks a text (§41): render2d's reader in the application.
    void setStyleCheck(StyleCheck check) { m_styleCheck = std::move(check); }
    const History& history() const { return m_history; }
    void clearHistory() { m_history.clear(); publishHistory(); }   // §53: New, Open

private:
    void publishHistory();

    Document&        m_doc;
    EventBus&        m_bus;
    CommandContext   m_ctx;
    LayerProbe       m_probe;      // before the registry: its layer.add refers to it
    StyleCheck       m_styleCheck; // the same for layer.setStyle
    Registry         m_registry;
    History          m_history;
    std::deque<nlohmann::json> m_queue;
    int              m_running = 0;   // > 0 while a command runs: run() then queues
};

} // namespace e2d
