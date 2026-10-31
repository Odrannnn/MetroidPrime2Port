#include "port_savestate.h"

#include "port_entity_schema.h"

#include "port_savestate_block.h"

#include "Kyoto/Animation/CAnimTreeNode.hpp"
#include "Kyoto/Animation/IAnimReader.hpp"
#include "MetroidPrime/BodyState/CBSLocomotion.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/BodyState/CBodyState.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/BodyState/CBodyStateInfo.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CAnimPlaybackParms.hpp"
#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/CGenericFSM2State.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Enemies/CStateMachine.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/TStateMachineState.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDebris.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPickup.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

// The kTagFields block: an enemy's own fields, read and written through the generated schema
// (port_entity_schema.h) instead of per-class code. See SavedField for what is kept.

// The codecs for what the schema can't copy as plain values. A friend of the classes it reads, so
// the private members are reachable from here. Each Put writes one payload; each Get validates all
// of it before it touches the entity and returns false if the entity can't take it.

// The version of a kTagFields block, written at its head. 1: engine references were saved in their
// raw POD bytes, trusted as they were. 2: those references are stable identities (PutUid/GetUid).
// 3: a body-state command's TUniqueId members are too. A reader takes any policy from 1 to this
// one, so a block an older port wrote still reads (each rule asks for the level it needs).
constexpr uint32_t kFieldPolicy = 3;
constexpr uint32_t kPolicyBodyCmdUids = 3;

constexpr uint32_t kUidNone = 0xFFFFFFFFu;
constexpr uint32_t kUidDynamic = 0xFFFFFFFEu;
constexpr uint32_t kUidPlayer = 0xFFFFFFFDu;
constexpr uint32_t kUidSelf = 0xFFFFFFFCu;

uint32_t PutUid(const CStateManager& mgr, const CEntity& owner, TUniqueId uid) {
  if (uid == kInvalidUniqueId) {
    return kUidNone;
  }
  if (uid == owner.GetUniqueId()) {
    return kUidSelf; // Also works for generated copies without a unique editor id.
  }
  CEntity* target = const_cast< CStateManager& >(mgr).ObjectById(uid);
  const TEditorId eid = mgr.GetEditorIdForUniqueId(uid);
  if (target != nullptr && target == mgr.Player(0)) {
    return kUidPlayer;
  }
  if (target != nullptr && eid.Value() != kInvalidEditorId.Value() &&
      !PortSaveState::IsScriptGeneratedCopy(mgr, *target)) {
    return eid.Value();
  }
  return kUidDynamic;
}

TUniqueId GetUid(CStateManager& mgr, CEntity& owner, uint32_t id, TUniqueId live) {
  if (id == kUidSelf) {
    return owner.GetUniqueId();
  }
  if (id == kUidPlayer) {
    return mgr.Player(0) != nullptr ? mgr.Player(0)->GetUniqueId() : kInvalidUniqueId;
  }
  if (id == kUidNone) {
    return kInvalidUniqueId;
  }
  if (id == kUidDynamic) {
    // Unsaved effects/collision actors belong to the rebuilt object. Keep only its live
    // reference, never the saved slot (which may now be a completely different class).
    return mgr.ObjectById(live) != nullptr ? live : kInvalidUniqueId;
  }
  const TUniqueId uid = mgr.GetIdForScript(TEditorId(id));
  return mgr.ObjectById(uid) != nullptr ? uid : kInvalidUniqueId;
}

struct PortStateCodec {
  using BlockWriter = PortSaveState::BlockWriter;
  using BlockReader = PortSaveState::BlockReader;

  static bool Ok(float f) { return f == f && f > -1e9f && f < 1e9f; }

  static void PutStr(BlockWriter& w, const char* s) { w.Bytes(std::string(s != nullptr ? s : "")); }

  // ---- state machines ----
  static TStateMachineStateBase< CPatterned >* Machine(CEntity& ent, const PortSchemaField& f) {
    void* p = f.addr(ent);
    if (std::strstr(f.type, "CGenericFSM2State") != nullptr && std::strstr(f.type, "single_ptr") == nullptr) {
      return static_cast< CGenericFSM2State< CPatterned >* >(p);
    }
    return static_cast< rstl::single_ptr< TStateMachineStateBase< CPatterned > >* >(p)->get();
  }

  // The active path of a CGenericFSM2State, top first: every level keeps its own clock, and
  // GetTime reads the deepest one.
  static void FsmLevels(CGenericFSM2State< CPatterned >* m, std::vector< CGenericFSM2State< CPatterned >* >& out) {
    for (int depth = 0; m != nullptr && depth < 8; ++depth) {
      out.push_back(m);
      const CState2SubMachine* sub = m->GetActiveSubMachine();
      const int i = sub != nullptr ? m->GetSubMachineIndex(sub) : -1;
      m = i >= 0 && i < m->mSubMachines.size() ? m->mSubMachines[i].mMachine.GetPtr() : nullptr;
    }
  }

  static void PutMachine(CEntity& ent, const PortSchemaField& f, BlockWriter& w) {
    TStateMachineStateBase< CPatterned >* m = Machine(ent, f);
    if (m == nullptr) {
      return;
    }
    auto* ts = dynamic_cast< TStateMachineState< CPatterned >* >(m);
    auto* fsm = dynamic_cast< CGenericFSM2State< CPatterned >* >(m);
    if (ts == nullptr && fsm == nullptr) {
      return;
    }
    const bool has = m->HasState() && m->GetName() != nullptr;
    w.U8(ts != nullptr ? 0 : 1);
    PutStr(w, has ? m->GetName() : "");
    w.F32(m->GetTime());
    w.F32(m->GetDelay());
    if (ts != nullptr) {
      int index = -1;
      if (ts->mMachine != nullptr && ts->mState != nullptr) {
        const rstl::vector< CState >& v = ts->mMachine->GetStateVector();
        for (int i = 0; i < v.size(); ++i) {
          if (&v[i] == ts->mState) {
            index = i;
          }
        }
      }
      w.U32(static_cast< uint32_t >(index));
      w.F32(ts->mRandom);
      w.F32(ts->mFixedRandom);
      w.U8(ts->mCodeTrigger ? 1 : 0);
    } else {
      std::vector< CGenericFSM2State< CPatterned >* > levels;
      FsmLevels(fsm, levels);
      w.U8(static_cast< uint8_t >(levels.size()));
      for (CGenericFSM2State< CPatterned >* l : levels) {
        w.F32(l->mTime);
        w.F32(l->mDelay);
      }
    }
  }

