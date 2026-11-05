#pragma once

#include "command/Document.h"

#include "style/classify.hpp"   // render2d: summaries, classify() (its §62, §63)
#include "style/style.hpp"

#include <optional>
#include <string>

namespace e2d {

// ============================================================================
// ClassifyPanel — the assistants Categorize and Graduate (notions §48 to §50):
// the "Classify" tab of the legend dialog.
//
// A field is chosen; its values are read once (render2d's summarizeField, in
// the main thread, as the probe of §38); the preview shows the legend to come,
// with the count of each class. Apply replaces the rules of the dialog's copy
// of the style with render2d's classify() — the same recipe as
// `render2d --graduate` (its §63): the classes repeated in each scale band.
// ============================================================================
class ClassifyPanel {
public:
    void reset();   // §53: a new project

    // Draws the tab's content. True when Apply was pressed: `style` was replaced.
    bool draw(const LayerEntry& layer, r2d::Style& style);

private:
    void read(const LayerEntry& layer, const std::string& field);   // the summary of a field
    r2d::ClassifySettings settings() const;
    r2d::Symbolizer fallback(const LayerEntry& layer) const;          // no symbol in the style

    std::string m_layer;                         // the summary is of this layer...
    std::string m_field;                         // ...and of this field
    std::optional<r2d::FieldSummary> m_summary;
    std::string m_error;
    double      m_readMs = 0.0;                  // §50: measured, to see if it is too slow
    bool        m_graduate = false;
    int         m_classes  = 5;
    int         m_method   = 0;                  // 0 equal intervals, 1 quantiles
    float       m_from[3] = {1.0f, 245.0f / 255.0f, 235.0f / 255.0f};   // #FFF5EB (§49)
    float       m_to[3]   = {140.0f / 255.0f, 45.0f / 255.0f, 4.0f / 255.0f};   // #8C2D04
};

} // namespace e2d
