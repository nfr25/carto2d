#include "panels/SymbolImage.h"

#include "render/svg_symbol.hpp"   // render2d §75: isSvgFile, svgSize
#include "style/symbol_folders.hpp" // render2d §76: findSymbol, symbolUrl, the libraries

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace e2d {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string imageBase(const std::string& styleFile, const std::string& configPath)
{
    if (!styleFile.empty())
        return fs::path(styleFile).parent_path().generic_string();
    if (!configPath.empty())
        return fs::path(configPath).parent_path().generic_string();
    return {};
}

std::string imageShown(const std::string& path, const std::string& base)
{
    return path.empty() ? std::string() : r2d::symbolUrl(path, base);
}

std::string imageStartFolder(const std::string& current, const std::string& base)
{
    std::error_code ec;
    if (!current.empty() && fs::is_directory(fs::path(current).parent_path(), ec))
        return fs::path(current).parent_path().generic_string();
    if (!base.empty() && fs::is_directory(fs::path(base) / "icons", ec))
        return (fs::path(base) / "icons").generic_string();
    for (const std::string& folder : r2d::symbolFolders())   // §78: the user's, then carto2D's
        if (fs::is_directory(folder, ec) && fs::directory_iterator(folder, ec) != fs::directory_iterator())
            return fs::path(folder).generic_string();
    return base;
}

std::vector<SymbolFamily> libraryFamilies()
{
    std::map<std::string, std::vector<std::string>> byName;
    auto isImage = [](const fs::path& p) {
        const std::string ext = lower(p.extension().string());
        return ext == ".svg" || ext == ".png";
    };
    std::error_code ec;
    for (const std::string& folder : r2d::symbolFolders()) {
        if (!fs::is_directory(folder, ec))
            continue;
        for (const fs::directory_entry& e : fs::directory_iterator(folder, ec)) {
            if (e.is_directory(ec)) {
                std::vector<std::string>& files = byName[e.path().filename().string()];
                for (auto it = fs::recursive_directory_iterator(e.path(), ec); it != fs::recursive_directory_iterator();
                     it.increment(ec))
                    if (it->is_regular_file(ec) && isImage(it->path()))
                        files.push_back(it->path().generic_string());
            } else if (e.is_regular_file(ec) && isImage(e.path())) {
                byName["other"].push_back(e.path().generic_string());
            }
        }
    }
    std::vector<SymbolFamily> out;
    for (auto& [name, files] : byName) {
        if (files.empty())
            continue;   // user/ with only its README: no tab
        std::sort(files.begin(), files.end());
        out.push_back({name, std::move(files)});
    }
    // The shipped families (alphabetical), then "other", then the user's own, last.
    auto rank = [](const SymbolFamily& f) { return f.name == "user" ? 2 : f.name == "other" ? 1 : 0; };
    std::stable_sort(out.begin(), out.end(),
                     [&](const SymbolFamily& a, const SymbolFamily& b) { return rank(a) < rank(b); });
    return out;
}

ImageCheck checkImage(const std::string& typed, const std::string& base, bool styleHasFile)
{
    ImageCheck c;
    const std::string ext = lower(fs::path(typed).extension().string());
    // §78: as render2d reads it: the style's folder, then the libraries.
    const fs::path p = fs::path(r2d::findSymbol(typed, base)).lexically_normal();
    if (ext != ".png" && ext != ".svg") {
        c.error = "an image is a .png or a .svg file";
    } else if (p.empty()) {
        c.error = "not found: " + typed + (fs::path(typed).is_absolute() ? "" : " (beside the style, nor in a symbol library)");
    } else if (r2d::isSvgFile(p.string())) {
        std::string error;
        if (!r2d::svgSize(p.string(), error))
            c.error = error;   // render2d could not draw it: say it now, not on the map
    }
    if (!c.error.empty())
        return c;
    c.path    = p.generic_string();
    c.outside = styleHasFile && fs::path(r2d::symbolUrl(c.path, base)).is_absolute();
    return c;
}

} // namespace e2d
