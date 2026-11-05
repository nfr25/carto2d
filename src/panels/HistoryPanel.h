#pragma once

#include "command/Commander.h"
#include "event/EventBus.h"

#include <deque>

namespace e2d {

// The history (notions §15): the past, the last done in bold, then the future
// in grey; Undo and Redo buttons (the same as Ctrl+Z, Ctrl+Y).
void drawHistoryPanel(Commander& commander);

// The last events published (notions §17), newest at the bottom: the hooks a
// script will receive at step 3, seen live.
void drawEventsPanel(const std::deque<Event>& events);

} // namespace e2d
