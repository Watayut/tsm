#pragma once

#include "Event.h"
#include "EventQueue.h"

#include <thread>
///
/// The default policy class for asynchronous event processing. This policy is
/// mixed in with a Hsm class to create an AsynchronousHsm. The client uses
/// the sendEvent method to communicate with the state machine. A separate
/// thread is created and blocks wating on events in the step method.
///
namespace tsm {

template<typename StateType>
struct AsyncExecutionPolicy : public StateType
{
    using EventQueue = EventQueueT<Event, std::mutex>;
    using ThreadCallback = void (AsyncExecutionPolicy::*)();

    AsyncExecutionPolicy()
      : threadCallback_(&AsyncExecutionPolicy::step)
    {}

    AsyncExecutionPolicy(AsyncExecutionPolicy const&) = delete;
    AsyncExecutionPolicy operator=(AsyncExecutionPolicy const&) = delete;
    AsyncExecutionPolicy(AsyncExecutionPolicy&&) = delete;
    AsyncExecutionPolicy operator=(AsyncExecutionPolicy&&) = delete;

    virtual ~AsyncExecutionPolicy()
    {
        interrupt_ = true;
        if (smThread_.joinable()) {
            smThread_.join();
        }
    }

    void onEntry(Event const& e) override
    {
        StateType::onEntry(e);
        smThread_ = std::thread(threadCallback_, this);
    }

    void onExit(Event const& e) override
    {
        eventQueue_.stop();
        interrupt_ = true;

        StateType::onExit(e);
    }

    virtual void step()
    {
        while (!interrupt_) {
            processEvent();
        }
    };

    void sendEvent(Event const& event) { eventQueue_.addEvent(event); }

  protected:
    ThreadCallback threadCallback_;
    std::thread smThread_;
    EventQueue eventQueue_;
    bool interrupt_{};

    void processEvent()
    {
        // This is a blocking wait
        Event const& nextEvent = eventQueue_.nextEvent();
        // go down the Hsm hierarchy to handle the event as that is the
        // "most active state"
        if (!eventQueue_.interrupted()) {
            StateType::dispatch(nextEvent);
        } else {
            LOG(WARNING) << this->id << ": Exiting event loop on interrupt";
        }
    }
};

///
/// Another asynchronous execution policy. The only difference with above is
/// that an Observer's notify method will be invoked at the end of processing
/// each event - specifically, right before the blocking wait for the next
/// event.
///
template<typename StateType, typename Observer>
struct AsyncExecWithObserver
  : public AsyncExecutionPolicy<StateType>
  , public Observer
{
    using AsyncExecutionPolicy<StateType>::interrupt_;
    using Observer::notify;

    AsyncExecWithObserver()
      : AsyncExecutionPolicy<StateType>()
      , Observer()
    {}

    void step() override
    {
        while (!interrupt_) {
            notify();
            this->processEvent();
        }
    }
};
} // namespace tsm
