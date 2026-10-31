#ifndef _TSTATEMACHINESTATE
#define _TSTATEMACHINESTATE

#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CStateMachine.hpp"
#include "MetroidPrime/StateMachineCommon.hpp"
#include "rstl/string.hpp"
#include "rstl/vector.hpp"

class CState;
class CStateMachine;
class CStateManager;

// Guessed name. Echoes dispatches owner-specific member functions, unlike Prime's AI state.
template < class T >
struct TStateMachineFunctionTypes {
  typedef int StateMsg;
  typedef float TriggerArg;
  typedef void (T::*StateFunc)(CStateManager&, int, float);
  typedef bool (T::*TriggerFunc)(CStateManager&, const float&);
};

namespace rstl {
template < class T >
struct is_trivially_destructible< void (T::*)(CStateManager&, int, float) > {
  enum { value = true };
};

template < class T >
struct is_trivially_destructible< bool (T::*)(CStateManager&, const float&) > {
  enum { value = true };
};

template < class T >
struct is_trivially_destructible< void (T::*)(CStateManager&, float) > {
  enum { value = true };
};
} // namespace rstl

// Guessed name. AFSM and FSM2 dispatchers share this virtual interface.
template < class T >
class TStateMachineStateBase {
public:
  typedef typename TStateMachineFunctionTypes< T >::StateFunc StateFunc;
  typedef typename TStateMachineFunctionTypes< T >::TriggerFunc TriggerFunc;
  typedef void (T::*CodeFunc)(CStateManager&, float); // Guessed name.
  struct SStateFunction {
    const char* mName;
    StateFunc mFunction;
  };
  struct STriggerFunction {
    const char* mName;
    TriggerFunc mFunction;
  };
  struct SCodeFunction { // Guessed name.
    const char* mName;
    CodeFunc mFunction;
  };

  virtual ~TStateMachineStateBase();
  virtual int GetType() const = 0;
  virtual void Reset(CStateManager& mgr, T& owner) = 0;
  virtual void SetStateFunctions(const SStateFunction* functions, int count) = 0;
  virtual void SetTriggerFunctions(const STriggerFunction* functions, int count) = 0;
  virtual void SetCodeFunctions(const SCodeFunction* functions, int count) = 0; // Guessed name.
  virtual void SetState(CStateManager& mgr, T& owner, const rstl::string& name) = 0;
  virtual void Update(CStateManager& mgr, T& owner, float dt) = 0;
  virtual bool HasState() const = 0;
  virtual const char* GetName() const = 0;
  virtual float GetTime() const = 0;
  virtual float GetDelay() const = 0;
  virtual void SetDelay(float delay) = 0;
#ifdef TARGET_PC
  // Port console (`fsm`): the names of the states this machine can enter - its own and, for an
  // FSM2, the states of its sub-machines.  Virtual, so the console never has to read a derived
  // member through a cast (the machine behind the base is either class).
  virtual void PortCollectStateNames(rstl::vector< rstl::string >& out, int maxDepth) const {}
#endif
};

// Guessed name. The target shares dispatch code across owners; template ownership is unverified.
template < class T >
class TStateMachineState : public TStateMachineStateBase< T > {
#ifdef TARGET_PC
  friend struct PortStateCodec; // port save state (port_savestate_fields.cpp)
#endif
public:
  typedef typename TStateMachineStateBase< T >::StateFunc StateFunc;
  typedef typename TStateMachineStateBase< T >::TriggerFunc TriggerFunc;
  typedef typename TStateMachineStateBase< T >::SStateFunction SStateFunction;
  typedef typename TStateMachineStateBase< T >::STriggerFunction STriggerFunction;
  typedef typename TStateMachineStateBase< T >::SCodeFunction SCodeFunction;

  TStateMachineState();

  // TStateMachineStateBase
  virtual ~TStateMachineState();
  virtual int GetType() const;
  virtual void Reset(CStateManager& mgr, T& owner);
  virtual void SetStateFunctions(const SStateFunction* functions, int count);
  virtual void SetTriggerFunctions(const STriggerFunction* functions, int count);
  virtual void SetCodeFunctions(const SCodeFunction* functions, int count);
  virtual void SetState(CStateManager& mgr, T& owner, const rstl::string& name);
  virtual void Update(CStateManager& mgr, T& owner, float dt);
  virtual bool HasState() const;
  virtual const char* GetName() const;
  virtual float GetTime() const { return mTime; }
  virtual float GetDelay() const;
  virtual void SetDelay(float delay);

