// "Connect to database" (notions §65): the connection string, the recent
// connections, and the browser's thread. No server needed; with
// E2D_TEST_PG set to a connection string ("PG:host=... dbname=..."), the
// browser also lists that database's tables.

#include "panels/LayerOrder.h"
#include "worker/DbBrowser.h"
#include "worker/DbConnection.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++g_failures;
}

void testConnectionString()
{
    using e2d::DbConnection;
    check(e2d::connectionString({"dbserver", "5432", "gis", "reader"}) ==
              "PG:host=dbserver dbname=gis user=reader connect_timeout=5",
          "the usual case: port 5432 left out, a 5 s timeout");
    check(e2d::connectionString({"srv", "5433", "gis", ""}) == "PG:host=srv port=5433 dbname=gis connect_timeout=5",
          "another port written; no user: libpq takes the system's");
    check(e2d::conninfoValue("my db") == "'my db'", "a space: quoted");
    check(e2d::conninfoValue("o'brien") == "'o\\'brien'", "a quote: escaped");
    check(e2d::conninfoValue("") == "''", "empty: two quotes");
}

void testRecent()
{
    std::vector<e2d::DbConnection> recent;
    e2d::rememberConnection(recent, {"a", "5432", "gis", "reader"});
    e2d::rememberConnection(recent, {"b", "5433", "test", ""});
    e2d::rememberConnection(recent, {"a", "5432", "gis", "reader"});
    check(recent.size() == 2 && recent[0].host == "a", "used again: first, not twice");
    const std::string text = e2d::recentText(recent);
    check(text == "a\t5432\tgis\treader\nb\t5433\ttest\t\n", "one line per connection, tabs, no password");
    check(e2d::parseRecent(text) == recent, "read back: the same");
    check(e2d::parseRecent("a\t5432\tgis\treader\r\nbroken line\n\t\t\t\n").size() == 1,
          "a Windows line end is fine; broken lines are skipped");
    for (int i = 0; i < 12; ++i)
        e2d::rememberConnection(recent, {"h" + std::to_string(i), "5432", "d", "u"});
    check(recent.size() == 8, "at most 8 kept");
}

// §66: surfaces at the bottom, lines, points on top; the list's order inside.
void testOrderForAdding()
{
    auto layer = [](std::string name, std::optional<r2d::GeomType> type) {
        r2d::LayerInfo l;
        l.name = std::move(name);
        l.type = type;
        return l;
    };
    using G = r2d::GeomType;
    const std::vector<r2d::LayerInfo> list = {layer("addresses", G::Point), layer("streets", G::Line),
                                              layer("buildings", G::Polygon), layer("trees", G::Point),
                                              layer("parcels", G::Polygon), layer("misc", std::nullopt)};
    const auto order = e2d::orderForAdding(list, {"addresses", "streets", "buildings", "trees", "parcels", "misc"});
    check(order == std::vector<std::string>{"buildings", "parcels", "misc", "streets", "addresses", "trees"},
          "buildings, parcels, misc (surfaces), streets, then addresses, trees (points on top)");
    check(e2d::orderForAdding(list, {"trees", "parcels"}) == std::vector<std::string>{"parcels", "trees"},
          "only the ticked ones");
}

// Waits for the browser's answer, at most 10 s.
std::optional<e2d::DbListing> wait(e2d::DbBrowser& b)
{
    for (int i = 0; i < 1000; ++i) {
        if (auto a = b.take())
            return a;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

void testBrowser()
{
    int rings = 0;
    e2d::DbBrowser b([&] { ++rings; });
    b.list("absent_database.gpkg");
    const auto a = wait(b);
    check(a && !a->error.empty() && a->tables.empty(), "a source that cannot be opened: the error, no table");
    check(rings == 1 && !b.busy(), "the loop was woken once; not busy any more");
    check(!b.take(), "the answer is taken once");

    const char* pg = std::getenv("E2D_TEST_PG");
    if (!pg) {
        std::printf("skip  no E2D_TEST_PG: no database listed\n");
        return;
    }
    b.list(pg);
    const auto db = wait(b);
    check(db && db->error.empty() && !db->tables.empty(), "E2D_TEST_PG: its tables listed");
    if (db)
        for (const auto& t : db->tables)
            std::printf("      %s  %lld  EPSG:%d\n", t.name.c_str(), t.featureCount, t.crs.epsg);
}

} // namespace

int main()
{
    testConnectionString();
    testRecent();
    testOrderForAdding();
    testBrowser();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
