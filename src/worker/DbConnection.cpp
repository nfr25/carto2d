#include "worker/DbConnection.h"

#include <algorithm>
#include <sstream>

namespace e2d {

std::string conninfoValue(const std::string& v)
{
    const bool plain = !v.empty() && v.find_first_of(" \t'\\") == std::string::npos;
    if (plain)
        return v;
    std::string out = "'";
    for (char ch : v) {
        if (ch == '\'' || ch == '\\')
            out += '\\';
        out += ch;
    }
    return out + "'";
}

std::string connectionString(const DbConnection& c)
{
    std::string s = "PG:host=" + conninfoValue(c.host);
    if (!c.port.empty() && c.port != "5432")
        s += " port=" + conninfoValue(c.port);
    s += " dbname=" + conninfoValue(c.dbname);
    if (!c.user.empty())
        s += " user=" + conninfoValue(c.user);
    // §63: a silent server answers "timeout" after 5 s instead of never.
    return s + " connect_timeout=5";
}

std::string recentText(const std::vector<DbConnection>& recent)
{
    std::string out;
    for (const DbConnection& c : recent)
        out += c.host + '\t' + c.port + '\t' + c.dbname + '\t' + c.user + '\n';
    return out;
}

std::vector<DbConnection> parseRecent(const std::string& text)
{
    std::vector<DbConnection> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();   // a file edited on Windows
        std::vector<std::string> f;
        std::size_t start = 0;
        for (std::size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1)
            f.push_back(line.substr(start, tab - start));
        f.push_back(line.substr(start));
        if (f.size() != 4 || f[0].empty() || f[2].empty())
            continue;
        out.push_back({f[0], f[1].empty() ? "5432" : f[1], f[2], f[3]});
    }
    return out;
}

void rememberConnection(std::vector<DbConnection>& recent, const DbConnection& c, std::size_t max)
{
    recent.erase(std::remove(recent.begin(), recent.end(), c), recent.end());
    recent.insert(recent.begin(), c);
    if (recent.size() > max)
        recent.resize(max);
}

} // namespace e2d
