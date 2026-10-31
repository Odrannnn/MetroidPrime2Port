#ifndef _CGENERICFSM2STATE
#define _CGENERICFSM2STATE

#include "MetroidPrime/Enemies/CGenericFSM2.hpp"
#include "MetroidPrime/TStateMachineState.hpp"
#include "rstl/rc_ptr.hpp"

// MP3 Proto name; guessed template form.
template < class T >
class CGenericFSM2State : public TStateMachineStateBase< T > {
#ifdef TARGET_PC
  friend struct PortStateCodec; // port save state (port_savestate_fields.cpp)
#endif
public:
  typedef typename TStateMachineStateBase< T >::StateFunc StateFunc;
  typedef typename TStateMachineStateBase< T >::TriggerFunc TriggerFunc;
  typedef typename TStateMachineStateBase< T >::CodeFunc CodeFunc;
  typedef typename TStateMachineStateBase< T >::SStateFunction SStateFunction;
  typedef typename TStateMachineStateBase< T >::STriggerFunction STriggerFunction;
  typedef typename TStateMachineStateBase< T >::SCodeFunction SCodeFunction;

  CGenericFSM2State();

  // TStateMachineStateBase
  virtual ~CGenericFSM2State() {}
  virtual int GetType() const;
  virtual void Reset(CStateManager& mgr, T& owner);
  virtual void SetStateFunctions(const SStateFunction* functions, int count);
  virtual void SetTriggerFunctions(const STriggerFunction* functions, int count);
  virtual void SetCodeFunctions(const SCodeFunction* functions, int count);
  virtual void SetState(CStateManager& mgr, T& owner, const rstl::string& name);
  virtual void Update(CStateManager& mgr, T& owner, float dt);
  virtual bool HasState() const;
  virtual const char* GetName() const;
  virtual float GetTime() const;
  virtual float GetDelay() const;
  virtual void SetDelay(float delay);

  void Setup(const CGenericFSM2& machine);
  // Guessed names.
  int GetStateIndex(const rstl::string& name) const;
  int GetTriggerIndex(const rstl::string& name) const;
  int GetCodeIndex(const rstl::string& name) const;
  int GetSubMachineIndex(const CState2* state) const;
  void SetStateFunction(const rstl::string& name, StateFunc function);
  void SetTriggerFunction(const rstl::string& name, TriggerFunc function);
  void SetCodeFunction(const rstl::string& name, CodeFunc function);
#ifdef TARGET_PC
  // Port console (`fsm`): the names of the states this machine can enter - its own and, depth
  // first, the states of its sub-machines (the ones PortSetStateDeep reaches).
  void PortCollectStateNames(rstl::vector< rstl::string >& out, int maxDepth) const override;
  // Port console (`fsm`): the depth of the first machine (this one 0, its sub-machines 1, ...)
  // whose own states hold `name`, or -1. PortSetStateDeep enters it the way a transition into a
  // sub-machine does, filling path[0..depth) with the sub-machine names it went through.
  int PortFindStateDepth(const rstl::string& name, int maxDepth) const;
  // `want[0..wantCount)`, when given, is the sub-machine path to follow (the same names path
  // reports): a leaf name can exist at several levels, so the name alone picks the wrong one.
  int PortSetStateDeep(CStateManager& mgr, T& owner, const rstl::string& name, const char** path,
                       int maxDepth, const char* const* want = nullptr, int wantCount = 0);
  // Port console (`statecall`): the function SetStateFunctions was given under `name`, whether or
  // not a node of the asset uses it (so a state the shipped FSM never enters can still run), or
  // null.
  StateFunc PortFindStateFunction(const char* name) const;
  // Port console (`trigcall`): the same for the trigger table SetTriggerFunctions was given.
  TriggerFunc PortFindTriggerFunction(const char* name) const;
  // Port console (`codecall`): the same for the code table SetCodeFunctions was given.
  CodeFunc PortFindCodeFunction(const char* name) const;
#endif

private:
  struct SSubMachine { // Guessed name.
    SSubMachine(const CState2* state, const rstl::rc_ptr< CGenericFSM2State< T > >& machine)
    : mState(state), mMachine(machine) {}
    const CState2* mState;
    rstl::rc_ptr< CGenericFSM2State< T > > mMachine;
  };

  // Guessed names.
  void CallCode(const CState2Code& state, CStateManager& mgr, T& owner, float dt);
  bool CallTrigger(const CState2Trigger& state, CStateManager& mgr, T& owner, float dt);
  // MP3 Proto names.
  void ExecuteSubflow(const CState2SubMachine& state, const rstl::string& name, CStateManager& mgr,
                      T& owner, EStateMsg msg, float dt);
  void ExecuteState(const CState2State& state, CStateManager& mgr, T& owner, EStateMsg msg,
                    float dt);
  // Guessed names.
  const CState2SubMachine* GetActiveSubMachine() const;
  const CState2State* GetActiveState() const;
  bool EvaluateTransitions(CStateManager& mgr, T& owner, float dt,
                           const rstl::vector< SStateMachine2Transition >& transitions);
  void ExitActive(CStateManager& mgr, T& owner);

  // Guessed member names.
  const CGenericFSM2* mMachine;
  rstl::vector< StateFunc > mStateFunctions;
  rstl::vector< CodeFunc > mCodeFunctions;
  rstl::vector< TriggerFunc > mTriggerFunctions;
  rstl::vector< SSubMachine > mSubMachines;
  const CState2* mState;
  float mTime;
  float mDelay;
#ifdef TARGET_PC
  const SStateFunction* mPortStateFunctions = nullptr; // the last table SetStateFunctions got
  int mPortStateFunctionCount = 0;
  const STriggerFunction* mPortTriggerFunctions = nullptr; // the last table SetTriggerFunctions got
  int mPortTriggerFunctionCount = 0;
  const SCodeFunction* mPortCodeFunctions = nullptr; // the last table SetCodeFunctions got
  int mPortCodeFunctionCount = 0;
#endif
};

#endif // _CGENERICFSM2STATE
