#include "Application.h"
#include "Version.h"
#include "window/Platform.h"

#include "backend/gdal/gdal_layer.hpp"   // §76: the name given to web services
#include "style/symbol_folders.hpp"       // §78: the symbol libraries

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// carto2d [CONFIG.yaml] [FILE ...]   (editor2d until 0.6, notions §79)
// CONFIG: a viewer.yaml (layers, start view), notions §13. FILEs: data files
// added on top, as if dropped on the window (§39); dropping files on
// carto2d.exe in the Explorer gives the same command line.
// No .yaml: an empty map, with only the files given; nothing at all:
// carto2d.yaml (or editor2d.yaml) beside the executable, if it is there (§71).

// §25: on a laptop with two graphics chips, Windows starts unknown programs
// on the small integrated one. These two exported values ask the NVIDIA and
// AMD drivers for the big card, as games do. Read by the driver at start.
#ifdef _WIN32
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char** argv)
{
    std::string              config;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto dot = a.find_last_of('.');
        const std::string ext = dot == std::string::npos ? "" : a.substr(dot);
        if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "usage: carto2d [project.yaml] [data files...]\n");
            return 2;
        }
        if ((ext == ".yaml" || ext == ".yml") && config.empty())
            config = a;
        else
            files.push_back(a);
    }

    // §71: nothing given: the project beside the executable, if there is one
    // (a fixed name: a style is a .yaml too).
    if (config.empty() && files.empty()) {
        // carto2d.yaml; editor2d.yaml as well, its name until 0.6 (§79, 7a).
        for (const char* name : {"carto2d.yaml", "editor2d.yaml"}) {
            const std::filesystem::path beside = e2d::executableFolder() / name;
            std::error_code ec;
            if (!beside.parent_path().empty() && std::filesystem::is_regular_file(beside, ec)) {
                config = beside.generic_string();
                break;
            }
        }
    }

    // §76: how carto2D presents itself to web services (OpenStreetMap blocks
    // the generic names): the program, not the person.
    r2d::setWebIdentity(std::string("carto2D/") + e2d::kVersion + " (+https://github.com/nfr25/render2d)");

    // §78: the symbol libraries, before any style is read: R2D_SYMBOLS, then
    // render2d's library, which carto2D shares: symbols/ beside the program
    // (installed beside render2d.exe); from a build, one folder up, else
    // render2d's tree beside editor2d's (../../render2d/render2d/symbols,
    // as CMake finds render2d, §2). Relative to the program: no path of this
    // computer is written in the executable.
    {
        std::vector<std::string> folders;
        if (const char* env = std::getenv("R2D_SYMBOLS"))
            for (std::string& f : r2d::splitFolderList(env))
                folders.push_back(std::move(f));
        const std::filesystem::path exe = e2d::executableFolder();
        if (!exe.empty()) {
            const std::filesystem::path candidates[] = {
                exe / "symbols", exe.parent_path() / "symbols",
                exe.parent_path() / "render2d" / "render2d" / "symbols",                   // the submodule (§79)
                exe.parent_path().parent_path() / "render2d" / "render2d" / "symbols"};    // beside, at home
            std::error_code ec;
            for (const std::filesystem::path& c : candidates)
                if (std::filesystem::is_directory(c, ec)) {
                    folders.push_back(c.lexically_normal().string());
                    break;
                }
        }
        r2d::setSymbolFolders(std::move(folders));
    }

    e2d::Application application;
    if (!application.init(config))
        return 1;
    application.addFiles(files);   // §39: as if dropped

    application.run();
    return 0;
}
