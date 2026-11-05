// selection.set (notions §22): the four modes, undo, refusals, the event.

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

nlohmann::json select(const std::string& mode, std::initializer_list<std::pair<const char*, int>> items)
{
    nlohmann::json j = {{"name", "selection.set"}, {"mode", mode}, {"items", nlohmann::json::array()}};
    for (const auto& [layer, fid] : items)
        j["items"].push_back({{"layer", layer}, {"fid", fid}});
    return j;
}

void testModes()
{
    e2d::Document doc;
    doc.layers = {{"batiments", "polygon", true}, {"adresses", "point", true}};
    e2d::EventBus bus;
    nlohmann::ordered_json last;
    bus.subscribe("selection.changed", [&](const e2d::Event& e) { last = e; });
    e2d::Commander cmd(doc, bus);
    std::string error;

    cmd.run(select("replace", {{"batiments", 1823}}), error);
    check(doc.selectionCount() == 1 && doc.selection["batiments"].count(1823), "replace: {1823}");
    check(last["count"] == 1 && last["items"][0]["fid"] == 1823, "event selection.changed, count 1");

    cmd.run(select("add", {{"batiments", 1907}, {"adresses", 45012}}), error);
    check(doc.selectionCount() == 3, "add: 3 features");

    cmd.run(select("toggle", {{"batiments", 1823}}), error);
    check(doc.selectionCount() == 2 && !doc.selection["batiments"].count(1823), "toggle: 1823 removed");

    // §58: remove takes out what is there, ignores what is not: {1907, 45012} - {1907, 999} = {45012}.
    cmd.run(select("remove", {{"batiments", 1907}, {"batiments", 999}}), error);
    check(doc.selectionCount() == 1 && doc.selection.count("batiments") == 0, "remove: 1907 out, 999 ignored");
    check(cmd.history().past().back()->label() == "Remove 2 features from selection", "label: Remove");
    cmd.undo();
    check(doc.selectionCount() == 2, "undo remove: 1907 back");

    cmd.run(select("replace", {{"adresses", 45012}}), error);
    check(doc.selectionCount() == 1 && doc.selection.count("batiments") == 0, "replace: only 45012, no empty layer");

    // §22: Ctrl+Z gives the lost selection back.
    cmd.undo();
    check(doc.selectionCount() == 2 && doc.selection["batiments"].count(1907), "undo: the 2 features are back");
    cmd.redo();
    check(doc.selectionCount() == 1, "redo: 1 feature");

    cmd.run(select("clear", {}), error);
    check(doc.selection.empty() && last["count"] == 0, "clear: empty, event count 0");
    check(cmd.history().past().back()->label() == "Clear selection", "label: Clear selection");

    const std::size_t entries = cmd.history().past().size();
    cmd.run(select("clear", {}), error);
    check(cmd.history().past().size() == entries, "clearing an empty selection: no history entry");
}

void testRefused()
{
    e2d::Document doc;
    doc.layers = {{"batiments", "polygon", true}};
    e2d::EventBus bus;
    e2d::Commander cmd(doc, bus);
    std::string error;

    check(!cmd.run(select("explode", {{"batiments", 1}}), error), "unknown mode: refused");
    check(!cmd.run(select("add", {{"batiments", 1}, {"rivers", 2}}), error) && doc.selection.empty(),
          "one unknown layer: all refused, nothing half done");
    nlohmann::json bad = {{"name", "selection.set"}, {"mode", "add"}, {"items", {{{"layer", "batiments"}, {"fid", "x"}}}}};
    check(!cmd.run(bad, error), "fid not an integer: refused");
}

} // namespace

int main()
{
    testModes();
    testRefused();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
