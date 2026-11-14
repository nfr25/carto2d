#include "Application.h"

#include "Version.h"
#include "panels/HistoryPanel.h"
#include "panels/LayersPanel.h"
#include "panels/SelectionPanel.h"
#include "panels/TablePanel.h"
#include "panels/Toolbar.h"
#include "view/ShapeOverlay.h"
#include "worker/LayerProbe.h"
#include "window/Platform.h"   // §71
#include "backend/gdal/gdal_reproject.hpp"   // §69: the map's system
#include "window/FileDialog.h"
#include "style/ysld_reader.hpp"   // render2d

// render2d
#include "app/version.hpp"
#include "backend/cairo/cairo_target.hpp"
#include "backend/gdal/gdal_layer.hpp"

#include <algorithm>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>   // InputText on a std::string

#include <cmath>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>

namespace e2d {

namespace {

constexpr int    kInitialWidth  = 1600; // logical pixels: 2400 x 1350 at 150 %
constexpr int    kInitialHeight = 900;
constexpr double kNoticeSeconds = 4.0;  // how long a notice stays in the status bar
constexpr double kAwakeSeconds  = 0.5;  // §56: frames go on this long after the last input
constexpr double kNapSeconds    = 0.5;  // §56: asleep, a look at the clock every half second

const ImVec4 kYellow(1.0f, 0.85f, 0.35f, 1.0f);
const ImVec4 kRed(1.0f, 0.45f, 0.40f, 1.0f);
const ImVec4 kGrey(0.65f, 0.65f, 0.65f, 1.0f);

} // namespace

bool Application::init(const std::string& configPath)
{
    const std::string title = std::string("carto2D ") + kVersion;
    if (!m_window.init(kInitialWidth, kInitialHeight, title.c_str())) {
        if (!m_window.failure().empty())
            showError(title, m_window.failure());   // §82: an old card says so, rather than nothing
        return false;
    }

    const float scale = m_window.contentScale();
    if (!m_imgui.init(m_window.handle(), scale))
        return false;

    m_commander.setLayerProbe(probeSource);   // §38: layer.add opens a source once, here
    // §41: layer.setStyle is checked by render2d's reader; what does not read is refused.
    m_commander.setStyleCheck([](const std::string& ysld, std::string& error) {
        const r2d::YsldResult r = r2d::parseYsld(ysld);
        if (!r.style)
            error = r.error;
        return r.style.has_value();
    });
    subscribe();
    // §81: what the map says goes to the status bar, a refused command to the Report.
    m_map.setMessages([this](const std::string& text) { notify(text); },
                      [this](const std::string& text) { m_commandError = text; });
    // §56: a worker that posts a result rings the bell of the main thread,
    // which may be asleep.
    m_map.start(scale, &Window::ring);

    // The configuration (render2d's reader, its §57). A bad or missing file
    // does not stop the program: the window opens, says why, and files can be
    // dropped on it (§39).
    if (!configPath.empty()) {
        std::string error;
        if (!loadProject(configPath, error)) {
            m_error = error;
            m_config.config.path = configPath;
        }
    }
    updateTitle();
    return true;
}

// §52: the map of a viewer.yaml, in place of the current one. Nothing changes
// when the file cannot be read.
bool Application::loadProject(const std::string& path, std::string& error)
{
    r2d::ConfigResult config = r2d::loadConfig(path);
    if (!config.ok) {
        error = config.error;
        return false;
    }
    clearProject();
    m_config      = std::move(config);
    m_projectPath = path;
    if (m_config.config.startBbox)
        m_map.setStartBbox(*m_config.config.startBbox);
    m_map.setCrsChosen(m_config.config.crs);   // §69: "" = the first layer's

    // The layers of the file, straight into the Document (not commands: there
    // is nothing to undo). Each one is probed like a dropped file (§38). One
    // that does not open is kept aside, and written back where it was (§52).
    for (const r2d::LayerSpec& spec : m_config.config.layers) {
        std::string why;
        const std::optional<ProbedLayer> p = probeSource(spec.source, why);
        if (!p) {
            m_startWarnings.push_back(why);
            m_unopened.emplace_back(m_doc.layers.size() + m_unopened.size(), spec);
            continue;
        }
        LayerEntry e(uniqueId(m_doc, p->name), p->geometry, spec.visible);   // visible: false in the file
        e.source  = spec.source;
        e.style   = spec.style.value_or("");
        e.title   = spec.title;
        e.colour  = m_doc.nextColour++;
        e.fields  = p->fields;
        e.numeric = p->numeric;
        e.extent  = p->extent;
        e.crs     = p->crs;
        e.crsDeclared = spec.crs;   // §69: crs: of the layer in the project
        e.web     = p->web;         // §76
        e.attribution = spec.attribution;   // §76
        e.legend      = spec.legend;        // render2d §74
        m_doc.layers.push_back(std::move(e));
    }
    ++m_doc.revision;   // never back: the threads compare numbers (§37)
    m_map.decideCrs();          // §69: before the extents, which are converted to it
    if (!m_map.startBbox())     // no start: bbox in the file: all the layers (§76: the basemaps only if alone)
        m_map.setStartBbox(m_map.dataExtent());
    m_savedSignature = projectSignature();
    return true;
}

// §53: New, and the first half of Open. The history goes: Ctrl+Z does not
// reach into another project.
void Application::clearProject()
{
    m_doc.layers.clear();
    m_doc.selection.clear();
    m_doc.nextColour = 0;
    ++m_doc.revision;
    m_commander.clearHistory();
    m_styleDialog.reset();
    m_unopened.clear();
    m_startWarnings.clear();
    m_projectBackedUp.clear();
    m_projectPath.clear();
    m_savedSignature.clear();
    m_config = {};
    m_error.clear();
    m_map.reset();   // no system, no view, an empty image in place of the old map
}

void Application::run()
{
    for (;;) {
        // §45, §52: closing with something not saved: a question first.
        if (m_window.shouldClose()) {
            if (m_quit)
                break;
            m_window.setShouldClose(false);
            if (m_next == Next::None)
                request(Next::Quit);
            if (m_quit)
                break;
        }
        waitForReason();   // §56: draw only when there is a reason

        int fbWidth = 0, fbHeight = 0;
        m_window.framebufferSize(fbWidth, fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);
        // Around the image (a band uncovered by a drag): the map's background.
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_imgui.beginFrame();   // first: ImGui then knows if the mouse is over a panel
        drawStatusBar(statusText());   // before the dockspace: it takes its height off (§19)
        // The menus and the icons (§27, §53, §61): the same, at the top.
        ToolbarState barState;
        barState.tool         = m_map.tool();
        barState.canUndo      = m_commander.history().canUndo();
        barState.canRedo      = m_commander.history().canRedo();
        barState.hasSelection = !m_doc.selection.empty();
        barState.hasLayers    = !m_doc.layers.empty();
        barState.crsEpsg      = m_map.crs().epsg;   // §69
        const ToolbarResult bar = drawToolbar(barState);
        if (bar.tool)
            m_map.setTool(*bar.tool);
        fileAction(bar.file);   // §53
        toolbarActions(bar);    // §61
        m_area = dockspace();          // the panels around, the map in the middle
        handleDrops();
        // §81: the map, as a widget: the gestures, its keys, what the threads
        // sent, then the image, the selection (§60: the designated one) and the shapes.
        m_map.input(m_area, m_window.consumeMouse(), m_imgui.wantsMouse(), m_window.time());
        handleShortcuts();
        m_map.collect();
        if (auto text = m_map.takeWhereResult()) {   // §58: the panel says what came of it
            m_selectionPanel.whereBusy   = false;
            m_selectionPanel.whereResult = std::move(*text);
        }
        m_map.draw({m_selectionPanel.layer, m_selectionPanel.fid});
        drawEmptyHint();
        drawAboutWindow();   // §71
        drawPrint();         // §77
        drawReportPanel();
        {
            const LayersPanelResult r =
                drawLayersPanel(m_doc, m_map.outOfScale(), unsavedStyles(), m_layerSymbols,
                                m_map.nav().view().scaleDenominator() * m_window.contentScale(),   // as the status bar
                                m_window.contentScale(), m_commander, &m_symbolTextures, &m_map.counts());
            if (!r.error.empty())
                m_commandError = r.error;
            if (!r.zoomTo.empty())
                m_map.zoomToLayer(r.zoomTo);
            if (!r.styleOf.empty())
                m_styleDialog.open(r.styleOf);
        }
        {
            // §42, §43: the legend dialog. A layer without style file that was
            // saved now uses the new file (a file written cannot be unwritten:
            // not a command, not undone).
            const StyleDialogResult r = m_styleDialog.draw(m_doc, m_commander, m_projectPath, m_window.handle(), &m_symbolTextures);
            if (!r.notice.empty())
                notify(r.notice);
            for (const auto& [id, path] : r.savedFiles)
                if (const int i = m_doc.findLayer(id); i >= 0) {
                    m_doc.layers[static_cast<std::size_t>(i)].style = path;
                    ++m_doc.revision;
                }
        }
        // §65, §66: the tables chosen in "Connect to database", the layers
        // chosen in a GeoPackage: added as dropped files.
        drawCrsDialog();   // §70
        std::vector<std::string> chosen = m_connect.draw();
        for (std::string& source : m_chooser.draw())
            chosen.push_back(std::move(source));
        for (const std::string& source : chosen)
            queueLayer(source);
        readDroppedFiles();   // §66
        addNextLayer();
        drawSaveQuestion();
        drawPathQuestion();
        updateTitle();
        drawHistoryPanel(m_commander);
        if (drawSelectionPanel(m_doc, m_map.known(), m_commander, m_selectionPanel))
            m_map.zoomToSelection();
        if (m_selectionPanel.whereAsked)
            askWhere();   // §58
        drawEventsPanel(m_events);
        // §60: the table of the selection; a double click zooms on a row.
        if (const auto row = drawTablePanel(m_doc, m_map.known(), m_selectionPanel, m_table))
            if (const auto it = m_map.known().find(*row);
                it != m_map.known().end() && !it->second.geometry.empty() && m_map.started())
                m_map.fitWithMargin(it->second.geometry.envelope());
        // §17: commands queued by subscribers, then ONE render for all the
        // changes of this frame.
        for (const auto& error : m_commander.processQueue())
            m_commandError = error;
        m_map.endFrame();
        m_imgui.endFrame();

        m_window.swapBuffers();
    }
    m_map.stop();
}

// §58: an expression from the Selection panel goes to the map's thread for
// questions; the answer comes back as text (run, takeWhereResult).
void Application::askWhere()
{
    SelectionPanelState& s = m_selectionPanel;
    s.whereAsked = false;
    m_map.askWhere(s.whereLayer, s.whereText, s.whereMode);
    s.whereBusy = true;
    s.whereResult.clear();
}

void Application::notify(std::string text)
{
    m_notice   = std::move(text);
    m_noticeAt = m_window.time();
}

// Ctrl+Z, Ctrl+Y (and Ctrl+Shift+Z), Esc, unless a text field has the keyboard.
void Application::handleShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;
    m_map.keys();   // §81: Esc, R P W D A, Enter, Backspace
    // §53: the File menu's shortcuts.
    if (io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_N, false))
            fileAction(FileAction::New);
        else if (ImGui::IsKeyPressed(ImGuiKey_O, false))
            fileAction(FileAction::Open);
        else if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            fileAction(io.KeyShift ? FileAction::SaveAs : FileAction::Save);
        else if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
            fileAction(FileAction::Quit);
        else if (ImGui::IsKeyPressed(ImGuiKey_P, false) && !m_doc.layers.empty())
            fileAction(FileAction::Print);   // §77
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        m_commander.undo();
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
             ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        m_commander.redo();
}