  // Why the last GetMachine returned false (GetCodec logs it).
  static inline const char* sWhy = "";
  static bool Fail(const char* why) {
    sWhy = why;
    return false;
  }

  static bool GetMachine(CStateManager& mgr, CEntity& ent, const PortSchemaField& f, const std::string& data) {
    CPatterned* owner = dynamic_cast< CPatterned* >(&ent);
    TStateMachineStateBase< CPatterned >* m = Machine(ent, f);
    if (owner == nullptr || m == nullptr) {
      return Fail("no patterned owner or no machine field");
    }
    BlockReader r(data);
    const uint32_t kind = r.U8();
    const std::string name = r.Bytes();
    const float time = r.F32();
    const float delay = r.F32();
    int index = -1;
    float random = 0.f, fixedRandom = 0.f;
    bool trigger = false;
    if (kind == 0) {
      index = static_cast< int >(r.U32());
      random = r.F32();
      fixedRandom = r.F32();
      trigger = r.U8() != 0;
    }
    // Per-level clocks of an FSM2 path (absent in states saved before they were added).
    std::vector< float > clocks;
    if (kind == 1 && r.ok && r.at < data.size()) {
      const uint32_t n = r.U8();
      for (uint32_t i = 0; i < n * 2 && r.ok; ++i) {
        clocks.push_back(r.F32());
      }
    }
    // A machine is set up lazily (first Think); a freshly loaded actor that has not run yet has
    // no definition to enter a state in.
    // A machine saved without a state never ran (inactive): leave it to start on its own.
    if (name.empty()) {
      return true;
    }
    if (!m->HasState()) {
      owner->InitializeStateMachine(mgr);
    }
    if (!r.ok || kind > 1 || !Ok(time) || !Ok(delay) || !Ok(random) || !Ok(fixedRandom)) {
      return Fail("payload malformed or a non-finite value");
    }
    for (float c : clocks) {
      if (!Ok(c)) {
        return Fail("non-finite sub-machine clock");
      }
    }
    if (kind == 0) {
      auto* ts = dynamic_cast< TStateMachineState< CPatterned >* >(m);
      if (ts == nullptr || name.empty()) {
        return Fail("FSM1 field is not an FSM1 state or the name is empty");
      }
      if (ts->mMachine == nullptr) {
        CStateMachine* sm = owner->GetStateMachine();
        if (sm == nullptr) {
          return Fail("owner has no CStateMachine");
        }
        ts->Setup(sm);
      }
      const rstl::vector< CState >& v = ts->mMachine->GetStateVector();
      int idx = -1;
      if (index >= 0 && index < v.size() && name == v[index].GetName()) {
        idx = index;
      }
      for (int i = 0; idx < 0 && i < v.size(); ++i) {
        if (name == v[i].GetName()) {
          idx = i;
        }
      }
      if (idx < 0) {
        return Fail("name not in the state vector");
      }
      if (ts->mState != &v[idx]) {
        ts->SetState(mgr, *owner, idx);
      }
      if (ts->mState != &v[idx]) {
        return Fail("SetState did not reach the state");
      }
      ts->mTime = time;
      ts->mRandom = random;
      ts->mDelay = delay;
      ts->mFixedRandom = fixedRandom;
      ts->mCodeTrigger = trigger;
      return true;
    }
    auto* fsm = dynamic_cast< CGenericFSM2State< CPatterned >* >(m);
    if (fsm == nullptr || name.empty()) {
      return Fail("FSM2 field is not an FSM2 state or the name is empty");
    }
    const char* now = fsm->HasState() ? fsm->GetName() : nullptr;
    if (now == nullptr || name != now) {
      const char* path[8] = {};
      if (fsm->PortSetStateDeep(mgr, *owner, rstl::string_l(name.c_str()), path, 8) < 0) {
        return Fail("PortSetStateDeep found no path to the name");
      }
    }
    std::vector< CGenericFSM2State< CPatterned >* > levels;
    FsmLevels(fsm, levels);
    if (!clocks.empty() && clocks.size() == levels.size() * 2) {
      for (size_t i = 0; i < levels.size(); ++i) {
        levels[i]->mTime = clocks[i * 2];
        levels[i]->mDelay = clocks[i * 2 + 1];
      }
    } else {
      fsm->mTime = time;
      fsm->SetDelay(delay);
    }
    return true;
  }

  // ---- body controller ----
  template < class F >
  static void ForEachCmd(CBodyStateCmdMgr& m, F&& f) {
    f(m.mGetup);
    f(m.mStep);
    f(m.mDie);
    f(m.mKnockDown);
    f(m.mKnockBack);
    f(m.mMeleeAttack);
    f(m.mProjectileAttack);
    f(m.mLoopAttack);
    f(m.mLoopReaction);
    f(m.mLoopHitReaction);
    f(m.mExitState);
    f(m.mLeanFromCover);
    f(m.mNextState);
    f(m.mAbortScripted);
    f(m.mMaintainVelocity);
    f(m.mGenerate);
    f(m.mHurled);
    f(m.mJump);
    f(m.x280_);
    f(m.x28c_);
    f(m.mSlide);
    f(m.mTaunt);
    f(m.mScripted);
    f(m.mCover);
    f(m.mWallHang);
    f(m.mLocomotion);
    f(m.mAdditiveIdle);
    f(m.mAdditiveAim);
    f(m.mAdditiveFlinch);
    f(m.mAdditiveReaction);
    f(m.mStopReaction);
    f(m.mAdditiveLoopReaction);
    f(m.mAdditiveWeight);
    f(m.x360_);
  }

