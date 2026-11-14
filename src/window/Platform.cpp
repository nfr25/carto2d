#include "window/Platform.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <cstdlib>
#endif

#include <cstdio>

#include <vector>

namespace e2d {

std::filesystem::path executableFolder()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0)
            return {};
        if (n < buffer.size())
            return std::filesystem::path(std::wstring(buffer.data(), n)).parent_path();
        buffer.resize(buffer.size() * 2);   // a very long path: try again, larger
    }
#else
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : exe.parent_path();
#endif
}

std::string fileUrl(const std::filesystem::path& path)
{
    std::string p = std::filesystem::absolute(path).generic_string();
    std::string out = "file://";
    if (!p.empty() && p[0] != '/')
        out += '/';   // file:///C:/...
    for (char c : p)
        out += c == ' ' ? std::string("%20") : std::string(1, c);
    return out;
}

void showError(const std::string& title, const std::string& text)
{
    std::fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
#ifdef _WIN32
    auto wide = [](const std::string& s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(static_cast<std::size_t>(n > 0 ? n : 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    };
    MessageBoxW(nullptr, wide(text).c_str(), wide(title).c_str(), MB_OK | MB_ICONERROR);
#endif
}

bool openInBrowser(const std::string& url)
{
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), n);
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return r > 32;   // ShellExecute's convention: above 32 is success
#else
    const std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#endif
}

} // namespace e2d
