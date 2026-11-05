#include "window/FileDialog.h"

#ifdef _WIN32
// Defined only if not yet: MinGW's C++ library already defines NOMINMAX.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <filesystem>
#include <vector>
#endif

namespace e2d {

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace

bool fileDialogAvailable()
{
    return true;
}

namespace {

std::optional<std::string> chooseFile(GLFWwindow* window, bool save, const std::string& start, const wchar_t* filter,
                                      const wchar_t* extension)
{
    // The buffer receives the chosen path; it starts with the proposed file name.
    std::vector<wchar_t> buffer(4096, L'\0');
    std::wstring folder;
    if (!start.empty()) {
        const std::filesystem::path p(widen(start));
        if (std::filesystem::is_directory(p)) {
            folder = p.wstring();
        } else {
            folder = p.parent_path().wstring();
            const std::wstring name = p.filename().wstring();
            std::copy_n(name.begin(), std::min(name.size(), buffer.size() - 1), buffer.begin());
        }
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof ofn;
    ofn.hwndOwner       = glfwGetWin32Window(window);
    ofn.lpstrFilter     = filter;
    ofn.lpstrFile       = buffer.data();
    ofn.nMaxFile        = static_cast<DWORD>(buffer.size());
    ofn.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    ofn.lpstrDefExt     = extension;
    ofn.Flags           = OFN_NOCHANGEDIR | OFN_EXPLORER |
                          (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok)
        return std::nullopt;   // cancelled
    std::string path = narrow(buffer.data());
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

} // namespace

std::optional<std::string> chooseYamlFile(GLFWwindow* window, bool save, const std::string& start)
{
    return chooseFile(window, save, start, L"Map (*.yaml, *.yml)\0*.yaml;*.yml\0All files\0*.*\0", L"yaml");
}

std::optional<std::string> choosePdfFile(GLFWwindow* window, const std::string& start)
{
    return chooseFile(window, true, start, L"PDF (*.pdf)\0*.pdf\0SVG (*.svg)\0*.svg\0", L"pdf");
}

std::optional<std::string> chooseImageFile(GLFWwindow* window, const std::string& start)
{
    return chooseFile(window, false, start,
                      L"Symbol images (*.svg, *.png)\0*.svg;*.png\0SVG (*.svg)\0*.svg\0PNG (*.png)\0*.png\0", L"svg");
}

#else

bool fileDialogAvailable()
{
    return false;
}

std::optional<std::string> chooseYamlFile(GLFWwindow*, bool, const std::string&)
{
    return std::nullopt;
}

std::optional<std::string> choosePdfFile(GLFWwindow*, const std::string&)
{
    return std::nullopt;
}

std::optional<std::string> chooseImageFile(GLFWwindow*, const std::string&)
{
    return std::nullopt;
}

#endif

} // namespace e2d
