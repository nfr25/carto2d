// layer.add, layer.remove, layer.move (notions §38), with a fake probe: no
// GDAL here. What they change, what they keep for Ctrl+Z, what they refuse.

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

// The fake probe: "x/batiments.fgb" is a polygon layer named "batiments";
// a ".tif" is a raster (§66); a name containing "absent" cannot be opened.
std::optional<e2d::ProbedLayer> fakeProbe(const std::string& source, std::string& error)
{
    if (source.find("absent") != std::string::npos) {
        error = "cannot open '" + source + "'";
        return std::nullopt;
    }
    e2d::ProbedLayer p;
    const auto slash = source.find_last_of('/');
    const auto dot   = source.find_last_of('.');
    p.name     = source.substr(slash + 1, dot - slash - 1);
    const bool raster = source.size() > 4 && source.substr(source.size() - 4) == ".tif";
    p.geometry = raster ? "raster" : "polygon";
    if (!raster)
        p.fields = {"AREA"};
    p.extent   = e2d::Extent{0, 0, 10, 10};
    return p;
}

struct Fixture {
    e2d::Document  doc;
    e2d::EventBus  bus;
    e2d::Commander cmd{doc, bus};
    std::vector<std::string> events;
    Fixture()
    {
        cmd.setLayerProbe(fakeProbe);
        bus.subscribe("*", [this](const e2d::Event& e) { events.push_back(e["event"]); });
    }
    bool run(const nlohmann::json& j)
    {
        std::string error;
        return cmd.run(j, error);
    }
    std::string order() const
    {
        std::string s;
        for (const auto& l : doc.layers)
            s += (s.empty() ? "" : ",") + l.id;
        return s;
    }
};

void testAdd()
{
    Fixture f;
    check(f.run({{"name", "layer.add"}, {"source", "d/parcelles.fgb"}}), "add parcelles");
    check(f.run({{"name", "layer.add"}, {"source", "d/batiments.fgb"}, {"style", "s/b.yaml"}}), "add batiments");
    check(f.order() == "parcelles,batiments", "on top by default: parcelles,batiments");
    check(f.doc.layers[1].style == "s/b.yaml" && f.doc.layers[1].fields.size() == 1 && f.doc.layers[1].extent,
          "the entry holds style, fields and extent");
    check(f.doc.layers[0].colour == 0 && f.doc.layers[1].colour == 1, "colours 0, 1 in order of arrival");
    check(f.doc.revision == 2, "revision + 1 per add");

    // §38: a third "batiments" gets "#2"... the name is taken.
    check(f.run({{"name", "layer.add"}, {"source", "e/batiments.fgb"}, {"at", 0}}), "add another batiments at 0");
    check(f.order() == "batiments#2,parcelles,batiments", "at 0: at the bottom, id batiments#2");

    // Undo: removed; the colour is not given again.
    f.cmd.undo();
    check(f.order() == "parcelles,batiments", "undo add: gone");
    check(f.run({{"name", "layer.add"}, {"source", "d/arbres.fgb"}}) && f.doc.layers[2].colour == 3,
          "the next layer gets colour 3, not 2 again");
    const std::string last = f.cmd.history().past().back()->toJson().dump();
    check(last.find("\"at\":2") != std::string::npos, "the journal says where it went: at 2");
}

void testAddRefused()
{
    Fixture f;
    std::string error;
    check(!f.cmd.run({{"name", "layer.add"}, {"source", "d/absent.fgb"}}, error) &&
              error.find("cannot open") != std::string::npos && f.doc.layers.empty() &&
              f.cmd.history().past().empty(),
          "a source that cannot be opened: refused whole, the probe's reason, no history");
    check(!f.cmd.run({{"name", "layer.add"}, {"source", "d/a.fgb"}, {"at", 3}}, error), "at beyond the end: refused");
    check(!f.cmd.run({{"name", "layer.add"}, {"source", "d/a.fgb"}, {"style", 7}}, error), "style not a string");
}

void testRemoveKeepsSelection()
{
    Fixture f;
    f.run({{"name", "layer.add"}, {"source", "d/parcelles.fgb"}});
    f.run({{"name", "layer.add"}, {"source", "d/batiments.fgb"}});
    f.run({{"name", "selection.set"}, {"mode", "replace"},
           {"items", {{{"layer", "batiments"}, {"fid", 12}}, {{"layer", "parcelles"}, {"fid", 3}}}}});
    f.events.clear();
    check(f.run({{"name", "layer.remove"}, {"layer", "batiments"}}), "remove batiments");
    check(f.order() == "parcelles" && f.doc.selectionCount() == 1, "gone, and out of the selection");
    check(f.events.size() == 3 && f.events[0] == "layer.removed" && f.events[1] == "selection.changed",
          "events: layer.removed, selection.changed, history.changed");
    f.cmd.undo();
    check(f.order() == "parcelles,batiments" && f.doc.selectionCount() == 2 && f.doc.selection.count("batiments"),
          "undo: back at its place, with its selected feature");
    check(f.doc.layers[1].colour == 1 && f.doc.layers[1].fields.size() == 1, "...and everything it had");
}

