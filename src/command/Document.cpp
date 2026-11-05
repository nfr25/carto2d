#include "command/Document.h"

#include <map>

namespace e2d {

int Document::findLayer(const std::string& id) const
{
    for (std::size_t i = 0; i < layers.size(); ++i)
        if (layers[i].id == id)
            return static_cast<int>(i);
    return -1;
}

std::size_t Document::selectionCount() const
{
    std::size_t n = 0;
    for (const auto& [layer, fids] : selection)
        n += fids.size();
    return n;
}

std::vector<std::string> uniqueIds(const std::vector<std::string>& names)
{
    std::map<std::string, int> seen;
    std::vector<std::string> ids;
    for (const auto& name : names) {
        const int n = ++seen[name];
        ids.push_back(n == 1 ? name : name + "#" + std::to_string(n));
    }
    return ids;
}

std::string uniqueId(const Document& doc, const std::string& name)
{
    if (doc.findLayer(name) < 0)
        return name;
    for (int n = 2;; ++n) {
        const std::string id = name + "#" + std::to_string(n);
        if (doc.findLayer(id) < 0)
            return id;
    }
}

} // namespace e2d