  // The TUniqueId members of one body-state command. CBCWallHangCmd::mWpId - the waypoint a wall
  // hang clings to (CBodyStateCmdMgr.hpp:401-410) - is the only id any command in that header
  // carries. A room rebuild hands its slot to another object, so from policy 3 the id is written by
  // identity (PutUid) and read back with GetUid, like the entity fields' references. A new command
  // with an id needs a line here (and the friend in the header), or its id is a saved slot again.
  static std::vector< TUniqueId* > CmdUids(CBCWallHangCmd& c) { return {&c.mWpId}; }
  template < typename T >
  static std::vector< TUniqueId* > CmdUids(T&) {
    return {};
  }

  static bool ValidState(CBodyStateInfo& info, int s) {
    return s >= 0 && s < info.mStates.size() && info.mStates[s] != nullptr;
  }

  static void PutBody(const CStateManager& mgr, CEntity& ent, const PortSchemaField& f, BlockWriter& w,
                      uint32_t policy) {
    CBodyController* bc = static_cast< rstl::single_ptr< CBodyController >* >(f.addr(ent))->get();
    if (bc == nullptr) {
      return;
    }
    CBodyStateInfo& info = bc->mBodyStateInfo;
    w.U32(static_cast< uint32_t >(info.mState));
    w.U32(static_cast< uint32_t >(info.mAdditiveState));
    w.U32(static_cast< uint32_t >(bc->mLocomotionType));
    w.U32(static_cast< uint32_t >(bc->mFallState));
    w.U32(static_cast< uint32_t >(bc->mCurAnim));
    w.F32(bc->mTurnSpeed);
    w.U8((bc->mAnimationOver ? 1 : 0) | (bc->mActive ? 2 : 0) | (bc->mFrozen ? 4 : 0) |
         (bc->mHasBeenFrozen ? 8 : 0) | (bc->mPlayDeathAnims ? 16 : 0));
    const float fl[] = {bc->mIntoFreezeDur, bc->mFrozenDur, bc->mBreakoutDur, bc->mTimeFrozen,
                        bc->mBackedUpForce.GetX(), bc->mBackedUpForce.GetY(), bc->mBackedUpForce.GetZ(),
                        bc->mFireDur, bc->mElectrocutionDur, bc->mTimeOnFire, bc->mTimeElectrocuting,
                        bc->mRestrictedFlyerMoveSpeed, info.mMaxPitch};
    for (float v : fl) {
      w.F32(v);
    }
    w.U8(info.mChangeLocoAtEndOfAnimOnly ? 1 : 0);
    CBodyStateCmdMgr& cm = bc->mCmdMgr;
    int n = 0;
    ForEachCmd(cm, [&](auto&) { ++n; });
    w.U8(static_cast< uint8_t >(n));
    ForEachCmd(cm, [&](auto& c) {
      using T = std::remove_reference_t< decltype(c) >;
      const char* payload = reinterpret_cast< const char* >(&c) + sizeof(void*);
      std::string bytes(payload, sizeof(T) - sizeof(void*));
      if (policy >= kPolicyBodyCmdUids) {
        for (TUniqueId* id : CmdUids(c)) {
          const uint32_t word = PutUid(mgr, ent, *id);
          std::memcpy(bytes.data() + (reinterpret_cast< const char* >(id) - payload), &word, sizeof word);
        }
      }
      w.U32(static_cast< uint32_t >(bytes.size()));
      w.bytes += bytes;
    });
    for (const CVector3f* v : {&cm.mMove, &cm.mFace, &cm.mTarget, &cm.mAdditiveTarget}) {
      w.Vec(*v);
    }
    w.U32(static_cast< uint32_t >(cm.mSteeringMode));
    w.F32(cm.mSteeringSpeedMin);
    w.F32(cm.mSteeringSpeedMax);
    w.F32(cm.mSteeringSpeed);
    w.U8(static_cast< uint8_t >(cm.mDeliveredCommands.size()));
    for (int i = 0; i < cm.mDeliveredCommands.size(); ++i) {
      w.U8(cm.mDeliveredCommands[i] ? 1 : 0);
    }
    CBodyState* cur = ValidState(info, info.mState) ? info.mStates[info.mState] : nullptr;
    auto* bi = dynamic_cast< CBSBiPedLocomotion* >(cur);
    auto* rs = dynamic_cast< CBSRestrictedLocomotion* >(cur);
    w.U8(bi != nullptr ? 1 : rs != nullptr ? 2 : 0);
    if (bi != nullptr) {
      w.U32(static_cast< uint32_t >(bi->mAnim));
      w.F32(bi->mPrimeTime);
    } else if (rs != nullptr) {
      w.U32(static_cast< uint32_t >(rs->mAnim));
      w.F32(0.f);
    }
  }

  static void AllDelivered(CBodyStateCmdMgr& cm, std::vector< char >& saved, bool on) {
    if (on) {
      saved.clear();
      for (int i = 0; i < cm.mDeliveredCommands.size(); ++i) {
        saved.push_back(cm.mDeliveredCommands[i] ? 1 : 0);
        cm.mDeliveredCommands[i] = true;
      }
    } else {
      for (int i = 0; i < cm.mDeliveredCommands.size() && i < static_cast< int >(saved.size()); ++i) {
        cm.mDeliveredCommands[i] = saved[i] != 0;
      }
    }
  }

