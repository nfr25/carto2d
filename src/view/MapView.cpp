#include "view/MapView.h"

#include "view/MeasureText.h"   // §72
#include "view/SelectionOverlay.h"
#include "view/ShapeOverlay.h"

#include <glad/gl.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace e2d {

namespace {

constexpr double kSettleSeconds = 0.2;  // §9: the wheel or the size quiet for 0.2 s
constexpr double kWheelStep     = 1.25; // zoom factor per wheel notch
constexpr std::size_t kMaxSelection = 10000;   // §30: beyond, a zone is refused whole

using Clock = std::chrono::steady_clock;

} // namespace

MapView::MapView(Document& doc, Commander& commander, EventBus& bus)
    : m_doc(doc), m_commander(commander), m_bus(bus)
{
    // §17: the map is a subscriber like the others. A visibility change asks
    // for a render (once per frame, however many changes).
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
}

void MapView::start(float pixelRatio, std::function<void()> ring)
{
    m_ratio = pixelRatio;
    // §37: the threads start with empty maps; the layers come with every
    // request, as a list numbered by the Document's revision.
    m_worker.start(pixelRatio, ring);
    m_query.start(pixelRatio, ring);
}

void MapView::stop()
{
    m_worker.stop();
    m_query.stop();
}

void MapView::setMessages(std::function<void(const std::string&)> notify,
                          std::function<void(const std::string&)> refused)
{
    m_notify  = std::move(notify);
    m_refused = std::move(refused);
}

// §53: New, and the first half of Open: no system, no view, no image.
void MapView::reset()
{
    m_known.clear();
    m_startBbox.reset();
    m_mapCrs    = {};    // §69: the next first layer decides
    m_crsChosen.clear();
    m_started = false;   // the next layers fit the view again (§39)
    m_startW = m_startH = 0;
    m_outOfScale.clear();
    m_counts.clear();
    m_reportLines.clear();
    m_warnings.clear();
    if (m_nav.width() > 0)
        requestRender(false);   // an empty image in place of the old map
}

std::optional<r2d::Envelope> MapView::currentBbox() const
{
    if (m_started)
        return m_nav.view().shown;
    return m_startBbox;
}

bool MapView::gestureGoingOn() const
{
    return m_wheelPending || m_sizePending || m_middleDown || m_toolPressed;
}

// ============================================================================
// THE GESTURES (notions §9, §10)
//
// Every gesture changes the WANTED view at once; the image we have is moved
// and stretched to match (drawImage). A render is asked for only at the
// END of a gesture: button released, wheel quiet for 0.2 s, window size
// quiet for 0.2 s. One gesture, one render.
// ============================================================================
void MapView::input(const MapArea& area, const MouseState& mouse, bool panelWantsMouse, double now)
{
    m_area = area;
    if (!m_area.valid())
        return;   // minimised, or the dockspace not laid out yet
    // The map's size in screen pixels: the central zone, not the window (§19).
    // ImGui units -> pixels: x 1 on Windows (§7).
    const ImVec2 s = ImGui::GetIO().DisplayFramebufferScale;
    const int mapW = static_cast<int>(m_area.width * s.x);
    const int mapH = static_cast<int>(m_area.height * s.y);

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
    const double mx = (mouse.x - m_area.x) * s.x, my = (mouse.y - m_area.y) * s.y;
    m_mouseX = mx;
    m_mouseY = my;
    m_mouseInMap = mx >= 0.0 && my >= 0.0 && mx < mapW && my < mapH;
    // Over the map = inside the zone AND not over a floating panel.
    const bool onMap = m_mouseInMap && !panelWantsMouse;

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

// The map's keys, unless a text field has the keyboard: Esc by priority, the
// tools' letters, the polygon's Enter and Backspace (§27, §29).
void MapView::keys()
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
}

void MapView::collect()
{
    collectResult();
    collectAnswer();
}

void MapView::draw(const std::pair<std::string, std::int64_t>& focus)
{
    drawImage();
    drawSelection(m_doc, m_known, m_nav, m_area, m_ratio, focus);   // the second drawing (§23)
    // The shape (§28, §29); §72: a measure with its figures next to the mouse.
    bool closed = false;
    const std::vector<r2d::Point> live = liveMeasure(closed);
    drawShape(m_tool, m_waitingZone, m_nav, m_area, m_ratio,
              live.size() >= 2 ? measureText(live, closed, false) : std::string());
    if (m_measured)
        drawMeasured(m_measured->points, m_measured->closed,
                     measureText(m_measured->points, m_measured->closed, false), m_nav, m_area, m_ratio);
}

