// The 39 triggers of the SpacePirate state machine (retail table fn_72_10E50 ->
// lbl_72_data_1D8). Every one of them is retail's: the bodies follow the REL's disassembly
// (build/G2ME01/SpacePirate/asm/auto_00_00000000_text.s), and the m2c pseudo-C
// (build/G2ME01/SpacePirate/m2c/fn_72_<off>.c) is only a reading aid. Triggers that read a flag
// bit use the names in CSpacePirate.hpp (MP1's where the mask matches).
#include "MetroidPrime/Enemies/CSpacePirate.hpp"

#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CMetroid.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAiJumpPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCoverPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTargetingPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/StateMachineCommon.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "rstl/algorithm.hpp"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

// PortCSpacePirate.cpp's TraceCall sits in that file's anonymous namespace, so it has no external
// linkage to declare here; this is the same MP2_DEBUG_SPACEPIRATE gate and line format, so a run
// that exercises a trigger below proves it exactly like the entry points there do.
namespace {
void TraceCall(const CSpacePirate& self, const char* what, int arg) {
  static const bool sOn = getenv("MP2_DEBUG_SPACEPIRATE") != nullptr;
  if (sOn) {
    fprintf(stderr, "[spacepirate] %u %s %d\n", self.GetUniqueId().Value(), what, arg);
  }
}
} // namespace

// @ fn_72_C488 (0xB0): the state machine's time (0x350, vtable slot 0x30, 0xC4B0-0xC4C0) must beat
// rodata B44 = 0.5 (0xC4CC `fcmpo`, 0xC4D0 `ble` -> false); then CPatterned::Stuck (0xC4E4, true ->
// 0xC4EC `bne`) or CPatterned::PathShagged (0xC4FC, 0xC504 `beq`). The gdb run (2026-10-21,
// casualpirates) ran every arm: 400 entries but 181 CPatterned::Stuck calls (the <= 0.5 arm), one
// Stuck true, and 7 results true against that one (PathShagged true) as well as false ones.
bool CSpacePirate::Stuck(CStateManager& mgr, const CTriggerData& data) const {
  if (mStateMachine->GetTime() > 0.5f) {
    return CPatterned::Stuck(mgr, data) || CPatterned::PathShagged(mgr, data);
  }
  return false;
}

// @ fn_72_C468 (0x20): a tail of CPatterned::Stuck (0xC474), no branch; the gdb run saw both results.
bool CSpacePirate::PatternShagged(CStateManager& mgr, const CTriggerData& data) const {
  return CPatterned::Stuck(mgr, data);
}

// @ fn_72_BC78 (0x5C): the noise flag is read and cleared in one go - asm 0xBC80 `extrwi. 1,25`
// reads the 0x8f6 mask 0x40 bit and asm 0xBC84 `rlwimi` clears it - and the old value is the
// result (asm 0xBC8C). The body has no branch, so the area run's calls prove all of it.
bool CSpacePirate::HearShot(CStateManager& mgr, const CTriggerData& data) const {
  const bool heard = x8f6_25_hearNoise;
  const_cast< CSpacePirate* >(this)->x8f6_25_hearNoise = false;
  TraceCall(*this, "HearShot trigger", heard ? 1 : 0);
  return heard;
}

// @ fn_72_BBC0 (0xB8): the trigger polls one player per call, so a second player does not add
// hearing range in the same frame. The player index at 0xbc0 is rotated by one modulo the player
// count (asm 0xBBD0 `divwu`), the player's velocity (0x1a8) has to move faster than rodata 0xB3C
// = 0.1 in squared magnitude (asm 0xBC24) and the player's translation (0x54/0x58/0x5c) has to
// be inside the data's hearing radius at 0x7d4 (asm 0xBC64). The index is checked against
// 0xffff before the player is read (asm 0xBBE8); the port's null check covers the same case,
// and its empty-player check keeps the modulo away from a division by zero. The trace's argument
// is the arm: 0 = no player or a player that is not moving, 1 = the player is moving and inside
// the hearing radius, 2 = the player is moving but too far. All three ran in the 2026-10-21 runs:
// arm 1 in 06_sand_alamofront, where a trooper with a 100-unit hearing radius was woken with
// `send <id> Activate` and the player walked inside its radius.
bool CSpacePirate::HearPlayer(CStateManager& mgr, const CTriggerData& data) const {
  CSpacePirate* self = const_cast< CSpacePirate* >(this);
  const int count = static_cast< int >(mgr.GetNumPlayers());
  if (count <= 0) {
    TraceCall(*this, "HearPlayer trigger", 0);
    return false;
  }
  self->xbc0_playerIndex = (self->xbc0_playerIndex + 1) % count;
  const CPlayer* player = mgr.GetPlayer(self->xbc0_playerIndex);
  // 0xBC24 fcmpo + 0xBC28 ble (rodata 0.1f): a NaN speed is not heard.
  if (player == nullptr || !(player->GetVelocityWR().MagSquared() > 0.1f)) {
    TraceCall(*this, "HearPlayer trigger", 0);
    return false;
  }
  const CVector3f delta = player->GetTranslation() - GetTranslation();
  const bool heard = delta.MagSquared() < mData.mHearingRadius * mData.mHearingRadius;
  // The trace's argument is the arm: 0 = no player or a player that is not moving, 1 = the
  // player is moving and inside the hearing radius, 2 = the player is moving but too far.
  TraceCall(*this, "HearPlayer trigger", heard ? 1 : 2);
  return heard;
}

