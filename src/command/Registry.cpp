#include "command/Registry.h"

namespace e2d {

namespace {

bool hasType(const nlohmann::json& v, const std::string& type)
{
    if (type == "string")  return v.is_string();
    if (type == "boolean") return v.is_boolean();
    if (type == "number")  return v.is_number();
    if (type == "array")   return v.is_array();
    return false;
}

} // namespace

void Registry::add(CommandSpec spec)
{
    const std::string name = spec.name;
    m_specs[name] = std::move(spec);
}

const CommandSpec* Registry::find(const std::string& name) const
{
    const auto it = m_specs.find(name);
    return it == m_specs.end() ? nullptr : &it->second;
}

std::unique_ptr<Command> Registry::create(const nlohmann::json& j, const Document& doc, std::string& error) const
{
    error.clear();
    if (!j.is_object() || !j.contains("name") || !j["name"].is_string()) {
        error = "a command is a JSON object with a \"name\"";
        return nullptr;
    }
    const std::string name = j["name"];
    const CommandSpec* spec = find(name);
    if (!spec) {
        error = "unknown command '" + name + "'";
        return nullptr;
    }
    for (const ParamSpec& p : spec->params) {
        if (!j.contains(p.name)) {
            if (p.optional)
                continue;
            error = name + ": missing parameter '" + p.name + "' (" + p.type + ")";
            return nullptr;
        }
        if (!hasType(j[p.name], p.type)) {
            error = name + ": parameter '" + p.name + "' must be a " + p.type;
            return nullptr;
        }
    }
    return spec->make(j, doc, error);
}

nlohmann::json Registry::describe() const
{
    nlohmann::json all = nlohmann::json::array();
    for (const auto& [name, spec] : m_specs) {
        nlohmann::json params = nlohmann::json::array();
        for (const ParamSpec& p : spec.params)
            params.push_back(
                {{"name", p.name}, {"type", p.type}, {"description", p.description}, {"optional", p.optional}});
        all.push_back({{"name", name}, {"description", spec.description}, {"params", params}});
    }
    return all;
}

} // namespace e2d
