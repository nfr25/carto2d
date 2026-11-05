#pragma once

#include "map/page.hpp"   // render2d: PageSpec (notions §74)

#include <optional>
#include <string>

struct GLFWwindow;

namespace e2d {

// File > Print... (notions §77): the page's format, its scale, its title,
// the legend, where to write the PDF. The page's centre is the map's: the
// outline drawn on the map follows it, so one places the page by moving and
// zooming the map, the dialog open. render2d composes the page (its §74).
class PrintDialog {
public:
    // `project`: the project's print: section, if any; `fitScale`: the round
    // scale at which the current view fits the frame (when the project has none).
    void open(const std::optional<r2d::PageSpec>& project, const std::string& path, double fitScale);
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

    // Every frame. `fitScale`: as above, for the button. `writing`: a page is
    // being written. Returns true when "Write" is pressed.
    bool draw(GLFWwindow* window, double fitScale, bool writing);

    const r2d::PageSpec& page() const { return m_page; }
    const std::string&   path() const { return m_path; }
    bool                 openAfter() const { return m_openAfter; }

    // What happened to the last page, said under the button.
    void setStatus(std::string text, bool error) { m_status = std::move(text); m_error = error; }

private:
    bool          m_open = false;
    r2d::PageSpec m_page;
    double        m_scale = 1000.0;   // the field, before it goes in m_page
    std::string   m_path;
    bool          m_openAfter = true;
    std::string   m_status;
    bool          m_error = false;
};

} // namespace e2d
