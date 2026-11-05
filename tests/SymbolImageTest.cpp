// A point symbol's image in the legend dialog (notions §78): the folder paths
// are relative to, what the dialog shows, where the file window opens, and
// the check of a path typed or chosen. No window.

#include "panels/SymbolImage.h"

#include "style/symbol_folders.hpp"   // render2d §76

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

namespace fs = std::filesystem;

} // namespace

int main()
{
    using namespace e2d;
    // A project as on disk: project.yaml, styles/topo.yaml, styles/icons/arbre.svg,
    // styles/icons/bus.png, styles/icons/broken.svg, elsewhere/far.svg.
    const fs::path root = fs::temp_directory_path() / "e2d_symbol_image";
    fs::remove_all(root);
    fs::create_directories(root / "styles" / "icons");
    fs::create_directories(root / "elsewhere");
    const char* svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><circle cx="16" cy="16" r="10" fill="#2F7D4F"/></svg>)";
    std::ofstream(root / "styles" / "icons" / "arbre.svg") << svg;
    std::ofstream(root / "elsewhere" / "far.svg") << svg;
    std::ofstream(root / "styles" / "icons" / "bus.png") << "x";   // the check does not open a PNG
    std::ofstream(root / "styles" / "icons" / "broken.svg") << "not an svg";
    std::ofstream(root / "styles" / "icons" / "photo.jpg") << "x";
    const std::string project = (root / "project.yaml").generic_string();
    const std::string style   = (root / "styles" / "topo.yaml").generic_string();
    const std::string styles  = (root / "styles").generic_string();
    const std::string tree    = (root / "styles" / "icons" / "arbre.svg").generic_string();

    // The base: the style file's folder, else the project's.
    check(imageBase(style, project) == styles, "base: the style's folder");
    check(imageBase("", project) == root.generic_string(), "base: no style file, the project's folder");
    check(imageBase("", "").empty(), "base: neither, none");

    // Shown relative when under the base, as the style file will hold it.
    check(imageShown(tree, styles) == "icons/arbre.svg", "shown: icons/arbre.svg");
    check(imageShown((root / "elsewhere" / "far.svg").generic_string(), styles) ==
              (root / "elsewhere" / "far.svg").generic_string(), "shown: outside, the whole path");
    check(imageShown("", styles).empty(), "shown: no image, nothing");

    // The file window opens in the image's folder, else icons/, else the base.
    check(imageStartFolder(tree, styles) == (root / "styles" / "icons").generic_string(), "start: the image's folder");
    check(imageStartFolder("", styles) == (root / "styles" / "icons").generic_string(), "start: the style's icons/");
    check(imageStartFolder("", root.generic_string()) == root.generic_string(), "start: no icons/, the base");

    // The check.
    ImageCheck c = checkImage("icons/arbre.svg", styles, true);
    check(c.error.empty() && c.path == tree && !c.outside, "typed relative: the whole path, inside");
    c = checkImage(tree, styles, true);
    check(c.error.empty() && c.path == tree, "chosen whole: the same");
    c = checkImage("icons/bus.png", styles, true);
    check(c.error.empty(), "a PNG");
    c = checkImage((root / "elsewhere" / "far.svg").generic_string(), styles, true);
    check(c.error.empty() && c.outside, "outside the style's folder: accepted, said");
    c = checkImage((root / "elsewhere" / "far.svg").generic_string(), root.generic_string(), false);
    check(c.error.empty() && !c.outside, "no style file yet: nothing to say");
    c = checkImage("icons/nothere.svg", styles, true);
    check(c.path.empty() && c.error.find("not found") != std::string::npos, "missing: not found");
    c = checkImage("icons/photo.jpg", styles, true);
    check(c.path.empty() && c.error.find(".png or a .svg") != std::string::npos, "a JPEG: refused");
    c = checkImage("icons/broken.svg", styles, true);
    check(c.path.empty() && c.error.find("broken.svg") != std::string::npos, "an SVG render2d cannot read: refused");

    // §78: the symbol libraries: the user's (empty at first), then carto2D's.
    const fs::path mine = root / "mine", shipped = root / "shipped";
    fs::create_directories(mine);
    fs::create_directories(shipped / "nature");
    std::ofstream(shipped / "nature" / "park.svg") << svg;
    r2d::setSymbolFolders({mine.string(), shipped.string()});
    const std::string park = (shipped / "nature" / "park.svg").generic_string();

    c = checkImage("nature/park.svg", styles, true);
    check(c.error.empty() && c.path == park && !c.outside, "a library's name typed: found there, nothing to say");
    c = checkImage(park, styles, true);
    check(c.error.empty() && !c.outside, "chosen in a library: not outside");
    check(imageShown(park, styles) == "nature/park.svg", "shown: the library's name, not its folder");
    check(imageShown(tree, styles) == "icons/arbre.svg", "shown: beside the style still first");
    c = checkImage("nature/gone.svg", styles, true);
    check(c.path.empty() && c.error.find("nor in a symbol library") != std::string::npos, "missing everywhere: said");
    check(imageStartFolder("", root.generic_string()) == shipped.generic_string(),
          "start: the user's library empty, carto2D's");
    std::ofstream(mine / "mine.svg") << svg;
    check(imageStartFolder("", root.generic_string()) == mine.generic_string(), "start: the user's library, once filled");
    check(imageStartFolder("", styles) == (root / "styles" / "icons").generic_string(), "start: the style's icons/ first");

    // The picker's families: one per folder, files below included, user last.
    fs::create_directories(mine / "user" / "trees");
    std::ofstream(mine / "user" / "trees" / "oak.svg") << svg;
    std::ofstream(mine / "user" / "README.txt") << "not an image";
    fs::create_directories(shipped / "user");                      // the shipped empty user/: no tab of its own
    std::ofstream(shipped / "nature" / "tree.png") << "x";
    std::ofstream(shipped / "nature" / "notes.txt") << "x";
    const std::vector<SymbolFamily> families = libraryFamilies();
    check(families.size() == 3, "families: nature, other, user");
    if (families.size() == 3) {
        check(families[0].name == "nature" && families[0].files.size() == 2, "nature: its .svg and .png, not the .txt");
        check(families[1].name == "other" && families[1].files.size() == 1, "other: a file lying in a library");
        check(families[2].name == "user" && families[2].files.size() == 1 &&
                  families[2].files[0].find("trees/oak.svg") != std::string::npos, "user: last, a sub-folder's symbol");
    }
    r2d::setSymbolFolders({});
    check(libraryFamilies().empty(), "no library: no family");

    fs::remove_all(root);
    std::printf("%s\n", g_failures == 0 ? "all ok" : "FAILURES");
    return g_failures == 0 ? 0 : 1;
}
