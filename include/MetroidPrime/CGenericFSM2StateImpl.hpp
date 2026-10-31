#ifndef _CGENERICFSM2STATEIMPL
#define _CGENERICFSM2STATEIMPL

// Member definitions of CGenericFSM2State. Only the translation units that instantiate the
// template include this; everything else uses the instantiation linked from the main program.
#include "MetroidPrime/CGenericFSM2State.hpp"

template < class T >
CGenericFSM2State< T >::CGenericFSM2State()
: mMachine(nullptr), mState(nullptr), mTime(0.f), mDelay(0.f) {}

template < class T >
int CGenericFSM2State< T >::GetType() const {
  return 1;
}

template < class T >
void CGenericFSM2State< T >::Reset(CStateManager& mgr, T& owner) {
  const CState2State* state = GetActiveState();
  if (state != nullptr) {
    ExecuteState(*state, mgr, owner, kStateMsg_Deactivate, 0.f);
  }
  mState = nullptr;
  mMachine = nullptr;
  mStateFunctions.clear();
  mTriggerFunctions.clear();
  mCodeFunctions.clear();
}

template < class T >
void CGenericFSM2State< T >::SetStateFunctions(const SStateFunction* functions, int count) {
#ifdef TARGET_PC
  mPortStateFunctions = functions;
  mPortStateFunctionCount = count;
#endif
  for (int i = 0; i < mSubMachines.size(); ++i) {
    mSubMachines[i].mMachine->SetStateFunctions(functions, count);
  }
  if (functions != nullptr) {
    for (int i = 0; i < count; ++i) {
      SetStateFunction(rstl::string_l(functions[i].mName), functions[i].mFunction);
    }
  }
}

template < class T >
void CGenericFSM2State< T >::SetTriggerFunctions(const STriggerFunction* functions, int count) {
#ifdef TARGET_PC
  mPortTriggerFunctions = functions;
  mPortTriggerFunctionCount = count;
#endif
  for (int i = 0; i < mSubMachines.size(); ++i) {
    mSubMachines[i].mMachine->SetTriggerFunctions(functions, count);
  }
  if (functions != nullptr) {
    for (int i = 0; i < count; ++i) {
      SetTriggerFunction(rstl::string_l(functions[i].mName), functions[i].mFunction);
    }
  }
}

template < class T >
void CGenericFSM2State< T >::SetCodeFunctions(const SCodeFunction* functions, int count) {
#ifdef TARGET_PC
  mPortCodeFunctions = functions;
  mPortCodeFunctionCount = count;
#endif
  for (int i = 0; i < mSubMachines.size(); ++i) {
    mSubMachines[i].mMachine->SetCodeFunctions(functions, count);
  }
  if (functions != nullptr) {
    for (int i = 0; i < count; ++i) {
      SetCodeFunction(rstl::string_l(functions[i].mName), functions[i].mFunction);
    }
  }
}

template < class T >
void CGenericFSM2State< T >::SetState(CStateManager& mgr, T& owner, const rstl::string& name) {
  const int index = GetStateIndex(name);
  if (mMachine != nullptr && index >= 0 && index < mMachine->GetStates().size()) {
    const CState2State* state = &mMachine->GetStates()[index];
    if (mState != state) {
      ExitActive(mgr, owner);
      ExecuteState(*state, mgr, owner, kStateMsg_Activate, 0.f);
    }
  }
}

template < class T >
void CGenericFSM2State< T >::Update(CStateManager& mgr, T& owner, float dt) {
  if (mState != nullptr) {
    mTime += dt;
    const CState2SubMachine* subMachine = GetActiveSubMachine();
    if (subMachine != nullptr) {
      ExecuteSubflow(*subMachine, rstl::string_l(""), mgr, owner, kStateMsg_Update, dt);
      EvaluateTransitions(mgr, owner, dt, subMachine->GetTransitions());
    } else {
      const CState2State* state = GetActiveState();
      if (state != nullptr) {
        ExecuteState(*state, mgr, owner, kStateMsg_Update, dt);
        EvaluateTransitions(mgr, owner, dt, state->GetTransitions());
      }
    }
  }
}

template < class T >
bool CGenericFSM2State< T >::HasState() const {
  return mState != nullptr;
}

template < class T >
const char* CGenericFSM2State< T >::GetName() const {
  const CState2SubMachine* subMachine = GetActiveSubMachine();
  if (subMachine != nullptr) {
    return mSubMachines[GetSubMachineIndex(subMachine)].mMachine->GetName();
  }
  const CState2State* state = GetActiveState();
  return state != nullptr ? state->GetName().data() : nullptr;
}