  void Setup(const CStateMachine* machine);
  void SetState(CStateManager& mgr, T& owner, int index);
  int GetStateIndex(const rstl::string& name) const;
  void SetStateFunction(const rstl::string& name, StateFunc func);
  void SetTriggerFunction(const rstl::string& name, TriggerFunc func);
  float GetRandom() const { return mRandom; }
  float GetFixedRandom() const { return mFixedRandom; }
  bool GetCodeTrigger() const { return mCodeTrigger; }
  const CState* GetCurrentState() const { return mState; } // Guessed name
  void SetCodeTrigger() { mCodeTrigger = true; }
  const CState* GetState() const { return mState; }
#ifdef TARGET_PC
  // Port console (`fsm`): the names of the states this machine holds, so an unknown name can be
  // reported with the list of the valid ones.
  void PortCollectStateNames(rstl::vector< rstl::string >& out, int maxDepth) const override;
  // Port console (`statecall`/`trigcall`): the function the owner's table gave under `name`,
  // whether or not the machine has a state or trigger of that name. Same as CGenericFSM2State's.
  StateFunc PortFindStateFunction(const char* name) const {
    for (int i = 0; i < mPortStateFunctionCount; ++i) {
      if (strcmp(mPortStateFunctions[i].mName, name) == 0) {
        return mPortStateFunctions[i].mFunction;
      }
    }
    return StateFunc();
  }
  TriggerFunc PortFindTriggerFunction(const char* name) const {
    for (int i = 0; i < mPortTriggerFunctionCount; ++i) {
      if (strcmp(mPortTriggerFunctions[i].mName, name) == 0) {
        return mPortTriggerFunctions[i].mFunction;
      }
    }
    return TriggerFunc();
  }
  // Port console (`var`): the time in the current state, so a state's timeout arm runs on demand.
  void PortSetTime(float time) { mTime = time; }
#endif

private:
  void CallState(const CState& state, CStateManager& mgr, T& owner, EStateMsg msg, float arg);
  bool CallTrigger(const CTrigger& trigger, CStateManager& mgr, T& owner);

  rstl::vector< StateFunc > mStateFunctions;
  rstl::vector< TriggerFunc > mTriggerFunctions;
  const CStateMachine* mMachine;
  const CState* mState;
  float mTime;
  float mRandom;
  float mDelay;
  float mFixedRandom;
  bool mCodeTrigger : 1;
#ifdef TARGET_PC
  const SStateFunction* mPortStateFunctions = nullptr; // the last table SetStateFunctions got
  int mPortStateFunctionCount = 0;
  const STriggerFunction* mPortTriggerFunctions = nullptr; // the last table SetTriggerFunctions got
  int mPortTriggerFunctionCount = 0;
#endif
};

template < class T >
TStateMachineState< T >::TStateMachineState()
: mMachine(nullptr), mState(nullptr), mTime(0.f), mRandom(0.f), mDelay(0.f), mCodeTrigger(false) {}

template < class T >
TStateMachineState< T >::~TStateMachineState() {}

template < class T >
int TStateMachineState< T >::GetType() const {
  return 0;
}

#ifdef TARGET_PC
template < class T >
void TStateMachineState< T >::PortCollectStateNames(rstl::vector< rstl::string >& out,
                                                   int maxDepth) const {
  if (mMachine != nullptr) {  // the asset is missing until the machine is set up
    for (const CState& state : mMachine->GetStateVector()) {
      out.push_back(rstl::string(state.GetName()));
    }
  }
  // The owner's own table (the port registers the module's states under the retail's names).
  for (int i = 0; i < mPortStateFunctionCount; ++i) {
    out.push_back(rstl::string(mPortStateFunctions[i].mName));
  }
}
#endif

template < class T >
void TStateMachineState< T >::Setup(const CStateMachine* machine) {
  if (machine == nullptr) {
    return;
  }
  mMachine = machine;
  const int stateCount = machine->GetStateVector().size();
  const int triggerCount = machine->GetTriggerVector().size();
  mStateFunctions.reserve(stateCount);
  mTriggerFunctions.reserve(triggerCount);
  for (int i = 0; i < stateCount; ++i) {
    mStateFunctions.push_back_unsafe(nullptr);
  }
  for (int i = 0; i < triggerCount; ++i) {
    mTriggerFunctions.push_back_unsafe(nullptr);
  }
}

template < class T >
void TStateMachineState< T >::Reset(CStateManager& mgr, T& owner) {
  if (mState != nullptr) {
    CallState(*mState, mgr, owner, kStateMsg_Deactivate, 0.f);
  }
  mState = nullptr;
  mMachine = nullptr;
  mStateFunctions.clear();
  mTriggerFunctions.clear();
}

template < class T >
void TStateMachineState< T >::SetState(CStateManager& mgr, T& owner, int index) {
  if (mMachine == nullptr || index < 0 || index >= mMachine->GetStateVector().size()) {
    return;
  }
  const CState* state = &mMachine->GetStateVector()[index];
  if (mState == state) {
    return;
  }
  if (mState != nullptr) {
    CallState(*mState, mgr, owner, kStateMsg_Deactivate, 0.f);
  }
  mState = state;
  mTime = 0.f;
  mRandom = mgr.Random()->Float();
  mCodeTrigger = false;
  CallState(*mState, mgr, owner, kStateMsg_Activate, 0.f);
}

template < class T >
void TStateMachineState< T >::SetState(CStateManager& mgr, T& owner, const rstl::string& name) {
  const int index = GetStateIndex(name);
  if (index != -1) {
    SetState(mgr, owner, index);
  }
}

