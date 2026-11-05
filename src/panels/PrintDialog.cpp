#include "panels/PrintDialog.h"

#include "window/FileDialog.h"

#include <imgui.h>
#include <imgui_stdlib.h>   // InputText on a std::string

#include <cmath>
#include <cstdio>

namespace e2d {

namespace {

const char* const kFormats[] = {"A4 landscape", "A4 portrait", "A3 landscape", "A3 portrait"};
const double kScales[] = {200, 500, 1000, 2000, 2500, 5000, 10000, 20000, 25000, 50000, 100000};

int formatIndex(const r2d::PageSpec& p)
{
    return (p.format == "A3" ? 2 : 0) + (p.landscape ? 0 : 1);
}

} // namespace

void PrintDialog::open(const std::optional<r2d::PageSpec>& project, const std::string& path, double fitScale)
{
    if (project) {
        m_page = *project;
        m_page.center.reset();   // the map's centre, at writing time
    }
    m_scale = m_page.scale ? *m_page.scale : fitScale;
    if (m_path.empty())
        m_path = path;
    m_status.clear();
    m_open = true;
}

bool PrintDialog::draw(GLFWwindow* window, double fitScale, bool writing)
{
    if (!m_open)
        return false;
    bool write = false;
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Print", &m_open, ImGuiWindowFlags_NoCollapse)) {
        // The sheet.
        int f = formatIndex(m_page);
        if (ImGui::Combo("page", &f, kFormats, 4)) {
            m_page.format    = f < 2 ? "A4" : "A3";
            m_page.landscape = f % 2 == 0;
        }

        // The scale: typed, chosen, or the one that fits the view.
        ImGui::SetNextItemWidth(120);
        ImGui::InputDouble("##scale", &m_scale, 0.0, 0.0, "1 : %.0f");
        if (m_scale < 1.0)
            m_scale = 1.0;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        if (ImGui::BeginCombo("##scales", "round values")) {
            for (double s : kScales) {
                char label[32];
                std::snprintf(label, sizeof label, "1 : %.0f", s);
                if (ImGui::Selectable(label, std::abs(s - m_scale) < 0.5))
                    m_scale = s;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("fit the view"))
            m_scale = fitScale;
        ImGui::SameLine();
        ImGui::TextUnformatted("scale");
        m_page.scale = m_scale;

        ImGui::InputText("title", &m_page.title);
        ImGui::InputText("subtitle", &m_page.subtitle);
        ImGui::Checkbox("legend", &m_page.legend);
        ImGui::SameLine();
        ImGui::Checkbox("scale bar", &m_page.scaleBar);
        ImGui::SameLine();
        ImGui::Checkbox("north arrow", &m_page.northArrow);

        ImGui::Separator();
        ImGui::SetNextItemWidth(fileDialogAvailable() ? -90.0f : -1.0f);
        ImGui::InputText("##path", &m_path);
        if (fileDialogAvailable()) {
            ImGui::SameLine();
            if (ImGui::Button("Choose..."))
                if (const auto chosen = choosePdfFile(window, m_path))
                    m_path = *chosen;
        }
        ImGui::Checkbox("open the PDF once written", &m_openAfter);

        ImGui::TextDisabled("The page's outline is on the map: move and zoom the map to place it.");
        ImGui::BeginDisabled(writing || m_path.empty());
        if (ImGui::Button(writing ? "Writing..." : "Write the page", ImVec2(160, 0)))
            write = true;
        ImGui::EndDisabled();
        if (!m_status.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, m_error ? ImVec4(0.95f, 0.45f, 0.4f, 1) : ImVec4(0.6f, 0.85f, 0.6f, 1));
            ImGui::TextWrapped("%s", m_status.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
    return write;
}

} // namespace e2d
