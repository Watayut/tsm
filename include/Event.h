#pragma once

#include <cstdint>
namespace tsm {

using event_id_t = uint32_t;
using event_data_t = uint32_t;
struct Event
{

    // For undefined-id event
    Event()
      : id(Event::counter_inc()), data(0)
    {}

    // For predefined-id event without event data
    Event(event_id_t id)
      : id(id), data(0)
    {}

    // For predefined-id event with event data
    Event(event_id_t id, event_data_t data)
      : id(id), data(data)
    {}

    bool operator==(const Event& rhs) const { return this->id == rhs.id; }
    bool operator!=(const Event& rhs) const { return !(*this == rhs); }
    bool operator<(const Event& rhs) const { return this->id < rhs.id; }

    event_id_t id;
    event_data_t data;

    static event_id_t counter_inc() {
      thread_local event_id_t counter = 0;
      return ++counter;
    }
};

///< For startSM and stopSM calls, the state machine
///< "automatically" transitions to the starting state.
///< However, the State interface requires that an event
///< be passed to the onEntry and onExit. Hence "null_event"

static Event const null_event = Event{};
} // namespace tsm
