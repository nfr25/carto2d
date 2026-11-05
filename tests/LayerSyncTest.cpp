// A thread's Map put in line with the Document's list (notions §37), with
// small GeoJSON files: the worked example of the notions, step by step.

#include "worker/LayerSync.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

// One point layer per name: the GDAL layer is named after the file.
std::string file(const std::string& name)
{
    const fs::path dir = fs::temp_directory_path() / "e2d_sync_test";
    fs::create_directories(dir);
    const fs::path p = dir / (name + ".geojson");
    std::ofstream(p) << R"({"type":"FeatureCollection","features":[
        {"type":"Feature","properties":{},"geometry":{"type":"Point","coordinates":[150000,160000]}}]})";
    return p.generic_string();
}

e2d::LayerState layer(const std::string& id, int colour)
{
    return {id, file(id), "", true, colour, "", "", "", "", true};
}

// The names of the Map's layers, bottom first: "parcelles,batiments".
std::string names(const r2d::Map& map)
{
    std::string s;
    for (int i = 0; i < map.layerCount(); ++i)
        s += (i ? "," : "") + map.layerInfo(i).name;
    return s;
}

std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& x : v)
        s += (s.empty() ? "" : ",") + x;
    return s;
}

void testExample()
{
    r2d::Map map = r2d::Map::open({});
    e2d::LayerSync sync;
    const auto parcelles = layer("parcelles", 0), batiments = layer("batiments", 1);
    const auto adresses = layer("adresses", 2), arbres = layer("arbres", 3);

    sync.apply(map, 1, "", {parcelles, batiments, adresses});
    check(names(map) == "parcelles,batiments,adresses", "revision 1: the three layers opened, in order");

    // §37: adresses removed, arbres added under the buildings.
    sync.apply(map, 2, "", {parcelles, arbres, batiments});
    check(names(map) == "parcelles,arbres,batiments", "revision 2: [parcelles, arbres, batiments]");
    check(join(sync.ids()) == "parcelles,arbres,batiments", "ids follow the Map's order");

    // Visibility at every request, even with the same revision.
    auto hidden = batiments;
    hidden.visible = false;
    sync.apply(map, 2, "", {parcelles, arbres, hidden});
    check(map.visible(0) && map.visible(1) && !map.visible(2), "same revision: only the visibility changes");
}

void testFailures()
{
    r2d::Map map = r2d::Map::open({});
    e2d::LayerSync sync;
    e2d::LayerState missing{"absent", "/no/such/file.fgb", "", true, 0, "", "", "", "", true};
    sync.apply(map, 1, "", {layer("parcelles", 0), missing});
    check(names(map) == "parcelles", "a source that cannot be opened is not in the Map...");
    check(sync.warnings().size() == 1, "...and its warning is kept");
    check(join(sync.ids()) == "parcelles", "ids: only what the Map holds");
    sync.apply(map, 2, "", {layer("parcelles", 0)});
    check(sync.warnings().empty(), "removed from the list: its warning goes with it");
}

// §41: a style changed in the dialog goes to the open layer; "" goes back to the file.
void testStyleText()
{
    r2d::Map map = r2d::Map::open({});
    e2d::LayerSync sync;
    auto a = layer("parcelles", 0);
    sync.apply(map, 1, "", {a});
    a.styleText = "[not a style";
    sync.apply(map, 2, "", {a});
    check(sync.warnings().size() == 1 && map.layerCount() == 1, "an unreadable style: said, the layer still there");
    a.styleText = "feature-styles: [{rules: [{symbolizers: [{point: {size: 9}}]}]}]";
    sync.apply(map, 3, "", {a});
    check(sync.warnings().empty(), "a good one: the warning goes");
    a.styleText.clear();
    sync.apply(map, 4, "", {a});
    check(map.layerCount() == 1 && join(sync.ids()) == "parcelles" && sync.warnings().empty(),
          "back to the file: reopened, still one layer");
}

// §69: the map's system comes with the request; both threads get the same.
void testCrs()
{
    r2d::Map map = r2d::Map::open({});
    e2d::LayerSync sync;
    sync.apply(map, 1, "EPSG:31370", {layer("parcelles", 0)});
    check(map.crs().epsg == 31370 && map.layerCount() == 1, "the system given: EPSG:31370, the layer opened");
    sync.apply(map, 1, "EPSG:3857", {layer("parcelles", 0)});
    check(map.crs().epsg == 3857 && map.layerCount() == 1, "another one: the Map follows, the layer stays open");
    // A file that says nothing, declared in the project (crs: of the layer): converted.
    auto declared = layer("parcelles", 0);
    declared.crs = "EPSG:31370";
    sync.apply(map, 3, "EPSG:3857", {declared});
    check(map.layerCount() == 1 && map.layerReprojection(0).find("EPSG:31370 -> EPSG:3857") == 0,
          "crs: of the layer: reopened, converted from it");
    sync.apply(map, 4, "not a system", {layer("parcelles", 0)});
    check(map.crs().epsg == 3857 && !sync.warnings().empty(), "an unknown one: said, the Map keeps its own");
}

} // namespace

int main()
{
    testExample();
    testFailures();
    testStyleText();
    testCrs();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