template < class T >
float CGenericFSM2State< T >::GetTime() const {
  const CState2SubMachine* subMachine = GetActiveSubMachine();
  if (subMachine != nullptr) {
    return mSubMachines[GetSubMachineIndex(subMachine)].mMachine->GetTime();
  }
  return mTime;
}

template < class T >
float CGenericFSM2State< T >::GetDelay() const {
  return mDelay;
}

template < class T >
void CGenericFSM2State< T >::SetDelay(float delay) {
  mDelay = delay;
}

template < class T >
void CGenericFSM2State< T >::Setup(const CGenericFSM2& machine) {
  if (mMachine == nullptr) {
    mMachine = &machine;
    mStateFunctions.resize(machine.GetStates().size(), StateFunc());
    mTriggerFunctions.resize(machine.GetTriggers().size(), TriggerFunc());
    mCodeFunctions.resize(machine.GetCodes().size(), CodeFunc());
    mSubMachines.reserve(machine.GetSubMachines().size());
    for (int i = 0; i < machine.GetSubMachines().size(); ++i) {
      const CState2SubMachine& subMachine = machine.GetSubMachines()[i];
      rstl::rc_ptr< CGenericFSM2State< T > > subState(rs_new CGenericFSM2State< T >);
      subState->Setup(*subMachine.GetMachine());
      mSubMachines.push_back_unsafe(SSubMachine(&subMachine, subState));
    }
  }
}

template < class T >
int CGenericFSM2State< T >::GetStateIndex(const rstl::string& name) const {
  if (mMachine != nullptr) {
    const rstl::vector< CState2State >& states = mMachine->GetStates();
    for (int i = 0; i < states.size(); ++i) {
      if (states[i].GetName() == name) {
        return i;
      }
    }
  }
  return -1;
}

template < class T >
int CGenericFSM2State< T >::GetTriggerIndex(const rstl::string& name) const {
  if (mMachine != nullptr) {
    const rstl::vector< CState2Trigger >& triggers = mMachine->GetTriggers();
    for (int i = 0; i < triggers.size(); ++i) {
      if (triggers[i].GetName() == name) {
        return i;
      }
    }
  }
  return -1;
}

template < class T >
int CGenericFSM2State< T >::GetCodeIndex(const rstl::string& name) const {
  if (mMachine != nullptr) {
    const rstl::vector< CState2Code >& codes = mMachine->GetCodes();
    for (int i = 0; i < codes.size(); ++i) {
      if (codes[i].GetName() == name) {
        return i;
      }
    }
  }
  return -1;
}

template < class T >
int CGenericFSM2State< T >::GetSubMachineIndex(const CState2* state) const {
  for (int i = 0; i < mSubMachines.size(); ++i) {
    if (mSubMachines[i].mState == state) {
      return i;
    }
  }
  return -1;
}

template < class T >
void CGenericFSM2State< T >::SetStateFunction(const rstl::string& name, StateFunc function) {
  if (mMachine != nullptr) {
    const rstl::vector< CState2State >& states = mMachine->GetStates();
    for (int i = 0; i < states.size(); ++i) {
      if (states[i].GetName() == name) {
        mStateFunctions[i] = function;
      }
    }
  }
}

template < class T >
void CGenericFSM2State< T >::SetTriggerFunction(const rstl::string& name, TriggerFunc function) {
  if (mMachine != nullptr) {
    const rstl::vector< CState2Trigger >& triggers = mMachine->GetTriggers();
    for (int i = 0; i < triggers.size(); ++i) {
      if (triggers[i].GetName() == name) {
        mTriggerFunctions[i] = function;
      }
    }
  }
}

template < class T >
void CGenericFSM2State< T >::SetCodeFunction(const rstl::string& name, CodeFunc function) {
  if (mMachine != nullptr) {
    const rstl::vector< CState2Code >& codes = mMachine->GetCodes();
    for (int i = 0; i < codes.size(); ++i) {
      if (codes[i].GetName() == name) {
        mCodeFunctions[i] = function;
      }
    }
  }
}

template < class T >
void CGenericFSM2State< T >::CallCode(const CState2Code& state, CStateManager& mgr, T& owner,
                                      float dt) {
  const int index = GetCodeIndex(state.GetName());
  if (mMachine != nullptr && index >= 0 && index < mMachine->GetCodes().size()) {
    CodeFunc function = mCodeFunctions[index];
    if (function != nullptr) {
      (owner.*function)(mgr, dt);
    }
  }
}

