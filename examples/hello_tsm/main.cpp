#include <tsm.h>

using namespace tsm;

struct SocketSMDefinition : StateMachineDef<SocketSMDefinition>
{
  public:
    SocketSMDefinition(IHsmDef* parent = nullptr)
      : StateMachineDef<SocketSMDefinition>("Socket State Machine", parent)
      , Closed("closed")
      , Ready("ready")
      , Bound("bound")
      , Open("open")
      , Listening("listening")
    {
        add(Closed, sock_open, Ready);
        add(Ready, connect, Open);
        add(Ready, bind, Bound);
        add(Bound, listen, Listening);
        add(Listening, accept, Listening);
        add(Listening, close, Closed);
        add(Open, close, Closed);
    }

    // Events
    Event sock_open;
    Event bind;
    Event listen;
    Event connect;
    Event accept;
    Event close;

    // States
    State Closed;
    State Ready;
    State Bound;
    State Open;
    State Listening;

    State* getStartState() override { return &Closed; }
    State* getStopState() override { return nullptr; }
};

using SocketHSMParentThread = SimpleStateMachine<SocketSMDefinition>;

int
main()
{
    SocketHSMParentThread sm;

    // Important!!
    sm.startSM();

    // send events... and step the sm
    sm.sendEvent(sm.sock_open);
    sm.step();

    sm.stopSM();
}
