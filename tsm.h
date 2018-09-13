#pragma once

#include "Event.h"
#include "EventQueue.h"
#include "State.h"
#include "Transition.h"

#include <atomic>
#include <future>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>

using std::shared_ptr;

namespace tsm {

typedef std::pair<std::shared_ptr<State>, Event> StateEventPair;

template<typename DerivedHSM>
struct StateMachine : public State
{
    using ActionFn = void (DerivedHSM::*)(void);
    using GuardFn = bool (DerivedHSM::*)(void);
    using Transition = TransitionT<State, Event, ActionFn, GuardFn>;
    using TransitionTable =
      std::unordered_map<StateEventPair, shared_ptr<Transition>>;
    using TransitionTableElement =
      std::pair<StateEventPair, shared_ptr<Transition>>;

    struct StateTransitionTable : private TransitionTable
    {
        using TransitionTable::end;
        using TransitionTable::find;
        using TransitionTable::insert;
        using TransitionTable::size;

      public:
        shared_ptr<Transition> next(shared_ptr<State> fromState, Event onEvent)
        {
            // Check if event in HSM
            StateEventPair pair(fromState, onEvent);
            auto it = find(pair);
            if (it != end()) {
                return it->second;
            }

            LOG(ERROR) << "No Transition:" << fromState->name
                       << "\tonEvent:" << onEvent.id;
            return nullptr;
        }

        void print()
        {
            for (const auto& it : *this) {
                LOG(INFO) << it.first.first->name << "," << it.first.second.id
                          << ":" << it.second->toState->name << "\n";
            }
        }
    };

  public:
    StateMachine() = delete;

    StateMachine(std::string name,
                 shared_ptr<State> startState,
                 shared_ptr<State> stopState,
                 EventQueue<Event>& eventQueue,
                 State* parent = nullptr)
      : State(name)
      , interrupt_(false)
      , currentState_(nullptr)
      , startState_(startState)
      , stopState_(std::move(stopState))
      , eventQueue_(eventQueue)
      , parent_(parent)
    {}

    StateMachine(std::string name,
                 EventQueue<Event>& eventQueue,
                 State* parent = nullptr)
      : StateMachine(name, nullptr, nullptr, eventQueue, parent)
    {}

    virtual ~StateMachine() override = default;

    void add(shared_ptr<State> fromState,
             Event onEvent,
             shared_ptr<State> toState,
             ActionFn action = nullptr,
             GuardFn guard = nullptr)
    {

        shared_ptr<Transition> t = std::make_shared<Transition>(
          fromState, onEvent, toState, action, guard);
        addTransition(fromState, onEvent, t);
        eventSet_.insert(onEvent);
    }

    virtual shared_ptr<State> getStartState() const { return startState_; }

    virtual shared_ptr<State> getStopState() const { return stopState_; }

    void OnEntry() override
    {
        DLOG(INFO) << "Entering: " << this->name;
        startHSM();
    }

    void OnExit() override
    {
        // TODO (sriram): This really depends on exit/history policy. Sometimes
        // you want to retain state information when you exit a subhsm for
        // certain events. Maybe adding a currenEvent_ variable would allow
        // DerivedHSMs to override OnExit appropriately.
        currentState_ = nullptr;

        stopHSM();
        DLOG(INFO) << "Exiting: " << name;
    }

    void startHSM()
    {
        DLOG(INFO) << "starting: " << name;
        currentState_ = getStartState();
        // Only start a separate thread if you are the base Hsm
        if (!parent_) {
            smThread_ = std::thread(&StateMachine::execute, this);
        }
        DLOG(INFO) << "started: " << name;
    }

    void stopHSM()
    {
        DLOG(INFO) << "stopping: " << name;

        interrupt_ = true;

        if (!parent_) {
            eventQueue_.stop();
            smThread_.join();
        }
        DLOG(INFO) << "stopped: " << name;
    }

    void execute() override
    {
        while (!interrupt_) {
            if (currentState_ == getStopState()) {
                DLOG(INFO) << this->name << " Done Exiting... ";
                interrupt_ = true;
                break;
            } else {
                DLOG(INFO) << this->name << ": Waiting for event";

                Event nextEvent;
                try {
                    nextEvent = eventQueue_.nextEvent();
                } catch (EventQueueInterruptedException const& e) {
                    if (!interrupt_) {
                        throw e;
                    }
                    LOG(WARNING)
                      << this->name << ": Exiting event loop on interrupt";
                    break;
                }

                LOG(INFO) << "Current State:" << currentState_->name
                          << " Event:" << nextEvent.id;

                shared_ptr<Transition> t =
                  table_.next(currentState_, nextEvent);

                if (!t) {
                    if (parent_) {
                        eventQueue_.addFront(nextEvent);
                        break;
                    } else {
                        LOG(ERROR)
                          << "Reached top level HSM. Cannot handle event";
                        continue;
                    }
                }

                bool result =
                  t->guard && (static_cast<DerivedHSM*>(this)->*(t->guard))();

                ActionFn action = nullptr;
                if (!(t->guard) || result) {
                    action = t->doTransition();
                    currentState_ = t->toState;

                    DLOG(INFO) << "Next State:" << currentState_->name;
                    if (action) {
                        (static_cast<DerivedHSM*>(this)->*action)();
                    }

                    // Now execute the current state
                    currentState_->execute();
                } else {
                    LOG(INFO) << "Guard prevented transition";
                }
            }
        }
    }

