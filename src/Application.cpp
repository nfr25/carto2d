#include "Application.h"

#include "Version.h"
#include "panels/HistoryPanel.h"
#include "panels/LayersPanel.h"
#include "panels/SelectionPanel.h"
#include "panels/TablePanel.h"
#include "panels/Toolbar.h"
#include "view/SelectionOverlay.h"
#include "view/MeasureText.h"   // §72
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

#include <chrono>
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
constexpr double kSettleSeconds = 0.2;  // §9: the wheel or the size quiet for 0.2 s
constexpr double kWheelStep     = 1.25; // zoom factor per wheel notch
constexpr std::size_t kMaxSelection = 10000;   // §30: beyond, a zone is refused whole
constexpr double kNoticeSeconds = 4.0;  // how long a notice stays in the status bar
constexpr double kAwakeSeconds  = 0.5;  // §56: frames go on this long after the last input
constexpr double kNapSeconds    = 0.5;  // §56: asleep, a look at the clock every half second

using Clock = std::chrono::steady_clock;

const ImVec4 kYellow(1.0f, 0.85f, 0.35f, 1.0f);
const ImVec4 kRed(1.0f, 0.45f, 0.40f, 1.0f);
const ImVec4 kGrey(0.65f, 0.65f, 0.65f, 1.0f);

} // namespace

bool Application::init(const std::string& configPath)
{
    const std::string title = std::string("carto2D ") + kVersion;
    if (!m_window.init(kInitialWidth, kInitialHeight, title.c_str()))
        return false;

    const float scale = m_window.contentScale();
    if (!m_imgui.init(m_window.handle(), scale))
        return false;

    // §37: the threads start with empty maps; the layers come with every
    // request, as a list numbered by the Document's revision.
    m_commander.setLayerProbe(probeSource);   // §38: layer.add opens a source once, here
    // §41: layer.setStyle is checked by render2d's reader; what does not read is refused.
    m_commander.setStyleCheck([](const std::string& ysld, std::string& error) {
        const r2d::YsldResult r = r2d::parseYsld(ysld);
        if (!r.style)
            error = r.error;
        return r.style.has_value();
    });
    subscribe();
    // §56: a worker that posts a result rings the bell of the main thread,
    // which may be asleep.
    m_worker.start(scale, &Window::ring);
    m_query.start(scale, &Window::ring);

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
        m_startBbox = *m_config.config.startBbox;
    m_crsChosen = m_config.config.crs;   // §69: "" = the first layer's

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
    decideCrs();        // §69: before the extents, which are converted to it
    if (!m_startBbox)   // no start: bbox in the file: all the layers (§76: the basemaps only if alone)
        m_startBbox = dataExtent();
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
    m_known.clear();
    m_styleDialog.reset();
    m_unopened.clear();
    m_startWarnings.clear();
    m_projectBackedUp.clear();
    m_projectPath.clear();
    m_savedSignature.clear();
    m_config = {};
    m_error.clear();
    m_startBbox.reset();
    m_mapCrs    = {};    // §69: the next first layer decides
    m_crsChosen.clear();
    m_started = false;   // the next layers fit the view again (§39)
    m_startW = m_startH = 0;
    m_outOfScale.clear();
    m_reportLines.clear();
    m_warnings.clear();
    if (m_nav.width() > 0)
        requestRender(false);   // an empty image in place of the old map
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
        barState.tool         = m_tool.kind();
        barState.canUndo      = m_commander.history().canUndo();
        barState.canRedo      = m_commander.history().canRedo();
        barState.hasSelection = !m_doc.selection.empty();
        barState.hasLayers    = !m_doc.layers.empty();
    barState.crsEpsg      = m_mapCrs.epsg;   // §69
        const ToolbarResult bar = drawToolbar(barState);
        if (bar.tool)
            setTool(*bar.tool);
        fileAction(bar.file);   // §53
        toolbarActions(bar);    // §61
        m_area = dockspace();          // the panels around, the map in the middle
        handleDrops();
        handleInput();
        handleShortcuts();
        collectResult();
        collectAnswer();
        drawMapImage();
        drawSelection(m_doc, m_known, m_nav, m_area, m_window.contentScale(),   // the second drawing (§23)
                      {m_selectionPanel.layer, m_selectionPanel.fid});          // §60: the designated one
        {   // the shape (§28, §29); §72: a measure with its figures next to the mouse
            bool closed = false;
            const std::vector<r2d::Point> live = liveMeasure(closed);
            drawShape(m_tool, m_waitingZone, m_nav, m_area, m_window.contentScale(),
                      live.size() >= 2 ? measureText(live, closed, false) : std::string());
            if (m_measured)
                drawMeasured(m_measured->points, m_measured->closed,
                             measureText(m_measured->points, m_measured->closed, false), m_nav, m_area,
                             m_window.contentScale());
        }
        drawEmptyHint();
        drawAboutWindow();   // §71
        drawPrint();         // §77
        drawReportPanel();
        {
            const LayersPanelResult r =
                drawLayersPanel(m_doc, m_outOfScale, unsavedStyles(), m_layerSymbols,
                                m_nav.view().scaleDenominator() * m_window.contentScale(),   // as the status bar
                                m_window.contentScale(), m_commander, &m_symbolTextures);
            if (!r.error.empty())
                m_commandError = r.error;
            if (!r.zoomTo.empty())
                zoomToLayer(r.zoomTo);
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
        if (drawSelectionPanel(m_doc, m_known, m_commander, m_selectionPanel))
            zoomToSelection();
        if (m_selectionPanel.whereAsked)
            askWhere();   // §58
        drawEventsPanel(m_events);
        // §60: the table of the selection; a double click zooms on a row.
        if (const auto row = drawTablePanel(m_doc, m_known, m_selectionPanel, m_table))
            if (const auto it = m_known.find(*row); it != m_known.end() && !it->second.geometry.empty() && m_started)
                fitWithMargin(it->second.geometry.envelope());
        // §17: commands queued by subscribers, then ONE render for all the
        // changes of this frame.
        for (const auto& error : m_commander.processQueue())
            m_commandError = error;
        if (m_renderPending) {
            m_renderPending = false;
            requestRender(false);
        }
        m_imgui.endFrame();

        m_window.swapBuffers();
    }
    m_worker.stop();
    m_query.stop();
}

