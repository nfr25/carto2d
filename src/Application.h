#pragma once

#include "command/Commander.h"
#include "command/Document.h"
#include "event/EventBus.h"
#include "map/config.hpp"   // render2d's reader of viewer.yaml (render2d notions §57)
#include "panels/Workspace.h"
#include "panels/SelectionPanel.h"
#include "panels/TablePanel.h"
#include "panels/ConnectDialog.h"
#include "panels/CrsDialog.h"
#include "panels/PrintDialog.h"
#include "panels/LayerChooser.h"
#include "panels/LayerSymbol.h"
#include "panels/StyleDialog.h"
#include "panels/Toolbar.h"
#include "view/DragGesture.h"
#include "view/MapTexture.h"
#include "view/SymbolTextures.h"
#include "view/Navigator.h"
#include "view/SelectionOverlay.h"
#include "view/Tool.h"
#include "window/ImGuiLayer.h"
#include "window/Window.h"
#include "worker/QueryWorker.h"
#include "worker/RenderWorker.h"

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace e2d {

// The main loop. The map sits in the central zone of a dockspace, the panels
// dock around it (§19). The map follows the mouse at 60 images per second (the
// image we have, moved and stretched, §10), and is drawn again in the render
// thread at the end of each gesture (§9, §11). Step 1c: every change goes
// through a command (§15, §16), and events tell whoever listens (§17).
// Step 2: a click asks the thread for questions what is there (§20, §21) and
// changes the selection (§22), drawn over the map (§23).
// Step 2b: tools (§27): a rectangle or a polygon asks what is in a zone (§30),
// the zoom window fits a rectangle (§28).
class Application {
public:
    bool init(const std::string& configPath);   // "": no file, an empty map (§39)
    void addFiles(std::vector<std::string> files) { m_pendingFiles = std::move(files); }   // as if dropped
    void run();

private:
    void handleInput();         // drag, wheel, window size -> the wanted view
    void requestRender(bool viewChanged);   // a new numbered request to the render thread
    void handleShortcuts();     // Ctrl+Z, Ctrl+Y
    void collectResult();       // a finished image -> the texture
    void askQuery(double x, double y);   // a click: "what is here?" (§21)
    void askZone(std::vector<r2d::Point> ring);   // a zone: "what is in it?" (§30)
    void askWhere();                              // §58: an expression from the Selection panel
    void finishShape(const ToolResult& r, double x, double y);   // the "end" of a tool (§27)
    void setTool(ToolKind kind);
    void notify(std::string text);   // a short message in the status bar
    void handleDrops();              // files dropped on the window -> layer.add (§39)
    std::vector<LayerState> layerStates() const;   // the Document's layers, for the threads (§37)
    void zoomToLayer(const std::string& id);       // §28, §39
    void zoomToSelection();
    void fitWithMargin(r2d::Envelope e);           // 5 % around; a point: 1:1000 (§28)
    // §69: the map's coordinate system.
    void decideCrs();                              // not decided yet: the project's, or the first layer's
    void setMapCrs(const std::string& text);       // View > Map system: the same ground, in another system
    std::optional<r2d::Envelope> mapExtent(const LayerEntry& layer) const;   // its extent in the map's system
    std::optional<r2d::Envelope> dataExtent() const;   // §76: all the layers, the basemaps only if alone
    void drawCrsDialog();                          // §70: View > Map system > Other...
    std::vector<CrsLayerRow> crsLayerRows() const; // §70: each layer's system, and its way to the map's
    void drawEmptyHint();                          // "drop files here" on an empty map
    std::set<std::string> unsavedStyles() const;   // §45: changed in the dialog, not saved

    // §52, §53: the project. What comes after the question "save first?".
    enum class Next { None, Quit, New, Open };
    bool loadProject(const std::string& path, std::string& error);   // the map of a viewer.yaml
    void clearProject();                           // an empty map, no file, no history
    r2d::Config projectConfig() const;             // what Save writes: the layers, the view
    std::string projectSignature() const;          // the layers as text: "changed?" (§52)
    bool projectChanged() const;
    void fileAction(FileAction action);            // the File menu and its shortcuts (§53)
    void toolbarActions(const ToolbarResult& bar); // Edit, Selection, View (§61)
    void request(Next next, const std::string& path = {});   // asks first if something is not saved
    void proceed();                                // does m_next
    void saveAndProceed();                         // the styles, then the project, then m_next
    void saveAs(Next then);                        // chooses a file, saves, then `then`
    bool saveProject(const std::string& path);
    void drawSaveQuestion();                       // §45, §52: Save / Don't save / Cancel
    void drawPathQuestion();                       // no Windows dialog (Linux): the path typed
    void updateTitle();                            // "carto2D 0.7.0 - ixelles.yaml *"
    void waitForReason();                          // §56: poll while awake, sleep otherwise
    void collectAnswer();       // an answer -> the selection.set command (§22)
    void drawMapImage();
    void drawAboutWindow();                        // §71: Help > About (what was the Build panel)
    void drawPrint();                              // §77: File > Print: the dialog, the page's outline
    void writePage();                              // §77: asks the render thread for the page
    double pageMapPerGround();                     // §77: at the view's centre, kept while it stays near
    double pageFitScale(const r2d::PageSpec& page);
    void openNotes();                              // §71: Help > editor2d notes, in the browser
    void drawReportPanel();
    std::string statusText() const;   // the status bar (§19)
    void subscribe();           // what the application itself listens to (§17)

    Window     m_window;
    ImGuiLayer m_imgui;

    r2d::ConfigResult m_config;
    std::optional<r2d::Envelope> m_startBbox;   // start: bbox, or the first layers' extent (§39)
    r2d::CrsInfo m_mapCrs;                      // §69: the map's system; sent to the threads with each request
    std::string  m_crsChosen;                   // from the project's crs: or the View menu; "" = the first layer's
    CrsDialog    m_crsDialog;                   // §70
    PrintDialog  m_printDialog;                 // §77
    bool         m_printing = false;            // §77: a page is being written
    double       m_pageK = 1.0;                 // §77: map units per metre of ground...
    std::optional<r2d::Point> m_pageKAt;        // ...computed there
    bool         m_aboutOpen = false;           // §71
    std::vector<CrsLayerRow> m_crsRows;         // §70: computed again when the layers or the system change...
    long         m_crsRowsRevision = -1;
    std::string  m_crsRowsFor;
    std::optional<r2d::Point> m_crsCentre;      // ...the map's centre in degrees, when the view has moved
    r2d::Point   m_crsCentreFrom;
    std::string  m_crsCentreFor;
    std::vector<std::string>     m_startWarnings;   // layers of the file that could not be opened
    std::vector<std::string>     m_pendingFiles;    // from the command line, added like dropped files

    // The state that commands change, the bus, the single door (§15 to §17).
    // Declared in this order: the Commander keeps references to the two others.
    Document  m_doc;
    EventBus  m_bus;
    Commander m_commander{m_doc, m_bus};
    bool      m_renderPending = false;   // set by events, done once per frame
    bool      m_quit    = false;         // answered: quit

    // §52, §53: the project file.
    std::string m_projectPath;           // "" = never saved
    std::string m_savedSignature;        // projectSignature() when read or saved
    std::vector<std::pair<std::size_t, r2d::LayerSpec>> m_unopened;   // layers of the file not opened: kept
    std::set<std::string> m_projectBackedUp;   // files copied to .bak in this session
    Next        m_next = Next::None;     // waiting for the question's answer
    std::string m_nextPath;              // Open: the file, when already known (a drop)
    bool        m_askSave = false;       // open the question at the next frame
    bool        m_askPath = false;       // Linux: open the path question
    bool        m_pathForSave = false;
    Next        m_afterPath = Next::None;
    std::string m_typedPath;
    std::string m_title;                 // the window's, set when it changes

    // §56: frames only when there is a reason.
    double m_awakeUntil    = 1.0;        // the first second: awake (the dockspace settles)
    long   m_naps          = 0;
    int    m_framesCounted = 0;
    double m_fpsSince      = 0.0;
    double m_fps           = 0.0;
    std::deque<Event> m_events;          // the last ones, for the Events panel
    std::string m_commandError;          // the last refused command
    std::string  m_error;       // config or map error: shown in red

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
    FeatureCache m_known;                            // (layer id, FID) -> geometry, attributes
    SelectionPanelState m_selectionPanel;
    TablePanelState     m_table;          // §60
    StyleDialog         m_styleDialog;   // §42
    ConnectDialog       m_connect{&Window::ring};   // §65
    LayerChooser        m_chooser;                  // §66: the layers of a dropped GeoPackage

    // §66: dropped files read in a thread, one after the other; the layers to
    // add, one per frame, so that the window says where it is.
    DbBrowser               m_fileReader{&Window::ring};
    std::deque<std::string> m_toRead;      // dropped files waiting for listLayers
    std::string             m_reading;     // the one being read ("" = none)
    std::deque<std::string> m_toAdd;       // sources for layer.add
    int                     m_addTotal = 0, m_addDone = 0;
    std::string             m_addErrors;
    void readDroppedFiles();               // every frame: the reader's answers, the next file
    void addNextLayer();                   // every frame: one layer.add
    void queueLayer(const std::string& source);
    std::vector<r2d::Point> m_waitingZone;   // a zone asked, not answered yet: still drawn (§30)

    // §72: measuring. The meter follows the map's system; a finished measure
    // stays on the map until Esc or another tool.
    struct Measured {
        std::vector<r2d::Point> points;
        bool                    closed = false;
    };
    std::optional<Measured>               m_measured;
    mutable std::unique_ptr<r2d::GeodesicMeter> m_meter;
    mutable std::string                   m_meterFor;      // the system it was made for
    const r2d::GeodesicMeter& meter() const;
    // The figures of a measure: `full` adds the map's own when they differ (the status bar).
    std::string measureText(const std::vector<r2d::Point>& points, bool closed, bool full) const;
    std::vector<r2d::Point> liveMeasure(bool& closed) const;   // the measure being clicked, with the mouse
    std::string m_notice;                    // a short message (§30: too many features)
    double      m_noticeAt = -1.0;

    // What the image shown says.
    std::vector<std::string> m_reportLines;
    std::vector<std::string> m_warnings;
    std::map<std::string, bool> m_outOfScale;   // per layer id, for the Layers panel (§18)
    LayerSymbols                m_layerSymbols; // the styles read once, for the Layers panel's symbols (§68)
    SymbolTextures              m_symbolTextures; // §78: its image symbols, drawn by render2d, on the card
    double                   m_renderMs = 0.0;
    double                   m_uploadMs = 0.0;   // glTextureSubImage2D (§39)
    double                   m_finishMs = 0.0;   // then glFinish
};

} // namespace e2d
