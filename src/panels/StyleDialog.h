#pragma once

#include "command/Commander.h"
#include "command/Document.h"
#include "panels/ClassifyPanel.h"
#include "panels/SymbolImage.h"

#include "style/catalog.hpp"  // what each symbolizer can receive (render2d §56)
#include "style/style.hpp"    // render2d's model of a style

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace e2d {

class SymbolTextures;

// What the dialog asks the application to do after a frame.
struct StyleDialogResult {
    std::string notice;          // for the status bar ("saved ...", an error)
    // Layers without style file that were saved: (layer id, new file). The
    // layer now uses it (§43), and the project changes (§52).
    std::vector<std::pair<std::string, std::string>> savedFiles;
};

// ============================================================================
// StyleDialog — the legend dialog (notions §41 to §46).
//
// Built from the layer's style, read by render2d (§33): one tree node per
// rule, one per symbolizer, one widget per value that is there. Each node has
// its "+" (what can still be added, from render2d's catalogue), its "x" and
// its arrows (§45); an expression offers the fields that fit (§46). Every value is edited in a COPY of
// the model; when a gesture ends (no widget active any more), the copy is
// written as YSLD and sent as ONE layer.setStyle command (§42). The map is the
// preview. Save writes the file (.bak first), Revert goes back to it (§43).
// ============================================================================
class StyleDialog {
public:
    void open(const std::string& layerId);
    void reset();   // §53: a new project: closed, nothing remembered

    // Draws the window when open. `configPath`: viewer.yaml, for the folder of
    // new style files ("" = next to the data). `window`: the owner of the
    // file dialog that chooses a symbol's image (§78).
    StyleDialogResult draw(const Document& doc, Commander& commander, const std::string& configPath,
                           GLFWwindow* window = nullptr, SymbolTextures* textures = nullptr);

    // Changed in the dialog, not written to a file yet (§45: the "*", the question on quitting).
    bool unsaved(const LayerEntry& layer) const;

    // Saves every unsaved layer. False if one could not be written (out.notice says why).
    bool saveAll(const Document& doc, Commander& commander, const std::string& configPath, StyleDialogResult& out);

private:
    // §45: a change of the tree's shape, asked while drawing, done after it.
    struct Edit {
        enum Kind { None, AddRule, RemoveRule, MoveRule, AddSymbolizer, RemoveSymbolizer, MoveSymbolizer,
                    AddProperty, RemoveProperty, AddFilter, RemoveFilter, AddElse, RemoveElse };
        Kind                kind = None;
        std::size_t         pass = 0, rule = 0, sym = 0;
        int                 delta = 0;        // a move: -1 up, +1 down
        std::string         key;              // a property of the catalogue
        r2d::SymbolizerKind symKind = r2d::SymbolizerKind::Polygon;
    };
    enum class Fields { None, Numbers, All };   // §46: the fields offered next to an expression

    void load(const LayerEntry& layer);   // the model: dialog text, else file, else render2d's default
    void drawRule(std::size_t pass, std::size_t r, std::size_t count);
    void drawSymbolizer(std::size_t pass, std::size_t r, std::size_t s, std::size_t count);
    void apply(const Edit& e);
    r2d::Symbolizer newSymbolizer(r2d::SymbolizerKind kind) const;   // a text gets the first field
    void commit(const LayerEntry& layer, Commander& commander, StyleDialogResult& out);
    bool save(const LayerEntry& layer, const std::string& configPath, StyleDialogResult& out);

    // A value field: the model is changed only when the text reads (§42).
    // `condition`: a filter; `literal`: a label, where a text without ${ } is fixed.
    // `remove`: the property its "x" removes ("" = none; "filter" for a filter).
    void exprField(const char* label, const std::string& key, std::optional<r2d::Expression>& target,
                   bool condition, bool literal, Fields fields, const std::string& remove);
    // The "x" after a row: removes this property of the current symbolizer.
    void removeButton(const std::string& property);
    void changed() { m_pending = true; }
    // §78: a point symbol's image: the path (relative to the style's folder),
    // "..." for the file dialog, "x" to remove it.
    void imageRow(const std::string& key, std::string& image);
    // §78: the symbol picker: a tab per family, a grid of thumbnails drawn by
    // render2d. The chosen file's whole path, or nothing.
    std::optional<std::string> symbolPicker(const std::string& current);

    bool        m_open = false;
    std::string m_layer;               // its id
    r2d::Style  m_style;               // the copy being edited
    std::string m_loadedText;          // the layer's styleText the copy came from: changed by Ctrl+Z -> reload
    bool        m_loaded = false;
    bool        m_pending = false;     // edited, not yet sent
    ClassifyPanel m_classify;          // §50: Categorize, Graduate (a tab)
    bool          m_showRules = false; // §50: after Apply, the Rules tab
    Edit        m_edit;                // asked this frame
    Edit        m_at;                  // the node being drawn: pass, rule, symbolizer
    std::string m_geometry;            // the layer's: which symbolizers draw something (§45)
    int         m_colour = 0;
    std::vector<std::string> m_fields; // the layer's, and which are numbers (§46)
    std::vector<bool>        m_numeric;
    GLFWwindow*  m_window = nullptr;   // §78: this frame's, for the file dialog
    SymbolTextures* m_textures = nullptr;   // §78: the picker's thumbnails
    std::vector<SymbolFamily> m_families;   // §78: read when the picker opens
    std::string  m_imageBase;          // §78: where relative image paths are read from
    bool         m_styleHasFile = false;
    std::string  m_notice;             // §78: said in the status bar after the frame

    std::map<std::string, std::string> m_typed;   // per expression field: the text being typed
    std::map<std::string, std::string> m_errors;  // per expression field: why it does not read
    std::map<std::string, std::string> m_saved;   // per layer: its styleText when last saved
    std::set<std::string>              m_backedUp;   // files copied to .bak in this session
};

} // namespace e2d
