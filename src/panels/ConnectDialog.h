#pragma once

#include "worker/DbBrowser.h"
#include "worker/DbConnection.h"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace e2d {

// File > Connect to database (notions §65): a server, its tables, a click.
// The connection is tested in a thread (DbBrowser); the tables chosen come
// back as sources for layer.add, exactly as a dropped file.
class ConnectDialog {
public:
    explicit ConnectDialog(std::function<void()> ring);

    void open();
    // Every frame. Returns the sources to add: "PG:...::table".
    std::vector<std::string> draw();

private:
    void connect();
    void loadRecent();
    void saveRecent() const;

    bool                     m_open = false;
    DbConnection             m_fields;
    std::string              m_password;        // typed: handed to render2d's memory, then forgotten here
    std::vector<DbConnection> m_recent;         // editor2d-connections.txt, no password
    DbBrowser                m_browser;
    DbConnection             m_asked;           // the connection under way
    std::optional<DbListing> m_listing;         // the last answer
    std::set<std::string>    m_chosen;          // table names ticked
};

} // namespace e2d
