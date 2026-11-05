#include "panels/HistoryPanel.h"

#include <imgui.h>

namespace e2d {

void drawHistoryPanel(Commander& commander)
{
    ImGui::Begin("History");   // docked on the left by default (Workspace)

    const History& h = commander.history();
    ImGui::BeginDisabled(!h.canUndo());
    if (ImGui::Button("Undo (Ctrl+Z)"))
        commander.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!h.canRedo());
    if (ImGui::Button("Redo (Ctrl+Y)"))
        commander.redo();
    ImGui::EndDisabled();
    ImGui::Separator();

    if (h.past().empty() && h.future().empty())
        ImGui::TextDisabled("(nothing done yet)");

    // The past, oldest first; the last one done marked.
    for (std::size_t i = 0; i < h.past().size(); ++i) {
        const bool last = i + 1 == h.past().size();
        ImGui::Text("%s %s", last ? ">" : " ", h.past()[i]->label().c_str());
    }
    // The future, next to redo first, in grey.
    for (auto it = h.future().rbegin(); it != h.future().rend(); ++it)
        ImGui::TextDisabled("  %s", (*it)->label().c_str());

    ImGui::End();
}

void drawEventsPanel(const std::deque<Event>& events)
{
    ImGui::Begin("Events");   // docked at the bottom by default, next to Report
    for (const Event& e : events)
        ImGui::TextUnformatted(e.dump().c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);   // stay at the bottom while new ones come
    ImGui::End();
}

} // namespace e2d