  static bool GetBody(CStateManager& mgr, CEntity& ent, const PortSchemaField& f, const std::string& data,
                      uint32_t policy) {
    CBodyController* bc = static_cast< rstl::single_ptr< CBodyController >* >(f.addr(ent))->get();
    if (bc == nullptr) {
      return false;
    }
    BlockReader r(data);
    CBodyStateInfo& info = bc->mBodyStateInfo;
    CBodyStateCmdMgr& cm = bc->mCmdMgr;
    const int state = static_cast< int >(r.U32());
    const int additive = static_cast< int >(r.U32());
    const int loco = static_cast< int >(r.U32());
    const int fall = static_cast< int >(r.U32());
    const int curAnim = static_cast< int >(r.U32());
    const float turn = r.F32();
    const uint32_t bits = r.U8();
    float fl[13];
    for (float& v : fl) {
      v = r.F32();
    }
    const bool changeLoco = r.U8() != 0;
    const uint32_t cmdCount = r.U8();
    std::vector< std::string > cmds;
    for (uint32_t i = 0; i < cmdCount && r.ok; ++i) {
      cmds.push_back(r.Bytes());
    }
    float steer[12];
    for (float& v : steer) {
      v = r.F32();
    }
    const int mode = static_cast< int >(r.U32());
    const float spd[3] = {r.F32(), r.F32(), r.F32()};
    const uint32_t delN = r.U8();
    std::vector< char > delivered;
    for (uint32_t i = 0; i < delN && r.ok; ++i) {
      delivered.push_back(static_cast< char >(r.U8()));
    }
    const uint32_t locoKind = r.U8();
    int locoAnim = -1;
    float prime = 0.f;
    if (locoKind != 0) {
      locoAnim = static_cast< int >(r.U32());
      prime = r.F32();
    }
    if (!r.ok || locoKind > 2 || !Ok(turn) || !Ok(spd[0]) || !Ok(spd[1]) || !Ok(spd[2]) || prime != prime ||
        mode < 0 || mode > kSBM_Clamped) {
      return false;
    }
    for (float v : fl) {
      if (!Ok(v)) {
        return false;
      }
    }
    for (float v : steer) {
      if (!Ok(v)) {
        return false;
      }
    }
    // Commands apply all or none.
    bool fits = static_cast< int >(cmds.size()) == [&] {
      int n = 0;
      ForEachCmd(cm, [&](auto&) { ++n; });
      return n;
    }() && static_cast< int >(delivered.size()) == cm.mDeliveredCommands.size();
    if (fits) {
      size_t i = 0;
      ForEachCmd(cm, [&](auto& c) {
        using T = std::remove_reference_t< decltype(c) >;
        if (cmds[i++].size() != sizeof(T) - sizeof(void*)) {
          fits = false;
        }
      });
    }

    // Commands go in before a state is started (CBSScripted::Start reads its anim from the
    // scripted command) and again after, as Start may consume them.
    auto applyCmds = [&] {
      size_t i = 0;
      ForEachCmd(cm, [&](auto& c) {
        using T = std::remove_reference_t< decltype(c) >;
        char* payload = reinterpret_cast< char* >(&c) + sizeof(void*);
        std::memcpy(payload, cmds[i].data(), sizeof(T) - sizeof(void*));
        if (policy >= kPolicyBodyCmdUids) {
          // The id the save carried is an identity (PutUid), not the slot it held: resolve it in
          // this rebuild's world, keeping the command's own id only when the save had none.
          for (TUniqueId* id : CmdUids(c)) {
            uint32_t word = kUidNone;
            std::memcpy(&word, cmds[i].data() + (reinterpret_cast< char* >(id) - payload), sizeof word);
            *id = GetUid(mgr, ent, word, *id);
          }
        }
        ++i;
      });
    };
    std::vector< char > keep;
    if (!bc->mActive && (bits & 2) != 0) {
      AllDelivered(cm, keep, true);
      bc->Activate(mgr, pas::kAS_Invalid);
      AllDelivered(cm, keep, false);
    }
    if (bc->mActive && (bits & 2) != 0) {
      if (fits) {
        applyCmds();
      }
      if (fits && static_cast< int >(info.mState) != state && ValidState(info, state)) {
        if (ValidState(info, info.mState)) {
          info.mStates[info.mState]->Shutdown(*bc);
        }
        info.SetState(static_cast< pas::EAnimationState >(state));
        AllDelivered(cm, keep, true);
        info.mStates[state]->Start(*bc, mgr);
        AllDelivered(cm, keep, false);
      }
      if (fits && static_cast< int >(info.mAdditiveState) != additive && ValidState(info, additive)) {
        if (ValidState(info, info.mAdditiveState)) {
          info.mStates[info.mAdditiveState]->Shutdown(*bc);
        }
        info.SetAdditiveState(static_cast< pas::EAnimationState >(additive));
        AllDelivered(cm, keep, true);
        info.mStates[additive]->Start(*bc, mgr);
        AllDelivered(cm, keep, false);
      }
    }
    if (fits) {
      applyCmds();
      cm.mMove = CVector3f(steer[0], steer[1], steer[2]);
      cm.mFace = CVector3f(steer[3], steer[4], steer[5]);
      cm.mTarget = CVector3f(steer[6], steer[7], steer[8]);
      cm.mAdditiveTarget = CVector3f(steer[9], steer[10], steer[11]);
      cm.mSteeringMode = static_cast< ESteeringBlendMode >(mode);
      cm.mSteeringSpeedMin = spd[0];
      cm.mSteeringSpeedMax = spd[1];
      cm.mSteeringSpeed = spd[2];
      for (int k = 0; k < cm.mDeliveredCommands.size(); ++k) {
        cm.mDeliveredCommands[k] = delivered[k] != 0;
      }
    }
    bc->mLocomotionType = static_cast< pas::ELocomotionType >(loco);
    bc->mFallState = static_cast< pas::EFallState >(fall);
    bc->mCurAnim = curAnim;
    bc->mTurnSpeed = turn;
    bc->mAnimationOver = (bits & 1) != 0;
    bc->mFrozen = (bits & 4) != 0;
    bc->mHasBeenFrozen = (bits & 8) != 0;
    bc->mPlayDeathAnims = (bits & 16) != 0;
    bc->mIntoFreezeDur = fl[0];
    bc->mFrozenDur = fl[1];
    bc->mBreakoutDur = fl[2];
    bc->mTimeFrozen = fl[3];
    bc->mBackedUpForce = CVector3f(fl[4], fl[5], fl[6]);
    bc->mFireDur = fl[7];
    bc->mElectrocutionDur = fl[8];
    bc->mTimeOnFire = fl[9];
    bc->mTimeElectrocuting = fl[10];
    bc->mRestrictedFlyerMoveSpeed = fl[11];
    info.mMaxPitch = fl[12];
    info.mChangeLocoAtEndOfAnimOnly = changeLoco;
    if (locoKind != 0 && static_cast< int >(info.mState) == state && ValidState(info, state) &&
        (locoAnim == pas::kLA_Invalid || locoAnim <= pas::kLA_StrafeDown)) {
      CBodyState* cur = info.mStates[state];
      if (locoKind == 1) {
        if (auto* bi = dynamic_cast< CBSBiPedLocomotion* >(cur)) {
          bi->mAnim = static_cast< pas::ELocomotionAnim >(locoAnim);
          bi->mPrimeTime = prime;
        }
      } else if (auto* rs = dynamic_cast< CBSRestrictedLocomotion* >(cur)) {
        rs->mAnim = static_cast< pas::ELocomotionAnim >(locoAnim);
      }
    }
    return true;
  }

