#pragma once

#include <optional>
#include <string>

struct GLFWwindow;

namespace e2d {

// The window to choose a file (notions §53): Windows' own, the one of
// Notepad. `save`: "Save as" (asks before replacing a file). `start`: a file
// or folder to begin in. Returns the chosen path (UTF-8, forward slashes), or
// nothing when cancelled.
//
// Elsewhere (Linux, where it is tested), there is none: available() is false
// and the application asks for the path in a text field instead.
bool fileDialogAvailable();
std::optional<std::string> chooseYamlFile(GLFWwindow* window, bool save, const std::string& start);

// §77: where to write a printed page (Save as, PDF).
std::optional<std::string> choosePdfFile(GLFWwindow* window, const std::string& start);

// §78: a point symbol's image (Open, SVG or PNG).
std::optional<std::string> chooseImageFile(GLFWwindow* window, const std::string& start);

} // namespace e2d