// §17: commands queued by subscribers came first; then ONE render for all
// the changes of this frame.
void MapView::endFrame()
{
    if (m_renderPending) {
        m_renderPending = false;
        requestRender(false);
    }
}

// §37: the copy of the Document's list that every request carries.
std::vector<LayerState> MapView::layerStates() const
{
    std::vector<LayerState> out;
    for (const LayerEntry& l : m_doc.layers)
        out.push_back({l.id, l.source, l.style, l.visible, l.colour, l.styleText, l.crsDeclared, l.attribution,
                       l.title, l.legend});
    return out;
}

// `viewChanged`: the end of a gesture (or a resize, or the start). Then the
// view.changed event goes out: once per gesture, not once per frame (§17).
void MapView::requestRender(bool viewChanged)
{
    RenderRequest r;
    r.id         = ++m_lastRequest;
    r.view       = m_nav.view();
    r.pixelRatio = m_ratio;
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
void MapView::askQuery(double x, double y)
{
    const ImGuiIO& io = ImGui::GetIO();
    QueryRequest q;
    q.id          = ++m_lastQuery;
    q.view        = m_nav.view();
    q.pixel       = {x, y};
    q.tolerancePx = DragGesture::kClickTolerance;   // §52: 4 pixels
    q.pixelRatio  = m_ratio;
    q.layers   = layerStates();
    q.revision = m_doc.revision;
    q.crs      = m_mapCrs.known ? m_mapCrs.wkt : std::string();   // §69: the same system as the render
    q.mode = io.KeyCtrl ? "toggle" : io.KeyShift ? "add" : "replace";
    m_query.ask(std::move(q));
}

// §30: a finished zone goes to the thread for questions, with the cap. It
// stays drawn until the answer comes.
void MapView::askZone(std::vector<r2d::Point> ring)
{
    const ImGuiIO& io = ImGui::GetIO();
    QueryRequest q;
    q.id         = ++m_lastQuery;
    q.view       = m_nav.view();
    q.pixelRatio = m_ratio;
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
// questions: the whole layer, with the cap. `mode`: 0 replace, 1 add, 2 remove.
void MapView::askWhere(const std::string& layer, const std::string& where, int mode)
{
    QueryRequest q;
    q.id         = ++m_lastQuery;
    q.view       = m_nav.view();
    q.pixelRatio = m_ratio;
    q.maxHits    = kMaxSelection;
    q.layers     = layerStates();
    q.revision   = m_doc.revision;
    q.crs        = m_mapCrs.known ? m_mapCrs.wkt : std::string();   // §69
    q.mode       = mode == 1 ? "add" : mode == 2 ? "remove" : "replace";
    q.whereLayer = layer;
    q.where      = where;
    m_whereResult.reset();
    m_query.ask(std::move(q));
}

std::optional<std::string> MapView::takeWhereResult()
{
    std::optional<std::string> r;
    r.swap(m_whereResult);
    return r;
}

// §27: what a tool produced, and what to do with it. (x, y): the mouse, in
// screen pixels from the map's corner.
void MapView::finishShape(const ToolResult& r, double x, double y)
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

void MapView::setTool(ToolKind kind)
{
    m_measured.reset();   // §72: a finished measure goes with its tool
    m_tool.setKind(kind);
    m_toolPressed = false;
    m_gesture = {};
}

// §22: the answer becomes a command, the same as a script would send. The
// first hit is the one on top; its geometry and attributes are kept, to draw
// the selection (§23) and show it (§24).
void MapView::collectAnswer()
{
    auto answer = m_query.takeAnswer();
    if (!answer || answer->id != m_lastQuery)
        return;   // an older question: the newest wins
    m_queryMs = answer->ms;

    std::string error;
    if (answer->where) {   // §58: the panel says what came of it
        char text[160] = "";
        if (answer->truncated)
            std::snprintf(text, sizeof text, "%zu features: more than %zu, narrow the expression",
                          answer->matched, kMaxSelection);
        else if (answer->error.empty())
            std::snprintf(text, sizeof text, "%zu feature%s in %.0f ms", answer->matched,
                          answer->matched == 1 ? "" : "s", answer->ms);
        m_whereResult = answer->error.empty() ? std::string(text) : answer->error;
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
            refused(error);
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
        refused(error);
}

// A finished image: to the texture, with the view it was drawn for. An image
// older than the one shown is ignored (numbered requests, §11).
void MapView::collectResult()
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
    m_counts      = std::move(result->counts);
    m_renderMs    = result->renderMs;
    // The render thread publishes nothing itself: the main thread does, on receipt (§17).
    m_bus.publish({{"event", "render.done"}, {"id", m_shownId}, {"ms", std::round(m_renderMs * 10.0) / 10.0}});
}

// The image we have, where it falls in the wanted view (§10): moved and
// stretched during a gesture, exactly on the pixels when they agree (§5).
void MapView::drawImage()
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

// ============================================================================
// NAVIGATION (notions §28, §76)
// ============================================================================

// §28: the rectangle with 5 % of margin on each side; a single point (no
// surface) is centred at 1:1000.
void MapView::fitWithMargin(r2d::Envelope e)
{
    const double w = e.xmax - e.xmin, h = e.ymax - e.ymin;
    if (w <= 0.0 || h <= 0.0) {
        // 1:1000 = res x pixel ratio / 0.28 mm  ->  res = 1000 x 0.00028 / ratio
        const double res = 1000.0 * r2d::kStandardPixelSizeM / m_ratio;
        const double cx = (e.xmin + e.xmax) / 2.0, cy = (e.ymin + e.ymax) / 2.0;
        const double hw = m_nav.width() * res / 2.0, hh = m_nav.height() * res / 2.0;
        e = {cx - hw, cy - hh, cx + hw, cy + hh};
    } else {
        e = {e.xmin - 0.05 * w, e.ymin - 0.05 * h, e.xmax + 0.05 * w, e.ymax + 0.05 * h};
    }
    m_nav.fitRect(e);
    requestRender(true);   // navigation: no history, a view.changed
}

void MapView::zoomToLayer(const std::string& id)
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
void MapView::zoomToSelection()
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

// Every layer whose extent is known (§76: the basemaps only if alone).
void MapView::zoomToAll()
{
    if (!m_started)
        return;
    if (const auto all = dataExtent())
        fitWithMargin(*all);
}

// §9: View > Zoom in / out, around the centre.
void MapView::zoomAtCentre(double factor)
{
    if (!m_started)
        return;
    m_nav.zoomAt(m_nav.width() / 2.0, m_nav.height() / 2.0, factor);
    requestRender(true);
}

// ============================================================================
// THE MAP'S COORDINATE SYSTEM (notions §69)
// ============================================================================

// Decided once, in the main thread, and sent to both threads with every
// request: they cannot each choose another. The project's crs:, else the
// first layer that declares a believable one (in degrees: EPSG:3857), as
// render2d's Map does on its own.
void MapView::decideCrs()
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

std::optional<r2d::Envelope> MapView::mapExtent(const LayerEntry& layer) const
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
std::optional<r2d::Envelope> MapView::dataExtent() const
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
void MapView::setCrs(const std::string& text)
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
            std::string refusedText;
            m_commander.run({{"name", "selection.set"}, {"mode", "clear"}}, refusedText);
        }
    }
    requestRender(true);
}