  // ---- animation ----
  static CAnimData* Anim(CEntity& ent, const PortSchemaField& f) {
    void* p = f.addr(ent);
    CModelData* md = nullptr;
    if (std::strcmp(f.type, "CModelData") == 0) {
      md = static_cast< CModelData* >(p);
    } else if (std::strstr(f.type, "single_ptr") != nullptr) {
      md = static_cast< rstl::single_ptr< CModelData >* >(p)->get();
    } else {
      auto* o = static_cast< rstl::optional_object< CModelData >* >(p);
      md = o->valid() ? o->get_ptr() : nullptr;
    }
    return md != nullptr ? md->AnimationData() : nullptr;
  }

  static void PutAnim(CEntity& ent, const PortSchemaField& f, BlockWriter& w) {
    CAnimData* a = Anim(ent, f);
    if (a == nullptr) {
      return;
    }
    w.U32(static_cast< uint32_t >(static_cast< int >(a->mCurrentAnim)));
    w.U8((a->mLoop ? 1 : 0) | (a->mAnimating ? 2 : 0));
    w.F32(a->mSpeedScale);
    float phase = -1.f;
    if (CAnimTreeNode* root = a->mAnimRoot.GetPtr()) {
      const float dur = root->VGetSteadyStateAnimInfo().GetDuration().GetSeconds();
      if (dur > 0.f) {
        phase = 1.f - root->VGetTimeRemaining().GetSeconds() / dur;
        phase = phase < 0.f ? 0.f : phase > 1.f ? 1.f : phase;
      }
    }
    w.F32(phase);
  }

  static bool GetAnim(CEntity& ent, const PortSchemaField& f, const std::string& data) {
    CAnimData* a = Anim(ent, f);
    if (a == nullptr) {
      return false;
    }
    BlockReader r(data);
    const int anim = static_cast< int >(r.U32());
    const uint32_t bits = r.U8();
    const float speed = r.F32();
    const float phase = r.F32();
    if (!r.ok || !Ok(speed) || phase != phase) {
      return false;
    }
    if (anim >= 0 && anim != a->GetCurrentAnimation()) {
      a->SetAnimation(CAnimPlaybackParms(anim, -1, 1.f, (bits & 2) != 0), false);
    }
    a->mLoop = (bits & 1) != 0;
    a->mAnimating = (bits & 2) != 0;
    a->mSpeedScale = speed;
    if (phase >= 0.f && phase <= 1.f && a->mAnimRoot.GetPtr() != nullptr) {
      a->SetPhase(phase);
    }
    return anim < 0 || a->GetCurrentAnimation() == anim;
  }

  // ---- path search ----
  static void PutPath(CEntity& ent, const PortSchemaField& f, BlockWriter& w) {
    const CPathFindSearch& p = *static_cast< CPathFindSearch* >(f.addr(ent));
    w.U8(static_cast< uint8_t >(p.mWaypoints.size()));
    for (int i = 0; i < p.mWaypoints.size(); ++i) {
      w.Vec(p.mWaypoints[i]);
    }
    w.U32(static_cast< uint32_t >(p.mCurWaypoint));
    w.U32(static_cast< uint32_t >(p.mResult));
  }

  static bool GetPath(CEntity& ent, const PortSchemaField& f, const std::string& data) {
    CPathFindSearch& p = *static_cast< CPathFindSearch* >(f.addr(ent));
    BlockReader r(data);
    const uint32_t n = r.U8();
    CVector3f pts[16];
    for (uint32_t i = 0; i < n && i < 16 && r.ok; ++i) {
      r.Vec(pts[i]);
    }
    const int cur = static_cast< int >(r.U32());
    const int result = static_cast< int >(r.U32());
    if (!r.ok || n > 16 || cur < 0 || (n != 0 && cur >= static_cast< int >(n)) || result < 0 ||
        result > CPathFindSearch::kR_NoPath) {
      return false;
    }
    for (uint32_t i = 0; i < n; ++i) {
      if (!Ok(pts[i].GetX()) || !Ok(pts[i].GetY()) || !Ok(pts[i].GetZ())) {
        return false;
      }
    }
    p.mWaypoints.clear();
    for (uint32_t i = 0; i < n; ++i) {
      p.mWaypoints.push_back(pts[i]);
    }
    p.mCurWaypoint = cur;
    p.mResult = static_cast< CPathFindSearch::EResult >(result);
    return true;
  }
};