// The 208 cycle does evaluate this trigger (2851 calls in the 2026-10-20 run), but it is always
// false there, so only the first branch is proven by the run.
// Retail: a pirate with the non-aggressive bit (0x8f4 mask 0x20) never charges. Otherwise it
// charges when it is always aggressive (0x8f8 mask 0x20), or when no pirate is charging yet (the
// module's static charge list at bss_40 is empty) and the player has been out of sight for more
// than 10 s (rodata B58). A charging pirate that is not in that list yet adds its id to it: the
// asm walk ends at the list's tail sentinel, so "reached the tail" means "not in the list" and
// pushes (fn_72_AEB8 -> fn_72_AEE0 is a push_back), while a match stops the walk and does not.
// @ fn_72_ADC0 (0xF8): the AggressionCheck trigger.
bool CSpacePirate::AggressionCheck(CStateManager& mgr, const CTriggerData& data) const {
  bool result = false;
  if (!x8f4_26_nonAggressive) {
    if (x8f8_26_alwaysAggressive) {
      result = true;
    } else if (mChargePlayerList.empty() && xa7c_timeNoPlayerLos > 10.f) {
      result = true;
    }
    if (result) {
      if (rstl::find(mChargePlayerList.begin(), mChargePlayerList.end(), GetUniqueId()) ==
          mChargePlayerList.end()) {
        mChargePlayerList.push_back(GetUniqueId());
      }
    }
  }
  TraceCall(*this, "AggressionCheck trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_ADB4 (0xC): the 0x8f8 byte (0xADB4) bit 27, mask 0x10 (0xADB8 `extrwi 1,27`). Branchless;
// 400+ calls in the gdb run (all false: no cover check is set in the shipped data).
bool CSpacePirate::CoverCheck(CStateManager& mgr, const CTriggerData& data) const {
  return x8f8_27_coverCheck;
}

// @ fn_72_ABC0 (0x1F4): the cover-point search - the fight run evaluates it 1822 times.
// Retail: the AiWaypoint object list (mgr+0x838) is scanned for the cover points this pirate
// could take - active (0x20 bit 7), not a land-hang point (fn_800EC6A8), not reserved by this
// pirate (fn_800EC550 with this pirate's id), in this area, not the cover point it used last
// time (0x902) and closer than mData.mSearchRadius (0x7c8) squared. The closest one that is
// not blown by the target position (fn_72_CB30, not the player) wins. Then the old cover point
// is released (retainCooldown 1), the new one taken, 0x902 remembers it and 0x914 becomes
// -cover forward. The target is fetched again through the object list, so a point that vanished
// leaves the trigger false.
bool CSpacePirate::CoverFind(CStateManager& mgr, const CTriggerData& data) const {
  CSpacePirate* self = const_cast< CSpacePirate* >(this);
  bool result = false;
  float minDistSq = mData.mSearchRadius * mData.mSearchRadius;
  const CScriptCoverPoint* closest = nullptr;
  const CObjectList& list = mgr.GetObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptCoverPoint* point = TCastToConstPtr< CScriptCoverPoint >(list[i]);
    if (point == nullptr) {
      continue;
    }
    if (!point->GetActive() || point->ShouldLandHere() || point->GetInUse(GetUniqueId()) ||
        point->GetCurrentAreaId() != GetCurrentAreaId() ||
        point->GetUniqueId() == self->x902_previousCoverPoint) {
      continue;
    }
    const float distSq = (GetTranslation() - point->GetTranslation()).MagSquared();
    // 0xACB8 fcmpo + 0xACBC bge: a NaN distance skips the point.
    if (!(distSq < minDistSq)) {
      continue;
    }
    if (point->Blown(self->GetTargetPos(mgr))) {
      continue;
    }
    minDistSq = distSq;
    closest = point;
  }
  if (closest != nullptr) {
    self->ReleaseCoverPoint(mgr, self->x900_coverPoint, true);
    CScriptCoverPoint* point = TCastToPtr< CScriptCoverPoint >(mgr.ObjectById(closest->GetUniqueId()));
    if (point != nullptr) {
      self->SetCoverPoint(point, self->x900_coverPoint);
      result = true;
      self->x902_previousCoverPoint = self->x900_coverPoint;
      self->x914_coverPointRearDir = -closest->GetTransform().GetForward();
    }
  }
  TraceCall(*this, "CoverFind trigger", result ? 1 : 0);
  return result;
}

// fn_72_AA64.
// Retail: the point that is tested is the pirate's own target position (fn_72_CB30), not the
// player. Inside the minimum attack range (0x3dc) the cover is not blown yet, so the trigger is
// true. Outside it the cover point reports whether that target is inside its safe angle
// (fn_800EC5A0). A cover that is not blown but more than 3 * scaleY away from the pirate (the
// model's Y scale, 0x60) also counts as blown - the asm compares the squared distance with
// 3.0 * scaleY, it does not square the radius.
// gcov 2026-10-21: ALL-ARMS. Sandfalls L3 pirates in PathFind/TargetCover give the cover-point arms;
// spacepirate_force5.py forces the rest one call each: the minimum attack range, a stopped pirate far
// from its cover, the same pirate near it (scale Y), and the same pirate stepping (kAS_Step).
// @ fn_72_AA64
bool CSpacePirate::CoverBlown(CStateManager& mgr, const CTriggerData& data) const {
  CSpacePirate* self = const_cast< CSpacePirate* >(this);
  bool result = true;
  const CVector3f target = self->GetTargetPos(mgr);
  const CVector3f toTarget = target - GetTranslation();
  if (toTarget.MagSquared() > mMinAttackRange * mMinAttackRange) {
    const CScriptCoverPoint* point = GetCoverPoint(mgr, x900_coverPoint);
    if (point != nullptr) {
      result = point->Blown(target);
      if (!result && x904_steeringSpeed == 0.f &&
          GetBodyController()->GetCurrentStateId() != pas::kAS_Step) {
        const CVector3f toCover = point->GetTranslation() - GetTranslation();
        if (toCover.MagSquared() > 3.f * GetModelData()->GetScale().GetY()) {
          result = true;
        }
      }
    }
  }
  TraceCall(*this, "CoverBlown trigger", result ? 1 : 0);
  return result;
}

// gcov 2026-10-21: ALL-ARMS. The no-player-target arm is forced once (spacepirate_force4.py: the
// attack target set to the invalid id on one call, restored at the next CoverBlown).
// Retail: with no cover point the trigger is true. With one it asks whether the cover is blown
// by the attack target's position plus its velocity (1.0 * velocity, so the raw velocity is
// added). The target is the pirate's attack target cast to CPlayer - not mgr.GetPlayer() - so a
// missing or non-player target leaves the trigger false.
// @ fn_72_A988
bool CSpacePirate::CoverNearlyBlown(CStateManager& mgr, const CTriggerData& data) const {
  bool result = true;
  const CScriptCoverPoint* point = GetCoverPoint(mgr, x900_coverPoint);
  if (point != nullptr) {
    result = false;
    const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget));
    if (player != nullptr) {
      const CVector3f pos = player->GetTranslation() + player->GetVelocityWR();
      result = point->Blown(pos);
    }
  }
  TraceCall(*this, "CoverNearlyBlown trigger", result ? 1 : 0);
  return result;
}