template < class T >
bool CGenericFSM2State< T >::CallTrigger(const CState2Trigger& state, CStateManager& mgr, T& owner,
                                         float dt) {
  const int index = GetTriggerIndex(state.GetName());
  if (mMachine != nullptr && index >= 0 && index < mMachine->GetTriggers().size()) {
    TriggerFunc function = mTriggerFunctions[index];
    if (function != nullptr) {
      if (state.IsNegated()) {
        return !(owner.*function)(mgr, state.GetArgument());
      }
      return (owner.*function)(mgr, state.GetArgument());
    }
  }
  return false;
}

template < class T >
void CGenericFSM2State< T >::ExecuteSubflow(const CState2SubMachine& state,
                                            const rstl::string& name, CStateManager& mgr, T& owner,
                                            EStateMsg msg, float dt) {
  const int index = GetSubMachineIndex(&state);
  if (mMachine != nullptr && index != -1) {
    const rstl::rc_ptr< CGenericFSM2State< T > >& subMachine = mSubMachines[index].mMachine;
    switch (msg) {
    case kStateMsg_Activate:
      mState = &state;
      mTime = 0.f;
      subMachine->SetState(mgr, owner, name);
      break;
    case kStateMsg_Update:
      subMachine->Update(mgr, owner, dt);
      break;
    case kStateMsg_Deactivate:
      subMachine->ExitActive(mgr, owner);
      break;
    }
  }
}

template < class T >
void CGenericFSM2State< T >::ExecuteState(const CState2State& state, CStateManager& mgr, T& owner,
                                          EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mState = &state;
    mTime = 0.f;
  }
  const int index = GetStateIndex(state.GetName());
  if (mMachine != nullptr && index >= 0 && index < mMachine->GetStates().size()) {
    StateFunc function = mStateFunctions[index];
    if (function != nullptr) {
      (owner.*function)(mgr, msg, dt);
    }
  }
}

template < class T >
const CState2SubMachine* CGenericFSM2State< T >::GetActiveSubMachine() const {
  if (mState != nullptr && mState->GetType() == CState2::kType_SubMachine) {
    return static_cast< const CState2SubMachine* >(mState);
  }
  return nullptr;
}

template < class T >
const CState2State* CGenericFSM2State< T >::GetActiveState() const {
  if (mState != nullptr && mState->GetType() == CState2::kType_State) {
    return static_cast< const CState2State* >(mState);
  }
  return nullptr;
}

template < class T >
bool CGenericFSM2State< T >::EvaluateTransitions(
    CStateManager& mgr, T& owner, float dt,
    const rstl::vector< SStateMachine2Transition >& transitions) {
  for (int i = 0; i < transitions.size(); ++i) {
    const SStateMachine2Transition& transition = transitions[i];
    const CState2* target = transition.mTarget;
    switch (target->GetType()) {
    case CState2::kType_State:
      ExitActive(mgr, owner);
      ExecuteState(*static_cast< const CState2State* >(target), mgr, owner, kStateMsg_Activate,
                   0.f);
      return true;
    case CState2::kType_Trigger: {
      const CState2Trigger* trigger = static_cast< const CState2Trigger* >(target);
      if (CallTrigger(*trigger, mgr, owner, dt) &&
          EvaluateTransitions(mgr, owner, dt, trigger->GetTransitions())) {
        return true;
      }
      break;
    }
    case CState2::kType_Code: {
      const CState2Code* code = static_cast< const CState2Code* >(target);
      CallCode(*code, mgr, owner, dt);
      if (EvaluateTransitions(mgr, owner, dt, code->GetTransitions())) {
        return true;
      }
      break;
    }
    case CState2::kType_SubMachine:
      ExitActive(mgr, owner);
      ExecuteSubflow(*static_cast< const CState2SubMachine* >(target), transition.mName, mgr, owner,
                     kStateMsg_Activate, 0.f);
      return true;
    }
  }
  return false;
}

template < class T >
void CGenericFSM2State< T >::ExitActive(CStateManager& mgr, T& owner) {
  const CState2State* state = GetActiveState();
  if (state != nullptr) {
    ExecuteState(*state, mgr, owner, kStateMsg_Deactivate, 0.f);
  }
  const CState2SubMachine* subMachine = GetActiveSubMachine();
  if (subMachine != nullptr) {
    ExecuteSubflow(*subMachine, rstl::string_l(""), mgr, owner, kStateMsg_Deactivate, 0.f);
  }
  mState = nullptr;
}

