#include "command/Commander.h"

#include "command/LayerCommands.h"
#include "command/SelectionCommands.h"

namespace e2d {

Commander::Commander(Document& doc, EventBus& bus) : m_doc(doc), m_bus(bus), m_ctx{doc, bus}
{
    registerLayerCommands(m_registry, m_probe, m_styleCheck);
    registerSelectionCommands(m_registry);
}

bool Commander::run(const nlohmann::json& j, std::string& error)
{
    error.clear();
    // §17: called from a subscriber while another command runs -> later.
    if (m_running > 0) {
        submit(j);
        return true;
    }

    const std::string name = j.is_object() ? j.value("name", std::string()) : std::string();
    if (name == "history.undo") {
        undo();
        return true;
    }
    if (name == "history.redo") {
        redo();
        return true;
    }

    auto command = m_registry.create(j, m_doc, error);
    if (!command)
        return error.empty();   // empty error: nothing to do

    ++m_running;
    m_history.execute(std::move(command), m_ctx);
    publishHistory();
    --m_running;
    return true;
}

void Commander::submit(nlohmann::json j)
{
    m_queue.push_back(std::move(j));
}

std::vector<std::string> Commander::processQueue()
{
    std::vector<std::string> errors;
    // A command run here may queue others: they run in the same call, in order.
    while (!m_queue.empty()) {
        nlohmann::json j = std::move(m_queue.front());
        m_queue.pop_front();
        std::string error;
        if (!run(j, error))
            errors.push_back(error);
    }
    return errors;
}

bool Commander::undo()
{
    ++m_running;
    const bool done = m_history.undo(m_ctx);
    if (done)
        publishHistory();
    --m_running;
    return done;
}

bool Commander::redo()
{
    ++m_running;
    const bool done = m_history.redo(m_ctx);
    if (done)
        publishHistory();
    --m_running;
    return done;
}

// {"event": "history.changed", "past": [...], "future": [...]}: oldest first
// for the past, next-to-redo first for the future.
void Commander::publishHistory()
{
    nlohmann::json past = nlohmann::json::array(), future = nlohmann::json::array();
    for (const auto& c : m_history.past())
        past.push_back(c->label());
    for (auto it = m_history.future().rbegin(); it != m_history.future().rend(); ++it)
        future.push_back((*it)->label());
    m_bus.publish({{"event", "history.changed"}, {"past", past}, {"future", future}});
}

} // namespace e2d
