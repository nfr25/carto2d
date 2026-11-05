#pragma once

#include <string>
#include <vector>

namespace e2d {

// The fields of "Connect to database" (notions §65). No password here: it
// lives in pgpass.conf, or in render2d's memory for this session
// (r2d::setSessionPassword, §65).
struct DbConnection {
    std::string host;
    std::string port = "5432";
    std::string dbname;
    std::string user;

    bool operator==(const DbConnection&) const = default;
};

// "PG:host=dbserver dbname=gis user=reader connect_timeout=5", the source
// GDAL opens. The port only when it is not 5432. A value with a space or a
// quote is written as libpq wants it: 'my db', with \' and \\ escaped.
std::string connectionString(const DbConnection& c);

// One value of a libpq connection string (§65).
std::string conninfoValue(const std::string& v);

// The recent connections, one per line: host, port, dbname, user, separated
// by tabs. No password, ever. Lines that cannot be read are skipped.
std::string recentText(const std::vector<DbConnection>& recent);
std::vector<DbConnection> parseRecent(const std::string& text);

// `c` first; at most `max` connections, no duplicate.
void rememberConnection(std::vector<DbConnection>& recent, const DbConnection& c, std::size_t max = 8);


} // namespace e2d
