#include "panels/ConnectDialog.h"

#include "panels/LayerList.h"

#include <imgui.h>
#include <imgui_stdlib.h>          // InputText on a std::string

#include <fstream>
#include <sstream>

namespace e2d {

namespace {

// Next to imgui.ini, in the folder carto2D is started from. No password.
// Until 0.6 (editor2d) the file was editor2d-connections.txt: read when the
// new one is not there yet, so the recent connections are not lost (§79, 7a).
const char* kRecentFile = "carto2d-connections.txt";
const char* kOldRecentFile = "editor2d-connections.txt";

const ImVec4 kRed(1.0f, 0.45f, 0.40f, 1.0f);

std::string recentLabel(const DbConnection& c)
{
    std::string s = (c.user.empty() ? "" : c.user + "@") + c.host;
    if (c.port != "5432")
        s += ":" + c.port;
    return s + "/" + c.dbname;
}

// One row of the form: the label in a column on the left.
void label(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(ImGui::GetFontSize() * 6.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

} // namespace

ConnectDialog::ConnectDialog(std::function<void()> ring) : m_browser(std::move(ring))
{
    loadRecent();
    if (!m_recent.empty())
        m_fields = m_recent.front();
}

void ConnectDialog::open()
{
    m_open = true;
    ImGui::SetWindowFocus("Connect to database");
}

void ConnectDialog::loadRecent()
{
    std::ifstream in(kRecentFile, std::ios::binary);
    if (!in)
        in.open(kOldRecentFile, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    m_recent = parseRecent(text.str());
}

void ConnectDialog::saveRecent() const
{
    std::ofstream(kRecentFile, std::ios::binary) << recentText(m_recent);
}

void ConnectDialog::connect()
{
    // §65: in render2d's memory for this session, added only when GDAL opens
    // this connection (the PGPASSWORD variable was not seen by libpq.dll on
    // Windows). Empty: pgpass.conf, or a password typed earlier, stays.
    if (!m_password.empty())
        r2d::setSessionPassword(connectionString(m_fields), m_password);
    m_password.clear();   // not kept in the dialog either
    m_asked = m_fields;
    m_listing.reset();
    m_chosen.clear();
    m_browser.list(connectionString(m_fields));
}

std::vector<std::string> ConnectDialog::draw()
{
    std::vector<std::string> add;
    // The answer arrives even when the window is closed: it waits for it.
    if (std::optional<DbListing> answer = m_browser.take()) {
        if (answer->error.empty()) {
            rememberConnection(m_recent, m_asked);
            saveRecent();
        }
        m_listing = std::move(answer);
    }
    if (!m_open)
        return add;

    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 34.0f, em * 26.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Connect to database", &m_open)) {
        ImGui::End();
        return add;
    }

    // The connections that worked before.
    if (!m_recent.empty()) {
        label("recent");
        if (ImGui::BeginCombo("##recent", recentLabel(m_fields).c_str())) {
            for (const DbConnection& c : m_recent)
                if (ImGui::Selectable(recentLabel(c).c_str(), c == m_fields))
                    m_fields = c;
            ImGui::EndCombo();
        }
    }
    label("host");
    ImGui::InputTextWithHint("##host", "an address or a name", &m_fields.host);
    label("port");
    ImGui::InputText("##port", &m_fields.port, ImGuiInputTextFlags_CharsDecimal);
    label("database");
    ImGui::InputText("##db", &m_fields.dbname);
    label("user");
    ImGui::InputText("##user", &m_fields.user);
    label("password");
    ImGui::InputTextWithHint("##pw", "empty: PGPASSWORD or pgpass.conf", &m_password, ImGuiInputTextFlags_Password);
    ImGui::SetItemTooltip("kept in memory for this session only, never written (notions §63, §65)");

    const bool busy = m_browser.busy();
    ImGui::BeginDisabled(busy || m_fields.host.empty() || m_fields.dbname.empty());
    if (ImGui::Button("Connect"))
        connect();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (busy)
        ImGui::TextDisabled("connecting to %s... (at most 5 s)", m_asked.host.c_str());

    // The answer: GDAL's message, or the tables.
    if (m_listing && !busy) {
        ImGui::Separator();
        if (!m_listing->error.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kRed, "%s", m_listing->error.c_str());
            const std::string& e = m_listing->error;
            if (e.find("password") != std::string::npos)
                ImGui::TextDisabled("type the password, or put it in pgpass.conf (notions §63)");
            else if (e.find("timeout") != std::string::npos || e.find("Connection refused") != std::string::npos)
                ImGui::TextDisabled("the server does not answer: listen_addresses, pg_hba.conf, firewall (§63)");
            ImGui::PopTextWrapPos();
        } else if (m_listing->tables.empty()) {
            ImGui::TextDisabled("connected (%.0f ms): no table with a geometry column", m_listing->ms);
        } else {
            ImGui::TextDisabled("%zu table(s), %.0f ms; tick, or double-click one", m_listing->tables.size(),
                                m_listing->ms);
            for (const std::string& name : drawLayerList(m_listing->tables, m_chosen).add)   // §66: shared list
                add.push_back(m_listing->source + "::" + name);
        }
    }
    ImGui::End();
    return add;
}

} // namespace e2d
