#pragma once

#include "command/Commander.h"
#include "command/Document.h"
#include "event/EventBus.h"
#include "panels/Workspace.h"        // MapArea: where the map is (§19)
#include "view/DragGesture.h"
#include "view/MapTexture.h"
#include "view/Navigator.h"
#include "view/SelectionOverlay.h"   // FeatureCache
#include "view/Tool.h"
#include "window/Window.h"           // MouseState
#include "worker/QueryWorker.h"
#include "worker/RenderWorker.h"

#include "backend/gdal/gdal_reproject.hpp"   // render2d: CrsInfo, GeodesicMeter (§69, §72)

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace e2d {

// ============================================================================
// MapView — the map as an ImGui widget (notions §81, 0.9.1).
//
// Everything that makes the map a map, gathered from Application: the wanted
// view and the image we have (§10), the gestures (§9) and the tools (§27),
// the render thread and its numbered requests (§11), the thread for questions
// and the selection it changes (§21, §22), the selection and shapes drawn
// over the map (§23, §28), measuring (§72), the map's coordinate system
// (§69), "zoom to" (§28, §76). The code moved, the behaviour did not.
//
// The program calls it once per frame, in this order:
//     input(area, mouse, wantsMouse, now)   the gestures
//     keys()                                Esc, the tools' letters, Enter, Backspace
//     collect()                             a finished image, an answer
//     draw(focus)                           the image, the selection, the shapes
//     endFrame()                            one render for the frame's changes
// A second program (editor2d) puts the same map in its own window.
// ============================================================================
class MapView {
public:
    MapView(Document& doc, Commander& commander, EventBus& bus);

    // The two threads; `ring` wakes the main thread when one has posted (§56).
    void start(float pixelRatio, std::function<void()> ring);
    void stop();
    // What the map wants to say (a notice in the status bar), and a command
    // that was refused (the Report).
    void setMessages(std::function<void(const std::string&)> notify, std::function<void(const std::string&)> refused);

    // ---- the frame -----------------------------------------------------------
    void input(const MapArea& area, const MouseState& mouse, bool panelWantsMouse, double now);
    void keys();
    void collect();
    void draw(const std::pair<std::string, std::int64_t>& focus = {});
    void endFrame();
    bool gestureGoingOn() const;   // §56: frames go on while true

    // ---- the project -----------------------------------------------------------
    void reset();                                      // a new project: no system, no view, no image
    void setStartBbox(std::optional<r2d::Envelope> e) { m_startBbox = e; }
    const std::optional<r2d::Envelope>& startBbox() const { return m_startBbox; }
    std::optional<r2d::Envelope> currentBbox() const;  // what Save writes: the view, else the start
    void requestRender(bool viewChanged);

    // ---- the map's coordinate system (§69) ----------------------------------------
    void setCrsChosen(const std::string& text) { m_crsChosen = text; }   // the project's crs:
    const std::string& crsChosen() const { return m_crsChosen; }
    const r2d::CrsInfo& crs() const { return m_mapCrs; }
    void decideCrs();                                  // not decided yet: the chosen one, or the first layer's
    void setCrs(const std::string& text);              // View > Map system: the same ground, in another system
    std::optional<r2d::Envelope> mapExtent(const LayerEntry& layer) const;   // in the map's system
    std::optional<r2d::Envelope> dataExtent() const;   // §76: all the layers, the basemaps only if alone

    // ---- navigation -------------------------------------------------------------
    bool started() const { return m_started; }
    const Navigator& nav() const { return m_nav; }
    const MapArea& area() const { return m_area; }
    float pixelRatio() const { return m_ratio; }
    void fitWithMargin(r2d::Envelope e);               // 5 % around; a point: 1:1000 (§28)
    void zoomToLayer(const std::string& id);
    void zoomToSelection();
    void zoomToAll();
    void zoomAtCentre(double factor);                  // §9: View > Zoom in / out

    // ---- tools and questions --------------------------------------------------------
    void setTool(ToolKind kind);
    ToolKind tool() const { return m_tool.kind(); }
    // §58: an expression from the Selection panel; its result, once, as text.
    void askWhere(const std::string& layer, const std::string& where, int mode);
    std::optional<std::string> takeWhereResult();

    // ---- what the map knows ---------------------------------------------------------
    FeatureCache& known() { return m_known; }          // (layer id, FID) -> geometry, attributes
    const std::vector<std::string>& reportLines() const { return m_reportLines; }
    const std::vector<std::string>& warnings() const { return m_warnings; }
    const std::map<std::string, bool>& outOfScale() const { return m_outOfScale; }
    const std::map<std::string, LayerCounts>& counts() const { return m_counts; }   // §83: of the image shown
    std::vector<LayerState> layerStates() const;       // the Document's layers, for the threads (§37)
    RenderWorker& worker() { return m_worker; }        // the printed page goes through it (§77)
    // The status bar's pieces (§19): before the program's own (the tool, the
    // measure, the mouse, the scale, the system), and after them (the threads).
    std::string statusBefore() const;
    std::string statusAfter() const;

private:
    void askQuery(double x, double y);                 // a click: "what is here?" (§21)
    void askZone(std::vector<r2d::Point> ring);        // a zone: "what is in it?" (§30)
    void finishShape(const ToolResult& r, double x, double y);   // the "end" of a tool (§27)
    void collectResult();                              // a finished image -> the texture
    void collectAnswer();                              // an answer -> the selection.set command (§22)
    void drawImage();
    void notify(const std::string& text) const { if (m_notify) m_notify(text); }
    void refused(const std::string& text) const { if (m_refused) m_refused(text); }
    const r2d::GeodesicMeter& meter() const;
    // The figures of a measure: `full` adds the map's own when they differ (the status bar).
    std::string measureText(const std::vector<r2d::Point>& points, bool closed, bool full) const;
    std::vector<r2d::Point> liveMeasure(bool& closed) const;   // the measure being clicked, with the mouse

    Document&  m_doc;
    Commander& m_commander;
    EventBus&  m_bus;
    std::function<void(const std::string&)> m_notify, m_refused;
    float      m_ratio = 1.0f;                // the screen's content scale (§7)
    bool       m_renderPending = false;       // set by events, done once per frame (§17)

    std::optional<r2d::Envelope> m_startBbox;   // start: bbox, or the first layers' extent (§39)
    r2d::CrsInfo m_mapCrs;                      // §69: the map's system; sent to the threads with each request
    std::string  m_crsChosen;                   // from the project's crs: or the View menu; "" = the first layer's

    // The central zone of the dockspace: where the map is (§19).
    MapArea    m_area;

    // The WANTED view (§10) and the image we have, with the view it was drawn for.
    Navigator  m_nav;
    bool       m_started = false;
    int        m_startW = 0, m_startH = 0;   // the zone's size at the previous frame, before starting
    MapTexture m_texture;
    ViewState  m_imageState;

    // Gestures (§9): one render per gesture, at its end.
    DragGesture m_gesture;                     // a click or a drag (§20), Pointer tool
    Tool        m_tool;                        // what the left button does (§27)
    bool        m_toolPressed = false;         // the left button went down on the map, tool not Pointer
    bool        m_middleDown = false;          // the middle button pans with every tool (§27)
    bool        m_middleMoved = false;
    double      m_middleX = 0.0, m_middleY = 0.0;
    double m_mouseX = -1.0, m_mouseY = -1.0;   // relative to the map's corner (§19)
    bool   m_mouseInMap = false;
    bool   m_mapFocused = true;                // the last click was on the map: keys move it (§9)
    bool   m_wheelPending = false;
    double m_lastWheel = 0.0;
    bool   m_sizePending = false;
    double m_sizeSince = 0.0;

    // The render thread and its numbered requests (§11).
    RenderWorker m_worker;
    long         m_lastRequest = 0;
    long         m_shownId = 0;

    // The thread for questions, its own Map (§21), and what the answers taught us.
    QueryWorker  m_query;
    long         m_lastQuery = 0;
    double       m_queryMs = 0.0;
    FeatureCache m_known;
    std::vector<r2d::Point> m_waitingZone;     // a zone asked, not answered yet: still drawn (§30)
    std::optional<std::string> m_whereResult;  // §58: for the Selection panel, once

    // §72: measuring. The meter follows the map's system; a finished measure
    // stays on the map until Esc or another tool.
    struct Measured {
        std::vector<r2d::Point> points;
        bool                    closed = false;
    };
    std::optional<Measured>               m_measured;
    mutable std::unique_ptr<r2d::GeodesicMeter> m_meter;
    mutable std::string                   m_meterFor;      // the system it was made for

    // What the image shown says.
    std::vector<std::string>    m_reportLines;
    std::vector<std::string>    m_warnings;
    std::map<std::string, bool> m_outOfScale;   // per layer id, for the Layers panel (§18)
    std::map<std::string, LayerCounts> m_counts;  // per layer id, the classes' counts (§83)
    double m_renderMs = 0.0;
    double m_uploadMs = 0.0;   // glTextureSubImage2D (§39)
    double m_finishMs = 0.0;   // then glFinish
};

} // namespace e2d