#ifdef TARGET_PC
template < class T >
void CGenericFSM2State< T >::PortCollectStateNames(rstl::vector< rstl::string >& out,
                                                   int maxDepth) const {
  if (mMachine == nullptr) {  // the asset is missing until the machine is set up
    return;
  }
  for (const CState2State& state : mMachine->GetStates()) {
    out.push_back(state.GetName());
  }
  // The owner's own table (the port registers the module's states under the retail's names).
  for (int i = 0; i < mPortStateFunctionCount; ++i) {
    out.push_back(rstl::string(mPortStateFunctions[i].mName));
  }
  if (maxDepth <= 0) {
    return;
  }
  for (int i = 0; i < mSubMachines.size(); ++i) {
    CGenericFSM2State< T >* sub = mSubMachines[i].mMachine.GetPtr();
    if (sub != nullptr) {  // a sub-machine without a machine has no states
      sub->PortCollectStateNames(out, maxDepth - 1);
    }
  }
}

template < class T >
int CGenericFSM2State< T >::PortFindStateDepth(const rstl::string& name, int maxDepth) const {
  if (mMachine == nullptr) {
    return -1;
  }
  if (GetStateIndex(name) != -1) {
    return 0;
  }
  if (maxDepth <= 0) {
    return -1;
  }
  for (int i = 0; i < mSubMachines.size(); ++i) {
    const int depth = mSubMachines[i].mMachine->PortFindStateDepth(name, maxDepth - 1);
    if (depth >= 0) {
      return depth + 1;
    }
  }
  return -1;
}

template < class T >
int CGenericFSM2State< T >::PortSetStateDeep(CStateManager& mgr, T& owner,
                                             const rstl::string& name, const char** path,
                                             int maxDepth, const char* const* want,
                                             int wantCount) {
  if (mMachine == nullptr) {
    return -1;
  }
  if (wantCount <= 0 && GetStateIndex(name) != -1) {
    SetState(mgr, owner, name);
    return 0;
  }
  if (maxDepth <= 0) {
    return -1;
  }
  for (int i = 0; i < mSubMachines.size(); ++i) {
    CGenericFSM2State< T >* sub = mSubMachines[i].mMachine.GetPtr();
    if (sub->PortFindStateDepth(name, maxDepth - 1) < 0) {
      continue;
    }
    if (wantCount > 0) {
      const CState2* n = mSubMachines[i].mState;
      const char* have = n != nullptr ? static_cast< const CState2SubMachine* >(n)->GetName().data() : "";
      const char* w = want[0];
      while (*have != '\0' && *have == *w) {
        ++have;
        ++w;
      }
      if (*have != *w) {
        continue;
      }
    }
    // ExecuteSubflow's activate, minus its one-level SetState: stay in the sub-machine when it
    // is already the active node, else leave the active node and make it active.
    const CState2* node = mSubMachines[i].mState;
    if (mState != node) {
      ExitActive(mgr, owner);
      mState = node;
      mTime = 0.f;
    }
    path[0] = static_cast< const CState2SubMachine* >(node)->GetName().data();
    const int depth = sub->PortSetStateDeep(mgr, owner, name, path + 1, maxDepth - 1,
                                                want != nullptr ? want + 1 : nullptr,
                                                wantCount > 0 ? wantCount - 1 : 0);
    return depth >= 0 ? depth + 1 : -1;
  }
  return -1;
}

template < class T >
typename CGenericFSM2State< T >::StateFunc
CGenericFSM2State< T >::PortFindStateFunction(const char* name) const {
  for (int i = 0; i < mPortStateFunctionCount; ++i) {
    const char* a = mPortStateFunctions[i].mName;
    const char* b = name;
    while (*a != '\0' && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == *b) {
      return mPortStateFunctions[i].mFunction;
    }
  }
  return StateFunc();
}

template < class T >
typename CGenericFSM2State< T >::TriggerFunc
CGenericFSM2State< T >::PortFindTriggerFunction(const char* name) const {
  for (int i = 0; i < mPortTriggerFunctionCount; ++i) {
    const char* a = mPortTriggerFunctions[i].mName;
    const char* b = name;
    while (*a != '\0' && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == *b) {
      return mPortTriggerFunctions[i].mFunction;
    }
  }
  return TriggerFunc();
}

template < class T >
typename CGenericFSM2State< T >::CodeFunc
CGenericFSM2State< T >::PortFindCodeFunction(const char* name) const {
  for (int i = 0; i < mPortCodeFunctionCount; ++i) {
    const char* a = mPortCodeFunctions[i].mName;
    const char* b = name;
    while (*a != '\0' && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == *b) {
      return mPortCodeFunctions[i].mFunction;
    }
  }
  return CodeFunc();
}
#endif

#endif // _CGENERICFSM2STATEIMPL