    virtual shared_ptr<State> const& getCurrentState() const
    {
        DLOG(INFO) << "GetState : " << this->name;
        return currentState_;
    }

    auto& getEvents() const { return eventSet_; }
    State* getParent() const { return parent_; }
    void setParent(State* parent) { parent_ = parent; }
    auto& getTable() const { return table_; }

  protected:
    std::atomic<bool> interrupt_;
    shared_ptr<State> currentState_;
    shared_ptr<State> startState_;
    shared_ptr<State> stopState_;
    EventQueue<Event>& eventQueue_;
    StateTransitionTable table_;
    State* parent_;
    std::thread smThread_;

  private:
    void addTransition(shared_ptr<State> fromState,
                       Event onEvent,
                       shared_ptr<Transition> t)
    {
        StateEventPair pair(fromState, onEvent);
        TransitionTableElement e(pair, t);
        table_.insert(e);
    }
    std::set<Event> eventSet_;
};

template<typename DerivedHSM1, typename DerivedHSM2>
struct OrthogonalHSM
  : public StateMachine<OrthogonalHSM<DerivedHSM1, DerivedHSM2>>
{
    using type = OrthogonalHSM<DerivedHSM1, DerivedHSM2>;
    using base_type = StateMachine<type>;

    OrthogonalHSM(std::string name,
                  EventQueue<Event>& eventQueue,
                  shared_ptr<StateMachine<DerivedHSM1>> hsm1,
                  shared_ptr<StateMachine<DerivedHSM2>> hsm2,
                  State* parent = nullptr)
      : base_type(name, nullptr, nullptr, eventQueue, parent)
      , hsm1_(hsm1)
      , hsm2_(hsm2)
    {
        hsm1_->setParent(this);
        hsm2_->setParent(this);
    }

    void OnEntry() override
    {
        DLOG(INFO) << "Entering: " << this->name;
        hsm1_->OnEntry();
        hsm2_->OnEntry();
        base_type::OnEntry();
    }

    void OnExit() override
    {
        // TODO(sriram): hsm1->currentState_ = nullptr; etc.

        // Stopping a HSM means stopping all of its sub HSMs
        hsm1_->OnExit();
        hsm2_->OnExit();
        base_type::OnExit();
    }

    void execute() override
    {
        while (!base_type::interrupt_) {
            hsm1_->execute();
            hsm2_->execute();
            Event nextEvent;
            try {
                nextEvent = base_type::eventQueue_.nextEvent();
            } catch (EventQueueInterruptedException const& e) {
                if (!base_type::interrupt_) {
                    throw e;
                }
                LOG(WARNING)
                  << this->name << ": Exiting event loop on interrupt";
                break;
            }
            auto const& hsm1Events = hsm1_->getEvents();
            if (hsm1Events.find(nextEvent) != hsm1Events.end()) {
                base_type::eventQueue_.addFront(nextEvent);
                continue;
            } else {
                if (base_type::parent_) {
                    base_type::eventQueue_.addFront(nextEvent);
                    break;
                } else {
                    LOG(ERROR) << "Reached top level HSM. Cannot handle event";
                    continue;
                }
            }
        }
    }

    auto& getHsm1() const { return hsm1_; }
    auto& getHsm2() const { return hsm2_; }

    shared_ptr<StateMachine<DerivedHSM1>> hsm1_;
    shared_ptr<StateMachine<DerivedHSM2>> hsm2_;
};

} // namespace tsm

// Provide a hash function for StateEventPair
namespace std {
template<>
struct hash<tsm::StateEventPair>
{
    size_t operator()(const tsm::StateEventPair& s) const
    {
        shared_ptr<tsm::State> statePtr = s.first;
        tsm::Event event = s.second;
        size_t address = reinterpret_cast<size_t>(statePtr.get());
        size_t id = event.id;
        size_t hash_value = hash<int>{}(address) ^ (hash<size_t>{}(id) << 1);
        return hash_value;
    }
};

} // namespace std