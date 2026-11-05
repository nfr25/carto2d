#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace e2d {

// ============================================================================
// EventBus — "something happened", sent to whoever subscribed (notions §17).
//
// An event is a JSON object with an "event" key:
//     {"event": "layer.visibility", "layer": "Topo_points", "visible": false}
// The same form a script will receive from the server at step 3.
//
// Main thread only. A subscriber must not run a command while it is being
// told: it puts the command in the Commander's queue (Commander::submit).
// ============================================================================

// ordered_json keeps the keys in the order written: "event" first, as one reads it.
using Event = nlohmann::ordered_json;

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    // `name`: one event name, or "*" for all of them.
    void subscribe(std::string name, Handler handler);

    // Calls the subscribers at once, in the order they subscribed.
    void publish(const Event& event);

private:
    struct Subscriber {
        std::string name;
        Handler     handler;
    };
    std::vector<Subscriber> m_subscribers;
};

} // namespace e2d