// ============================================================================
// THE GESTURES (notions §9, §10)
//
// Every gesture changes the WANTED view at once; the image we have is moved
// and stretched to match (drawMapImage). A render is asked for only at the
// END of a gesture: button released, wheel quiet for 0.2 s, window size
// quiet for 0.2 s. One gesture, one render.
// ============================================================================
void Application::handleInput()
{
    if (!m_area.valid())
        return;   // minimised, or the dockspace not laid out yet
    // The map's size in screen pixels: the central zone, not the window (§19).
    // ImGui units -> pixels: x 1 on Windows (§7).
    const ImVec2 s = ImGui::GetIO().DisplayFramebufferScale;
    const int mapW = static_cast<int>(m_area.width * s.x);
    const int mapH = static_cast<int>(m_area.height * s.y);
    const double now = m_window.time();

    // The first frames: the dockspace lays itself out over two frames; start
    // once the zone has the same size twice in a row, not to render a size
    // that is gone at the next frame.
    if (!m_started && (mapW != m_startW || mapH != m_startH)) {
        m_startW = mapW;
        m_startH = mapH;
        return;
    }

    // Then: the start bbox, widened to the map's shape (§14). An empty map
    // waits for its first layer, which gives the start bbox (§39).
    if (!m_started && !m_startBbox)
        return;
    if (!m_started) {
        m_nav.fit(*m_startBbox, mapW, mapH);
        m_started = true;
        requestRender(true);
    }

    // The zone changed size (window, or a panel moved): the corner and the
    // scale stay (Navigator::resize); render when it is quiet (§14).
    if (mapW != m_nav.width() || mapH != m_nav.height()) {
        m_nav.resize(mapW, mapH);
        m_sizePending = true;
        m_sizeSince   = now;
    }

    // The mouse, in screen pixels, counted from the map's corner (§19).
    const MouseState mouse = m_window.consumeMouse();
    const double mx = (mouse.x - m_area.x) * s.x, my = (mouse.y - m_area.y) * s.y;
    m_mouseX = mx;
    m_mouseY = my;
    m_mouseInMap = mx >= 0.0 && my >= 0.0 && mx < mapW && my < mapH;
    // Over the map = inside the zone AND not over a floating panel.
    const bool onMap = m_mouseInMap && !m_imgui.wantsMouse();

    // §9: the keys move the map when the map has the focus: the last click
    // went to the map, not to a panel. A click on the map also takes the
    // keyboard away from the panels (ImGui would move between widgets with
    // the arrows otherwise).
    if (mouse.leftPressed || mouse.middlePressed) {
        m_mapFocused = onMap;
        if (onMap)
            ImGui::SetWindowFocus(nullptr);
    }

    // The middle button pans, whatever the tool (§27): the other hand moves the sheet.
    if (mouse.middlePressed && onMap && !m_gesture.pressed()) {
        m_middleDown  = true;
        m_middleMoved = false;
        m_middleX = mx;
        m_middleY = my;
    }
    if (m_middleDown) {
        if (mx != m_middleX || my != m_middleY) {
            m_nav.pan(mx - m_middleX, my - m_middleY);
            m_middleX = mx;
            m_middleY = my;
            m_middleMoved = true;
        }
        if (!mouse.middle) {
            m_middleDown = false;
            if (m_middleMoved)
                requestRender(true);   // the end of a drag
        }
    }

    if (m_tool.kind() == ToolKind::Pointer) {
        // The left button: starts on the map, not on a panel. A click or a drag
        // is decided by the gesture (§20): within 4 pixels the map stays.
        if (mouse.leftPressed && onMap && !m_middleDown)
            m_gesture.press(mx, my);
        if (m_gesture.pressed()) {
            const DragGesture::Pan pan = m_gesture.move(mx, my);
            if (pan.dx != 0.0 || pan.dy != 0.0)
                m_nav.pan(pan.dx, pan.dy);
            if (!mouse.left) {
                if (m_gesture.release())
                    askQuery(mx, my);       // a click: what is here?
                else
                    requestRender(true);    // the end of a drag
            }
        }
    } else {
        // Another tool (§27): the left button draws a shape, in terrain
        // coordinates (after the middle button's pan, so it follows it).
        const r2d::Point world = m_nav.toWorld(mx, my);
        const double res = m_nav.state().res;
        if (mouse.leftPressed && onMap) {
            m_toolPressed = true;
            m_tool.press(world);
            // §29: a double-click comes at the 2nd press; it finishes a polygon.
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                finishShape(m_tool.doubleClick(), mx, my);
        }
        m_tool.move(world);
        if (m_toolPressed && !mouse.left) {
            m_toolPressed = false;
            finishShape(m_tool.release(world, res), mx, my);
        }
    }

    // Wheel: zoom around the mouse (§10, example 2), render when it is quiet.
    if (mouse.scroll != 0.0f && onMap && !m_gesture.dragging()) {
        m_nav.zoomAt(mx, my, std::pow(kWheelStep, mouse.scroll));
        m_wheelPending = true;
        m_lastWheel    = now;
    }
    // §9: the keyboard. + and - zoom by 2 around the centre of the map; the
    // arrows move it by a third of its width or height, towards the arrow.
    // Like the wheel: the image is stretched at once, the render comes when
    // the keys have been quiet for 0.2 s (one render for five quick presses).
    const ImGuiIO& io = ImGui::GetIO();
    if (m_mapFocused && !io.WantTextInput && !m_gesture.dragging() && mapW > 0 && mapH > 0) {
        bool moved = false;
        for (const ImWchar c : io.InputQueueCharacters) {   // the typed character: any keyboard layout, keypad too
            if (c == '+' || c == '=') {
                m_nav.zoomAt(mapW / 2.0, mapH / 2.0, 2.0);
                moved = true;
            } else if (c == '-') {
                m_nav.zoomAt(mapW / 2.0, mapH / 2.0, 0.5);
                moved = true;
            }
        }
        const double thirdX = mapW / 3.0, thirdY = mapH / 3.0;
        // Right arrow: look further east, so the map slides to the left.
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { m_nav.pan(-thirdX, 0.0); moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  { m_nav.pan(thirdX, 0.0);  moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    { m_nav.pan(0.0, thirdY);  moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  { m_nav.pan(0.0, -thirdY); moved = true; }
        if (moved) {
            m_wheelPending = true;
            m_lastWheel    = now;
        }
    }
    if (m_wheelPending && now - m_lastWheel >= kSettleSeconds) {
        m_wheelPending = false;
        requestRender(true);
    }
    if (m_sizePending && now - m_sizeSince >= kSettleSeconds && !m_gesture.dragging()) {
        m_sizePending = false;
        requestRender(true);
    }
}

// `viewChanged`: the end of a gesture (or a resize, or the start). Then the
// view.changed event goes out: once per gesture, not once per frame (§17).
void Application::requestRender(bool viewChanged)
{
    RenderRequest r;
    r.id         = ++m_lastRequest;
    r.view       = m_nav.view();
    r.pixelRatio = m_window.contentScale();
    r.layers     = layerStates();   // a copy of the state (§11, §37)
    r.revision   = m_doc.revision;
    r.crs        = m_mapCrs.known ? m_mapCrs.wkt : std::string();   // §69
    if (viewChanged) {
        const r2d::Envelope& e = r.view.shown;
        auto cm = [](double v) { return std::round(v * 100.0) / 100.0; };   // metres, to the centimetre
        m_bus.publish({{"event", "view.changed"},
                       {"bbox", {cm(e.xmin), cm(e.ymin), cm(e.xmax), cm(e.ymax)}},
                       {"scale", std::lround(r.view.resolution * r.pixelRatio / r2d::kStandardPixelSizeM)}});
    }
    m_worker.request(std::move(r));
}

// §21: the click goes to the thread for questions, with copies of what it
// needs: the wanted view, the point, the visible layers, the gesture.
// Shift adds, Ctrl toggles, a plain click replaces (§20).
void Application::askQuery(double x, double y)
{
    const ImGuiIO& io = ImGui::GetIO();
    QueryRequest q;
    q.id          = ++m_lastQuery;
    q.view        = m_nav.view();
    q.pixel       = {x, y};
    q.tolerancePx = DragGesture::kClickTolerance;   // §52: 4 pixels
    q.pixelRatio  = m_window.contentScale();
    q.layers   = layerStates();
    q.revision = m_doc.revision;
    q.crs      = m_mapCrs.known ? m_mapCrs.wkt : std::string();   // §69: the same system as the render
    q.mode = io.KeyCtrl ? "toggle" : io.KeyShift ? "add" : "replace";
    m_query.ask(std::move(q));
}

// §30: a finished zone goes to the thread for questions, with the cap. It
// stays drawn until the answer comes.
void Application::askZone(std::vector<r2d::Point> ring)
{
    const ImGuiIO& io = ImGui::GetIO();
    QueryRequest q;
    q.id         = ++m_lastQuery;
    q.view       = m_nav.view();
    q.pixelRatio = m_window.contentScale();
    q.maxHits    = kMaxSelection;
    q.layers   = layerStates();
    q.revision = m_doc.revision;
    q.crs      = m_mapCrs.known ? m_mapCrs.wkt : std::string();   // §69
    q.mode        = io.KeyCtrl ? "toggle" : io.KeyShift ? "add" : "replace";   // read at the end (§28)
    m_waitingZone = ring;
    q.ring        = std::move(ring);
    m_query.ask(std::move(q));
}

// §58: an expression from the Selection panel goes to the thread for
// questions: the whole layer, with the cap.
void Application::askWhere()
{
    SelectionPanelState& s = m_selectionPanel;
    s.whereAsked = false;
    QueryRequest q;
    q.id         = ++m_lastQuery;
    q.view       = m_nav.view();
    q.pixelRatio = m_window.contentScale();
    q.maxHits    = kMaxSelection;
    q.layers     = layerStates();
    q.revision   = m_doc.revision;
    q.crs        = m_mapCrs.known ? m_mapCrs.wkt : std::string();   // §69
    q.mode       = s.whereMode == 1 ? "add" : s.whereMode == 2 ? "remove" : "replace";
    q.whereLayer = s.whereLayer;
    q.where      = s.whereText;
    s.whereBusy  = true;
    s.whereResult.clear();
    m_query.ask(std::move(q));
}

// §27: what a tool produced, and what to do with it. (x, y): the mouse, in
// screen pixels from the map's corner.
void Application::finishShape(const ToolResult& r, double x, double y)
{
    const bool zoom = m_tool.kind() == ToolKind::ZoomWindow;
    if (r.kind == ToolResult::Kind::Click) {
        if (zoom) {   // §28: a click with the zoom window zooms x 2 around it
            m_nav.zoomAt(x, y, 2.0);
            requestRender(true);
        } else {
            askQuery(x, y);   // a click with the rectangle selects, as the pointer
        }
    } else if (r.kind == ToolResult::Kind::Measure) {   // §72: kept on the map
        m_measured = Measured{r.ring, r.closed};
    } else if (r.kind == ToolResult::Kind::Zone) {
        if (zoom) {
            r2d::Envelope e{r.ring[0].x, r.ring[0].y, r.ring[0].x, r.ring[0].y};
            for (const r2d::Point& p : r.ring) {
                e.xmin = std::min(e.xmin, p.x);
                e.ymin = std::min(e.ymin, p.y);
                e.xmax = std::max(e.xmax, p.x);
                e.ymax = std::max(e.ymax, p.y);
            }
            m_nav.fitRect(e);
            requestRender(true);   // navigation: no history, a view.changed (§28)
        } else {
            askZone(r.ring);
        }
    }
}

void Application::setTool(ToolKind kind)
{
    m_measured.reset();   // §72: a finished measure goes with its tool
    m_tool.setKind(kind);
    m_toolPressed = false;
    m_gesture = {};
}

void Application::notify(std::string text)
{
    m_notice   = std::move(text);
    m_noticeAt = m_window.time();
}

// §22: the answer becomes a command, the same as a script would send. The
// first hit is the one on top; its geometry and attributes are kept, to draw
// the selection (§23) and show it (§24).
void Application::collectAnswer()
{
    auto answer = m_query.takeAnswer();
    if (!answer || answer->id != m_lastQuery)
        return;   // an older question: the newest wins
    m_queryMs = answer->ms;

    std::string error;
    if (answer->where) {   // §58: the panel says what came of it
        SelectionPanelState& s = m_selectionPanel;
        s.whereBusy = false;
        char text[160] = "";
        if (answer->truncated)
            std::snprintf(text, sizeof text, "%zu features: more than %zu, narrow the expression",
                          answer->matched, kMaxSelection);
        else if (answer->error.empty())
            std::snprintf(text, sizeof text, "%zu feature%s in %.0f ms", answer->matched,
                          answer->matched == 1 ? "" : "s", answer->ms);
        s.whereResult = answer->error.empty() ? std::string(text) : answer->error;
        if (!answer->error.empty() || answer->truncated)
            return;
    }
    if (answer->zone) {
        // §30: all the features of the zone in ONE command (one undo), or
        // nothing at all beyond the cap.
        m_waitingZone.clear();
        if (!answer->error.empty()) {
            notify(answer->error);
            return;
        }
        if (answer->truncated) {
            notify("more than " + std::to_string(kMaxSelection) + " features: zoom in or draw a smaller zone");
            return;
        }
        if (answer->hits.empty()) {
            if (answer->mode == "replace")
                m_commander.run({{"name", "selection.set"}, {"mode", "clear"}, {"items", nlohmann::json::array()}},
                                error);
            return;
        }
        nlohmann::json items = nlohmann::json::array();
        for (r2d::QueryHit& hit : answer->hits) {
            // §37: hit.layer is an index in the THREAD's map; its id says which.
            const std::string& layer = answer->layerIds.at(static_cast<std::size_t>(hit.layer));
            if (m_doc.findLayer(layer) < 0)
                continue;   // removed since the question was asked
            items.push_back({{"layer", layer}, {"fid", hit.fid}});
            m_known[{layer, hit.fid}] = {std::move(hit.geometry), std::move(hit.attributes)};
        }
        m_commander.run({{"name", "selection.set"}, {"mode", answer->mode}, {"items", std::move(items)}}, error);
        if (!error.empty())
            m_commandError = error;
        return;
    }
    if (answer->hits.empty()) {
        if (answer->mode == "replace")   // a click in the void clears the selection
            m_commander.run({{"name", "selection.set"}, {"mode", "clear"}, {"items", nlohmann::json::array()}}, error);
        return;
    }
    r2d::QueryHit& hit = answer->hits.front();
    const std::string layer = answer->layerIds.at(static_cast<std::size_t>(hit.layer));
    if (m_doc.findLayer(layer) < 0)
        return;   // removed since the question was asked
    m_known[{layer, hit.fid}] = {std::move(hit.geometry), std::move(hit.attributes)};
    m_commander.run({{"name", "selection.set"},
                     {"mode", answer->mode},
                     {"items", {{{"layer", layer}, {"fid", hit.fid}}}}},
                    error);
    if (!error.empty())
        m_commandError = error;
}

// Ctrl+Z, Ctrl+Y (and Ctrl+Shift+Z), Esc, unless a text field has the keyboard.
void Application::handleShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;
    // §27: Esc by priority: the shape in progress, then the tool, then the selection.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_toolPressed = false;
        if (m_tool.escape()) {
            // the shape is gone, the tool stays
        } else if (m_measured) {
            m_measured.reset();   // §72: then the finished measure
        } else if (m_tool.kind() != ToolKind::Pointer) {
            setTool(ToolKind::Pointer);
        } else if (!m_doc.selection.empty()) {
            std::string error;
            m_commander.run({{"name", "selection.set"}, {"mode", "clear"}, {"items", nlohmann::json::array()}},
                            error);
        }
    }
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
    // R, P, W: the tools (no modifier: Ctrl+... stays free).
    if (!io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            setTool(ToolKind::Rectangle);
        else if (ImGui::IsKeyPressed(ImGuiKey_P, false))
            setTool(ToolKind::Polygon);
        else if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            setTool(ToolKind::ZoomWindow);
        else if (ImGui::IsKeyPressed(ImGuiKey_D, false))
            setTool(ToolKind::MeasureLine);   // §72
        else if (ImGui::IsKeyPressed(ImGuiKey_A, false))
            setTool(ToolKind::MeasureArea);
    }
    // §29: the polygon's keys.
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        finishShape(m_tool.enter(), m_mouseX, m_mouseY);
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
        m_tool.backspace();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        m_commander.undo();
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
             ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        m_commander.redo();
}

// §17: the application is a subscriber like the others. A visibility change
// asks for a render (once per frame, however many changes); every event is
// kept for the Events panel.
void Application::subscribe()
{
    m_bus.subscribe("layer.visibility", [this](const Event&) { m_renderPending = true; });
    // §37: a layer added, removed or moved: the next request carries the new list.
    m_bus.subscribe("layer.removed", [this](const Event&) { m_renderPending = true; });
    m_bus.subscribe("layer.moved", [this](const Event&) { m_renderPending = true; });
    m_bus.subscribe("layer.style", [this](const Event&) { m_renderPending = true; });   // §42: the map is the preview
    m_bus.subscribe("layer.added", [this](const Event& e) {
        m_renderPending = true;
        decideCrs();   // §69: the first layer with a system gives the map its own
        // §39: the first layer of an empty map gives the first view.
        if (m_startBbox)
            return;
        const int i = m_doc.findLayer(e["layer"].get<std::string>());
        if (i >= 0)
            m_startBbox = mapExtent(m_doc.layers[static_cast<std::size_t>(i)]);
    });
    m_bus.subscribe("*", [this](const Event& e) {
        m_events.push_back(e);
        if (m_events.size() > 50)
            m_events.pop_front();
    });
}

// A finished image: to the texture, with the view it was drawn for. An image
// older than the one shown is ignored (numbered requests, §11).
void Application::collectResult()
{
    auto result = m_worker.takeResult();
    if (!result || result->id <= m_shownId)
        return;

    // §39: the upload in two times. The call itself, then glFinish: the time
    // the graphics card still needs. A long first time: the driver converts
    // the bytes; a long second one: the card is busy (§25).
    const auto t0 = Clock::now();
    m_texture.upload(result->pixels.data(), result->width, result->height, result->stride);
    const auto t1 = Clock::now();
    glFinish();
    m_uploadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    m_finishMs = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();

    m_shownId     = result->id;
    m_imageState  = stateOf(result->view);
    m_reportLines = std::move(result->reportLines);
    m_warnings    = std::move(result->warnings);
    m_outOfScale  = std::move(result->outOfScale);
    m_renderMs    = result->renderMs;
    // The render thread publishes nothing itself: the main thread does, on receipt (§17).
    m_bus.publish({{"event", "render.done"}, {"id", m_shownId}, {"ms", std::round(m_renderMs * 10.0) / 10.0}});
}

// The image we have, where it falls in the wanted view (§10): moved and
// stretched during a gesture, exactly on the pixels when they agree (§5).
void Application::drawMapImage()
{
    if (!m_texture.valid())
        return;
    Placement p = m_nav.placement(m_imageState);
    if (std::fabs(p.k - 1.0) < 1e-9) {   // not stretched: on whole pixels (§5)
        p.x0 = std::round(p.x0);
        p.y0 = std::round(p.y0);
    }
    // Screen pixels -> ImGui units (the same numbers on Windows), from the
    // corner of the central zone (§19).
    const ImVec2 s = ImGui::GetIO().DisplayFramebufferScale;
    const ImVec2 corner(m_area.x, m_area.y);
    const ImVec2 a(corner.x + static_cast<float>(p.x0) / s.x, corner.y + static_cast<float>(p.y0) / s.y);
    const ImVec2 b(corner.x + static_cast<float>(p.x0 + m_texture.width() * p.k) / s.x,
                   corner.y + static_cast<float>(p.y0 + m_texture.height() * p.k) / s.y);
    // Clipped to the zone: a stretched image must not spill under the panels.
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(corner, ImVec2(corner.x + m_area.width, corner.y + m_area.height));
    // uv (0,0) -> (1,1): Cairo's first row is the top row, no flip (§5).
    draw->AddImage(static_cast<ImTextureID>(m_texture.id()), a, b);
    draw->PopClipRect();
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

    if (!m_reportLines.empty()) {
        ImGui::Separator();
        for (const auto& line : m_reportLines)
            ImGui::TextUnformatted(line.c_str());
    }
    for (const auto& w : m_startWarnings)   // layers of the file that could not be opened
        ImGui::TextColored(kYellow, "warning: %s", w.c_str());
    for (const auto& w : m_warnings)
        ImGui::TextColored(kYellow, "warning: %s", w.c_str());

    ImGui::End();
}

// ============================================================================
// STEP 3a: LAYERS THAT COME AND GO (notions §37 to §39)
// ============================================================================

// §37: the copy of the Document's list that every request carries.
std::vector<LayerState> Application::layerStates() const
{
    std::vector<LayerState> out;
    for (const LayerEntry& l : m_doc.layers)
        out.push_back({l.id, l.source, l.style, l.visible, l.colour, l.styleText, l.crsDeclared, l.attribution,
                       l.title, l.legend});
    return out;
}

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

// §28: the rectangle with 5 % of margin on each side; a single point (no
// surface) is centred at 1:1000.
void Application::fitWithMargin(r2d::Envelope e)
{
    const double w = e.xmax - e.xmin, h = e.ymax - e.ymin;
    if (w <= 0.0 || h <= 0.0) {
        // 1:1000 = res x pixel ratio / 0.28 mm  ->  res = 1000 x 0.00028 / ratio
        const double res = 1000.0 * r2d::kStandardPixelSizeM / m_window.contentScale();
        const double cx = (e.xmin + e.xmax) / 2.0, cy = (e.ymin + e.ymax) / 2.0;
        const double hw = m_nav.width() * res / 2.0, hh = m_nav.height() * res / 2.0;
        e = {cx - hw, cy - hh, cx + hw, cy + hh};
    } else {
        e = {e.xmin - 0.05 * w, e.ymin - 0.05 * h, e.xmax + 0.05 * w, e.ymax + 0.05 * h};
    }
    m_nav.fitRect(e);
    requestRender(true);   // navigation: no history, a view.changed
}

// ============================================================================
// THE MAP'S COORDINATE SYSTEM (notions §69)
// ============================================================================

// Decided once, in the main thread, and sent to both threads with every
// request: they cannot each choose another. The project's crs:, else the
// first layer that declares a believable one (in degrees: EPSG:3857), as
// render2d's Map does on its own.
void Application::decideCrs()
{
    if (m_mapCrs.known)
        return;
    std::string error;
    if (!m_crsChosen.empty()) {
        if (const auto c = r2d::crsFromUser(m_crsChosen, error))
            m_mapCrs = *c;
        else
            notify("crs: " + error);
        return;
    }
    for (const LayerEntry& e : m_doc.layers) {
        const std::string& text = e.crsDeclared.empty() ? e.crs : e.crsDeclared;   // declared wins
        if (text.empty())
            continue;
        const auto c = r2d::crsFromText(text, error);
        if (!c)
            continue;
        std::optional<r2d::Envelope> x;
        if (e.extent)
            x = r2d::Envelope{(*e.extent)[0], (*e.extent)[1], (*e.extent)[2], (*e.extent)[3]};
        if (e.crsDeclared.empty() && !r2d::crsBelievable(*c, x))
            continue;   // a GeoJSON in metres that says degrees (§69)
        m_mapCrs = r2d::crsForMap(*c);
        if (c->geographic)
            notify(e.id + " is in degrees: the map uses EPSG:3857 (View > Map system to change)");
        return;
    }
}

std::optional<r2d::Envelope> Application::mapExtent(const LayerEntry& layer) const
{
    if (!layer.extent)
        return std::nullopt;
    const r2d::Envelope x{(*layer.extent)[0], (*layer.extent)[1], (*layer.extent)[2], (*layer.extent)[3]};
    const std::string& text = layer.crsDeclared.empty() ? layer.crs : layer.crsDeclared;   // declared wins
    std::optional<r2d::Envelope> inMap = x;   // no system declared: assumed to be the map's
    if (!text.empty() && m_mapCrs.known) {
        std::string error;
        if (const auto c = r2d::crsFromText(text, error))
            inMap = r2d::extentInCrs(*c, x, m_mapCrs);
    }
    // §76: a basemap covers far more than the map's system was made for
    // (OpenStreetMap: the world): only its part in the system's area of use,
    // as render2d warps it (its §73).
    if (!layer.web || !m_mapCrs.known)
        return inMap;
    const auto area = r2d::areaOfUse(m_mapCrs);
    if (!area)
        return inMap;
    if (!inMap)
        return area;
    const r2d::Envelope both{std::max(inMap->xmin, area->xmin), std::max(inMap->ymin, area->ymin),
                             std::min(inMap->xmax, area->xmax), std::min(inMap->ymax, area->ymax)};
    return both.xmax > both.xmin && both.ymax > both.ymin ? both : *area;
}

// §76: the data's extent, "zoom to all": the basemaps count only when there
// is nothing else (a map of OpenStreetMap alone opens on its system's area).
std::optional<r2d::Envelope> Application::dataExtent() const
{
    std::optional<r2d::Envelope> data, web;
    for (const LayerEntry& l : m_doc.layers)
        if (const auto e = mapExtent(l)) {   // §69: in the map's system
            std::optional<r2d::Envelope>& into = l.web ? web : data;
            into = into ? into->merged(*e) : *e;
        }
    return data ? data : web;
}

// The same ground, in another system: the view is converted, the threads
// get the new system with the next request, and the selection goes (its
// geometries were in the old one).
void Application::setMapCrs(const std::string& text)
{
    std::string error;
    const auto c = r2d::crsFromUser(text, error);
    if (!c) {
        notify(error);
        return;
    }
    if (m_mapCrs.known && r2d::sameCrs(*c, m_mapCrs) && !m_crsChosen.empty())
        return;
    const r2d::CrsInfo old = m_mapCrs;
    m_mapCrs    = *c;
    m_crsChosen = text;
    m_measured.reset();   // §72: its points were in the old system
    if (old.known && !r2d::sameCrs(*c, old)) {
        if (m_started) {
            if (const auto e = r2d::extentInCrs(old, m_nav.view().shown, *c))
                m_nav.fitRect(*e);
        } else if (m_startBbox) {
            m_startBbox = r2d::extentInCrs(old, *m_startBbox, *c);
        }
        if (!m_doc.selection.empty()) {
            std::string refused;
            m_commander.run({{"name", "selection.set"}, {"mode", "clear"}}, refused);
        }
    }
    requestRender(true);
}

// §70: the dialog, with what only the main thread knows: the map's centre
// in degrees (to find the systems made for that place) and each layer's way
// to the map's system. Both computed again only when they can have changed.
void Application::drawCrsDialog()
{
    if (!m_crsDialog.isOpen())
        return;
    const std::string system = m_mapCrs.known ? m_mapCrs.wkt : std::string();
    if (m_crsRowsRevision != m_doc.revision || m_crsRowsFor != system) {
        m_crsRows         = crsLayerRows();
        m_crsRowsRevision = m_doc.revision;
        m_crsRowsFor      = system;
    }
    std::optional<r2d::Point> centre;
    if (m_started && m_mapCrs.known) {
        const r2d::Envelope& shown = m_nav.view().shown;
        const r2d::Point c = shown.center();
        // Again after a move of a tenth of the view: not at every frame of a drag.
        const double step = shown.width() / 10.0;
        if (system != m_crsCentreFor || std::abs(c.x - m_crsCentreFrom.x) > step ||
            std::abs(c.y - m_crsCentreFrom.y) > step) {
            m_crsCentre     = r2d::toLonLat(m_mapCrs, c);
            m_crsCentreFrom = c;
            m_crsCentreFor  = system;
        }
        centre = m_crsCentre;
    }
    const std::string chosen = m_crsDialog.draw(m_mapCrs, centre, m_crsRows);
    if (!chosen.empty())
        setMapCrs(chosen);
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
        } else if (!m_mapCrs.known || r2d::sameCrs(*c, m_mapCrs)) {
            r.conversion = "the map's: nothing to convert";
        } else if (const auto t = r2d::CrsTransform::create(*c, m_mapCrs, x, error)) {
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

// ============================================================================
// MEASURING (notions §72)
// ============================================================================

const r2d::GeodesicMeter& Application::meter() const
{
    const std::string system = m_mapCrs.known ? m_mapCrs.wkt : std::string();
    if (!m_meter || m_meterFor != system) {   // made again when the map's system changes
        m_meter    = std::make_unique<r2d::GeodesicMeter>(m_mapCrs);
        m_meterFor = system;
    }
    return *m_meter;
}

std::vector<r2d::Point> Application::liveMeasure(bool& closed) const
{
    std::vector<r2d::Point> pts;
    const bool line = m_tool.kind() == ToolKind::MeasureLine, area = m_tool.kind() == ToolKind::MeasureArea;
    if ((!line && !area) || m_tool.vertices().empty())
        return pts;
    pts = m_tool.vertices();
    if (!m_tool.onFirstVertex(m_nav.state().res))
        pts.push_back(m_tool.hover());   // the mouse is the next point (unless it is closing)
    closed = area && pts.size() >= 3;
    return pts;
}

std::string Application::measureText(const std::vector<r2d::Point>& points, bool closed, bool full) const
{
    const r2d::Measurement m = meter().measure(points, closed);
    const double length = m.onGround ? m.length : m.mapLength;
    const double area   = m.onGround ? m.area : m.mapArea;
    std::string text = closed ? "area " + areaText(area) + "   perimeter " + lengthText(length)
                              : "length " + lengthText(length);
    if (!m.onGround)
        return text + (full ? " (map units: the map has no system)" : "");
    if (full) {
        const double ratio = closed ? m.mapArea / std::max(m.area, 1e-9) : m.mapLength / std::max(m.length, 1e-9);
        if (std::abs(ratio - 1.0) > 0.001) {
            const std::string code = m_mapCrs.epsg ? "EPSG:" + std::to_string(m_mapCrs.epsg) : m_mapCrs.code;
            text = "ground: " + text + "   map: " + (closed ? areaText(m.mapArea) : lengthText(m.mapLength)) + " (" +
                   code + ")";
        }
    }
    return text;
}

void Application::zoomToLayer(const std::string& id)
{
    const int i = m_doc.findLayer(id);
    if (i < 0 || !m_started)
        return;
    if (const auto x = mapExtent(m_doc.layers[static_cast<std::size_t>(i)]))   // §69: in the map's system
        fitWithMargin(*x);
    else
        notify(id + ": its extent is unknown");
}

// The geometries are already here, to draw the selection (§23).
void Application::zoomToSelection()
{
    std::optional<r2d::Envelope> all;
    for (const auto& [layer, fids] : m_doc.selection)
        for (std::int64_t fid : fids) {
            const auto it = m_known.find({layer, fid});
            if (it == m_known.end() || it->second.geometry.empty())
                continue;
            const r2d::Envelope e = it->second.geometry.envelope();
            all = all ? all->merged(e) : e;
        }
    if (all && m_started)
        fitWithMargin(*all);
}

// An empty map is a normal state (§39): it says what to do.
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
        zoomToSelection();
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
    if (bar.zoom > 0.0 && m_started) {   // §9: View > Zoom in / out, around the centre
        m_nav.zoomAt(m_nav.width() / 2.0, m_nav.height() / 2.0, bar.zoom);
        requestRender(true);
    }
    if (!bar.crs.empty())   // §69: View > Map system
        setMapCrs(bar.crs);
    if (bar.crsDialog)      // §70: ... > Other...
        m_crsDialog.open();
    if (bar.about)          // §71: Help
        m_aboutOpen = true;
    if (bar.notes)
        openNotes();
    if (bar.render2dNotes && !openInBrowser("https://nfr25.github.io/render2d/render2d/docs/notions.en.html"))
        notify("the browser could not be started");
    if (bar.fullExtent && m_started) {   // every layer whose extent is known (§76: the basemaps only if alone)
        if (const auto all = dataExtent())
            fitWithMargin(*all);
    }
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
    const bool gesture = m_wheelPending || m_sizePending || m_middleDown || m_toolPressed ||
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
    c.crs  = m_crsChosen;   // §69: written only when chosen (the file's, or the View menu's)
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
    if (m_started) {
        // §52: reopened where it was left, to the centimetre.
        const r2d::Envelope e = m_nav.view().shown;
        auto cm = [](double v) { return std::round(v * 100.0) / 100.0; };
        c.startBbox = r2d::Envelope{cm(e.xmin), cm(e.ymin), cm(e.xmax), cm(e.ymax)};
    }
    else
        c.startBbox = m_startBbox;
    return c;
}

// §52: the layers as text, the view left out (moving is not changing).
std::string Application::projectSignature() const
{
    std::string text = "crs " + m_crsChosen + '\n';   // §69: another system is a change to save
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
    const r2d::View v = m_nav.view();
    const r2d::Point c{(v.shown.xmin + v.shown.xmax) / 2.0, (v.shown.ymin + v.shown.ymax) / 2.0};
    // PROJ is asked again only when the centre has moved by a tenth of the view.
    if (!m_pageKAt || std::hypot(c.x - m_pageKAt->x, c.y - m_pageKAt->y) > v.shown.width() / 10.0) {
        m_pageK   = r2d::mapPerGroundAt(m_mapCrs, c);
        m_pageKAt = c;
    }
    return m_pageK;
}

double Application::pageFitScale(const r2d::PageSpec& page)
{
    const r2d::View v = m_nav.view();
    const double k = pageMapPerGround();
    const r2d::FrameMm f = r2d::pageFrame(page);
    return r2d::niceScale(std::max(v.shown.width() / k / (f.width / 1000.0), v.shown.height() / k / (f.height / 1000.0)));
}

void Application::drawPrint()
{
    // The answer of the render thread, once.
    if (auto done = m_worker.takePrintResult()) {
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
    const r2d::View v = m_nav.view();
    const r2d::Point c{(v.shown.xmin + v.shown.xmax) / 2.0, (v.shown.ymin + v.shown.ymax) / 2.0};
    const double k = pageMapPerGround(), scale = page.scale ? *page.scale : 1000.0;
    const r2d::FrameMm f = r2d::pageFrame(page);
    const double hw = f.width / 1000.0 * scale * k / 2.0, hh = f.height / 1000.0 * scale * k / 2.0;
    drawPageOutline({c.x - hw, c.y - hh, c.x + hw, c.y + hh},
                    page.format + (page.landscape ? " landscape" : " portrait") + " \u00B7 " + r2d::scaleText(scale),
                    m_nav, m_area, m_window.contentScale());
}

void Application::writePage()
{
    r2d::PageSpec page = m_printDialog.page();
    const r2d::View v  = m_nav.view();
    page.center    = r2d::Point{(v.shown.xmin + v.shown.xmax) / 2.0, (v.shown.ymin + v.shown.ymax) / 2.0};
    page.generator = std::string("carto2D ") + kVersion + " \u00B7 render2d " + r2d::kVersion;
    PrintRequest r;
    r.page     = page;
    r.path     = m_printDialog.path();
    r.layers   = layerStates();
    r.revision = m_doc.revision;
    r.crs      = m_mapCrs.known ? m_mapCrs.wkt : std::string();
    m_worker.print(std::move(r));
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
    if (!m_started)
        return m_error.empty() ? std::string("starting...") : "error: " + m_error;

    // Pieces appended one after the other, each with its own small buffer.
    std::string status;
    auto add = [&status](const char* fmt, auto... args) {
        char piece[160];
        std::snprintf(piece, sizeof piece, fmt, args...);
        status += piece;
    };
    // §27: what the left button does, when it is not the pointer.
    switch (m_tool.kind()) {
    case ToolKind::Pointer:
        break;
    case ToolKind::Rectangle:
        status += "[Rectangle: drag; Shift add, Ctrl toggle; Esc]      ";
        break;
    case ToolKind::Polygon:
        status += "[Polygon: click vertices, double click / Enter: finish, Backspace, Esc]      ";
        break;
    case ToolKind::ZoomWindow:
        status += "[Zoom window: drag; click: x2; Esc]      ";
        break;
    case ToolKind::MeasureLine:   // §72
        status += "[Distance: click points, double click / Enter: finish, Backspace, Esc]      ";
        break;
    case ToolKind::MeasureArea:
        status += "[Area: click vertices, the first one again / double click / Enter: finish, Esc]      ";
        break;
    }
    {   // §72: the figures, with the map's own when they differ
        bool closed = false;
        std::vector<r2d::Point> pts = liveMeasure(closed);
        if (pts.size() < 2 && m_measured) {
            pts    = m_measured->points;
            closed = m_measured->closed;
        }
        if (pts.size() >= 2)
            status += measureText(pts, closed, true) + "      ";
    }
    if (m_mouseInMap) {
        const r2d::Point p = m_nav.toWorld(std::floor(m_mouseX) + 0.5, std::floor(m_mouseY) + 0.5);
        add("X %.2f  Y %.2f m      ", p.x, p.y);
    }
    const double scale = m_nav.view().scaleDenominator() * m_window.contentScale();
    add("1:%.0f      ", scale);
    if (m_mapCrs.known)   // §69: the system of X, Y and the scale
        add("%s      ", m_mapCrs.epsg ? ("EPSG:" + std::to_string(m_mapCrs.epsg)).c_str() : m_mapCrs.name.c_str());
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
    if (m_worker.busy())
        add("drawing view #%ld...", m_lastRequest);
    else
        add("view #%ld: render %.1f ms, upload[%d] %.1f + finish %.1f ms", m_shownId, m_renderMs, m_texture.method(), m_uploadMs, m_finishMs);
    if (m_lastQuery > 0)
        add("      query %.1f ms", m_queryMs);
    add("      %.0f fps", m_fps);   // §56: 60 while something moves, ~2 at rest

    if (m_noticeAt >= 0.0 && m_window.time() - m_noticeAt < kNoticeSeconds)
        status += "      " + m_notice;
    // §26: a value copied from the Selection panel, said for 2 seconds.
    if (m_selectionPanel.copiedAt >= 0.0 && ImGui::GetTime() - m_selectionPanel.copiedAt < 2.0)
        status += "      copied " + m_selectionPanel.copiedField;
    return status;
}

} // namespace e2d
