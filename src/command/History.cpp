#include "command/History.h"

namespace e2d {

void History::execute(std::unique_ptr<Command> command, CommandContext& ctx)
{
    command->apply(ctx);
    m_past.push_back(std::move(command));
    m_future.clear();   // a new action: the old future is gone

    if (m_past.size() > kMaxEntries)
        m_past.erase(m_past.begin());   // forget the oldest
}

bool History::undo(CommandContext& ctx)
{
    if (m_past.empty())
        return false;
    std::unique_ptr<Command> command = std::move(m_past.back());
    m_past.pop_back();
    command->revert(ctx);
    m_future.push_back(std::move(command));
    return true;
}

bool History::redo(CommandContext& ctx)
{
    if (m_future.empty())
        return false;
    std::unique_ptr<Command> command = std::move(m_future.back());
    m_future.pop_back();
    command->apply(ctx);
    m_past.push_back(std::move(command));   // the future stays: redo is not a new action
    return true;
}

} // namespace e2d