namespace PortSaveState {
namespace {

bool Named(const char* name, std::initializer_list< const char* > names) {
  for (const char* n : names) {
    if (std::strcmp(name, n) == 0) {
      return true;
    }
  }
  return false;
}

bool Enabled() {
  static const bool on = [] {
    const char* v = std::getenv("MP_SAVESTATE_FIELDS");
    return v == nullptr || std::strcmp(v, "0") != 0;
  }();
  return on;
}

// The types a codec exists for (the schema has other state machines and models: the player's).
bool CodecTypeKnown(const PortSchemaField& f) {
  switch (f.kind) {
  case PortSchemaKind::state_machine:
    return Named(f.type, {"rstl::single_ptr<TStateMachineStateBase<CPatterned>>", "CGenericFSM2State<CPatterned>"});
  case PortSchemaKind::body_controller:
    return std::strcmp(f.type, "rstl::single_ptr<CBodyController>") == 0;
  case PortSchemaKind::anim_data:
    return Named(f.type, {"CModelData", "rstl::single_ptr<CModelData>", "rstl::optional_object<CModelData>"});
  case PortSchemaKind::path_search:
    return std::strcmp(f.type, "CPathFindSearch") == 0;
  default:
    return true;
  }
}

bool SavedField(const PortSchemaClass& cls, const PortSchemaField& f) {
  if (!CodecTypeKnown(f)) {
    return false;
  }
  if ((f.flags & kPortSchemaConst) != 0) {
    return false;
  }
  if (std::strncmp(f.name, "mPort", 5) == 0 || std::strncmp(f.name, "xPort", 5) == 0) {
    return false;
  }
  switch (f.kind) {
  case PortSchemaKind::bool_:
  case PortSchemaKind::int_:
  case PortSchemaKind::float_:
  case PortSchemaKind::bitfield:
  case PortSchemaKind::math:
  case PortSchemaKind::uid:
  case PortSchemaKind::pod:
  case PortSchemaKind::state_machine:
  case PortSchemaKind::body_controller:
  case PortSchemaKind::anim_data:
  case PortSchemaKind::path_search:
    break;
  default:
    return false;
  }
  if (f.kind != PortSchemaKind::bitfield && f.addr == nullptr) {
    return false;
  }
  if (std::strcmp(cls.name, "CEntity") == 0) {
    return false;
  }
  // Asset streaming is this run's, not the game's.
  if (std::strstr(f.name, "oaded") != nullptr || std::strstr(f.name, "oading") != nullptr) {
    return false;
  }
  if (std::strcmp(cls.name, "CActor") == 0) {
    return Named(f.name, {"mModelData", "mDrawFlags", "mMuted", "mCallTouch", "mRenderParticleDBInside",
                          "mTargetableVisorFlags", "mEnableRender", "mDrawEnabled", "mDoTargetDistanceTest",
                          // Render-sort cache: SetTransform sets it, the next sort clears it.
                          "mNotInSortedLists"});
  }
  if (std::strcmp(cls.name, "CPhysicsActor") == 0) {
    // Motion too, though kTagMotion has velocity and momentum: these are applied after the
    // codecs, and the force/impulse a Think queued for the next physics step would be lost.
    return Named(f.name, {"mMovable", "mAngularEnabled", "mConstantForce", "mAngularMomentum", "mVelocity",
                          "mAngularVelocity", "mMomentum", "mForce", "mImpulse", "mTorque", "mAngularImpulse",
                          "mMoveImpulse", "mMoveAngularImpulse", "mNumTicksStuck"});
  }
  // CAi's health is the kTagHealth block's.
  if (std::strcmp(cls.name, "CAi") == 0) {
    return std::strcmp(f.name, "mHealthInfo") != 0;
  }
  return true;
}

std::vector< TUniqueId* > PodUids(CEntity& ent, const PortSchemaField& f) {
  std::vector< TUniqueId* > refs;
  if (f.uidAt != nullptr) {
    for (unsigned i = 0; TUniqueId* ref = f.uidAt(ent, i); ++i) {
      refs.push_back(ref);
    }
  }
  return refs;
}

bool IsCodec(PortSchemaKind kind) {
  return kind == PortSchemaKind::state_machine || kind == PortSchemaKind::body_controller ||
         kind == PortSchemaKind::anim_data || kind == PortSchemaKind::path_search;
}

// The pass a field is applied on (ApplyEntityFields): the plain fields on 1, the AI and body
// codecs on 0, the animation on 3 (after the body's Start picked one) and the path on 2
// (after the AI state's Activate searched one).
int FieldPass(PortSchemaKind kind) {
  switch (kind) {
  case PortSchemaKind::anim_data:
    return 3;
  case PortSchemaKind::path_search:
    return 2;
  default:
    return IsCodec(kind) ? 0 : 1;
  }
}

std::string PutCodec(const CStateManager& mgr, CEntity& ent, const PortSchemaField& f, uint32_t policy) {
  PortSaveState::BlockWriter w;
  switch (f.kind) {
  case PortSchemaKind::state_machine:
    PortStateCodec::PutMachine(ent, f, w);
    break;
  case PortSchemaKind::body_controller:
    PortStateCodec::PutBody(mgr, ent, f, w, policy);
    break;
  case PortSchemaKind::path_search:
    PortStateCodec::PutPath(ent, f, w);
    break;
  default:
    PortStateCodec::PutAnim(ent, f, w);
    break;
  }
  return std::move(w.bytes);
}

bool GetCodec(CStateManager& mgr, CEntity& ent, const PortSchemaField& f, const std::string& data,
              uint32_t policy) {
  if (data.empty()) {
    return false;
  }
  switch (f.kind) {
  case PortSchemaKind::state_machine: {
    PortStateCodec::sWhy = "";
    if (PortStateCodec::GetMachine(mgr, ent, f, data)) {
      return true;
    }
    PortStateCodec::BlockReader r(data);
    r.U8();
    const std::string name = r.Bytes();
    const PortSchemaClass* cls = PortEntitySchema::For(ent);
    std::fprintf(stderr, "[savestate] machine not restored: %s e%08x field %s state '%s': %s\n",
                 cls != nullptr ? cls->name : "?", mgr.GetEditorIdForUniqueId(ent.GetUniqueId()).Value(), f.name,
                 name.c_str(), PortStateCodec::sWhy);
    return false;
  }
  case PortSchemaKind::body_controller:
    return PortStateCodec::GetBody(mgr, ent, f, data, policy);
  case PortSchemaKind::path_search:
    return PortStateCodec::GetPath(ent, f, data);
  default:
    return PortStateCodec::GetAnim(ent, f, data);
  }
}

// The schema classes of `ent`, most derived first.
std::vector< const PortSchemaClass* > Chain(const CEntity& ent) {
  std::vector< const PortSchemaClass* > chain;
  for (const PortSchemaClass* c = PortEntitySchema::For(ent); c != nullptr && chain.size() < 32;
       c = PortEntitySchema::Base(*c)) {
    chain.push_back(c);
  }
  return chain;
}

void PutVar(BlockWriter& w, long long v) {
  uint64_t z = (static_cast< uint64_t >(v) << 1) ^ static_cast< uint64_t >(v >> 63);
  while (z >= 0x80) {
    w.U8(static_cast< uint8_t >(z | 0x80));
    z >>= 7;
  }
  w.U8(static_cast< uint8_t >(z));
}

long long GetVar(BlockReader& r) {
  uint64_t z = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    const uint8_t b = static_cast< uint8_t >(r.U8());
    if (!r.ok) {
      return 0;
    }
    z |= static_cast< uint64_t >(b & 0x7F) << shift;
    if ((b & 0x80) == 0) {
      return static_cast< long long >((z >> 1) ^ (~(z & 1) + 1));
    }
  }
  r.ok = false;
  return 0;
}

void PutField(const CStateManager& mgr, CEntity& ent, const PortSchemaField& f, BlockWriter& w) {
  if (IsCodec(f.kind)) {
    const std::string payload = PutCodec(mgr, ent, f, kFieldPolicy);
    w.U32(static_cast< uint32_t >(payload.size()));
    w.bytes += payload;
    return;
  }
  switch (f.kind) {
  case PortSchemaKind::bool_:
  case PortSchemaKind::int_:
  case PortSchemaKind::bitfield:
    PutVar(w, f.getInt != nullptr ? f.getInt(ent) : 0);
    return;
  case PortSchemaKind::uid: {
    const TUniqueId uid = *static_cast< const TUniqueId* >(f.addr(ent));
    w.U32(PutUid(mgr, ent, uid));
    return;
  }
  default: {
    // float, math and pod: the bytes, after their size (a pod's differs between compilers
    // when its padding does; a reader skips one it cannot take).
    const auto refs = PodUids(ent, f);
    w.U32(f.size + static_cast< uint32_t >(refs.size()) * 4);
    const char* src = static_cast< const char* >(f.addr(ent));
    std::string bytes(src, f.size);
    for (const TUniqueId* ref : refs) {
      const size_t off = reinterpret_cast< const char* >(ref) - src;
      std::memcpy(bytes.data() + off, &kInvalidUniqueId, sizeof(TUniqueId));
    }
    w.bytes += bytes;
    for (const TUniqueId* ref : refs) {
      w.U32(PutUid(mgr, ent, *ref));
    }
    return;
  }
  }
}

// Reads past one field as PutField wrote it.
void SkipField(const PortSchemaField& f, BlockReader& r) {
  switch (f.kind) {
  case PortSchemaKind::bool_:
  case PortSchemaKind::int_:
  case PortSchemaKind::bitfield:
    GetVar(r);
    return;
  case PortSchemaKind::uid:
    r.U32();
    return;
  default: {
    const uint32_t size = r.U32();
    if (!r.ok || r.in.size() - r.at < size) {
      r.ok = false;
    } else {
      r.at += size;
    }
    return;
  }
  }
}

// Returns whether the field was set; false with r.ok still true is a field skipped.
// Only a field of the given pass is set (FieldPass); the rest are read past.
bool GetField(CStateManager& mgr, CEntity& ent, const PortSchemaField& f, BlockReader& r, int pass,
              uint32_t policy) {
  if (FieldPass(f.kind) != pass) {
    SkipField(f, r);
    return false;
  }
  if (IsCodec(f.kind)) {
    const uint32_t size = r.U32();
    if (!r.ok || r.in.size() - r.at < size) {
      r.ok = false;
      return false;
    }
    const std::string payload = r.in.substr(r.at, size);
    r.at += size;
    return GetCodec(mgr, ent, f, payload, policy);
  }
  switch (f.kind) {
  case PortSchemaKind::bool_:
  case PortSchemaKind::int_:
  case PortSchemaKind::bitfield: {
    const long long v = GetVar(r);
    if (!r.ok || f.setInt == nullptr) {
      return false;
    }
    if (f.kind == PortSchemaKind::bool_ && v != 0 && v != 1) {
      return false;
    }
    f.setInt(ent, v);
    return true;
  }
  case PortSchemaKind::uid: {
    const uint32_t id = r.U32();
    if (!r.ok) {
      return false;
    }
    auto* dst = static_cast< TUniqueId* >(f.addr(ent));
    *dst = GetUid(mgr, ent, id, *dst);
    return true;
  }
  default: {
    const uint32_t size = r.U32();
    if (!r.ok || r.in.size() - r.at < size) {
      r.ok = false;
      return false;
    }
    const size_t from = r.at;
    r.at += size;
    const auto refs = PodUids(ent, f);
    const uint32_t refBytes = policy >= 2 ? static_cast< uint32_t >(refs.size()) * 4 : 0;
    if (size != f.size + refBytes) {
      return false;
    }
    std::vector< unsigned char > buf(r.in.begin() + from, r.in.begin() + from + f.size);
    // A vector or transform that went NaN or huge is not put back.
    for (uint32_t i = 0; f.kind == PortSchemaKind::math && i + 4 <= f.size; i += 4) {
      float v;
      std::memcpy(&v, buf.data() + i, 4);
      if (!(v == v) || v > 1e9f || v < -1e9f) {
        return false;
      }
    }
    void* dst = f.addr(ent);
    const std::string refData = r.in.substr(from + f.size, refBytes);
    BlockReader ids(refData);
    for (TUniqueId* ref : refs) {
      // Policy 1 wrote raw POD IDs; retain the rebuilt reference rather than trusting
      // those bytes. Policy 2 names saved targets by identity, just like top-level IDs.
      const TUniqueId uid = GetUid(mgr, ent, policy >= 2 ? ids.U32() : kUidDynamic, *ref);
      const size_t off = reinterpret_cast< char* >(ref) - static_cast< char* >(dst);
      std::memcpy(buf.data() + off, &uid, sizeof(uid));
    }
    std::memcpy(dst, buf.data(), f.size);
    return true;
  }
  }
}

// One pass over the classes of a kTagFields block (GetField's `pass`).
int ApplyClasses(CStateManager& mgr, CEntity& ent, const std::vector< const PortSchemaClass* >& chain,
                 int classes, BlockReader& r, int pass, uint32_t policy) {
  int set = 0;
  const TUniqueId uid = ent.GetUniqueId();
  for (int c = 0; c < classes && r.ok; ++c) {
    const uint64_t lo = r.U32();
    const uint64_t fingerprint = lo | (static_cast< uint64_t >(r.U32()) << 32);
    const uint32_t len = r.U32();
    if (!r.ok || r.in.size() - r.at < len) {
      break;
    }
    const PortSchemaClass* cls = nullptr;
    for (const PortSchemaClass* k : chain) {
      if (k->fingerprint == fingerprint) {
        cls = k;
      }
    }
    const size_t end = r.at + len;
    if (cls != nullptr) {
      const std::string body = r.in.substr(r.at, len);
      BlockReader br(body);
      for (int i = 0; i < cls->fieldCount && br.ok && mgr.ObjectById(uid) == &ent; ++i) {
        if (SavedField(*cls, cls->fields[i]) && GetField(mgr, ent, cls->fields[i], br, pass, policy)) {
          ++set;
        }
      }
    }
    r.at = end;
  }
  return set;
}

} // namespace

