#pragma once

#include "command/Command.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace e2d {

// ============================================================================
// History — the two stacks of notions §15, copied from editor3d.
//
//     do X  : apply X,              push X on the past,   CLEAR the future
//     undo  : pop the past,  revert, push it on the future
//     redo  : pop the future, apply, push it on the past
//
// A new action clears the future: the undone commands were recorded in a
// world that no longer exists. (editor3d's record() and "dirty" flag are not
// needed yet: nothing changes live over frames, nothing is saved.)
// ============================================================================
class History {
public:
    void execute(std::unique_ptr<Command> command, CommandContext& ctx);

    bool undo(CommandContext& ctx);   // false if there was nothing to undo
    bool redo(CommandContext& ctx);

    void clear() { m_past.clear(); m_future.clear(); }   // §53: a new project

    bool canUndo() const { return !m_past.empty(); }
    bool canRedo() const { return !m_future.empty(); }

    // Oldest first. The top of each stack is its back.
    const std::vector<std::unique_ptr<Command>>& past() const { return m_past; }
    const std::vector<std::unique_ptr<Command>>& future() const { return m_future; }

    // Beyond this, the oldest entries are forgotten.
    static constexpr std::size_t kMaxEntries = 200;

private:
    std::vector<std::unique_ptr<Command>> m_past;
    std::vector<std::unique_ptr<Command>> m_future;
};

} // namespace e2d
