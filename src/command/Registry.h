#pragma once

#include "command/Command.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace e2d {

// ============================================================================
// Registry — from a JSON command to a C++ Command (notions §16).
//
//   {"name": "layer.setVisible", "layer": "Topo_points", "visible": false}
//        │ find "layer.setVisible", check the parameters, call its factory
//        ▼
//   SetLayerVisibleCommand
//
// The interface and the future API go through here, the same way. Invalid
// data is refused with a message, never half executed.
// ============================================================================

struct ParamSpec {
    std::string name;
    std::string type;          // "string", "boolean", "number", "array"
    std::string description;
    bool        optional = false;   // may be absent; checked when present
};

// Builds the command from checked JSON. Returns nullptr with `error` set when
// the data is wrong (unknown layer...), or nullptr with `error` empty when
// there is nothing to do (the layer is already visible): no history entry.
using CommandFactory =
    std::function<std::unique_ptr<Command>(const nlohmann::json& j, const Document& doc, std::string& error)>;

struct CommandSpec {
    std::string            name;
    std::string            description;
    std::vector<ParamSpec> params;   // required, unless marked optional
    CommandFactory         make;
};

class Registry {
public:
    void add(CommandSpec spec);

    const CommandSpec* find(const std::string& name) const;

    // "name" present and known, every parameter present with its type, then
    // the factory. See CommandFactory for the meaning of nullptr.
    std::unique_ptr<Command> create(const nlohmann::json& j, const Document& doc, std::string& error) const;

    // Every command with its parameters: the future GET /commands.
    nlohmann::json describe() const;

private:
    std::map<std::string, CommandSpec> m_specs;   // sorted by name
};

} // namespace e2d
