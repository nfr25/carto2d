// File > Print (notions §77): the render thread writes the page with the map
// it holds, through its own mailbox; a view asked meanwhile does not replace
// it. A 1 km square of parcels in Lambert 72, an A4 at 1:5 000.

#include "worker/RenderWorker.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

std::optional<e2d::PrintResult> waitPrint(e2d::RenderWorker& w)
{
    for (int k = 0; k < 500; ++k) {   // 5 s at most
        if (auto r = w.takePrintResult())
            return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

} // namespace

int main()
{
    const fs::path dir = fs::temp_directory_path() / "e2d_print_test";
    fs::create_directories(dir);
    const fs::path square = dir / "parcels.geojson";
    std::ofstream(square) << R"({"type":"FeatureCollection",
 "crs":{"type":"name","properties":{"name":"urn:ogc:def:crs:EPSG::31370"}},
 "features":[{"type":"Feature","properties":{},"geometry":{"type":"Polygon",
   "coordinates":[[[148000,170000],[149000,170000],[149000,171000],[148000,171000],[148000,170000]]]}}]})";

    e2d::RenderWorker worker;
    worker.start(1.5);   // a screen at 150 %: the page must not care

    e2d::LayerState layer;
    layer.id     = "parcels";
    layer.source = square.generic_string();
    layer.title  = "Parcels of the test";

    e2d::PrintRequest r;
    r.page.scale    = 5000;
    r.page.center   = r2d::Point{148500, 170500};
    r.page.title    = "Print test";
    r.page.generator = "editor2d test";
    r.path          = (dir / "page.pdf").generic_string();
    r.layers        = {layer};
    r.revision      = 1;
    fs::remove(r.path);
    worker.print(r);

    const auto done = waitPrint(worker);
    check(done.has_value(), "the render thread answers");
    if (done) {
        check(done->ok, "the page is written");
        check(fs::exists(r.path) && fs::file_size(r.path) > 1000, "a PDF on the disk");
        std::ifstream in(r.path, std::ios::binary);
        char head[5] = {};
        in.read(head, 4);
        check(std::string(head) == "%PDF", "it starts as a PDF does");
        check(!done->lines.empty() && done->lines.front().find("A4-landscape, 1:5 000") != std::string::npos,
              "the page's line: A4 landscape at 1:5 000");
        bool legend = false;
        for (const auto& l : done->lines)
            if (l.find("legend: Parcels of the test") != std::string::npos)
                legend = true;
        check(legend, "the legend names the layer by the project's title");
        for (const auto& l : done->lines)
            std::printf("      %s\n", l.c_str());
    }

    // A path that cannot be written: said, not thrown.
    r.path = (dir / "no_such_folder" / "page.pdf").generic_string();
    worker.print(r);
    const auto bad = waitPrint(worker);
    check(bad && !bad->ok && !bad->error.empty(), "an impossible path: the reason is given");

    worker.stop();
    std::printf("%s\n", g_failures ? "SOME TESTS FAILED" : "all print tests passed");
    return g_failures ? 1 : 0;
}
