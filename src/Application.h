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
#include "view/MapView.h"
#include "view/SymbolTextures.h"
#include "window/ImGuiLayer.h"
#include "window/Window.h"

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
// 0.9.1: the map itself is a MapView (§81); the program around it keeps the
// project, the panels, the menus and the frames.
class Application {
public:
    bool init(const std::string& configPath);   // "": no file, an empty map (§39)
    void addFiles(std::vector<std::string> files) { m_pendingFiles = std::move(files); }   // as if dropped
    void run();

private:
    void handleShortcuts();     // the map's keys, then Ctrl+N, O, S, Q, P, Z, Y
    void askWhere();                              // §58: an expression from the Selection panel
    void notify(std::string text);   // a short message in the status bar
    void handleDrops();              // files dropped on the window -> layer.add (§39)
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
    MapView   m_map{m_doc, m_commander, m_bus};   // §81: after the three it keeps references to
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

    std::string m_notice;                    // a short message (§30: too many features)
    double      m_noticeAt = -1.0;

    LayerSymbols                m_layerSymbols; // the styles read once, for the Layers panel's symbols (§68)
    SymbolTextures              m_symbolTextures; // §78: its image symbols, drawn by render2d, on the card
};

} // namespace e2d
