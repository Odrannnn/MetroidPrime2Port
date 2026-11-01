// The Dark Samus's state machine: the 49 triggers and the 43 states the REL hands to the machine,
// and the bodies behind them.
//
// fn_10_18D78 is the module's own setup (called from the first Think, fn_10_1B1FC, when the
// machine has no state yet): it calls CPatterned's InitializeStateMachine and then hands the
// machine two tables - lbl_10_data_450 with 49 entries (the triggers) and lbl_10_data_964 with 43
// entries (the states). The names in the tables are the module's own strings (the .rodata at
// lbl_10_rodata_579 and lbl_10_rodata_871) and the machine finds a node's handler by name.
//
// Which REL function implements which name: every entry has a 12-byte record beside it (the 49 at
// lbl_10_data_204, the 43 at lbl_10_data_760) and the .ctors initialiser fn_10_221C0 copies the
// record into the entry (entry+4 = record[0], entry+8 = record[1], entry+12 = record[2]). The
// record's second word is the byte offset of the handler inside lbl_10_data_CBC - the blob that
// holds the actor's vtable (words 0..81) and then the 92 handlers (words 82..173): the triggers
// take the words 82..130 and the states 131..173. That is what tells the two halves apart - the
// first 49 handlers return a bool and take (mgr, float), the last 43 return nothing and take
// (mgr, EStateMsg, float) - and it lines up with the two tables' names.
//
// The bodies below are stand-ins: each logs the first time it runs and does nothing else. The
// tables, the names and the name-to-function mapping are retail's; the behaviour is not yet.
#include "MetroidPrime/Enemies/CDarkSamus.hpp"
#include "MetroidPrime/Enemies/CDarkSamusBoostBall.hpp"
#include "MetroidPrime/Enemies/CScattershotProjectile.hpp"

#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CEnvFxManager.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "WorldFormat/CMetroidAreaCollider.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/CRandom16.hpp"
#include "MetroidPrime/Enemies/CGenericFSM2.hpp"
#include "MetroidPrime/Enemies/CDarkSamusBattleStage.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCoverPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptRepulsor.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/PathFinding/CPathFindArea.hpp"
#include "MetroidPrime/PathFinding/CPathFindRegion.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Animation/CPASDatabase.hpp"
#include "MetroidPrime/StateMachineCommon.hpp"
#include "MetroidPrime/CAxisAngle.hpp"
#include "MetroidPrime/CRumbleManager.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CEnergyProjectile.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CAnimPlaybackParms.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "MetroidPrime/CDecalManager.hpp"
#include "Weapons/CCollisionResponseData.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CModelData.hpp"

#include <stdio.h>
#include <stdlib.h>

#include "types.h"

namespace {

typedef CPatterned::StateMachine::TriggerFunc TriggerFunc;
typedef CPatterned::StateMachine::StateFunc StateFunc;
typedef CPatterned::StateMachine::STriggerFunction STriggerFunction;
typedef CPatterned::StateMachine::SStateFunction SStateFunction;

#define DS_TRIGGER(name) {#name, static_cast< TriggerFunc >(&CDarkSamus::name)}
#define DS_STATE(name) {#name, static_cast< StateFunc >(&CDarkSamus::name)}

// The once-only log the milestone-1 body uses: one line per retail function, the first time it
// runs, so a log lists what is still a shell.
void FsmStandIn(const char* fn, const char* what) {
  static const char* sSeen[128] = {};
  static int sCount = 0;
  for (int i = 0; i < sCount; ++i) {
    if (sSeen[i] == fn) {
      return;
    }
  }
  if (sCount < 128) {
    sSeen[sCount++] = fn;
  }
  fprintf(stderr, "[darksamus] fsm stand-in %s (%s)\n", fn, what);
}

// fn_10_18D78's trigger table (lbl_10_data_450), in the REL's order. The comment is the REL's
// handler, read out of the record beside the entry.
const STriggerFunction skTriggers[] = {

    DS_TRIGGER(Alerted), // fn_10_F840
    DS_TRIGGER(Arrived), // fn_10_62B4
    DS_TRIGGER(AnyIncomingMissiles), // fn_10_1BB08
    DS_TRIGGER(BreakFreezeBeam), // fn_10_16B48
    DS_TRIGGER(BreakDash), // fn_10_149B4
    DS_TRIGGER(BreakDiveAttack), // fn_10_11C54
    DS_TRIGGER(BreakNormalMissile), // fn_10_16B5C
    DS_TRIGGER(BreakPause), // fn_10_106F8
    DS_TRIGGER(BreakPhazonAttack), // fn_10_4ABC
    DS_TRIGGER(BreakSuperMissile), // fn_10_16B54
    DS_TRIGGER(CanMeleeAttack), // fn_10_10634
    DS_TRIGGER(CanMissileDeflect), // fn_10_14284
    DS_TRIGGER(DiveAfterBoost), // fn_10_95A8
    DS_TRIGGER(DoneRolling), // fn_10_9314
    DS_TRIGGER(DoneTurning), // fn_10_F864
    DS_TRIGGER(FacingPlayer), // fn_10_142CC
    DS_TRIGGER(HitDuringEnrage), // fn_10_7B68
    DS_TRIGGER(OutOfMeleeRange), // fn_10_17CB8
    DS_TRIGGER(PickedBoostBall), // fn_10_116F8
    DS_TRIGGER(PickedFreezeBeam), // fn_10_11760
    DS_TRIGGER(PickedDiveAttack), // fn_10_1174C
    DS_TRIGGER(PickedMelee), // fn_10_117D8
    DS_TRIGGER(PickedMeleeDash), // fn_10_11710
    DS_TRIGGER(PickedMissileJump), // fn_10_1178C
    DS_TRIGGER(PickedMovingSShot), // fn_10_11724
    DS_TRIGGER(PickedNormalMissile), // fn_10_117A0
    DS_TRIGGER(PickedPause), // fn_10_11800
    DS_TRIGGER(PickedPhazonAttack), // fn_10_8680
    DS_TRIGGER(PickedPhazonEnrage), // fn_10_8114
    DS_TRIGGER(PickedScatterShot), // fn_10_11738
    DS_TRIGGER(PickedSuperMissile), // fn_10_11778
    DS_TRIGGER(PickedSweepBeam), // fn_10_CDA0
    DS_TRIGGER(PickedTaunt), // fn_10_117EC
    DS_TRIGGER(PlayerIsMorphball), // fn_10_112E8
    DS_TRIGGER(PlayerTooClose), // fn_10_10658
    DS_TRIGGER(ReachedPissedOff), // fn_10_FAF4
    DS_TRIGGER(ReadyForMissiles), // fn_10_6A40
    DS_TRIGGER(ShouldEnragedMelee), // fn_10_6B78
    DS_TRIGGER(ShouldFadeIn), // fn_10_BB78
    DS_TRIGGER(ShouldFadeOut), // fn_10_BBE4
    DS_TRIGGER(ShouldGetPissedOff), // fn_10_FD30
    DS_TRIGGER(ShouldLeaveSphere), // fn_10_6A60
    DS_TRIGGER(ShouldReact), // fn_10_11158
    DS_TRIGGER(ShouldRiseAgain), // fn_10_6AE8
    DS_TRIGGER(SkipPhazonAttack), // fn_10_110E0
    DS_TRIGGER(StartsInTheAir), // fn_10_F924
    DS_TRIGGER(TooMuchDamage), // fn_10_10698
    DS_TRIGGER(WallCollision), // fn_10_F800
    DS_TRIGGER(WasHeavyHit), // fn_10_112AC
};

// fn_10_18D78's state table (lbl_10_data_964), in the REL's order.
const SStateFunction skStates[] = {
    DS_STATE(BoostBallFire), // fn_10_9724
    DS_STATE(BoostBallInto), // fn_10_B164
    DS_STATE(BoostBallRoll), // fn_10_A3B0
    DS_STATE(FreezeBeam), // fn_10_16CDC
    DS_STATE(Dash), // fn_10_150DC
    DS_STATE(DiveAttack), // fn_10_11FA4
    DS_STATE(DiveFromBoost), // fn_10_9424
    DS_STATE(EnrageHitReact), // fn_10_7A14
    DS_STATE(EnragedTravel), // fn_10_6E38
    DS_STATE(FadeIn), // fn_10_BD40
    DS_STATE(FadeOut), // fn_10_BC54
    DS_STATE(FallFromTheAir), // fn_10_F86C
    DS_STATE(GetUp), // fn_10_18EAC
    DS_STATE(GoToPhazonAttack), // fn_10_652C
    DS_STATE(GoToPhazonEnrage), // fn_10_6460
    DS_STATE(HeavyHit), // fn_10_10C9C
    DS_STATE(Interrupt), // fn_10_10FF4
    DS_STATE(MeleeAttack), // fn_10_17BC0
    DS_STATE(MeleeDash), // fn_10_17ABC
    DS_STATE(MissileDeflect), // fn_10_178E4
    DS_STATE(MissileJump), // fn_10_177BC
    DS_STATE(MovingScatterShot), // fn_10_11AD8
    DS_STATE(NormalMissileAttack), // fn_10_17448
    DS_STATE(Null), // fn_10_14448
    DS_STATE(Dead), // fn_10_143A8
    DS_STATE(Patrol), // fn_10_1142C
    DS_STATE(Pause), // fn_10_10710
    DS_STATE(PhazonAttack), // fn_10_81D4
    DS_STATE(PhazonEnraged), // fn_10_7BC0
    DS_STATE(PhazonFall), // fn_10_6BA0
    DS_STATE(PhazonMissiles), // fn_10_67B8
    DS_STATE(PhazonRise), // fn_10_7054
    DS_STATE(PhazonSuck), // fn_10_4AC8
    DS_STATE(PissedOff), // fn_10_F930
    DS_STATE(ScatterShot), // fn_10_175AC
    DS_STATE(SideDash), // fn_10_153EC
    DS_STATE(Start), // fn_10_F920
    DS_STATE(SuperMissileAttack), // fn_10_16ECC
    DS_STATE(SweepBeam), // fn_10_CA0C
    DS_STATE(Taunt), // fn_10_11544
    DS_STATE(TravelToCoverPoints), // fn_10_1DF74
    DS_STATE(TravelToPissedOff), // fn_10_FB8C
    DS_STATE(TurnToFacePlayer), // fn_10_FE18
};

// ---- the FSM2 probe (MP2_DS_FSM) --------------------------------------------------------------
//
// The transitions are not in the REL's .data - they come from the actor's 'FSM2' asset - so the
// machine is what tells which trigger leads to which state. The probe has the shape of the Boost
// Guardian's MP2_BG_FSM probe: one line per node and per transition, once per process.

// The node a transition points at.
struct SDsFsmNode {
  const char* mKind; // "state", "trigger", "code", "submachine" or "none".
  int mIndex;        // Index in the machine's own list, -1 when the target is not in it.
  const char* mName; // The node's name.
};

// Finds the list the target belongs to (by its type) and then the element the target points at.
SDsFsmNode DsFsmNodeOf(const CGenericFSM2& machine, const CState2* target) {
  SDsFsmNode node = { "none", -1, "" };
  if (target == nullptr) {
    return node;
  }
  switch (target->GetType()) {
  case CState2::kType_State: {
    const rstl::vector< CState2State >& states = machine.GetStates();
    for (int i = 0; i < states.size(); ++i) {
      if (static_cast< const CState2* >( &states[i] ) == target) {
        node.mKind = "state";
        node.mIndex = i;
        node.mName = states[i].GetName().data();
        return node;
      }
    }
    break;
  }
  case CState2::kType_Trigger: {
    const rstl::vector< CState2Trigger >& triggers = machine.GetTriggers();
    for (int i = 0; i < triggers.size(); ++i) {
      if (static_cast< const CState2* >( &triggers[i] ) == target) {
        node.mKind = "trigger";
        node.mIndex = i;
        node.mName = triggers[i].GetName().data();
        return node;
      }
    }
    break;
  }
  case CState2::kType_Code: {
    const rstl::vector< CState2Code >& codes = machine.GetCodes();
    for (int i = 0; i < codes.size(); ++i) {
      if (static_cast< const CState2* >( &codes[i] ) == target) {
        node.mKind = "code";
        node.mIndex = i;
        node.mName = codes[i].GetName().data();
        return node;
      }
    }
    break;
  }
  case CState2::kType_SubMachine: {
    const rstl::vector< CState2SubMachine >& subMachines = machine.GetSubMachines();
    for (int i = 0; i < subMachines.size(); ++i) {
      if (static_cast< const CState2* >( &subMachines[i] ) == target) {
        node.mKind = "submachine";
        node.mIndex = i;
        node.mName = subMachines[i].GetName().data();
        return node;
      }
    }
    break;
  }
  }
  return node;
}

// The state a node's own transitions lead to (the first one that is a state).
const char* DsFsmStateOf(const CState2* node) {
  if (node == nullptr) {
    return "none";
  }
  const rstl::vector< SStateMachine2Transition >* transitions = nullptr;
  switch (node->GetType()) {
  case CState2::kType_Trigger:
    transitions = &static_cast< const CState2Trigger* >( node )->GetTransitions();
    break;
  case CState2::kType_Code:
    transitions = &static_cast< const CState2Code* >( node )->GetTransitions();
    break;
  default:
    break;
  }
  if (transitions != nullptr) {
    for (int i = 0; i < transitions->size(); ++i) {
      const CState2* target = (*transitions)[i].mTarget;
      if (target != nullptr && target->GetType() == CState2::kType_State) {
        return static_cast< const CState2State* >( target )->GetName().data();
      }
    }
  }
  return "none";
}

// One line per transition of a node: the trigger it waits for (with the parameter the trigger
// carries) and the state that trigger leads to.
void DsDumpFsmTransitions(const CGenericFSM2& machine,
                          const rstl::vector< SStateMachine2Transition >& transitions,
                          const char* indent) {
  for (int i = 0; i < transitions.size(); ++i) {
    const SStateMachine2Transition& transition = transitions[i];
    const CState2* target = transition.mTarget;
    if (target == nullptr) {
      fprintf(stderr, "[dsfsm] %s-> none\n", indent);
      continue;
    }
    if (target->GetType() == CState2::kType_Trigger) {
      const CState2Trigger* trigger = static_cast< const CState2Trigger* >( target );
      fprintf(stderr, "[dsfsm] %strigger %s (parm=%f negate=%d) -> state %s\n", indent,
              trigger->GetName().data(), trigger->GetArgument().GetFloat(),
              trigger->IsNegated() ? 1 : 0, DsFsmStateOf(target));
    } else {
      const SDsFsmNode node = DsFsmNodeOf(machine, target);
      fprintf(stderr, "[dsfsm] %s%s %s\n", indent, node.mKind, node.mName);
      if (node.mKind[0] == 'c' || node.mKind[0] == 's') {
        // A code or a sub-machine carries its own transitions: the state it leads to is in them.
        DsDumpFsmTransitions(machine,
                             node.mKind[0] == 'c'
                                 ? static_cast< const CState2Code* >( target )->GetTransitions()
                                 : static_cast< const CState2SubMachine* >( target )->GetTransitions(),
                             indent);
      }
    }
  }
}

// One line per node and one line per transition, for every list of the machine. A sub-machine
// that carries its own machine is dumped one level deeper.
void DsDumpFsmMachine(const CGenericFSM2& machine, const char* indent, const char* childIndent) {
  fprintf(stderr, "[dsfsm] %smachine: %d states, %d triggers, %d codes, %d sub-machines\n", indent,
          machine.GetStates().size(), machine.GetTriggers().size(), machine.GetCodes().size(),
          machine.GetSubMachines().size());

  const rstl::vector< CState2State >& states = machine.GetStates();
  for (int i = 0; i < states.size(); ++i) {
    fprintf(stderr, "[dsfsm] %sstate %d %s\n", indent, i, states[i].GetName().data());
    DsDumpFsmTransitions(machine, states[i].GetTransitions(), childIndent);
  }

  const rstl::vector< CState2Trigger >& triggers = machine.GetTriggers();
  for (int i = 0; i < triggers.size(); ++i) {
    fprintf(stderr, "[dsfsm] %strigger %d %s (parm=%f negate=%d)\n", indent, i,
            triggers[i].GetName().data(), triggers[i].GetArgument().GetFloat(),
            triggers[i].IsNegated() ? 1 : 0);
  }

  const rstl::vector< CState2Code >& codes = machine.GetCodes();
  for (int i = 0; i < codes.size(); ++i) {
    fprintf(stderr, "[dsfsm] %scode %d %s\n", indent, i, codes[i].GetName().data());
    DsDumpFsmTransitions(machine, codes[i].GetTransitions(), childIndent);
  }

  const rstl::vector< CState2SubMachine >& subMachines = machine.GetSubMachines();
  for (int i = 0; i < subMachines.size(); ++i) {
    fprintf(stderr, "[dsfsm] %ssubmachine %d %s\n", indent, i, subMachines[i].GetName().data());
    DsDumpFsmTransitions(machine, subMachines[i].GetTransitions(), childIndent);
    const CGenericFSM2* sub = subMachines[i].GetMachine();
    if (sub != nullptr) {
      fprintf(stderr, "[dsfsm] %ssubmachine %d machine:\n", indent, i);
      DsDumpFsmMachine(*sub, "    ", "      ");
    }
  }
}

// Dumps the machine once per process. The machine is the actor's 'FSM2' asset; its nodes carry
// the names the REL's own tables (fn_10_18D78) use, so the dump gives the machine's shape and
// the names the module looks its handlers up by.
void DsDumpFsmOnce(CDarkSamus* darksamus) {
  static bool sDumped = false;
  if (sDumped) {
    return;
  }
  sDumped = true;
  const CGenericFSM2* machine = darksamus->GetStateMachine2();
  if (machine == nullptr) {
    fprintf(stderr, "[dsfsm] no FSM2 machine\n");
    return;
  }
  DsDumpFsmMachine(*machine, "", "  ");
}

// The state change itself: the machine sends the state its body the Activate message when it
// enters it.
void DsStateEntered(const char* name, EStateMsg msg) {
  if (msg == kStateMsg_Activate && getenv("MP2_DS_FSM") != nullptr) {
    fprintf(stderr, "[dsfsm] state change: %s\n", name);
  }
}

} // namespace

// The two tables go into the machine under the REL's own names, which is how the machine finds the
// handler for a node: the FSM2 asset's node names are the REL's names.
void CDarkSamus::InstallStateMachineFunctions() {
  StateMachine* stateMachine = mStateMachine.get();
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));

  // The transitions are only in the actor's 'FSM2' asset, so the probe (MP2_DS_FSM) dumps the
  // machine the asset holds: every state with the trigger of each of its transitions, the
  // parameter that trigger carries and the state it leads to. Once per process.
  if (getenv("MP2_DS_FSM") != nullptr) {
    DsDumpFsmOnce(this);
  }
}

// fn_10_142F4: is the player within `angle` of the actor's facing axis? The direction to the
// player and the forward axis (the guest's 0x28/0x38/0x48 = the transform's forward column) are
// flattened into the XY plane (the two zero stores at 0x00014364/0x00014368), normalized (the two
// calls at 0x0001436C/0x00014374) and compared (the fcmpo at 0x00014384; the cr bit 31 at
// 0x0001438C is the "less than" of the compare, so the limit is strict).
// @ fn_10_142F4
bool CDarkSamus::IsPlayerInFront(CStateManager& mgr, float angle) const {
  const CVector3f toPlayer = mgr.GetPlayer(0)->GetTranslation() - GetTranslation();
  const CVector3f forward = GetTransform().GetForward();
  const CVector3f flatToPlayer = toPlayer.DropZ().AsNormalized();
  const CVector3f flatForward = CVector3f(forward.GetX(), forward.GetY(), 0.f).AsNormalized();
  return CVector3f::GetAngleDiff(flatForward, flatToPlayer) < angle;
}

// fn_10_17E14: the fight script's object - the one with the uid at the guest's 0x974 - and
// module 11's accessor on it (fn_11_B70, which returns the object's 0x24, the stage info).
// fn_10_17DE0 looks the uid up in the state manager (GetObjectById at 0x00017DFC) and casts the
// result to CScriptDarkSamusBattleStage (the cast at 0x00017E00); a null object (no script) ends
// the helper with a null (the cmplwi at 0x00017E24). The uid is written by AcceptScriptMsg's
// 'XALD' arm (0x0001EBB8, the first stage) and by fn_10_193A0 (0x00019410, the next stage);
// until the area is loaded the lookup finds nothing and the triggers below answer false.
// @ fn_10_17E14
// @ fn_10_17DE0
const CDarkSamusBattleStage* CDarkSamus::GetBattleStage(CStateManager& mgr) const {
  return TCastToConstPtr< CDarkSamusBattleStage >(mgr.GetObjectById(mBattleStageUid));
}

// fn_10_17E90 (0x00017E90, 0x74 bytes): the health left in percent: lbl_10_rodata_3F0 = 100.0f
// times the health info's +4 (the health) over its +0 (the initial health), both through the
// vtable slot 0x3C (0x00017EB0/0x00017EC8).
// @ fn_10_17E90
float CDarkSamus::GetHealthPercent() const {
  const CHealthInfo* health = GetHealthInfo();
  return 100.f * (health->GetHP() / health->GetInitialHP());
}

// fn_10_194AC (0x000194AC, 0xA0 bytes): the stage after the current one: the stage object's first
// kSS_Connect link, any message (FindConnectedObject at 0x00019508). No stage object (0x000194E0)
// or no link (0x0001951C) answers the current stage's uid 0x974.
// @ fn_10_194AC
TUniqueId CDarkSamus::GetNextBattleStage(CStateManager& mgr) const {
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  if (stage == nullptr) { // 0x000194E0
    return mBattleStageUid;
  }
  const TUniqueId next = stage->FindConnectedObject(mgr, kSS_Connect, kSM_None);
  if (next != kInvalidUniqueId) {
    return next;
  }
  return mBattleStageUid;
}

// fn_10_B86C (0x0000B86C, 0x1CC bytes): walks the AI hint chains the actor links with
// kSS_InternalState6 (FindConnectedObjects at 0x0000B89C, any message). A chain starts at a hint of
// type 0x11 (0x158, `cmpwi r0, 0x11` at 0x0000B8E4) and each hint on it is activated (the vtable
// slot 0x1C, SetActive(true), at 0x0000B908) before the next is looked for among its kSS_Arrived
// links (0x0000B928): the first that is a hint. An active next hint whose 0x15C (the radius) is
// lbl_10_rodata_25C = 0.0f counts up (0x0000B99C), an inactive one resets the count (0x0000B9A4);
// four (`cmpwi r29, 0x4` at 0x0000B9CC) or no next hint ends the chain. The links vector copy
// (fn_10_B570 at 0x0000B8A8) and its destructor (fn_10_B670 at 0x0000B8B4) are the host vector's.
// Arms: the chain walk, plus forced calls: the first start lookup null, a start of another type,
// the first arrived lookup null, the first arrived hint inactive, one with a radius (ds45 gdb
// calls, tools/cov/specs/darksamus_ds44_calls.gdb).
// @ fn_10_B86C
void CDarkSamus::ActivateHintChains(CStateManager& mgr) {
  const rstl::vector< TUniqueId > starts =
      FindConnectedObjects(mgr, kSS_InternalState6, kSM_None);
  for (rstl::vector< TUniqueId >::const_iterator it = starts.begin(); it != starts.end(); ++it) {
    CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(mgr.ObjectById(*it));
    if (hint == nullptr ||                                   // 0x0000B8DC
        static_cast< int >(hint->GetHintType()) != 0x11) {   // 0x0000B8E8
      continue;
    }
    int count = 0;
    while (hint != nullptr) { // 0x0000B9F4
      hint->SetActive(true);
      CScriptAIHint* next = nullptr;
      const rstl::vector< TUniqueId > arrived =
          hint->FindConnectedObjects(mgr, kSS_Arrived, kSM_None);
      for (rstl::vector< TUniqueId >::const_iterator a = arrived.begin(); a != arrived.end();
           ++a) {
        CScriptAIHint* other = TCastToPtr< CScriptAIHint >(mgr.ObjectById(*a));
        if (other == nullptr) { // 0x0000B968
          continue;
        }
        if (other->GetActive() == true) {
          if (0.f == other->GetRadius()) {
            ++count;
          }
        } else {
          count = 0;
        }
        next = other;
        break;
      }
      if (count >= 4) {
        break;
      }
      hint = next;
    }
  }
}

// fn_10_193A0 (0x000193A0, 0x10C bytes): the stage advance. A stage (fn_10_17E14, 0x000193C4) with
// an advance threshold above lbl_10_rodata_3FC = 0.0f (its info's +0x0, 0x000193DC) that the
// health percent (fn_10_17E90) has fallen below (`bge` at 0x000193F0) gives way to the next stage
// (fn_10_194AC; the uid to 0x974 at 0x00019410, 0x978 counts up at 0x0001941C), and a next stage
// whose info's +0x35 is 1 (0x00019440) wakes the hint chains (fn_10_B86C). Then, with a stage
// (0x0001945C), 0x97C takes its flags: 0x80 = the info's +0x34 (usesAlternateScannableInfo),
// 0x40 = its +0x32 is not 1 (the subfic/subi/or at 0x0001947C).
// @ fn_10_193A0
void CDarkSamus::AdvanceBattleStage(CStateManager& mgr) {
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  if (stage != nullptr && stage->GetStageInfo().unknown_0xb5af7831 > 0.f && // 0x000193C8
      GetHealthPercent() < stage->GetStageInfo().unknown_0xb5af7831) {
    mBattleStageUid = GetNextBattleStage(mgr);
    ++mStageCount;
    if (GetBattleStage(mgr) != nullptr && // 0x00019428
        GetBattleStage(mgr)->GetStageInfo().unknown_0xaa85c885 == true) {
      ActivateHintChains(mgr);
    }
  }
  if (const CDarkSamusBattleStage* now = GetBattleStage(mgr)) {
    mUseAltScan = now->GetStageInfo().usesAlternateScannableInfo;
    mCanDie = now->GetStageInfo().unknown_0xc96b8223 != true;
  }
}

// fn_10_1D794 (0x0001D794, 0xC8 bytes): the still timer (the guest's 0x964) and the position it
// measures from (the guest's 0x958/0x95C/0x960).
//
// On kStateMsg_Activate (the guest's argument r4 against 0, 0x0001D7B0) the timer is cleared and
// the reference position becomes the actor's own position (the base class' position, the guest's
// 0x54/0x58/0x5C): 0x0001D810..0x0001D834. On any other message the distance between the actor's
// position and the reference position is taken (the three fsubs at 0x0001D7D0..0x0001D7E4 and
// Magnitude at 0x0001D7FC): when it is more than 0.6 (lbl_10_rodata_424, the ble at 0x0001D80C) the
// actor has walked away, so the timer is cleared and the reference position becomes the actor's
// position again (0x0001D810); when it is not, the timer grows by dt (0x0001D838).
// @ fn_10_1D794
void CDarkSamus::UpdateStillTimer(EStateMsg msg, float dt) {
  if (msg == kStateMsg_Activate) {
    mStillTime = 0.f;
    mStillPosition = GetTranslation();
    return;
  }
  const CVector3f delta = GetTranslation() - mStillPosition;
  if (!(delta.Magnitude() > 0.6f)) { // 0x1D80C ble to the += (a NaN too)
    mStillTime += dt;
    return;
  }
  mStillTime = 0.f;
  mStillPosition = GetTranslation();
}

// fn_10_1C778 (0x0001C778, 0x84 bytes): the destination of the path find (the guest's
// 0x8B4/0x8B8/0x8BC) and the path find itself.
//
// The vector is written into the three floats (the three stfs at 0x0001C7A4..0x0001C7BC), then
// the base class' path find navigation (the guest's 0x6F4) gets the destination (the
// SetDestination call at 0x0001C7C0) and is told to find a path (the PathFind call at
// 0x0001C7D8) with the message kStateMsg_Activate (the `li r5, 0x0` at 0x0001C7D4) and the
// float argument.
// @ fn_10_1C778
void CDarkSamus::SetPathFindDestination(CStateManager& mgr, const CVector3f& destination,
                                        float dt) {
  mPathFindDestination = destination;
  mPathFindNavigation.SetDestination(destination);
  mPathFindNavigation.PathFind(mgr, kStateMsg_Activate, dt, *this);
}

// fn_10_1E26C (0x0001E26C, 0xDC bytes): the uid of the attacker that hit the actor last (the
// guest's 0xC24) and the two uids before it (the count at 0xC28, the uids at 0xC2C).
//
// The uid is stored (0x0001E288) and an invalid one ends the function (the cmplw against
// kInvalidUniqueId at 0x0001E294). When there are two uids already the oldest is dropped
// (fn_10_1E2E8 at 0x0001E2B0: it shifts the rest one down and counts one less), and then the uid
// is appended (0x0001E2C4) and the count grows (0x0001E2D0).
// fn_10_1E26C (0x0001E26C, 0x7C bytes): the attacker's uid (0xC24), the count (0xC28)
// and the uid list (0xC2C). Arms: the uid is invalid (nothing is stored, 0x0001E298),
// the list has two entries (the oldest is dropped first, fn_10_1E2E8 at 0x0001E2B0) and
// the list has room.
// // The arms: the coverage run (tools/cov/specs/darksamus_helpers2.py, the rooftop fight: Sanctuary 10_cliff_rooftop, boss u04f7, 6400 updates) showed the "the list has two" and the "the list has room" arms; the
// "invalid uid" arm was forced (the uid written at the caller's entry,
// tools/cov/specs/darksamus_helpers2_force.py).
// @ fn_10_1E26C
void CDarkSamus::PushAttacker(TUniqueId uid) {
  mLastAttackerUid = uid;
  if (mLastAttackerUid == kInvalidUniqueId) {
    return;
  }
  if (mAttackerCount >= 2) {
    EraseFirstAttacker();
  }
  mAttackerUids[mAttackerCount] = mLastAttackerUid;
  mAttackerCount++;
}

// fn_10_1E2E8 (0x0001E2E8, 0x78 bytes): the oldest uid is dropped and the rest move one down -
// the guest's loop copies every uid one slot down (the lhz/sth at 0x0001E318/0x0001E31C) and the
// count grows one less (0x0001E33C/0x0001E344). The retail body is the generic erase(pos) of a
// reserved_vector<TUniqueId, 2>; its range checks (0x0001E2F4, 0x0001E30C) and the loop
// (0x0001E338) are fixed by the only caller, fn_10_1E26C at 0x0001E2B0 in its own body, which
// passes begin (`addi r4, r31, 0xc2c`) with the count at 2 (`cmpwi r0, 0x2; blt` before it): the
// position is in range and the loop copies one uid.
// @ fn_10_1E2E8
void CDarkSamus::EraseFirstAttacker() {
  mAttackerUids[0] = mAttackerUids[1];
  mAttackerCount--;
}

// fn_10_1E360 (0x0001E360, 0x60 bytes): the point on the player the cover points are scored
// against - the player's aim position (the state manager's 0x14FC and its vtable slot 0x54) a
// lbl_10_rodata_55C = 0.65f ahead.
// @ fn_10_1E360
CVector3f CDarkSamus::GetPlayerAimPosition(CStateManager& mgr) const {
  return mgr.GetPlayer(0)->GetAimPosition(mgr, 0.65f);
}

// fn_10_1E3C0 (0x0001E3C0, 0x104 bytes): the best cover point to travel to.
//
// The AI waypoint list (the state manager's 0x838, its first index at 0x2008) is walked
// (0x0001E41C..0x0001E4A0): each cover point (the TCastToPtr at 0x0001E428) that is active (the
// byte at 0x20's bit 7, 0x0001E434) and in the actor's area (0x0001E458) is scored against the
// player's aim position (fn_10_1E360 at 0x0001E3F8, fn_10_1E4C4 at 0x0001E46C); the best score
// above lbl_10_rodata_410 = -1000.0f (the fcmpo at 0x0001E470, strict) wins. No cover point
// answers kInvalidUniqueId (the lhz at 0x0001E3E8).
// Arms: the fight, plus a scored cover point made inactive and moved to another area (ds43 gdb
// calls).
// @ fn_10_1E3C0
TUniqueId CDarkSamus::FindBestCoverPoint(CStateManager& mgr) const {
  TUniqueId best = kInvalidUniqueId;
  float bestScore = -1000.f;
  const CVector3f target = GetPlayerAimPosition(mgr);
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptCoverPoint* cover = TCastToPtr< CScriptCoverPoint >(list[i]);
    if (cover == nullptr || cover->GetActive() != true ||
        cover->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    const float score = ScoreCoverPoint(*cover, target);
    if (score > bestScore) { // 0x0001E470
      bestScore = score;
      best = cover->GetUniqueId();
    }
  }
  return best;
}

// fn_10_1E4C4 (0x0001E4C4, 0x27C bytes): how good a cover point is against `target`.
//
// The score starts at lbl_10_rodata_404 = 10.0f and loses 10 for each recent attacker uid (the
// guest's 0xC28 count, the uids at 0xC2C) that is this cover point (0x0001E510..0x0001E528). A
// cover point between the attack ranges (the 3D distance to the target, Magnitude at
// 0x0001E588, strictly above 0x3DC and below 0x3E0) gains lbl_10_rodata_40C = 5.0f. The flat
// (z = lbl_10_rodata_3FC = 0.0f) directions from the target to the cover point and to the actor
// (each normalized when it can be, 0x0001E5B4/0x0001E610) more than lbl_10_rodata_43C = pi / 2
// apart (the fcmpo at 0x0001E63C) lose lbl_10_rodata_4C8 = 15.0f, and the flat forward axis (the
// guest's 0x28/0x38/0x48) and the flat direction from the actor to the cover point more than
// pi / 2 apart (0x0001E704) lose 10.
// @ fn_10_1E4C4
float CDarkSamus::ScoreCoverPoint(const CScriptCoverPoint& cover, const CVector3f& target) const {
  float score = 10.f;
  for (uint i = 0; i < mAttackerCount; ++i) {
    if (mAttackerUids[i] == cover.GetUniqueId()) { // 0x0001E518
      score -= 10.f;
    }
  }
  const CVector3f toCover = cover.GetTranslation() - target;
  CVector3f flatToCover(toCover.GetX(), toCover.GetY(), 0.f);
  const float dist = toCover.Magnitude();
  if (dist > mMinAttackRange && dist < mMaxAttackRange) { // 0x0001E590/0x0001E59C
    score += 5.f;
  }
  if (flatToCover.CanBeNormalized()) {
    flatToCover.Normalize();
  }
  const CVector3f toSelf = GetTranslation() - target;
  CVector3f flatToSelf(toSelf.GetX(), toSelf.GetY(), 0.f);
  if (flatToSelf.CanBeNormalized()) {
    flatToSelf.Normalize();
  }
  if (CVector3f::GetAngleDiff(flatToCover, flatToSelf) > 1.5707964f) { // 0x0001E63C
    score -= 15.f;
  }
  const CVector3f forward = GetTransform().GetForward();
  CVector3f flatForward(forward.GetX(), forward.GetY(), 0.f);
  if (flatForward.CanBeNormalized()) {
    flatForward.Normalize();
  }
  const CVector3f selfToCover = cover.GetTranslation() - GetTranslation();
  CVector3f flatSelfToCover(selfToCover.GetX(), selfToCover.GetY(), 0.f);
  if (flatSelfToCover.CanBeNormalized()) {
    flatSelfToCover.Normalize();
  }
  if (CVector3f::GetAngleDiff(flatForward, flatSelfToCover) > 1.5707964f) { // 0x0001E704
    score -= 10.f;
  }
  return score;
}

// fn_10_1E740 (0x0001E740, 0x124 bytes): should the actor forget the uid of the attacker that hit
// it last (the guest's 0xC24)?
//
// Without an attacker there is nothing to forget (the lhz against kInvalidUniqueId at
// 0x0001E760..0x0001E774). A shagged path (PathShagged at 0x0001E788, its result's low byte
// against 1 at 0x0001E78C..0x0001E794) forgets the attacker, and so does a path find that is
// over: the guest's 0x888 - the search's current waypoint - against the guest's 0x7C4 - the
// search's waypoint count - minus one (0x0001E798..0x0001E7A8). An attacker without a cover
// point forgets it too (the null check at 0x0001E7CC). Otherwise the distance between the cover
// point and the actor (the three fsubs at 0x0001E7DC..0x0001E804) is squared (the three fmuls
// at 0x0001E810..0x0001E828) and compared with lbl_10_rodata_408 = 4.0f (the fcmpo at
// 0x0001E840); the mfcr/srwi at 0x0001E844..0x0001E848 keeps the compare's "less than" bit, so
// the attacker is forgotten once it is within 2 units.
// @ fn_10_1E740
bool CDarkSamus::ShouldForgetLastAttacker(CStateManager& mgr) const {
  if (mLastAttackerUid == kInvalidUniqueId) {
    return false;
  }
  if (PathShagged(mgr, CTriggerData(0.f))) {
    return true;
  }
  const CPathFindSearch* search = GetSearchPath();
  if (search->GetCurrentWaypoint() >= search->GetWaypoints().size() - 1) {
    return true;
  }
  const CScriptCoverPoint* cover = GetCoverPoint(mgr, mLastAttackerUid);
  if (cover == nullptr) {
    return true;
  }
  return (cover->GetTranslation() - GetTranslation()).MagSquared() < 4.0f;
}

// fn_10_1D9A4 (0x0001D9A4, 0x370 bytes): the point the actor is pushed away from.
//
// The actor's connections are walked (the begin/end at 0x1C/0x14, 0x0001D9CC/0x0001DB2C): each
// one's editor id is resolved (GetIdForScript at 0x0001D9F8, the invalid uid skipped at
// 0x0001DA0C) and cast to a repulsor (0x0001DA20). An active repulsor (the byte at 0x20's bit 7,
// the extrwi at 0x0001DA30) in the actor's area (0x0001DA40) without the repel-player flag (the
// flags at 0x164 & 1, 0x0001DA4C) is measured: the offset from the actor to it (0x0001DA54..),
// with the height dropped for a cylinder (the shape against 1 at 0x0001DA88, lbl_10_rodata_3FC =
// 0.0f), squared against the radius squared (the fcmpo at 0x0001DAD4; a point outside is
// skipped). A repulsor that holds the actor answers its position (0x0001DB0C), and `strength`,
// when not null (0x0001DADC), gets 1 - distance / radius (lbl_10_rodata_3E8 = 1.0f, 0x0001DB04).
//
// Without one, the area's path find area (the world's area of the actor's area id, its post
// constructed data's 0xEC, 0x0001DB44..0x0001DB68) is looked at; without a path area the answer
// is zero (0x0001DB6C). The actor's position lifted by lbl_10_rodata_3E4 = 0.5f (and
// lbl_10_rodata_3FC = 0.0f on x and y) is taken into the area's frame (the translation at
// 0x1C8/0x1D8/0x1E8 subtracted, TransposeRotate at 0x0001DBF4) and the regions there are found
// with the search's region flags and creature mask (0x0001DC1C..0x0001DC44, the last argument
// true); the first region with kPFO_Unknown2 obstructions (GetObstructionCount(2) > 0 at
// 0x0001DC60) answers its centroid (the region's 0x28, 0x0001DC6C). Otherwise the last contact
// point answers for lbl_10_rodata_46C = 0.3 seconds after the contact (0x0001DCAC..0x0001DCB4)
// and the zero vector after that (0x0001DCD4).
// Arms: the fight (11 repulsor connections), plus forced calls: every connection invalid; a
// repulsor holding the actor with and without a strength, a cylinder, repel-player, in another
// area, inactive, outside; no path area; a region with an obstruction; a contact under and over
// 0.3 s ago (ds45).
// @ fn_10_1D9A4
CVector3f CDarkSamus::GetRepulsionPoint(CStateManager& mgr, float* strength) const {
  const rstl::vector< SConnection >& connections = GetConnectionList();
  for (rstl::vector< SConnection >::const_iterator it = connections.begin();
       it != connections.end(); ++it) {
    const TUniqueId uid = mgr.GetIdForScript(it->objId);
    if (uid == kInvalidUniqueId) {
      continue;
    }
    const CScriptRepulsor* repulsor = TCastToConstPtr< CScriptRepulsor >(mgr.GetObjectById(uid));
    if (repulsor == nullptr || !repulsor->GetActive() ||
        repulsor->GetCurrentAreaId() != GetCurrentAreaId() ||
        (repulsor->GetFlags() & CScriptRepulsor::kF_RepelPlayer) != 0) { // 0x0001DA4C
      continue;
    }
    CVector3f delta = repulsor->GetTranslation() - GetTranslation();
    if (repulsor->GetShape() == CScriptRepulsor::kS_Cylinder) { // 0x0001DA88
      delta.SetZ(0.f);
    }
    const float radius = repulsor->GetRadius();
    if (delta.MagSquared() > repulsor->GetRadius() * radius) { // 0x0001DAD4
      continue;
    }
    if (strength != nullptr) { // 0x0001DADC
      *strength = 1.f - delta.Magnitude() / repulsor->GetRadius();
    }
    return repulsor->GetTranslation();
  }

  CPFArea* area =
      mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetPostConstructed()->mPathArea;
  if (area == nullptr) { // 0x0001DB6C
    return CVector3f::Zero();
  }
  const CTransform4f& areaXf = area->GetTransform();
  const CVector3f position = GetTranslation() + CVector3f(0.f, 0.f, 0.5f);
  const CVector3f local = areaXf.TransposeRotate(position - areaXf.GetTranslation());
  rstl::reserved_vector< CPFRegion*, 8 > regions;
  const uint creatureMask = mPathFindSearch.GetCreatureMask();
  area->FindRegions(regions, local, mPathFindSearch.GetRegionFlags(), creatureMask, true);
  for (int i = 0; i < regions.size(); ++i) {
    CPFRegion* region = regions[i];
    if (region->GetObstructionCount(kPFO_Unknown2) > 0) { // 0x0001DC64
      return region->GetCentroid();
    }
  }
  if (0.3f + mContactTime > mElapsedTime) { // 0x0001DCB0
    return mContactPoint;
  }
  return CVector3f::Zero();
}

// fn_10_1DD14 (0x0001DD14, 0x260 bytes): the separation.
//
// The steering direction (the guest's 0x910) is cleared (0x0001DD50..0x0001DD68) and the
// repulsion point is asked for (fn_10_1D9A4 at 0x0001DD70) with the strength 0 (lbl_10_rodata_3FC,
// 0x0001DD6C); the zero vector ends the function (the three fcmpu at 0x0001DD8C..0x0001DDB0).
// The point is taken at
// the actor's height (0x0001DDD4). Retail then builds the flat direction from the actor to its
// path find navigation's target (the guest's 0x700, the navigation's mPathTarget) or, when that
// cannot be normalized, the actor's forward axis (0x0001DDC4..0x0001DE3C), but no later
// instruction reads it (the stack slot at 0x3C is dead), so the port leaves it out. The speed is
// lbl_10_rodata_408 = 4.0f, lbl_10_rodata_414 = 8.0f for 0.3 seconds after the last contact
// (0x0001DE5C), and scales with the strength below lbl_10_rodata_46C = 0.3 (0x0001DE78..). The
// base's steering separation from the point (the radius lbl_10_rodata_448 = 12.0f, 0x0001DEA4)
// is flattened (0x0001DEC4) and, when it can be normalized (0x0001DED8), becomes a locomotion
// command (its direction normalized, the zero face vector and the speed, 0x0001DF38) and the
// steering direction (0x0001DF40..0x0001DF50).
// @ fn_10_1DD14
void CDarkSamus::Separate(CStateManager& mgr) {
  mSeparationDir = CVector3f::Zero();
  float strength = 0.f;
  CVector3f point = GetRepulsionPoint(mgr, &strength);
  if (point == CVector3f::Zero()) {
    return;
  }
  point.SetZ(GetTranslation().GetZ());
  float speed = 4.f;
  if (0.3f + mContactTime > mElapsedTime) { // 0x0001DE5C
    speed = 8.f;
  }
  if (strength < 0.3f) { // 0x0001DE78
    speed *= strength / 0.3f;
  }
  CVector3f separation = mSteeringBehaviors.Separation(*this, point, 12.f);
  separation.SetZ(0.f);
  if (separation.CanBeNormalized()) { // 0x0001DED8
    separation.Normalize();
    BodyController()->CommandMgr().DeliverCmd(
        CBCLocomotionCmd(separation.AsNormalized(), CVector3f::Zero(), speed));
    mSeparationDir = separation;
  }
}

// fn_10_1D860 (0x0001D860, 0x144 bytes): the additive animation of the state.
//
// kStateMsg_Activate (the r5 against 0 at 0x0001D868) builds the parms - the state
// kAS_AdditiveReaction (0x18, the li r4 at 0x0001D8F4), the enum 5 (FromEnum at 0x0001D8C4) and
// seven empty parms (NoParameter, 0x0001D888..) - through fn_10_13FFC (0x0001D8F8, the module's
// copy of the CPASAnimParmData constructor: it stores the state and appends the eight parms)
// and asks the animation's PAS database for the best animation (FindBestAnimation with the
// state manager's random generator and -1, 0x0001D918). A score above 0 (lbl_10_rodata_3FC =
// 0.0f, the fcmpo at 0x0001D92C) keeps the animation's index (0x0001D934) and adds it with the
// weight lbl_10_rodata_3E8 = 1.0f, active and fading out (the li r5/r6 = 1 and
// AddAdditiveAnimation at 0x0001D958). kStateMsg_Deactivate (the r5 against 2 at 0x0001D960)
// removes the animation when there is one (the index against -1, 0x0001D96C/0x0001D970;
// DelAdditiveAnimation at 0x0001D980) and forgets it (0x0001D988). kStateMsg_Update does nothing.
// Arms: the fight, plus a FindBestAnimation score forced to 0 and Deactivate with no animation
// (ds43).
// @ fn_10_1D860
void CDarkSamus::UpdateAdditiveAnimation(CStateManager& mgr, EStateMsg msg) {
  if (msg == kStateMsg_Activate) {
    const CPASAnimParmData parms(pas::kAS_AdditiveReaction, CPASAnimParm::FromEnum(5));
    const rstl::pair< float, int > best =
        GetAnimationData()->GetPASDatabase().FindBestAnimation(parms, *mgr.Random(), -1);
    if (best.first > 0.f) { // 0x0001D92C
      mAdditiveAnim = best.second;
      AnimationData()->AddAdditiveAnimation(mAdditiveAnim, 1.f, true, true);
    }
  } else if (msg == kStateMsg_Deactivate) {
    if (mAdditiveAnim != -1) { // 0x0001D970
      AnimationData()->DelAdditiveAnimation(mAdditiveAnim);
      mAdditiveAnim = -1;
    }
  }
}

// fn_10_EFC8 (0x0000EFC8, 0x138 bytes): is the actor where it must not take cover?
//
// With a safe position (the guest's 0x928 against 1 at 0x0000EFF0) the actor is when it is more
// than lbl_10_rodata_404 = 10.0f below it (the fcmpo at 0x0000F00C, strict). Otherwise the AI
// waypoint list (the state manager's 0x838) is walked (0x0000F028..0x0000F0D4): an active AI hint
// (the TCastToPtr at 0x0000F034, the byte at 0x20's bit 7) in the actor's area (0x0000F054) of
// type 0x1C (the word at 0x158, 0x0000F060) whose radius (0x15C) holds the actor (the 3D
// distance, Magnitude at 0x0000F0A0, strictly less) says it is.
// @ fn_10_EFC8
bool CDarkSamus::IsInUnsafeSpot(CStateManager& mgr) const {
  if (mHasSafePos == 1 && GetTranslation().GetZ() < mSafePos.GetZ() - 10.f) { // 0x0000F00C
    return true;
  }
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(list[i]);
    if (hint == nullptr || !hint->GetActive() ||
        hint->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    // 0x1C is not in CScriptAIHint::EHintType yet.
    if (static_cast< int >(hint->GetHintType()) != 0x1C) { // 0x0000F060
      continue;
    }
    if ((GetTranslation() - hint->GetTranslation()).Magnitude() < hint->GetRadius()) {
      return true; // 0x0000F0A4
    }
  }
  return false;
}

// fn_10_11334 (0x00011334, 0xF8 bytes): the looping glide emitter at the actor.
//
// kStateMsg_Activate (0x00011354) starts it unless the sound is kInternalInvalidSfxId (the
// cmplw at 0x00011378): AddEmitter at the actor's position in its area, with acoustics and
// looping (the li r7/r8 = 1 at 0x00011394/0x00011398) and kMedPriority (the lha at 0x00011388),
// and keeps the handle (0x000113A4). kStateMsg_Update starts it first when there is no handle
// (the recursive call with kStateMsg_Activate at 0x000113BC) and then, with a handle, moves it
// to the actor (UpdateEmitter with the zero direction and CAudioSys::kMaxVolume, 0x000113EC).
// kStateMsg_Deactivate (the case 2 at 0x00011368) removes the emitter when there is one
// (0x0001140C) and clears the handle (0x00011414). Other messages do nothing.
// fn_10_11334 (0x00011334, 0xF8 bytes): the looping glide emitter (the sound id at 0xC48,
// the handle at 0xC4C). Arms: the activate with the invalid id (0x00011378) - dead on
// shipped data (the id comes from the loader's constructor argument and is valid: the four
// AddEmitter calls), the activate adds the emitter, the update without an emitter
// (0x000113B0) adds it first, the update with it moves it, the deactivate without an
// emitter (0x000113F8) does nothing, the deactivate removes it.
// // The arms: the coverage run (tools/cov/specs/darksamus_helpers2.py, the rooftop fight: Sanctuary 10_cliff_rooftop, boss u04f7, 6400 updates) showed the activate and the update arms; the "no emitter" arms were forced
// (the handle cleared at the entry, tools/cov/specs/darksamus_helpers2_force.py).
// @ fn_10_11334
void CDarkSamus::UpdateGlideEmitter(EStateMsg msg) {
  switch (msg) {
  case kStateMsg_Activate:
    if (mGlideSfxId == CSfxManager::kInternalInvalidSfxId) { // 0x00011378
      return;
    }
    mGlideSfx = CSfxManager::AddEmitter(mGlideSfxId, GetTranslation(),
                                        GetCurrentAreaId().Value(), true, true,
                                        CSfxManager::kMedPriority);
    break;
  case kStateMsg_Update:
    if (!mGlideSfx) { // 0x000113B0
      UpdateGlideEmitter(kStateMsg_Activate);
    }
    if (!mGlideSfx) { // 0x000113C4
      return;
    }
    CSfxManager::UpdateEmitter(mGlideSfx, GetTranslation(), CVector3f::Zero(),
                               CAudioSys::kMaxVolume);
    break;
  case kStateMsg_Deactivate:
    if (!mGlideSfx) { // 0x000113F8
      return;
    }
    CSfxManager::RemoveEmitter(mGlideSfx);
    mGlideSfx = CSfxHandle();
    break;
  default:
    break;
  }
}

// fn_10_128D8 (0x000128D8, 0x88 bytes): the first position the actor is safe at.
//
// Nothing happens once there is one (the guest's 0x928 at 0x000128F0) or while the actor is
// unsafe (fn_10_EFC8 at 0x000128F8). Otherwise the actor's position is kept (0x91C/0x920/0x924)
// and the flag is set (0x0001292C); the guest checks the flag again (0x0001290C) and keeps the
// position without setting it when it is already set, which the first check makes unreachable.
// fn_10_128D8 (0x000128D8, 0x88 bytes): the safe position (0x91C/0x920/0x924, the flag
// byte at 0x928) and the unsafe-spot test (fn_10_EFC8). Arms: the flag is set (the
// position is kept) and the flag is clear (the unsafe-spot test runs; it answered false
// in the run, so the boss was not in an unsafe spot).
// // The arms: the coverage run (tools/cov/specs/darksamus_helpers2.py, the rooftop fight: Sanctuary 10_cliff_rooftop, boss u04f7, 6400 updates) showed both.
// @ fn_10_128D8
void CDarkSamus::UpdateSafePosition(CStateManager& mgr) {
  if (mHasSafePos != 0) {
    return;
  }
  if (IsInUnsafeSpot(mgr)) {
    return;
  }
  mSafePos = GetTranslation();
  mHasSafePos = 1;
}

// fn_10_1E178 (0x0001E178, 0xF0 bytes): the damage the actor has taken (the guest's 0xCC0).
//
// The health info's current health (the base's GetHealthInfo - the vtable slot 0x3C - and the
// float at its +4) is compared with the health of the last call (the guest's 0xCC4): when they
// differ (the fcmpu and the beq at 0x0001E1B8) and the message (the guest's argument r4) is not
// kStateMsg_Activate
// and the last health was above zero (the fcmpo and the ble at 0x0001E1C8), the health that is
// gone (0xCC4 - the current health) is added to the damage (0x0001E1F4..0x0001E200), and the last
// health is updated (0x0001E218). Then the damage decays by the rate at 0xCCC (1.0f) times dt
// (0x0001E22C..0x0001E238) and never goes below zero (0x0001E248).
// fn_10_1E178 (0x0001E178, 0xF4 bytes): the damage the actor has taken (0xCC0), the
// health at the last check (0xCC4) and the decay rate (0xCCC). Arms: the health is the
// same, the health changed and it counts (the last health above zero and the message is
// not the activate), the health changed and it does not count, and the damage decays
// below zero (it is clamped at 0x0001E248).
// // The arms: the coverage run (tools/cov/specs/darksamus_helpers2.py, the rooftop fight: Sanctuary 10_cliff_rooftop, boss u04f7, 6400 updates) showed the "same", the "does not count" and the "decays" arms; the "it
// counts" arm was forced (the last health written at the entry,
// tools/cov/specs/darksamus_helpers2_force.py).
// @ fn_10_1E178
void CDarkSamus::AccumulateDamage(EStateMsg msg, float dt) {
  const CHealthInfo* health = GetHealthInfo();
  const float current = health->GetHP();
  if (current != mLastHealth) {
    if (mLastHealth > 0.f && msg != kStateMsg_Activate) {
      mAccumulatedDamage += (mLastHealth - current);
    }
    mLastHealth = current;
  }
  mAccumulatedDamage -= mDamageDecay * dt;
  if (mAccumulatedDamage < 0.f) {
    mAccumulatedDamage = 0.f;
  }
}

// ---- the trigger bodies (stand-ins) ------------------------------------------------------------

// fn_10_F840 (0x0000F840, 0xC bytes): the flag byte 0x90C's bit 7 - the extrwi at 0x0000F844
// keeps the byte's bit 24, the mask 0x80 (the PowerPC bit 24 is 1u << (31 - 24) = 0x80). The
// flag is set by AcceptScriptMsg's 'ALRT' arm (0x0001EA1C), so both outcomes come: the
// TravelToCoverPoints node of the FSM2 evaluates this trigger (the MP2_DS_FSM dump) and the
// 'ALRT' message (the console's `send <id> Alert`) sets the bit.
// @ fn_10_F840
bool CDarkSamus::Alerted(CStateManager&, const CTriggerData&) const {
  return (mFlags90C & 0x80) != 0; // 0x0000F844 `extrwi r3, r0, 1, 24` (the bit 24 = 0x80)
}

// fn_10_62B4 (0x000062B4, 0xDC bytes): the Arrived trigger. The shield flag byte 0xCD0
// (mGuestCD0, the lbz at 0x000062C0) short-circuits the time test: with it set the trigger is
// the distance test alone (0x0000630C). Without it the float 0xF64 (mGuestF64) plus
// lbl_10_rodata_190 = 6.0f against the elapsed time 0x8B0 (0x000062D0..0x000062E4) answers false
// while the pause is still running (0x000062E8), and lbl_10_rodata_194 = 20.0f plus the same
// offset (0x000062F0..0x00006300) answers true when it is over (0x00006304). Otherwise the
// distance to the point 0xEE4/0xEE8 (the helpers branch's mGuestEE4) in the XY plane - the z
// is lbl_10_rodata_13C = 0.0f, the store at 0x00006348 overwrites the z difference - against
// lbl_10_rodata_15C = 3.0f decides (the magnitude at 0x0000634C, the compare at 0x00006358).
// The trigger is evaluated by the module's FSM2 in the GoToPhazonAttack state (the module's FSM2
// dump: "state 43 GoToPhazonAttack -> trigger Arrived -> state PhazonSuck"), so the fight calls
// it when the boss is put in that state. The arms that the fight cannot reach are written at the
// trigger's entry (gdb) in the forcing run; all five ran.
// @ fn_10_62B4
bool CDarkSamus::Arrived(CStateManager&, const CTriggerData&) const {
  if (mGuestCD0 == 0) { // 0x000062C0
    if (6.f + mGuestF64 > mElapsedTime) { // 0x000062E0
      return false;                        // 0x000062E8
    }
    if (20.f + mGuestF64 < mElapsedTime) { // 0x000062FC
      return true;                        // 0x00006304
    }
  }
  const CVector3f pos = GetTranslation();
  // 0xEE4/0xEE8 (the helpers branch's mGuestEE4, the path find's fallback destination) and the
  // z is lbl_10_rodata_13C = 0.0f (the store at 0x00006348 overwrites the z difference).
  const CVector3f flat(pos.GetX() - mGuestEE4.GetX(), pos.GetY() - mGuestEE4.GetY(), 0.f);
  return flat.Magnitude() < 3.f; // 0x00006358
}

// fn_10_1BB08 (0x0001BB08, 0x100 bytes): the AnyIncomingMissiles trigger. The near list (the
// box of half-size lbl_10_rodata_3F0 = 100.0f around the actor, 0x0001BB30..0x0001BB7C) with
// the default filter and the actor itself (BuildNearList at 0x0001BBB8); every entry is cast to
// a CEnergyProjectile (0x0001BBDC) and fn_10_1B9B0 with the weapon type 6 (kWT_Missile,
// 0x0001BBF4) must let it through (0x0001BBFC) and fn_10_145E4 must find the line to the
// projectile's position on the mesh (0x0001BC10) for the trigger to answer true (0x0001BC20).
// The trigger itself has no transition in the FSM2 (the MP2_DS_FSM dump lists it only in the
// machine's 92 triggers), but the module's own ShouldReact (fn_10_11158) calls it (0x00011210)
// and the FSM2 uses ShouldReact, so the body does run in retail.
// Run (darksamus_react.py, `trigcall ShouldReact`): no projectile, a shot the walk check takes and one from
// above it refuses (`shoot`), the filter asked with the type -1 (forced) for the shots.
// @ fn_10_1BB08
bool CDarkSamus::AnyIncomingMissiles(CStateManager& mgr, const CTriggerData&) const {
  const CVector3f& pos = GetTranslation();
  const CVector3f max(pos.GetX() + 100.f, pos.GetY() + 100.f, pos.GetZ() + 100.f);
  const CVector3f min(pos.GetX() - 100.f, pos.GetY() - 100.f, pos.GetZ() - 100.f);
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, CAABox(min, max), CMaterialFilter(), this);
  for (rstl::reserved_vector< TUniqueId, kMaxObjects >::const_iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    // The retail casts the entry and hands it to fn_10_1B9B0, which answers false for a
    // null projectile (the `mr.` test at 0x0001B9C0), so the cast's result is used as it is.
    const CEnergyProjectile* proj =
        TCastToConstPtr< CEnergyProjectile >(mgr.ObjectById(*it));
    if (!CanReflectProjectile(mgr, proj, 6)) { // kWT_Missile, 0x0001BBF4
      continue;
    }
    if (!CanWalkToPoint(proj->GetTranslation())) { // fn_10_145E4, 0x0001BC10
      continue;
    }
    return true; // 0x0001BC20
  }
  return false;
}

// fn_10_16B48 (0x00016B48, 0xC bytes): the byte at guest 0xAB8's bit 7 - the extrwi at
// 0x00016B4C keeps the byte's bit 24, the mask 0x80 (the PowerPC bit 24 is 1u << (31 - 24)).
// The only writer of that bit is the anim-event handler's Landing for the freeze beam
// (fn_10_1BEFC, 0x0001C350).
// Run (darksamus_uevent.py, `trigcall`): false, true after `uevent 12` with the freeze beam (the attack forced),
// false after the key 99 put the byte back.
// @ fn_10_16B48
bool CDarkSamus::BreakFreezeBeam(CStateManager&, const CTriggerData&) const {
  return (mGuestAB8 & 0x80) != 0;
}

// fn_10_149B4 (0x000149B4, 0x4C bytes): the dash record's flag 0x80 (0xA08's
// `extrwi r0, r0, 1, 24` at 0x000149C4) breaks the dash; otherwise not while 0xA04 is above 0 (`cmpwi; ble` at
// 0x000149DC), else the base's GetAnimOver (0x000149EC).
// @ fn_10_149B4
bool CDarkSamus::BreakDash(CStateManager& mgr, const CTriggerData& data) const {
  if (mDash.mBlocked == true) {
    return true;
  }
  if (mDash.mStepsLeft > 0) {
    return false;
  }
  return GetAnimOver(mgr, data);
}

// fn_10_11C54 (0x00011C54, 0xC bytes): the dive record's done bit (0xB20, `extrwi 1,24` = 0x80).
// @ fn_10_11C54
bool CDarkSamus::BreakDiveAttack(CStateManager&, const CTriggerData&) const {
  return mDive.mDone;
}

// fn_10_16B5C (0x00016B5C, 0x8 bytes): the byte at guest 0xA40 (the lbz at 0x00016B5C).
// NormalMissileAttack (fn_10_17448) clears it on Activate; the only writer of a non-zero value is
// the anim-event handler's Landing (fn_10_1BEFC, 0x0001C30C).
// Run (darksamus_triggers2.py, `trigcall`): true and false, 0xA40 forced; darksamus_uevent.py: true after the
// fight's own Landing event with the normal missile (dt 0, attack 0x40) and after `uevent 12`.
// @ fn_10_16B5C
bool CDarkSamus::BreakNormalMissile(CStateManager&, const CTriggerData&) const {
  return mGuestA40 != 0;
}

// fn_10_106F8 (0x000106F8, 0x1C bytes): the float at guest 0xC30 against the actor's elapsed
// time 0x8B0 (the lfs at 0x000106FC, the fcmpo at 0x00010700); the srwi at 0x00010708 takes
// the compare's "less than" bit.
// @ fn_10_106F8
bool CDarkSamus::BreakPause(CStateManager&, const CTriggerData&) const {
  return mGuestC30 < mElapsedTime;
}

// fn_10_4ABC (0x00004ABC, 0xC bytes): the byte at guest 0xF5C's bit 0x80 (`extrwi 1,24`), which
// the attack animation's Landing event sets (DoUserAnimEvent) and fn_10_452C clears.
// Run (darksamus_triggers.py, `trigcall`): false, then true after `uevent 12`.
// @ fn_10_4ABC
bool CDarkSamus::BreakPhazonAttack(CStateManager&, const CTriggerData&) const {
  return mGuestF5C80;
}

// fn_10_16B54 (0x00016B54, 0x8 bytes): the byte at guest 0xA80 (the lbz at 0x00016B54).
// SuperMissileAttack (fn_10_16ECC) clears it on Activate; the only writer of a non-zero value is
// the anim-event handler's Landing (fn_10_1BEFC, 0x0001C31C).
// Run (darksamus_triggers2.py, `trigcall`): true and false, 0xA80 forced; darksamus_uevent.py: true after
// `uevent 12` with the super missile (the attack forced).
// @ fn_10_16B54
bool CDarkSamus::BreakSuperMissile(CStateManager&, const CTriggerData&) const {
  return mGuestA80 != 0;
}

// fn_10_10634 (0x00010634, 0x24 bytes): fn_10_17D50 with the attack 1 (the melee, `li r5, 0x1`
// at 0x0001063C), the port's IsAttackEnabled.
// Run (darksamus_triggers2.py, `trigcall`): ran (true); no branch of its own.
// @ fn_10_10634
bool CDarkSamus::CanMeleeAttack(CStateManager& mgr, const CTriggerData&) const {
  return IsAttackEnabled(mgr, 1);
}

// fn_10_14284 (0x00014284): the fight script's object (fn_10_17E14) - without one the trigger
// answers false (the cmplwi at 0x00014294) - and the float at the stage info's +0x24 (the lfs at
// 0x000142A8) against zero (lbl_10_rodata_3FC): the trigger answers when the float is above
// zero (the fcmpo at 0x000142B0 and the extrwi at 0x000142B8, which keeps the compare's "greater
// than" bit).
// NaN: the extrwi keeps GT, so a NaN time answers false, as `> 0.f` does.
// Run (ds41, coverage build, rooftop, tools/drivers/darksamus_ds40.con with
// tools/cov/specs/darksamus_ds41_force.py, 5900 fast frames): ALL-ARMS - the shipped 45.0f
// (true) natural; no stage and -1.0f forced (GetBattleStage answering null / the stage's float
// written for the call).
// @ fn_10_14284
bool CDarkSamus::CanMissileDeflect(CStateManager& mgr, const CTriggerData&) const {
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  if (stage == nullptr) {
    return false;
  }
  return stage->GetMissileDeflectTime() > 0.f; // 0x000142B0 fcmpo, extrwi r3,r0,1,1 (GT)
}

// fn_10_95A8 (0x000095A8, 0x7C bytes): the DiveAfterBoost trigger. With the boost ball's bit 0x20
// of 0xEBC (`extrwi r0, r0, 1, 26` at 0x000095C8, the PowerPC bit 26 being 1u << (31 - 26) =
// 0x20; `cmplwi r0, 0x1` at 0x000095CC) the trigger answers false (0x000095D4). Otherwise the
// stage info (fn_10_17E14, 0x000095DC; null answers false at 0x000095E4) answers with its byte
// +0x78 (`lbz r0, 0x78(r3)` at 0x000095F4, `cmplwi r0, 0x1` at 0x000095F8): the bool
// unknown_0x2d7551e6, the one after the boostBall float (+0x74) in SLdrDSStageInfo.
// Run (darksamus_dsfsm.py, `trigcall`): the bit set and no stage forced; the byte 0 natural (the
// shipped value), 1 forced (true).
// @ fn_10_95A8
bool CDarkSamus::DiveAfterBoost(CStateManager& mgr, const CTriggerData&) const {
  if (mBoostBallFlag20) { // 0x000095C8
    return false;         // 0x000095D4
  }
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  if (stage == nullptr) { // 0x000095E0
    return false;
  }
  return stage->GetDiveAfterBoost(); // 0x000095F4 lbz 0x78, cmplwi 1
}

// fn_10_9314 (0x00009314, 0xDC bytes): the DoneRolling trigger. Without the boost ball actor
// (the uid 0xE38, 0x0000932C) the trigger answers false (0x0000933C). With it the roll's time
// 0xEA4 (mGuestEA4) against lbl_10_rodata_27C = 2.0f (0x00009350) answers true when it is over
// (0x00009358). With a boost hint (the uid 0xEC0, 0x00009360) the hint's byte 0xEC8
// (mGuestEC8, 0x0000936C) answers. With the boost ball's bit 0x80 of 0xEBC
// (`extrwi r0, r0, 1, 24` at 0x00009378, the PowerPC bit 24 being 1u << (31 - 24) = 0x80) the
// trigger answers true (0x00009384). With the roll's other time 0xEA8 (mGuestEA8) plus
// lbl_10_rodata_274 = 5.0f against the elapsed time (0x0000939C) it answers true when that is
// over (0x000093A8). Otherwise the distance to the point 0xEAC/0xEB0/0xEB4 (mGuestEAC,
// mGuestEB0, mGuestEB4) against lbl_10_rodata_280 = 12.0f plus the radius 0xEB8 (mGuestEB8)
// decides (the magnitude at 0x000093F0, the compare at 0x00009404: the "greater than" bit,
// `extrwi r3, r0, 1, 1`).
// @ fn_10_9314
bool CDarkSamus::DoneRolling(CStateManager&, const CTriggerData&) const {
  if (mBoostBallUid == kInvalidUniqueId) { // 0x0000932C
    return false;                          // 0x0000933C
  }
  if (mGuestEA4 > 2.f) { // 0x00009350
    return true;               // 0x00009358
  }
  if (mBoostHintUid != kInvalidUniqueId) { // 0x00009364
    return mGuestEC8 != 0;                    // 0x0000936C
  }
  if (mBoostBallFlag80) { // 0x00009378
    return true;               // 0x00009384
  }
  if (5.f + mGuestEA8 < mElapsedTime) { // 0x000093A0
    return true;                            // 0x000093A8
  }
  const CVector3f pos = GetTranslation();
  // 0xEAC/0xEB0/0xEB4 are the point (the fsm2 lane's mGuestEAC) and 0xEB8 the radius.
  const CVector3f d(pos.GetX() - mGuestEAC.GetX(), pos.GetY() - mGuestEAC.GetY(),
                    pos.GetZ() - mGuestEAC.GetZ());
  return d.Magnitude() > 12.f + mGuestEB8; // 0x00009404
}

// fn_10_F864: the trigger is the byte at 0xD4C (the lbz at 0x0000F864) - the flag the
// TurnToFacePlayer state sets when the actor faces the player.
// @ fn_10_F864
bool CDarkSamus::DoneTurning(CStateManager&, const CTriggerData&) const {
  return mTurnDone != 0;
}

// fn_10_142CC (0x000142CC, 0x28 bytes): fn_10_142F4 with lbl_10_rodata_4C4 = 0.17453292f
// (10 degrees, the lfs at 0x000142DC), the port's IsPlayerInFront.
// Run (darksamus_triggers2.py, `trigcall`): ran (true); no branch of its own.
// @ fn_10_142CC
bool CDarkSamus::FacingPlayer(CStateManager& mgr, const CTriggerData&) const {
  return IsPlayerInFront(mgr, 0.17453292f);
}

// fn_10_7B68 (0x00007B68, 0x58 bytes): the health (vtable 0x3C, its +4) plus lbl_10_rodata_14C =
// 1.0f below the health the enrage started at, 0xFC4 (the fcmpo at 0x00007BA0, `srwi 31` its
// "less than" bit). Run (darksamus_triggers.py, `trigcall`): both answers, 0xFC4 forced.
// @ fn_10_7B68
bool CDarkSamus::HitDuringEnrage(CStateManager&, const CTriggerData&) const {
  return 1.f + GetHealthInfo()->GetHP() < mGuestFC4;
}

// fn_10_17CB8 (0x00017CB8, 0x34 bytes): fn_10_17CEC (the distance to the player) against
// lbl_10_rodata_460 = 30.0f (the lfs at 0x00017CCC); the extrwi at 0x00017CD8 takes the
// compare's "greater than" bit.
// Run (darksamus_triggers2.py, `trigcall`): both answers, the player placed 60 and 1 away.
// @ fn_10_17CB8
bool CDarkSamus::OutOfMeleeRange(CStateManager& mgr, const CTriggerData&) const {
  return GetDistanceToPlayer(mgr) > 30.f;
}

// fn_10_116F8 (0x000116F8): the word at 0x984 - the fight script's picked attack id - against
// 0x20000, the id of the boost ball. The body is the cntlzw/srwi idiom over `lis r0,0x2`
// (0x20000) - `subf` - `cntlzw` - `srwi r3,r0,5` (0x000116FC..0x0001170C), which makes the result
// 1 only for an exact match.
// @ fn_10_116F8
bool CDarkSamus::PickedBoostBall(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x20000;
}

// fn_10_11760 (0x00011760): the word at 0x984 against 0x100000, the id of the freeze beam. The
// body is the cntlzw/srwi idiom over `lis r0,0x10` (0x100000) (0x00011764..0x00011774).
// Run (darksamus_triggers2.py, `trigcall`): both answers, 0x984 forced.
// @ fn_10_11760
bool CDarkSamus::PickedFreezeBeam(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x100000;
}

// fn_10_1174C (0x0001174C): the word at 0x984 - the fight script's picked attack id - against 0x20, the
// id of the dive attack. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_1174C
bool CDarkSamus::PickedDiveAttack(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x20;
}

// fn_10_117D8 (0x000117D8): the word at 0x984 - the fight script's picked attack id - against 1, the
// id of the melee attack. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_117D8
bool CDarkSamus::PickedMelee(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 1;
}

// fn_10_11710 (0x00011710): the word at 0x984 - the fight script's picked attack id - against 4, the
// id of the melee dash. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_11710
bool CDarkSamus::PickedMeleeDash(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 4;
}

// fn_10_1178C (0x0001178C): the word at 0x984 - the fight script's picked attack id - against 0x80, the
// id of the missile jump. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_1178C
bool CDarkSamus::PickedMissileJump(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x80;
}

// fn_10_11724 (0x00011724): the word at 0x984 - the fight script's picked attack id - against 0x10, the
// id of the moving scatter shot. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_11724
bool CDarkSamus::PickedMovingSShot(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x10;
}

// fn_10_117A0 (0x000117A0): the byte at 0xDEC (the fade flag) first - when it is 1 the
// trigger answers for the id 0x200000 (the compare at 0x000117B0, `lis r0,0x20`), otherwise
// for the normal missile's id 0x40 (the compare at 0x000117C4).
// Run (darksamus_triggers2.py, `trigcall`): every arm, 0xDEC and 0x984 forced.
// @ fn_10_117A0
bool CDarkSamus::PickedNormalMissile(CStateManager&, const CTriggerData&) const {
  if (mFadeOut == 1) {
    return mPickedAttack == 0x200000;
  }
  return mPickedAttack == 0x40;
}

// fn_10_11800 (0x00011800): the word at 0x984 - the fight script's picked attack id - against 0x400, the
// id of the pause. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_11800
bool CDarkSamus::PickedPause(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x400;
}

// fn_10_8680 (DOL, the REL's table only names it): the word at 0x984 against 0x400000, the id of
// the phazon attack. The body is the cntlzw/srwi idiom over `lis r0,0x40` (0x400000) -
// `subf` - `cntlzw` - `srwi r3,r0,5` (0x00008680..0x00008698). The DOL's copy is not in the
// port's sources, so the body is ported here against the host's member.
// @ fn_10_8680
bool CDarkSamus::PickedPhazonAttack(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x400000;
}

// fn_10_8114 (DOL, the REL's table only names it): the word at 0x984 against 0x800000, the id of
// the phazon enrage. The body is the cntlzw/srwi idiom over `lis r0,0x80` (0x800000) (0x00008114
// ..0x0000812C). The DOL's copy is not in the port's sources, so the body is ported here.
// @ fn_10_8114
bool CDarkSamus::PickedPhazonEnrage(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x800000;
}

// fn_10_11738 (0x00011738): the word at 0x984 - the fight script's picked attack id - against 0x8, the
// id of the scatter shot. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_11738
bool CDarkSamus::PickedScatterShot(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x8;
}

// fn_10_11778 (0x00011778): the word at 0x984 - the fight script's picked attack id - against 0x100, the
// id of the super missile. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_11778
bool CDarkSamus::PickedSuperMissile(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x100;
}

// fn_10_CDA0 (DOL, the REL's table only names it): the word at 0x984 against 0x10000, the id of
// the sweep beam. The body is the cntlzw/srwi idiom over `lis r0,0x1` (0x10000) - `subf` -
// `cntlzw` - `srwi r3,r0,5` (0x0000CDA0..0x0000CDB8). The DOL's copy is not in the port's
// sources, so the body is ported here against the host's member.
// @ fn_10_CDA0
bool CDarkSamus::PickedSweepBeam(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x10000;
}

// fn_10_117EC (0x000117EC): the word at 0x984 - the fight script's picked attack id - against 0x200, the
// id of the taunt. The body is the cntlzw/srwi idiom (the four instructions after the load),
// which makes the result 1 only for an exact match.
// @ fn_10_117EC
bool CDarkSamus::PickedTaunt(CStateManager&, const CTriggerData&) const {
  return mPickedAttack == 0x200;
}

// fn_10_112E8 (0x000112E8, 0x20 bytes): fn_10_11308, the port's IsPlayerMorphed.
// Run (darksamus_triggers2.py, `trigcall`): ran (false); no branch of its own.
// @ fn_10_112E8
bool CDarkSamus::PlayerIsMorphball(CStateManager& mgr, const CTriggerData&) const {
  return IsPlayerMorphed(mgr);
}

// fn_10_10658 (0x00010658, 0x40 bytes): fn_10_17CEC (the distance to the player) against the
// actor's own minimum attack range 0x3DC (the lfs at 0x0001066C); the srwi at 0x0001067C takes
// the compare's "less than" bit.
// @ fn_10_10658
bool CDarkSamus::PlayerTooClose(CStateManager& mgr, const CTriggerData&) const {
  return GetDistanceToPlayer(mgr) < mMinAttackRange;
}

// fn_10_FAF4 (0x0000FAF4, 0x98 bytes): no area hint of type 0xE (fn_10_FC64, 0x0000FB14) is
// "reached"; otherwise the flat distance (z = lbl_10_rodata_3FC = 0.0f, 0x0000FB5C) from the hint
// to the actor below lbl_10_rodata_438 = 3.0f (the fcmpo at 0x0000FB6C, strict).
// @ fn_10_FAF4
bool CDarkSamus::ReachedPissedOff(CStateManager& mgr, const CTriggerData&) const {
  const CScriptAIHint* hint = FindAreaHint(mgr, 0xE);
  if (hint == nullptr) { // 0x0000FB14
    return true;
  }
  const CVector3f delta = GetTranslation() - hint->GetTranslation();
  return CVector3f(delta.GetX(), delta.GetY(), 0.f).Magnitude() < 3.f;
}

// fn_10_6A40 (0x00006A40, 0x20 bytes): the enrage's timer 0xFB0 at or below lbl_10_rodata_13C =
// 0.0f (the fcmpo at 0x00006A4C, `cror eq, lt, eq`).
// Run (darksamus_triggers.py, `trigcall`): both answers, 0xFB0 forced.
// @ fn_10_6A40
bool CDarkSamus::ReadyForMissiles(CStateManager&, const CTriggerData&) const {
  return mGuestFB0 <= 0.f;
}

// fn_10_6B78 (0x00006B78, 0x28 bytes): fn_10_1AA6C, the port's GetPlayerRangeBand, is 0 (the
// player closer than the attack range): `cntlzw; srwi 5` at 0x00006B88 is "== 0" (the port had
// "== 1"). Run (darksamus_triggers.py, `trigcall`): the player placed 1 and 60 away.
// @ fn_10_6B78
bool CDarkSamus::ShouldEnragedMelee(CStateManager& mgr, const CTriggerData&) const {
  return GetPlayerRangeBand(mgr) == 0;
}

// fn_10_BB78 (0x0000BB78, 0x6C bytes): the fight script's object (fn_10_17E14) - without one
// the trigger answers true (the beq at 0x0000BBA4) - and its byte at the stage info's +0x31
// (the lbz at 0x0000BBB4), which must be 0.
// Run (darksamus_uevent.py, `trigcall`): no stage (the uid forced invalid), the shipped flag 0, the flag 1 (forced).
// @ fn_10_BB78
bool CDarkSamus::ShouldFadeIn(CStateManager& mgr, const CTriggerData&) const {
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  return stage == nullptr || !stage->GetFadeOutFlag();
}

// fn_10_BBE4 (0x0000BBE4): the fight script's object (fn_10_17E14) - without one the trigger
// answers false (the cmplwi at 0x0000BC04) - its byte at the stage info's +0x31 (the lbz at
// 0x0000BC18), which must be 1 (the cmplwi at 0x0000BC1C), and then the actor's own fade flag
// (the guest's 0xDEC, the lbz at 0x0000BC24), which must be 0.
// Run (darksamus_uevent.py, `trigcall`): no stage, the shipped flag 0, the flag 1 (forced) with 0xDEC 0 and 1 (the
// FadeIn / FadeOut events).
// @ fn_10_BBE4
bool CDarkSamus::ShouldFadeOut(CStateManager& mgr, const CTriggerData&) const {
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  if (stage == nullptr || !stage->GetFadeOutFlag()) {
    return false;
  }
  return mFadeOut == 0;
}

// fn_10_FD30 (0x0000FD30): the byte at 0x420's bit 6 (0x40 - the extrwi at 0x0000FD34 keeps
// the byte's bit 25, which is 1u << (31 - 25) = 0x40) and then the byte at 0xD58. The bit is
// the base class' own flag: fn_10_F40C (the scannable info) and fn_10_17F04 (the render
// pass) test it together with the frozen and the visible flags, so it is the alive flag
// the host keeps in GetAlive(). The byte at 0xD58 is the flag the PissedOff state clears
// (fn_10_F930) and the fight script sets.
// Run (darksamus_triggers2.py, `trigcall`): every arm, 0xD58 and the alive flag forced.
// @ fn_10_FD30
bool CDarkSamus::ShouldGetPissedOff(CStateManager&, const CTriggerData&) const {
  if (GetAlive()) {
    return mPissedOff != 0;
  }
  return false;
}

// fn_10_6A60 (0x00006A60, 0x88 bytes): false while the enrage's timer 0xFB0 is above
// lbl_10_rodata_13C = 0.0f (0x00006A84) or the health (vtable 0x3C, its +4) below
// lbl_10_rodata_19C = 65.0f (0x00006AB0); else the volleys to fire 0x1058 at most the volleys
// fired 0x105C (the signed `srawi; srwi; subfc; adde` at 0x00006AC4).
// Run (darksamus_triggers.py, `trigcall`): every arm, the timer, health and volleys forced.
// @ fn_10_6A60
bool CDarkSamus::ShouldLeaveSphere(CStateManager&, const CTriggerData&) const {
  if (mGuestFB0 > 0.f) {
    return false;
  }
  if (GetHealthInfo()->GetHP() < 65.f) {
    return false;
  }
  return mGuest1058 <= mGuest105C;
}

// fn_10_11158 (0x00011158, 0x154 bytes): the trigger's switch over the attack the actor is
// performing (the word at 0x980, read at 0x00011218) stands behind four of the module's own
// triggers, called through the handler table the vptr points at (the words at 0x208, 0x174,
// 0x184 and 0x14C of lbl_10_data_CBC - docs/module-analysis/DarkSamus_fsm.tsv): the word at
// 0x208 is WasHeavyHit (fn_10_112AC) and answers the trigger at once when it is true (the
// li r3, 1 at 0x00011198), and then the words at 0x174 (CanMissileDeflect, fn_10_14284),
// 0x184 (FacingPlayer, fn_10_142CC) and 0x14C (AnyIncomingMissiles, fn_10_1BB08) must all
// answer true (the cmplwi against 1 at 0x000111C0, 0x000111E4 and 0x00011210) or the trigger
// answers false (0x0001128C).
//
// The four answers are read as one byte: `clrlwi r0, r3, 24` clears the HIGH 24 bits and keeps
// the low byte (the mask is 0xFF, not 0xFFFFFF), and `cmplwi r0, 1` compares that byte with 1.
// The triggers return a bool, so the test is the bool itself.
//
// The switch over the attack id: 0x8 the scatter shot, 0x40 the normal missile, 0x200 the taunt,
// 0x400 the pause, 0x10000 the sweep beam and 0x200000 the fade variant of the normal missile
// answer true (0x0001127C), 0xFFFF (no attack) answers the fade flag at 0xDEC (0x00011284) and
// everything else answers false.
// Run (darksamus_react.py, `trigcall`): a heavy hit; no deflect (the shipped float 45.0f); facing away (`place
// player -20`); every attack arm with the float forced to -1.0f and a shot in the near list.
// @ fn_10_11158
bool CDarkSamus::ShouldReact(CStateManager& mgr, const CTriggerData& data) const {
  if (WasHeavyHit(mgr, data)) {
    return true;
  }
  if (!CanMissileDeflect(mgr, data) || !FacingPlayer(mgr, data) ||
      !AnyIncomingMissiles(mgr, data)) {
    return false;
  }
  switch (mCurrentAttack) {
  case 0x8:      // the scatter shot
  case 0x40:     // the normal missile
  case 0x200:    // the taunt
  case 0x400:    // the pause
  case 0x10000:  // the sweep beam
  case 0x200000: // the fade variant of the normal missile
    return true;
  case 0xFFFF:   // no attack
    return mFadeOut != 0;
  default:
    return false;
  }
}

// fn_10_6AE8 (0x00006AE8, 0x90 bytes): the health (vtable 0x3C, its +4) at most
// lbl_10_rodata_1A0 = 61.0f (0x00006B1C), else at most the phazon phase's starting health 0x1054
// less lbl_10_rodata_1A4 = 58.0f (0x00006B58).
// Run (darksamus_triggers.py, `trigcall`): every arm, the health and 0x1054 forced.
// @ fn_10_6AE8
bool CDarkSamus::ShouldRiseAgain(CStateManager&, const CTriggerData&) const {
  if (GetHealthInfo()->GetHP() <= 61.f) {
    return true;
  }
  return GetHealthInfo()->GetHP() <= mPhazonHealthMark - 58.f;
}

// fn_10_110E0 (0x000110E0, 0x3C bytes): fn_10_1A5BC (the stage's weights) into an empty list and
// mask (the stores at 0x000110F8/0x000110FC), then the mask's phazon attack bit 0x800000
// (`extrwi 1,8` at 0x00011108).
// Run (darksamus_triggers.py, `trigcall`): ran (false in the rooftop fight); no branch of its own.
// @ fn_10_110E0
bool CDarkSamus::SkipPhazonAttack(CStateManager& mgr, const CTriggerData&) const {
  SAttackWeights weights;
  weights.count = 0;
  uint mask = 0;
  CollectStageWeights(mgr, weights, &mask);
  return (mask & 0x800000) != 0;
}

// fn_10_F924: the trigger is the flag byte's bit 6 - the extrwi at 0x0000F928 keeps the byte's
// bit 25, the mask 0x40 (the PowerPC bit 25 is 1u << (31 - 25) = 0x40).
// @ fn_10_F924
bool CDarkSamus::StartsInTheAir(CStateManager&, const CTriggerData&) const {
  return (mFlags90C & 0x40) != 0;
}

// fn_10_10698 (0x00010698, 0x5C bytes): the trigger answers when the damage the actor has taken
// (the float at 0xCC0) is above the threshold (the float at 0xCC8): the fcmpo at 0x000106E8
// and the extrwi at 0x000106F0, which takes the CR0 bit 1 - the "greater than" of an fcmpo
// (bit 0 is the "less than") - so the test is 0xCC0 > 0xCC8.
//
// Two guards come first. The attack the actor is performing (the word at 0x980) must not be
// 0x8000: the cmplwi at 0x000106A0 compares the whole word with 0x8000 - the `addis r0, r4, 0x0`
// at 0x0001069C is a plain copy of the word into the register cmplwi destroys, and cmplwi
// compares the 32-bit register against the 16-bit immediate, so the test is the whole word
// against 0x00008000. And when the actor has a history (the count at 0x994, the ids at 0x998),
// the last id of the history (the word at 0x998 + (count - 1) * 4, the slwi/add at
// 0x000106BC..0x000106C8) must not be 0x8000 either (the copy at 0x000106CC and the cmplwi at
// 0x000106D0).
// Run (darksamus_triggers2.py, `trigcall`): every arm, 0x980, the history and the damage forced.
// @ fn_10_10698
bool CDarkSamus::TooMuchDamage(CStateManager&, const CTriggerData&) const {
  if (mCurrentAttack == 0x8000) {
    return false;
  }
  if (mAttackHistoryCount > 0 && mAttackHistory[mAttackHistoryCount - 1] == 0x8000) {
    return false;
  }
  return mAccumulatedDamage > mDamageThreshold;
}

// fn_10_F800 (0x0000F800, 0x40 bytes): the trigger answers when the actor has stood still for
// more than 1.2 seconds (the float at 0x964 against lbl_10_rodata_42C = 1.2f: the fcmpo at
// 0x0000F80C and the ble at 0x0000F810, so the test is 0x964 > 1.2f) - and otherwise when more
// than 0.2 seconds have passed since the last wall collision (the float at 0xC44 plus
// lbl_10_rodata_428 = 0.2f against the elapsed time at 0x8B0: the fadds at 0x0000F82C, the
// fcmpo at 0x0000F830 and the extrwi at 0x0000F838, which takes the CR0 bit 1 - the "greater
// than" of an fcmpo).
// Run (darksamus_triggers2.py, `trigcall`): every arm, 0x964 and 0xC44 forced.
// @ fn_10_F800
bool CDarkSamus::WallCollision(CStateManager&, const CTriggerData&) const {
  if (mStillTime > 1.2f) {
    return true;
  }
  return (0.2f + mLastWallHitTime) > mElapsedTime;
}

// fn_10_112AC (0x000112AC, 0x3C bytes): the elapsed time of the last hit (the guest's 0xC0C)
// must not be 0.0f (the fcmpu at 0x000112B8) and lbl_10_rodata_46C = 0.3f plus it must be
// past the actor's elapsed time 0x8B0 (the fcmpo at 0x000112D8, whose "greater than" bit the
// extrwi at 0x000112E0 takes).
// Run (darksamus_triggers2.py, `trigcall`): every arm, 0xC0C forced (0, now, now - 1).
// @ fn_10_112AC
bool CDarkSamus::WasHeavyHit(CStateManager& mgr, const CTriggerData&) const {
  if (mGuestC0C == 0.f) {
    return false;
  }
  return 0.3f + mGuestC0C > mElapsedTime;
}

// ---- the state bodies (stand-ins) --------------------------------------------------------------

// fn_10_9724 (0x00009724, 0x1FC bytes): the BoostBallFire state. Every message runs the attack
// bookkeeping with id 0x20000 (`lis r4, 0x2` at 0x00009758, the call at 0x00009760). Activate
// (0x00009764) removes the roll
// emitter (0x00009778) and clears its handle (0x00009784), turns the actor around (fn_10_9644
// at 0x00009788), takes 0xD24 = lbl_10_rodata_284 = 0.5f (0x000097AC) and sets the boost ball's
// bit 0x40 of 0xEBC (`rlwimi r3, r4, 6, 25, 25` at 0x000097B4, the PowerPC bit 25 being
// 1u << (31 - 25) = 0x40). Update (0x0000983C) deletes the boost ball (fn_10_9AC4 at
// 0x0000984C) and Deactivate (0x00009854) deletes the boost ball's particle effects (fn_10_99B8
// at 0x00009864). Then the turn toward the player
// (fn_10_16B64 at 0x00009880, the rate lbl_10_rodata_28C = 5.2359877f) and the melee attack
// command (fn_10_8764 at 0x000098E0, the severity lbl_10 = 8 = pas::kS_Eight).
// Activate also empties the element generator's auto_ptr at 0xE60/0xE64 (the old one deleted at
// 0x000097F0, the stores at 0x000097FC/0x00009804).
// Arms: all natural but one, dead: the last test's false arm (`bne` at 0x00009858 to 0x00009868,
// a message that is neither 0, 1 nor 2) - EStateMsg has only Activate/Update/Deactivate (0..2).
// @ fn_10_9724
void CDarkSamus::BoostBallFire(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("BoostBallFire", msg);
  UpdateCurrentAttack(0x20000, mgr, msg); // `lis r4, 0x2` at 0x00009758, the call at 0x00009760
  if (msg == kStateMsg_Activate) {
    CSfxManager::RemoveEmitter(mBoostBallRollSfx); // 0x00009778
    mBoostBallRollSfx = CSfxHandle();              // 0x00009784
    TurnAround();                                  // fn_10_9644, 0x00009788
    mGuestD24 = 0.5f;                              // lbl_10_rodata_284, 0x000097AC
    mBoostBallFlag40 = true;                       // 0x000097B4
    mBoostBallElementGen = nullptr;                // 0x000097F0..0x00009804
  } else if (msg == kStateMsg_Update) {
    DeleteBoostBall(mgr); // fn_10_9AC4, 0x0000984C
  } else if (msg == kStateMsg_Deactivate) {
    StopBoostBallParticles(); // fn_10_99B8, 0x00009864
  }
  TurnTowardPlayer(mgr, dt, 5.2359877f); // 0x00009880 (lbl_10_rodata_28C)
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Eight)); // 0x000098E0
}

// fn_10_B164 (0x0000B164, 0x2F4 bytes): the BoostBallInto state, the morph into the boost ball.
// The shield flag (fn_10_105F4 at 0x0000B1A4) and the collision failsafe (fn_10_1AEC0 at
// 0x0000B1B0) run first. Activate (`cmpwi r31, 0x0` at 0x0000B1C0) starts the roll emitter at the
// actor (AddEmitter with mBoostBallRollSfxId 0xE68 in the actor's area, not looped at
// 0x0000B1E8, with acoustics at 0x0000B1EC, kMedPriority, the handle to 0xE6C at 0x0000B204), clears 0xEC8 and
// the hint 0xEC0 and looks for the hint to roll to: the nearest boost hint (fn_10_B6C4,
// `cmplwi r3, 0x0` at 0x0000B220) and the first active hint it arrives at (fn_10_B458,
// 0x0000B244). With one, 0xECC is the zero vector, 0xEC0 its uid, 0xEC4 the distance to it and
// 0xED8 its value parameter clamped to lbl_10_rodata_25C = 0.0f .. lbl_10_rodata_27C = 2.0f when
// above 0.0f (`fcmpo` at 0x0000B2C8, 0x0000B2E0, 0x0000B2F4), else lbl_10_rodata_264 = 1.0f
// (0x0000B314). Update (`cmpwi r31, 0x1` at 0x0000B1B4) moves the ball and the emitter
// (fn_10_A06C). Every message then runs the attack bookkeeping with id 0x20000 (`lis r4, 0x2`,
// fn_10_1B478 at 0x0000B338) and turns: toward the player at lbl_10_rodata_2B4 = 1.7453293f
// rad/s with no hint (`cmplw r4, r0` at 0x0000B348, fn_10_16B64), else toward the hint actor at
// lbl_10_rodata_2B8 = 3.4906585f (`mr. r5, r3` at 0x0000B388, fn_10_16B8C). It clears 0xEBC
// bit 0x40 (`rlwimi r0, r8, 6, 25, 25` at 0x0000B3B4) and runs the melee attack command with
// severity 7 (0x0000B3E0; pas::kAS_MeleeAttack = 7 at 0x0000B3F8; fn_10_8764 at
// 0x0000B414).
// Arms: the fight, plus forced calls: no boost hint, no arrived hint, a hint value of 0, 3 and
// 1.5, a hint uid that is not an actor (ds45). Dead: the clamp's `0.f > value` arm (0x0000B2E8) sits under the value > 0.0f
// test of the same getter (0x0000B2C8), so it cannot run; a NaN value takes the `ble` at
// 0x0000B2CC to the 1.0f arm, as the port's plain `>` does, so the two inner compares
// (0x0000B2E4 `ble`, 0x0000B2F8 `bge`) never see one.
// @ fn_10_B164
void CDarkSamus::BoostBallInto(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("BoostBallInto", msg);
  UpdateShieldFlag(msg);
  RunCollisionFailsafe(mgr);
  if (msg == kStateMsg_Update) {
    UpdateBoostBallAtActor(mgr);
  } else if (msg == kStateMsg_Activate) {
    mBoostBallRollSfx = CSfxManager::AddEmitter(mBoostBallRollSfxId, GetTranslation(),
                                                GetCurrentAreaId().Value(), false, true,
                                                CSfxManager::kMedPriority);
    mGuestEC8 = 0;
    mBoostHintUid = kInvalidUniqueId;
    const CScriptAIHint* start = FindBoostBallHint(mgr);
    if (start != nullptr) { // 0x0000B220
      CScriptAIHint* hint = FindArrivedHint(mgr, start->GetUniqueId());
      if (hint != nullptr) { // 0x0000B244
        mGuestECC = CVector3f::Zero();
        mBoostHintUid = hint->GetUniqueId();
        mBoostHintDist = (hint->GetTranslation() - GetTranslation()).Magnitude();
        if (hint->GetValueParm() > 0.f) { // 0x0000B2C8
          const float value = hint->GetValueParm();
          float clamped;
          if (0.f > value) { // 0x0000B2E0
            clamped = 0.f;
          } else if (2.f >= value) { // 0x0000B2F4
            clamped = value;
          } else {
            clamped = 2.f;
          }
          mBoostHintValue = clamped;
        } else {
          mBoostHintValue = 1.f; // 0x0000B310
        }
      }
    }
  }
  UpdateCurrentAttack(0x20000, mgr, msg);
  if (mBoostHintUid == kInvalidUniqueId) { // 0x0000B348
    TurnTowardPlayer(mgr, dt, 1.7453293f);
  } else if (const CActor* act = TCastToConstPtr< CActor >(mgr.GetObjectById(mBoostHintUid))) {
    TurnTowardPoint(dt, act->GetTranslation(), 3.4906585f); // 0x0000B388
  }
  mBoostBallFlag40 = false; // 0x0000B3B4
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Seven));
}

// fn_10_B6C4 (0x0000B6C4, 0x1A8 bytes): the hints the actor links with kSS_InternalState6 (`lis
// r6, 0x4953` at 0x0000B6EC, any message) and keeps the nearest by squared distance (from
// lbl_10_rodata_2BC = FLT_MAX, `fcmpo cr0, f30, f31` at 0x0000B7E8) that is active (the 0x20
// bit 0x80, `cmplwi r0, 0x1` at 0x0000B768), of type 0x11 (`cmpwi r0, 0x11` at 0x0000B774),
// whose radius 0x15C holds the actor (`fcmpo cr0, f30, f0` at 0x0000B7E0: not beyond it) and
// whose chain fn_10_BA38 does not answer true for (`clrlwi. r0, r3, 24` at 0x0000B80C).
// Arms: the fight, plus the first hint inactive / of another type / with a chain answering true,
// and no object for any uid (ds43 gdb calls).
// @ fn_10_B6C4
const CScriptAIHint* CDarkSamus::FindBoostBallHint(CStateManager& mgr) const {
  const rstl::vector< TUniqueId > links = FindConnectedObjects(mgr, kSS_InternalState6, kSM_None);
  float best = 3.4028235e38f; // lbl_10_rodata_2BC (0x0000B728)
  const CScriptAIHint* found = nullptr;
  for (rstl::vector< TUniqueId >::const_iterator it = links.begin(); it != links.end(); ++it) {
    const CScriptAIHint* hint = TCastToConstPtr< CScriptAIHint >(mgr.GetObjectById(*it));
    if (hint == nullptr) { // 0x0000B750
      continue;
    }
    if (hint->GetActive() != true) { // 0x0000B768
      continue;
    }
    if (static_cast< int >(hint->GetHintType()) != 0x11) { // 0x0000B774
      continue;
    }
    const CVector3f delta = hint->GetTranslation() - GetTranslation();
    const float distSq = delta.MagSquared();
    const float radius = hint->GetRadius();
    if (distSq > radius * radius) { // 0x0000B7E4
      continue;
    }
    if (!(distSq < best)) { // `bge` at 0x0000B7EC: a NaN distance is skipped too
      continue;
    }
    if (BoostChainHasInactiveHint(mgr, hint->GetUniqueId())) { // 0x0000B80C
      continue;
    }
    best = distSq;
    found = hint;
  }
  return found;
}

// fn_10_B458 (0x0000B458, 0x118 bytes): null when uid is no hint (0x0000B48C);
// else the first active hint (`cmplwi r0, 0x1` at 0x0000B50C) among its kSS_Arrived
// links (`lis r6, 0x4152` at 0x0000B49C, any message), null when there is none (0x0000B550).
// Retail copies the returned links vector (fn_10_B570, the module's rstl::vector<TUniqueId> copy
// constructor, 0x0000B4C0) and frees the temporary (fn_10_B670, its destructor, 0x0000B4CC); the
// port's copy elision does both.
// @ fn_10_B458
CScriptAIHint* CDarkSamus::FindArrivedHint(CStateManager& mgr, TUniqueId uid) {
  const CScriptAIHint* from = TCastToConstPtr< CScriptAIHint >(mgr.GetObjectById(uid));
  if (from == nullptr) { // 0x0000B48C
    return nullptr;
  }
  const rstl::vector< TUniqueId > links = from->FindConnectedObjects(mgr, kSS_Arrived, kSM_None);
  for (rstl::vector< TUniqueId >::const_iterator it = links.begin(); it != links.end(); ++it) {
    CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(mgr.ObjectById(*it));
    if (hint == nullptr) { // 0x0000B4F4
      continue;
    }
    if (hint->GetActive() == true) { // 0x0000B50C
      return hint;
    }
  }
  return nullptr;
}

// fn_10_BA38 (0x0000BA38, 0x140 bytes): follows a hint chain for at most 15 steps (`cmpwi r29,
// 0xf` at 0x0000BB4C). A step that finds no hint at uid answers false (`cmplwi r3, 0x0` at
// 0x0000BA80); else the first of its kSS_Arrived links (`lis r31, 0x4152` at 0x0000BA4C) that is a
// hint of type 0x11 (`cmpwi r0, 0x11` at 0x0000BAEC) answers true when it is inactive (the 0x20
// bit 0x80, `extrwi. r0, r0, 1, 24` at 0x0000BAF8) and is the next step's uid when active. A
// step with no such link repeats the same uid; after 15 steps the answer is false (0x0000BB54).
// The links vector is copied and its temporary freed as in fn_10_B458 (fn_10_B570 at 0x0000BAB0,
// fn_10_B670 at 0x0000BABC).
// @ fn_10_BA38
bool CDarkSamus::BoostChainHasInactiveHint(CStateManager& mgr, TUniqueId uid) const {
  for (int step = 0; step < 15; ++step) { // 0x0000BB4C
    const CScriptAIHint* from = TCastToConstPtr< CScriptAIHint >(mgr.GetObjectById(uid));
    if (from == nullptr) { // 0x0000BA80
      return false;
    }
    const rstl::vector< TUniqueId > links =
        from->FindConnectedObjects(mgr, kSS_Arrived, kSM_None);
    for (rstl::vector< TUniqueId >::const_iterator it = links.begin(); it != links.end(); ++it) {
      const CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(mgr.ObjectById(*it));
      if (hint == nullptr) { // 0x0000BAE0
        continue;
      }
      if (static_cast< int >(hint->GetHintType()) != 0x11) { // 0x0000BAEC
        continue;
      }
      if (!hint->GetActive()) { // 0x0000BAF8
        return true;
      }
      uid = hint->GetUniqueId(); // 0x0000BB18
      break;
    }
  }
  return false;
}

// fn_10_9920 (0x00009920, 0x4C bytes): null when the ball's uid 0xE38 is kInvalidUniqueId
// (`cmplw r6, r0` at 0x00009938), else the actor it names.
// @ fn_10_9920
// Run 2026-10-21 (Sanctuary 10_cliff_rooftop, boss u04f7): 644 calls. The null arm ran on its own
// (the boost ball paths run with no ball); the actor arm was forced (gdb `set var
// mBoostBallUid.value = 0x4f7` at the second call). See docs/module-analysis/DarkSamus_helpers.md.
CActor* CDarkSamus::GetBoostBallActor(CStateManager& mgr) const {
  if (mBoostBallUid == kInvalidUniqueId) { // 0x00009938
    return nullptr;
  }
  return TCastToPtr< CActor >(mgr.ObjectById(mBoostBallUid));
}

// fn_10_996C (0x0000996C, 0x4C bytes): fn_10_9920's twin for the const callers (GetAimPosition
// fn_10_10210 at 0x0001025C/0x00010270, GetOrbitPosition fn_10_10358 at 0x00010398/0x000103AC):
// null when the ball's uid 0xE38 is kInvalidUniqueId (`cmplw r6, r0` at 0x00009984, `bne` at
// 0x00009988), else the actor GetObjectById (the const lookup, 0x000099A0) names.
// The null arm (0x0000998C) is dead: both callers run it only after fn_10_9624 found the same
// uid 0xE38 valid (`cmplwi r0, 0x1; bne` at 0x0001024C/0x00010250 and 0x00010388/0x0001038C),
// with nothing between that writes 0xE38.
// Run (ds41, darksamus_ds41_force.py): the lookup arm, the ball's uid forced to the player's in
// GetOrbitPosition / GetAimPosition (the fight never asks them while the ball rolls).
// @ fn_10_996C
const CActor* CDarkSamus::GetBoostBallActor(const CStateManager& mgr) const {
  if (mBoostBallUid == kInvalidUniqueId) { // 0x00009984
    return nullptr;
  }
  // TCastToPtr<CActor> at 0x000099A4; the host's const cast stands for it on the const lookup.
  return TCastToConstPtr< CActor >(mgr.GetObjectById(mBoostBallUid));
}

// fn_10_A06C (0x0000A06C, 0xE8 bytes): with a ball actor (`mr. r31, r3` at 0x0000A088) its
// transform moves to the actor's position offset by (lbl_10_rodata_25C = 0.0f, 0.0f,
// lbl_10_rodata_290 = -0.45f) with the actor's rotation (0x0000A0E0..0x0000A0F8); with a roll
// emitter (`cmplwi r0, 0x0` at 0x0000A100) the emitter moves to the actor along its forward
// vector (0x28/0x38/0x48) at volume 0x7F (`li r6, 0x7f` at 0x0000A120).
// Run (darksamus_boostroll.py, DSR_MODE=1/2): every arm; the emitter-less arm after DiveFromBoost.
// @ fn_10_A06C
void CDarkSamus::UpdateBoostBallAtActor(CStateManager& mgr) {
  if (CActor* ball = GetBoostBallActor(mgr)) { // 0x0000A088
    CTransform4f xf(ball->GetTransform());
    const CVector3f& pos = GetTranslation();
    xf.SetTranslation(CVector3f(pos.GetX() + 0.f, pos.GetY() + 0.f, pos.GetZ() + -0.45f));
    xf.SetRotation(GetTransform().GetRotation());
    ball->SetTransform(xf);
  }
  if (mBoostBallRollSfx) { // 0x0000A100
    CSfxManager::UpdateEmitter(mBoostBallRollSfx, GetTranslation(), GetTransform().GetForward(),
                               0x7F);
  }
}

// fn_10_A3B0 (0x0000A3B0, 0x384 bytes): the BoostBallRoll state - the boss rolls the boost ball
// toward the hint. Every message runs the attack bookkeeping with id 0x20000 (fn_10_1B478, the
// `lis r4, 0x2` at 0x0000A3EC, the call at 0x0000A3F4), the shield flag (fn_10_105F4 at
// 0x0000A408) and turns the "BoostThingy" effect on (the string at lbl_10_rodata_2E4 + 7, the
// SetEffectState at 0x0000A434).
// Activate (0x0000A464): the script state "SI20" (kSS_InternalState2, 0x0000A48C); the boost
// ball is spawned (fn_10_9E00 at 0x0000A4AC) when there is none (the uid 0xE38 is
// kInvalidUniqueId, 0x0000A49C); the actor's position goes to the ball's target 0xE98..0xEA0 and
// the roll's time 0xEA4 = 0.0f (0x0000A4B0..0x0000A4D0).
// Update (0x0000A4D8): the boost ball's bit 0x20 of 0xEBC must be clear (the `extrwi` at
// 0x0000A4DC takes the bit 26 = 1u << (31 - 26) = 0x20), the ball's actor must exist
// (fn_10_9920 at 0x0000A4EC) and the bit 0x40 of 0x318 must be set (fn_10_2F04 at 0x0000A4FC; the
// `clrlwi` at 0x0000A500 takes the bool's low byte, so the check is the bit 0x40 itself). Then
// fn_10_A1C0 (0x0000A514), the shield flag 0xCD0 is cleared (0x0000A520), the ball's actor
// takes the damage (0x0000A654) and the flags follow: the bit 0x20 of 0xEBC is set (0x0000A660)
// and the shield flag 0xCD0 becomes 1 (0x0000A668).
// Deactivate (0x0000A6DC): the script state "SI21" (kSS_InternalState3, 0x0000A704).
// Update then rolls the ball to the hint (0x0000A66C..0x0000A6D8; Activate and Deactivate skip it):
// with a hint (the uid 0xEC0, 0x0000A66C)
// fn_10_AA04 (0x0000A68C), else the actor moves by (0.0f, lbl_10_rodata_26C = 1.25f, 0.0f)
// (MoveInOneFrameOR at 0x0000A6BC); fn_10_A06C (0x0000A6C8) and fn_10_A154 (0x0000A6D4) follow.
// Run (darksamus_boostroll.py, DSR_MODE=1/2): every arm but the dead `default` (EStateMsg has three
// values). dsball (tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con) with the real
// ball: fn_10_2F04 ret 0 and 1 (`damage u0703 <n> darkburst` on the ball's collision actor), the
// damage min both ways (50 below the health - 1: 1500 -> 1450; 3000 above it: 1500 -> 1).
// @ fn_10_A3B0
void CDarkSamus::BoostBallRoll(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("BoostBallRoll", msg);
  UpdateCurrentAttack(0x20000, mgr, msg);                  // 0x0000A3F4
  UpdateShieldFlag(msg);                                   // 0x0000A408
  AnimationData()->SetEffectState(rstl::string_l("BoostThingy"), true, mgr); // 0x0000A434
  switch (msg) {
  case kStateMsg_Activate:
    SendScriptMsgs(kSS_InternalState2, mgr, kInvalidUniqueId,
                   static_cast< EScriptObjectMessage >(-1)); // 0x0000A48C
    if (mBoostBallUid == kInvalidUniqueId) {               // 0x0000A49C
      SpawnBoostBall(mgr);                                 // fn_10_9E00, 0x0000A4AC
    }
    mBoostBallPos = GetTranslation(); // 0x0000A4B0..0x0000A4CC
    mGuestEA4 = 0.f;                 // 0x0000A4D0
    break;
  case kStateMsg_Update: {
    CDarkSamusBoostBall* ball = static_cast< CDarkSamusBoostBall* >(GetBoostBallActor(mgr));
    if (mBoostBallFlag20 == false &&      // 0x0000A4DC (the bit 0x20 of 0xEBC)
        ball != nullptr &&                // 0x0000A4EC (fn_10_9920, kept in r29)
        ball->HasBeenDamaged() == true) { // 0x0000A4FC (fn_10_2F04 on the ball: the bit 0x40 of
                                          // its byte 0x318, the `clrlwi` at 0x0000A500 takes
                                          // the bool's low byte)
      StartBoostBallRoll(mgr); // fn_10_A1C0, 0x0000A514
      mGuestCD0 = 0;                                     // 0x0000A520
      // 0x0000A524..0x0000A538: the health (the vtable's slot 0x3C, the float at its +4) and
      // 0x0000A544: the health - 1.0f (lbl_10_rodata_264). 0x0000A548: the ball's float at 0x314
      // (fn_10_2EFC on r29, the call's result in f1). 0x0000A54C..0x0000A558: the `fcmpo` of that
      // value with the health - 1.0f - `bge` takes the health - 1.0f, the other way the float at
      // 0x314: the damage is min(the float at 0x314, the health - 1.0f); `bge` is taken on a NaN,
      // so the unordered case takes the health - 1.0f.
      const float health = GetHealthInfo()->GetHP();
      const float healthMinusOne = health - 1.f;
      const float rollDamage = ball->GetDamageTaken();
      const float damage = (rollDamage < healthMinusOne) ? rollDamage : healthMinusOne;
      // 0x0000A564..0x0000A5EC: the damage info - the weapon mode word 0x13 in the high half
      // (the `li r0, 0x13` and the `rlwimi` at 0x0000A560/0x0000A564) with the low half and the
      // bits 16, 17, 18, 24 and 25 cleared, so the type is 0x13 = 19 = kWT_UnknownSource and the
      // mode's flags (the charged, the comboed, the instant-kill) are all false; the damage, the
      // radius damage amount (the same value, 0x0000A568), the damage radius 0.0f, the knockback
      // 0.0f, the three sound ids 0xffff (`subi r6, r5, 0x1` with r5 = 0x10000 at 0x0000A5A0) and
      // the flags byte with its bits 0x80 (mNoImmunity, 0x0000A580) and 0x40 (mApplyRadiusDamage,
      // 0x0000A5BC) cleared.
      const CDamageInfo info(CWeaponMode(kWT_UnknownSource, false, false, false), damage, 0.f, 0.f,
                             false, false);
      // 0x0000A5B4..0x0000A5F0: the filter - the include list is the material bit 1 << 0x3B
      // (lbl_10_data_1E0 = 0x3B through the `__shl2i` at 0x0000A5F0), which is 59 =
      // kMT_Solid, the exclude list is empty and the type is 3 = kFT_IncludeExclude
      // (0x0000A5F8/0x0000A62C).
      const CMaterialFilter filter(CMaterialList(kMT_Solid), CMaterialList(),
                                   CMaterialFilter::kFT_IncludeExclude);
      // 0x0000A630..0x0000A650: all three uids are this actor's own (`lhz r0, 0x8(r30)`), so the
      // retail damages the Dark Samus herself; the ball's actor is not used.
      mgr.ApplyDamage(GetUniqueId(), GetUniqueId(), GetUniqueId(), info, filter,
                      CVector3f::Zero()); // 0x0000A654 (sZeroVector, 0x0000A600)
      mBoostBallFlag20 = true; // 0x0000A660 (the `rlwimi` at 0x0000A660 sets the bit 0x20 of 0xEBC)
      mGuestCD0 = 1;            // 0x0000A668
    }
    // The movement (0x0000A66C..0x0000A6D4) is Update's only: Activate leaves through
    // `b .L_0000A708` at 0x0000A4D4 and Deactivate's SendScriptMsgs (0x0000A704) falls into the
    // epilogue, so neither moves the actor with the transition's dt of 0.0f (a 1/dt impulse).
    if (mBoostHintUid != kInvalidUniqueId) { // 0x0000A66C (the uid 0xEC0)
      RollToBoostHint(mgr, dt); // fn_10_AA04, 0x0000A68C
    } else {
      MoveInOneFrameOR(CVector3f(0.f, 1.25f, 0.f), dt); // 0x0000A6BC
    }
    UpdateBoostBallAtActor(mgr); // fn_10_A06C, 0x0000A6C8
    RollBoostBallToPosition(dt); // fn_10_A154, 0x0000A6D4
    break;                       // 0x0000A6D8 `b .L_0000A708`
  }
  case kStateMsg_Deactivate:
    SendScriptMsgs(kSS_InternalState3, mgr, kInvalidUniqueId,
                   static_cast< EScriptObjectMessage >(-1)); // 0x0000A704
    break;
  default:
    break; // `cmpwi r29, 0x3` / `bge .L_0000A708` at 0x0000A458
  }
}

// fn_10_A1C0 (0x0000A1C0, 0x1F0 bytes): the roll's start. The hint 0xEC0 is cleared
// (kInvalidUniqueId, 0x0000A1EC); the DoneRolling trigger's record takes the elapsed time 0x8B0
// (0xEA8, 0x0000A1FC), the actor's position (0xEAC/0xEB0/0xEB4, 0x0000A204..0x0000A214) and the
// radius lbl_10_rodata_294 = 1000.0f (0xEB8, 0x0000A218). The direction from the actor to the
// player (mgr+0x14FC, 0x0000A21C) with z = lbl_10_rodata_25C = 0.0f (0x0000A238), normalized
// when it can be (`clrlwi r0, r3, 24` at 0x0000A258, `cmplwi r0, 0x1` at 0x0000A25C), is reversed
// (lbl_10_rodata_288 = -1.0f, 0x0000A274..0x0000A28C): the actor turns away from the player.
// The turn: LookAt from the facing (the transform's Y column 0x28/0x38/0x48) to it with the
// limit lbl_10_rodata_298 = 6.2831855f (0x0000A2E8), its vector part taken into the actor's frame
// (TransposeRotate at 0x0000A318), the matrix's quaternion times it (0x0000A344, 0x0000A354), the
// forces cleared (0x0000A37C) and the transform rebuilt at the position 0x54 (0x0000A38C,
// SetTransform at 0x0000A398). No arm but the Normalize test.
// Run (darksamus_boostroll.py, DSR_MODE=1/2): every arm, the Normalize-false arm forced (mode 2).
// @ fn_10_A1C0
void CDarkSamus::StartBoostBallRoll(CStateManager& mgr) {
  mBoostHintUid = kInvalidUniqueId; // 0x0000A1EC
  mGuestEA8 = mElapsedTime;         // 0x0000A1FC
  mGuestEAC = GetTranslation();     // 0x0000A204..0x0000A214
  mGuestEB8 = 1000.f;               // lbl_10_rodata_294, 0x0000A218
  CVector3f away = mgr.GetPlayer(0)->GetTranslation() - GetTranslation();
  away.SetZ(0.f);                      // lbl_10_rodata_25C, 0x0000A238
  if (away.CanBeNormalized() == true) { // 0x0000A25C
    away.Normalize();                  // 0x0000A268
  }
  away = -1.f * away; // lbl_10_rodata_288, 0x0000A274..0x0000A28C
  const CVector3f fwd = GetTransform().GetForward(); // guest 0x28/0x38/0x48
  const CQuaternion look = CQuaternion::LookAt(
      CUnitVector3f(fwd.GetX(), fwd.GetY(), fwd.GetZ()),
      CUnitVector3f(away.GetX(), away.GetY(), away.GetZ()),
      CRelAngle::FromRadians(6.2831855f)); // lbl_10_rodata_298, 0x0000A2E8
  const CQuaternion local(look.GetScalar(),
                          GetTransform().TransposeRotate(look.GetVector())); // 0x0000A318
  const CQuaternion rot = CQuaternion::FromMatrix(GetTransform()) * local; // 0x0000A344/0x0000A354
  ClearForcesAndTorques();                                  // 0x0000A37C
  SetTransform(rot.BuildTransform4f(GetTranslation()));     // 0x0000A38C, 0x0000A398
}

// fn_10_A814 (0x0000A814, 0x1F0 bytes): the boost ball's collision at a hint. The one-shot
// collision sound (guest 0xE80, `addi r7, r30, 0xe80`) plays at the actor's position 0x54 toward
// lbl_10_rodata_288 = -1.0f times sForwardVector (0x0000A84C..0x0000A870), not looped (`li r8,
// 0x0`, PlayCustomSound at 0x0000A874). With the collision 'PART' (the valid byte 0xE7C,
// `cmplwi r0, 0x0` at 0x0000A87C) a CExplosion (`new 0x180` at 0x0000A8F8) named
// "Dark Samus Boost Collision" (lbl_10_rodata_2E4 + 0x13, 0x0000A918) is made at the actor's
// transform whose translation is the position + (lbl_10_rodata_25C = 0.0f, 0.0f,
// lbl_10_rodata_29C = 1.8f) (0x0000A8A4..0x0000A8D8), scaled by lbl_10_rodata_27C = 2.0f
// (ScaleBy at 0x0000A8DC), in the actor's area (0x4) with the null connection list, active (`li
// r6, 0x1`) and kInvalidEditorId (0x0000A96C), a new uid (0x0000A97C), the flags 0 (`li r9,
// 0x0`), the model's scale (0x60, 0x0000A904..0x0000A92C), white (0x0000A988) and the player
// index -1 (`li r0, -0x1` at 0x0000A990), and added to the state manager (0x0000A9EC).
// Run (darksamus_boostroll.py, DSR_MODE=1/2): every arm, the token-absent arm forced (mode 1); the
// null `rs_new` arms are the compiler's.
// @ fn_10_A814
void CDarkSamus::PlayBoostBallCollision(CStateManager& mgr) {
  PlayCustomSound(GetTranslation(), -1.f * CVector3f::Forward(),
                  mData.mBoostBallCollisionSoundOneShot, false); // 0x0000A874
  if (mBoostBallCollisionToken.valid()) { // 0x0000A87C (the byte 0xE7C)
    CTransform4f xf(GetTransform());      // 0x0000A88C
    const CVector3f& pos = GetTranslation();
    xf.SetTranslation(CVector3f(pos.GetX() + 0.f, pos.GetY() + 0.f,
                                pos.GetZ() + 1.8f)); // 0x0000A8A4..0x0000A8D8
    xf.ScaleBy(2.f);                                 // lbl_10_rodata_27C, 0x0000A8DC
    mgr.AddObject(rs_new CExplosion(
        *mBoostBallCollisionToken, mgr.AllocateUniqueId(),
        CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId),
        rstl::string_l("Dark Samus Boost Collision"), xf, 0, GetModelData()->GetScale(),
        CColor::White(), -1)); // 0x0000A9B8, 0x0000A9EC
  }
}

// fn_10_AA04 (0x0000AA04, 0x760 bytes): the roll along the boost hints. Nothing without a hint
// at 0xEC0 (`mr. r31, r3` at 0x0000AA48). The hint's direction from the actor in the XY plane
// (z = lbl_10_rodata_25C = 0.0f, 0x0000AAA4) gives the squared distance (0x0000AA80..0x0000AAA0)
// and, normalized when it can be (0x0000AAB0), the turn: LookAt from the facing at the rate
// lbl_10_rodata_2A0 = 0.017453292f times lbl_10_rodata_2A4 = 935.99994f times dt
// (0x0000AAD8/0x0000AAF0, 0x0000AB28), its vector part in the actor's frame (0x0000AB58), by
// RotateInOneFrameOR (0x0000AB88). The actor moves forward by lbl_10_rodata_26C = 1.25f times
// lbl_10_rodata_2A8 = 60.0f times dt times the hint value 0xED8 (0x0000ABA4..0x0000ABCC,
// MoveInOneFrameOR at 0x0000ABD0).
// The path direction: the zero vector (0x0000ABD4..0x0000ABFC) plus the XY direction from the
// hint to the next active Arrived hint (fn_10_B458 at 0x0000AC08, `mr. r30, r3` at 0x0000AC0C;
// normalized when it can be, 0x0000AC60) plus the XY direction from the last hint 0xECC to the
// hint unless 0xECC is the zero vector (the `fcmpu` chain at 0x0000ACB0..0x0000ACD0, normalized
// at 0x0000AD2C), normalized when it can be (0x0000AD7C). A zero sum sets 0xEC8 = 1 and ends
// (0x0000ADC8..0x0000ADD4).
// Arrived (r29): within lbl_10_rodata_2AC = 25.0f squared (`fcmpo cr0, f31, f0; bge` at
// 0x0000ADE4/0x0000ADE8), past the plane through the hint minus k times the direction - k is
// lbl_10_rodata_284 = 0.5f, or lbl_10_rodata_264 = 1.0f when the hint's second value is below
// 0.5f (0x0000AE04/0x0000AE08) - (dot(dir, pos) >= dot(dir, hint - k dir): `cror eq, gt, eq` at
// 0x0000AE6C, `extrwi r0, r0, 1, 2` at 0x0000AE74). Not arrived but the bit 0x80 of 0xEBC set
// (`extrwi r0, r4, 1, 24` at 0x0000AE90, the PowerPC bit 24 = 1u << (31 - 24) = 0x80): arrived
// when within the 25.0f squared (0x0000AEA8), else the bit is cleared (0x0000AEB8: `rlwimi r4,
// r0, 7, 24, 24`).
// On arrival (0x0000AEC8): the hint is deactivated (the vtable's slot 0x1C = SetActive(false),
// 0x0000AEE0) and sends kSS_Arrived (`lis r4, 0x4152; addi r4, r4, 0x5256` = 0x41525256 at
// 0x0000AEE8/0x0000AEF8) with the player's uid (0x0000AEF4) and the message -1 (0x0000AF0C).
// Without a next hint 0xEC8 = 1 (0x0000B12C); with one (0x0000AF14) it becomes the hint 0xEC0
// (0x0000AF20), the last hint 0xECC is the old one's position (0x0000AF24..0x0000AF38) and the
// value 0xED8 is its value parameter clamped to lbl_10_rodata_2B0 = 0.1f .. lbl_10_rodata_27C =
// 2.0f when above 0.0f (`fcmpo` at 0x0000AF48, 0x0000AF60, 0x0000AF74), else 1.0f (0x0000AF94).
// When its second value is above 0.5f (0x0000AFA8/0x0000AFAC) the collision plays (fn_10_A814 at
// 0x0000AFB8) and the actor faces the next hint at once (the XY direction normalized at
// 0x0000B008, LookAt with lbl_10_rodata_298 = 6.2831855f at 0x0000B070, TransposeRotate at
// 0x0000B0A0, FromMatrix * it at 0x0000B0CC/0x0000B0DC, ClearForcesAndTorques at 0x0000B104,
// SetTransform at 0x0000B120). Every arrival ends with the bit 0x80 of 0xEBC cleared
// (`rlwimi r0, r3, 7, 24, 24` at 0x0000B138).
// Run (darksamus_boostroll.py, DSR_MODE=1/2): every arm, the lookups, Normalize tests, bit 0x80 and
// the hint values forced (the branch table in docs/module-analysis/DarkSamus.md, slice ds2opus).
// @ fn_10_AA04
void CDarkSamus::RollToBoostHint(CStateManager& mgr, float dt) {
  CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(mgr.ObjectById(mBoostHintUid));
  if (hint == nullptr) { // 0x0000AA48
    return;
  }
  CVector3f toHint = hint->GetTranslation() - GetTranslation();
  toHint.SetZ(0.f);                         // lbl_10_rodata_25C, 0x0000AAA4
  const float distSq = toHint.MagSquared(); // 0x0000AA80..0x0000AAA0 (z = 0.0f squared)
  if (toHint.CanBeNormalized() == true) {   // 0x0000AAB0
    toHint.Normalize();                     // 0x0000AABC
  }
  const CVector3f fwd = GetTransform().GetForward(); // guest 0x28/0x38/0x48
  const CQuaternion turn = CQuaternion::LookAt(
      CUnitVector3f(fwd.GetX(), fwd.GetY(), fwd.GetZ()),
      CUnitVector3f(toHint.GetX(), toHint.GetY(), toHint.GetZ()),
      CRelAngle::FromRadians(0.017453292f * (935.99994f * dt))); // lbl_10_rodata_2A0/2A4, 0x0000AB28
  RotateInOneFrameOR(CQuaternion(turn.GetScalar(),
                                 GetTransform().TransposeRotate(turn.GetVector())), // 0x0000AB58
                     dt); // 0x0000AB88
  MoveInOneFrameOR(CVector3f(0.f, mBoostHintValue * (1.25f * (60.f * dt)), 0.f),
                   dt); // lbl_10_rodata_26C/2A8, 0x0000ABD0

  CVector3f dir = CVector3f::Zero(); // 0x0000ABD4..0x0000ABFC
  CScriptAIHint* next = FindArrivedHint(mgr, mBoostHintUid); // fn_10_B458, 0x0000AC08
  if (next != nullptr) {                                      // 0x0000AC0C
    CVector3f toNext = next->GetTranslation() - hint->GetTranslation();
    toNext.SetZ(0.f);                       // 0x0000AC54
    if (toNext.CanBeNormalized() == true) { // 0x0000AC60
      toNext.Normalize();
    }
    dir += toNext; // 0x0000AC70..0x0000AC9C
  }
  if (mGuestECC != CVector3f::Zero()) { // 0x0000ACB0..0x0000ACE0
    CVector3f fromLast = hint->GetTranslation() - mGuestECC;
    fromLast.SetZ(0.f);                       // 0x0000AD20
    if (fromLast.CanBeNormalized() == true) { // 0x0000AD2C
      fromLast.Normalize();
    }
    dir += fromLast; // 0x0000AD3C..0x0000AD68
  }
  if (dir.CanBeNormalized() == true) { // 0x0000AD7C
    dir.Normalize();
  }
  if (dir == CVector3f::Zero()) { // 0x0000AD98..0x0000ADC8
    mGuestEC8 = 1;                // 0x0000ADD0
    return;
  }

  bool arrived = false;
  if (distSq < 25.f) { // lbl_10_rodata_2AC, 0x0000ADE8 `bge` (a NaN skips)
    float back = 0.5f; // lbl_10_rodata_284, 0x0000ADF4
    if (hint->GetValueParm2() < 0.5f) { // 0x0000AE04/0x0000AE08 `bge` (a NaN skips)
      back = 1.f;                       // lbl_10_rodata_264, 0x0000AE10
    }
    const CVector3f plane = hint->GetTranslation() - back * dir; // 0x0000AE14..0x0000AE58
    if (CVector3f::Dot(dir, GetTranslation()) >= CVector3f::Dot(plane, dir)) { // 0x0000AE6C
      arrived = true; // 0x0000AE80
    }
  }
  if (!arrived && mBoostBallFlag80 == true) { // 0x0000AE88, 0x0000AE90 (the bit 0x80 of 0xEBC)
    if (distSq < 25.f) {                      // 0x0000AEA8 `bge` (a NaN skips)
      arrived = true;                         // 0x0000AEAC
    } else {
      mBoostBallFlag80 = false; // 0x0000AEB8
    }
  }
  if (arrived != true) { // 0x0000AEC8
    return;
  }
  hint->SetActive(false); // the vtable's slot 0x1C, 0x0000AEE0
  hint->SendScriptMsgs(kSS_Arrived, mgr, mgr.GetPlayer(0)->GetUniqueId(),
                       static_cast< EScriptObjectMessage >(-1)); // 0x0000AF0C
  if (next == nullptr) { // 0x0000AF14
    mGuestEC8 = 1;       // 0x0000B12C
  } else {
    mBoostHintUid = next->GetUniqueId(); // 0x0000AF20
    mGuestECC = hint->GetTranslation();  // 0x0000AF24..0x0000AF38
    if (next->GetValueParm() > 0.f) {    // 0x0000AF48/0x0000AF4C `ble` (a NaN skips)
      const float value = next->GetValueParm();
      float clamped;
      if (0.1f > value) { // lbl_10_rodata_2B0, 0x0000AF60/0x0000AF64 `ble` (a NaN skips)
        clamped = 0.1f;
      } else if (!(2.f < value)) { // lbl_10_rodata_27C, 0x0000AF74/0x0000AF78 `bge` (NaN too)
        clamped = value;
      } else {
        clamped = 2.f;
      }
      mBoostHintValue = clamped; // 0x0000AF84
    } else {
      mBoostHintValue = 1.f; // lbl_10_rodata_264, 0x0000AF94
    }
    if (next->GetValueParm2() > 0.5f) { // lbl_10_rodata_284, 0x0000AFA8/0x0000AFAC `ble`
      PlayBoostBallCollision(mgr);      // fn_10_A814, 0x0000AFB8
      CVector3f toNext = next->GetTranslation() - GetTranslation();
      toNext.SetZ(0.f);                       // 0x0000AFFC
      if (toNext.CanBeNormalized() == true) { // 0x0000B008
        toNext.Normalize();
      }
      const CVector3f face = GetTransform().GetForward(); // guest 0x28/0x38/0x48
      const CQuaternion look = CQuaternion::LookAt(
          CUnitVector3f(face.GetX(), face.GetY(), face.GetZ()),
          CUnitVector3f(toNext.GetX(), toNext.GetY(), toNext.GetZ()),
          CRelAngle::FromRadians(6.2831855f)); // lbl_10_rodata_298, 0x0000B070
      const CQuaternion local(look.GetScalar(),
                              GetTransform().TransposeRotate(look.GetVector())); // 0x0000B0A0
      const CQuaternion rot = CQuaternion::FromMatrix(GetTransform()) * local; // 0x0000B0DC
      ClearForcesAndTorques();                              // 0x0000B104
      SetTransform(rot.BuildTransform4f(GetTranslation())); // 0x0000B114, 0x0000B120
    }
  }
  mBoostBallFlag80 = false; // 0x0000B138
}

// fn_10_16CDC (0x00016CDC, 0x1F0 bytes): the FreezeBeam state. Every message runs the attack
// bookkeeping with id 0x100000 (`lis r4, 0x10` at 0x00016D10, the call at 0x00016D18), the
// collision failsafe (fn_10_1AEC0 at
// 0x00016D24), the shield flag (fn_10_105F4 at 0x00016D38) and the ground collision
// (fn_10_18F90 at 0x00016D48). Activate (0x00016D4C) clears the beam's counter 0xAB0
// (0x00016D5C), clears the bits 0x40 and 0x80 of 0xAB8 (the `rlwimi` at 0x00016D6C clears the
// bit 0x40 - the PowerPC bit 25 - and the one at 0x00016D7C sets the bit 0x80 - the PowerPC bit
// 24 - to the bit 0x40's value, which the first one just cleared) and zeroes the dodge record
// (0x1090 = 0.0f at 0x00016D9C and the direction 0x1094..0x109C at 0x00016D88..0x00016D98). The
// other messages deliver the exit state command (fn_800EE45C at 0x00016DD0, the command id
// 0xA = 10 = kBSC_ExitState) when the counter 0xAB0 is at least 1 (0x00016DA8). With the bit
// 0x40 of 0xAB8 set (0x00016DEC..0x00016DF4) the actor turns toward the player (fn_10_16B64 at
// 0x00016E1C, the rate lbl_10_rodata_454 = 0.017453292 radians a degree times
// lbl_10_bss_118 = 160.0f, the module's own turn speed) and dodges (fn_10_1707C at 0x00016E3C,
// with lbl_10_bss_11C = 22.0f and lbl_10_bss_120 = 0.5f - the module's init fn_10_CDB8 writes
// them at 0x0000CE38 and 0x0000CE3C - the port's UpdateDodge). Then
// fn_10_8870 (0x00016E8C) - the state's command, the port's RunLoopAttackCmd.
// The arms: the Activate (0x00016D4C), the Update, the Deactivate and the exit state command
// (the counter 0xAB0 at least 1, 0x00016DA8) and the turn and the dodge (the bit 0x40 of 0xAB8,
// 0x00016DF0) - the last two need the members written at the Update's entry, because the
// Activate clears both (0x00016D5C and 0x00016D6C).
// @ fn_10_16CDC
void CDarkSamus::FreezeBeam(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("FreezeBeam", msg);
  UpdateCurrentAttack(0x100000, mgr, msg); // `lis r4, 0x10` at 0x00016D10, the call at 0x00016D18
  RunCollisionFailsafe(mgr);           // 0x00016D24
  UpdateShieldFlag(msg);              // 0x00016D38
  UpdateGroundCollision(mgr, msg);    // 0x00016D48
  if (msg == kStateMsg_Activate) {
    mGuestAB0 = 0;                    // 0x00016D5C
    mGuestAB8 &= 0x3F;                // 0x00016D6C, 0x00016D7C (the bits 0x40 and 0x80)
    mDodgeTime = 0.f;                 // 0x00016D9C
    mDodgeDir = CVector3f::Zero();    // 0x00016D88
  } else if (mGuestAB0 >= 1) {        // 0x00016DA8
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState)); // 0x00016DD0
    return; // the b to the epilogue at 0x00016DE8: no turn, no loop attack command
  }
  if ((mGuestAB8 & 0x40) != 0) { // 0x00016DF0 (the bit 0x40 of 0xAB8)
    TurnTowardPlayer(mgr, dt, 0.017453292f * mBss118); // 0x00016E1C (lbl_10_rodata_454 times
                                                              // lbl_10_bss_118 = 160.0f)
    // 0x00016E3C: fn_10_1707C - the dodge step, with lbl_10_bss_11C = 22.0f (the impulse) and
    // lbl_10_bss_120 = 0.5f (the dodge's time) - the module's init fn_10_CDB8 writes them at
    // 0x0000CE38 and 0x0000CE3C - the port's UpdateDodge.
    UpdateDodge(mgr, dt, 22.f, 0.5f); // 0x00016E3C
  }
  // 0x00016E40..0x00016E88: the loop attack command - the vtable lbl_10_data_1B0 (the base
  // lbl_10_data_1D4), the id 7 = kBSC_LoopAttack (0x00016E70), the type 4 = pas::kLAT_Four
  // (0x00016E80), the wait flag 0 (0x00016E84) and the byte with its bit 0x80 (the skip-into)
  // cleared (`rlwimi r0, r7, 7, 24, 24` at 0x00016E54). 0x00016E78's `li r5, 0x9` is fn_10_8870's
  // animation state (pas::kAS_LoopAttack), not a command id.
  const CBCLoopAttackCmd attack(pas::kLAT_Four, false);
  RunLoopAttackCmd(msg, attack); // fn_10_8870, 0x00016E8C
}

// fn_10_150DC (0x000150DC, 0x26C bytes): the Dash state. Activate (`mr. r31, r5` at 0x000150EC)
// clears the double-dash flag (0x40, 0x00015110) and sets it when the stage (fn_10_17E14, the null
// test at 0x00015120) rolls Range(0.0f, lbl_10_rodata_3F0 = 100.0f) below its chanceToDoubleDash
// (the stage info's +0x20, `fcmpo; bge` at 0x00015144). A double dash is fn_10_14D14 (0x00015178).
// Otherwise Activate (0x00015184) starts from a fresh dash record (fn_10_15348 on the stack, copied
// in at 0x000151A0..0x000151F0), pushes the best cover point (fn_10_1E3C0 / fn_10_1E26C at
// 0x000151F4/0x00015208) and picks a dash (fn_10_154FC at 0x00015298, eight weights of
// lbl_10_rodata_3E8 = 1.0f) toward the cover point (GetCoverPoint of 0xC24 at 0x0001524C; without
// one the actor's own position); a pick of weight 0.0f blocks the dash (0x80 at 0x000152B4) and
// runs the collision failsafe (0x000152C4). Then the stuck count as in fn_10_14FE0
// (0x000152C8..0x00015308). Every message, unless blocked (`extrwi.` at 0x00015310), runs the dash
// step (fn_10_12CE0 at 0x00015324).
// Arms: the fight, plus no stage, a double dash, stuck once/twice, no cover point and a pick of
// weight 0 (ds43 gdb calls).
// @ fn_10_150DC
void CDarkSamus::Dash(CStateManager& mgr, EStateMsg msg, float) {
  DsStateEntered("Dash", msg);
  if (msg == kStateMsg_Activate) {
    mDash.mDoubleDash = false;
    const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
    if (stage != nullptr && // `mr. r28, r3` at 0x0001511C/0x00015120
        mgr.Random()->Range(0.f, 100.f) < stage->GetStageInfo().chanceToDoubleDash) {
      mDash.mDoubleDash = true;
    }
  }
  if (mDash.mDoubleDash == true) {
    UpdateDoubleDash(mgr, msg);
    return;
  }
  if (msg == kStateMsg_Activate) {
    mDash = CDarkSamusDash();
    PushAttacker(FindBestCoverPoint(mgr));
    float weights[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    CVector3f target = GetTranslation();
    if (const CScriptCoverPoint* cover = GetCoverPoint(mgr, mLastAttackerUid)) {
      target = cover->GetTranslation();
    }
    if (0.f == PickDash(mgr, weights, &target)) {
      mDash.mBlocked = true;
      RunCollisionFailsafe(mgr);
    }
    if (mStillTime > 1.2f) {
      ++mDashStuckCount;
      if (mDashStuckCount >= 2) {
        ApplyDashStuckBlast(mgr);
      }
    } else {
      mDashStuckCount = 0;
    }
  }
  if (mDash.mBlocked == false) {
    UpdateDashStep(mgr, msg);
  }
}

// fn_10_11FA4 (0x00011FA4, 0x764 bytes): the DiveAttack state, a jump at the player that lands
// on the dive target and bounces off walls. Every message runs the attack bookkeeping with id 0x20
// (fn_10_1B478 at 0x00011FEC), the shield flag (fn_10_105F4, 0x00012000) and the ground collision
// (fn_10_18F90, 0x00012010), and the collision failsafe unless the dive starts from the boost ball
// (`clrlwi.` at 0x00012018). The jump type is 3 from the boost ball, else 0 (0x00012038).
// Activate (0x00012068) resets the dive record (fn_10_E910), takes the position for the stuck
// timer, sets the animation state to 1 (kAS_Ready, 0x00012098), picks the target (fn_10_12B38,
// fn_10_12960), aims the body at it (fn_10_12B00) and delivers the jump (0x000120FC).
// Update (0x0001211C, the stage fn_10_17E14): after a bounce (0x00012138) the actor falls along
// the bounce direction and nothing else runs; before the launch (0x000122E8) the target follows
// the player and the actor turns to it; the jump is re-delivered while CanIssueCommand(kAS_Jump)
// answers true (0x0001239C); the animation's end (0x000123F0) or the floor (0x00012420) ends the
// flight; in flight (0x00012488..0x0001249C) the push toward the target, then a wall bounces
// (0x0001252C) or the stuck timer runs; the landing (0x00012568) sends the attack and stops on
// the ground; the projectile reflect (0x000125CC..0x000125F0) and, before the launch, the dodge
// (0x00012608..0x00012644). Deactivate (0x0001266C) stops the actor, clears the animation state
// and 0xC00, delivers kBSC_NextState and turns the bubble off.
// The static wall hit (0x00012534) needs the dive record's boost bit 0x01 (0xB20), which
// DiveFromBoost fn_10_9424 sets (`rlwimi r0, r3, 0, 31, 31` at 0x00009460, ported): without
// the bit the collision failsafe (`clrlwi.` at 0x00012018) runs first in the same message and
// puts an actor placed in a wall back at its safe position. With the bit set - the boost
// ball's dive, forced with `fsm <id> DiveFromBoost` - an actor placed into the wall while it
// is in flight reaches the static collision test and the wall bounce runs.
// @ fn_10_11FA4
void CDarkSamus::DiveAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("DiveAttack", msg);
  UpdateCurrentAttack(0x20, mgr, msg);
  UpdateShieldFlag(msg);
  UpdateGroundCollision(mgr, msg);
  if (mDive.mFromBoost != true) { // 0x0001201C
    RunCollisionFailsafe(mgr);
  }
  switch (msg) {
  case kStateMsg_Activate:
    mDive.Reset();
    mDive.mLastPos = GetTranslation();
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    mDive.mTarget = GetDiveTarget(mgr);
    FindDiveLanding(mgr, &mDive.mTarget);
    SetDiveTargetVector();
    DoDiveJumpCmd(); // fn_10_12764 (0x000120FC)
    break;
  case kStateMsg_Update: {
    const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
    if (mDive.mBounced == true) { // 0x00012138
      mDive.mBounceTime += dt;
      if (mDive.mDone) { // 0x0001214C
        return;
      }
      BodyController()->CommandMgr().DeliverCmd(kBSC_Unknown19); // `li r4, 0x13` at 0x0001217C
      CVector3f vel(0.f, 0.f, -1.f * GetGravityConstant()); // lbl_10_rodata_3EC, 0x000121D8
      if (mDive.mBounceTime < 0.6f) { // lbl_10_rodata_424, `fcmpo; bge` at 0x000121EC
        // lbl_10_rodata_3E8 = 1.0f, lbl_10_rodata_488 = 45.0f
        vel += 45.f * ((1.f - mDive.mBounceTime) * mDive.mBounceDir);
      } else {
        vel.SetZ(vel.GetZ() * 1.4f); // lbl_10_rodata_48C, 0x00012254
        CVector3f flat(mDive.mBounceDir.GetX(), mDive.mBounceDir.GetY(), 0.f);
        if (flat.CanBeNormalized() == true) { // 0x00012280
          vel += 10.f * flat.AsNormalized(); // lbl_10_rodata_404
        }
      }
      SetVelocityWR(vel);
      return;
    }
    if (!mDive.mLaunched) { // 0x000122E8
      CVector3f target = GetDiveTarget(mgr);
      if (FindDiveLanding(mgr, &target) == true) { // 0x0001232C
        mDive.mTarget = target;
      }
      SetDiveTargetVector();
      // the stage null test at 0x00012358, its +0x58 at 0x00012364: lbl_10_rodata_490 /
      // lbl_10_rodata_494
      const float rate =
          stage != nullptr && stage->GetStageInfo().unknown_0xb2c1e4fa == true ? 3.8397243f
                                                                               : 2.0943952f;
      TurnTowardPoint(dt, mDive.mTarget, rate);
    }
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Jump)) { // `li r5, 0xd`
      DoDiveJumpCmd();
    }
    if (mAnimationState.GetState() == CAnimationState::kAS_Over) { // `cmpwi r0, 0x3` at 0x000123F0
      const float stuckTime = mDive.mStuckTime;
      mDive.Reset();
      mDive.mStuckTime = stuckTime;
      mDive.mDone = true;
    } else if (mDive.mHitFloor == true) { // 0x00012420
      BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_ExitState)); // `li r0, 0xa`
      mDive.mLanding = true;
      mDive.mHitFloor = false;
    }
    if (mDive.mLaunched == true && !mDive.mDone && !mDive.mLanding) { // 0x0001248C..0x0001249C
      // the stage null test at 0x000124A8: lbl_10_rodata_498 = 1.15f, else lbl_10_rodata_3E8
      UpdateDiveFlight(stage != nullptr && stage->GetStageInfo().unknown_0xb2c1e4fa == true
                           ? 1.15f
                           : 1.f);
      // lbl_10_data_CB4 = 0x1E (kMT_Wall), include-only (`li r5, 0x1` at 0x000124E8)
      if (CGameCollision::DetectStaticCollisionBoolean(
              mgr, *GetCollisionPrimitive(), GetTransform(),
              CMaterialFilter::MakeInclude(CMaterialList(kMT_Wall))) == true) { // 0x00012534
        BounceDive(mgr, CVector3f::Zero());
        return;
      }
      UpdateDiveStuck(mgr, dt);
    }
    if (mDive.mLanding == true) { // 0x0001256C
      SendDiveAttack(mgr);
      StopDiveBubble(mgr);
      SetGrounded(mgr, false);
      SetVelocityWR(CVector3f(0.f, 0.f, -20.f)); // lbl_10_rodata_49C
    }
    if (mDive.mAttackSent == true && !mDive.mBounced && // 0x000125D0, 0x000125D8
        stage != nullptr && stage->GetStageInfo().unknown_0xf5cf3c0f == true) { // 0x000125E0/EC
      mGuestC00 = mMinAttackRange;
      ReflectProjectiles(mgr);
    }
    if (!mDive.mDone && !mDive.mLanding && !mDive.mAttackSent && !mDive.mLaunched &&
        !mDive.mBounced && // 0x0001260C..0x0001262C
        stage != nullptr && stage->GetStageInfo().unknown_0xb2c1e4fa == true) { // 0x00012634/40
      UpdateDodge(mgr, dt, 30.f, 0.35f); // lbl_10_rodata_460, lbl_10_rodata_4A0
    }
    break;
  }
  case kStateMsg_Deactivate:
    SetVelocityWR(CVector3f::Zero());
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    mGuestC00 = 0.f;
    BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_NextState)); // `li r0, 0xc`
    StopDiveBubble(mgr);
    break;
  default:
    break;
  }
}

// fn_10_E784 (0x0000E784, 0x18C bytes): the vectors to zero, the damage copied to +0x30
// (0x0000E83C..0x0000E85C), the two impulses to +0x50/+0x54 (0x0000E860/0x0000E864), the
// boost-ball bit cleared (0x0000E86C), the collidable box null with the material list
// 1 << lbl_10_data_CB8 = 0x3B, kMT_Solid (__shl2i at 0x0000E880, the ctor at 0x0000E8A8), and
// fn_10_E910 (0x0000E8F0).
// @ fn_10_E784
CDarkSamusDive::CDarkSamusDive(const CDamageInfo& damage, float impulseHorizontal,
                               float impulseVertical)
: mTarget(CVector3f::Zero())
, mStart(CVector3f::Zero())
, mBounceDir(CVector3f::Zero())
, mLastPos(CVector3f::Zero())
, mDamage(damage)
, mImpulseHorizontal(impulseHorizontal)
, mImpulseVertical(impulseVertical)
, mBounceTime(0.f)
, mStuckTime(0.f)
, mFromBoost(false)
, mCollidable(CAABox::MakeNullBox(), CMaterialList(kMT_Solid)) {
  Reset();
}

// fn_10_E910 (0x0000E910, 0xF8 bytes): every flag but the boost-ball bit cleared (the rlwimi
// chain 0x0000E918..0x0000E990 copies a cleared 0x02 down to 0x80), the four vectors to zero,
// the record's float +0x4C to lbl_10_rodata_3EC = -1.0f (0x0000E9E0) and both timers to
// lbl_10_rodata_3FC = 0.0f (0x0000E9E4/0x0000E9E8).
// @ fn_10_E910
void CDarkSamusDive::Reset() {
  mBounced = false;
  mAttackSent = false;
  mHitFloor = false;
  mLanding = false;
  mHitPlayer = false;
  mLaunched = false;
  mDone = false;
  mBounceDir = CVector3f::Zero();
  mStart = mBounceDir;
  mTarget = mStart;
  mStuckTime = 0.f;
  mBounceTime = 0.f;
  mLastPos = CVector3f::Zero();
  mGuest4C = -1.f; // 0x0000E9E0 (lbl_10_rodata_3EC = -1.0f)
}

// fn_10_12960 (0x00012960, 0x1A0 bytes): the flat line from the target back to the actor
// (normalized when it can be, 0x000129F0), stepped by lbl_10_rodata_3E4 = 0.5f from
// lbl_10_rodata_3FC = 0.0f while within lbl_10_rodata_414 = 8.0f (`fcmpo; cror` at 0x00012AC4);
// the first point with a clear ring path (fn_10_16600 with lbl_10_rodata_3E8 = 1.0f and
// lbl_10_rodata_4A4 = 1.7f, 0x00012A60) becomes the target.
// Arms: the target on the actor (no flat line), every ring path blocked, a clear path (ds43 gdb
// calls).
// @ fn_10_12960
bool CDarkSamus::FindDiveLanding(CStateManager& mgr, CVector3f* target) const {
  CVector3f dir = GetTranslation() - *target;
  dir.SetZ(0.f);
  if (dir.CanBeNormalized() == true) {
    dir.Normalize();
  }
  for (float f = 0.f; f <= 8.f; f += 0.5f) {
    const CVector3f candidate = *target + f * dir;
    if (IsRingPathClear(mgr, candidate, 1.f, 1.7f) == true) {
      *target = candidate;
      return true;
    }
  }
  return false;
}

// fn_10_12B00 (0x00012B00, 0x38 bytes): SetTargetVector(target - translation).
// @ fn_10_12B00
void CDarkSamus::SetDiveTargetVector() {
  BodyController()->CommandMgr().SetTargetVector(mDive.mTarget - GetTranslation());
}

// fn_10_1285C (0x0001285C, 0x7C bytes) builds the CBCJumpCmd - the dive target, the jump type
// (0 or 3), kJS_IntoJump (`li r6, 0x0`), animation variant 0 (`li r7, 0x0`) and facing flags 2
// (`li r8, 0x2`) - and fn_10_12764 copies it into the command manager's jump slot after
// DeliverCmd(kBSC_Jump), which is DeliverCmd(const CBodyStateCmd&) on the host.
// @ fn_10_1285C
// @ fn_10_12764
void CDarkSamus::DoDiveJumpCmd() {
  BodyController()->CommandMgr().DeliverCmd(
      CBCJumpCmd(mDive.mTarget, mDive.mFromBoost == true ? pas::kJT_Three : pas::kJT_Normal,
                 pas::kJS_IntoJump, 0, 2));
}

// fn_10_11C60 (0x00011C60, 0x17C bytes): the direction from the launch point to the target
// (normalized when it can be, 0x00011CD8); below lbl_10_rodata_418 = 50.0f * scale of speed
// (`fcmpo; bge` at 0x00011CF8) the velocity becomes 50 * scale along it, else below
// lbl_10_rodata_480 = 80.0f * scale (0x00011D60) an impulse of lbl_10_rodata_484 = 800.0f * scale.
// Arms: the zero direction (CanBeNormalized false) forced (the target = the start,
// tools/cov/specs/darksamus_ds39_force.py); the rest natural (DiveAttack).
// @ fn_10_11C60
void CDarkSamus::UpdateDiveFlight(float scale) {
  CVector3f dir = mDive.mTarget - mDive.mStart;
  if (dir.CanBeNormalized() == true) {
    dir.Normalize();
  }
  if (GetVelocityWR().Magnitude() < 50.f * scale) {
    SetVelocityWR(scale * (50.f * dir));
  } else if (GetVelocityWR().Magnitude() < 80.f * scale) {
    ApplyImpulseWR(scale * (800.f * dir), CAxisAngle::Identity());
  }
}

// fn_10_1CDD0 (0x0001CDD0, 0x1B4 bytes): the bounce (0x02 at 0x0001CDF8), the bubble off
// (fn_10_12E90), kBSC_Unknown19 (`li r4, 0x13` at 0x0001CE2C); a flat normal that can be
// normalized (0x0001CE90) turns the actor to face away from it (LookAt of lbl_10_rodata_3EC =
// -1.0f times it, 0x0001CEE4; the host's SetTransform stands in for retail's SetTransformAlt,
// which the host CActor lacks). The bounce direction is the flat normal with z
// lbl_10_rodata_3E0 = 2.0f, normalized (0x0001CF20); then vertical movement on, off the ground
// and off static ground (0x0001CF48..0x0001CF60).
// Arms: no normal (via 11EB8) and a flat normal (ds43 gdb calls).
// @ fn_10_1CDD0
void CDarkSamus::BounceDive(CStateManager& mgr, const CVector3f& normal) {
  mDive.mBounced = true;
  StopDiveBubble(mgr);
  BodyController()->CommandMgr().DeliverCmd(kBSC_Unknown19);
  CVector3f dir(normal.GetX(), normal.GetY(), 0.f);
  if (dir.CanBeNormalized() == true) {
    dir.Normalize();
    SetTransform(CQuaternion::FromMatrix(
                     CTransform4f::LookAt(CVector3f::Zero(), -1.f * dir, CVector3f::Up()))
                     .BuildTransform4f(GetTranslation()));
  }
  dir.SetZ(2.f);
  dir.Normalize();
  mDive.mBounceDir = dir;
  mVerticalMovement = true;
  mOnGround = false;
  mOnStaticGround = false;
}

// fn_10_11EB8 (0x00011EB8, 0xEC bytes): a move of more than lbl_10_rodata_428 = 0.2f since the
// last position (`fcmpo; ble` at 0x00011F20) restarts the stuck timer there; otherwise the timer
// runs and past lbl_10_rodata_3E4 = 0.5f (0x00011F68) the actor bounces with no normal.
// Arms: moved, still under 0.5 s, stuck past 0.5 s (ds43 gdb calls).
// @ fn_10_11EB8
void CDarkSamus::UpdateDiveStuck(CStateManager& mgr, float dt) {
  if ((mDive.mLastPos - GetTranslation()).Magnitude() > 0.2f) {
    mDive.mLastPos = GetTranslation();
    mDive.mStuckTime = 0.f;
  } else {
    mDive.mStuckTime += dt;
    if (mDive.mStuckTime > 0.5f) {
      BounceDive(mgr, CVector3f::Zero());
    }
  }
}

// fn_10_11DDC (0x00011DDC, 0xDC bytes): once (0x04, 0x00011E00): kSS_Attack to the links
// (0x00011E2C) and the 0x04 bit; then, unbounced (0x00011E44) on a stage (the null test at
// 0x00011E5C) whose +0x59 is set (0x00011E70), fn_10_18628 with lbl_10_rodata_3FC = 0.0f and
// lbl_10_rodata_258 = 5.0f; otherwise the shield byte 0xCD0 is cleared (0x00011E9C).
// Arms: already sent, bounced, no stage, +0x59 clear, +0x59 set (ds43 gdb calls).
// @ fn_10_11DDC
void CDarkSamus::SendDiveAttack(CStateManager& mgr) {
  if (mDive.mAttackSent == true) {
    return;
  }
  SendScriptMsgs(kSS_Attack, mgr, kInvalidUniqueId, static_cast< EScriptObjectMessage >(-1));
  mDive.mAttackSent = true;
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  if (!mDive.mBounced && stage != nullptr && stage->GetStageInfo().unknown_0xf5cf3c0f == true) {
    SetGuestE04(0.f, 5.f);
  } else {
    mGuestCD0 = 0;
  }
}

// fn_10_12E90 (0x00012E90, 0x68 bytes) / fn_10_12EF8 (0x00012EF8, 0x68 bytes): SetEffectState of
// "DiveBubble" (lbl_10_rodata_560 + 0x52F) false / true.
// @ fn_10_12E90
void CDarkSamus::StopDiveBubble(CStateManager& mgr) {
  AnimationData()->SetEffectState(rstl::string_l("DiveBubble"), false, mgr);
}

// @ fn_10_12EF8
void CDarkSamus::StartDiveBubble(CStateManager& mgr) {
  AnimationData()->SetEffectState(rstl::string_l("DiveBubble"), true, mgr);
}

// fn_10_1B658 (0x0001B658, 0x2E0 bytes): the projectile reflect. The near list (0x0001B708) is the
// box of half-size lbl_10_rodata_3F0 = 100.0f around the actor (lbl_10_rodata_54C = -100.0f for
// the low corner, 0x0001B680..0x0001B6CC) with the default filter (include 0xFFFFFFFF, type 0,
// 0x0001B6D4..0x0001B704) and the actor itself; each entry is cast to a CEnergyProjectile
// (0x0001B730) and skipped unless fn_10_1B9B0 with any weapon type (`li r6, -0x1` at 0x0001B748)
// lets it through (0x0001B750) and it is within 0xC00 of the root bone 0x8FC (Magnitude at
// 0x0001B798, `fcmpo; bgt` at 0x0001B7A4). Then the ricochet sound 0xC50 plays at the projectile
// in the actor's area 0x4 (AddEmitter at 0x0001B7C8: acoustics `li r7, 0x1` at 0x0001B7C0, not looped
// `li r8, 0x0` at 0x0001B7C4, kMedPriority at 0x0001B7B0). The new direction is the projectile's
// forward (its transform's column 1, 0x0001B7CC..0x0001B7E4) with x and y each moved by
// lbl_10_rodata_520 = 0.4f - lbl_10_rodata_550 = 0.8f * random (`fnmsubs` at 0x0001B804 and
// 0x0001B82C) and z lowered by lbl_10_rodata_3E4 = 0.5f * random + lbl_10_rodata_474 = 0.05f
// (`fmadds` at 0x0001B854); normalized when it can be (0x0001B868), else the forward again
// (0x0001B87C). The projectile explodes there with response 0xD (`li r6, 0xd` at 0x0001B8C4), the
// reflect vulnerability (fn_800DBB40 at 0x0001B8A0) and no hit actor (kInvalidUniqueId at
// 0x0001B898) through its vtable 0x8C (Explode, 0x0001B8B4). Last 0xC08 takes the elapsed time
// (0x0001B8D4), the oldest uid goes when five are kept (`cmpwi r0, 0x5` at 0x0001B8DC,
// fn_10_1B938 at 0x0001B8EC) and the projectile's uid is appended (0x0001B8FC).
// The not-normalizable arm is dead: the forward is a unit vector and the largest offset is
// |(0.4, 0.4, 0.55)|, whose square 0.6225 is below 1, so the sum is never zero.
// @ fn_10_1B658
void CDarkSamus::ReflectProjectiles(CStateManager& mgr) {
  const CVector3f& pos = GetTranslation();
  const CVector3f max(pos.GetX() + 100.f, pos.GetY() + 100.f, pos.GetZ() + 100.f);
  const CVector3f min(pos.GetX() + -100.f, pos.GetY() + -100.f, pos.GetZ() + -100.f);
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, CAABox(min, max), CMaterialFilter(), this);
  for (rstl::reserved_vector< TUniqueId, kMaxObjects >::iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    CEnergyProjectile* proj = TCastToPtr< CEnergyProjectile >(mgr.ObjectById(*it));
    if (!CanReflectProjectile(mgr, proj, -1)) {
      continue;
    }
    if ((mRootPos - proj->GetTranslation()).Magnitude() > mGuestC00) {
      continue;
    }
    CSfxManager::AddEmitter(mRicochetSfxId, proj->GetTranslation(), GetCurrentAreaId().Value(),
                            true, false, CSfxManager::kMedPriority);
    CVector3f dir = proj->GetTransform().GetForward();
    dir.SetX(dir.GetX() + (0.4f - 0.8f * mgr.Random()->Float()));
    dir.SetY(dir.GetY() + (0.4f - 0.8f * mgr.Random()->Float()));
    dir.SetZ(dir.GetZ() - (0.5f * mgr.Random()->Float() + 0.05f));
    if (dir.CanBeNormalized() == true) {
      dir.Normalize();
    } else {
      dir = proj->GetTransform().GetForward();
    }
    proj->Explode(proj->GetTranslation(), dir, EWeaponCollisionResponseTypes(0xD), mgr,
                  CDamageVulnerability::ReflectVulnerabilty(), kInvalidUniqueId);
    mLastReflectTime = mElapsedTime;
    if (mReflectedUids.size() >= 5) {
      mReflectedUids.erase(mReflectedUids.begin());
    }
    mReflectedUids.push_back(proj->GetUniqueId());
  }
}

// fn_10_1B9B0 (0x0001B9B0, 0x158 bytes): a projectile may be sent back unless it is null
// (`mr.` at 0x0001B9C0), the actor's own (its owner 0x15C against the uid 0x8, 0x0001B9E4), already
// exploded (0x562 `extrwi r3, r0, 1, 26` at 0x0001B9F0), of another weapon type than a given one
// (`cmpwi r6, -0x1` at 0x0001BA10, its 0x160 at 0x0001BA1C) or already sent back (the walk over
// 0xBF0 at 0x0001BA48..0x0001BA58). During attack 0x800000 (`subis r0, r3, 0x80` at 0x0001BA70)
// phazon and cannon-ball projectiles (the damage's weapon type 0x19C:
// `cmpwi r0, 0xa` at 0x0001BA80,
// `cmpwi r0, 0x8` at 0x0001BA88) are not; any other is. Outside it any weapon type
// (`cmpwi r6, -0x1` at 0x0001BA98) passes; a given type is compared with the stage's chance: the
// stage (fn_10_17E14 at 0x0001BAA8, null passes at 0x0001BAB0) and fn_10_F298's roll for the uid
// against the stage info's +0x24 (`fcmpo` at 0x0001BAD4). Callers: fn_10_1B658 with -1 and
// AnyIncomingMissiles fn_10_1BB08 with a type (0x0001BBF8).
// Run (darksamus_uevent.py, dsfsm2-runs/s10/u10): every arm (key 42 the missile type, 45..49 the
// reject arms, 60 no stage, `trigcall AnyIncomingMissiles` for the typed path and its roll).
// @ fn_10_1B9B0
bool CDarkSamus::CanReflectProjectile(CStateManager& mgr, const CEnergyProjectile* proj,
                                      int type) const {
  if (proj == nullptr || proj->GetOwnerId() == GetUniqueId() || proj->HasExploded()) {
    return false;
  }
  if (type != -1 && type != proj->GetType()) {
    return false;
  }
  const TUniqueId* it = mReflectedUids.begin();
  while (it != mReflectedUids.end() && *it != proj->GetUniqueId()) {
    ++it;
  }
  if (it != mReflectedUids.end()) {
    return false;
  }
  if (mCurrentAttack == 0x800000) {
    const EWeaponType weapon = proj->GetCurrentDamageInfo().GetWeaponMode().GetType();
    return weapon != kWT_Phazon && weapon != kWT_CannonBall;
  }
  if (type == -1) { // 0x0001BA9C
    return true;
  }
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr); // fn_10_17E14, 0x0001BAA8
  if (stage == nullptr) {                                   // 0x0001BAB0
    return true;
  }
  // fn_10_F298 (0x0001BACC) rolls the chance for the projectile's uid and the stage info's +0x24
  // (the missiles' first float, lbl_10_rodata of SLdrDSMissiles) is the chance the roll is
  // compared with (`fcmpo cr0, f1, f0` at 0x0001BAD4, the less-than bit at 0x0001BADC).
  const float roll = RollReflectChance(mgr, proj->GetUniqueId());
  return roll < stage->GetStageInfo().missiles.unknown_0x7ee77018;
}

// fn_10_F210 (0x0000F210, 0x88 bytes): the reflect chance cache's entry erased - the entries
// behind it move down (the next entry's uid at +0x0C, its roll at +0x10 and its roll at +0x14
// go to +0x00, +0x04 and +0x08 of this one, 0x0000F240..0x0000F254) and the count falls by one
// (0x0000F27C). The guest's range test (0x0000F218 and 0x0000F230) has no host counterpart:
// the only caller erases the first entry of a full table.
// @ fn_10_F210
void CDarkSamus::EraseReflectChance(uint index) const {
  for (uint i = index + 1; i < mReflectChanceCount; ++i) {
    mReflectChances[i - 1] = mReflectChances[i];
  }
  --mReflectChanceCount;
}

// fn_10_F298 (0x0000F298, 0x174 bytes): the reflect chance for a projectile's uid. The cache
// (the count at guest 0x10A8 and the ten entries from 0x10AC) is walked (0x0000F2E4..0x0000F2FC)
// and the entry's +0x04 answers when the uid is in it (0x0000F2F0). Otherwise, with ten entries
// (0x0000F300), the first one is erased (fn_10_F210, 0x0000F310) and two rolls are made
// (Range(0.0f, lbl_10_rodata_3F0 = 100.0f), 0x0000F32C and 0x0000F348). The time since the last
// hit or heavy hit (the larger of guest 0xC0C and 0xC20, 0x0000F34C and 0x0000F350) shifts the
// rolls: zero for the first four seconds (lbl_10_rodata_408 = 4.0f, 0x0000F378) and -50.0f
// (lbl_10_rodata_418 = 50.0f, 0x0000F3B0) for the next four (lbl_10_rodata_414 = 8.0f,
// 0x0000F39C). The uid and the two rolls go into the next entry (0x0000F3C8..0x0000F3D4) and the
// count rises (0x0000F3E0). The second roll answers.
// Run (darksamus_uevent.py, dsfsm2-runs/s11/u8): every arm (key 65 the cache full, 66/67 the
// two windows, a uid asked twice for the cached answer).
// @ fn_10_F298
float CDarkSamus::RollReflectChance(CStateManager& mgr, TUniqueId uid) const {
  for (uint i = 0; i < mReflectChanceCount; ++i) {
    if (mReflectChances[i].mUid == uid) {
      return mReflectChances[i].mChance; // 0x0000F2F0
    }
  }
  if (mReflectChanceCount == 10) { // 0x0000F300
    EraseReflectChance(0);          // fn_10_F210, 0x0000F310
  }
  float roll = mgr.Random()->Range(0.f, 100.f);      // 0x0000F32C, lbl_10_rodata_3F0
  float chance = mgr.Random()->Range(0.f, 100.f);    // 0x0000F348
  const float since = mGuestC0C > mGuestC20 ? mGuestC0C : mGuestC20; // 0x0000F34C, 0x0000F350
  if (4.f + since > mElapsedTime) { // lbl_10_rodata_408, 0x0000F378
    roll = 0.f;
    chance = 0.f;
  } else if (8.f + since > mElapsedTime) { // lbl_10_rodata_414, 0x0000F39C
    roll -= 50.f;                             // lbl_10_rodata_418, 0x0000F3B0
    chance -= 50.f;
  }
  mReflectChances[mReflectChanceCount].mUid = uid; // 0x0000F3C8
  mReflectChances[mReflectChanceCount].mChance = chance; // 0x0000F3D0
  mReflectChances[mReflectChanceCount].mRoll = roll; // 0x0000F3D4
  ++mReflectChanceCount; // 0x0000F3E0
  return chance;
}

// fn_10_F100 (0x0000F100, 0x110 bytes): the reflect chance for a projectile's uid, the cache's
// other writer (the hit arm's). The cache is walked (0x0000F14C..0x0000F164) and the entry's
// +0x08 answers when the uid is in it (0x0000F158). Otherwise, with ten entries (0x0000F168) the
// first one is erased (fn_10_F210, 0x0000F178) and two rolls are made (Range(0.0f,
// lbl_10_rodata_3F0 = 100.0f), 0x0000F194 and 0x0000F1B0); the uid and the two rolls go into
// the next entry (0x0000F1CC..0x0000F1D4) and the count rises (0x0000F1E4). The first roll (f31,
// 0x0000F1DC) answers, the value a cache hit returns.
// Arms: eleven new uids (the eleventh erases the first) and a cache hit (ds43 gdb calls).
// @ fn_10_F100
float CDarkSamus::RollReflectChanceForHit(CStateManager& mgr, TUniqueId uid) const {
  for (uint i = 0; i < mReflectChanceCount; ++i) {
    if (mReflectChances[i].mUid == uid) {
      return mReflectChances[i].mRoll; // 0x0000F158
    }
  }
  if (mReflectChanceCount == 10) { // 0x0000F168
    EraseReflectChance(0);          // fn_10_F210, 0x0000F178
  }
  const float first = mgr.Random()->Range(0.f, 100.f);  // 0x0000F194
  const float second = mgr.Random()->Range(0.f, 100.f); // 0x0000F1B0
  mReflectChances[mReflectChanceCount].mUid = uid;    // 0x0000F1CC
  mReflectChances[mReflectChanceCount].mChance = second; // 0x0000F1D0
  mReflectChances[mReflectChanceCount].mRoll = first;    // 0x0000F1D4
  ++mReflectChanceCount; // 0x0000F1E4
  return first; // 0x0000F1DC fmr f1,f31: the first roll, as the cache hit returns mRoll
}

// fn_10_1EE4C (0x0001EE4C, 0xE4 bytes): the damage that hit the actor may be sent back. The
// time since the last hit or heavy hit (the larger of guest 0xC0C and 0xC20, 0x0001EE74 and
// 0x0001EE78) plus lbl_10_rodata_438 = 3.0f must be past the elapsed time 0x8B0
// (0x0001EE90), the damage's weapon type must be 6 (kWT_Missile, 0x0001EEA8) and the stage
// must exist (0x0001EEB8). Then fn_10_F100's roll (0x0001EED8) is compared with the stage
// info's +0x2C (the third missile float) when the weapon is comboed (the super missile; the
// weapon mode's byte 2 bit 0x40, `lbz r0, 0x2(r29)` + `extrwi r3, r0, 1, 25`, 0x0001EEDC) and with its +0x28 (the second missile float, 0x0001EF0C) otherwise.
// Arms: too soon, not a missile, no stage, plain and comboed missiles under/over the stage chances
// (ds43 gdb calls).
// @ fn_10_1EE4C
bool CDarkSamus::CanReflectHit(CStateManager& mgr, const CDamageInfo& damage, TUniqueId uid) const {
  const float since = mGuestC0C > mGuestC20 ? mGuestC0C : mGuestC20; // 0x0001EE74, 0x0001EE78
  if (3.f + since > mElapsedTime) { // lbl_10_rodata_438, 0x0001EE8C
    return false;
  }
  if (damage.GetWeaponMode().GetType() != kWT_Missile) { // 0x0001EEA8
    return false;
  }
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  if (stage == nullptr) { // the retail's own null test, 0x0001EEBC
    return false;
  }
  const float roll = RollReflectChanceForHit(mgr, uid); // fn_10_F100, 0x0001EED8
  if (damage.GetWeaponMode().IsComboed()) { // the weapon mode's byte 2 bit 0x40, 0x0001EEDC
    return roll < stage->GetStageInfo().missiles.unknown_0x1e2debc6; // the stage info's +0x2C
  }
  return roll < stage->GetStageInfo().missiles.unknown_0xbfb7ca5c; // the stage info's +0x28
}

// fn_10_138A4 (0x000138A4, 0xF4 bytes): the dive's hit on the player. The dive record's 0x20 is
// set (`rlwimi r0, r5, 5, 26, 26` at 0x000138D0), the dive damage 0xAF0 goes to the player
// (0x14FC's uid at 0x00013934) from the actor (ApplyDamage at 0x0001394C) through the filter
// including lbl_10_data_200 = 0x3B (kMT_Solid, the __shl2i at 0x000138E4) with type 3
// (`li r0, 0x3` at 0x000138EC) and no knock back (sZeroVector, 0x000138F8), the player's orbit
// target is cleared (fn_10_1382C at 0x00013958), and unless the player is morphed (0x38C:
// `cmpwi r0, 0x1` at 0x00013964) fn_10_13750 pushes it with the dive's impulses 0xB10/0xB14
// (0x0001396C/0x00013974).
// @ fn_10_138A4
void CDarkSamus::HitPlayerWithDive(CStateManager& mgr) {
  mDive.mHitPlayer = true;
  CPlayer* player = mgr.GetPlayer(0);
  mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), mDive.mDamage,
                  CMaterialFilter(CMaterialList(kMT_Solid), CMaterialList(),
                                  CMaterialFilter::kFT_IncludeExclude),
                  CVector3f::Zero());
  ClearPlayerOrbitTarget(mgr);
  if (mgr.GetPlayer(0)->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
    PushPlayer(mgr, mDive.mImpulseHorizontal, mDive.mImpulseVertical);
  }
}

// fn_10_1382C (0x0001382C, 0x3C bytes): the player (0x14FC) orbits nothing: SetOrbitTargetId
// with kInvalidUniqueId (0x00013840, the call at 0x00013854).
// @ fn_10_1382C
void CDarkSamus::ClearPlayerOrbitTarget(CStateManager& mgr) {
  mgr.GetPlayer(0)->SetOrbitTargetId(kInvalidUniqueId, mgr);
}

// fn_10_13750 (0x00013750, 0xDC bytes): the direction from the actor to the player (0x14FC's
// position less the actor's, 0x00013784..0x000137B4) with z = lbl_10_rodata_3FC = 0.0f
// (0x000137C4), normalized (0x000137C8), x and y times `horizontal` (0x000137DC..0x000137EC), z
// replaced by `vertical` (0x000137FC), then fn_10_1367C (0x00013800).
// @ fn_10_13750
void CDarkSamus::PushPlayer(CStateManager& mgr, float horizontal, float vertical) {
  const CVector3f& playerPos = mgr.GetPlayer(0)->GetTranslation();
  const CVector3f& pos = GetTranslation();
  CVector3f dir(playerPos.GetX() - pos.GetX(), playerPos.GetY() - pos.GetY(), 0.f);
  dir.Normalize();
  KnockPlayer(mgr, CVector3f(dir.GetX() * horizontal, dir.GetY() * horizontal, vertical));
}

// fn_10_1367C (0x0001367C, 0xD4 bytes): the player (0x14FC) stops (0x000136A4), takes the impulse
// times its mass 0x158 (0x000136A8..0x000136C0) with no turn (CAxisAngle::Identity, the call at
// 0x000136E0), moves to state 2 (kMS_ApplyJump: `li r4, 0x2` at 0x000136EC, SetMoveState at
// 0x000136F0) and its rumble manager (0x152C by MaskUIdNumPlayers, 0x00013708) plays effect 6
// (`li r5, 0x6` at 0x00013728) at lbl_10_rodata_3E8 = 1.0f with priority 2
// (`li r6, 0x2` at 0x0001372C).
// @ fn_10_1367C
void CDarkSamus::KnockPlayer(CStateManager& mgr, const CVector3f& impulse) {
  CPlayer* player = mgr.GetPlayer(0);
  player->Stop();
  player->ApplyImpulseWR(impulse * player->GetMass(), CAxisAngle::Identity());
  player->SetMoveState(NPlayer::kMS_ApplyJump, mgr);
  mgr.RumbleManager(mgr.MaskUIdNumPlayers(player->GetUniqueId()))
      ->Rumble(mgr, kRFX_CameraShake, 1.f, kRP_Two);
}

// fn_10_18628 (0x00018628, 0x10 bytes): 0xE04 = the elapsed time 0x8B0, 0xE08 = a, 0xE0C = b.
// @ fn_10_18628
void CDarkSamus::SetGuestE04(float a, float b) {
  mGuestE04 = mElapsedTime;
  mGuestE08 = a;
  mGuestE0C = b;
}

// fn_10_18FCC (0x00018FCC, 0x48 bytes): vertical movement off (0x00018FE0), on the ground as given
// (0x00018FF8) and kMT_GroundCollider (`li r4, 0x25`) added (0x00019000).
// @ fn_10_18FCC
void CDarkSamus::SetGrounded(CStateManager& mgr, bool onGround) {
  mVerticalMovement = false;
  mOnGround = onGround;
  AddMaterial(kMT_GroundCollider, mgr);
}

// fn_10_1707C (0x0001707C, 0x3CC bytes): the dodge timer 0x1090 runs down (`ble` at 0x000170CC)
// and at 0 (`cror` at 0x000170E0) the direction 0x1094 clears. With no dodge (`fcmpu` at
// 0x00017118) the flat line to the player's aim position (vtable 0x54, dt 0.0f) that can be
// normalized (0x0001719C): far enough (lbl_10_rodata_500 = 4.5f, `blt` at 0x00017204) with both
// gun lines lbl_10_rodata_3E4 = 0.5f to either side clear (fn_10_164C0 at 0x00017240/0x0001728C)
// needs no dodge; otherwise a side lbl_10_rodata_438 = 3.0f..lbl_10_rodata_47C = 6.0f away
// (Range at 0x000172E4), right first (0x00017344) then left (0x0001736C), whose gun line to the
// aim is clear becomes the direction, and the timer starts at time (0x000173A8). While it runs
// (`ble` at 0x000173BC) the impulse mass * impulse along it (0x00017410).
// Arms: the fight (idle, clear line), plus forced calls (IsGunLineClear answers forced): a dodge
// running, one running out, the right side clear, only the left, neither, the player too close,
// an inf position (ds45).
// @ fn_10_1707C
void CDarkSamus::UpdateDodge(CStateManager& mgr, float dt, float impulse, float time) {
  if (mDodgeTime > 0.f) {
    mDodgeTime -= dt;
    if (mDodgeTime <= 0.f) {
      mDodgeTime = 0.f;
      mDodgeDir = CVector3f::Zero();
    }
  }
  if (0.f == mDodgeTime) {
    const CVector3f aim = mgr.GetPlayer(0)->GetAimPosition(mgr, 0.f);
    CVector3f delta = aim - GetTranslation();
    delta.SetZ(0.f);
    if (delta.CanBeNormalized() == true) {
      const float distance = delta.Magnitude();
      delta.Normalize();
      const CVector3f right(0.5f * delta.GetY(), -(0.5f * delta.GetX()), 0.f);
      const CVector3f left(-(0.5f * delta.GetY()), 0.5f * delta.GetX(), 0.f);
      if (distance < 4.5f || !IsGunLineClear(mgr, right, aim + right) ||
          !IsGunLineClear(mgr, left, aim + left)) {
        const float r = mgr.Random()->Range(3.f, 6.f);
        const CVector3f sideRight = r * CVector3f(delta.GetY(), -delta.GetX(), 0.f);
        const CVector3f sideLeft = r * CVector3f(-delta.GetY(), delta.GetX(), 0.f);
        const CVector3f* side = nullptr;
        if (IsGunLineClear(mgr, sideRight, aim) == true) {
          side = &sideRight;
        } else if (IsGunLineClear(mgr, sideLeft, aim) == true) {
          side = &sideLeft;
        }
        if (side != nullptr) { // 0x00017380
          mDodgeDir = *side;
          mDodgeDir.Normalize();
          mDodgeTime = time;
        }
      }
    }
  }
  if (mDodgeTime > 0.f) {
    ApplyImpulseWR(GetMass() * (impulse * mDodgeDir), CAxisAngle::Identity());
  }
}

// fn_10_1C99C (0x0001C99C, 0x434 bytes): only in the dive (0x980 == 0x20, 0x0001C9C4) in flight
// (0x40, 0x0001C9D4) and not bounced, landing or done (0x0001C9DC..0x0001C9EC). First pass
// (0x0001CB84): a contact with ceiling or wall (lbl_10_data_C34/C38) and no floor (C3C) bounces
// the actor off its normal (fn_10_1CDD0 at 0x0001CC14) - unless the collider is an actor (the
// null test at 0x0001CBD8) whose vulnerability lets the dive's weapon hit it (WeaponHits at
// 0x0001CBF8). Second pass (0x0001CCE8): a contact with no platform (C30) and floor or player
// (C40/C44) lands the dive (0x08, 0x0001CDA8), hitting the player first when it is the player
// (C48) and the dive has not hit yet (0x20, 0x0001CD8C).
// Arms: forced calls on a built list: the five guards; a wall with no collider; a ceiling whose
// collider the dive's weapon hits (WeaponHits answered 1) and a wall it does not (0); wall and
// floor; platform, nothing, then the player, before and after the player was hit; no listed
// material; an empty list (ds45).
// @ fn_10_1C99C
void CDarkSamus::CheckDiveCollision(CStateManager& mgr, CActor* collider,
                                    const CCollisionInfoList& list) {
  if (mCurrentAttack != 0x20 || mDive.mLaunched != true || mDive.mBounced || mDive.mLanding ||
      mDive.mDone) {
    return;
  }
  for (const CCollisionInfo* it = list.Begin(); it != list.End(); ++it) {
    const CMaterialList& mat = it->GetMaterialLeft();
    if ((mat.HasMaterial(kMT_Ceiling) || mat.HasMaterial(kMT_Wall)) &&
        !mat.HasMaterial(kMT_Floor)) {
      if (collider != nullptr && // 0x0001CBD8
          collider->GetDamageVulnerability()->WeaponHits(mDive.mDamage.GetWeaponMode(), 0) ==
              true) {
        return;
      }
      BounceDive(mgr, it->GetNormalLeft());
      return;
    }
  }
  for (const CCollisionInfo* it = list.Begin(); it != list.End(); ++it) {
    const CMaterialList& mat = it->GetMaterialLeft();
    if (!mat.HasMaterial(kMT_Platform) &&
        (mat.HasMaterial(kMT_Floor) || mat.HasMaterial(kMT_Player))) {
      if (mat.HasMaterial(kMT_Player) && !mDive.mHitPlayer) {
        HitPlayerWithDive(mgr);
      }
      mDive.mHitFloor = true;
      return;
    }
  }
}

// fn_10_9424 (0x00009424, 0x184 bytes): the DiveFromBoost state, the dive out of the boost
// ball. Only Activate runs the body (`mr. r31, r5` at 0x00009444 and `bne .L_0000954C` at
// 0x00009454 skip it for the other messages): the dive record's boost bit 0x01
// (`rlwimi r0, r3, 0, 31, 31` at 0x00009460, the byte 0xB20), the roll emitter removed
// (`bl RemoveEmitter__11CSfxManagerF10CSfxHandle` at 0x00009474) and its handle cleared
// (`stw r0, 0xe6c(r29)` at 0x00009480), fn_10_9644 (the turn around, 0x00009484), 0xD24 =
// lbl_10_rodata_284 = 0.5f (`stfs f0, 0xd24(r29)` at 0x000094A8), 0xEBC bit 0x40
// (`rlwimi r3, r4, 6, 25, 25` at 0x000094B0), the element generator's auto_ptr 0xE60/0xE64
// emptied (the old one deleted at 0x000094EC, the stores at 0x000094F8/0x00009500), fn_10_9AC4 (the boost ball's deletion,
// 0x0000953C) and fn_10_99B8 (0x00009548). Then the base state handler - the vtable's word
// 136 (`lwz r12, 0x220(r12)` at 0x00009560), the DiveAttack state fn_10_11FA4 - runs on
// every message. Deactivate (`cmpwi r31, 0x2` at 0x0000956C) clears the boost bit again
// (`rlwimi r0, r3, 0, 31, 31` at 0x0000957C).
// Run (darksamus_boostroll.py, DSR_MODE=1/2): every arm (the Activate and the Deactivate).
// @ fn_10_9424
void CDarkSamus::DiveFromBoost(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("DiveFromBoost", msg);
  if (msg == kStateMsg_Activate) {                 // 0x00009454 `bne .L_0000954C`
    mDive.mFromBoost = true;                       // 0x00009460 `rlwimi r0, r3, 0, 31, 31`
    CSfxManager::RemoveEmitter(mBoostBallRollSfx); // 0x00009474
    mBoostBallRollSfx = CSfxHandle();              // 0x00009480 `stw r0, 0xe6c(r29)`
    TurnAround();                                  // fn_10_9644, 0x00009484
    mGuestD24 = 0.5f;                              // lbl_10_rodata_284, 0x000094A8
    mBoostBallFlag40 = true;                       // 0x000094B0 `rlwimi r3, r4, 6, 25, 25`
    mBoostBallElementGen = nullptr;                // 0x000094EC..0x00009500
    DeleteBoostBall(mgr);                          // fn_10_9AC4, 0x0000953C
    StopBoostBallParticles();                      // fn_10_99B8, 0x00009548
  }
  DiveAttack(mgr, msg, dt); // 0x00009560, the vtable's word 136
  if (msg == kStateMsg_Deactivate) { // 0x0000956C `cmpwi r31, 0x2`
    mDive.mFromBoost = false;        // 0x0000957C `rlwimi r0, r3, 0, 31, 31`
  }
}

// fn_10_9644 (0x00009644, 0xDC bytes): the actor turned around. Its facing (the transform's
// Y column, guest 0x28/0x38/0x48) is a vector that is normalized when it can be
// (`clrlwi r0, r3, 24` and `cmplwi r0, 0x1` at 0x00009688 and 0x0000968C), scaled by
// lbl_10_rodata_288 = -1.0f (0x000096C0) and used as the LookAt target (0x000096E4); the
// matrix's quaternion (0x000096F0) and the actor's position (guest 0x54, 0x000096FC) build
// the transform (0x00009700) the actor takes (0x0000970C).
// The arm whose facing cannot be normalized is dead: the transform's Y column is a unit
// vector, so CanBeNormalized is always true.
// @ fn_10_9644
void CDarkSamus::TurnAround() {
  CVector3f dir = GetTransform().GetColumn(kDY); // guest 0x28/0x38/0x48
  if (dir.CanBeNormalized() == true) {            // 0x0000968C
    dir.Normalize();
    SetTransform(CQuaternion::FromMatrix(CTransform4f::LookAt(CVector3f::Zero(), -1.f * dir,
                                                               CVector3f::Up()))
                     .BuildTransform4f(GetTranslation()));
  }
}

// fn_10_9AC4 (0x00009AC4, 0x60 bytes): the boost ball actor (guest 0xE38) deleted from the
// state manager (`bl DeleteObjectRequest__13CStateManagerF9TUniqueId` at 0x00009B00) and the
// uid set to kInvalidUniqueId (`sth r0, 0xe38(r31)` at 0x00009B0C), unless it already is (the
// test at 0x00009AE8).
// Unmarked: the arm that deletes an actor needs a boost ball in the world. The uid 0xE38 is
// set by the BoostBallRoll (fn_10_A3B0) and BoostBallFire (fn_10_9724) paths, which are
// outside the port, so only the "no ball" arm ran.
// @ fn_10_9AC4
// Run 2026-10-21 (Sanctuary 10_cliff_rooftop, boss u04f7): the null arm ran on its own (the dive's
// exit calls it with no ball); the delete arm was forced (gdb `set var mBoostBallUid.value = 0x4f7`
// at the dive's entry). See docs/module-analysis/DarkSamus_helpers.md.
void CDarkSamus::DeleteBoostBall(CStateManager& mgr) {
  if (mBoostBallUid != kInvalidUniqueId) { // 0x00009AE8
    mgr.DeleteObjectRequest(mBoostBallUid); // 0x00009B00
    mBoostBallUid = kInvalidUniqueId;       // 0x00009B0C
  }
}

// fn_10_7A14 (0x00007A14, 0x154 bytes): the EnrageHitReact state. Every message sets the body's
// locomotion type 10 (pas::kLT_Internal10, the call at 0x00007A4C). Deactivate (0x00007A50)
// draws the time 0xFB0 from lbl_10_rodata_1CC = 0.2f and lbl_10_rodata_1D0 = 4.5f
// (0x00007A70), steps the guest's own random (the `mulhw` with 0x55555556 at 0x00007A8C: 0x105C
// grows by the high word of 0x55555556 * 0x1058 and its carry bit) and stores the health at that
// moment (0xFC4, the vtable's slot 0x3C at 0x00007AA0 and the store at 0x00007AB4). Then
// fn_10_7F98 (0x00007AC8) - the phazon enrage frame, UpdatePhazonEnrage - and the
// melee attack command (fn_10_8764 at 0x00007B28, the severity 8 = pas::kS_Eight).
// Arms: both natural (tools/drivers/darksamus_ds39.con).
// @ fn_10_7A14
void CDarkSamus::EnrageHitReact(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("EnrageHitReact", msg);
  BodyController()->SetLocomotionType(pas::kLT_Internal10); // 0x00007A4C
  if (msg == kStateMsg_Deactivate) {
    mGuestFB0 = mgr.Random()->Range(0.2f, 4.5f); // 0x00007A70
    // 0x00007A8C: the `mulhw` with 0x55555556 - the high word of 0x55555556 * 0x1058 and its
    // carry bit - is added to 0x105C. The two words are the guest's own (the phazon volleys' in
    // the fsm2 lane's naming); the retail's are unsigned, so the step is done in unsigned.
    // mulhw is the signed high word, plus its sign bit: the signed division by 3 (rounding to 0).
    mGuest105C = static_cast< int >(static_cast< uint >(mGuest105C) +
                                    static_cast< uint >(static_cast< int >(mGuest1058) / 3)); // 0x00007A8C
    mGuestFC4 = GetHealthInfo()->GetHP(); // 0x00007AB0
  }
  UpdatePhazonEnrage(mgr, msg, dt); // fn_10_7F98 at 0x00007AC8
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Eight)); // 0x00007B28
}

// fn_10_6E38 (0x00006E38, 0x158 bytes): the EnragedTravel state. Every message reads the player's
// aim position (the state manager's player at 0x14FC, the vtable's slot 0x54 at 0x00006E78; the
// guest's extra argument &lbl_10_rodata_1AC = 0.5f has no host counterpart, the dt is 0.5f).
// Activate (0x00006E90) runs fn_10_6F90 - the travel, the helpers branch's
// FindPathToDestination - and forgets the target (0x9FC = -1, 0x00006EC4). The other messages measure the aim position
// against the point 0x1048/0x104C/0x1050 (the squared distance at 0x00006EEC..0x00006EF4
// against lbl_10_rodata_1B0 = 16.0f at 0x00006EF8) and run fn_10_6F90 when it is bigger
// (0x00006F10); then the player's uid (its halfword at +8) becomes the navigation's face target
// (0x718, 0x00006F30), the navigation's path find runs (CPathFindNavigation::PathFind on 0x6F4,
// 0x00006F34) and fn_10_1DD14 (the separation, 0x00006F40). Every
// message then runs fn_10_6DAC (0x00006F4C - a stateless one the helper lanes port),
// fn_10_1D860 (the additive animation, 0x00006F5C) and fn_10_11334 (the glide emitter,
// 0x00006F68).
// Arms: all natural (tools/drivers/darksamus_ds39.con: the player moved mid-state for 0x00006EF8).
// @ fn_10_6E38
void CDarkSamus::EnragedTravel(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("EnragedTravel", msg);
  const CVector3f aim = mgr.GetPlayer(0)->GetAimPosition(mgr, 0.5f); // 0x00006E88
  if (msg == kStateMsg_Activate) {
    FindPathToDestination(mgr, aim, dt); // fn_10_6F90, 0x00006EBC
    mDash.mLastSlot = -1;               // 0x00006EC4 (`stw r0, 0x9fc(r29)`)
  } else {
    const CVector3f d(aim.GetX() - mGuest1048, aim.GetY() - mGuest104C, aim.GetZ() - mGuest1050);
    if (CVector3f::Dot(d, d) > 16.f) { // 0x00006EF8 (the squared distance)
      FindPathToDestination(mgr, aim, dt); // fn_10_6F90, 0x00006F10
    }
    mPathFindNavigation.SetFaceTarget(mgr.GetPlayer(0)->GetUniqueId()); // 0x718, 0x00006F30
    mPathFindNavigation.PathFind(mgr, msg, dt, *this);                  // 0x6F4, 0x00006F34
    Separate(mgr); // fn_10_1DD14, 0x00006F40
  }
  UpdateShieldFromHealth(msg); // fn_10_6DAC, 0x00006F4C
  UpdateAdditiveAnimation(mgr, msg);                        // fn_10_1D860, 0x00006F5C
  UpdateGlideEmitter(msg);                                  // fn_10_11334, 0x00006F68
}

// fn_10_BD40 (0x0000BD40, 0xDC bytes): the FadeIn state. Every message runs the attack
// bookkeeping with id 0x40000 (`lis r4, 0x4` at 0x0000BD60, the call at 0x0000BD70), the ground
// collision (fn_10_18F90 at
// 0x0000BD80) and the melee attack command (fn_10_8764 at 0x0000BDE0, the severity 4 =
// pas::kS_Four, the target position the zero vector and no target).
// @ fn_10_BD40
void CDarkSamus::FadeIn(CStateManager& mgr, EStateMsg msg, float) {
  DsStateEntered("FadeIn", msg);
  UpdateCurrentAttack(0x40000, mgr, msg); // `lis r4, 0x4` at 0x0000BD60, the call at 0x0000BD70
  UpdateGroundCollision(mgr, msg); // 0x0000BD80
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Four)); // 0x0000BDE0
}

// fn_10_BC54 (0x0000BC54, 0xDC bytes): the FadeOut state. Every message runs the attack
// bookkeeping with id 0x80000 (`lis r4, 0x8` at 0x0000BC74, the call at 0x0000BC84), the ground
// collision (fn_10_18F90 at
// 0x0000BC94), the shield flag (fn_10_105F4 at 0x0000BCA8, with the zero vector) and the melee
// attack command (fn_10_8764 at 0x0000BD08, the severity 3 = pas::kS_Three).
// @ fn_10_BC54
void CDarkSamus::FadeOut(CStateManager& mgr, EStateMsg msg, float) {
  DsStateEntered("FadeOut", msg);
  UpdateCurrentAttack(0x80000, mgr, msg); // `lis r4, 0x8` at 0x0000BC74, the call at 0x0000BC84
  UpdateGroundCollision(mgr, msg); // 0x0000BC94
  UpdateShieldFlag(msg);           // 0x0000BCA8
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Three)); // 0x0000BD08
}

// fn_10_F86C (0x0000F86C, 0xDC bytes): the FallFromTheAir state. Every message runs the attack
// bookkeeping with id 0x4000 (fn_10_1B478 at 0x0000F89C), the ground collision (fn_10_18F90 at
// 0x0000F8AC) and the taunt command (fn_10_8698 at 0x0000F8E8: the command id 0x15 = 21 =
// kBSC_Taunt, the taunt type 0 = pas::kTT_Zero, the animation state 0x10 = pas::kAS_Taunt).
// @ fn_10_F86C
void CDarkSamus::FallFromTheAir(CStateManager& mgr, EStateMsg msg, float) {
  DsStateEntered("FallFromTheAir", msg);
  UpdateCurrentAttack(0x4000, mgr, msg); // 0x0000F89C
  UpdateGroundCollision(mgr, msg);       // 0x0000F8AC
  RunTauntCmd(msg, pas::kTT_Zero);       // 0x0000F8E8
}

// fn_10_17BC0 (0x00017BC0, 0xF8 bytes): the MeleeAttack state. The attack bookkeeping with id 1
// (`li r4, 0x1`, fn_10_1B478 at 0x00017BEC); Activate (0x00017BF0) resets the melee contact
// (fn_10_EA08 on 0x9A4); the melee attack command with severity 1 (`li r7, 0x1` at 0x00017C28,
// pas::kAS_MeleeAttack = `li r5, 0x7`, fn_10_8764 at 0x00017C5C); Deactivate (`cmpwi r30, 0x2` at
// 0x00017C6C) clears 0xC00 (lbl_10_rodata_3FC = 0.0f) and forgets the last attacker (0xC24 =
// kInvalidUniqueId); every message stores the elapsed time 0x8B0 to 0x9C0 (0x00017C9C).
// @ fn_10_17BC0
void CDarkSamus::MeleeAttack(CStateManager& mgr, EStateMsg msg, float) {
  DsStateEntered("MeleeAttack", msg);
  UpdateCurrentAttack(0x1, mgr, msg);
  if (msg == kStateMsg_Activate) {
    mMeleeContact.Reset();
  }
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_One));
  if (msg == kStateMsg_Deactivate) {
    mGuestC00 = 0.f;
    mLastAttackerUid = kInvalidUniqueId;
  }
  mLastMeleeTime = mElapsedTime;
}

// fn_10_17ABC (0x00017ABC, 0x104 bytes): the MeleeDash state. The attack bookkeeping with id 4
// (`li r4, 0x4`, fn_10_1B478 at 0x00017AEC) and the collision failsafe (fn_10_1AEC0 at
// 0x00017AF8); Activate (`cmpwi r30, 0x0` at 0x00017AFC) resets the melee contact (fn_10_EA08 on
// 0x9A4); the melee attack command with severity 2 (`li r7, 0x2` at 0x00017B34,
// pas::kAS_MeleeAttack = `li r5, 0x7`, fn_10_8764 at 0x00017B68); Deactivate (`cmpwi r30, 0x2`
// at 0x00017B78) clears 0xC00 (lbl_10_rodata_3FC = 0.0f) and the contact's uid (0x9DC =
// kInvalidUniqueId, 0x00017BA0).
// @ fn_10_17ABC
void CDarkSamus::MeleeDash(CStateManager& mgr, EStateMsg msg, float) {
  DsStateEntered("MeleeDash", msg);
  UpdateCurrentAttack(0x4, mgr, msg);
  RunCollisionFailsafe(mgr);
  if (msg == kStateMsg_Activate) {
    mMeleeContact.Reset();
  }
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Two));
  if (msg == kStateMsg_Deactivate) {
    mGuestC00 = 0.f;
    mMeleeContact.mHitUid = kInvalidUniqueId;
  }
}

// fn_10_177BC (0x000177BC, 0x128 bytes): the MissileJump state. The attack bookkeeping with id
// 0x80 (`li r4, 0x80`, fn_10_1B478 at 0x000177F8), the shield flag (fn_10_105F4 at 0x0001780C),
// the ground collision (fn_10_18F90 at 0x0001781C), the turn toward the player at
// lbl_10_rodata_508 = 1.0471976f rad/s (fn_10_16B64 at 0x00017838), the collision failsafe
// (fn_10_1AEC0 at 0x00017844) and the melee attack command with severity 10 (0x00017870;
// pas::kAS_MeleeAttack = 7, fn_10_8764 at 0x000178A4).
// @ fn_10_177BC
void CDarkSamus::MissileJump(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("MissileJump", msg);
  UpdateCurrentAttack(0x80, mgr, msg);
  UpdateShieldFlag(msg);
  UpdateGroundCollision(mgr, msg);
  TurnTowardPlayer(mgr, dt, 1.0471976f);
  RunCollisionFailsafe(mgr);
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Ten));
}

// fn_10_8764 (0x00008764, 0x10C bytes): the switch over the message (0x0000876C..0x000087A0).
// Activate (0x000087A4) stores 1 (kAS_Ready) to the animation state 0x6B4 and delivers the
// command (DeliverCmd of its id, then the copy into the command manager's melee slot
// 0x180..0x194); Update (0x000087F4) re-delivers it when the DOL's ShouldRetry with `state`
// answers true (0x00008800) - the host's CAnimationState::CanIssueCommand, as in
// TurnToFacePlayer; Deactivate (0x00008850) stores 0 (kAS_NotReady).
// Arms: Activate, Update with ShouldRetry false and true, Deactivate, natural
// (tools/drivers/darksamus_ds39.con).
// @ fn_10_8764
void CDarkSamus::RunMeleeAttackCmd(EStateMsg msg, pas::EAnimationState state,
                                   const CBCMeleeAttackCmd& cmd) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(cmd);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), state)) {
      BodyController()->CommandMgr().DeliverCmd(cmd);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  default:
    // Dead: EStateMsg has three values (StateMachineCommon.hpp) and every caller passes its
    // state's message; retail's out-of-range exits are the 0x00008794 (`b`, below 0) and
    // 0x0000879C (`bge`, 3 and up).
    break;
  }
}

// fn_10_EA08 (0x0000EA08, 0x1C bytes): the contact's uid to kInvalidUniqueId (0x0000EA18) and its
// hit byte to 0 (0x0000EA1C).
// @ fn_10_EA08
void CDarkSamusMeleeContact::Reset() {
  mHitUid = kInvalidUniqueId;
  mHit = 0;
}

// fn_10_11AD8 (0x00011AD8, 0x17C bytes): the MovingScatterShot state, the scatter shot fired
// from a side step. Activate (0x00011B2C) clears the start time (0xBA4 = lbl_10_rodata_3FC = 0.0f),
// the firing byte 0xBAD and the shot count 0xBA0 and forgets the projectile (0xBE4 =
// kInvalidUniqueId); Deactivate (0x00011B54) only forgets the projectile. Every message then turns
// toward the player at lbl_10_rodata_434 = 1.7453293f rad/s (fn_10_16B64, 0x00011B78), runs the fire
// timer (fn_10_11974, 0x00011B88) and the attack bookkeeping with id 0x10 (`li r4, 0x10`,
// fn_10_1B478 at 0x00011B9C), and last the step command (fn_10_21DF8 at 0x00011C14,
// pas::kAS_Step = `li r5, 0x3`): a break-dodge step (`li r7, 0x2`) to the right (`li r9, 0x3`)
// unless the side byte 0xBAC is set (`cmplwi r0, 0x0` at 0x00011BA8), then to the left (0x2).
// The shots start from DoUserAnimEvent's kUE_EventStart arm with the side 0xBAC (0x0001C264).
// @ fn_10_11AD8
void CDarkSamus::MovingScatterShot(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("MovingScatterShot", msg);
  switch (msg) {
  case kStateMsg_Activate:
    mScatterStartTime = 0.f;
    mScatterFiring = 0;
    mScatterShotCount = 0;
    mScatterProjectileUid = kInvalidUniqueId;
    break;
  case kStateMsg_Update:
    break;
  case kStateMsg_Deactivate:
    mScatterProjectileUid = kInvalidUniqueId;
    break;
  }
  TurnTowardPlayer(mgr, dt, 1.7453293f);
  UpdateScatterShotFire(mgr, dt);
  UpdateCurrentAttack(0x10, mgr, msg);
  DoStepCmd(msg, pas::kAS_Step,
            CBCStepCmd(mScatterSide != 0 ? pas::kSD_Left : pas::kSD_Right, pas::kStep_BreakDodge));
}

// fn_10_14448 (0x00014448, 4 bytes): the Null state, a bare `blr`. DsStateEntered is the port's
// state log. No arm to run: the rooftop FSM2 has no node named Null (`fsm u04f7 Null` finds
// none), so the handler is never entered there.
// @ fn_10_14448
void CDarkSamus::Null(CStateManager&, EStateMsg msg, float) {
  DsStateEntered("Null", msg);
}

// fn_10_143A8 (0x000143A8, 0xA0 bytes): the Dead state. Every message runs the base's Dead
// (Dead__10CPatterned = fn_80152820, 0x000143CC). Activate only (`cmpwi r31, 0x0` at 0x000143D0):
// the boost ball roll emitter 0xE6C, when there is one (`cmplwi r0, 0x0` at 0x000143DC), is
// removed (RemoveEmitter at 0x000143F0) and cleared (0x000143F8); the environment effects' density
// fades to lbl_10_rodata_3FC = 0.0f over lbl_10_rodata_254 = 0xFA0 = 4000 (the manager at 0x1630,
// SetDensity__13CEnvFxManagerFfi = the host's FadeDensity, 0x00014418) and bit 0x20 of 0x90C is
// cleared (`rlwimi r0, r3, 5, 26, 26` with r3 = 0 at 0x00014424, the store at 0x00014428).
// asmcheck's three findings are names: x90c_20 is mFlags90C's 0x20, SetDensity the host's
// FadeDensity and fn_80152820 CPatterned::Dead (symbols.txt).
// Run (darksamus_dsfsm.py, darksamus_dsfsm.con / _dead.con): Activate with the roll emitter
// (after BoostBallInto) and without it, Update and Deactivate - every arm natural.
// @ fn_10_143A8
void CDarkSamus::Dead(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("Dead", msg);
  CPatterned::Dead(mgr, msg, dt); // 0x000143CC
  if (msg != kStateMsg_Activate) { // 0x000143D0
    return;
  }
  if (mBoostBallRollSfx) { // 0x000143DC
    CSfxManager::RemoveEmitter(mBoostBallRollSfx); // 0x000143F0
    mBoostBallRollSfx = CSfxHandle();              // 0x000143F8
  }
  mgr.EnvFxManager()->FadeDensity(0.f, 4000); // 0x00014418
  mFlags90C &= ~0x20;                         // 0x00014424
}

// fn_10_10710 (0x00010710, 0x154 bytes): the Pause state. The rest of its body is still a
// stand-in, but its write of the guest's 0xC30 is ported: on Activate (0x00010754) 0xC30 takes
// the elapsed time 0x8B0 (0x00010768) and then the stage's pause duration (the stage's +0x18
// and +0x1C, the random range at 0x000107A0) is added (0x000107AC) - 0xC30 is the time the
// pause ends, which the BreakPause trigger (fn_10_106F8) compares with the elapsed time.
// fn_10_10710 (0x00010710, 0x154 bytes): the Pause state. Every message runs the attack
// bookkeeping with id 0x400 (fn_10_1B478 at 0x00010750). Activate (0x00010754) takes the elapsed
// time 0x8B0 as the pause's end time (0x00010768) and adds the stage's pause duration (the stage
// info's +0x18 and +0x1C, the random range at 0x000107A0, the sum at 0x000107AC) - the
// BreakPause trigger (fn_10_106F8) compares it with the elapsed time. Deactivate (0x000107B4)
// forgets the last attacker (0xC24 = kInvalidUniqueId, 0x000107C4). Then the damage
// (fn_10_1E178 at 0x000107D4), the turn toward the player (fn_10_16B64 at 0x000107F0, the rate
// lbl_10_rodata_44C = 1.3962634f) and the locomotion command (fn_800EE3C4 at 0x00010838 - the
// CBCLocomotionCmd with the zero move and face vectors and the weight 1.0f).
// @ fn_10_10710
void CDarkSamus::Pause(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("Pause", msg);
  UpdateCurrentAttack(0x400, mgr, msg); // 0x00010750
  if (msg == kStateMsg_Activate) {
    mGuestC30 = mElapsedTime; // 0x00010768
    const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
    if (stage != nullptr) { // 0x00010770
      mGuestC30 += mgr.Random()->Range(stage->GetStageInfo().pauseDurationMin,
                                       stage->GetStageInfo().pauseDurationMax); // 0x000107A0
    }
  } else if (msg == kStateMsg_Deactivate) {
    mLastAttackerUid = kInvalidUniqueId; // 0x000107C4
  }
  AccumulateDamage(msg, dt); // 0x000107D4
  TurnTowardPlayer(mgr, dt, 1.3962634f); // 0x000107F0 (lbl_10_rodata_44C)
  BodyController()->CommandMgr().DeliverCmd(
      CBCLocomotionCmd(CVector3f::Zero(), CVector3f::Zero(), 1.f)); // 0x00010838
}

// fn_10_F930 (0x0000F930, 0x1C4 bytes): the PissedOff state. Activate (`mr. r31, r5` at
// 0x0000F950) clears 0xD59 and 0xD58 (0x0000F96C/0x0000F97C), sends kSS_InternalState0 ('IS00',
// 0x0000F994) and puts the actor on the area hint of type 0xE (fn_10_FC64, SetTranslation at
// 0x0000F9B8); Deactivate (0x0000F9C4) sends kSS_InternalState1 ('IS01', 0x0000F9EC). Every
// message runs the attack bookkeeping with 0x1000 (fn_10_1B478, 0x0000FA00), the shield flag
// (fn_10_105F4, 0x0000FA14) and the ground collision (fn_10_18F90, 0x0000FA24), turns toward the
// hint of type 0xF at lbl_10_rodata_430 = 5.2359877f rad/s (fn_10_16B8C, 0x0000FA58) or, with
// none, toward the player at lbl_10_rodata_434 = 1.7453293f (fn_10_16B64, 0x0000FA78), and runs
// the taunt command with taunt type 1 (fn_10_8698 with pas::kAS_Taunt = 0x10, the command
// {0x15 = kBSC_Taunt, 1} at 0x0000FA88..0x0000FAB4; see TurnToFacePlayer for the host form).
// @ fn_10_F930
void CDarkSamus::PissedOff(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("PissedOff", msg);
  if (msg == kStateMsg_Activate) {
    mStageChanged = 0;
    mPissedOff = 0;
    SendScriptMsgs(kSS_InternalState0, mgr, kInvalidUniqueId, kSM_None);
    if (const CScriptAIHint* hint = FindAreaHint(mgr, 0xE)) { // 0x0000F9AC
      SetTranslation(hint->GetTranslation());
    }
  } else if (msg == kStateMsg_Deactivate) {
    SendScriptMsgs(kSS_InternalState1, mgr, kInvalidUniqueId, kSM_None);
  }
  UpdateCurrentAttack(0x1000, mgr, msg);
  UpdateShieldFlag(msg);
  UpdateGroundCollision(mgr, msg);
  if (const CScriptAIHint* face = FindAreaHint(mgr, 0xF)) { // 0x0000FA3C
    TurnTowardPoint(dt, face->GetTranslation(), 5.2359877f);
  } else {
    TurnTowardPlayer(mgr, dt, 1.7453293f);
  }
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(pas::kTT_One));
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Taunt)) {
      BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(pas::kTT_One));
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  }
}

// fn_10_FC64 (0x0000FC64, 0xCC bytes): the first area hint of `type` - the AI waypoint list (the
// state manager's 0x838, its first index at 0x2008) is walked for an active (the byte at 0x20's
// bit 7, 0x0000FCC4) CScriptAIHint (the cast at 0x0000FCA0) in the actor's area (0x0000FCD4)
// whose hint type (0x158) is `type` (0x0000FCE0). None answers null (0x0000FD0C).
// @ fn_10_FC64
const CScriptAIHint* CDarkSamus::FindAreaHint(CStateManager& mgr, int type) const {
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(list[i]);
    if (hint == nullptr || hint->GetActive() != true || // 0x0000FCA0/0x0000FCC4
        hint->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    if (static_cast< int >(hint->GetHintType()) == type) {
      return hint;
    }
  }
  return nullptr;
}

// fn_10_105F4 (0x000105F4, 0x40 bytes): Activate sets the 0xCD0 byte (0x00010604) and copies
// lbl_10_bss_128 = (0.3, 0.6, 1.0) to 0xD18 (0x0001060C..0x00010618); Deactivate (the `cmpwi r4,
// 0x2` at 0x00010620) clears the byte (0x0001062C). The byte picks the shielded collision response
// (fn_10_F84C) and the 0xCD4 vulnerability (fn_10_100DC, still a stand-in).
// @ fn_10_105F4
void CDarkSamus::UpdateShieldFlag(EStateMsg msg) {
  if (msg == kStateMsg_Activate) {
    mGuestCD0 = 1;
    mGuestD18 = CVector3f(0.3f, 0.6f, 1.f); // lbl_10_bss_128 (fn_10_221C0: rodata 46C/424/3E8)
  } else if (msg == kStateMsg_Deactivate) {
    mGuestCD0 = 0;
  }
}

// fn_10_18F90 (0x00018F90, 0x3C bytes): Activate (0x00018FA0) runs fn_10_19014 - vertical
// movement on, off the ground and no kMT_GroundCollider (0x0001902C/0x00019040/0x00019048);
// Deactivate (0x00018FB0) runs fn_10_18FCC with 1 - vertical movement off, on the ground and the
// material back (0x00018FE0/0x00018FF8/0x00019000).
// @ fn_10_18F90
void CDarkSamus::UpdateGroundCollision(CStateManager& mgr, EStateMsg msg) {
  if (msg == kStateMsg_Activate) {
    LeaveGround(mgr); // fn_10_19014 at 0x00018FA4
  } else if (msg == kStateMsg_Deactivate) {
    SetGrounded(mgr, true); // fn_10_18FCC at 0x00018FBC, `li r5, 0x1`
  }
}

// fn_10_19014 (0x00019014, 0x48 bytes): vertical movement on, off the ground and
// kMT_GroundCollider removed. Branch-free; its only caller is fn_10_18F90's Activate.
// 0x0001902C `rlwimi r0, r5, 6, 25, 25` with r5 = 1: PowerPC bit 25 of the byte 0x34C, 0x40.
// 0x00019040 `rlwimi r0, r6, 3, 28, 28` with r6 = 0: bit 28, 0x8.
// 0x00019034 `li r4, 0x25` (kMT_GroundCollider), removed at 0x00019048.
// @ fn_10_19014
void CDarkSamus::LeaveGround(CStateManager& mgr) {
  mVerticalMovement = true;                 // 0x0001902C
  mOnGround = false;                        // 0x00019040
  RemoveMaterial(kMT_GroundCollider, mgr); // 0x00019048
}

// fn_10_FD4C (0x0000FD4C, 0xCC bytes): a stage count (fn_10_17E48, 0x978) other than the one last
// seen (0xD54, the beq at 0x0000FD74) clears 0xD59 (0x0000FD80) and reads the last seen stage
// (0xD50): its info's +0x84 (unknown_0x911a2476, 0x0000FDB4) set gets the actor pissed off (0xD58
// = 1, 0x0000FDBC), clear - or no stage (0x0000FD9C) or no info (0x0000FDA8, the accessor is a
// member address and is never null) - sets 0xD59 (0x0000FDC8/0x0000FDEC). The current stage and
// count become the seen ones (0x0000FDD4..0x0000FDDC, 0x0000FDF4..0x0000FDFC).
// @ fn_10_FD4C
void CDarkSamus::WatchStageChange(CStateManager& mgr) {
  if (GetStageCount() == mSeenStageCount) { // fn_10_17E48 at 0x0000FD68, the beq at 0x0000FD74
    return;
  }
  mStageChanged = 0;
  const CDarkSamusBattleStage* seen =
      TCastToConstPtr< CDarkSamusBattleStage >(mgr.GetObjectById(mSeenStageUid));
  if (seen != nullptr) { // 0x0000FD9C
    if (seen->GetStageInfo().unknown_0x911a2476 == true) {
      mPissedOff = 1;
    } else {
      mStageChanged = 1;
    }
  } else {
    mStageChanged = 1;
  }
  mSeenStageUid = mBattleStageUid;
  mSeenStageCount = GetStageCount(); // fn_10_17E48 at 0x0000FDD8 / 0x0000FDF8
}

// fn_10_175AC (0x000175AC, 0x1B4 bytes): the ScatterShot state. Every message runs the aim
// points (fn_10_10520, 0x000175DC) and the attack bookkeeping with the scatter shot's id 8
// (`li r4, 0x8`, fn_10_1B478 at 0x000175F0) first. Activate (0x00017618) clears the delay
// (0xBA4 = lbl_10_rodata_3FC = 0.0f), the firing byte 0xBAD and the shot count 0xBA0, forgets the
// projectile (0xBE4 = kInvalidUniqueId) and runs the collision failsafe (fn_10_1AEC0,
// 0x00017648). Update (0x00017650) turns toward the player at lbl_10_rodata_434 = 1.7453293f
// rad/s (fn_10_16B64) and runs the fire timer (fn_10_11974). Deactivate (0x00017680) forgets the
// projectile and runs the failsafe. Last, for every message, the projectile-attack command
// (fn_10_21F14 at 0x00017720, pas::kAS_ProjectileAttack = `li r5, 0x12`) with severity 1
// (`li r7, 0x1`), the target lbl_10_rodata_3E4 = 0.5f times the sum of the line's ends (0xB7C +
// 0xB94, 0x000176B8..0x000176E8) and no blend (`li r0, 0x0`). The state ends on the FSM's
// AnimOver, which the command's animation state drives.
// Arms: all natural but the switch's out-of-range one, dead (a message below 0, the fall-through
// at 0x00017608, or of 3 and up, the `bge` at 0x00017610, both to 0x0001769C) - EStateMsg has only
// 0..2.
// @ fn_10_175AC
void CDarkSamus::ScatterShot(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("ScatterShot", msg);
  UpdateAttackAimPoints(mgr, msg);
  UpdateCurrentAttack(8, mgr, msg);
  switch (msg) {
  case kStateMsg_Activate:
    mScatterStartTime = 0.f;
    mScatterFiring = 0;
    mScatterShotCount = 0;
    mScatterProjectileUid = kInvalidUniqueId;
    RunCollisionFailsafe(mgr);
    break;
  case kStateMsg_Update:
    TurnTowardPlayer(mgr, dt, 1.7453293f);
    UpdateScatterShotFire(mgr, dt);
    break;
  case kStateMsg_Deactivate:
    mScatterProjectileUid = kInvalidUniqueId;
    RunCollisionFailsafe(mgr);
    break;
  }
  DoProjectileAttackCmd(msg, pas::kAS_ProjectileAttack,
                        CBCProjectileAttackCmd(pas::kS_One,
                                               0.5f * (mScatterLineStart + mScatterLineEnd),
                                               false));
}

// fn_10_EDB8 (0x0000EDB8, 0x14 bytes): the side dash's weight of a slot, lbl_10_rodata_3BC[slot]:
// 0.6f, 0.7f, 1.0f, 1.0f, 0.8f, 0.8f, 0.9f, 0.9f (slots 2/3 first, the forward slots 0/1 last).
// Class T (ds41): its only caller is fn_10_153EC (the `bl` at 0x00015480), the SideDash state,
// which the shipped FSM2 never enters (no SideDash node among its 59 states, DarkSamus.md
// "SideDash"), so it never runs in retail.
static float GetSideDashWeight(int slot) {
  static const float kWeights[8] = {0.6f, 0.7f, 1.f, 1.f, 0.8f, 0.8f, 0.9f, 0.9f};
  return kWeights[slot];
}

// fn_10_153EC (0x000153EC, 0x110 bytes): the SideDash state. Activate (`mr. r31, r5` at
// 0x000153FC) clears the dash record in place - the start and the destination to the zero vector,
// both slots to -1, the weight to lbl_10_rodata_3FC = 0.0f and the 0x80 / 0x40 flags (0x0001540C..
// 0x00015478; the steps-left word 0xA04 is left alone) - and picks a dash with no target (fn_10_154FC
// at 0x000154A8, `li r6, 0x0`) over fn_10_EDB8's weights (the loop at 0x0001547C); a pick of weight
// 0.0f (`fcmpu` at 0x000154B4) blocks the dash (0x80 at 0x000154C4). Every message, unless blocked
// (`extrwi.` at 0x000154D0), runs the dash step (fn_10_12CE0 at 0x000154E4).
// Class T (ds43): the shipped FSM2 has no SideDash node (as fn_10_EDB8 above), so retail never
// enters it; `fsm u04f7 SideDash` enters and leaves at once.
void CDarkSamus::SideDash(CStateManager& mgr, EStateMsg msg, float) {
  DsStateEntered("SideDash", msg);
  if (msg == kStateMsg_Activate) {
    mDash.mStartPosition = CVector3f::Zero();
    mDash.mDestination = mDash.mStartPosition;
    mDash.mLastSlot = -1;
    mDash.mSlot = -1;
    mDash.mWeight = 0.f;
    mDash.mBlocked = false;
    mDash.mDoubleDash = false;
    float weights[8];
    for (int i = 0; i < 8; ++i) {
      weights[i] = GetSideDashWeight(i);
    }
    if (0.f == PickDash(mgr, weights, nullptr)) {
      mDash.mBlocked = true;
    }
  }
  if (mDash.mBlocked == false) {
    UpdateDashStep(mgr, msg);
  }
}

// The base's Start body is empty, so it stands in for the module's until that one is ported.
// fn_10_F920: the state body is a single blr (0x0000F920) - the state does nothing but mark the
// entry.
// @ fn_10_F920
void CDarkSamus::Start(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("Start", msg);
  CPatterned::Start(mgr, msg, dt);
}

// fn_10_CA0C (0x0000CA0C, 0x394 bytes): the SweepBeam state. Every message first runs the attack
// bookkeeping with id 0x10000 (`lis r4, 0x1` at 0x0000CA44, fn_10_1B478 at 0x0000CA4C) and the
// melee attack command with severity 5 (`li r8, 0x5` at 0x0000CA5C; pas::kAS_MeleeAttack = 7,
// `li r5, 0x7` at 0x0000CA90; fn_10_8764 at 0x0000CAAC). Activate (`cmpwi r28, 0x0` at 0x0000CAD4)
// turns the kAR_Flinch reaction off (`li r4, 0x1` at 0x0000CAF0,
// `li r5, 0x0` at 0x0000CAF4), clears 0xDC8 bit 0x80 (`rlwimi r0, r7, 7, 24, 24` at 0x0000CB04) and
// bit 0x40 (`rlwimi r0, r7, 6, 25, 25` at 0x0000CB64), zeroes the last and current directions
// (0x0000CB2C..0x0000CB5C) and starts two emitters in the actor's area, at volume 0x7F
// (`li r6, 0x7f` at 0x0000CB28), without acoustics
// (`li r8, 0x0` at 0x0000CB30), not looped
// (`li r9, 0x0` at 0x0000CB34), kMedPriority: the beam sound 0xDD8 at the actor (the handle to
// 0xDDC at 0x0000CB88) and the end sound 0xDE0 lbl_10_rodata_2D8 = 20.0f along the actor's forward
// vector (0x0000CB94; the handle to 0xDE4 at 0x0000CC1C). It keeps the speed 0x3C8 in 0xDC4
// (0x0000CC2C) and scales it by Range(lbl_10_rodata_2DC = 0.7f, lbl_10_rodata_264 = 1.0f)
// (0x0000CC30), then runs the collision failsafe (fn_10_1AEC0 at 0x0000CC48). Update
// (`cmpwi r28, 0x1` at 0x0000CABC) aims the beam (fn_10_C2B0 at 0x0000CC5C), sweeps the damage rays
// while 0xDC8 bit 0x40 is set (`extrwi r0, r0, 1, 25` at 0x0000CC64, fn_10_C104 at 0x0000CC78) and,
// with a beam sound (`cmplwi r0, 0x0` at 0x0000CC80), moves it lbl_10_rodata_2E0 = 7.0f along the
// gun's forward vector (0x0000CC90) at volume 0x7F (`li r6, 0x7f` at 0x0000CCD4). Deactivate
// (`cmpwi r28, 0x3` at 0x0000CAE0) turns kAR_Flinch back on (`li r5, 0x1` at 0x0000CD0C), releases
// the effect (fn_10_C798 at 0x0000CD18), removes each emitter that is set
// (`cmplwi r0, 0x0` at 0x0000CD20, 0x0000CD3C), zeroes both handles (0x0000CD5C, 0x0000CD68),
// restores the speed (0x0000CD70) and runs the failsafe (0x0000CD74).
// Arms: forced calls: Activate; Update with the beam on and a sound, then off with none;
// Deactivate with both handles set (to ids StopSound ignores: the rooftop's sweep sounds give
// null handles), then zero; message 3 (ds45).
// @ fn_10_CA0C
void CDarkSamus::SweepBeam(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("SweepBeam", msg);
  UpdateCurrentAttack(0x10000, mgr, msg);
  RunMeleeAttackCmd(msg, pas::kAS_MeleeAttack, CBCMeleeAttackCmd(pas::kS_Five));
  if (msg == kStateMsg_Activate) {
    KnockBackController().EnableAnimReaction(CKnockBackMgr::kAR_Flinch, false); // 0x0000CAF4
    mSweepBeamHitPlayer = false; // 0x0000CB04
    mSweepBeamPrevDir = CVector3f::Zero();
    mSweepBeamDir = mSweepBeamPrevDir;
    mSweepBeamOn = false; // 0x0000CB64
    mSweepBeamSfx = CSfxManager::AddEmitter(mSweepBeamSfxId, GetTranslation(), 0x7F,
                                            GetCurrentAreaId().Value(), false, false,
                                            CSfxManager::kMedPriority); // 0x0000CB28
    mSweepBeamEndSfx = CSfxManager::AddEmitter(
        mSweepBeamEndSfxId, GetTranslation() + 20.f * GetTransform().GetForward(), 0x7F,
        GetCurrentAreaId().Value(), false, false, CSfxManager::kMedPriority); // 0x0000CB94
    mSweepBeamSpeed = mSpeed;                                             // 0x0000CC2C
    mSpeed = mSweepBeamSpeed * mgr.Random()->Range(0.7f, 1.f);            // 0x0000CC30
    RunCollisionFailsafe(mgr);
  } else if (msg == kStateMsg_Update) {
    UpdateSweepBeamAim(mgr, dt);
    if (mSweepBeamOn == true) { // 0x0000CC64
      SweepBeamDamage(mgr);
    }
    if (mSweepBeamSfx) { // 0x0000CC80
      const CVector3f fwd = mGunLctrXf.GetForward();
      CSfxManager::UpdateEmitter(mSweepBeamSfx, mGunLctrXf.GetTranslation() + 7.f * fwd, fwd,
                                 0x7F); // 0x0000CC90
    }
  } else if (msg == kStateMsg_Deactivate) {
    KnockBackController().EnableAnimReaction(CKnockBackMgr::kAR_Flinch, true); // 0x0000CD0C
    StopSweepBeam();
    if (mSweepBeamSfx) { // 0x0000CD20
      CSfxManager::RemoveEmitter(mSweepBeamSfx);
    }
    if (mSweepBeamEndSfx) { // 0x0000CD3C
      CSfxManager::RemoveEmitter(mSweepBeamEndSfx);
    }
    mSweepBeamSfx = CSfxHandle();
    mSweepBeamEndSfx = mSweepBeamSfx;
    mSpeed = mSweepBeamSpeed; // 0x0000CD70
    RunCollisionFailsafe(mgr);
  }
}

// fn_10_C85C (0x0000C85C, 0x1B0 bytes): releases any effect (fn_10_C798 at 0x0000C87C), makes a
// CElementGen (`li r3, 0x338` at 0x0000C890) from the 'PART' 0xD74 (`lis r5, 0x5041` at 0x0000C8A8)
// with kMOT_Normal (`li r5, 0x0` at 0x0000C8F8) and kOSF_One
// (`li r6, 0x1` at 0x0000C8FC) into 0xD78, turns its emission on
// (`li r4, 0x1` at 0x0000C9C8, vtable slot 0x2C) and sets 0xDC8 bit 0x40
// (`rlwimi r0, r3, 6, 25, 25` at 0x0000C9E4).
// Host guard: Agon's boss (10_sand_alamostorage) leaves sweepSwoosh unset (kInvalidAssetId), and a
// forced SweepBeam there built the effect from that id and crashed in CElementGen's constructor.
// The guard stays around the CElementGen only: the retail builds the effect whatever the id is
// and sets 0xDC8 bit 0x40 (0x0000C9E4) with no test of it, so the damage rays (fn_10_C104, the
// part test at 0x0000C130) and the collision response (fn_10_C470, the same bit at 0x0000C5C4)
// run as in the retail - the ray code tolerates a null generator (0x0000C32C).
// @ fn_10_C85C
void CDarkSamus::StartSweepBeam() {
  StopSweepBeam();
  if (mSweepBeamPart != kInvalidAssetId) {
    mSweepBeamGen = new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', mSweepBeamPart)),
                                    CElementGen::kMOT_Normal, CElementGen::kOSF_One);
    mSweepBeamGen->SetParticleEmission(true); // 0x0000C9C8
  }
  mSweepBeamOn = true; // 0x0000C9E4
}

// fn_10_C798 (0x0000C798, 0xC4 bytes): the effect 0xD78 reset to null (the auto_ptr's
// self-assignment test `cmplw r3, r0` at 0x0000C7BC; an owned old effect,
// `cmplwi r0, 0x0` at 0x0000C7CC and
// `cmplwi r3, 0x0` at 0x0000C7D8, deleted at 0x0000C7F0) and 0xDC8 bit 0x40 cleared
// (`rlwimi r0, r3, 6, 25, 25` at 0x0000C840).
// @ fn_10_C798
void CDarkSamus::StopSweepBeam() {
  mSweepBeamGen = nullptr;
  mSweepBeamOn = false; // 0x0000C840
}

// fn_10_C2B0 (0x0000C2B0, 0x1C0 bytes): finds the end point (fn_10_C470 at 0x0000C2D0), then the
// direction 0xD9C = end point 0xDB4 - the gun's position (the 0x8CC transform's origin,
// 0x0000C2F8..0x0000C31C) and the length 0xDC0 its magnitude (0x0000C324). With an effect
// (`cmplwi r3, 0x0` at 0x0000C32C) the effect goes to the gun (slot 0x20 at 0x0000C33C), looks at
// the end point (LookAt with the up vector at 0x0000C35C, slot 0x1C at 0x0000C378), is scaled to
// (lbl_10_rodata_278 = 4.0f, lbl_10_rodata_2D0 = 0.18f * length, 4.0f) (slot 0x24 at 0x0000C3B4)
// and updated by dt (slot 0xC at 0x0000C3CC). The direction is normalized when it can be
// (`cmplwi r0, 0x1` at 0x0000C3E4). With an end sound
// (`cmplwi r0, 0x0` at 0x0000C3F8) it moves to the end point along lbl_10_rodata_288 = -1.0f times
// the actor's forward vector (0x0000C410) at volume 0x7F (`li r6, 0x7f` at 0x0000C42C).
// Arms: the fight, plus a gun facing straight up (no normalize) and an end sound handle (ds43 gdb
// calls).
// @ fn_10_C2B0
void CDarkSamus::UpdateSweepBeamAim(CStateManager& mgr, float dt) {
  UpdateSweepBeamEnd(mgr);
  const CVector3f gunPos = mGunLctrXf.GetTranslation();
  const CVector3f end = mSweepBeamEnd;
  mSweepBeamDir = end - gunPos;
  mSweepBeamLength = mSweepBeamDir.Magnitude();
  if (CElementGen* gen = mSweepBeamGen.get()) { // 0x0000C32C
    gen->SetGlobalTranslation(gunPos);
    gen->SetGlobalOrientation(CTransform4f::LookAt(gunPos, end, CVector3f::Up()));
    gen->SetGlobalScale(CVector3f(4.f, 0.18f * mSweepBeamLength, 4.f)); // 0x0000C39C
    gen->Update(dt);
  }
  if (mSweepBeamDir.CanBeNormalized() == true) { // 0x0000C3E4
    mSweepBeamDir.Normalize();
  }
  if (mSweepBeamEndSfx) { // 0x0000C3F8
    const CVector3f fwd = GetTransform().GetForward();
    CSfxManager::UpdateEmitter(mSweepBeamEndSfx, mSweepBeamEnd, -1.f * fwd, 0x7F); // 0x0000C42C
  }
}

// fn_10_C470 (0x0000C470, 0x328 bytes): the beam's end point. The ray runs from the gun along the
// gun's forward vector with its z set to lbl_10_rodata_25C = 0.0f (0x0000C4BC), normalized when it
// can be (`cmplwi r0, 0x1` at 0x0000C4C8), for lbl_10_rodata_2D4 = 100.0f (0x0000C580) against a
// filter built once (the guard `extsb. r0, r0` at 0x0000C4E0): include lbl_10_data_1E4 = 0x3B
// (kMT_Solid), exclude lbl_10_data_1E8 = 0x20 (kMT_Player) and lbl_10_data_1EC = 0x2F
// (kMT_CollisionActor), kFT_IncludeExclude (`li r7, 0x3` at 0x0000C53C). On a hit
// (`cmplwi r0, 0x1` at 0x0000C5B8) while 0xDC8 bit 0x40 is set
// (`cmplwi r0, 0x1` at 0x0000C5C8) the response 0xDCC runs for the hit with no target, the beam
// damage and false (`li r8, 0x0` at 0x0000C5EC; DoCollisionResponse at 0x0000C5F4) and its decal
// for type 1 (`li r5, 0x1` at 0x0000C600), when it has one (`cmplwi r0, 0x0` at 0x0000C664), goes
// at the hit point facing the hit plane's normal (LookAt from the origin with the up vector at
// 0x0000C684; AddDecal at 0x0000C6D4, the ray's direction unnormalized). The end point is the hit
// point on any hit (0x0000C6FC..0x0000C710), else the gun plus 100.0f times the direction
// (0x0000C718..0x0000C758); its z is then the gun's (0x0000C774).
// Arms: the fight (no hit, beam off), plus forced calls: a hit with the beam on and off, the
// sweep CRSC's type-1 decal missing and (the player's power beam CRSC entry swapped in) present
// (ds45). Not run: the static filter's guard-abort arm and the decal path's exception pads (host
// compiler arms).
// @ fn_10_C470
void CDarkSamus::UpdateSweepBeamEnd(CStateManager& mgr) {
  CVector3f dir(mGunLctrXf.GetForward().GetX(), mGunLctrXf.GetForward().GetY(), 0.f);
  if (dir.CanBeNormalized() == true) { // 0x0000C4C8
    dir.Normalize();
  }
  static const CMaterialFilter skFilter(CMaterialList(kMT_Solid),
                                        CMaterialList(kMT_Player, kMT_CollisionActor),
                                        CMaterialFilter::kFT_IncludeExclude); // 0x0000C53C
  const CVector3f gunPos = mGunLctrXf.GetTranslation();
  const CRayCastResult result = mgr.RayStaticIntersection(gunPos, dir, 100.f, skFilter);
  if (result.IsValid() == true) { // 0x0000C5B8
    if (mSweepBeamOn == true) {   // 0x0000C5C8
      mgr.DoCollisionResponse(**mSweepBeamResponse, result, kInvalidUniqueId, mSweepBeamDamageInfo,
                              false);
      const rstl::optional_object< TLockedToken< CDecalDescription > > decal =
          mSweepBeamResponse->GetDecalDescription(static_cast< EWeaponCollisionResponseTypes >(1));
      if (decal.valid()) { // 0x0000C664
        CTransform4f xf =
            CTransform4f::LookAt(CVector3f::Zero(), result.GetPlane().GetNormal(), CVector3f::Up());
        xf.SetTranslation(result.GetPoint());
        CDecalManager::AddDecal(*decal, xf, CUnitVector3f(dir, CUnitVector3f::kN_No), mgr);
      }
    }
    mSweepBeamEnd = result.GetPoint();
  } else {
    mSweepBeamEnd = gunPos + 100.f * dir; // 0x0000C720
  }
  mSweepBeamEnd.SetZ(gunPos.GetZ()); // 0x0000C774
}

// fn_10_C104 (0x0000C104, 0x1AC bytes): with a beam effect asset 0xD74
// (`cmplwi r0, 0x0` at 0x0000C130) and a last direction 0xDA8 that is not the zero vector
// (`fcmpu cr0, f1, f0` at 0x0000C148, 0x0000C158, 0x0000C168) the angle between the last and this
// direction (GetAngleDiff at 0x0000C184) is halved (lbl_10_rodata_284 = 0.5f) and the ray count
// doubled until it is at most lbl_10_rodata_2C0 = 0.05235988f (3 degrees,
// `fcmpo cr0, f1, f0` at 0x0000C1A8); with more than one ray (`cmpwi r27, 0x1` at 0x0000C1B0) the
// rays i = 1..n-1 (`cmpw r26, r27` at 0x0000C258) run along last * (lbl_10_rodata_264 = 1.0f - i/n)
// + this * i/n for the length 0xDC0 (fn_10_BE18 at 0x0000C250). Then the ray along this direction
// (0x0000C270), and this direction becomes the last (0x0000C274..0x0000C288).
// Arms: the fight, plus no beam asset (ds43 gdb call). Retail interpolates with fmadds (fused); the
// host multiplies and adds (a known fma knife-edge, as the mathdiff ruling).
// @ fn_10_C104
void CDarkSamus::SweepBeamDamage(CStateManager& mgr) {
  if (mSweepBeamPart == 0) { // 0x0000C130
    return;
  }
  if (!(mSweepBeamPrevDir == CVector3f::Zero())) { // 0x0000C148
    float angle = CVector3f::GetAngleDiff(mSweepBeamDir, mSweepBeamPrevDir);
    int count = 1;
    while (angle > 0.05235988f) { // 0x0000C1A8
      angle *= 0.5f;              // lbl_10_rodata_284 (0x0000C190)
      count <<= 1;
    }
    if (count > 1) { // 0x0000C1B0
      for (int i = 1; i < count; ++i) { // 0x0000C258
        const float t = static_cast< float >(i) / static_cast< float >(count);
        const float u = 1.f - t; // 0x0000C224
        const CVector3f dir(mSweepBeamPrevDir.GetX() * u + mSweepBeamDir.GetX() * t,
                            mSweepBeamPrevDir.GetY() * u + mSweepBeamDir.GetY() * t,
                            mSweepBeamPrevDir.GetZ() * u + mSweepBeamDir.GetZ() * t);
        SweepBeamRay(mgr, dir, mSweepBeamLength);
      }
    }
  }
  SweepBeamRay(mgr, mSweepBeamDir, mSweepBeamLength);
  mSweepBeamPrevDir = mSweepBeamDir;
}

// fn_10_BE18 (0x0000BE18, 0x2EC bytes): one damage ray from the gun along dir for length. The near
// list and the dynamic ray share a filter built once (the guard `extsb. r0, r0` at 0x0000BE48):
// include lbl_10_data_1F0 = 0x20 (kMT_Player) and lbl_10_data_1F4 = 0x31 (kMT_Platform),
// kFT_Include (`li r7, 0x1` at 0x0000BE88); the near list ignores no actor
// (`li r8, 0x0` at 0x0000BEEC). A hit on the player (`cmplw r4, r0` at 0x0000BF64) takes the beam
// damage 0xD80 once per beam (0xDC8 bit 0x80, `extrwi. r0, r0, 1, 24` at 0x0000BF70; set by
// `rlwimi r0, r3, 7, 24, 24` at 0x0000BFF8). Any other hit (`cmplw r4, r0` at 0x0000C00C, against
// kInvalidUniqueId) takes lbl_10_rodata_294 = 1000.0f (0x0000C034) with radius and knock-back
// lbl_10_rodata_25C = 0.0f, no immunity (`rlwimi r4, r0, 7, 24, 24` at 0x0000C03C), no radius
// damage (`rlwimi r0, r3, 6, 25, 25` at 0x0000C054) and the zeroed weapon mode lbl_10_bss_114
// (0x0000C024). Both go through ApplyDamage from the actor with a filter including lbl_10_data_1E0
// = 0x3B (kMT_Solid), kFT_IncludeExclude (`li r10, 0x3` at 0x0000BF94, 0x0000C08C), and the zero
// vector.
// @ fn_10_BE18
void CDarkSamus::SweepBeamRay(CStateManager& mgr, const CVector3f& dir, float length) {
  static const CMaterialFilter skFilter = CMaterialFilter::MakeInclude(
      CMaterialList(kMT_Player, kMT_Platform)); // 0x0000BE88
  const CVector3f gunPos = mGunLctrXf.GetTranslation();
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, gunPos, dir, length, skFilter, nullptr);
  TUniqueId hit = kInvalidUniqueId;
  CGameCollision::RayDynamicIntersection(mgr, hit, gunPos, dir, length, skFilter, nearList);
  const CMaterialFilter damageFilter(CMaterialList(kMT_Solid), CMaterialList(),
                                     CMaterialFilter::kFT_IncludeExclude); // 0x0000BF94
  if (hit == mgr.GetPlayer(0)->GetUniqueId()) { // 0x0000BF64
    if (mSweepBeamHitPlayer) { // 0x0000BF70
      return;
    }
    mgr.ApplyDamage(GetUniqueId(), hit, GetUniqueId(), mSweepBeamDamageInfo, damageFilter,
                    CVector3f::Zero());
    mSweepBeamHitPlayer = true; // 0x0000BFF8
  } else if (hit != kInvalidUniqueId) { // 0x0000C00C
    // 0x0000C018/0x0000C024: the module's weapon mode (lbl_10_bss_114, the type 10 =
    // kWT_Phazon - the guest keeps the type in the high half of the word) and the store into
    // the damage info at 0x0000C05C.
    const CDamageInfo info(mSweepBeamWeaponMode, 1000.f, 0.f, 0.f, true); // 0x0000C034
    mgr.ApplyDamage(GetUniqueId(), hit, GetUniqueId(), info, damageFilter, CVector3f::Zero());
  }
}

// fn_10_1DF74 (0x0001DF74, 0x204 bytes): the state that glides the actor from cover point to
// cover point.
//
// The additive animation runs first (fn_10_1D860 at 0x0001DFA8). kStateMsg_Activate keeps the speed
// (the base's 0x3C8 into the guest's 0x96C, 0x0001DFC4), speeds up by lbl_10_rodata_4DC = 1.1f
// (0x0001DFD8) and keeps the safe position (fn_10_128D8 at 0x0001DFDC). kStateMsg_Deactivate puts
// the speed back (0x0001DFF0), stops the glide emitter (fn_10_11334 at 0x0001E004), drops the cover
// point (0x0001E010) and is done. Any other message (and Activate) clears the byte at 0xCD0
// (0x0001E028) and the float at 0xC00 (lbl_10_rodata_3FC = 0.0f, 0x0001E030), forgets the cover
// point when fn_10_1E740 says so (0x0001E050) and, without one, pushes the best one (fn_10_1E3C0
// and fn_10_1E26C at 0x0001E074/0x0001E088). A cover point that is gone (GetCoverPoint at
// 0x0001E0A0) ends the call. On Activate the wall hit time becomes lbl_10_rodata_410 = -1000.0f
// (0x0001E0BC). On Activate or a new cover point (the uid read at 0x0001E034 against the current
// one, 0x0001E0CC) the path find heads for the cover point (fn_10_1C778 at 0x0001E0E4) and the word
// at 0x9FC becomes -1 (0x0001E0EC); otherwise the path find faces the player (the player's uid into
// the navigation's face target, 0x0001E110) and runs (0x0001E114), and the actor separates
// (fn_10_1DD14 at 0x0001E120). Then the damage (fn_10_1E178), the glide emitter (fn_10_11334) and
// the still timer (fn_10_1D794) are updated with the message.
// @ fn_10_1DF74
void CDarkSamus::TravelToCoverPoints(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("TravelToCoverPoints", msg);
  UpdateAdditiveAnimation(mgr, msg);
  if (msg == kStateMsg_Activate) {
    mSavedSpeed = mSpeed;
    mSpeed = 1.1f * mSpeed;
    UpdateSafePosition(mgr);
  } else if (msg == kStateMsg_Deactivate) {
    mSpeed = mSavedSpeed;
  }
  if (msg == kStateMsg_Deactivate) {
    UpdateGlideEmitter(msg);
    mLastAttackerUid = kInvalidUniqueId;
    return;
  }
  mGuestCD0 = 0;
  mGuestC00 = 0.f;
  const TUniqueId prev = mLastAttackerUid;
  if (ShouldForgetLastAttacker(mgr) == true) { // 0x0001E044
    mLastAttackerUid = kInvalidUniqueId;
  }
  if (mLastAttackerUid == kInvalidUniqueId) { // 0x0001E064
    PushAttacker(FindBestCoverPoint(mgr));
  }
  const CScriptCoverPoint* cover = GetCoverPoint(mgr, mLastAttackerUid);
  if (cover == nullptr) { // 0x0001E0A8
    return;
  }
  if (msg == kStateMsg_Activate) {
    mLastWallHitTime = -1000.f;
  }
  if (msg == kStateMsg_Activate || prev != mLastAttackerUid) { // 0x0001E0C4/0x0001E0D0
    SetPathFindDestination(mgr, cover->GetTranslation(), dt);
    mDash.mLastSlot = -1;
  } else {
    mPathFindNavigation.SetFaceTarget(mgr.GetPlayer(0)->GetUniqueId());
    mPathFindNavigation.PathFind(mgr, msg, dt, *this);
    Separate(mgr);
  }
  AccumulateDamage(msg, dt);
  UpdateGlideEmitter(msg);
  UpdateStillTimer(msg, dt);
}

// fn_10_FB8C (0x0000FB8C, 0xD8 bytes): the TravelToPissedOff state: the attack bookkeeping with
// 0x2000 (fn_10_1B478, 0x0000FBC4), the shield flag (fn_10_105F4, 0x0000FBD8), then the path to
// the area hint of type 0xE (fn_10_FC64, 0x0000FBE8): Activate (0x0000FBF8) sets the destination
// (fn_10_1C778, 0x0000FC0C), every other message faces the hint (the uid to the navigation's face
// target 0x718, 0x0000FC24) and runs the path find (0x0000FC30). No hint skips the path. Last the
// still timer (fn_10_1D794, 0x0000FC40).
// @ fn_10_FB8C
void CDarkSamus::TravelToPissedOff(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("TravelToPissedOff", msg);
  UpdateCurrentAttack(0x2000, mgr, msg);
  UpdateShieldFlag(msg);
  if (const CScriptAIHint* hint = FindAreaHint(mgr, 0xE)) {
    if (msg == kStateMsg_Activate) {
      SetPathFindDestination(mgr, hint->GetTranslation(), dt);
    } else {
      mPathFindNavigation.SetFaceTarget(hint->GetUniqueId());
      mPathFindNavigation.PathFind(mgr, msg, dt, *this);
    }
  }
  UpdateStillTimer(msg, dt);
}

// fn_10_FE18: the TurnToFacePlayer state. The activate half (0x0000FE40) sets the flag byte's
// bit 7 and clears the turn flag, and the turn ends at once when the player is within 90 degrees
// of the actor's facing axis (fn_10_142F4, the call at 0x0000FE60); otherwise the taunt command
// (fn_10_8698, the call at 0x0000FEBC) runs for every message until the base's AnimOver (the
// call at 0x0000FEEC) answers true - the animation state the command advances is the byte the
// base's GetAnimOver reads, so the taunt ends the turn when it is over.
// @ fn_10_FE18
void CDarkSamus::TurnToFacePlayer(CStateManager& mgr, EStateMsg msg, float dt) {
  DsStateEntered("TurnToFacePlayer", msg);
  if (msg == kStateMsg_Activate) {
    // 0x0000FE40: the flag byte's bit 7 (the rlwimi over the byte's bit 24) and the turn flag
    // cleared (the stb at 0x0000FE5C).
    mFlags90C |= 0x80;
    mTurnDone = 0;
    // 0x0000FE60: fn_10_142F4 - the player within 90 degrees (rodata_43C = 1.5707964) of the
    // actor's facing axis ends the turn at once.
    if (IsPlayerInFront(mgr, 1.5707964f)) {
      mTurnDone = 1;
    }
  }
  if (mTurnDone == 0) {
    // fn_10_8698: the taunt command, which the state runs for every message while the turn is
    // not done. The guest's byte at 0x6B4 is the base's animation state - the byte fn_10_8698
    // stores 1 to on Activate (0x000087E0) and 0 to on Deactivate (0x00008820), and the DOL's
    // ShouldRetry (the call at 0x00008710, with 0x10 = pas::kAS_Taunt) advances it to kAS_Repeat
    // and then to kAS_Over. It is the member the base's AnimOver reads
    // (CPatterned::GetAnimOver = mAnimationState.IsOver()), so the host drives it the way the
    // other ported actors do (CDarkCommando::Lurk).
    switch (msg) {
    case kStateMsg_Activate:
      mAnimationState.SetState(CAnimationState::kAS_Ready);
      BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(pas::kTT_Six));
      break;
    case kStateMsg_Update:
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Taunt)) {
        BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(pas::kTT_Six));
      }
      break;
    case kStateMsg_Deactivate:
      mAnimationState.SetState(CAnimationState::kAS_NotReady);
      break;
    }
    // 0x0000FEEC: fn_801519A4, the base's AnimOver - the animation over ends the turn.
    if (AnimOver(mgr, CTriggerData(0))) {
      mTurnDone = 1;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// The attack picker's leaves. An attack is a bit (1 << i, i < 24 - the `cmpwi 0x18` loops) of a
// mask the collectors below clear or set, and a weight from the stage info.

// fn_10_1AA30 (0x0001AA30, 0x2C bytes): `set` (the `clrlwi 24` at 0x0001AA30) == 1 ors the bit
// into the mask (0x0001AA40), anything else clears it (the andc at 0x0001AA50).
// @ fn_10_1AA30
void CDarkSamus::SetAttackBit(uint* mask, uchar set, uint bit) {
  if (set == 1) {
    *mask |= bit;
  } else {
    *mask &= ~bit;
  }
}

// fn_10_1A980 (0x0001A980, 0xB0 bytes): the first bit always, the next three up to the first
// that is 0xFFFF (the whole-word compares at 0x0001A9B0/0x0001A9D8/0x0001AA00).
// @ fn_10_1A980
void CDarkSamus::SetAttackBits(uint* mask, uchar set, uint a, uint b, uint c, uint d) {
  SetAttackBit(mask, set, a);
  if (b != 0xFFFF) {
    SetAttackBit(mask, set, b);
    if (c != 0xFFFF) {
      SetAttackBit(mask, set, c);
      if (d != 0xFFFF) {
        SetAttackBit(mask, set, d);
      }
    }
  }
}

// fn_10_1A8F8 (0x0001A8F8, 0x88 bytes): every one of the 24 bits (0x0001A964) except the four
// given (the cmpw chain at 0x0001A930..0x0001A948).
// @ fn_10_1A8F8
void CDarkSamus::SetAttackBitsExcept(uint* mask, uchar set, uint a, uint b, uint c, uint d) {
  for (int i = 0; i < 24; ++i) {
    const uint bit = 1u << i;
    if (bit != a && bit != b && bit != c && bit != d) {
      SetAttackBit(mask, set, bit);
    }
  }
}

// fn_10_1AA5C (0x0001AA5C, 0x10 bytes): the bit is 2 (the subfic/cntlzw at 0x0001AA5C).
// @ fn_10_1AA5C
bool CDarkSamus::IsAttackTwo(uint bit) { return bit == 2; }

// fn_10_10BBC (0x00010BBC): the index of the bit (the first i < 24 with 1 << i == bit), else
// 0xFFFF.
// @ fn_10_10BBC
int CDarkSamus::GetAttackBitIndex(uint bit) {
  for (int i = 0; i < 24; ++i) {
    if ((1u << i) == bit) {
      return i;
    }
  }
  return 0xFFFF;
}

// fn_10_1A6C4 (0x0001A6C4, 0x234 bytes): the stage info's weight of an attack (the switch's lfs
// offsets: 1 0x44, 4 0x48, 8 0x4C, 0x10 0x50, 0x20 0x54, 0x40 0x5C, 0x80 0x60, 0x100 0x64,
// 0x200 0x3C, 0x400 0x38, 0x10000 0x70, 0x20000 0x74, 0x100000 0x68, 0x200000 0x6C, 0x400000
// 0x7C, 0x800000 0x80); every other id answers lbl_10_rodata_3FC = 0.0f.
// @ fn_10_1A6C4
float CDarkSamus::GetAttackWeight(const SLdrDSStageInfo& stage, int attack) {
  switch (attack) {
  case 0x1: return stage.meleeAttack;                   // 0x0001A86C
  case 0x4: return stage.meleeDash;                     // 0x0001A874
  case 0x8: return stage.scatterShot;                   // 0x0001A87C
  case 0x10: return stage.movingScatterShot;            // 0x0001A884
  case 0x20: return stage.diveAttack;                   // 0x0001A88C
  case 0x40: return stage.normalMissile;                // 0x0001A894
  case 0x80: return stage.missileJump;                  // 0x0001A89C
  case 0x100: return stage.superMissile;                // 0x0001A8A4
  case 0x200: return stage.taunt;                       // 0x0001A8AC
  case 0x400: return stage.pause;                       // 0x0001A8B4
  case 0x10000: return stage.sweepBeam;                 // 0x0001A8BC
  case 0x20000: return stage.boostBall;                 // 0x0001A8C4
  case 0x100000: return stage.freezeBeamWhenInvisible;  // 0x0001A8CC
  case 0x200000: return stage.normalMissileWhenInvisible; // 0x0001A8D4
  case 0x400000: return stage.phazonAttack;             // 0x0001A8DC
  case 0x800000: return stage.phazonEnrage;             // 0x0001A8E4
  default: return 0.f;                                  // 0x0001A8EC
  }
}

// fn_10_17D50 (0x00017D50, 0x90 bytes): no stage answers false (0x00017D78); 0x800 is the stage's
// lookAround byte (the lbz 0x40 at 0x00017DA0), 2 is false (0x00017D98), every other attack is
// on when its weight is above lbl_10_rodata_3FC = 0.0f (the fcmpo's gt bit, 0x00017DC4).
// @ fn_10_17D50
bool CDarkSamus::IsAttackEnabled(CStateManager& mgr, int attack) const {
  const CDarkSamusBattleStage* stage = GetBattleStage(mgr);
  if (stage == nullptr) {
    return false;
  }
  switch (attack) {
  case 0x800:
    return stage->GetStageInfo().lookAround;
  case 0x2:
    return false;
  default:
    return GetAttackWeight(stage->GetStageInfo(), attack) > 0.f;
  }
}

// fn_10_11308 (0x00011308, 0x2C bytes): the player (the state manager's 0x14FC) is morphed - its
// 0x390 is 0 and its 0x38C is 1 (the subfic/cntlzw at 0x00011324).
// @ fn_10_11308
bool CDarkSamus::IsPlayerMorphed(CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer(0);
  const int state = player->GetSpawnedMorphballState() == 0 // 0x00011310
                        ? static_cast< int >(player->GetMorphballTransitionState())
                        : 0;
  return state == 1;
}

// fn_10_17CEC (0x00017CEC, 0x64 bytes): the distance from the player (0x14FC) to the actor
// (the fsubs of 0x54/0x58/0x5C and Magnitude at 0x00017D3C). It has no branch, and it runs in
// every fight (the attack pick and the triggers call it).
// @ fn_10_17CEC
float CDarkSamus::GetDistanceToPlayer(CStateManager& mgr) const {
  return (GetTranslation() - mgr.GetPlayer(0)->GetTranslation()).Magnitude();
}

// fn_10_1AA6C (0x0001AA6C, 0x58 bytes): 0 below the attack range (0x3DC, the bge at 0x0001AA8C),
// 2 above it (0x3E0, the ble at 0x0001AAA0), 1 inside.
// @ fn_10_1AA6C
int CDarkSamus::GetPlayerRangeBand(CStateManager& mgr) const {
  const float dist = GetDistanceToPlayer(mgr);
  if (dist < mMinAttackRange) {
    return 0;
  }
  if (dist > mMaxAttackRange) {
    return 2;
  }
  return 1;
}

// fn_10_1A5BC (0x0001A5BC, 0x108 bytes): one weight per bit (0x0001A6A8). An attack with no stage
// (0x0001A5F8), the attack 2 (fn_10_1AA5C, 0x0001A610) or one the stage turns off (fn_10_17D50,
// 0x0001A628) gets lbl_10_data_C4C = 0; the others get their stage weight (fn_10_1A6C4, the
// fctiwz at 0x0001A670) and their bit is added to the mask (the add at 0x0001A69C).
// @ fn_10_1A5BC
void CDarkSamus::CollectStageWeights(CStateManager& mgr, SAttackWeights& weights,
                                     uint* mask) const {
  for (int i = 0; i < 24; ++i) {
    const uint bit = 1u << i;
    if (GetBattleStage(mgr) == nullptr || IsAttackTwo(bit) == true ||
        !IsAttackEnabled(mgr, bit)) {
      weights.weights[weights.count] = 0; // lbl_10_data_C4C
      weights.count++;
    } else {
      weights.weights[weights.count] =
          static_cast< int >(GetAttackWeight(GetBattleStage(mgr)->GetStageInfo(), bit));
      weights.count++;
      *mask += bit;
    }
  }
}

// fn_10_1A554 (0x0001A554, 0x68 bytes): faded (0xDEC == 1, 0x0001A564) the mask keeps only the
// freeze beam, the melee and the phazon attacks (fn_10_1A8F8 with 0x100000, 1, 0x400000,
// 0x800000); visible it loses the freeze beam and the fade missile (0x100000, 0x200000).
// fn_10_1A554 (0x0001A554, 0x68 bytes): the fade attacks (the fade flag at 0xDEC).
// Arms: the actor is fading out (the flag is one) and it is not.
// // The arms: the coverage run (tools/cov/specs/darksamus_helpers2.py, the rooftop fight: Sanctuary 10_cliff_rooftop, boss u04f7, 6400 updates) showed the "not fading" arm; the fade-out arm was forced (the flag
// written at the entry, tools/cov/specs/darksamus_helpers2_force.py).
// @ fn_10_1A554
void CDarkSamus::CollectFadeAttacks(CStateManager&, SAttackWeights&, uint* mask) const {
  if (mFadeOut == 1) {
    SetAttackBitsExcept(mask, 0, 0x100000, 1, 0x400000, 0x800000);
  } else {
    SetAttackBits(mask, 0, 0x100000, 0x200000, 0xFFFF, 0xFFFF);
  }
}

// fn_10_1A278 (0x0001A278, 0xD4 bytes): a morphed player (fn_10_11308, 0x0001A29C) takes away the
// scatter shots and the dive (8, 0x10, 0x20), the missile jump, the super missile, the taunt and
// the sweep beam (0x80, 0x100, 0x200, 0x10000) and turns the normal missile on (0x40, set 1 at
// 0x0001A2FC); otherwise the melee dash (4) goes.
// @ fn_10_1A278
void CDarkSamus::CollectMorphballAttacks(CStateManager& mgr, SAttackWeights&, uint* mask) const {
  if (IsPlayerMorphed(mgr) == true) {
    SetAttackBits(mask, 0, 0x8, 0x10, 0x20, 0xFFFF);
    SetAttackBits(mask, 0, 0x80, 0x100, 0x200, 0x10000);
    SetAttackBits(mask, 1, 0x40, 0xFFFF, 0xFFFF, 0xFFFF);
  } else {
    SetAttackBits(mask, 0, 0x4, 0xFFFF, 0xFFFF, 0xFFFF);
  }
}

// fn_10_1A440 (0x0001A440, 0x114 bytes): by the range band (fn_10_1AA6C): close (0) keeps only
// the melee, the attack 2 and the look-around (1, 2, 0x800); in range (1) loses the melee; far
// (2) loses 1, 4, 8, 0x10, then 0x20, 0x40, 0x100, then 0x200000, 0x100000.
// @ fn_10_1A440
void CDarkSamus::CollectRangeAttacks(CStateManager& mgr, SAttackWeights&, uint* mask) const {
  switch (GetPlayerRangeBand(mgr)) {
  case 0:
    SetAttackBitsExcept(mask, 0, 0x1, 0x2, 0x800, 0xFFFF);
    break;
  case 1:
    SetAttackBits(mask, 0, 0x1, 0xFFFF, 0xFFFF, 0xFFFF);
    break;
  case 2:
    SetAttackBits(mask, 0, 0x1, 0x4, 0x8, 0x10);
    SetAttackBits(mask, 0, 0x20, 0x40, 0x100, 0xFFFF);
    SetAttackBits(mask, 0, 0x200000, 0x100000, 0xFFFF, 0xFFFF);
    break;
  }
}

// fn_10_1A34C (0x0001A34C, 0xF4 bytes): the finished attacks (0x998, the count at 0x994), newest
// first, lower their weights - the newest by 15, the others by 5 (0x0001A388/0x0001A390) - except
// the melee, the melee dash and the missile jump (1, 4, 0x80: 0x0001A398..0x0001A3A8). A melee as
// the newest one within lbl_10_rodata_544 = 2.5f of 0x9C0 (the fcmpo at 0x0001A3F0) clears the
// melee bit.
// @ fn_10_1A34C
void CDarkSamus::CollectHistoryPenalties(CStateManager&, SAttackWeights& weights,
                                         uint* mask) const {
  const int last = static_cast< int >(mAttackHistoryCount) - 1;
  for (int i = last; i >= 0; --i) {
    const int penalty = i == last ? -15 : -5;
    const int attack = static_cast< int >(mAttackHistory[i]);
    if (attack != 1 && attack != 4 && attack != 0x80) {
      weights.weights[GetAttackBitIndex(attack)] += penalty;
    }
    if (attack == 1 && i == last && mLastMeleeTime + 2.5f > mElapsedTime) {
      SetAttackBits(mask, 0, 0x1, 0xFFFF, 0xFFFF, 0xFFFF);
    }
  }
}

// fn_10_1A1D4 (0x0001A1D4, 0xA4 bytes): the look-around (0x800) goes; a player outside
// lbl_10_rodata_4C4 = 0.17453292f (10 degrees) of the facing (fn_10_142F4, 0x0001A234) leaves only
// the melee (fn_10_1A8F8 with 1).
// @ fn_10_1A1D4
void CDarkSamus::CollectFacingAttacks(CStateManager& mgr, SAttackWeights&, uint* mask) const {
  SetAttackBits(mask, 0, 0x800, 0xFFFF, 0xFFFF, 0xFFFF);
  if (!IsPlayerInFront(mgr, 0.17453292f)) {
    SetAttackBitsExcept(mask, 0, 0x1, 0xFFFF, 0xFFFF, 0xFFFF);
  }
}

// fn_10_19918 (0x00019918, 0x104 bytes): the weighted pick. The masked attacks' weights are
// raised to at least 1 (0x00019968), the others zeroed (0x00019978); with no weight left the pick
// is 0xFFFF (0x0001999C). A draw of the state manager's random (the CRandom16 at 0x16E4,
// Next at 0x000199A8) modulo the total (the divw/mullw at 0x000199AC) walks the positive weights
// until it is used up (the subf. at 0x000199D8) and picks that attack's bit.
// @ fn_10_19918
int CDarkSamus::PickWeightedAttack(CStateManager& mgr, SAttackWeights& weights, uint mask) const {
  int total = 0;
  for (int i = 0; i < weights.count; ++i) {
    if ((mask & (1u << i)) != 0) {
      if (weights.weights[i] <= 0) {
        weights.weights[i] = 1;
      }
      total += weights.weights[i];
    } else {
      weights.weights[i] = 0;
    }
  }
  if (total == 0) {
    return 0xFFFF;
  }
  int draw = mgr.Random()->Next() % total;
  for (int i = 0; i < weights.count; ++i) {
    if (weights.weights[i] > 0) {
      draw -= weights.weights[i];
      if (draw <= 0) {
        return 1 << i;
      }
    }
  }
  return 0xFFFF;
}

// fn_10_1905C (0x0001905C, 0x3C4 bytes): the flat direction to the player (the z cleared at
// 0x000190F8; not normalizable answers 0, 0x00019154). The rays start lbl_10_rodata_414 = 8.0f
// short of the player (at least at the actor, the bge at 0x0001911C) and are
// lbl_10_rodata_40C = 5.0f longer than the player's distance; one per height of
// lbl_10_rodata_334 = {1.0f, 2.5f, 4.0f} (the cmpwi 3 at 0x00019318), filtered to include
// only the material lbl_10_data_C50 = 0x31 (the __shl2i at 0x00019170, the filter type 1 at
// 0x000191C8). The nearest hit (from the unshifted start) that is not 8 short of the player
// (the blt at 0x000192F0) is kept (lbl_10_rodata_3EC = -1.0f = none, 0x00019300); none is 0
// (0x0001932C), short of the player 1 (0x0001933C), else 2.
// Arms: the fight, plus forced calls: the actor on the player's xy, the ray hits forced near and
// far, an inf x (ds45).
// @ fn_10_1905C
int CDarkSamus::ClassifyLineToPlayer(CStateManager& mgr) const {
  CVector3f dir = mgr.GetPlayer(0)->GetTranslation() - GetTranslation();
  dir.SetZ(0.f);
  const float dist = dir.Magnitude();
  float back = dist - 8.f;
  if (back < 0.f) { // 0x0001911C
    back = 0.f;
  }
  const float length = (5.f + dist) - back;
  if (dir.CanBeNormalized() != true) { // 0x00019144
    return 0;
  }
  dir.Normalize();
  const CMaterialFilter filter =
      CMaterialFilter::MakeInclude(CMaterialList(static_cast< EMaterialTypes >(0x31)));
  static const float heights[3] = {1.f, 2.5f, 4.f}; // lbl_10_rodata_334
  float best = -1.f;
  for (int i = 0; i < 3; ++i) {
    const CVector3f origin = GetTranslation() + CVector3f(0.f, 0.f, heights[i]);
    const CVector3f start = origin + back * dir;
    rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
    mgr.BuildNearList(nearList, start, dir.AsNormalized(), length, filter, nullptr);
    TUniqueId hitId = kInvalidUniqueId;
    const CRayCastResult result =
        mgr.RayWorldIntersection(hitId, start, dir, length, filter, nearList);
    if (result.IsValid() == true) { // 0x000192A0
      const float hitDist = (result.GetPoint() - origin).Magnitude();
      if (!(hitDist < dist - 8.f) && (best < 0.f || hitDist < best)) { // 0x000192F0..0x00019308
        best = hitDist;
      }
    }
  }
  if (best < 0.f) { // 0x0001932C
    return 0;
  }
  if (best < dist) { // 0x0001933C
    return 1;
  }
  return 2;
}

// fn_10_19A1C (0x00019A1C, 0x148 bytes): a repulsion point (fn_10_1D9A4, any but the zero vector
// - the fcmpu chain at 0x00019A70..0x00019A8C) leaves only the melee (fn_10_1A8F8 with 1,
// 0x00019AC0). Otherwise the line to the player (fn_10_1905C, 0x00019AD0): blocked short of it
// (1) adds 10 to the normal and the super missile's weights (0x40, 0x100: 0x00019AEC..), blocked
// beyond it (2) adds 10 to the dive attack's (0x20, 0x00019B28).
// @ fn_10_19A1C
void CDarkSamus::CollectLineAttacks(CStateManager& mgr, SAttackWeights& weights,
                                    uint* mask) const {
  const CVector3f point = GetRepulsionPoint(mgr, nullptr);
  if (!(point.GetX() == CVector3f::Zero().GetX() && point.GetY() == CVector3f::Zero().GetY() &&
        point.GetZ() == CVector3f::Zero().GetZ())) {
    SetAttackBitsExcept(mask, 0, 0x1, 0xFFFF, 0xFFFF, 0xFFFF);
    return;
  }
  switch (ClassifyLineToPlayer(mgr)) {
  case 1:
    weights.weights[GetAttackBitIndex(0x40)] += 10;  // 0x00019B04
    weights.weights[GetAttackBitIndex(0x100)] += 10; // 0x00019B1C
    break;
  case 2:
    weights.weights[GetAttackBitIndex(0x20)] += 10; // 0x00019B3C
    break;
  }
}

// fn_10_14648 (0x00014648, 0x2F4 bytes): no path area answers false (0x000146E0). The regions
// at the player (0.5 up, lbl_10_rodata_3E4, in the area's frame) are searched for the first
// obstructed one (the obstruction 2 count above 0, 0x000147C4) that holds the point (0x000147DC);
// no region at all makes "off the mesh" answer true (0x0001479C). From the player back to the
// actor in lbl_10_rodata_428 = 0.2f steps (t from 1, 0x00014918), every point 0.5 up that is not
// inside that region (the world-space point against the region, 0x000148A4) must be on the
// mesh (an off-mesh point answers the "no region" flag, 0x000148D8) within lbl_10_rodata_40C =
// 5.0f of the actor's own height (0x000148FC/0x00014908).
// @ fn_10_14648
bool CDarkSamus::CanWalkToPlayer(CStateManager& mgr) const {
  const CVector3f playerPos = mgr.GetPlayer(0)->GetTranslation();
  CPFRegion* inside = nullptr;
  bool noRegion = false;
  CPFArea* area =
      mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetPostConstructed()->mPathArea;
  if (area == nullptr) { // 0x000146DC
    return false;
  }
  const CTransform4f& areaXf = area->GetTransform();
  const CVector3f local =
      areaXf.TransposeRotate(playerPos + CVector3f(0.f, 0.f, 0.5f) - areaXf.GetTranslation());
  rstl::reserved_vector< CPFRegion*, 8 > regions;
  const uint creatureMask = mPathFindSearch.GetCreatureMask();
  area->FindRegions(regions, local, mPathFindSearch.GetRegionFlags(), creatureMask, true);
  if (regions.size() == 0) { // 0x00014794
    noRegion = true;
  } else {
    for (int i = 0; i < regions.size(); ++i) {
      CPFRegion* region = regions[i];
      if (region->GetObstructionCount(kPFO_Unknown2) > 0 &&
          region->IsPointInside(local) == true) { // 0x000147C4/0x000147DC
        inside = region;
        break;
      }
    }
  }
  float height = GetTranslation().GetZ();
  mPathFindSearch.GetHeightOfPointAboveMesh(GetTranslation(), height, 0.f); // 0x0001481C
  for (float t = 1.f; t > 0.f; t -= 0.2f) {
    const CVector3f point = (1.f - t) * GetTranslation() + t * playerPos + CVector3f(0.f, 0.f, 0.5f);
    if (inside == nullptr || inside->IsPointInside(point) != true) {
      float pointHeight = 0.f;
      if (mPathFindSearch.GetHeightOfPointAboveMesh(point, pointHeight, 0.f) !=
          CPathFindSearch::kR_Success) { // 0x000148D8
        return noRegion;
      }
      if (pointHeight > 5.f + height || pointHeight < height - 5.f) { // 0x000148FC/0x00014908
        return false;
      }
    }
  }
  return true;
}

// fn_10_EDE0 / fn_10_EB90 are defined with the dash below.
static pas::EStepDirection GetDashStepDirection(int slot);
static CVector3f GetDashDirection(uint slot, const CVector3f& f);

// fn_10_164C0 (0x000164C0, 0x140 bytes): is the line from the gun locator (0x8CC's translation,
// 0x8D8/0x8E8/0x8F8) plus offset to target clear? A static ray (RayStaticIntersection at
// 0x000165C8) over the whole line, the filter including lbl_10_data_C60 / C64 (0x3B kMT_Solid,
// 0x30 kMT_AIBlock; type 1 at 0x00016550 = include only); fn_10_2C8 is the module's copy of the
// CRayCastResult copy constructor, and the answer is the result not valid (0x000165DC).
// Arms: the ray cast found nothing (the line is clear) and it found something (the
// line is blocked); the coverage run showed both (the caller fn_10_16418 answered false
// four times, and it is the same test).
// @ fn_10_164C0
bool CDarkSamus::IsGunLineClear(CStateManager& mgr, const CVector3f& offset,
                                const CVector3f& target) const {
  const CVector3f start = mGunLctrXf.GetTranslation() + offset;
  const CVector3f delta = target - start;
  const float length = delta.Magnitude();
  const CRayCastResult result = mgr.RayStaticIntersection(
      start, delta.AsNormalized(), length,
      CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid, kMT_AIBlock)));
  return !result.IsValid();
}

// fn_10_16418 (0x00016418, 0xA8 bytes): the gun line (fn_10_164C0 at 0x000164A4) raised by
// lbl_10_rodata_40C = 5.0f in z to the player's aim position (the vtable call at 0x00016458,
// slot 0x54 = GetAimPosition, dt lbl_10_rodata_3FC = 0.0f).
// Arms: the aim line is clear and it is blocked; the coverage run showed both (20 true
// and 4 false).
// @ fn_10_16418
bool CDarkSamus::IsPlayerAimLineClear(CStateManager& mgr) const {
  const CVector3f aim = mgr.GetPlayer(0)->GetAimPosition(mgr, 0.f);
  return IsGunLineClear(mgr, CVector3f(0.f, 0.f, 5.f), aim);
}

// fn_10_16984 (0x00016984, 0x158 bytes): the ring ray's world-space offset, not normalized
// (jumptable_10_data_F94): 0 (0, 1, 0), 1 (1, 1, 0), 2 (1, 0, 0), 3 (1, -1, 0), 4 (0, -1, 0),
// 5 (-1, -1, 0), 6 (-1, 0, 0), 7 (-1, 1, 0); past 7 (`cmplwi r5, 0x7` at 0x00016984) the zero
// vector.
// @ fn_10_16984
static CVector3f GetRingRayOffset(int i) {
  switch (i) {
  case 0:
    return CVector3f(0.f, 1.f, 0.f);
  case 1:
    return CVector3f(1.f, 1.f, 0.f);
  case 2:
    return CVector3f(1.f, 0.f, 0.f);
  case 3:
    return CVector3f(1.f, -1.f, 0.f);
  case 4:
    return CVector3f(0.f, -1.f, 0.f);
  case 5:
    return CVector3f(-1.f, -1.f, 0.f);
  case 6:
    return CVector3f(-1.f, 0.f, 0.f);
  case 7:
    return CVector3f(-1.f, 1.f, 0.f);
  default:
    return CVector3f::Zero();
  }
}

// fn_10_16ADC (0x00016ADC, 0x8 bytes): the ring's ray count, 8.
// @ fn_10_16ADC
static int GetRingRayCount() { return 8; }

// fn_10_166AC (0x000166AC, 0x1B4 bytes): ring ray i: from the actor's position and to dest, both
// moved by radius along fn_10_16984(i); a line longer than lbl_10_rodata_3E4 = 0.5f (0x000167E4)
// is cast (RayStaticIntersection at 0x00016814) over reach times its length, the filter including
// lbl_10_data_C54 / C58 (0x3B kMT_Solid, 0x30 kMT_AIBlock) and excluding C5C (0x31 kMT_Platform).
// A valid hit (`cmplwi r0, 0x1` at 0x00016828) answers false, anything else true.
// @ fn_10_166AC
bool CDarkSamus::IsRingRayClear(CStateManager& mgr, const CVector3f& dest, int i, float reach,
                                float radius) const {
  const CVector3f offset = radius * GetRingRayOffset(i);
  const CVector3f start = GetTranslation() + offset;
  const CVector3f delta = (dest + offset) - start;
  const float length = delta.Magnitude();
  if (length > 0.5f) {
    const CRayCastResult result = mgr.RayStaticIntersection(
        start, delta.AsNormalized(), length * reach,
        CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid, kMT_AIBlock),
                                            CMaterialList(kMT_Platform)));
    if (result.IsValid() == true) {
      return false;
    }
  }
  return true;
}

// fn_10_16600 (0x00016600, 0xAC bytes): every ring ray (fn_10_166AC, i below fn_10_16ADC) clear;
// the first blocked one answers false (0x00016664).
// @ fn_10_16600
bool CDarkSamus::IsRingPathClear(CStateManager& mgr, const CVector3f& dest, float reach,
                                 float radius) const {
  for (int i = 0; i < GetRingRayCount(); ++i) {
    if (!IsRingRayClear(mgr, dest, i, reach, radius)) {
      return false;
    }
  }
  return true;
}

// fn_10_12B38 (0x00012B38, 0xF4 bytes): the dive's landing point: the player's position when the
// player is morphed (0x38C == 1, 0x00012BA8), else lbl_10_rodata_40C = 5.0f past the player along
// the flat line from the actor (z cleared at 0x00012B98, Normalize at 0x00012B9C).
// @ fn_10_12B38
CVector3f CDarkSamus::GetDiveTarget(CStateManager& mgr) const {
  const CPlayer* player = mgr.GetPlayer(0);
  const CVector3f& playerPos = player->GetTranslation();
  CVector3f dir(playerPos.GetX() - GetTranslation().GetX(),
                playerPos.GetY() - GetTranslation().GetY(), 0.f);
  dir.Normalize();
  if (static_cast< int >(player->GetMorphballTransitionState()) == 1) {
    return playerPos;
  }
  return playerPos + 5.f * dir;
}

// fn_10_141BC (0x000141BC, 0xC8 bytes): the player's distance (fn_10_17CEC) inside
// [minScale, maxScale] times the animation's distance (GetAnimationDistance at 0x00014224) times
// the scale's y; above the top (`bgt` at 0x00014238) or below the bottom (`bge` at 0x00014244)
// answers false.
// @ fn_10_141BC
bool CDarkSamus::IsPlayerInAnimReach(CStateManager& mgr, const CPASAnimParmData& parms,
                                     float minScale, float maxScale) const {
  const float dist = GetDistanceToPlayer(mgr);
  const float reach = GetAnimationDistance(parms) * GetModelData()->GetScale().GetY();
  // 0x14238 bgt to false (a NaN goes on), 0x14244 bge to true (a NaN too).
  if (dist > reach * maxScale || dist < reach * minScale) {
    return false;
  }
  return true;
}

// fn_10_13EE0 (0x00013EE0, 0xE0 bytes): the step animation's distance for a step direction
// (kAS_Step, FromEnum(dir), FromEnum(2) = kStep_BreakDodge; fn_10_13FFC at 0x00013F74 is the
// module's copy of the CPASAnimParmData constructor) times the scale's y.
// @ fn_10_13EE0
float CDarkSamus::GetBreakDodgeDistance(int dir) const {
  return GetAnimationDistance(CPASAnimParmData(pas::kAS_Step, CPASAnimParm::FromEnum(dir),
                                               CPASAnimParm::FromEnum(pas::kStep_BreakDodge))) *
         GetModelData()->GetScale().GetY();
}

// fn_10_13E2C (0x00013E2C, 0xB4 bytes): the dash slot's break-dodge step as an offset: the slot's
// direction (fn_10_EB90) from the transform's forward column (0x28/0x38/0x48, not flattened) times
// fn_10_13EE0 of the slot's step direction (fn_10_EDE0).
// @ fn_10_13E2C
CVector3f CDarkSamus::GetDodgeStepOffset(int slot) const {
  const float dist = GetBreakDodgeDistance(GetDashStepDirection(slot));
  return dist * GetDashDirection(slot, GetTransform().GetForward());
}

// fn_10_16B1C (0x00016B1C, 0x2C bytes): the point on the path (OnPath of 0x7C0 at 0x00016B2C
// answers kR_Success).
// Arms: the point is on the path and it is not. The coverage run (tools/cov/run.sh with
// tools/cov/specs/darksamus_helpers2_arms.py, the rooftop fight) saw both: 47 on the path
// and 21 off it.
// @ fn_10_16B1C
bool CDarkSamus::IsOnPath(const CVector3f& point) const {
  return mPathFindSearch.OnPath(point) == CPathFindSearch::kR_Success;
}

// fn_10_1444C (0x0001444C, 0x198 bytes): from point back to the actor in lbl_10_rodata_41C =
// 0.1f steps (t from lbl_10_rodata_3E8 = 1.0f while above 0.0f), every point on the line must be
// on the mesh (GetHeightOfPointAboveMesh at 0x00014544, padding 0.0f; a failure answers false)
// within lbl_10_rodata_40C = 5.0f of height (0x0001455C/0x00014564).
// @ fn_10_1444C
bool CDarkSamus::IsLineOnMesh(const CVector3f& point, float height) const {
  const float top = 5.f + height;
  const float bottom = height - 5.f;
  for (float t = 1.f; t > 0.f; t -= 0.1f) {
    const CVector3f along = (1.f - t) * GetTranslation() + t * point;
    float pointHeight = 0.f;
    if (mPathFindSearch.GetHeightOfPointAboveMesh(along, pointHeight, 0.f) !=
        CPathFindSearch::kR_Success) {
      return false;
    }
    if (pointHeight > top || pointHeight < bottom) {
      return false;
    }
  }
  return true;
}

// fn_10_145E4 (0x000145E4, 0x64 bytes): the actor's own height (GetHeightOfPointAboveMesh at
// 0x0001461C, from its z, padding 0.0f, the result unread) and fn_10_1444C to point.
// @ fn_10_145E4
bool CDarkSamus::CanWalkToPoint(const CVector3f& point) const {
  float height = GetTranslation().GetZ();
  mPathFindSearch.GetHeightOfPointAboveMesh(GetTranslation(), height, 0.f);
  return IsLineOnMesh(point, height);
}

// fn_10_19B64 (0x00019B64, 0x670 bytes): the collector for the attacks' own reach checks.
// 0x1 goes when the player is beyond the min attack range (0x3DC, `ble` at 0x00019BE4); 0x100 when
// the aim line is blocked (fn_10_16418); 0x200/0x400/0x40/0x4 and 0x8/0x10/0x1/0x80 when the actor
// cannot walk to the player (fn_10_14648 at 0x00019C58); 0x20 when the ring path to the dive target
// (fn_10_12B38) is blocked (fn_10_16600 with lbl_10_rodata_534 = 0.75f and lbl_10_rodata_4DC =
// 1.1f); 0x4 and 0x80 when the player is outside the melee animations' reach (fn_10_141BC with
// kAS_MeleeAttack (FromEnum(2) / FromEnum(0xA), FromEnum of the body's locomotion type 0x580):
// lbl_10_rodata_538 = 0.7f .. lbl_10_rodata_53C = 1.3f, lbl_10_rodata_540 = 0.9f ..
// lbl_10_rodata_438 = 3.0f). For 0x10 the dodge side nearer the player (fn_10_13E2C of slots 2 and
// 3, the squared distances' `bge` at 0x0001A014) sets the side byte 0xBAC (1 for slot 2 at
// 0x0001A020, 0 for slot 3 at 0x0001A0D0); 0x10 goes unless that end point and the point
// lbl_10_rodata_53C = 1.3f times the offset out are on the path (fn_10_16B1C) and walkable
// (fn_10_145E4).
// The CPASAnimParmData builds go through fn_10_13FFC (0x00019DB4, 0x00019E9C), the module's copy of the host
// header's ctor.
// Arms: forced calls (the callees' answers forced): every check failing, every check passing,
// walkable but out of anim reach, no bits, and for each dodge side nearer the player the four
// path/walk outcomes (ds45).
// @ fn_10_19B64
void CDarkSamus::CollectReachAttacks(CStateManager& mgr, SAttackWeights&, uint* mask) {
  const float dist = GetDistanceToPlayer(mgr);
  if ((*mask & 0x1) != 0 && dist > mMinAttackRange) { // `clrlwi.` at 0x00019BD4
    SetAttackBits(mask, 0, 0x1, 0xFFFF, 0xFFFF, 0xFFFF);
  }
  if ((*mask & 0x100) != 0 && !IsPlayerAimLineClear(mgr)) { // rlwinm 23,23 at 0x00019C10
    SetAttackBits(mask, 0, 0x100, 0xFFFF, 0xFFFF, 0xFFFF);
  }
  if (!CanWalkToPlayer(mgr)) {
    SetAttackBits(mask, 0, 0x200, 0x400, 0x40, 0x4);
    SetAttackBits(mask, 0, 0x8, 0x10, 0x1, 0x80);
  }
  if ((*mask & 0x20) != 0) { // rlwinm 26,26 at 0x00019CA8
    const CVector3f target = GetDiveTarget(mgr);
    if (!IsRingPathClear(mgr, target, 0.75f, 1.1f)) {
      SetAttackBits(mask, 0, 0x20, 0xFFFF, 0xFFFF, 0xFFFF);
    }
  }
  if ((*mask & 0x4) != 0) { // rlwinm 29,29 at 0x00019D2C
    const int loco = GetBodyController()->GetLocomotionType();
    const CPASAnimParmData parms(pas::kAS_MeleeAttack, CPASAnimParm::FromEnum(2),
                                 CPASAnimParm::FromEnum(loco));
    if (!IsPlayerInAnimReach(mgr, parms, 0.7f, 1.3f)) {
      SetAttackBits(mask, 0, 0x4, 0xFFFF, 0xFFFF, 0xFFFF);
    }
  }
  if ((*mask & 0x80) != 0) { // rlwinm 24,24 at 0x00019E14
    const int loco = GetBodyController()->GetLocomotionType();
    const CPASAnimParmData parms(pas::kAS_MeleeAttack, CPASAnimParm::FromEnum(0xA),
                                 CPASAnimParm::FromEnum(loco));
    if (!IsPlayerInAnimReach(mgr, parms, 0.9f, 3.f)) {
      SetAttackBits(mask, 0, 0x80, 0xFFFF, 0xFFFF, 0xFFFF);
    }
  }
  if ((*mask & 0x10) != 0) { // rlwinm 27,27 at 0x00019EFC
    const CVector3f playerPos = mgr.GetPlayer(0)->GetTranslation();
    const CVector3f offset2 = GetDodgeStepOffset(2);
    const CVector3f offset3 = GetDodgeStepOffset(3);
    const CVector3f end2 = GetTranslation() + offset2;
    const CVector3f end3 = GetTranslation() + offset3;
    const bool nearer2 = (end2 - playerPos).MagSquared() < (end3 - playerPos).MagSquared();
    const CVector3f& end = nearer2 ? end2 : end3;
    const CVector3f& offset = nearer2 ? offset2 : offset3;
    mScatterSide = nearer2 ? 1 : 0;
    if (!(IsOnPath(end) && IsOnPath(GetTranslation() + 1.3f * offset) && CanWalkToPoint(end))) {
      SetAttackBits(mask, 0, 0x10, 0xFFFF, 0xFFFF, 0xFFFF);
    }
  }
}

// fn_10_197D8 (0x000197D8, 0x140 bytes): the visible picker. An empty weight list and mask
// (0x000197E4), then the collectors in retail's order (fn_10_1A5BC, 1A554, 1A278, 1A440, 1A34C,
// 1A1D4, 19B64, 19A1C), the copy of the weights into 0xC54/0xC58 (0x000198C0..0x000198DC), the
// mask into 0xCB8 (0x000198F0) and the weighted pick (fn_10_19918) into 0xCBC (0x000198FC),
// which it answers.
// @ fn_10_197D8
int CDarkSamus::PickVisibleAttack(CStateManager& mgr) {
  SAttackWeights weights;
  weights.count = 0;
  uint mask = 0;
  CollectStageWeights(mgr, weights, &mask);
  CollectFadeAttacks(mgr, weights, &mask);
  CollectMorphballAttacks(mgr, weights, &mask);
  CollectRangeAttacks(mgr, weights, &mask);
  CollectHistoryPenalties(mgr, weights, &mask);
  CollectFacingAttacks(mgr, weights, &mask);
  CollectReachAttacks(mgr, weights, &mask);
  CollectLineAttacks(mgr, weights, &mask);
  mAttackWeights = weights;
  mAttackMask = mask;
  mWeightedPick = PickWeightedAttack(mgr, weights, mask);
  return mWeightedPick;
}

// fn_10_1969C (0x0001969C, 0x13C bytes): the faded picker - fn_10_1A5BC, 1A554, 1A440, 1A34C and
// 1A1D4, then a player the actor cannot walk to (fn_10_14648 false) takes away the boost ball
// and the sweep beam (fn_10_1A980 with 0x20000, 0x10000); the copy and the pick as in
// fn_10_197D8.
// @ fn_10_1969C
int CDarkSamus::PickFadedAttack(CStateManager& mgr) {
  SAttackWeights weights;
  weights.count = 0;
  uint mask = 0;
  CollectStageWeights(mgr, weights, &mask);
  CollectFadeAttacks(mgr, weights, &mask);
  CollectRangeAttacks(mgr, weights, &mask);
  CollectHistoryPenalties(mgr, weights, &mask);
  CollectFacingAttacks(mgr, weights, &mask);
  if (CanWalkToPlayer(mgr) == false) {
    SetAttackBits(&mask, 0, 0x20000, 0x10000, 0xFFFF, 0xFFFF);
  }
  mAttackWeights = weights;
  mAttackMask = mask;
  mWeightedPick = PickWeightedAttack(mgr, weights, mask);
  return mWeightedPick;
}

// fn_10_1954C (0x0001954C, 0x150 bytes): nothing while an attack runs (0x980 not 0xFFFF, the
// whole-word compare at 0x00019574), within lbl_10_rodata_530 = 0.25f of the last pick
// (0x00019594) or while the fight script holds the actor (0xD58 == 1, 0x000195A0). Out of close
// range (fn_10_1AA6C nonzero, 0x000195B0) the pick waits for 0x990 (0x000195C0); close by, it may
// come lbl_10_rodata_424 = 0.6f of the stage's shortest action gap early (the stage's +0x4,
// faded its +0xC, 0x00019604/0x00019618; no stage lbl_10_rodata_3E8 = 1.0f; the fnmsubs at
// 0x0001962C). A pick counts (0x988, 0x00019640), runs the faded picker when 0xDEC is set
// (0x00019648), stores the attack in 0x984 and the time in 0x98C (0x00019678).
// @ fn_10_1954C
void CDarkSamus::UpdateAttackPick(CStateManager& mgr) {
  if (mCurrentAttack != 0xFFFF) {
    return;
  }
  if (0.25f + mLastPickTime > mElapsedTime) {
    return;
  }
  if (mPissedOff == 1) {
    return;
  }
  if (GetPlayerRangeBand(mgr) != 0) {
    if (mNextActionTime > mElapsedTime) {
      return;
    }
  } else {
    float gap = 1.f;
    if (GetBattleStage(mgr) != nullptr) {
      if (mFadeOut == 1) {
        gap = GetBattleStage(mgr)->GetStageInfo().minTimeBetweenActionsWhenInvisible;
      } else {
        gap = GetBattleStage(mgr)->GetStageInfo().minTimeBetweenActions;
      }
    }
    if (-(0.6f * gap - mNextActionTime) > mElapsedTime) {
      return;
    }
  }
  mPickCount++;
  if (mFadeOut == 0) {
    mPickedAttack = PickVisibleAttack(mgr);
  } else {
    mPickedAttack = PickFadedAttack(mgr);
  }
  mLastPickTime = mElapsedTime;
}

// fn_10_1B5E4 (0x0001B5E4, 0x74 bytes): mAttackHistory's erase. A position outside
// [begin, end) (the cmplw pair at 0x0001B5EC/0x0001B604) answers end; otherwise the later ids move
// down one (0x0001B614..0x0001B630), the count drops (0x0001B63C) and the position is answered.
// The loop bound (0x0001B620..0x0001B62C) is the vector base + count * 4; the elements start at
// base + 4, so that is &data[count - 1], and the last shift reads the last element, not one past it.
uint* CDarkSamus::EraseFinishedAttack(uint* pos) {
  if (pos < mAttackHistory || pos >= mAttackHistory + mAttackHistoryCount) {
    return mAttackHistory + mAttackHistoryCount;
  }
  for (uint* it = pos; it + 1 < mAttackHistory + mAttackHistoryCount; ++it) {
    it[0] = it[1];
  }
  mAttackHistoryCount--;
  return pos;
}

// fn_10_1B478 (0x0001B478, 0x16C bytes): the attack bookkeeping. Activate (the `cmpwi r6, 0x0`
// arm, 0x0001B4C8) makes `attack` the current attack (0x980, 0x0001B4CC), clears the pick (0x984
// = 0xFFFF, 0x0001B4E0) and draws the next pick's delay into 0x990 (0x0001B558) between the
// stage's minimum and maximum action gap (+0x4/+0x8, 0x0001B534/0x0001B544; +0xC/+0x10 when
// faded, 0xDEC == 1 at 0x0001B500; without a stage both are lbl_10_rodata_3FC = 0.0f) with the
// state manager's random (the CRandom16 at 0x16E4, 0x0001B54C). Deactivate (`cmpwi r6, 0x3` /
// the 0x0001B560 arm) trims the oldest finished attack when there are three (0x0001B564,
// fn_10_1B5E4 at 0x0001B574), pushes the current one (0x0001B590), counts it (0x0001B5A4), clears
// 0x980 to 0xFFFF (0x0001B5A8) and turns the delay into a time (0x990 += 0x8B0, 0x0001B5B8).
// Update does nothing.
// @ fn_10_1B478
// @ fn_10_1B5E4
void CDarkSamus::UpdateCurrentAttack(uint attack, CStateManager& mgr, EStateMsg msg) {
  switch (msg) {
  case kStateMsg_Activate: {
    mCurrentAttack = attack;
    mPickedAttack = 0xFFFF;
    float minGap = 0.f;
    float maxGap = minGap;
    if (GetBattleStage(mgr) != nullptr) {
      if (mFadeOut == 1) {
        minGap = GetBattleStage(mgr)->GetStageInfo().minTimeBetweenActionsWhenInvisible;
        maxGap = GetBattleStage(mgr)->GetStageInfo().maxTimeBetweenActionsWhenInvisible;
      } else {
        minGap = GetBattleStage(mgr)->GetStageInfo().minTimeBetweenActions;
        maxGap = GetBattleStage(mgr)->GetStageInfo().maxTimeBetweenActions;
      }
    }
    mNextActionTime = (maxGap - minGap) * mgr.Random()->Float() + minGap;
    break;
  }
  case kStateMsg_Deactivate:
    if (static_cast< int >(mAttackHistoryCount) >= 3) {
      EraseFinishedAttack(mAttackHistory);
    }
    // 0x0001B584: the placement-new null test of the push; the slot is never null.
    mAttackHistory[mAttackHistoryCount] = mCurrentAttack;
    mAttackHistoryCount++;
    mCurrentAttack = 0xFFFF;
    mNextActionTime += mElapsedTime;
    break;
  default:
    break;
  }
}

// fn_10_10520 (0x00010520, 0xD4 bytes): on Activate (0x00010528) the base's orbit position
// (CPatterned::GetOrbitPosition, called directly at 0x00010550) into 0xD28 and its aim position
// at lbl_10_rodata_3FC = 0.0f (CPatterned::GetAimPosition, 0x00010580) into 0xD34; on Deactivate
// (0x000105A0) both become the zero vector.
// @ fn_10_10520
void CDarkSamus::UpdateAttackAimPoints(CStateManager& mgr, EStateMsg msg) {
  if (msg == kStateMsg_Activate) {
    mAttackOrbitPos = CPatterned::GetOrbitPosition(mgr);
    mAttackAimPos = CPatterned::GetAimPosition(mgr, 0.f);
  } else if (msg == kStateMsg_Deactivate) {
    mAttackOrbitPos = CVector3f::Zero();
    mAttackAimPos = CVector3f::Zero();
  }
}

// fn_10_1AEC0 (0x0001AEC0, 0xF4 bytes): the motion volume at lbl_10_rodata_3FC = 0.0f
// (0x0001AEEC) builds an area collision cache (0x0001AF28, filled at 0x0001AF34) and a collider
// list (0x0001AF50), and CGameCollision::CollisionFailsafe runs with the actor's collision
// primitive (vtable 0x7C, 0x0001AF64), dt 0.0f, 0 ticks and 0.0f (0x0001AF8C); the cache's
// destructor (fn_10_1AFB4, 0x0001AF98) is the host's at the end of the scope.
// @ fn_10_1AEC0
void CDarkSamus::RunCollisionFailsafe(CStateManager& mgr) {
  const CAABox motionVolume = GetMotionVolume(0.f);
  CAreaCollisionCache cache(motionVolume);
  CGameCollision::BuildAreaCollisionCache(mgr, cache);
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildColliderList(nearList, *this, motionVolume);
  CGameCollision::CollisionFailsafe(mgr, cache, *this, *GetCollisionPrimitive(), nearList, 0.f, 0,
                                    0.f);
}

// fn_10_16B8C (0x00016B8C, 0x150 bytes): nothing when dt <= lbl_10_rodata_3FC = 0.0f (the cror at
// 0x00016BCC). Otherwise the flattened facing axis (the transform's second column with z = 0.0f,
// 0x00016BD4..0x00016BF0) and the flattened line to the point (0x00016C0C..0x00016C4C), each
// normalized when it can be (0x00016BF4/0x00016C50), give a rotation arc clamped to dt * rate
// (0x00016C68, 0x00016C80) that the actor turns by in one frame (0x00016CB0).
// @ fn_10_16B8C
void CDarkSamus::TurnTowardPoint(float dt, const CVector3f& point, float rate) {
  if (dt <= 0.f) {
    return;
  }
  const CVector3f forward = GetTransform().GetForward();
  CVector3f flatForward(forward.GetX(), forward.GetY(), 0.f);
  if (flatForward.CanBeNormalized() == true) {
    flatForward.Normalize();
  }
  const CVector3f pos = GetTranslation();
  CVector3f toPoint(point.GetX() - pos.GetX(), point.GetY() - pos.GetY(), 0.f);
  if (toPoint.CanBeNormalized() == true) {
    toPoint.Normalize();
  }
  const CQuaternion arc =
      CQuaternion::ShortestRotationArcClamped(flatForward, toPoint, CRelAngle::FromRadians(dt * rate));
  RotateInOneFrameOR(arc, dt);
}

// fn_10_16B64 (0x00016B64, 0x28 bytes): fn_10_16B8C toward the player's position (the state
// manager's player at 0x14FC, its position at +0x54, 0x00016B70).
// @ fn_10_16B64
void CDarkSamus::TurnTowardPlayer(CStateManager& mgr, float dt, float rate) {
  TurnTowardPoint(dt, mgr.GetPlayer(0)->GetTranslation(), rate);
}

// fn_10_11814 (0x00011814, 0x160 bytes): the scatter shot's start, from DoUserAnimEvent's
// kUE_EventStart arm (0x0001C25C with side 0 for the ScatterShot attack 8, 0x0001C268 with the
// byte 0xBAC for MovingScatterShot's 0x10). The start time 0xBA4 is the elapsed time 0x8B0
// (0x00011858), the firing byte 0xBAD is set (0x00011868) and the fire timer 0xBA8 starts at
// lbl_10_rodata_474 = 0.05f (0x00011874). The aim 0xB88 is the player's position (the state
// manager's player at 0x14FC, +0x54, 0x00011878..0x00011890); the flattened direction to it
// (z = lbl_10_rodata_3FC = 0.0f, 0x000118B8) is normalized (0x000118BC) and the line's ends sit
// lbl_10_rodata_478 = 6.5f to either side of the aim (the perpendicular (y, -x), 0x000118E0..
// 0x00011908; z = aim z + 0.0f). Side 1 (`cmplwi r0, 0x1` at 0x000118EC) starts the line on the
// (y, -x) side, any other side on the (-y, x) side.
// @ fn_10_11814
void CDarkSamus::StartScatterShotFire(CStateManager& mgr, uchar side) {
  mScatterStartTime = mElapsedTime;
  mScatterFiring = 1;
  mScatterTimer = 0.05f;
  const CVector3f player = mgr.GetPlayer(0)->GetTranslation();
  mScatterAim = player;
  const CVector3f pos = GetTranslation();
  CVector3f dir(player.GetX() - pos.GetX(), player.GetY() - pos.GetY(), 0.f);
  dir.Normalize();
  const CVector3f right(player.GetX() + 6.5f * dir.GetY(), player.GetY() + 6.5f * -dir.GetX(),
                        player.GetZ() + 0.f);
  const CVector3f left(player.GetX() + 6.5f * -dir.GetY(), player.GetY() + 6.5f * dir.GetX(),
                       player.GetZ() + 0.f);
  if (side == 1) {
    mScatterLineStart = right;
    mScatterLineEnd = left;
  } else {
    mScatterLineStart = left;
    mScatterLineEnd = right;
  }
}

// fn_10_13230 (0x00013230, 0x1A0 bytes): the point the next shot aims at. The sweep fraction is
// the time since the start (0x8B0 - 0xBA4) over lbl_10_rodata_4A8 = 0.47f for the ScatterShot
// attack 8 (`cmpwi r0, 0x8` at 0x00013290), lbl_10_rodata_4AC = 0.67f for any other, clamped to
// lbl_10_rodata_3E8 = 1.0f (0x000132BC). The point is the line start plus the fraction of the line
// (0xB94 - 0xB7C), jittered by the state manager's random (0x16E4): x and y by
// lbl_10_rodata_3E0 = 2.0f * (lbl_10_rodata_408 = 4.0f * rand) - 4.0f (0x000132F0..0x0001334C), z
// by lbl_10_rodata_4B4 = 9.0f * rand + lbl_10_rodata_4B0 = -1.5f (0x00013350..0x00013378).
// @ fn_10_13230
CVector3f CDarkSamus::GetScatterShotAimPoint(CStateManager& mgr) const {
  float t = (mElapsedTime - mScatterStartTime) / (mCurrentAttack == 8 ? 0.47f : 0.67f);
  if (t > 1.f) {
    t = 1.f;
  }
  const float sx = mScatterLineStart.GetX();
  const float sy = mScatterLineStart.GetY();
  const float sz = mScatterLineStart.GetZ();
  const float dx = mScatterLineEnd.GetX() - sx;
  const float dy = mScatterLineEnd.GetY() - sy;
  const float dz = mScatterLineEnd.GetZ() - sz;
  const float x = 2.f * (4.f * mgr.Random()->Float()) + ((sx + t * dx) - 4.f);
  const float y = 2.f * (4.f * mgr.Random()->Float()) + ((sy + t * dy) - 4.f);
  const float z = 9.f * mgr.Random()->Float() + (-1.5f + (sz + t * dz));
  return CVector3f(x, y, z);
}

// fn_10_12F60 (0x00012F60, 0x2D0 bytes): one scatter shot. The shot's transform looks from the gun
// locator transform 0x8CC's translation at the aim point (fn_10_13230, 0x00012F98) with the
// locator's up (LookAt at 0x00013000). With a live projectile in 0xBE4 (0x0001301C;
// TCastToPtr<CScattershotProjectile> at 0x00013038, else 0xBE4 = kInvalidUniqueId) the shot is
// added to it (fn_10_C98 at 0x0001308C, from the locator's translation along the shot's forward).
// Otherwise the second projectile's chance 0xBDC - divided by lbl_10_rodata_3F0 = 100.0f when above
// lbl_10_rodata_3E8 = 1.0f (0x000130A0) - against the state manager's random decides 0xBAE
// (`fcmpo; mfcr; srwi 31` = rand < chance, 0x000130CC), which ProjectileInfo (vtable 0xDC) reads;
// when its weapon description is loaded (TCachedToken::TryCache, 0x000130E4..0x00013124) a new
// uid goes to 0xBE4 (0x00013150) and the 0x27B0-byte CScattershotProjectile (fn_10_2AD0 at
// 0x000131DC: the info's token, kWT_AI = `li r5, 0xb`, the transform, the info's damage at +0xC,
// the uid, the area 0x4, the owner 0x8 and lbl_10_rodata_488 = 45.0f) is added (0x000131F4), or
// 0xBE4 is cleared when the allocation fails (0x00013204).
// CScattershotProjectile is src/MetroidPrime/Enemies/PortCScattershotProjectile.cpp.
// Arms: the fight, plus forced calls (attack 8): a uid that is not a scatter projectile, a
// chance above 1, the description not cached (ds45).
// @ fn_10_12F60
void CDarkSamus::FireScatterShotProjectile(CStateManager& mgr) {
  const CVector3f aim = GetScatterShotAimPoint(mgr);
  const CTransform4f xf = CTransform4f::LookAt(mGunLctrXf.GetTranslation(), aim, mGunLctrXf.GetUp());
  if (mScatterProjectileUid != kInvalidUniqueId) {
    if (CScattershotProjectile* scatter =
            TCastToPtr< CScattershotProjectile >(mgr.ObjectById(mScatterProjectileUid))) {
      scatter->AddShot(mGunLctrXf.GetTranslation(), xf.GetColumn(kDY)); // 0x0001308C
      return;
    }
    mScatterProjectileUid = kInvalidUniqueId;
  }
  float chance = mScatterChance;
  if (chance > 1.f) {
    chance = chance / 100.f;
  }
  mScatterUseSecondInfo = mgr.Random()->Float() < chance;
  CProjectileInfo* info = ProjectileInfo();
  if (!info->Token().TryCache()) {
    return;
  }
  mScatterProjectileUid = mgr.AllocateUniqueId();
  CScattershotProjectile* projectile = rs_new CScattershotProjectile(
      ProjectileInfo()->Token(), kWT_AI, xf, ProjectileInfo()->GetDamage(), mScatterProjectileUid,
      GetCurrentAreaId(), GetUniqueId(), 45.f); // lbl_10_rodata_488
  if (projectile != nullptr) {
    mgr.AddObject(projectile);
    return;
  }
  mScatterProjectileUid = kInvalidUniqueId;
}

// fn_10_11974 (0x00011974, 0x164 bytes): only while the firing byte 0xBAD is set (0x0001199C).
// The aim point 0xB88 moves toward the player by lbl_10_rodata_47C = 6.0f * dt of the way
// (0x000119BC..0x00011A00) and the line's ends 0xB7C/0xB94 move with it (0x00011A04..0x00011A48);
// the timer 0xBA8 counts dt (0x00011A78). With a rate 0xBE0 above 0.0f (0x00011A84) one projectile
// (fn_10_12F60, 0x00011AA8) fires per lbl_10_rodata_3E8 = 1.0f / rate of the timer (0x00011A8C,
// the loop at 0x00011AB4).
// fn_10_11974 (0x00011974, 0x164 bytes): the scatter shot's fire timer (the flag at
// 0xBAD, the rate at 0xBE0). Arms: not firing (0x000119A4) - the shot is off, the rate at
// zero - dead on shipped data (the rate comes from the loader's constructor argument
// (mScatterShotProjectilesPerSecond) and is above zero in every hit) - and firing.
// // The arms: the coverage run (tools/cov/specs/darksamus_helpers2.py, the rooftop fight: Sanctuary 10_cliff_rooftop, boss u04f7, 6400 updates) showed both.
// @ fn_10_11974
void CDarkSamus::UpdateScatterShotFire(CStateManager& mgr, float dt) {
  if (mScatterFiring == 0) {
    return;
  }
  const float t = 6.f * dt;
  const float keep = 1.f - t;
  const CVector3f& player = mgr.GetPlayer(0)->GetTranslation();
  const float dx = (mScatterAim.GetX() * keep + player.GetX() * t) - mScatterAim.GetX();
  const float dy = (mScatterAim.GetY() * keep + player.GetY() * t) - mScatterAim.GetY();
  const float dz = (mScatterAim.GetZ() * keep + player.GetZ() * t) - mScatterAim.GetZ();
  const CVector3f delta(dx, dy, dz);
  mScatterLineStart += delta;
  mScatterLineEnd += delta;
  mScatterAim += delta;
  mScatterTimer += dt;
  if (mScatterRate <= 0.f) {
    return;
  }
  const float interval = 1.f / mScatterRate;
  while (mScatterTimer > interval) {
    mScatterTimer -= interval;
    FireScatterShotProjectile(mgr);
  }
}

// fn_10_15348 (0x00015348, 0xA4 bytes): the dash record, in the retail store order.
// @ fn_10_15348
CDarkSamusDash::CDarkSamusDash() {
  mDestination = CVector3f::Zero();
  mStartPosition = CVector3f::Zero();
  mStepsLeft = 0;
  mDoubleDash = false;
  mStartPosition = CVector3f::Zero();
  mDestination = mStartPosition;
  mLastSlot = -1;
  mSlot = -1;
  mWeight = 0.f;
  mBlocked = false;
}

// fn_10_EDE0 (0x0000EDE0, 0x14 bytes): the dash slot (0..7) to its step direction, the table
// lbl_10_rodata_37C = {0, 1, 2, 3, 6, 7, 8, 9} (no bounds test). Echoes' step directions go past
// the host enum's kSD_Down (5): 6..9 are the four diagonals, cast through as retail passes them.
// @ fn_10_EDE0
static pas::EStepDirection GetDashStepDirection(int slot) {
  static const int skDirections[8] = {0, 1, 2, 3, 6, 7, 8, 9};
  return static_cast< pas::EStepDirection >(skDirections[slot]);
}

// fn_10_EB90 (0x0000EB90, 0x228 bytes): the dash slot's direction from the flat facing axis f, the
// jump table jumptable_10_data_F74 (`cmplwi r4, 0x7` at 0x0000EB98 to the zero vector):
// 0 f, 1 -f (lbl_10_rodata_3EC = -1.0f), 2 (-f.y, f.x, 0), 3 (f.y, -f.x, 0), and the diagonals
// normalized (AsNormalized at 0x0000EC70/0x0000ECC4/0x0000ED2C/0x0000ED80): 4 the sum of f and
// slot 2's, 6 slot 2's minus f, 5 the sum of f and slot 3's, 7 slot 3's minus f (the `cmpwi` at
// 0x0000EC14/0x0000EC3C and 0x0000ECD0/0x0000ECF8 split the shared arms).
// @ fn_10_EB90
static CVector3f GetDashDirection(uint slot, const CVector3f& f) {
  switch (slot) {
  case 0:
    return f;
  case 1:
    return -1.f * f;
  case 2:
  case 4:
  case 6: {
    const float nfy = -f.GetY();
    if (slot == 2) {
      return CVector3f(nfy, f.GetX(), 0.f);
    }
    if (slot == 4) {
      return CVector3f(nfy + f.GetX(), f.GetX() + f.GetY(), 0.f + f.GetZ()).AsNormalized();
    }
    const CVector3f neg = -1.f * f;
    return CVector3f(nfy + neg.GetX(), f.GetX() + neg.GetY(), 0.f + neg.GetZ()).AsNormalized();
  }
  case 3:
  case 5:
  case 7: {
    const float nfx = -f.GetX();
    if (slot == 3) {
      return CVector3f(f.GetY(), nfx, 0.f);
    }
    if (slot == 5) {
      return CVector3f(f.GetY() + f.GetX(), nfx + f.GetY(), 0.f + f.GetZ()).AsNormalized();
    }
    const CVector3f neg = -1.f * f;
    return CVector3f(f.GetY() + neg.GetX(), nfx + neg.GetY(), 0.f + neg.GetZ()).AsNormalized();
  }
  default:
    return CVector3f::Zero();
  }
}

// fn_10_12C2C (0x00012C2C, 0xB4 bytes): the step command (fn_10_21DF8 at 0x00012D88,
// pas::kAS_Step = `li r5, 0x3` at 0x00012D6C) toward the dash slot's direction (fn_10_EDE0 of
// 0x9E0 at 0x00012D28, stored at the command's 0x10), a normal step (`li r0, 0x0` stored at 0x14).
// @ fn_10_12C2C
void CDarkSamus::DoDashStepCmd(EStateMsg msg) {
  DoStepCmd(msg, pas::kAS_Step, CBCStepCmd(GetDashStepDirection(mDash.mSlot), pas::kStep_Normal));
}

// fn_10_12CE0 (0x00012CE0, 0xF0 bytes): the attack bookkeeping with id 0x8000 (`lis r4, 0x1;
// addi r4, r4, -0x8000`, fn_10_1B478 at 0x00012D14), the collision failsafe (fn_10_1AEC0 at
// 0x00012D20), the step command of fn_10_12C2C (inlined, 0x00012D24..0x00012D88), and on Activate
// (`cmpwi r31, 0x0` at 0x00012D98) the slot 0x9E0 is copied to 0x9FC (0x00012DB0).
// @ fn_10_12CE0
void CDarkSamus::UpdateDashStep(CStateManager& mgr, EStateMsg msg) {
  UpdateCurrentAttack(0x8000, mgr, msg);
  RunCollisionFailsafe(mgr);
  DoDashStepCmd(msg);
  if (msg == kStateMsg_Activate) {
    mDash.mLastSlot = mDash.mSlot;
  }
}

// fn_10_14A00 (0x00014A00, 0x250 bytes): the blast when the dash is stuck. The damage
// (0x00014A3C..0x00014AF0): kWT_UnknownSource (`li r0, 0x13` into the mode's high half), not
// charged / comboed (0x8000 / 0x4000 cleared at 0x00014A78/0x00014A94), instant kill (0x2000 set
// at 0x00014AB0); damage and radius damage lbl_10_rodata_3F0 = 100.0f, radius
// lbl_10_rodata_4C8 = 15.0f + 0x968 (the int-to-float at 0x00014A68, fadds at 0x00014A9C),
// knockback lbl_10_rodata_3FC = 0.0f, no immunity (0x80 set at 0x00014AA8), no radius-damage flag
// (0x40 cleared at 0x00014ABC). The near list (0x00014BC0) is the box of half-size radius around
// the actor (0x00014B60..0x00014BA0) filtered to include lbl_10_data_C94 / C98 (0x3B kMT_Solid,
// 0x31 kMT_Platform) and exclude lbl_10_data_C9C (0x20 kMT_Player), type 3; every object found
// (ObjectById, the null test at 0x00014BE4) takes the radius damage from the actor's position
// with the actor's uid (ApplyRadiusDamage at 0x00014C0C). Last the collision failsafe
// (0x00014C30).
// Arms: the fight and the stuck-twice Dash call (ds43); the null arm (`mr. r6, r3` at 0x00014BE4)
// is dead: the uids come from BuildNearList in the same frame and deletions are deferred.
// @ fn_10_14A00
void CDarkSamus::ApplyDashStuckBlast(CStateManager& mgr) {
  const float radius = 15.f + static_cast< float >(mDashStuckCount);
  const CDamageInfo info(CWeaponMode(kWT_UnknownSource, false, false, true), 100.f, radius, 0.f,
                         true, false);
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_Platform), CMaterialList(kMT_Player));
  const CVector3f& pos = GetTranslation();
  const CVector3f max(pos.GetX() + radius, pos.GetY() + radius, pos.GetZ() + radius);
  const CVector3f min(pos.GetX() + -radius, pos.GetY() + -radius, pos.GetZ() + -radius);
  const CAABox box(min, max);
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, box, filter, this);
  for (rstl::reserved_vector< TUniqueId, kMaxObjects >::iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    // The near list holds actors only; retail passes the entity on as one.
    CActor* actor = static_cast< CActor* >(mgr.ObjectById(*it));
    if (actor != nullptr) { // `mr. r6, r3` at 0x00014BE4/0x00014BE8
      mgr.ApplyRadiusDamage(*this, GetTranslation(), *actor, GetUniqueId(), info);
    }
  }
  RunCollisionFailsafe(mgr);
}

// fn_10_16048 (0x00016048, 0x3D0 bytes): the flat line from the actor to dest (z of the delta
// cleared at 0x000160EC; not normalizable answers lbl_10_rodata_3FC = 0.0f at 0x00016128) is cast
// three times (the loop `cmpwi r27, 0x3` at 0x000163B0) from the actor's position raised by
// lbl_10_rodata_3E0 = 2.0f, offset lbl_10_rodata_4F0 = 1.5f along the zero vector (k 0), the right
// normal (dir.y, -dir.x, 0) (k 1, 0x00016210) and the left one (-dir.y, dir.x, 0) (k 2,
// 0x0001622C), over the delta's length. The static test (RayStaticLineOfSightTest on the actor's
// area at 0x000162A4) includes lbl_10_data_C68 (0x3B kMT_Solid) and excludes C6C / C70 (0x2F
// kMT_CollisionActor, 0x1C kMT_AIPassthrough); the dynamic one (RayDynamicLineOfSightTest at
// 0x00016380, the near list, the actor ignored) includes C74 / C78 (kMT_Solid, 0x30 kMT_AIBlock)
// and excludes C7C / C80 (0x20 kMT_Player, kMT_AIPassthrough). A blocked centre ray answers 0.0f
// (0x000162B0/0x0001638C); a blocked side ray takes lbl_10_rodata_4F4 = 0.49f off the answer,
// which starts at lbl_10_rodata_3E8 = 1.0f.
// @ fn_10_16048
float CDarkSamus::GetDashPathClearance(
    CStateManager& mgr, const CVector3f& dest,
    const rstl::reserved_vector< TUniqueId, kMaxObjects >& nearList) const {
  const CVector3f& pos = GetTranslation();
  const CVector3f delta(dest.GetX() - pos.GetX(), dest.GetY() - pos.GetY(), 0.f);
  const float length = delta.Magnitude();
  if (!delta.CanBeNormalized()) {
    return 0.f;
  }
  const CVector3f dir = delta.AsNormalized();
  const CMaterialFilter staticFilter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid), CMaterialList(kMT_CollisionActor, kMT_AIPassthrough));
  const CMaterialFilter dynamicFilter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_AIBlock), CMaterialList(kMT_Player, kMT_AIPassthrough));
  float clearance = 1.f;
  for (int k = 0; k < 3; ++k) {
    CVector3f offset = CVector3f::Zero();
    if (k == 1) {
      offset = CVector3f(dir.GetY(), -dir.GetX(), 0.f);
    } else if (k == 2) {
      offset = CVector3f(-dir.GetY(), dir.GetX(), 0.f);
    }
    const CVector3f start(pos.GetX() + 0.f + 1.5f * offset.GetX(),
                          pos.GetY() + 0.f + 1.5f * offset.GetY(),
                          pos.GetZ() + 2.f + 1.5f * offset.GetZ());
    const CGameArea& area = mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId());
    if (!CGameCollision::RayStaticLineOfSightTest(area, start, dir, length, staticFilter)) {
      if (k == 0) {
        return 0.f;
      }
      clearance -= 0.49f;
      continue;
    }
    if (!CGameCollision::RayDynamicLineOfSightTest(mgr, start, dir, length, dynamicFilter,
                                                   nearList, this)) {
      if (k == 0) {
        return 0.f;
      }
      clearance -= 0.49f;
    }
  }
  return clearance;
}

// fn_10_154FC (0x000154FC, 0xB4C bytes): weighs the eight dash slots and picks one.
//
// The facing axis (the transform's forward column, z cleared at 0x000155FC, normalized or
// (lbl_10_rodata_3E8 = 1.0f, 0, 0) at 0x0001561C) and the squared distance to the player (mgr's
// 0x14FC). While steps are left (0xA04 > 0, 0x0001564C) the last slot and the slot opposite it
// (lbl_10_rodata_340 = {1, 0, 3, 2, 7, 6, 5, 4}) get no weight (0x00015718..), otherwise the last
// slot's weight is scaled by lbl_10_rodata_4D8 = 0.01f (0x000157D8..). 0x9E0 becomes 1
// (0x0001586C). The height of the actor above the path mesh (GetHeightOfPointAboveMesh at
// 0x00015888, padding 1.0f) and the near list (0x00015A58) around the forward step's reach (the
// animation distance of kAS_Step (FromEnum(0), FromEnum(0)) times the scale's y, times
// lbl_10_rodata_4DC = 1.1f, in x/y; lbl_10_rodata_4E0 = -2.0f .. lbl_10_rodata_47C = 6.0f in z),
// including lbl_10_data_C84 / C88 (kMT_Solid, kMT_AIBlock) and excluding C8C / C90 (kMT_Player,
// kMT_AIPassthrough). Per slot (0x00015A78, 8 slots at 0x00015E00): the slot's end point is the
// actor's position plus the slot's direction (fn_10_EB90) times its step's animation distance
// (kAS_Step (FromEnum(fn_10_EDE0(slot)), FromEnum(0))) times the scale's y. A path less than clear
// (fn_10_16048 below 1.0f, 0x00015B94) scales the weight by lbl_10_rodata_41C = 0.1f * clearance
// when the height query failed (0x00015B9C), drops the slot when the actor has stood still for
// less than lbl_10_rodata_42C = 1.2f (0x00015BCC), else scales it by lbl_10_rodata_4E4 = 0.02f *
// clearance. With steps left an end point nearer the player than the actor drops the slot
// (0x00015C58), else the weight is scaled by the squared distance gained (0x00015C70); without,
// the closest the flat line to the end point passes the player (t from lbl_10_rodata_474 = 0.05f
// by 0.05f while <= 1.0f, the start at lbl_10_rodata_4E8 = 8.01f) under lbl_10_rodata_414 = 8.0f
// scales it by lbl_10_rodata_3E4 = 0.5f * (closest * lbl_10_rodata_4EC = 0.125f). Then an end
// point nearer the player halves it (0x00015D50), and with a target (0x00015D6C) a distance under
// the base's max attack range (0x3E0) scales it by 1 - distance / range, else by 0.01f. Last the
// largest weight (from 0.0f) is looked for from a random slot on (Next, the signed modulo 8 at
// 0x00015EB0..); the slot (1 when none matches), its end point, the actor's position and its weight
// go into the dash record (0x00015F94..0x00015FD4), and the weight is the answer.
// Arms: the fight (blocked slots dropped or scaled, steps left, a target), plus forced calls
// (tools/cov/specs/darksamus_ds46_pickdash.{py,gdb}): the facing flat, the last slot with and
// without steps, the actor next to the player, a far target, the clearance forced under 1 with the
// height query failed / the actor still / moving, weights all negative (no slot matches: 1) (ds46).
// @ fn_10_154FC
float CDarkSamus::PickDash(CStateManager& mgr, float* weights, const CVector3f* target) {
  static const int skOpposite[8] = {1, 0, 3, 2, 7, 6, 5, 4};
  const CVector3f playerPos = mgr.GetPlayer(0)->GetTranslation();
  const CVector3f& pos = GetTranslation();
  const CVector3f toPlayer = playerPos - pos;
  const float playerDistSq = toPlayer.MagSquared();
  const CVector3f forward = GetTransform().GetForward();
  CVector3f facing(forward.GetX(), forward.GetY(), 0.f);
  if (facing.CanBeNormalized() == true) {
    facing.Normalize();
  } else {
    facing = CVector3f(1.f, 0.f, 0.f);
  }

  CVector3f ends[8];
  int slots[8];
  for (int i = 0; i < 8; ++i) {
    ends[i] = CVector3f::Zero();
  }
  if (mDash.mStepsLeft > 0) {
    for (int i = 0; i < 8; ++i) {
      slots[i] = i;
      if (mDash.mLastSlot == i || mDash.mLastSlot == skOpposite[i]) {
        weights[i] = 0.f;
      }
    }
  } else {
    for (int i = 0; i < 8; ++i) {
      slots[i] = i;
      if (mDash.mLastSlot == i) {
        weights[i] = weights[i] * 0.01f;
      }
    }
  }

  mDash.mSlot = 1;
  float height = pos.GetZ();
  const CPathFindSearch::EResult heightResult =
      mPathFindSearch.GetHeightOfPointAboveMesh(pos, height, 1.f);

  const float scaleY = GetModelData()->GetScale().GetY();
  const float reach =
      1.1f * (GetAnimationDistance(CPASAnimParmData(pas::kAS_Step, CPASAnimParm::FromEnum(0),
                                                    CPASAnimParm::FromEnum(0))) *
              scaleY);
  const CVector3f boxMax(pos.GetX() + reach, pos.GetY() + reach, pos.GetZ() + 6.f);
  const CVector3f boxMin(pos.GetX() + -reach, pos.GetY() + -reach, pos.GetZ() + -2.f);
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_AIBlock), CMaterialList(kMT_Player, kMT_AIPassthrough));
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, CAABox(boxMin, boxMax), filter, this);

  for (int i = 0; i < 8; ++i) {
    float& weight = weights[i];
    const float step = GetAnimationDistance(CPASAnimParmData(
                           pas::kAS_Step, CPASAnimParm::FromEnum(GetDashStepDirection(i)),
                           CPASAnimParm::FromEnum(0))) *
                       GetModelData()->GetScale().GetY();
    const CVector3f dir = GetDashDirection(i, facing);
    const CVector3f end(pos.GetX() + step * dir.GetX(), pos.GetY() + step * dir.GetY(),
                        pos.GetZ() + step * dir.GetZ());
    ends[i] = end;
    const float clearance = GetDashPathClearance(mgr, end, nearList);
    if (clearance < 1.f) {
      if (heightResult != CPathFindSearch::kR_Success) {
        weight = weight * (0.1f * clearance);
      } else if (mStillTime < 1.2f) {
        weight = 0.f;
        continue;
      } else {
        weight = weight * (0.02f * clearance);
      }
    }
    const float endDistSq = (end - playerPos).MagSquared();
    if (mDash.mStepsLeft > 0) {
      if (endDistSq < playerDistSq) {
        weight = 0.f;
        continue;
      }
      weight = weight * (endDistSq - playerDistSq);
    } else {
      float closest = 8.01f;
      // z is the player's z minus itself (`fsubs f21, f25, f25` at 0x00015C90), not 0.0f: NaN
      // for a non-finite player z.
      const float alongZ = playerPos.GetZ() - playerPos.GetZ();
      for (float t = 0.05f; t <= 1.f; t += 0.05f) {
        const CVector3f along((1.f - t) * pos.GetX() + t * end.GetX() - playerPos.GetX(),
                              (1.f - t) * pos.GetY() + t * end.GetY() - playerPos.GetY(), alongZ);
        const float dist = along.Magnitude();
        if (dist < closest) {
          closest = dist;
        }
      }
      if (closest < 8.f) {
        weight = weight * (0.5f * (closest * 0.125f));
      }
    }
    if (endDistSq < playerDistSq) {
      weight = weight * 0.5f;
    }
    if (target != nullptr) { // `cmplwi r25, 0x0` at 0x00015D6C/0x00015D70
      const float dist = (*target - end).Magnitude();
      if (dist < mMaxAttackRange) {
        weight = weight * (1.f - dist / mMaxAttackRange);
      } else {
        weight = weight * 0.01f;
      }
    }
  }

  float best = 0.f;
  for (int i = 0; i < 8; ++i) {
    if (weights[i] > best) {
      best = weights[i];
    }
  }
  const int start = mgr.Random()->Next();
  int pick = 1;
  for (int k = 0; k < 8; ++k) {
    const int idx = (start + k) % 8;
    if (best == weights[idx]) {
      pick = idx;
      break;
    }
  }
  mDash.mSlot = slots[pick];
  mDash.mDestination = ends[pick];
  mDash.mStartPosition = GetTranslation();
  mDash.mWeight = weights[pick];
  return weights[pick];
}

// fn_10_14FE0 (0x00014FE0, 0xFC bytes): the best cover point pushed (fn_10_1E3C0 / fn_10_1E26C at
// 0x00015008/0x0001501C), the dash picked with eight weights of lbl_10_rodata_3E8 = 1.0f and no
// target (fn_10_154FC at 0x0001505C, `li r6, 0x0`); a pick of weight 0.0f blocks the dash (0x80
// set at 0x00015078). Then the stuck count: standing still for more than lbl_10_rodata_42C = 1.2f
// (0x0001508C) counts 0x968 up and two or more (`cmplwi r0, 0x2` at 0x000150A4) fire the stuck
// blast (fn_10_14A00); otherwise the count is cleared (0x000150C0).
// fn_10_14FE0 (0x00014FE0, 0xFC bytes): the next dash. Arms: no dash to take (the pick
// answered zero), the actor stood still (the still timer above 1.2f, 0x000150A4) and the
// dash is stuck (the counter at 0x968 is two or more).
// // The arms: the coverage run (tools/cov/specs/darksamus_helpers2.py, the rooftop fight: Sanctuary 10_cliff_rooftop, boss u04f7, 6400 updates) showed the first two; the "stood still" and "stuck" arms were forced (the
// still timer and the counter written at the entry,
// tools/cov/specs/darksamus_helpers2_force.py).
// @ fn_10_14FE0
void CDarkSamus::PickNextDash(CStateManager& mgr) {
  PushAttacker(FindBestCoverPoint(mgr));
  float weights[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
  if (0.f == PickDash(mgr, weights, nullptr)) {
    mDash.mBlocked = true;
  }
  if (mStillTime > 1.2f) {
    ++mDashStuckCount;
    if (mDashStuckCount >= 2) {
      ApplyDashStuckBlast(mgr);
    }
  } else {
    mDashStuckCount = 0;
  }
}

// fn_10_14E5C (0x00014E5C, 0x184 bytes): the dash slot's step animation (kAS_Step with
// FromEnum(fn_10_EDE0(0x9E0)) and FromEnum(0), fn_10_13FFC at 0x00014F04 is the module's copy of
// the CPASAnimParmData constructor) found in the body controller's PAS database
// (FindBestAnimation at 0x00014F28 with mgr's random and -1), set on the animation data
// (CAnimPlaybackParms(anim, -1, 1.0f, true), SetAnimation at 0x00014F7C, no query) and advanced by
// lbl_10_rodata_46C = 0.3f (AdvanceAnimation at 0x00014FB4, the actor's area, the tree advanced,
// camera distance 0.0f); last the collision failsafe (0x00014FC0).
// @ fn_10_14E5C
void CDarkSamus::PlayDashStepAnimation(CStateManager& mgr) {
  const CPASAnimParmData parms(pas::kAS_Step,
                               CPASAnimParm::FromEnum(GetDashStepDirection(mDash.mSlot)),
                               CPASAnimParm::FromEnum(0));
  const rstl::pair< float, int > best =
      GetBodyController()->GetPASDatabase().FindBestAnimation(parms, *mgr.Random(), -1);
  ModelData()->AnimationData()->SetAnimation(CAnimPlaybackParms(best.second, -1, 1.f, true),
                                             false);
  ModelData()->AdvanceAnimation(0.3f, mgr, GetCurrentAreaId(), true, 0.f);
  RunCollisionFailsafe(mgr);
}

// fn_10_14C50 (0x00014C50, 0xC4 bytes): a fresh dash record (the zero vectors, -1 slots, 0.0f
// weight, 0x80 cleared: 0x00014C88..0x00014CCC) whose last slot is the slot just dashed
// (0x00014CD0), a new pick (fn_10_14FE0), one step less (0x00014CE8), the step command delivered
// as on Activate (fn_10_12C2C with `li r4, 0x0` at 0x00014CEC) and its animation (fn_10_14E5C).
// @ fn_10_14C50
void CDarkSamus::StartNextDashStep(CStateManager& mgr) {
  const int slot = mDash.mSlot;
  mDash.mStartPosition = CVector3f::Zero();
  mDash.mDestination = mDash.mStartPosition;
  mDash.mLastSlot = -1;
  mDash.mSlot = -1;
  mDash.mWeight = 0.f;
  mDash.mBlocked = false;
  mDash.mLastSlot = slot;
  PickNextDash(mgr);
  mDash.mStepsLeft = mDash.mStepsLeft - 1;
  DoDashStepCmd(kStateMsg_Activate);
  PlayDashStepAnimation(mgr);
}

// fn_10_14D14 (0x00014D14, 0x148 bytes): the double dash. The attack bookkeeping with id 0x8000
// (fn_10_1B478 at 0x00014D48); on Activate (`cmpwi r31, 0x0` at 0x00014D4C) the dash record is
// reset as in fn_10_14C50 (0x00014D74..0x00014DBC), one step is left (0x00014DC0) and a dash is
// picked (fn_10_14FE0). Then the collision failsafe (0x00014DD0); unless blocked (0x80, the
// `extrwi.` at 0x00014DD8) the step command (fn_10_12C2C) and, while a step is left and the base's
// GetAnimOver answers (CTriggerData(0.0f), 0x00014E14), the next step (fn_10_14C50). Deactivate
// (`cmpwi r31, 0x2` at 0x00014E30) clears the steps left.
// fn_10_14D14 (0x00014D14, 0x148 bytes): the double dash. Arms: the activate (the dash's
// state is reset and the next dash is picked, 0x00014D44), the deactivate (the step counter is
// cleared, 0x00014D8C), the dash is not blocked (the step command) and the dash is blocked,
// and the dash has a step left and the animation is over (the next dash step,
// 0x00014DA4).
// The coverage run (tools/cov/run.sh with tools/cov/specs/darksamus_helpers2_loc.py, the
// rooftop fight: 109 calls) showed all of them: the activate 3, the deactivate 3, the step
// command 108 (the dash is blocked once), the next dash step 1.
// @ fn_10_14D14
void CDarkSamus::UpdateDoubleDash(CStateManager& mgr, EStateMsg msg) {
  UpdateCurrentAttack(0x8000, mgr, msg);
  if (msg == kStateMsg_Activate) {
    mDash.mStartPosition = CVector3f::Zero();
    mDash.mDestination = mDash.mStartPosition;
    mDash.mLastSlot = -1;
    mDash.mSlot = -1;
    mDash.mWeight = 0.f;
    mDash.mBlocked = false;
    mDash.mStepsLeft = 1;
    PickNextDash(mgr);
  }
  RunCollisionFailsafe(mgr);
  if (mDash.mBlocked == false) {
    DoDashStepCmd(msg);
    if (mDash.mStepsLeft > 0 && GetAnimOver(mgr, CTriggerData(0.f)) == true) {
      StartNextDashStep(mgr);
    }
  }
  if (msg == kStateMsg_Deactivate) {
    mDash.mStepsLeft = 0;
  }
}

// fn_10_21DF8 (0x00021DF8, 0x11C bytes): the step command, the same shape as fn_10_21F14. Activate
// (0x00021E38) sets the animation state 0x6B4 to 1 (kAS_Ready) and delivers the command (DeliverCmd
// at 0x00021E50, the step slot copy at 0x00021E54..0x00021E88); Update (0x00021E90) delivers it
// again while CanIssueCommand(state) answers true (0x00021E98); Deactivate (0x00021EF4) sets the
// animation state to 0 (kAS_NotReady).
// @ fn_10_21DF8
void CDarkSamus::DoStepCmd(EStateMsg msg, pas::EAnimationState state, const CBCStepCmd& cmd) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(cmd);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), state)) {
      BodyController()->CommandMgr().DeliverCmd(cmd);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  default:
    break;
  }
}

// fn_10_21F14 (0x00021F14, 0x10C bytes): Activate (0x00021F54) sets the animation state to 1
// (0x6B4, the base's CAnimationState: kAS_Ready) and delivers the command (DeliverCmd at
// 0x00021F6C, the projectile-attack slot copy at 0x00021F78..0x00021F9C); Update (0x00021FA4)
// delivers it again while CanIssueCommand(state) answers true (0x00021FAC); Deactivate
// (0x00022000) sets the animation state to 0 (kAS_NotReady).
// Arms: Activate, Update with CanIssueCommand false, Deactivate natural; CanIssueCommand true
// forced (the animation state Ready, tools/cov/specs/darksamus_ds39_force.py).
// @ fn_10_21F14
void CDarkSamus::DoProjectileAttackCmd(EStateMsg msg, pas::EAnimationState state,
                                       const CBCProjectileAttackCmd& cmd) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(cmd);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), state)) {
      BodyController()->CommandMgr().DeliverCmd(cmd);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  default:
    // Dead: EStateMsg has three values (StateMachineCommon.hpp) and every caller passes its
    // state's message; retail's out-of-range exits are the 0x00021F44 (`b`, below 0) and
    // 0x00021F4C (`bge`, 3 and up).
    break;
  }
}
