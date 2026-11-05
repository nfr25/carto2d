#pragma once

#include <string>
#include <vector>

namespace e2d {

// §78: a point symbol's image (PNG or SVG) in the legend dialog. Pure
// functions on paths, tested without a window (symbol_image_test).

// The folder a relative image path is read from: the style file's (as
// render2d reads it, its §42), else the project's, else "" (absolute only).
std::string imageBase(const std::string& styleFile, const std::string& configPath);

// The path as the dialog shows it, as the style file will hold it
// (render2d's symbolUrl, its §76): relative to `base` ("icons/arbre.svg"),
// else a library's name ("nature/park.svg"), else whole.
std::string imageShown(const std::string& path, const std::string& base);

// Where the file dialog opens: the current image's folder, else base/icons
// when it exists, else the first symbol library holding something (the
// user's, then carto2D's), else base.
std::string imageStartFolder(const std::string& current, const std::string& base);

// A path typed or chosen, checked: `path` is the whole path to put in the
// style, or `error` says why not (not found, not .png / .svg, an SVG
// render2d cannot read). A name is looked for as render2d does (§76): the
// style's folder, then the libraries. `outside`: the style has a file and
// the image is neither under its folder nor in a library, so the style will
// hold a whole path.
struct ImageCheck {
    std::string path;
    std::string error;
    bool        outside = false;
};
ImageCheck checkImage(const std::string& typed, const std::string& base, bool styleHasFile);

// §78: the library as the symbol picker shows it: one family per folder of
// the symbol libraries ("nature", "transport"... "user"), its .svg and .png
// files found in it and below (a folder made in user/ shows there), sorted.
// The same family in two libraries is one. Files lying directly in a
// library form a family "other". Order: the shipped families, "other", "user".
struct SymbolFamily {
    std::string              name;
    std::vector<std::string> files;   // whole paths
};
std::vector<SymbolFamily> libraryFamilies();

} // namespace e2d