// ============================================================================
// MEASURING (notions §72)
// ============================================================================

const r2d::GeodesicMeter& MapView::meter() const
{
    const std::string system = m_mapCrs.known ? m_mapCrs.wkt : std::string();
    if (!m_meter || m_meterFor != system) {   // made again when the map's system changes
        m_meter    = std::make_unique<r2d::GeodesicMeter>(m_mapCrs);
        m_meterFor = system;
    }
    return *m_meter;
}

std::vector<r2d::Point> MapView::liveMeasure(bool& closed) const
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

std::string MapView::measureText(const std::vector<r2d::Point>& points, bool closed, bool full) const
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

// ============================================================================
// THE STATUS BAR'S PIECES (notions §19)
// ============================================================================

std::string MapView::statusBefore() const
{
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
    const double scale = m_nav.view().scaleDenominator() * m_ratio;
    add("1:%.0f      ", scale);
    if (m_mapCrs.known)   // §69: the system of X, Y and the scale
        add("%s      ", m_mapCrs.epsg ? ("EPSG:" + std::to_string(m_mapCrs.epsg)).c_str() : m_mapCrs.name.c_str());
    return status;
}

std::string MapView::statusAfter() const
{
    std::string status;
    auto add = [&status](const char* fmt, auto... args) {
        char piece[160];
        std::snprintf(piece, sizeof piece, fmt, args...);
        status += piece;
    };
    if (m_worker.busy())
        add("drawing view #%ld...", m_lastRequest);
    else
        add("view #%ld: render %.1f ms, upload[%d] %.1f + finish %.1f ms", m_shownId, m_renderMs, m_texture.method(), m_uploadMs, m_finishMs);
    if (m_lastQuery > 0)
        add("      query %.1f ms", m_queryMs);
    return status;
}

} // namespace e2d