bool EntityFieldsSaved(const CStateManager& mgr, CEntity& ent) {
  // Enemies, and the pickups and debris a generator made (life timers, generated flag).
  return Enabled() &&
         (TCastToPtr< CPatterned >(&ent) != nullptr ||
          (IsScriptGeneratedCopy(mgr, ent) &&
           (TCastToPtr< CScriptPickup >(&ent) != nullptr || TCastToPtr< CScriptDebris >(&ent) != nullptr)));
}

bool SchemaFieldSaved(const PortSchemaClass& cls, const PortSchemaField& f) { return SavedField(cls, f); }

bool CaptureEntityFields(const CStateManager& mgr, CEntity& ent, std::string& out) {
  if (!EntityFieldsSaved(mgr, ent)) {
    return false;
  }
  const std::vector< const PortSchemaClass* > chain = Chain(ent);
  BlockWriter w;
  w.U32(kFieldPolicy);
  w.U8(static_cast< uint8_t >(chain.size()));
  for (const PortSchemaClass* cls : chain) {
    BlockWriter body;
    for (int i = 0; i < cls->fieldCount; ++i) {
      if (SavedField(*cls, cls->fields[i])) {
        PutField(mgr, ent, cls->fields[i], body);
      }
    }
    w.U32(static_cast< uint32_t >(cls->fingerprint));
    w.U32(static_cast< uint32_t >(cls->fingerprint >> 32));
    w.U32(static_cast< uint32_t >(body.bytes.size()));
    w.bytes += body.bytes;
  }
  out = std::move(w.bytes);
  return true;
}

