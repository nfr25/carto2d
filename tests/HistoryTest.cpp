// The two stacks of notions §15, gesture by gesture, as in its table, and the
// events of §17. No window: the commands only need the Document and the bus.

#include "command/Commander.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

// Four layers, all visible: Parcelles, Batiments, Adresses, Arbres.
e2d::Document fourLayers()
{
    e2d::Document doc;
    for (const char* id : {"Parcelles", "Batiments", "Adresses", "Arbres"})
        doc.layers.push_back({id, "polygon", true});
    return doc;
}

nlohmann::json hide(const std::string& layer)
{
    return {{"name", "layer.setVisible"}, {"layer", layer}, {"visible", false}};
}

std::vector<std::string> labels(const std::vector<std::unique_ptr<e2d::Command>>& stack)
{
    std::vector<std::string> out;
    for (const auto& c : stack)
        out.push_back(c->label());
    return out;
}

void testTable()
{
    e2d::Document doc = fourLayers();
    e2d::EventBus bus;
    std::vector<e2d::Event> events;
    bus.subscribe("*", [&](const e2d::Event& e) { events.push_back(e); });
    e2d::Commander cmd(doc, bus);
    std::string error;

    // 1. Hide Parcelles.
    check(cmd.run(hide("Parcelles"), error) && !doc.layers[0].visible, "hide Parcelles");
    check(events.size() == 2 && events[0]["event"] == "layer.visibility" && events[0]["visible"] == false &&
              events[1]["event"] == "history.changed",
          "events: layer.visibility, then history.changed");

    // 2. Hide Arbres.
    cmd.run(hide("Arbres"), error);
    check(labels(cmd.history().past()) == std::vector<std::string>{"Hide Parcelles", "Hide Arbres"},
          "past: [Hide Parcelles, Hide Arbres]");

    // 3. Ctrl+Z: Arbres back.
    cmd.undo();
    check(doc.layers[3].visible, "undo: Arbres shown again");
    check(labels(cmd.history().future()) == std::vector<std::string>{"Hide Arbres"}, "future: [Hide Arbres]");

    // 4. Ctrl+Z: Parcelles back, both in the future.
    cmd.run({{"name", "history.undo"}}, error);   // the JSON form of Ctrl+Z
    check(doc.layers[0].visible && cmd.history().past().empty(), "undo (JSON): Parcelles shown, past empty");
    check(cmd.history().future().size() == 2, "future: two commands");

    // 5. Ctrl+Y: Parcelles hidden again, Hide Arbres still in the future.
    cmd.redo();
    check(!doc.layers[0].visible, "redo: Parcelles hidden");
    check(labels(cmd.history().future()) == std::vector<std::string>{"Hide Arbres"}, "future: [Hide Arbres]");

    // 6. Hide Adresses: a new action clears the future.
    cmd.run(hide("Adresses"), error);
    check(labels(cmd.history().past()) == std::vector<std::string>{"Hide Parcelles", "Hide Adresses"},
          "past: [Hide Parcelles, Hide Adresses]");
    check(cmd.history().future().empty(), "future cleared by a new action");

    // The journal: each command as the JSON that would create it again (§16).
    check(cmd.history().past()[1]->toJson() == hide("Adresses"), "toJson: the same JSON as the request");
}

// §17: a subscriber does not run a command while being told; it queues it.
void testQueueFromSubscriber()
{
    e2d::Document doc = fourLayers();
    e2d::EventBus bus;
    e2d::Commander cmd(doc, bus);
    // "When Parcelles is hidden, hide Batiments too."
    bus.subscribe("layer.visibility", [&](const e2d::Event& e) {
        if (e["layer"] == "Parcelles" && e["visible"] == false)
            cmd.submit(hide("Batiments"));
    });
    std::string error;
    cmd.run(hide("Parcelles"), error);
    check(doc.layers[1].visible, "the subscriber's command waits in the queue");
    check(cmd.processQueue().empty() && !doc.layers[1].visible, "processQueue: then Batiments is hidden");
    check(labels(cmd.history().past()) == std::vector<std::string>{"Hide Parcelles", "Hide Batiments"},
          "two entries, in the right order");
}

} // namespace

int main()
{
    testTable();
    testQueueFromSubscriber();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