void testMove()
{
    Fixture f;
    for (const char* s : {"d/a.fgb", "d/b.fgb", "d/c.fgb"})
        f.run({{"name", "layer.add"}, {"source", s}});
    check(f.run({{"name", "layer.move"}, {"layer", "c"}, {"to", 0}}) && f.order() == "c,a,b", "move c to 0: c,a,b");
    check(f.doc.layers[0].colour == 2, "c keeps its colour 2");
    f.cmd.undo();
    check(f.order() == "a,b,c", "undo: a,b,c");
    std::string error;
    check(f.cmd.run({{"name", "layer.move"}, {"layer", "c"}, {"to", 2}}, error) && f.cmd.history().past().size() == 3,
          "already there: nothing to do, no history entry");
    check(!f.cmd.run({{"name", "layer.move"}, {"layer", "c"}, {"to", 3}}, error), "to beyond the end: refused");
    check(!f.cmd.run({{"name", "layer.move"}, {"layer", "z"}, {"to", 0}}, error), "unknown layer: refused");
}

// §41: layer.setStyle, the style as text; "" = back to the file.
void testSetStyle()
{
    Fixture f;
    f.cmd.setStyleCheck([](const std::string& ysld, std::string& error) {
        if (ysld.find("feature-styles") == std::string::npos) {
            error = "not a style";
            return false;
        }
        return true;
    });
    f.run({{"name", "layer.add"}, {"source", "d/batiments.fgb"}, {"style", "s/b.yaml"}});
    const std::string red = "feature-styles: [{rules: [{symbolizers: [{polygon: {fill-color: '#FF0000'}}]}]}]";
    f.events.clear();
    check(f.run({{"name", "layer.setStyle"}, {"layer", "batiments"}, {"ysld", red}}) &&
              f.doc.layers[0].styleText == red && f.events[0] == "layer.style",
          "setStyle: the text kept, event layer.style");
    check(f.doc.layers[0].style == "s/b.yaml", "the style file is still known (for Save)");
    std::string error;
    check(!f.cmd.run({{"name", "layer.setStyle"}, {"layer", "batiments"}, {"ysld", "oops"}}, error) &&
              error.find("not a style") != std::string::npos && f.doc.layers[0].styleText == red,
          "a text the check refuses: refused whole, the style unchanged");
    check(f.run({{"name", "layer.setStyle"}, {"layer", "batiments"}, {"ysld", ""}}) && f.doc.layers[0].styleText.empty(),
          "\"\": back to the file (Revert)");
    f.cmd.undo();
    check(f.doc.layers[0].styleText == red, "undo Revert: the changed style again");
    f.cmd.undo();
    check(f.doc.layers[0].styleText.empty(), "undo setStyle: the file's style");
    const auto before = f.cmd.history().past().size();
    f.run({{"name", "layer.setStyle"}, {"layer", "batiments"}, {"ysld", ""}});
    check(f.cmd.history().past().size() == before, "the same style: no history entry");
}

// §66: a raster goes under the vector layers, above the rasters already there.
void testAddRaster()
{
    Fixture f;
    f.run({{"name", "layer.add"}, {"source", "d/parcelles.fgb"}});
    f.run({{"name", "layer.add"}, {"source", "d/batiments.fgb"}});
    check(f.run({{"name", "layer.add"}, {"source", "o/ortho.tif"}}) && f.order() == "ortho,parcelles,batiments",
          "a raster: at the bottom, under the vectors");
    check(f.doc.layers[0].geometry == "raster" && f.doc.layers[0].fields.empty(), "type raster, no field");
    check(f.run({{"name", "layer.add"}, {"source", "o/dsm.tif"}}) && f.order() == "ortho,dsm,parcelles,batiments",
          "a second raster: above the first, still under the vectors");
    check(f.cmd.history().past().back()->toJson()["at"] == 1, "the journal says where it went: at 1");
    check(f.run({{"name", "layer.add"}, {"source", "o/hillshade.tif"}, {"at", 4}}) &&
              f.order() == "ortho,dsm,parcelles,batiments,hillshade",
          "'at' given: obeyed, even on top");
    f.cmd.undo();
    f.cmd.undo();
    check(f.order() == "ortho,parcelles,batiments", "undo: the rasters go away");
}

} // namespace

int main()
{
    testAdd();
    testAddRefused();
    testAddRaster();
    testRemoveKeepsSelection();
    testMove();
    testSetStyle();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