// §17: the application is a subscriber like the others: every event is kept
// for the Events panel. The map subscribes on its own (§81).
void Application::subscribe()
{
    m_bus.subscribe("*", [this](const Event& e) {
        m_events.push_back(e);
        if (m_events.size() > 50)
            m_events.pop_front();
    });
}

// What the program was built with, and what GLFW says about the screen (§7).
// §71: Help > About carto2D (editor2d until 0.6): what was the Build panel (versions, graphics
// card, what GLFW says, §7), in a window opened on demand.
void Application::drawAboutWindow()
{
    if (!m_aboutOpen)
        return;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 30.0f, em * 22.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("About carto2D", &m_aboutOpen)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("carto2D - see, query, measure and print maps, on render2d");
    ImGui::TextDisabled("Copyright (c) 2026 Noel Frankinet");

    ImGui::SeparatorText("Versions");
    ImGui::Text("carto2D   %s (%s)", kVersion, kVersionStep);
    ImGui::Text("render2d  %s (%s)", r2d::kVersion, r2d::kVersionStep);
    ImGui::Text("GDAL      %s", r2d::gdalVersion().c_str());
    ImGui::Text("Cairo     %s", r2d::cairoVersion().c_str());
    ImGui::Text("ImGui     %s", IMGUI_VERSION);
    ImGui::Text("OpenGL    %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // §25: on a laptop, the integrated chip or the big card? The driver says.
    ImGui::SeparatorText("Graphics card");
    ImGui::TextWrapped("vendor    %s", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    ImGui::TextWrapped("renderer  %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    ImGui::SeparatorText("GLFW says (notions 7)");
    int winW = 0, winH = 0, fbW = 0, fbH = 0;
    m_window.windowSize(winW, winH);
    m_window.framebufferSize(fbW, fbH);
    const float scale = m_window.contentScale();
    ImGui::Text("window size       %d x %d", winW, winH);
    ImGui::Text("framebuffer size  %d x %d", fbW, fbH);
    ImGui::Text("content scale     %.2f", scale);
    ImGui::Text("logical size      %.0f x %.0f", fbW / scale, fbH / scale);

    ImGui::End();
}

// The configuration and its errors, render2d's report of the image shown,
// the warnings. (The mouse and the scale are in the status bar, §19.)
void Application::drawReportPanel()
{
    ImGui::Begin("Report");

    ImGui::TextWrapped("config: %s", m_config.config.path.empty() ? "(none: dropped files)" : m_config.config.path.c_str());
    if (!m_error.empty())
        ImGui::TextColored(kRed, "error: %s", m_error.c_str());
    for (const auto& w : m_config.warnings)   // an unknown key: often a typo (render2d §57)
        ImGui::TextColored(kYellow, "warning: %s", w.c_str());
    if (!m_commandError.empty())
        ImGui::TextColored(kRed, "refused: %s", m_commandError.c_str());

    if (!m_map.reportLines().empty()) {
        ImGui::Separator();
        for (const auto& line : m_map.reportLines())
            ImGui::TextUnformatted(line.c_str());
    }
    for (const auto& w : m_startWarnings)   // layers of the file that could not be opened
        ImGui::TextColored(kYellow, "warning: %s", w.c_str());
    for (const auto& w : m_map.warnings())
        ImGui::TextColored(kYellow, "warning: %s", w.c_str());

    ImGui::End();
}

// ============================================================================
// STEP 3a: LAYERS THAT COME AND GO (notions §37 to §39)
// ============================================================================

// §66: the answers of the file reader, then the next file to read.
void Application::readDroppedFiles()
{
    if (std::optional<DbListing> answer = m_fileReader.take()) {
        const std::string path = answer->source;
        m_reading.clear();
        // The tables without geometry (a GeoPackage's lists of values) are
        // not layers to draw: counted, not offered.
        std::vector<const r2d::LayerInfo*> drawable;
        for (const r2d::LayerInfo& l : answer->tables)
            if (l.hasGeometry)
                drawable.push_back(&l);
        if (drawable.size() > 1)
            m_chooser.offer(path, std::move(answer->tables));             // to tick
        else if (drawable.size() == 1 && answer->tables.size() > 1)
            queueLayer(path + "::" + drawable.front()->name);              // the only one, by its name
        else
            queueLayer(path);   // one layer, a raster, or unreadable: layer.add says what it can
    }
    if (m_reading.empty() && !m_fileReader.busy() && !m_toRead.empty()) {
        m_reading = m_toRead.front();
        m_toRead.pop_front();
        m_fileReader.list(m_reading);
    }
}

void Application::queueLayer(const std::string& source)
{
    if (m_toAdd.empty() && m_addDone >= m_addTotal) {   // a new batch
        m_addTotal = 0;
        m_addDone  = 0;
    }
    m_toAdd.push_back(source);
    ++m_addTotal;
}

// §66: one layer.add per frame: between two, the window is drawn and the
// status bar says "adding Buildings (2 of 18)".
void Application::addNextLayer()
{
    if (m_toAdd.empty())
        return;
    const std::string source = m_toAdd.front();
    m_toAdd.pop_front();
    ++m_addDone;
    std::string error;
    const std::size_t before = m_doc.layers.size();
    if (!m_commander.run({{"name", "layer.add"}, {"source", source}}, error))
        m_addErrors += (m_addErrors.empty() ? "" : "; ") + error;
    else if (m_doc.layers.size() > before)
        if (const int more = m_doc.layers.back().otherLayers; more > 0 && source.find("::") == std::string::npos)
            m_addErrors += (m_addErrors.empty() ? "" : "; ") + m_doc.layers.back().id + ": " +
                           std::to_string(more) + " more layer(s) in this file (the first one is added)";
    if (m_toAdd.empty() && !m_addErrors.empty()) {
        notify(m_addErrors);
        m_addErrors.clear();
    }
}

// §39: each dropped file becomes a layer.add, on top. One that cannot be
// opened is said; the others are added all the same. §66: read first, in a
// thread; added one per frame.
void Application::handleDrops()
{
    std::vector<std::string> paths = m_window.takeDrops();
    paths.insert(paths.begin(), m_pendingFiles.begin(), m_pendingFiles.end());
    m_pendingFiles.clear();
    // §53: a viewer.yaml dropped opens that project. §71: a .yaml that render2d
    // refuses is never tried as a layer: the reason is said instead (a style,
    // or the project's error).
    auto isYaml = [](const std::string& path) {
        std::string ext = std::filesystem::path(path).extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".yaml" || ext == ".yml";
    };
    for (const std::string& path : paths) {
        if (!isYaml(path))
            continue;
        const r2d::ConfigResult config = r2d::loadConfig(path);
        if (config.ok) {
            if (paths.size() > 1)
                notify("a project was dropped: it is opened, the other files are not added");
            request(Next::Open, path);
            return;
        }
        const std::string name = std::filesystem::path(path).filename().string();
        const r2d::YsldResult style = r2d::readYsldFile(path);
        if (style.style && !style.style->featureStyles.empty())
            notify(name + " is a style, not a project: Style... in a layer's menu");
        else
            notify(name + " is not a project: " + config.error);
    }
    for (std::string path : paths) {
        if (isYaml(path))
            continue;   // said above
        // A relative path (the command line) becomes complete: the project
        // may be saved in another folder (§52).
        if (path.rfind("PG:", 0) != 0 && std::filesystem::path(path).is_relative())
            path = std::filesystem::absolute(path).lexically_normal().generic_string();
        // §66: a file is read first (in a thread): one layer or several? A
        // connection, or "file::layer", is one layer already.
        if (path.rfind("PG:", 0) != 0 && path.find("::") == std::string::npos)
            m_toRead.push_back(path);
        else
            queueLayer(path);
    }
}

// ============================================================================
// THE MAP'S COORDINATE SYSTEM (notions §69)
// ============================================================================

// §70: the dialog, with what only the main thread knows: the map's centre
// in degrees (to find the systems made for that place) and each layer's way
// to the map's system. Both computed again only when they can have changed.
void Application::drawCrsDialog()
{
    if (!m_crsDialog.isOpen())
        return;
    const std::string system = m_map.crs().known ? m_map.crs().wkt : std::string();
    if (m_crsRowsRevision != m_doc.revision || m_crsRowsFor != system) {
        m_crsRows         = crsLayerRows();
        m_crsRowsRevision = m_doc.revision;
        m_crsRowsFor      = system;
    }
    std::optional<r2d::Point> centre;
    if (m_map.started() && m_map.crs().known) {
        const r2d::Envelope& shown = m_map.nav().view().shown;
        const r2d::Point c = shown.center();
        // Again after a move of a tenth of the view: not at every frame of a drag.
        const double step = shown.width() / 10.0;
        if (system != m_crsCentreFor || std::abs(c.x - m_crsCentreFrom.x) > step ||
            std::abs(c.y - m_crsCentreFrom.y) > step) {
            m_crsCentre     = r2d::toLonLat(m_map.crs(), c);
            m_crsCentreFrom = c;
            m_crsCentreFor  = system;
        }
        centre = m_crsCentre;
    }
    const std::string chosen = m_crsDialog.draw(m_map.crs(), centre, m_crsRows);
    if (!chosen.empty())
        m_map.setCrs(chosen);
}

std::vector<CrsLayerRow> Application::crsLayerRows() const
{
    auto label = [](const r2d::CrsInfo& c) {
        const std::string code = c.epsg ? "EPSG:" + std::to_string(c.epsg) : c.code;
        return code.empty() ? c.name : code + " (" + c.name + ")";
    };
    std::vector<CrsLayerRow> rows;
    for (auto it = m_doc.layers.rbegin(); it != m_doc.layers.rend(); ++it) {   // the top layer first, as the panel
        const LayerEntry& e = *it;
        CrsLayerRow r;
        r.layer = e.id;
        std::string error;
        const std::string& text = e.crsDeclared.empty() ? e.crs : e.crsDeclared;   // declared wins
        const auto c = text.empty() ? std::nullopt : r2d::crsFromText(text, error);
        std::optional<r2d::Envelope> x;
        if (e.extent)
            x = r2d::Envelope{(*e.extent)[0], (*e.extent)[1], (*e.extent)[2], (*e.extent)[3]};
        if (!c) {
            r.system     = "none declared";
            r.conversion = "assumed to be the map's";
        } else if (r.system = label(*c) + (e.crsDeclared.empty() ? "" : " (declared)");
                   e.crsDeclared.empty() && !r2d::crsBelievable(*c, x)) {
            r.conversion = "says degrees, its coordinates are not: assumed to be the map's";
        } else if (!m_map.crs().known || r2d::sameCrs(*c, m_map.crs())) {
            r.conversion = "the map's: nothing to convert";
        } else if (const auto t = r2d::CrsTransform::create(*c, m_map.crs(), x, error)) {
            const std::string& d = t->description();   // "A -> B by the way (1 m)"
            const auto by = d.find(" by ");
            r.conversion = by == std::string::npos ? d : d.substr(by + 4);
        } else {
            r.conversion = "no way known, not drawn: " + error;
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

// §71: these notes, beside the executable or one folder up (build/), in the browser.
void Application::openNotes()
{
    const std::filesystem::path exe = executableFolder();
    for (const std::filesystem::path& p : {exe / "docs" / "notions.en.html", exe.parent_path() / "docs" / "notions.en.html"}) {
        std::error_code ec;
        if (!exe.empty() && std::filesystem::is_regular_file(p, ec)) {
            if (!openInBrowser(fileUrl(p)))
                notify("the browser could not be started for " + p.generic_string());
            return;
        }
    }
    notify("carto2D's notes not found (docs/notions.en.html beside the program, or one folder up)");
}

// §45: the layers whose legend changed in the dialog and was not saved.
std::set<std::string> Application::unsavedStyles() const
{
    std::set<std::string> ids;
    for (const LayerEntry& layer : m_doc.layers)
        if (m_styleDialog.unsaved(layer))
            ids.insert(layer.id);
    return ids;
}

// §61: Edit, Selection and View, from the menus or the icons.
void Application::toolbarActions(const ToolbarResult& bar)
{
    if (bar.undo)
        m_commander.undo();
    if (bar.redo)
        m_commander.redo();
    if (bar.zoomSelection)
        m_map.zoomToSelection();
    if (bar.clearSelection) {
        std::string error;
        m_commander.run({{"name", "selection.set"}, {"mode", "clear"}, {"items", nlohmann::json::array()}}, error);
    }
    if (bar.selectByAttributes)
        ImGui::SetWindowFocus("Selection");   // its first lines: layer, where (§58)
    if (!bar.showPanel.empty())
        ImGui::SetWindowFocus(bar.showPanel.c_str());
    if (bar.resetLayout)
        resetLayout();   // Workspace: the first layout, at the next frame
    if (bar.zoom > 0.0)   // §9: View > Zoom in / out, around the centre
        m_map.zoomAtCentre(bar.zoom);
    if (!bar.crs.empty())   // §69: View > Map system
        m_map.setCrs(bar.crs);
    if (bar.crsDialog)      // §70: ... > Other...
        m_crsDialog.open();
    if (bar.about)          // §71: Help
        m_aboutOpen = true;
    if (bar.notes)
        openNotes();
    if (bar.render2dNotes && !openInBrowser("https://nfr25.github.io/render2d/render2d/docs/notions.en.html"))
        notify("the browser could not be started");
    if (bar.fullExtent)   // every layer whose extent is known (§76: the basemaps only if alone)
        m_map.zoomToAll();
}

// ============================================================================
// DRAW ONLY WHEN THERE IS A REASON (notions §56)
// ============================================================================

// Awake (60 frames a second, vsync): some input in the last half second, a
// gesture going on, a wheel or a size waiting to settle (§9). Otherwise
// asleep: no frame until an input, a worker's bell, or the half-second look
// at the clock (a notice to clear, a text cursor).
void Application::waitForReason()
{
    const double now = m_window.time();
    const bool gesture = m_map.gestureGoingOn() || m_styleDialog.busy() ||   // §82: the Classify bar moves
                         ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                         !m_toAdd.empty() || !m_toRead.empty();   // §66: work queued, one piece per frame
    if (now < m_awakeUntil || gesture) {
        m_window.pollEvents();
    } else {
        m_window.waitEvents(kNapSeconds);
        ++m_naps;
    }
    if (m_window.takeInput())
        m_awakeUntil = m_window.time() + kAwakeSeconds;

    // Frames per second, over the last second: 60 awake, about 2 asleep.
    ++m_framesCounted;
    if (const double t = m_window.time(); t - m_fpsSince >= 1.0) {
        m_fps           = m_framesCounted / (t - m_fpsSince);
        m_framesCounted = 0;
        m_fpsSince      = t;
    }
}

// ============================================================================
// THE PROJECT (notions §52, §53)
// ============================================================================

// What Save writes: the layers in drawing order (those of the file that did
// not open, back in their place), and the view of the moment.
r2d::Config Application::projectConfig() const
{
    r2d::Config c;
    c.path = m_projectPath;
    c.crs  = m_map.crsChosen();   // §69: written only when chosen (the file's, or the View menu's)
    c.print = m_config.config.print;   // render2d §74: the page, kept as the file had it
    for (const LayerEntry& e : m_doc.layers) {
        r2d::LayerSpec spec(e.source, e.style.empty() ? std::nullopt : std::optional<std::string>(e.style));
        spec.visible = e.visible;
        spec.title   = e.title;
        spec.crs     = e.crsDeclared;   // §69: written back as it was
        spec.attribution = e.attribution;   // §76
        spec.legend      = e.legend;        // render2d §74
        c.layers.push_back(std::move(spec));
    }
    for (const auto& [at, spec] : m_unopened)
        c.layers.insert(c.layers.begin() + static_cast<long>(std::min(at, c.layers.size())), spec);
    if (m_map.started()) {
        // §52: reopened where it was left, to the centimetre.
        const r2d::Envelope e = m_map.nav().view().shown;
        auto cm = [](double v) { return std::round(v * 100.0) / 100.0; };
        c.startBbox = r2d::Envelope{cm(e.xmin), cm(e.ymin), cm(e.xmax), cm(e.ymax)};
    }
    else
        c.startBbox = m_map.startBbox();
    return c;
}

// §52: the layers as text, the view left out (moving is not changing).
std::string Application::projectSignature() const
{
    std::string text = "crs " + m_map.crsChosen() + '\n';   // §69: another system is a change to save
    for (const r2d::LayerSpec& l : projectConfig().layers)
        text += l.source + '\t' + l.style.value_or("") + '\t' + (l.visible ? "1" : "0") + '\t' + l.title + '\n';
    return text;
}

bool Application::projectChanged() const
{
    return projectSignature() != m_savedSignature;
}

void Application::updateTitle()
{
    const std::string name = m_projectPath.empty() ? std::string("(new map)")
                                                   : std::filesystem::path(m_projectPath).filename().string();
    const std::string title = std::string("carto2D ") + kVersion + " - " + name + (projectChanged() ? " *" : "");
    if (title != m_title) {
        m_title = title;
        m_window.setTitle(title);
    }
}

// ============================================================================
// FILE > PRINT (notions §77): render2d composes the page (its §74), in the
// render thread that holds the map; here, the dialog and the outline.
// ============================================================================

double Application::pageMapPerGround()
{
    const r2d::View v = m_map.nav().view();
    const r2d::Point c{(v.shown.xmin + v.shown.xmax) / 2.0, (v.shown.ymin + v.shown.ymax) / 2.0};
    // PROJ is asked again only when the centre has moved by a tenth of the view.
    if (!m_pageKAt || std::hypot(c.x - m_pageKAt->x, c.y - m_pageKAt->y) > v.shown.width() / 10.0) {
        m_pageK   = r2d::mapPerGroundAt(m_map.crs(), c);
        m_pageKAt = c;
    }
    return m_pageK;
}

double Application::pageFitScale(const r2d::PageSpec& page)
{
    const r2d::View v = m_map.nav().view();
    const double k = pageMapPerGround();
    const r2d::FrameMm f = r2d::pageFrame(page);
    return r2d::niceScale(std::max(v.shown.width() / k / (f.width / 1000.0), v.shown.height() / k / (f.height / 1000.0)));
}

void Application::drawPrint()
{
    // The answer of the render thread, once.
    if (auto done = m_map.worker().takePrintResult()) {
        m_printing = false;
        if (done->ok) {
            std::string said = "written: " + done->path;
            if (!done->lines.empty())
                said += "\n" + done->lines.front();
            if (!done->warnings.empty())
                said += "\n" + std::to_string(done->warnings.size()) + " warning(s), first: " + done->warnings.front();
            m_printDialog.setStatus(said, false);
            notify("page written: " + done->path);
            if (m_printDialog.openAfter() && !openInBrowser(fileUrl(std::filesystem::path(done->path))))
                notify("the PDF reader could not be started");
        } else {
            m_printDialog.setStatus("not written: " + done->error, true);
        }
    }
    if (!m_printDialog.isOpen())
        return;
    if (m_printDialog.draw(m_window.handle(), pageFitScale(m_printDialog.page()), m_printing))
        writePage();

    // The frame on the ground, at the view's centre: where the page will be.
    const r2d::PageSpec& page = m_printDialog.page();
    const r2d::View v = m_map.nav().view();
    const r2d::Point c{(v.shown.xmin + v.shown.xmax) / 2.0, (v.shown.ymin + v.shown.ymax) / 2.0};
    const double k = pageMapPerGround(), scale = page.scale ? *page.scale : 1000.0;
    const r2d::FrameMm f = r2d::pageFrame(page);
    const double hw = f.width / 1000.0 * scale * k / 2.0, hh = f.height / 1000.0 * scale * k / 2.0;
    drawPageOutline({c.x - hw, c.y - hh, c.x + hw, c.y + hh},
                    page.format + (page.landscape ? " landscape" : " portrait") + " \u00B7 " + r2d::scaleText(scale),
                    m_map.nav(), m_area, m_window.contentScale());
}

void Application::writePage()
{
    r2d::PageSpec page = m_printDialog.page();
    const r2d::View v  = m_map.nav().view();
    page.center    = r2d::Point{(v.shown.xmin + v.shown.xmax) / 2.0, (v.shown.ymin + v.shown.ymax) / 2.0};
    page.generator = std::string("carto2D ") + kVersion + " \u00B7 render2d " + r2d::kVersion;
    PrintRequest r;
    r.page     = page;
    r.path     = m_printDialog.path();
    r.layers   = m_map.layerStates();
    r.revision = m_doc.revision;
    r.crs      = m_map.crs().known ? m_map.crs().wkt : std::string();
    m_map.worker().print(std::move(r));
    m_printing = true;
    m_printDialog.setStatus("writing " + m_printDialog.path() + "...", false);
    page.generator.clear();
    m_config.config.print = page;   // the project keeps its page (print:, render2d §74)
}

void Application::fileAction(FileAction action)
{
    switch (action) {
    case FileAction::None:
        break;
    case FileAction::New:
        request(Next::New);
        break;
    case FileAction::Open:
        request(Next::Open);
        break;
    case FileAction::Save:   // everything: the styles changed, then the project
        if (m_next == Next::None)
            saveAndProceed();
        break;
    case FileAction::SaveAs:
        if (m_next == Next::None)
            saveAs(Next::None);
        break;
    case FileAction::Connect:
        m_connect.open();   // §65
        break;
    case FileAction::Print: {   // §77: from the project's print:, else the view fitted
        r2d::PageSpec page = m_config.config.print ? *m_config.config.print : r2d::PageSpec{};
        std::string path = "map.pdf";
        if (!m_projectPath.empty())
            path = std::filesystem::path(m_projectPath).replace_extension(".pdf").generic_string();
        m_printDialog.open(m_config.config.print, path, pageFitScale(page));
        break;
    }
    case FileAction::Quit:
        m_window.setShouldClose(true);   // the loop asks, as for the window's cross
        break;
    }
}

// New, Open, Quit: first the question if something is not saved (§45, §52).
void Application::request(Next next, const std::string& path)
{
    if (m_next != Next::None)
        return;   // a question is already open
    m_next     = next;
    m_nextPath = path;
    if (projectChanged() || !unsavedStyles().empty()) {
        m_askSave = true;
        return;
    }
    proceed();
}

void Application::proceed()
{
    const Next next = m_next;
    m_next = Next::None;
    switch (next) {
    case Next::None:
        break;
    case Next::Quit:
        m_quit = true;
        m_window.setShouldClose(true);
        break;
    case Next::New:
        clearProject();
        notify("new map: drop files on it");
        break;
    case Next::Open: {
        std::string path = m_nextPath;
        if (path.empty()) {
            if (!fileDialogAvailable()) {   // Linux: the path typed (§53)
                m_askPath     = true;
                m_pathForSave = false;
                m_afterPath   = Next::None;
                m_typedPath   = m_projectPath;
                return;
            }
            const auto chosen = chooseYamlFile(m_window.handle(), false, m_projectPath);
            if (!chosen)
                return;   // cancelled
            path = *chosen;
        }
        std::string error;
        if (loadProject(path, error))
            notify("opened " + path);
        else
            notify("cannot open: " + error);   // the map stays as it was
        break;
    }
    }
}

// The styles changed (each to its file), then the project, then what was asked.
void Application::saveAndProceed()
{
    StyleDialogResult r;
    const bool stylesOk = m_styleDialog.saveAll(m_doc, m_commander, m_projectPath, r);
    for (const auto& [id, path] : r.savedFiles)   // new style files: the project uses them
        if (const int i = m_doc.findLayer(id); i >= 0) {
            m_doc.layers[static_cast<std::size_t>(i)].style = path;
            ++m_doc.revision;
        }
    if (!stylesOk) {
        notify(r.notice);   // not written: we stay
        m_next = Next::None;
        return;
    }
    if (m_projectPath.empty()) {
        if (m_doc.layers.empty() && m_unopened.empty()) {
            proceed();   // an empty map: nothing to write
            return;
        }
        const Next then = m_next;
        m_next = Next::None;
        saveAs(then);
        return;
    }
    if (!saveProject(m_projectPath)) {
        m_next = Next::None;
        return;
    }
    proceed();
}

void Application::saveAs(Next then)
{
    if (!fileDialogAvailable()) {   // Linux: the path typed (§53)
        m_askPath     = true;
        m_pathForSave = true;
        m_afterPath   = then;
        m_typedPath   = m_projectPath.empty() ? (std::filesystem::current_path() / "map.yaml").generic_string()
                                              : m_projectPath;
        return;
    }
    const auto chosen = chooseYamlFile(m_window.handle(), true, m_projectPath);
    if (!chosen)
        return;   // cancelled: nothing saved, nothing done
    if (saveProject(*chosen)) {
        m_next = then;
        proceed();
    }
}

// §52: .bak once, then render2d's writeConfig (paths relative to the file).
bool Application::saveProject(const std::string& path)
{
    namespace fs = std::filesystem;
    if (m_doc.layers.empty() && m_unopened.empty()) {
        notify("nothing to save: the map has no layer");
        return false;
    }
    std::error_code ec;
    if (fs::exists(path) && !m_projectBackedUp.count(path)) {
        fs::copy_file(path, path + ".bak", fs::copy_options::overwrite_existing, ec);
        if (ec) {
            notify("cannot back up " + path + ": " + ec.message() + " (not saved)");
            return false;
        }
        m_projectBackedUp.insert(path);
    } else if (!fs::exists(path) && !m_projectPath.empty() && fs::exists(m_projectPath)) {
        // Save as: start from the old file, so that viewer2d's keys (render2d,
        // tiles...) come along; writeConfig replaces the layers and the view.
        fs::copy_file(m_projectPath, path, ec);
    }
    std::string error;
    if (!r2d::writeConfig(projectConfig(), path, error)) {
        notify(error);
        return false;
    }
    m_projectPath        = path;
    m_config.config.path = path;
    m_savedSignature     = projectSignature();
    notify("saved " + path);
    return true;
}

// §45, §52: Save / Don't save / Cancel, before Quit, New or Open.
void Application::drawSaveQuestion()
{
    if (m_askSave) {
        ImGui::OpenPopup("Not saved###save");
        m_askSave = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Not saved###save", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const std::set<std::string> ids = unsavedStyles();
    if (projectChanged())
        ImGui::BulletText("the project: %s", m_projectPath.empty() ? "(new map, never saved)" : m_projectPath.c_str());
    for (const std::string& id : ids)
        ImGui::BulletText("the style of %s", id.c_str());
    ImGui::Spacing();
    const char* verb = m_next == Next::Quit ? "quit" : m_next == Next::New ? "start a new map" : "open another";
    const std::string save = std::string("Save and ") + verb;
    const std::string drop = std::string("Don't save, ") + verb;
    if (ImGui::Button(save.c_str())) {
        ImGui::CloseCurrentPopup();
        saveAndProceed();
    }
    ImGui::SameLine();
    if (ImGui::Button(drop.c_str())) {
        ImGui::CloseCurrentPopup();
        proceed();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_next = Next::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Without Windows' dialog (Linux): the path, typed.
void Application::drawPathQuestion()
{
    if (m_askPath) {
        ImGui::OpenPopup("File###path");
        m_askPath = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("File###path", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted(m_pathForSave ? "Save the project as:" : "Open the project:");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 40.0f);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##path", &m_typedPath, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("OK") || enter) {
        ImGui::CloseCurrentPopup();
        if (m_pathForSave) {
            if (saveProject(m_typedPath)) {
                m_next = m_afterPath;
                proceed();
            }
        } else {
            m_next     = Next::Open;
            m_nextPath = m_typedPath;
            proceed();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void Application::drawEmptyHint()
{
    if (!m_doc.layers.empty() || !m_area.valid())
        return;
    const char* text = "drop files here (shapefile, FlatGeobuf, GeoPackage, GeoJSON, GeoTIFF...)";
    const ImVec2 size = ImGui::CalcTextSize(text);
    ImGui::GetBackgroundDrawList()->AddText(
        ImVec2(m_area.x + (m_area.width - size.x) / 2.0f, m_area.y + (m_area.height - size.y) / 2.0f),
        IM_COL32(120, 120, 120, 255), text);
}

// §19: the mouse (centre of the pixel under it, §12, in the WANTED view), the
// scale, and the state of the render thread. Always visible.
std::string Application::statusText() const
{
    if (!m_map.started())
        return m_error.empty() ? std::string("starting...") : "error: " + m_error;

    // The map's pieces (§81) around the program's own, each with its own small buffer.
    std::string status = m_map.statusBefore();
    auto add = [&status](const char* fmt, auto... args) {
        char piece[160];
        std::snprintf(piece, sizeof piece, fmt, args...);
        status += piece;
    };
    if (const std::size_t sel = m_doc.selectionCount())
        add("%zu selected      ", sel);
    // §66: a file being read, layers being added: said first.
    if (!m_reading.empty())
        add("reading %s...      ", std::filesystem::path(m_reading).filename().string().c_str());
    if (!m_toAdd.empty()) {
        std::string name = m_toAdd.front();
        if (const auto sep = name.rfind("::"); sep != std::string::npos)
            name = name.substr(sep + 2);
        else
            name = std::filesystem::path(name).filename().string();
        add("adding %s (%d of %d)...      ", name.c_str(), m_addDone + 1, m_addTotal);
    }
    status += m_map.statusAfter();
    add("      %.0f fps", m_fps);   // §56: 60 while something moves, ~2 at rest

    if (m_noticeAt >= 0.0 && m_window.time() - m_noticeAt < kNoticeSeconds)
        status += "      " + m_notice;
    // §26: a value copied from the Selection panel, said for 2 seconds.
    if (m_selectionPanel.copiedAt >= 0.0 && ImGui::GetTime() - m_selectionPanel.copiedAt < 2.0)
        status += "      copied " + m_selectionPanel.copiedField;
    return status;
}

} // namespace e2d