int ApplyEntityFields(CStateManager& mgr, CEntity& ent, const std::string& data) {
  if (!Enabled()) {
    return 0;
  }
  BlockReader r(data);
  const uint32_t policy = r.U32();
  if (policy < 1 || policy > kFieldPolicy || !r.ok) {
    return 0;
  }
  const std::vector< const PortSchemaClass* > chain = Chain(ent);
  const int classes = r.U8();
  const size_t start = r.at;
  // The plain fields go in before the codecs too, so the saved state's Activate starts from
  // the saved fields, and again after, over what entering the states changed. The first
  // pass is not counted.
  int set = 0;
  const int passes[] = {1, 0, 3, 2, 1};
  for (int i = 0; i < 5; ++i) {
    r.at = start;
    // Entering a saved state may delete the enemy (one told to go away).
    if (i >= 2 && mgr.ObjectById(ent.GetUniqueId()) != &ent) {
      break;
    }
    const int n = ApplyClasses(mgr, ent, chain, classes, r, passes[i], policy);
    if (i > 0) {
      set += n;
    }
  }
  return set;
}

bool EntityFieldsCover(CEntity& ent, const std::string& data) {
  if (!Enabled()) {
    return false;
  }
  BlockReader r(data);
  const uint32_t policy = r.U32();
  if (policy < 1 || policy > kFieldPolicy || !r.ok) {
    return false;
  }
  const std::vector< const PortSchemaClass* > chain = Chain(ent);
  const int classes = r.U8();
  int matched = 0;
  for (int c = 0; c < classes && r.ok; ++c) {
    const uint64_t lo = r.U32();
    const uint64_t fingerprint = lo | (static_cast< uint64_t >(r.U32()) << 32);
    const uint32_t len = r.U32();
    if (!r.ok || r.in.size() - r.at < len) {
      return false;
    }
    for (const PortSchemaClass* k : chain) {
      if (k->fingerprint == fingerprint) {
        ++matched;
      }
    }
    r.at += len;
  }
  return !chain.empty() && matched == static_cast< int >(chain.size());
}

} // namespace PortSaveState
