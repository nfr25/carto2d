// JSON -> command (notions §16): what is accepted, what is refused (with a
// message, nothing executed), what does nothing; unique layer ids.

#include "command/Commander.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

e2d::Document twoLayers()
{
    e2d::Document doc;
    doc.layers.push_back({"Parcelles", "polygon", true});
    doc.layers.push_back({"Topo_points", "point", true});
    return doc;
}

// Refused: false, a message, the document and the history untouched.
void refused(const nlohmann::json& j, const char* what)
{
    e2d::Document doc = twoLayers();
    e2d::EventBus bus;
    e2d::Commander cmd(doc, bus);
    std::string error;
    const bool ok = cmd.run(j, error);
    check(!ok && !error.empty() && doc.layers[0].visible && doc.layers[1].visible && cmd.history().past().empty(),
          what);
    if (!error.empty())
        std::printf("      -> %s\n", error.c_str());
}

void testRefused()
{
    refused(nlohmann::json::array(), "not an object: refused");
    refused({{"layer", "Parcelles"}}, "no name: refused");
    refused({{"name", "layer.explode"}}, "unknown command: refused");
    refused({{"name", "layer.setVisible"}, {"visible", false}}, "missing 'layer': refused");
    refused({{"name", "layer.setVisible"}, {"layer", "Parcelles"}, {"visible", "no"}}, "'visible' not a boolean: refused");
    refused({{"name", "layer.setVisible"}, {"layer", "Rivers"}, {"visible", false}}, "unknown layer: refused");
}

void testNothingToDo()
{
    e2d::Document doc = twoLayers();
    e2d::EventBus bus;
    int events = 0;
    bus.subscribe("*", [&](const e2d::Event&) { ++events; });
    e2d::Commander cmd(doc, bus);
    std::string error;
    const bool ok = cmd.run({{"name", "layer.setVisible"}, {"layer", "Parcelles"}, {"visible", true}}, error);
    check(ok && error.empty() && cmd.history().past().empty() && events == 0,
          "already visible: accepted, no history entry, no event");
}

void testDescribe()
{
    e2d::Document doc = twoLayers();
    e2d::EventBus bus;
    e2d::Commander cmd(doc, bus);
    const nlohmann::json d = cmd.registry().describe();
    // Sorted by name: layer.add, layer.move, layer.remove, layer.setStyle, layer.setVisible, selection.set.
    check(d.is_array() && d.size() == 6 && d[0]["name"] == "layer.add" && d[0]["params"].size() == 3 &&
              d[0]["params"][1]["optional"] == true && d[3]["name"] == "layer.setStyle" &&
              d[4]["name"] == "layer.setVisible" && d[4]["params"].size() == 2 && d[5]["name"] == "selection.set",
          "describe(): the six commands, their parameters, the optional ones (the future GET /commands)");
}

void testUniqueIds()
{
    const auto ids = e2d::uniqueIds({"batiments", "parcelles", "batiments", "batiments"});
    check(ids == std::vector<std::string>{"batiments", "parcelles", "batiments#2", "batiments#3"},
          "uniqueIds: batiments, parcelles, batiments#2, batiments#3");
}

} // namespace

int main()
{
    testRefused();
    testNothingToDo();
    testDescribe();
    testUniqueIds();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
