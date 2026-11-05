#pragma once

#include "command/Document.h"
#include "event/EventBus.h"

#include <nlohmann/json.hpp>

#include <string>

namespace e2d {

// What a command may touch: the document, and the bus to tell the others.
struct CommandContext {
    Document& doc;
    EventBus& bus;
};

// ============================================================================
// Command — one change that knows how to undo itself (notions §15).
// The pattern of editor3d's scene/History, plus one thing: every command can
// be written as JSON (notions §16), the form the API will use at step 3.
//
//   apply  : make the change, or make it again (redo); publish its event
//   revert : put back exactly what was there before; publish its event
// ============================================================================
class Command {
public:
    virtual ~Command() = default;

    virtual void apply(CommandContext& ctx)  = 0;
    virtual void revert(CommandContext& ctx) = 0;

    // What the History panel shows: "Hide UrbISBuildings_04000_Addresses".
    virtual std::string label() const = 0;

    // The JSON that would create this command again (the journal, §16).
    virtual nlohmann::json toJson() const = 0;
};

} // namespace e2d
