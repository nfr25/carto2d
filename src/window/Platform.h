#pragma once

#include <filesystem>
#include <string>

namespace e2d {

// The folder of the running executable (notions §71): asked to the system,
// not the current folder, which depends on how the program was started.
// Empty when it cannot be known.
std::filesystem::path executableFolder();

// Opens a web address or a local file ("file:///...") in the default browser.
// False when the system refused.
bool openInBrowser(const std::string& url);

// "file:///C:/.../notions.en.html" for a local file.
std::string fileUrl(const std::filesystem::path& path);

} // namespace e2d