// gcov 2026-10-21: ALL-ARMS. The other-area arm is forced once (spacepirate_force4.py: this
// pirate's area id = 99 on one call, restored at the next CoverBlown).
// Retail: the listening-AI object list (mgr+0x830) is scanned for other pirates; one of them
// that has its in-attack-state bit (0x8f6 mask 0x01) set and is in this area means this pirate
// is covering that one's fire. The scan does not stop at the first hit.
// @ fn_72_A8D0
bool CSpacePirate::CoveringFire(CStateManager& mgr, const CTriggerData& data) const {
  bool result = false;
  const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CSpacePirate* pirate = TCastToConstPtr< CSpacePirate >(list[i]);
    if (pirate == nullptr || pirate == this) {
      continue;
    }
    if (!pirate->x8f6_31_inAttackState || pirate->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    result = true;
  }
  TraceCall(*this, "CoveringFire trigger", result ? 1 : 0);
  return result;
}

// Coverage (2026-10-22, sp20): `trigcall <pirate> ShouldAttack` in 07_sand_alamoportalobservation with
// five pirates placed 4 units off the player and sent to Attack gives the closer>3 (false) arm and the
// non-player target; the own-list entry, another area and a dead attacker are FORCED arms
// (tools/cov/specs/spacepirate_force7.py: target = the player, this pirate's area 77, the last
// attacking pirate's mAlive 0, each for one call).
// The asm casts the attack target to CPlayer (it does not compare ids as MP1 does) and then refuses
// the attack while more than three other pirates, alive and in the attack state in this area, are
// closer to that player than this one.
// @ fn_72_A71C
bool CSpacePirate::ShouldAttack(CStateManager& mgr, const CTriggerData& data) const {
  bool result = true;
  if (TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget)) != nullptr) {
    const CVector3f target = const_cast< CSpacePirate* >(this)->GetTargetPos(mgr);
    const float distSq = (GetTranslation() - target).MagSquared();
    int closer = 0;
    const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
    for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
      const CSpacePirate* pirate = TCastToConstPtr< CSpacePirate >(list[i]);
      if (pirate == nullptr || pirate == this || !pirate->x8f6_31_inAttackState ||
          !pirate->GetAlive() || pirate->GetCurrentAreaId() != GetCurrentAreaId()) {
        continue;
      }
      if ((pirate->GetTranslation() - target).MagSquared() < distSq) {
        ++closer;
        if (closer > 3) {
          result = false;
        }
      }
    }
  }
  TraceCall(*this, "ShouldAttack trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_9378 (0x14): the 0x8f7 byte's bit 28 (mask 0x08, the data flag 0x10) inverted - the
// `cntlzw`/`srwi` pair at asm 0x9380 is that inversion. The body is one bit test, so every
// evaluation does the same work and the area run's 2279 calls prove all of it.
bool CSpacePirate::LineOfSight(CStateManager& mgr, const CTriggerData& data) const {
  const bool result = !x8f7_28_noPlayerLos;
  TraceCall(*this, "LineOfSight trigger", result ? 1 : 0);
  return result;
}

// Retail reads the waypoint navigation's destination (this+0x6c0, which TargetPatrol sets), not
// MP1's mDestObj: with mDestObj, TargetPatrol ended on its first tick and the skiff pirates in
// Central Mining Station lurked on the skiff instead of walking off it.
// @ fn_72_BC94
bool CSpacePirate::PatternOver(CStateManager& mgr, const CTriggerData& data) const {
  return mWaypointNavigation.GetDestination() == kInvalidUniqueId;
}

// The detection cone test, over every player: the first one whose offset lies inside
// mDetectionAngle (cosine) of the transform's forward is spotted.
// @ fn_72_938C (0xEC): for every player the trigger asks whether the player's direction is
// inside the view cone - `dot(player - pirate, the pirate's forward) > |player - pirate| * the
// radius at 0x3d8` (asm 0x9404-0x9438) - and returns true on the first that is. The module's FSM
// data (the AFSM 0x5652a12f, read by tools/fsm_state_triggers.py) names it in the Patrol, Lurk
// and PathFind states. The 2026-10-21 area run evaluated it 2710 times - 2601 true and 109
// false - so both arms ran.
bool CSpacePirate::SpotPlayer(CStateManager& mgr, const CTriggerData& data) const {
  const uint count = mgr.GetNumPlayers();
  for (uint i = 0; i < count; ++i) {
    const CPlayer* player = mgr.GetPlayer(i);
    if (player == nullptr) {
      continue;
    }
    const CVector3f toPlayer = player->GetTranslation() - GetTranslation();
    if (CVector3f::Dot(toPlayer, GetTransform().GetForward()) >
        toPlayer.Magnitude() * mDetectionAngle) {
      TraceCall(*this, "SpotPlayer trigger", 1);
      return true;
    }
  }
  TraceCall(*this, "SpotPlayer trigger", 0);
  return false;
}

// @ fn_72_82D0
// Retail reads 0x8f4 bit 0x20, the nonAggressive bit, as MP1 does (the ctor unpacks mFlags 0x4
// into it; mFlags 0x20 is x8f4_29_onlyAttackInRange, bit 0x04).
bool CSpacePirate::ShouldDodge(CStateManager& mgr, const CTriggerData& data) const {
  bool result = false;
  if (x8f8_28_enableDodge) {
    if (!x8f4_26_nonAggressive && !x8f8_29_noPlayerDodge) {
      const CVector3f toTarget =
          const_cast< CSpacePirate* >(this)->GetTargetPos(mgr) - GetTranslation();
      result = CVector3f::Dot(toTarget, GetTransform().GetForward()) > 0.f &&
               (xb24_timeSinceHitByPlayer < 0.33f || xb28_lowHealthFrenzyTimer < 0.33f) &&
               xa7c_timeNoPlayerLos < 0.5f;
    }
    // A dodge also answers a metroid that is attacking this pirate from the front.
    if (!result) {
      const CMetroid* metroid = TCastToConstPtr< CMetroid >(mgr.GetObjectById(mAttackTarget));
      if (metroid != nullptr && metroid->IsAttacking()) {
        const CVector3f delta = GetTranslation() - metroid->GetTranslation();
        if (CVector3f::Dot(delta, metroid->GetTransform().GetForward()) > 0.f) {
          result = true;
        }
      }
    }
  }
  TraceCall(*this, "ShouldDodge trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_7C28
// This trigger writes: the retreat waypoint becomes the destination and the pirate drops its cover
// point, aim and noise state. With no waypoint it retreats to where it already stands, and mDestObj
// keeps whatever GetConnectedObject returned (kInvalidUniqueId), as retail stores that same value.
bool CSpacePirate::ShouldRetreat(CStateManager& mgr, const CTriggerData& data) const {
  if (!x8f6_29_enableRetreat || x8f6_24_trooper) {
    TraceCall(*this, "ShouldRetreat trigger", 0);
    return false;
  }
  CSpacePirate* self = const_cast< CSpacePirate* >(this);
  const TUniqueId wpId = GetConnectedObject(mgr, kSS_Patrol, kSM_Follow);
  const CScriptWaypoint* wp = TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(wpId));
  if (wp == nullptr) {
    const rstl::vector< SConnection >& connections = GetConnectionList();
    for (AUTO(it, connections.begin()); it != connections.end(); ++it) {
      if (it->state != kSS_Patrol || it->msg != kSM_Follow) {
        continue;
      }
      wp = TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(mgr.GetIdForScript(it->objId)));
      if (wp != nullptr) {
        break;
      }
    }
  }
  if (wp != nullptr) {
    self->mDestObj = wpId;
    self->SetDestPos(wp->GetTranslation());
  } else {
    self->mDestObj = kInvalidUniqueId;
    self->SetDestPos(GetTranslation());
  }
  self->x8f6_29_enableRetreat = false;
  self->mReflectedDestPos = GetTranslation();
  self->mInPosition = false;
  self->ReleaseCoverPoint(mgr, self->x900_coverPoint, true);
  self->x8f6_25_hearNoise = false;
  self->mEnableAim = false;
  self->SetHitByPlayerProjectile(false);
  TraceCall(*this, "ShouldRetreat trigger", 1);
  return true;
}

// @ fn_72_8A84 (0xC): the 0x8f9 byte (0x8A84) bit 27, mask 0x10 (0x8A88 `extrwi 1,27`). Branchless; the
// gdb run evaluated it 59 times (07_sand_alamoportalobservation L3, the PathFind state).
bool CSpacePirate::InRange(CStateManager& mgr, const CTriggerData& data) const {
  return x8f9_27_inRange;
}

// @ fn_72_99A4. no state the 208 cycle reaches lists this trigger (2026-10-20 run).
// Retail: the pirate's own cover point (0x900) is fetched; with no cover point the trigger is
// false, with one it is whatever the cover point's crouch flag says (fn_800EC6CC, returned
// unmasked).
bool CSpacePirate::ShouldCrouch(CStateManager& mgr, const CTriggerData& data) const {
  bool result = false;
  const CScriptCoverPoint* point = GetCoverPoint(mgr, x900_coverPoint);
  if (point != nullptr) {
    result = point->ShouldCrouch();
  }
  TraceCall(*this, "ShouldCrouch trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_7BD4. no state the 208 cycle reaches lists this trigger (2026-10-20 run).
bool CSpacePirate::ShouldMove(CStateManager& mgr, const CTriggerData& data) const {
  const CScriptCoverPoint* cover = GetCoverPoint(mgr, x900_coverPoint);
  const bool result = cover != nullptr && !cover->ShouldStay();
  TraceCall(*this, "ShouldMove trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_7BA0 (0x34): the same shape as `7B6C` - the trigger's float argument, or rodata
// 0xB44 = 0.5 when it is 0, against 0xb28 (`xb28_lowHealthFrenzyTimer`, which the Think zeroes
// when a player projectile is in the pirate's path), the comparison being `timer < window`
// (asm 0x7BC0-0x7BD0). Both arms ran on 2026-10-21 in 06_sand_alamofront: the false one
// while the pirate attacked and no shot had passed it in the last 0.5 s, the true one right
// after a shot that did.
bool CSpacePirate::ShotAt(CStateManager& mgr, const CTriggerData& data) const {
  const float window = data.GetFloat() != 0.f ? data.GetFloat() : 0.5f;
  const bool result = xb28_lowHealthFrenzyTimer < window;
  TraceCall(*this, "ShotAt trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_7B6C
bool CSpacePirate::Attacked(CStateManager& mgr, const CTriggerData& data) const {
  const float window = data.GetFloat() != 0.f ? data.GetFloat() : 0.5f;
  return xb24_timeSinceHitByPlayer < window;
}

// The 208 cycle does evaluate this trigger (about 2850 calls in the 2026-10-20 run), but it is
// always false there and the attack target is the player every time, so only the box/near-list
// branch is proven by the run: the team-target branch and the active-non-player early return are
// not.
// Retail: the attack target (0xa94) is cast to CActor and then to CPlayer. An active non-player
// target ends the trigger right there (true, nothing else runs). Otherwise the body runs: with
// no player target the pirate picks a team target (fn_72_EFD4, which returns the id), tells the
// squad (fn_72_DE5C) and points its bone tracking at that target. Then a CAABox around the
// translation - the margin is mData.mSearchRadius (0x7c8) times the rodata constant 1.0 - is
// searched for targeting points, with the filter built from the rodata word lbl_72_data_8A8
// (0x3b = kMT_Solid) as an exclude list (include 0xffffffff, exclude 1 << 59, type 2). The first
// targeting point that is active, in this area and not locked (fn_8012CB70) becomes the attack
// target; the squad is told and the bone tracking follows. The asm breaks out of the loop on
// that first hit (the m2c loop only looks like it does not advance).
// @ fn_72_7970 (0x1FC): the HasTargetingPoint trigger.
bool CSpacePirate::HasTargetingPoint(CStateManager& mgr, const CTriggerData& data) const {
  CSpacePirate* self = const_cast< CSpacePirate* >(this);
  bool result = false;
  const CActor* actor = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget));
  const CPlayer* player = TCastToConstPtr< CPlayer >(actor);
  if (player == nullptr && actor != nullptr && actor->GetActive()) {
    TraceCall(*this, "HasTargetingPoint trigger", 1);
    return true;
  }
  if (player == nullptr) {
    self->mAttackTarget = self->ChooseTeamTarget(mgr);
    self->UpdateTeamTarget(mgr);
    self->mBoneTracking.SetTarget(self->mAttackTarget);
  }
  const float margin = mData.mSearchRadius * 1.f;
  const CVector3f center = GetTranslation();
  const CAABox bounds(center - CVector3f(margin, margin, margin),
                      center + CVector3f(margin, margin, margin));
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, bounds, CMaterialFilter::MakeExclude(CMaterialList(kMT_Solid)),
                     nullptr);
  for (uint i = 0; i < nearList.size(); ++i) {
    const CScriptTargetingPoint* point =
        TCastToConstPtr< CScriptTargetingPoint >(mgr.GetObjectById(nearList[i]));
    if (point == nullptr || !point->GetActive() ||
        point->GetCurrentAreaId() != GetCurrentAreaId() || point->GetLocked()) {
      continue;
    }
    result = true;
    self->mAttackTarget = point->GetUniqueId();
    self->UpdateTeamTarget(mgr);
    self->mBoneTracking.SetTarget(self->mAttackTarget);
    break;
  }
  TraceCall(*this, "HasTargetingPoint trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_767C. no state the 208 cycle reaches lists this trigger (2026-10-20 run).
// Retail: the pirate's own cover point (0x900) is fetched; with no cover point the trigger is
// false, with one it is the cover point's wall-hang flag (fn_800EC6B4, tested on the top byte
// of the return value).
bool CSpacePirate::ShouldWallHang(CStateManager& mgr, const CTriggerData& data) const {
  bool result = false;
  const CScriptCoverPoint* point = GetCoverPoint(mgr, x900_coverPoint);
  if (point != nullptr) {
    result = point->ShouldWallHang();
  }
  TraceCall(*this, "ShouldWallHang trigger", result ? 1 : 0);
  return result;
}

// fn_72_689C (0x48): in a wall hang (the 0x8f7 byte's bit 29, asm 0x68AC) the animation is over
// once the body has left kAS_WallHang (20; the body controller 0x48c, its state 0x37c, asm 0x68B4-0x68C8 is the branchless `state != 20`);
// otherwise CPatterned::AnimOver (asm 0x68D0). Both arms ran (tools/cov, 08_sand_casualpirates L3 and
// 07_sand_alamoportalobservation L3, 2026-10-21); the wall-hang arm only from a forced WallHang.
// @ fn_72_689C
bool CSpacePirate::AnimOver(CStateManager& mgr, const CTriggerData& data) const {
  if (x8f7_29_inWallHang) {
    return GetBodyController()->GetCurrentStateId() != pas::kAS_WallHang;
  }
  return CPatterned::AnimOver(mgr, data);
}

// gcov 2026-10-22 (sp22, ALL-ARMS): `trigcall <trooper> ShouldStrafe` with the other trooper put
// around it (tools/drivers/spacepirate_strafe.py, 07_sand_alamoportalobservation L3); forced: the
// other-area skip (spacepirate_force9.py, this pirate read as area 77 for one call in ten).
// Retail's step-direction picker for the strafe trigger: a side is dropped when another pirate of
// the same area is that way (dot against the transform's right axis, in the squared-distance frame
// MP1 uses), then the survivors must be on the path find mesh and have a clear dynamic ray. Both
// sides clear is a coin flip on CRandom16 bit 0x4000. Returns pas::EStepDirection.
// @ fn_72_CE30
int CSpacePirate::GetStrafeDir(CStateManager& mgr, float dist) {
  const float distSquared = dist * dist;
  bool canStrafeLeft = true;
  bool canStrafeRight = true;
  CObjectList& list = mgr.ObjectListById(kOL_ListeningAi);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CSpacePirate* other = TCastToConstPtr< CSpacePirate >(list[i]);
    if (other == nullptr || other == this || other->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    const CVector3f delta = other->GetTranslation() - GetTranslation();
    const float magSquared = delta.MagSquared();
    // 0xCED8 fcmpo + 0xCEDC bge skips this pirate: a NaN distance is skipped too.
    if (!(magSquared < distSquared)) {
      continue;
    }
    const float rightDot = CVector3f::Dot(delta, GetTransform().GetRight());
    if (rightDot > 0.866f * magSquared || (rightDot > 0.f && magSquared < 3.f)) {
      canStrafeRight = false;
    } else if (rightDot < 0.866f * -magSquared || (rightDot < 0.f && magSquared < 3.f)) {
      canStrafeLeft = false;
    }
  }

  const CVector3f center = GetBoundingBox().GetCenterPoint();
  const CVector3f right = GetTransform().GetRight();
  if (canStrafeRight) {
    canStrafeRight =
        mPathFindSearch.OnPath(center + dist * right) == CPathFindSearch::kR_Success;
  }
  if (canStrafeLeft) {
    canStrafeLeft = mPathFindSearch.OnPath(center - dist * right) == CPathFindSearch::kR_Success;
  }
  if (!canStrafeLeft && !canStrafeRight) {
    TraceCall(*this, "GetStrafeDir", pas::kSD_Invalid);
    return pas::kSD_Invalid;
  }

  // The near list is anchored on the centre when the left side is open and on the left end point
  // otherwise; its length doubles when both sides are still in play (0xB60 = 2).
  const CVector3f startPoint = canStrafeLeft ? center : center - dist * right;
  const float length = canStrafeLeft && canStrafeRight ? 2.f * dist : dist;
  const CMaterialFilter filter = CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid));
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, startPoint, right, length, filter, this);
  if (canStrafeLeft) {
    canStrafeLeft = CGameCollision::RayDynamicLineOfSightTest(mgr, startPoint, right, dist, filter,
                                                             nearList, nullptr);
  }
  if (canStrafeRight) {
    canStrafeRight = CGameCollision::RayDynamicLineOfSightTest(mgr, center, right, dist, filter,
                                                               nearList, nullptr);
  }
  if (canStrafeLeft && canStrafeRight) {
    if ((mgr.Random()->Next() & 0x4000) != 0) {
      canStrafeLeft = false;
    } else {
      canStrafeRight = false;
    }
  }

  int straightDir = pas::kSD_Invalid;
  if (canStrafeLeft) {
    straightDir = pas::kSD_Left;
  } else if (canStrafeRight) {
    straightDir = pas::kSD_Right;
  }
  TraceCall(*this, "GetStrafeDir", straightDir);
  return straightDir;
}

// @ fn_72_960C
// Two ways to skid: a hurt or frenzied pirate whose target is ahead and close to its own axis
// strafes at 10 m (0xB58) around a 0.66 s (0xC04) window and a zero delay timer; failing that, a
// player the pirate has lost line of sight to for over a second and that stands within 15 m
// (0xB88) makes it strafe at 5 m (0xB78). Either way -1 leaves it facing the target.
bool CSpacePirate::ShouldStrafe(CStateManager& mgr, const CTriggerData& data) const {
  CSpacePirate* self = const_cast< CSpacePirate* >(this);
  self->xb08_skidDir = -1;
  bool result = false;
  bool noPlayerStrafe = false;
  if (!x8f4_26_nonAggressive) {
    const CVector3f toTarget = self->GetTargetPos(mgr) - GetTranslation();
    if (CVector3f::Dot(toTarget, GetTransform().GetForward()) > 0.f) {
      if ((xb28_lowHealthFrenzyTimer < 0.66f || xb24_timeSinceHitByPlayer < 0.66f) &&
          xb0c_strafeDelayTimer == 0.f) {
        const CVector3f center = GetBoundingBox().GetCenterPoint();
        const CVector3f toTargetFromCenter = (self->GetTargetPos(mgr) - center).AsNormalized();
        if (CVector3f::Dot(toTargetFromCenter, GetTransform().GetForward()) > 0.707f) {
          self->xb08_skidDir = self->GetStrafeDir(mgr, 10.f);
          if (self->xb08_skidDir != -1) {
            result = true;
          } else {
            noPlayerStrafe = true;
          }
        }
      }
      if (!noPlayerStrafe && !result && xa7c_timeNoPlayerLos > 1.f) {
        const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mAttackTarget));
        if (player != nullptr) {
          const CVector3f delta = player->GetTranslation() - GetTranslation();
          if (delta.Magnitude() < 15.f && self->xb08_skidDir == -1) {
            self->xb08_skidDir = self->GetStrafeDir(mgr, 5.f);
            if (self->xb08_skidDir != -1) {
              result = true;
            }
          }
        }
      }
    }
  }
  TraceCall(*this, "ShouldStrafe trigger", result ? 1 : 0);
  return result;
}

// Dead in shipped data: neither SpacePirate FSM on the disc (AFSM 0x5652a12f, 0x7b49526b) lists this
// trigger (disc scan of every PIRT placement, 2026-10-21).
bool CSpacePirate::ShouldSpecialAttack(CStateManager& mgr, const CTriggerData& data) const {
  return x8f4_29_onlyAttackInRange && !mBurstFire.IsBurstSet() && mAttackRemTime > 2.f;
}

// Dead in shipped data: neither SpacePirate FSM on the disc (AFSM 0x5652a12f, 0x7b49526b) lists this
// trigger (disc scan of every PIRT placement, 2026-10-21).
bool CSpacePirate::StartAttack(CStateManager& mgr, const CTriggerData& data) const {
  if (x8f8_31_mayStartAttack) {
    const_cast< CSpacePirate* >(this)->x8f8_31_mayStartAttack = false;
    return true;
  }
  return false;
}

// @ fn_72_6328
// Retail is `lbz 0x8f5; extrwi 1, 25` (x8f5_25_breakAttack, data flag 0x400). Ambushing's trigger list
// evaluates it every frame (gcov 2026-10-21, a forced `fsm Ambushing`).
bool CSpacePirate::BreakAttack(CStateManager& mgr, const CTriggerData& data) const {
  return x8f5_25_breakAttack;
}

// Dead in shipped data: neither SpacePirate FSM on the disc (AFSM 0x5652a12f, 0x7b49526b) lists this
// trigger (disc scan of every PIRT placement, 2026-10-21).
bool CSpacePirate::LostInterest(CStateManager& mgr, const CTriggerData& data) const {
  return x8f4_29_onlyAttackInRange && mAttackRemTime < 1.5f;
}

// fn_72_602C. Unmarked: no state the 208 cycle reaches lists this trigger (2026-10-20 run).
// Retail: the AiWaypoint object list (mgr+0x838) is scanned for the jump point this pirate
// should bounce off. A candidate has to be active (0x20 bit 7), free (fn_8014FFF0 with this
// pirate's id), of type 0 (0x15c), have a next waypoint (0x182 != kInvalidUniqueId) and be in
// this area (0x4 == the pirate's). It then has to be closer than the best candidate so far and
// its forward (0x28/0x38/0x48 = the transform's forward) has to point along the way to it.
// The score is the squared distance to the jump point plus the squared distance from the
// pirate's destination (0x334) to the next waypoint, with the height part of that second
// distance weighted 9 times (the asm adds 9*dz^2 on top of the plain dz^2 of the square).
// The next waypoint's forward has to point along the way to the destination as well.
// The path find search then has to connect the pirate to the jump point, and the next waypoint
// to the destination; the latter failing costs 1000 but does not rule the candidate out. The
// lowest score wins, and the scan stops as soon as a candidate that reaches the destination
// beats the best score so far.
// The winner's current waypoint (0x180) decides: only if it still resolves does the pirate get
// the jump point's translation as its destination, its id (0xb14), its jump apex (0x158 ->
// 0xaf8) and the current waypoint's translation (0xafc).
// Dead in shipped data: neither SpacePirate FSM on the disc (AFSM 0x5652a12f, 0x7b49526b) lists this
// trigger (disc scan of every PIRT placement, 2026-10-21).
bool CSpacePirate::BounceFind(CStateManager& mgr, const CTriggerData& data) const {
  CSpacePirate* self = const_cast< CSpacePirate* >(this);
  bool result = false;
  float bestScore = FLT_MAX;
  const CScriptAiJumpPoint* best = nullptr;
  const CObjectList& list = mgr.GetObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CScriptAiJumpPoint* jumpPoint = TCastToConstPtr< CScriptAiJumpPoint >(list[i]);
    if (jumpPoint == nullptr || !jumpPoint->GetActive() || jumpPoint->GetInUse(GetUniqueId()) ||
        jumpPoint->GetType() != 0 || jumpPoint->GetJumpTarget() == kInvalidUniqueId ||
        jumpPoint->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    const CVector3f toJump = jumpPoint->GetTranslation() - GetTranslation();
    const float distSq = toJump.MagSquared();
    if (distSq >= bestScore) {
      continue;
    }
    if (CVector3f::Dot(jumpPoint->GetTransform().GetForward(), toJump) <= 0.f) {
      continue;
    }
    const CScriptWaypoint* waypoint =
        TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(jumpPoint->GetJumpTarget()));
    if (waypoint == nullptr) {
      continue;
    }
    const CVector3f toDest = mDestPos - waypoint->GetTranslation();
    float score = distSq + toDest.MagSquared() + 9.f * toDest.GetZ() * toDest.GetZ();
    if (score >= bestScore) {
      continue;
    }
    if (CVector3f::Dot(waypoint->GetTransform().GetForward(), toDest) <= 0.f) {
      continue;
    }
    if (self->GetSearchPath()->PathExists(GetTranslation(), jumpPoint->GetTranslation()) !=
        CPathFindSearch::kR_Success) {
      continue;
    }
    bool reachesDest = false;
    if (self->GetSearchPath()->PathExists(waypoint->GetTranslation(), mDestPos) !=
        CPathFindSearch::kR_Success) {
      score += 1000.f;
    } else {
      reachesDest = true;
    }
    if (score >= bestScore) {
      continue;
    }
    bestScore = score;
    best = jumpPoint;
    if (reachesDest) {
      break;
    }
  }
  if (best != nullptr) {
    const CScriptWaypoint* waypoint =
        TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(best->GetJumpPoint()));
    if (waypoint != nullptr) {
      self->SetDestPos(best->GetTranslation());
      result = true;
      self->xb14_jumpPoint = best->GetUniqueId();
      self->xaf8_jumpHeight = best->GetJumpApex();
      self->xafc_patrolDestPos = waypoint->GetTranslation();
    }
  }
  TraceCall(*this, "BounceFind trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_55B8
// The 208 cycle does evaluate this trigger (2980 calls in the 2026-10-20 run), but it is always
// false there (the pirates stand on the ground), so only the body-state test is proven.
// Retail: false while the body is in the jump state (the body controller's state word at +0x37c
// compared with 0xd = pas::kAS_Jump); otherwise the opposite of IsOnGround (the vtable slot
// 0x110 call, whose result is tested on the top byte).
bool CSpacePirate::OffLine(CStateManager& mgr, const CTriggerData& data) const {
  bool result = false;
  if (GetBodyController()->GetCurrentStateId() != pas::kAS_Jump && !IsOnGround()) {
    result = true;
  }
  TraceCall(*this, "OffLine trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_558C. no state the 208 cycle reaches lists this trigger (2026-10-20 run).
// Retail: the vtable slot 0x110 call (IsOnGround) and nothing else; its result is returned
// unmasked.
bool CSpacePirate::Landed(CStateManager& mgr, const CTriggerData& data) const {
  const bool result = IsOnGround();
  TraceCall(*this, "Landed trigger", result ? 1 : 0);
  return result;
}

// fn_72_686C (0x30): true unless the 0x8f4 byte's bit 28 (mask 0x08, the data flag 0x10; asm
// 0x6874 `extrwi.`) is set; with it set, true once the hold-position time (0xbac) is above 6
// (rodata BE8 = 6.0, asm 0x6880-0x688C `fcmpo` + `ble`).
// All three arms ran (tools/cov, 2026-10-21): bit clear (06_sand_alamofront L2, 400 hits), bit set
// with the hold time <= 6 and > 6 (08_sand_casualpirates L3 0036013d/003600fa, console `fsm Attack`/
// `fsm Lurk` and 12 s held, then `fsm JumpBack`: 246 hits with the time > 6).
// @ fn_72_686C
bool CSpacePirate::ShouldJumpBack(CStateManager& mgr, const CTriggerData& data) const {
  return !x8f4_28_noShuffleCloseCheck || xbac_holdPositionTime > 6.f;
}

// @ fn_72_52F8. no state the 208 cycle reaches lists this trigger (2026-10-20 run).
// Retail: the leash timer (0xbb0) against the trigger's float argument. `fcmpo cr0` + `mfcr` +
// `extrwi r3, r0, 1, 1` takes CR bit 1, cr0's GT bit (LT is bit 0, EQ bit 2), so the trigger is
// "timer > argument", as in MP1.
bool CSpacePirate::Leash(CStateManager& mgr, const CTriggerData& data) const {
  const bool result = xbb0_leashTimer > data.GetFloat();
  TraceCall(*this, "Leash trigger", result ? 1 : 0);
  return result;
}

// @ fn_72_7804 (0xC): the 0x8f4 byte's bit 24 (mask 0x80, the data flag 0x1) returned unmasked
// (asm 0x7808 `extrwi`). One bit test, so every evaluation does the same work.
bool CSpacePirate::IsAmbushing(CStateManager& mgr, const CTriggerData& data) const {
  TraceCall(*this, "IsAmbushing trigger", x8f4_24_pendingAmbush ? 1 : 0);
  return x8f4_24_pendingAmbush;
}

// Retail fn_72_77D0 (0x34): the warp point (0xc04) has to be -1 and the 0x8f6 byte's bit 24 (mask
// 0x80, the data flag 0x40000 - the trooper) and the 0x8fa byte's bit 26 (mask 0x20) have to be set
// (asm 0x77D8-0x77F8). Bit 26 is not a data flag: the kSM_Activate handler sets it at run time for a
// trooper (fn_72_E498, asm 0xE38C). The trace's argument is the arm: 1 = the warp point is not -1,
// 2 = not a trooper, 3 = the trooper has not been activated, 4 = all three hold.
// All four arms ran (tools/cov, 2026-10-21): 1 in 07_sand_alamoportalobservation L3 (a trooper that
// warped out, 0xc04 = 1), 2 for the non-troopers, 3 and 4 for the troopers before and after their
// activation message.
// @ fn_72_77D0
bool CSpacePirate::ShouldWarpIn(CStateManager& mgr, const CTriggerData& data) const {
  const int arm = xc04_warpPoint != -1 ? 1 : (!x8f6_24_trooper ? 2 : (!x8fa_26_ ? 3 : 4));
  TraceCall(*this, "ShouldWarpIn trigger", arm);
  return arm == 4;
}

// @ fn_72_779C (0x2C): the launcher block's +0x00 is 1 - the loadout is the grenade launcher -
// and the attack timer has run out (rodata B9C = 0, compared with the `cror lt/eq` idiom, so `<= 0`).
bool CSpacePirate::ShouldLaunchGrenade(CStateManager& mgr, const CTriggerData& data) const {
  return mData.mEquippedWeapon == 1 && mAttackRemTime <= 0.f;
}

// @ fn_72_76D0 (0xD4): a switch on the same launcher word. Unarmed (0) is always in range,
// anything but 0/1 never is, and the grenade launcher (1) wants a live attack target whose
// squared 3-D distance to the pirate falls inside [minAttackDist, maxAttackDist] squared
// (data +0x12c/+0x130, rodata-free: both bounds are inclusive `cror ge`/`cror le` tests).
bool CSpacePirate::InProjectileRange(CStateManager& mgr, const CTriggerData& data) const {
  switch (mData.mEquippedWeapon) {
  case 0:
    return true;
  case 1: {
    const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mAttackTarget));
    if (target == nullptr) {
      return false;
    }
    const float distSq = (target->GetTranslation() - GetTranslation()).MagSquared();
    return distSq >= mData.mGrenadeMinAttackDist * mData.mGrenadeMinAttackDist &&
           distSq <= mData.mGrenadeMaxAttackDist * mData.mGrenadeMaxAttackDist;
  }
  default:
    return false;
  }
}

// @ fn_72_7810 (0x54): the pirate has an attack pattern when its ATTK/FOLW connection
// resolves to a live object (asm: GetConnectedObject, then the `id != kInvalidUniqueId` idiom).
bool CSpacePirate::HasAttackPattern(CStateManager& mgr, const CTriggerData& data) const {
  return GetConnectedObject(mgr, kSS_Attack, kSM_Follow) != kInvalidUniqueId;
}