template < class T >
void TStateMachineState< T >::Update(CStateManager& mgr, T& owner, float dt) {
  if (mState == nullptr) {
    return;
  }
  mTime += dt;
  CallState(*mState, mgr, owner, kStateMsg_Update, dt);
  for (int i = 0; i < mState->GetNumTriggers(); ++i) {
    const CTrigger* trigger = mState->GetTrig(i);
    const CState* state = nullptr;
    bool andPassed = true;
    while (andPassed && trigger != nullptr) {
      andPassed = false;
      if (CallTrigger(*trigger, mgr, owner)) {
        andPassed = true;
        state = trigger->GetState();
        trigger = trigger->GetAnd();
      }
    }
    if (andPassed && state != nullptr) {
      CallState(*mState, mgr, owner, kStateMsg_Deactivate, 0.f);
      mState = state;
      mTime = 0.f;
      mCodeTrigger = false;
      mRandom = mgr.Random()->Float();
      CallState(*mState, mgr, owner, kStateMsg_Activate, 0.f);
      return;
    }
  }
}

template < class T >
const char* TStateMachineState< T >::GetName() const {
  return mState != nullptr ? mState->GetName() : nullptr;
}

template < class T >
bool TStateMachineState< T >::HasState() const {
  return mState != nullptr;
}

template < class T >
float TStateMachineState< T >::GetDelay() const {
  return mDelay;
}

template < class T >
void TStateMachineState< T >::SetDelay(float delay) {
  mDelay = delay;
}

template < class T >
int TStateMachineState< T >::GetStateIndex(const rstl::string& name) const {
  if (mMachine != nullptr) {
    const rstl::vector< CState >& states = mMachine->GetStateVector();
    for (int i = 0; i < states.size(); ++i) {
      if (strncmp(states[i].GetName(), name.data(), 31) == 0) {
        return i;
      }
    }
  }
  return -1;
}

template < class T >
void TStateMachineState< T >::SetStateFunction(const rstl::string& name, StateFunc func) {
  if (mMachine != nullptr) {
    const rstl::vector< CState >& states = mMachine->GetStateVector();
    for (int i = 0; i < states.size(); ++i) {
      if (strncmp(states[i].GetName(), name.data(), 31) == 0) {
        mStateFunctions[i] = func;
      }
    }
  }
}

template < class T >
void TStateMachineState< T >::SetStateFunctions(const SStateFunction* functions, int count) {
#ifdef TARGET_PC
  mPortStateFunctions = functions;
  mPortStateFunctionCount = count;
#endif
  if (functions != nullptr) {
    for (int i = 0; i < count; ++i) {
      SetStateFunction(functions[i].mName, functions[i].mFunction);
    }
  }
}

template < class T >
void TStateMachineState< T >::SetTriggerFunctions(const STriggerFunction* functions, int count) {
#ifdef TARGET_PC
  mPortTriggerFunctions = functions;
  mPortTriggerFunctionCount = count;
#endif
  if (functions != nullptr) {
    for (int i = 0; i < count; ++i) {
      SetTriggerFunction(functions[i].mName, functions[i].mFunction);
    }
  }
}

template < class T >
void TStateMachineState< T >::SetTriggerFunction(const rstl::string& name, TriggerFunc func) {
  if (mMachine != nullptr) {
    const rstl::vector< CTrigger >& triggers = mMachine->GetTriggerVector();
    for (int i = 0; i < triggers.size(); ++i) {
      if (strncmp(triggers[i].GetName(), name.data(), 31) == 0) {
        mTriggerFunctions[i] = func;
      }
    }
  }
}

template < class T >
void TStateMachineState< T >::SetCodeFunctions(const SCodeFunction* functions, int count) {}

template < class T >
bool TStateMachineState< T >::CallTrigger(const CTrigger& trigger, CStateManager& mgr, T& owner) {
  if (mMachine == nullptr || trigger.GetIndex() >= mMachine->GetTriggerVector().size()) {
    return false;
  }
  TriggerFunc func = mTriggerFunctions[trigger.GetIndex()];
  bool result = trigger.IsDefault();
  if (func != nullptr) {
    const typename TStateMachineFunctionTypes< T >::TriggerArg arg(trigger.GetArg());
    result = (owner.*func)(mgr, arg);
    if (trigger.IsNot()) {
      result = !result;
    }
  }
  return result;
}

template < class T >
void TStateMachineState< T >::CallState(const CState& state, CStateManager& mgr, T& owner,
                                        EStateMsg msg, float arg) {
  if (mMachine != nullptr && state.GetIndex() < mMachine->GetStateVector().size()) {
    StateFunc func = mStateFunctions[state.GetIndex()];
    if (func != nullptr) {
      (owner.*func)(mgr, static_cast< typename TStateMachineFunctionTypes< T >::StateMsg >(msg),
                    arg);
    }
  }
}

template < class T >
TStateMachineStateBase< T >::~TStateMachineStateBase() {}

#endif // _TSTATEMACHINESTATE
