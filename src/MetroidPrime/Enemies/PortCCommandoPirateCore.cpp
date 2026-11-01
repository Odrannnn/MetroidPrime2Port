// CCommandoPirate (REL module 9): the core states and overrides the first slice left as
// stand-ins - Listen, CoverBlown, the melee and E-grenade attacks, the jump-point boost, the
// warp-in, the cover states and GetUp - plus the two one-line vtable overrides the base's defaults
// stood in for. Every compare, store, mask and rodata constant carries its guest offset; the
// guest's member map is in docs/module-analysis/CommandoPirate.md.
#include "MetroidPrime/Enemies/CCommandoPirate.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Collision/CMaterialList.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CVector2f.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Particles/CGenDescription.hpp"
#include "Kyoto/TToken.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/BodyState/CBodyState.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CEntityInfo.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CRELFileToken.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CBouncyGrenade.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAiJumpPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCoverPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "rstl/reserved_vector.hpp"
#include "rstl/vector.hpp"

#include <float.h>
#include <stdio.h>

// lbl_9_bss_38: the live E-grenades (the ctor's increment at 0x0000F4E4..0x0000F4EC, the dtor's
// decrement in fn_9_F290), defined in PortCCommandoPirate.cpp where fn_9_662C and ShouldFireEGrenade
// read it.
extern int sEgrenadeObjectCount;

namespace {

// lbl_9_rodata_408: the melee attack table, three rows of 0xc bytes (the mulli at 0x0000A5E0):
// the severity (an int, the lwzx at 0x0000A5F8), the widest angle to the target the row allows and
// its weight in the random pick (fn_9_3480).
struct SMeleeAttack {
  int mSeverity;
  float mAngle;
  float mWeight;
};
const SMeleeAttack skMeleeAttacks[3] = {
    {0, 1.5707964f, 50.f},
    {1, 1.5707964f, 25.f},
    {2, 0.5235988f, 25.f},
};

}  // namespace

// fn_9_3480 (0x1f8 bytes): the melee-attack picker. Without the target's aim position (fn_9_3CBC
// with 0.5f, rodata_440, 0x000034D8..0x000034EC) it answers -1 (0x000034D4). Otherwise the angle
// between the flat aim delta (aim minus the translation, z = 0.f rodata_494, 0x00003528..0x0000354C)
// and the forward (0x000034F0..0x00003520) picks the rows: each row whose angle is >= it adds its
// weight to the sum (0x0000356C..0x000035B4); the sum times CRandom16::Float (0x000035BC) walks the
// same rows in order and answers the first whose weight is >= what is left, taking each passed
// weight off (0x000035C0..0x00003644). Nothing found answers -1.
// Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): every arm; no target
// forced on every 3rd call, the rows picked from eight stand-offs.
// @ fn_9_3480
int CCommandoPirate::x3480_PickMeleeAttack(CStateManager& mgr) const {
  int result = -1;                                                     // 0x000034D4
  CVector3f aim = CVector3f::Zero();                                   // 0x000034A8..0x000034E0
  if (!x3CBC_(mgr, aim, 0.5f)) {                                       // rodata_440, 0x000034E4..0x000034EC
    return result;
  }
  const CVector3f forward = GetTransform().GetForward();               // +0x28/+0x38/+0x48, 0x000034F0..0x00003520
  const CVector3f translation = GetTranslation();
  const CVector3f delta(aim.GetX() - translation.GetX(), aim.GetY() - translation.GetY(),
                        0.f);                                          // rodata_494, 0x00003528..0x0000354C
  const float angle = CVector3f::GetAngleDiff(delta, forward);         // 0x00003550
  float sum = 0.f;                                                     // rodata_494, 0x00003568
  for (int i = 0; i < 3; ++i) {                                        // unrolled, 0x0000356C..0x000035B4
    if (skMeleeAttacks[i].mAngle >= angle) {
      sum += skMeleeAttacks[i].mWeight;
    }
  }
  float pick = sum * mgr.Random()->Float();                            // this+0x16e4, 0x000035B8..0x000035C4
  for (int i = 0; i < 3; ++i) {                                        // unrolled, 0x000035C0..0x00003644
    if (skMeleeAttacks[i].mAngle >= angle) {
      if (skMeleeAttacks[i].mWeight >= pick) {
        return i;                                                      // 0x000035E4 / 0x00003614 / 0x00003644
      }
      pick -= skMeleeAttacks[i].mWeight;                               // 0x000035EC / 0x0000361C
    }
  }
  return result;
}

// @ fn_9_68
// fn_9_68 (vtable slot 0xc8, 0x8 bytes): `lbz r3, 0x44f(r3)` - the colour's alpha byte. One arm.
// Run: cov (specs/commandopirate.py, drivers/commandopirate_core.py), area 59 + swamp: 50+50.
uchar CCommandoPirate::GetModelAlphau8(const CStateManager& mgr) const {
  return mColor.GetAlphau8();  // 0x00000068
}

// @ fn_9_AC
// fn_9_AC (vtable slot 0xb4, 0x1c bytes): the three floats at this+0x54..0x5C (the position) are
// copied out (0x000000AC..0x000000C0). One arm; the team AI asks it. Run: cov, swamp_timber!: 50.
CVector3f CCommandoPirate::GetOrigin(const CStateManager& mgr, const CTeamAiRole& role,
                                     const CVector3f& aimPos) const {
  return GetTranslation();  // 0x000000AC..0x000000C0
}

// fn_9_BE4C (vtable slot 0xb0, 0xf8 bytes): what the actor hears. Nothing while it is not alive
// (the 0x40 bit of the byte at this+0x420, the extrwi at 0x0000BE54). Noise type 4 (0x0000BE5C)
// within 40 units (the squared distance against lbl_9_rodata_4E4 = 1600.f, 0x0000BEAC) sets the
// 0x80 bit of the byte at this+0xC63 (0x0000BEBC) and is heard. Type 0 (0x0000BE68) has to be
// within the hearing radius (this+0x81C, squared, 0x0000BF04) and, unless the detection height
// range (this+0x3D4) is 0.f (lbl_9_rodata_494, 0x0000BF1C), within that height (dz squared against
// its square, 0x0000BF24); it clears the timer at this+0xB60 (0x0000BF38) and is heard.
// @ fn_9_BE4C
// The branch table with the coverage evidence (the natural arms and the two forced under gdb):
// docs/module-analysis/CommandoPirate.md, section 6.1.
bool CCommandoPirate::Listen(CStateManager& mgr, const CVector3f& position, EListenNoiseType type) {
  bool heard = false;                                                  // 0x0000BE50
  if (!GetAlive()) {                                                   // 0x0000BE4C..0x0000BE58
    return heard;
  }
  switch (type) {                                                      // 0x0000BE5C..0x0000BE70
  case kLNT_PathObstruction: {                                         // 4, 0x0000BE5C..0x0000BE60
    if ((position - GetTranslation()).MagSquared() < 1600.f) {         // rodata_4E4, 0x0000BEAC..0x0000BEB0
      heard = true;                                                    // 0x0000BEB8
      xc63_ |= 0x80;                                                   // 0x0000BEB4..0x0000BEC0
    }
    break;
  }
  case kLNT_PlayerFire: {                                              // 0, 0x0000BE68..0x0000BE6C
    const CVector3f delta = position - GetTranslation();               // 0x0000BEC8..0x0000BEF0
    const float dzSq = delta.GetZ() * delta.GetZ();                    // 0x0000BEF8
    if (!(delta.GetX() * delta.GetX() + delta.GetY() * delta.GetY() + dzSq <
          mData.mHearingRadius * mData.mHearingRadius)) {              // this+0x81C, 0x0000BF04..0x0000BF08 bge
      break;
    }
    if (mDetectionHeightRange != 0.f &&                                // this+0x3D4, rodata_494, 0x0000BF1C..0x0000BF20
        !(dzSq < mDetectionHeightRange * mDetectionHeightRange)) {     // 0x0000BF24..0x0000BF28 bge
      break;
    }
    heard = true;                                                      // 0x0000BF30
    xb60_ = 0.f;                                                       // rodata_494, 0x0000BF38
    break;
  }
  default:                                                             // 1..3 and >= 5, 0x0000BE64/0x0000BE70
    break;
  }
  return heard;                                                        // 0x0000BF3C
}

// fn_9_AFD4 (0xe0 bytes): the CoverBlown trigger. The target's aim position (fn_9_3CBC with
// 1.f, lbl_9_rodata_43C, from a zero vector, 0x0000AFDC..0x0000B020) has to exist and be farther
// than the minimum attack range (this+0x3DC, both squared, 0x0000B064..0x0000B068); then the
// cover point the actor holds (this+0xBD2, 0x0000B080) decides with CScriptCoverPoint::Blown
// (0x0000B090). Every other arm answers true (0x0000B098).
// Run: cov 2026-10-22 (cp6: tools/drivers/commandopirate_cp6.py, 05_swamp_timber!): every arm; no target,
// the target inside the minimum range, no cover point, and Blown both ways forced (xbca/xbd2 set
// to other cover points and waypoints, the actor placed beside them).
// @ fn_9_AFD4
bool CCommandoPirate::CoverBlown(CStateManager& mgr, const CTriggerData& data) const {
  CVector3f aim = CVector3f::Zero();                                   // 0x0000AFDC..0x0000B01C
  if (x3CBC_(mgr, aim, 1.f) &&                                         // rodata_43C, 0x0000B020..0x0000B028
      (aim - GetTranslation()).MagSquared() > mMinAttackRange * mMinAttackRange) {  // this+0x3DC, 0x0000B064..0x0000B068
    if (const CScriptCoverPoint* cover = GetCoverPoint(mgr, xbd2_)) {  // 0x0000B06C..0x0000B088
      return cover->Blown(aim);                                        // 0x0000B08C..0x0000B094
    }
  }
  return true;                                                         // 0x0000B098
}

// fn_9_A4BC (0x290 bytes): the MeleeAttack state.
// Activate: the picker fn_9_3480 chooses the attack row (this+0xC54, 0x0000A508); the target's aim
// position (fn_9_3CBC into this+0xB94 with 0.f, 0x0000A51C) has to exist and the row must not be
// -1 (0x0000A52C). With no team (this+0xBC4 == kInvalidUniqueId, 0x0000A540) or a team that lets
// the attack start (StartAttack kAT_Melee, 0x0000A56C) the animation state goes ready
// (0x0000A580), the locomotion is combat (3, 0x0000A588) and this+0xC65 takes 0x20 (0x0000A594).
// Otherwise the state is over (3, 0x0000A5A4 / 0x0000A5B0).
// Update: once the melee command may be issued (kAS_MeleeAttack = 7, 0x0000A5C0) the command (id
// 5) goes out with the row's severity (0x0000A5F8..0x0000A640) and the blade's collision manager
// is switched on (0x0000A698); otherwise the body's target vector (bc+0x34) is the aim position
// minus the position (0x0000A6C8..0x0000A6D0).
// Deactivate: this+0xC65 loses 0x20 (0x0000A6E8), the melee cooldown this+0xB7C is 0.f
// (0x0000A700), the animation state goes not ready (0x0000A704), the team hears the attack ended
// (EndAttack kAT_Melee, no role clear, 0x0000A71C) and the blade goes off (0x0000A72C).
// Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): every guest arm;
// StartAttack refused by two commandos entering in one tick. Open: the host blade guards, the default.
// @ fn_9_A4BC
void CCommandoPirate::MeleeAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate: {                                           // 0x0000A500
    xc54_ = x3480_PickMeleeAttack(mgr);                                // 0x0000A500..0x0000A508
    if (!x3CBC_(mgr, xb94_, 0.f) || xc54_ == -1) {                     // rodata_494, 0x0000A51C..0x0000A530
      mAnimationState.SetState(CAnimationState::kAS_Over);             // 0x0000A5AC..0x0000A5B0
      break;
    }
    if (xbc4_teamId != kInvalidUniqueId &&                             // 0x0000A538..0x0000A544
        !CScriptTeamAiMgr::StartAttack(CScriptTeamAiMgr::kAT_Melee, mgr, xbc4_teamId,
                                       GetUniqueId())) {               // 0x0000A548..0x0000A574
      mAnimationState.SetState(CAnimationState::kAS_Over);             // 0x0000A5A0..0x0000A5A4
      break;
    }
    mAnimationState.SetState(CAnimationState::kAS_Ready);              // 0x0000A578..0x0000A580
    BodyController()->SetLocomotionType(pas::kLT_Combat);              // 3, 0x0000A57C..0x0000A588
    xc65_ |= 0x20;                                                     // 0x0000A58C..0x0000A598
    break;
  }
  case kStateMsg_Update: {                                             // 0x0000A5B8
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_MeleeAttack)) {  // 7, 0x0000A5BC..0x0000A5CC
      BodyController()->CommandMgr().DeliverCmd(CBCMeleeAttackCmd(
          static_cast< pas::ESeverity >(skMeleeAttacks[xc54_].mSeverity)));  // rodata_408, 0x0000A5D0..0x0000A640
      if (mCollisionBlade.get() != nullptr) {  // host guard: the guest calls through (0x0000A690)
        mCollisionBlade->SetActive(mgr, true);                         // 0x0000A668..0x0000A698
      }
    } else {
      BodyController()->CommandMgr().SetTargetVector(xb94_ - GetTranslation());  // 0x0000A6A0..0x0000A6D0
    }
    break;
  }
  case kStateMsg_Deactivate: {                                         // 0x0000A6D8
    xc65_ &= ~0x20;                                                    // 0x0000A6D8..0x0000A6F0
    xb7c_ = 0.f;                                                       // rodata_494, 0x0000A700
    mAnimationState.SetState(CAnimationState::kAS_NotReady);           // 0x0000A704
    CScriptTeamAiMgr::EndAttack(CScriptTeamAiMgr::kAT_Melee, mgr, xbc4_teamId, GetUniqueId(),
                                false);                                // 0x0000A6F8..0x0000A71C
    if (mCollisionBlade.get() != nullptr) {  // host guard: the guest calls through (0x0000A720)
      mCollisionBlade->SetActive(mgr, false);                          // 0x0000A728..0x0000A72C
    }
    break;
  }
  default:
    break;
  }
}

// fn_9_A2EC (0x174 bytes): the EGrenadeAttack state.
// Activate: ready (0x0000A330) and this+0xC65 takes 0x01 (0x0000A338).
// Update: once the projectile command may be issued (kAS_ProjectileAttack = 0x12, 0x0000A34C)
// and the target's aim position exists (fn_9_3CBC into this+0xB94 with 0.f, 0x0000A374), the
// projectile attack (id 6) goes out with severity 2 (0x0000A3A0), the aim position and no blend
// (0x0000A3A4, 0x0000A3D4).
// Deactivate: not ready (0x0000A430), the timer this+0xB74 is 0.f (0x0000A438) and this+0xC65
// loses 0x01 (0x0000A440).
// Run: cov 2026-10-22 (cp6: tools/drivers/commandopirate_cp6.py, 05_swamp_timber!): every arm but the
// dead default; the aim position forced missing (xbca invalid) and the command refused (state
// not ready).
// @ fn_9_A2EC
void CCommandoPirate::EGrenadeAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:                                             // 0x0000A32C
    mAnimationState.SetState(CAnimationState::kAS_Ready);              // 0x0000A330
    xc65_ |= 0x01;                                                     // 0x0000A334..0x0000A33C
    break;
  case kStateMsg_Update:                                               // 0x0000A344
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_ProjectileAttack) &&  // 0x12, 0x0000A348..0x0000A358
        x3CBC_(mgr, xb94_, 0.f)) {                                     // rodata_494, 0x0000A35C..0x0000A37C
      BodyController()->CommandMgr().DeliverCmd(
          CBCProjectileAttackCmd(pas::kS_Two, xb94_, false));          // 0x0000A380..0x0000A41C
    }
    break;
  case kStateMsg_Deactivate:                                           // 0x0000A428
    mAnimationState.SetState(CAnimationState::kAS_NotReady);           // 0x0000A430
    xb74_ = 0.f;                                                       // rodata_494, 0x0000A438
    xc65_ &= ~0x01;                                                    // 0x0000A43C..0x0000A444
    break;
  default:
    break;
  }
}

// fn_9_A110 (0x1dc bytes): the PostEGrenadeAttack state.
// Activate: ready when the target (this+0xBCA) exists (0x0000A168..0x0000A178), over otherwise
// (0x0000A184).
// Update: over once the state's time reaches the grenade's post-attack pause (this+0x884, the
// fcmpo/cror at 0x0000A1A4..0x0000A1AC). Otherwise the target's aim position (fn_9_3CBC into
// this+0xB94 with 0.5f, lbl_9_rodata_440, 0x0000A1D4) has to exist - over when it does not
// (0x0000A2BC); the actor turns to it with a locomotion command (the normalized direction, weight
// 1.f lbl_9_rodata_43C, 0x0000A2B0) when the direction is safe (0x0000A214) and more than 20
// degrees off (lbl_9_rodata_4B4, 0x0000A24C..0x0000A250).
// Deactivate: not ready (0x0000A2C8).
// Run: cov 2026-10-22 (cp6: tools/drivers/commandopirate_cp6.py, 05_swamp_timber!): every arm but the
// dead default; the pause forced over (fsmtime), no target / no aim position (xbca invalid),
// the yaw swept for the turn / no turn, and the target on the actor for the unsafe delta.
// @ fn_9_A110
void CCommandoPirate::PostEGrenadeAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:                                             // 0x0000A158
    if (mgr.GetObjectById(xbca_) != nullptr) {                         // 0x0000A158..0x0000A170
      mAnimationState.SetState(CAnimationState::kAS_Ready);            // 0x0000A178
    } else {
      mAnimationState.SetState(CAnimationState::kAS_Over);             // 0x0000A184
    }
    break;
  case kStateMsg_Update: {                                             // 0x0000A18C
    if (StateMachineState().GetTime() >= mData.mGrenade.mGrenadePostAttackPause) {  // this+0x884, 0x0000A1A0..0x0000A1AC
      mAnimationState.SetState(CAnimationState::kAS_Over);             // 0x0000A1B4
      break;
    }
    if (!x3CBC_(mgr, xb94_, 0.5f)) {                                   // rodata_440, 0x0000A1BC..0x0000A1DC
      mAnimationState.SetState(CAnimationState::kAS_Over);             // 0x0000A2BC
      break;
    }
    const CVector3f delta = xb94_ - GetTranslation();                  // 0x0000A1E0..0x0000A210
    if (!delta.IsMagnitudeSafe()) {                                    // 0x0000A214..0x0000A21C
      break;
    }
    if (CVector3f::GetAngleDiff(GetTransform().GetForward(), delta) > 0.34906584f) {  // rodata_4B4, 0x0000A240..0x0000A250
      BodyController()->CommandMgr().DeliverCmd(
          CBCLocomotionCmd(CVector3f::Zero(), delta.AsNormalized(), 1.f));  // rodata_43C, 0x0000A254..0x0000A2B0
    }
    break;
  }
  case kStateMsg_Deactivate:                                           // 0x0000A2C4
    mAnimationState.SetState(CAnimationState::kAS_NotReady);           // 0x0000A2C8
    break;
  default:
    break;
  }
}

// fn_9_9BB4 (0x1fc bytes): the JetBoost state - the jump from the jump point JumpPointFind chose.
// Activate: over first (0x00009C00); the jump point (this+0xBD0, 0x00009C10..0x00009C1C) and
// its waypoint (the jump point's +0x180, 0x00009C34..0x00009C40) both have to resolve. Then the
// waypoint's position is the jump target (this+0xBF0, 0x00009C4C..0x00009C5C), the jump point's
// forward the direction (this+0xBA0, 0x00009C6C..0x00009C74), its apex this+0xBEC (the +0x158,
// 0x00009C7C) and the state is ready (0x00009C80).
// Update: while ready (0x00009C90) and the facing is more than 20 degrees off that direction
// (lbl_9_rodata_4B4, 0x00009CC4..0x00009CC8) the actor turns (a locomotion command, weight 1.f,
// 0x00009D20) and does not jump (0x00009CD8). Otherwise, once the jump may be issued (kAS_Jump =
// 0xd, 0x00009D34) the jump command goes out (fn_9_9EA8 builds it: the target, type 0, state 0,
// variant 0, facing flags 2, 0x00009D44..0x00009D5C; fn_9_9DB0 delivers it, 0x00009D6C).
// Deactivate: not ready (0x00009D90).
// @ fn_9_9BB4
void CCommandoPirate::JetBoost(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate: {                                           // 0x00009BF8
    mAnimationState.SetState(CAnimationState::kAS_Over);               // 0x00009BF8..0x00009C00
    const CScriptAiJumpPoint* jumpPoint =
        TCastToConstPtr< CScriptAiJumpPoint >(mgr.GetObjectById(xbd0_));  // 0x00009C04..0x00009C1C
    if (jumpPoint == nullptr) {
      break;
    }
    const CScriptWaypoint* waypoint =
        TCastToConstPtr< CScriptWaypoint >(mgr.GetObjectById(jumpPoint->GetJumpPoint()));  // 0x00009C20..0x00009C40
    if (waypoint == nullptr) {
      break;
    }
    xbf0_ = waypoint->GetTranslation();                                // 0x00009C44..0x00009C5C
    xba0_ = jumpPoint->GetTransform().GetForward();                    // 0x00009C60..0x00009C74
    xbec_ = jumpPoint->GetJumpApex();                                  // 0x00009C78..0x00009C7C
    mAnimationState.SetState(CAnimationState::kAS_Ready);              // 0x00009C48, 0x00009C80
    break;
  }
  case kStateMsg_Update: {                                             // 0x00009C88
    bool mayJump = true;                                               // 0x00009C8C
    if (mAnimationState.GetState() == CAnimationState::kAS_Ready &&   // 0x00009C88..0x00009C94
        CVector3f::GetAngleDiff(GetTransform().GetForward(), xba0_) > 0.34906584f) {  // rodata_4B4, 0x00009C98..0x00009CC8
      mayJump = false;                                                 // 0x00009CD8
      BodyController()->CommandMgr().DeliverCmd(
          CBCLocomotionCmd(CVector3f::Zero(), xba0_, 1.f));            // rodata_43C, 0x00009CCC..0x00009D20
    }
    if (mayJump &&                                                     // 0x00009D24..0x00009D28
        mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Jump)) {  // 0xd, 0x00009D2C..0x00009D40
      BodyController()->CommandMgr().DeliverCmd(
          CBCJumpCmd(xbf0_, pas::kJT_Normal, pas::kJS_IntoJump, 0,
                     CBCJumpCmd::kFF_AmbushJump));                     // 0x00009D44..0x00009D6C
    }
    break;
  }
  case kStateMsg_Deactivate:                                           // 0x00009D8C
    mAnimationState.SetState(CAnimationState::kAS_NotReady);           // 0x00009D90
    break;
  default:
    break;
  }
}

// fn_9_9064 (0x528 bytes): the WarpIn state - the commando appears at one of its GRNT/FOLW
// waypoints and fades in while the generate animation plays.
// Activate: ready (0x000090C0), alpha 0 (the (u8) cast of 0.f, 0x000090C4..0x000090E4), the alpha
// delta 0.f (0x000090E8), the state word this+0x920 = 0 (0x000090EC), this+0xC65 loses 0x40
// (0x000090F4) and the actor stops being character, solid, target and orbit (materials 0x21,
// 0x3b, 0x28, 0x29, 0x000090FC).
// Update, word 0 (0x00009120): the connections with state GRNT (0x47524E54, 0x0000914C) and
// message FOLW (0x464F4C57, 0x0000915C) that resolve to waypoints (0x00009194..0x0000919C) are
// collected; when there are any (0x000091D8) one is picked (Range(0, n - 1), 0x000091EC) and the
// actor takes its position (0x00009204) and orientation (0x00009218..0x00009234). Then a box of
// +-10 (lbl_9_rodata_444) around the actor (0x000092D4..0x00009308) and the filter "solid, player,
// character" (0x3b, 0x20, 0x21, 0x00009240..0x00009290) build a near list (0x00009320); with no
// collision (0x00009348..0x0000934C) the word becomes 1 (0x00009358) and the actor is
// character, solid and target again (0x0000936C).
// Update, word 1 (0x00009380): once the generate command may be issued (kAS_Generate = 0xc,
// 0x00009388) the command (id 0xf, type 0, anim -1, flags clear) goes out (0x00009398..0x00009414);
// otherwise while the body is generating (0x00009474) the first frame takes this+0xC65's 0x40
// (0x0000948C) and keeps the remaining time (this+0xC58, 0x0000949C), and later frames, while that
// time is above the epsilon (lbl_9_rodata_4C4, 0x000094B0), set the alpha to (u8)(255.f *
// max(0.f, 1.f - remaining / kept)) (lbl_9_rodata_43C, 494, 4DC, 0x000094C8..0x00009500).
// Deactivate: not ready (0x00009510), the four materials back (0x00009524), alpha (u8)255.f
// (lbl_9_rodata_4DC, 0x0000952C..0x0000954C), the alpha delta 0.f (0x00009550), the word -1
// (0x00009554) and this+0xC63 loses 0x20 (0x0000955C).
// Run: cov 2026-10-22 (cp6: tools/drivers/commandopirate_cp6.py, 05_swamp_timber!): every arm but the
// dead default and the waypoint pick: the collision forced (placed on the player), x920 and the
// kept time (xc58) forced, the body state forced generating (bodystate 12). The GRNT/FOLW
// waypoint arm (0x00009144..0x00009234) is data-dead: no commando in the shipped data has a
// GRNT connection (tools/scan_commando_pattern.py --all: 9 CMDO, only ACTV/PLAY, ATTK/FOLW,
// MDFY/FOLW, RATL/ZERO and RATL/INCR).
// @ fn_9_9064
void CCommandoPirate::WarpIn(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:                                             // 0x000090B4
    mAnimationState.SetState(CAnimationState::kAS_Ready);              // 0x000090C0
    mColor.SetAlpha(static_cast< uchar >(0.f));                        // rodata_494, 0x000090C4..0x000090E4
    mAlphaDelta = 0.f;                                                 // 0x000090E8
    x920_ = 0;                                                         // 0x000090EC
    xc65_ &= ~0x40;                                                    // 0x000090F0..0x000090F8
    RemoveMaterial(kMT_Character, kMT_Solid, kMT_Target, kMT_Orbit, mgr);  // 0x000090D0..0x000090FC
    break;
  case kStateMsg_Update:                                               // 0x00009104
    if (x920_ == 0) {                                                  // 0x00009114..0x00009118
      rstl::vector< CScriptWaypoint* > points;                         // 0x00009120..0x00009138
      const rstl::vector< SConnection >& connections = GetConnectionList();
      for (rstl::vector< SConnection >::const_iterator it = connections.begin();
           it != connections.end(); ++it) {                            // 0x000091BC..0x000091D0
        if (it->state != kSS_GRNT || it->msg != kSM_Follow) {          // 0x00009148..0x00009160
          continue;
        }
        CScriptWaypoint* wp =
            TCastToPtr< CScriptWaypoint >(mgr.ObjectById(mgr.GetIdForScript(it->objId)));  // 0x00009178..0x0000919C
        if (wp != nullptr) {
          points.push_back(wp);                                        // 0x000091A0..0x000091B4
        }
      }
      if (!points.empty()) {                                           // 0x000091D8..0x000091DC
        CScriptWaypoint* wp = points[mgr.Random()->Range(0, int(points.size()) - 1)];  // 0x000091E4..0x000091F4
        SetTranslation(wp->GetTranslation());                          // 0x00009204
        SetTransform(
            CQuaternion::FromMatrix(wp->GetTransform()).BuildTransform4f(GetTranslation()));  // 0x00009218..0x00009234
      }
      const CVector3f extent(10.f, 10.f, 10.f);                        // rodata_444, 0x000092B4
      const CAABox bounds(GetTranslation() - extent, GetTranslation() + extent);  // 0x000092D4..0x00009308
      const CMaterialFilter filter =
          CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid, kMT_Player, kMT_Character));  // 0x00009238..0x000092C4
      rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
      mgr.BuildNearList(nearList, bounds, filter, this);               // 0x00009320
      if (!CGameCollision::DetectDynamicCollisionBoolean(*GetCollisionPrimitive(), GetTransform(),
                                                         nearList, mgr)) {  // 0x00009330..0x0000934C
        x920_ = 1;                                                     // 0x00009358
        AddMaterial(kMT_Character, kMT_Solid, kMT_Target, mgr);        // 0x00009360..0x0000936C
      }
    } else if (x920_ == 1) {                                           // 0x00009108..0x0000910C
      if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Generate)) {  // 0xc, 0x00009388..0x00009394
        BodyController()->CommandMgr().DeliverCmd(CBCGenerateCmd(pas::kGType_Zero, -1));  // 0x00009398..0x00009414
      } else if (BodyController()->GetCurrentStateId() == pas::kAS_Generate) {  // 0x0000946C..0x00009478
        if ((xc65_ & 0x40) == 0) {                                     // 0x0000947C..0x00009484
          xc65_ |= 0x40;                                               // 0x00009488..0x00009490
          xc58_ = BodyController()->GetAnimTimeRemaining();            // 0x00009498..0x0000949C
        } else if (xc58_ > 1.1920929e-07f) {                           // rodata_4C4, 0x000094A4..0x000094B4
          float f = 1.f - BodyController()->GetAnimTimeRemaining() / xc58_;  // rodata_43C, 0x000094B8..0x000094DC
          // bge (branch when lt is clear): taken on NaN too, so the clamp is !(0.f < f).
          if (!(0.f < f)) {                                            // rodata_494, 0x000094E4..0x000094F0
            f = 0.f;
          }
          mColor.SetAlpha(static_cast< uchar >(255.f * f));            // rodata_4DC, 0x000094F4..0x00009500
        }
      }
    }
    break;
  case kStateMsg_Deactivate:                                           // 0x00009508
    mAnimationState.SetState(CAnimationState::kAS_NotReady);           // 0x00009510
    AddMaterial(kMT_Character, kMT_Solid, kMT_Target, kMT_Orbit, mgr);  // 0x00009514..0x00009524
    mColor.SetAlpha(static_cast< uchar >(255.f));                      // rodata_4DC, 0x0000952C..0x0000954C
    mAlphaDelta = 0.f;                                                 // rodata_494, 0x00009550
    x920_ = -1;                                                        // 0x00009554
    xc63_ &= ~0x20;                                                    // 0x00009558..0x00009560
    break;
  default:
    break;
  }
}

// fn_9_7724 (0x148 bytes): the Crouch state.
// Activate: crouch locomotion (0, 0x00007768) and this+0xC66 loses 0x40 (0x00007774).
// Update: with a cover point (this+0xBD2, 0x0000778C..0x00007794) whose forward is more than 20
// degrees off the actor's (lbl_9_rodata_4B4, 0x000077D0..0x000077E0) the actor turns to it (a
// locomotion command, weight 1.f lbl_9_rodata_43C, 0x00007840).
// Deactivate: combat locomotion (3, 0x0000784C..0x00007850).
// Runs: cov2 (no cover point) and force2, 05_swamp_timber! (2026-10-21): every arm; the cover
// point forced (tools/cov/force.sh, FORCE "7724 Crouch cover": xbd2_ = u05c8 from the 3rd call),
// both sides of the 20-degree test with it (GetAngleDiff 22 calls, DeliverCmd 13).
// @ fn_9_7724
void CCommandoPirate::Crouch(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:                                             // 0x00007760
    BodyController()->SetLocomotionType(pas::kLT_Crouch);              // 0, 0x00007764..0x00007768
    xc66_ &= ~0x40;                                                    // 0x0000776C..0x00007778
    break;
  case kStateMsg_Update: {                                             // 0x00007780
    const CScriptCoverPoint* cover = GetCoverPoint(mgr, xbd2_);        // 0x00007780..0x00007794
    if (cover == nullptr) {
      break;
    }
    const CVector3f coverForward = cover->GetTransform().GetForward(); // 0x000077B8..0x000077CC
    if (CVector3f::GetAngleDiff(GetTransform().GetForward(), coverForward) > 0.34906584f) {  // rodata_4B4, 0x000077D0..0x000077E0
      BodyController()->CommandMgr().DeliverCmd(
          CBCLocomotionCmd(CVector3f::Zero(), coverForward, 1.f));     // rodata_43C, 0x000077E4..0x00007840
    }
    break;
  }
  case kStateMsg_Deactivate:                                           // 0x00007848
    BodyController()->SetLocomotionType(pas::kLT_Combat);              // 3, 0x0000784C..0x00007850
    break;
  default:
    break;
  }
}

// fn_9_6DBC (0x284 bytes): the Cover state.
// Activate: ready unless the body is in the cover state already (0x13, 0x00006E10..0x00006E1C);
// this+0xC66 loses 0x40 (0x00006E28).
// Update: with a cover point (this+0xBD2, 0x00006E40..0x00006E48) the side is its attack
// direction's 0x4 bit (the extrwi/neg/or/srwi at 0x00006E50..0x00006E68: right when set). Once
// the cover command may be issued (kAS_Cover = 0x13, 0x00006E5C..0x00006E74) it goes out (id
// 0x17) with the side, the cover's position and its negated forward (0x00006E78..0x00006EE8).
// Otherwise, while the body cannot shoot (CBodyState vslot 0x28, 0x00006F48..0x00006F68), the
// body's target vector (bc+0x34) is the negated cover forward (0x00006F6C..0x00006F9C) and the
// actor slides 5% of the way to the cover's position (lbl_9_rodata_470 = 0.05f, the local offset
// from TransposeRotate, 0x00006FA0..0x0000700C).
// Deactivate: not ready (0x00007018).
// Run: cov 2026-10-22 (cp6: tools/drivers/commandopirate_cp6.py, 05_swamp_timber!; side forced under
// gdb, tools/cov/specs/commandopirate_cp6.py): every arm but the dead default; already in the
// cover state (bodystate 19), no cover point, the command refused can / cannot shoot (bodystate
// 19 / 0). Every Swamp cover point has the same 0x4 bit, so the other side is forced: every
// other GetAttackDirection call from Cover answers with the bit flipped (return value only).
// @ fn_9_6DBC
void CCommandoPirate::Cover(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:                                             // 0x00006E08
    if (BodyController()->GetCurrentStateId() != pas::kAS_Cover) {     // 0x13, 0x00006E08..0x00006E14
      mAnimationState.SetState(CAnimationState::kAS_Ready);            // 0x00006E18..0x00006E1C
    }
    xc66_ &= ~0x40;                                                    // 0x00006E20..0x00006E2C
    break;
  case kStateMsg_Update: {                                             // 0x00006E34
    const CScriptCoverPoint* cover = GetCoverPoint(mgr, xbd2_);        // 0x00006E34..0x00006E48
    if (cover == nullptr) {
      break;
    }
    const pas::ECoverDirection side =
        (cover->GetAttackDirection() & 0x4) != 0 ? pas::kCD_Right : pas::kCD_Left;  // 0x00006E4C..0x00006E68
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Cover)) {  // 0x13, 0x00006E5C..0x00006E74
      BodyController()->CommandMgr().DeliverCmd(CBCCoverCmd(
          side, cover->GetTranslation(), -cover->GetTransform().GetForward()));  // 0x00006E78..0x00006EE8
    } else if (!BodyController()->BodyStateInfo().GetCurrentState()->CanShoot()) {  // 0x00006F48..0x00006F68
      BodyController()->CommandMgr().SetTargetVector(-cover->GetTransform().GetForward());  // 0x00006F6C..0x00006F9C
      MoveInOneFrameOR(0.05f * GetTransform().TransposeRotate(cover->GetTranslation() -
                                                               GetTranslation()),
                       dt);                                            // rodata_470, 0x00006FA0..0x0000700C
    }
    break;
  }
  case kStateMsg_Deactivate:                                           // 0x00007014
    mAnimationState.SetState(CAnimationState::kAS_NotReady);           // 0x00007018
    break;
  default:
    break;
  }
}

// fn_9_6C68 (0x154 bytes): the CoverAttack state.
// Activate: the lean-from-cover command (id 0xb, 0x00006CAC..0x00006CC8).
// Update: while the body cannot shoot (CBodyState vslot 0x28, 0x00006CDC..0x00006CFC) and a cover
// point exists (this+0xBD2, 0x00006D00..0x00006D1C) the actor slides 5% of the way to it
// (lbl_9_rodata_470 = 0.05f, 0x00006D20..0x00006D98).
// Deactivate: nothing (the guest's switch has no arm for it, 0x00006C98).
// Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): every arm; xbd2_
// forced valid/invalid on alternate half seconds.
// @ fn_9_6C68
void CCommandoPirate::CoverAttack(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:                                             // 0x00006CA8
    BodyController()->CommandMgr().DeliverCmd(kBSC_LeanFromCover);     // 0xb, 0x00006CA8..0x00006CC8
    break;
  case kStateMsg_Update: {                                             // 0x00006CDC
    if (BodyController()->BodyStateInfo().GetCurrentState()->CanShoot()) {  // 0x00006CDC..0x00006CFC
      break;
    }
    const CScriptCoverPoint* cover = GetCoverPoint(mgr, xbd2_);        // 0x00006D00..0x00006D1C
    if (cover == nullptr) {
      break;
    }
    MoveInOneFrameOR(0.05f * GetTransform().TransposeRotate(cover->GetTranslation() -
                                                             GetTranslation()),
                     dt);                                              // rodata_470, 0x00006D20..0x00006D98
    break;
  }
  default:
    break;
  }
}

// fn_9_6A40 (0x130 bytes): the GetUp state.
// Activate: this+0xC65 takes 0x10 (0x00006A80) and the state is ready (0x00006A88).
// Update: lying on the ground (2, the body's state at bc+0x37C, 0x00006A98) where the path search
// finds no source point (Search(pos, pos) == 2, 0x00006AAC..0x00006AB4), the actor dies
// (this+0x421's 0x10, the pending death, 0x00006AC0); otherwise once the getup command may be
// issued (kAS_Getup = 1, 0x00006AD4..0x00006AE0) it goes out (id 0, type 0, 0x00006AE4..0x00006B18).
// Deactivate: not ready (0x00006B4C) and this+0xC65 loses 0x10 (0x00006B54).
// Run: cov 2026-10-22 (cp6: tools/drivers/commandopirate_cp6.py, 05_swamp_timber!): every arm but the
// dead default and the death: standing / lying (bodystate 5 / 2), the command refused (not
// ready), lying off the mesh. The death arm is dead: GetUp searches from its own position to the
// same vector (r4 = r5 = this+0x54, 0x00006AA0..0x00006AA8), and the DOL's
// CPathFindSearch::Search(const CVector3f&, const CVector3f&) answers kR_Success first when
// close_enough(source, dest) (bl close_enough at 0x8013DD44, beq at 0x8013DD4C; the port's
// CPathFindSearch.cpp:57 the same), before any source-point lookup. Only a NaN position fails it.
// @ fn_9_6A40
void CCommandoPirate::GetUp(CStateManager& mgr, EStateMsg msg, float dt) {
  switch (msg) {
  case kStateMsg_Activate:                                             // 0x00006A78
    xc65_ |= 0x10;                                                     // 0x00006A78..0x00006A84
    mAnimationState.SetState(CAnimationState::kAS_Ready);              // 0x00006A88
    break;
  case kStateMsg_Update:                                               // 0x00006A90
    if (BodyController()->GetCurrentStateId() == pas::kAS_LieOnGround &&  // 2, 0x00006A90..0x00006A9C
        mPathFindSearch.Search(GetTranslation(), GetTranslation()) ==
            CPathFindSearch::kR_NoSourcePoint) {                       // 2, 0x00006AA0..0x00006AB4
      mPendingDeath = true;                                            // 0x00006AB8..0x00006AC4
    } else if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Getup)) {  // 1, 0x00006ACC..0x00006AE0
      BodyController()->CommandMgr().DeliverCmd(CBCGetupCmd(pas::kGetup_Zero));  // 0x00006AE4..0x00006B18
    }
    break;
  case kStateMsg_Deactivate:                                           // 0x00006B48
    mAnimationState.SetState(CAnimationState::kAS_NotReady);           // 0x00006B48..0x00006B4C
    xc65_ &= ~0x10;                                                    // 0x00006B50..0x00006B58
    break;
  default:
    break;
  }
}

// fn_9_F58C (0x110): the shield effects' maker (the ctor's calls at 0x0000E090 and 0x0000E09C).
// An invalid id gives null (the addis/cmplwi pair at 0x0000F598..0x0000F5A8); otherwise the PART
// token from the simple pool (the tag's 'PART' at 0x0000F5AC..0x0000F5B4) and a new CElementGen
// (0x338 bytes, 0x0000F618) on it with kMOT_Normal and kOSF_One (r5 = 0, r6 = 1 at 0x0000F648).
// Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): every arm; the
// invalid id forced.
// @ fn_9_F58C
CElementGen* CCommandoPirate::xF58C_CreateEffect(CAssetId id) {
  if (id == kInvalidAssetId) {
    return nullptr;  // 0x0000F680
  }
  TToken< CGenDescription > token = gpSimplePool->GetObj(SObjectTag('PART', id));  // 0x0000F5DC
  return rs_new CElementGen(token, CElementGen::kMOT_Normal, CElementGen::kOSF_One);  // 0x0000F650
}

namespace {

// The E-grenade's extra materials (lbl_9_bss_30, built by the module's static init in
// auto_fn_9_F9F8: the bits of lbl_9_data_7BC = 0x3b, kMT_Solid, and lbl_9_data_7C0 = 0x23,
// kMT_Projectile).
CMaterialList skEGrenadeMaterials(kMT_Solid, kMT_Projectile);
// lbl_9_bss_0/lbl_9_bss_C: the module's own REL name (rodata 0x909), the grenade's file token.
rstl::string skEGrenadeRELName = rstl::string_l("CommandoPirate.rel");

// The "Commando E-Grenade" (0x410 bytes, vtable lbl_9_data_AAC): a CBouncyGrenade whose Think
// (fn_9_F0F8) adds the EMP's static. Its other slots are CBouncyGrenade's own.
class CCommandoPirateEGrenade : public CBouncyGrenade {
public:
  // fn_9_F418: the base with the first 0x50 bytes of the module's grenade block as its
  // CBouncyGrenadeData (the guest passes the block itself, r30 at 0x0000F480), no explode
  // distance, an inverted box, no target, no homing (f2 = f3 = 0.f, rodata_494), flags 0 and
  // lbl_9_bss_30 (0x0000F45C..0x0000F494); then the whole block (fn_9_EC88 at 0x0000F4B0, this+0x390),
  // the EMP timer (this+0x400 = 0.f, 0x0000F4D0) and the REL token (this+0x404, 0x0000F4D4).
  // Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): every arm.
  // @ fn_9_F418
  CCommandoPirateEGrenade(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                          const CTransform4f& xf, const CModelData& modelData,
                          const CActorParameters& actorParams, TUniqueId owner,
                          const CCommandoPirateGrenadeData& data, float speed)
  : CBouncyGrenade(uid, name, info, xf, modelData, actorParams, owner, speed,
                   CBouncyGrenadeData(data.mGrenadeMass, data.mUnknownED086CE0, data.mGrenadeDamage,
                                      data.mGrenadeNumBounces, data.mGrenadeExplosion,
                                      data.mGrenadeExplosion2, data.mGrenadeTrail,
                                      data.mGrenadeEffect, data.mSoundGrenadeBounce,
                                      data.mSoundGrenadeExplode, data.mRadiusMin, data.mDamageMax,
                                      data.mRadiusMax, data.mDamageMin, (data.mFlags & 0x80) != 0),  // the bit fn_9_98C @0x00000A14 sets
                   0.f, CAABox::MakeMaxInvertedBox(), kInvalidUniqueId, 0.f, 0,
                   &skEGrenadeMaterials, nullptr)
  , mData(data)
  , mEMPTime(0.f)
  , mRelToken(skEGrenadeRELName, 0) {
    ++sEgrenadeObjectCount;
  }

  // fn_9_F290: the count, the token, the base.
  // Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): no branches.
  // @ fn_9_F290
  ~CCommandoPirateEGrenade() override { --sEgrenadeObjectCount; }

  // fn_9_F0F8: the base's grenade effects (UpdateGrenadeFX, 0x0000F128); once it has exploded
  // (the base's byte at guest +0x388, its 0x80 bit, 0x0000F12C..0x0000F134) the EMP timer runs
  // and every player within 20 units (rodata_46C) gets static by distance and the time left,
  // clamped to 0..1, for 0.2s (rodata_458); the grenade goes when the EMP time is up. The guest's
  // test of the time sits inside the exploded branch (0x0000F23C..0x0000F264).
  // Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): every arm;
  // the EMP duration forced to 0 on every 2nd call.
  // @ fn_9_F0F8
  void Think(float dt, CStateManager& mgr) override {
    UpdateGrenadeFX(dt, mgr);                                         // 0x0000F128
    if (!HasExploded()) {                                             // 0x0000F12C..0x0000F134
      return;
    }
    mEMPTime += dt;                                                   // 0x0000F138..0x0000F148
    if (mData.mEMPDuration > 0.f) {                                   // guest +0x3e0, 0x0000F14C..0x0000F154
      for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {                // 0x0000F230..0x0000F238
        const float distance =
            (mgr.GetPlayer(i)->GetTranslation() - GetTranslation()).Magnitude();  // 0x0000F16C..0x0000F1A4
        if (distance < 20.f) {                                        // rodata_46C, 0x0000F1A8..0x0000F1AC
          const float magnitude = CMath::Clamp(
              0.f, ((20.f - distance) / 20.f) * ((mData.mEMPDuration - mEMPTime) / mData.mEMPDuration),
              1.f);                                                   // 0x0000F1B8..0x0000F1FC
          mgr.PlayerState(i)->StaticInterference().AddSource(GetUniqueId(), magnitude,
                                                             0.2f);  // rodata_458, 0x0000F224
        }
      }
    }
    if (mEMPTime >= mData.mEMPDuration) {                             // 0x0000F23C..0x0000F24C
      mgr.DeleteObjectRequest(GetUniqueId());                         // 0x0000F264
    }
  }

private:
  CCommandoPirateGrenadeData mData;  // guest +0x390 (0x70)
  float mEMPTime;                    // guest +0x400
  CRELFileToken mRelToken;           // guest +0x404
};

}  // namespace

// fn_9_52BC (0x154): the grenade's aim point - the target's aim position (its vtable +0x54 with
// 0.5f, rodata_440, 0x0000530C..0x00005314), half the eye height lower for an unmorphed player
// (the player's +0x38c is 0, 0x00005334..0x00005360), and 3 units (rodata_448) towards the
// actor when it is more than 6 (rodata_49C) away (0x0000538C..0x000053CC).
// @ fn_9_52BC
CVector3f CCommandoPirate::x52BC_GetGrenadeTarget(const CStateManager& mgr,
                                                  const CActor& target) const {
  CVector3f aim = target.GetAimPosition(mgr, 0.5f);                 // 0x00005314
  const CPlayer* player = TCastToConstPtr< CPlayer >(target);       // 0x00005328
  if (player != nullptr && player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed) {
    aim -= CVector3f(0.f, 0.f, 0.5f * player->GetEyeHeight());      // 0x00005340..0x00005360
  }
  const CVector3f diff = GetTranslation() - aim;                    // 0x00005364..0x00005388
  const float distance = diff.Magnitude();                          // 0x0000538C
  if (distance > 6.f) {                                             // 0x00005398..0x0000539C
    aim += (3.f / distance) * diff;                                 // 0x000053A0..0x000053CC
  }
  return aim;
}

// fn_9_5008 (0x2b4): the launch angle and speed. Ten angles from 20 degrees up by 2.5 (rodata_4B4,
// rodata_4BC) - from 45 degrees down (rodata_4C0) when the target is above the origin
// (0x00005144..0x0000515C) - the first whose speed for the arc (half the gravity, lbl_8041B758
// and rodata_440) is within the block's launch speeds wins; otherwise the one closest to them.
// Both outputs are written (0x00005220..0x00005224), the defaults being 20 degrees and the
// minimum speed (0x000050D4, 0x000050E0).
// Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): every arm; the
// aim point forced below / far above / straight over the origin.
// @ fn_9_5008
void CCommandoPirate::x5008_SolveGrenadeLaunch(const CVector3f& target, const CVector3f& origin,
                                               float& angle, float& speed) const {
  const float heightDelta = target.GetZ() - origin.GetZ();          // 0x000050BC
  const float distance =
      CVector2f(target.GetX() - origin.GetX(), target.GetY() - origin.GetY()).Magnitude();  // 0x00005100
  const float minSpeed = mData.mGrenade.mGrenadeMinLaunchSpeed;     // guest this+0x890
  const float maxSpeed = mData.mGrenade.mGrenadeMaxLaunchSpeed;     // guest this+0x894
  const float halfGravityDistSq = distance * (0.5f * kDefaultGravityAccel * distance);  // 0x00005138, 0x00005178
  const float minSpeedSq = minSpeed * minSpeed;                     // 0x0000513C
  const float maxSpeedSq = maxSpeed * maxSpeed;                     // 0x0000514C
  float startAngle = 0.34906584f;                                   // rodata_4B4
  float stepAngle = 0.043633234f;                                   // rodata_4BC
  if (target.GetZ() > origin.GetZ()) {                              // 0x00005144..0x00005150
    startAngle = 0.7853982f;                                        // rodata_4C0
    stepAngle = -stepAngle;                                         // 0x00005158
  }
  float bestAngle = 0.34906584f;                                    // 0x000050D4
  float bestSpeed = minSpeed;                                       // 0x000050E0
  float bestError = FLT_MAX;                                        // rodata_4B8, 0x000050C0
  for (float i = 0.f; i < 10.f; i += 1.f) {                         // rodata_494/43C/444, 0x00005214..0x0000521C
    const float candidate = stepAngle * i + startAngle;             // 0x0000518C
    const float cosine = CMath::FastCosR(candidate);
    const float sine = CMath::FastSinR(candidate);
    const float denominator = distance * (cosine * sine) - heightDelta * (cosine * cosine);  // 0x000051A4..0x000051B0
    if (!(denominator > 1.1920929e-7f)) {                           // rodata_4C4, 0x000051B4..0x000051B8 ble
      continue;
    }
    const float speedSq = halfGravityDistSq / denominator;          // 0x000051BC
    if (speedSq >= minSpeedSq && speedSq <= maxSpeedSq) {           // 0x000051C0..0x000051D4
      bestAngle = candidate;
      bestSpeed = CMath::SqrtF(speedSq);                            // 0x000051DC
      break;
    }
    const float error = speedSq > maxSpeedSq ? speedSq - maxSpeedSq : minSpeedSq - speedSq;  // 0x000051E8..0x000051F8
    if (error < bestError) {                                        // 0x000051FC..0x00005200
      bestAngle = candidate;
      bestSpeed = CMath::SqrtF(speedSq);                            // 0x00005208
      bestError = error;
    }
  }
  angle = bestAngle;                                                // 0x00005220
  speed = bestSpeed;                                                // 0x00005224
}

// fn_9_5410 (0x370): the kUE_Projectile event's helper. With a target (guest this+0xBCA, 0x0000547C..
// 0x0000548C) it launches a "Commando E-Grenade" from the Mid_Launch locator (guest this+0xC5D):
// the aim point (fn_9_52BC) and the arc (fn_9_5008), the target's heading on the floor plane -
// the actor's forward when it has none, and no more than 30 degrees (rodata_4C8) off it - raised
// by the arc's angle, and the grenade looking along it; then the grenade-pending bit of
// guest this+0xC63 (0x02) is cleared (0x0000575C..0x00005768). With no target nothing happens.
// Run: cov 2026-10-21 (core drivers, 59 + swamp; force2/force3): every arm; no
// target forced on calls 1, 4, 7...
// @ fn_9_5410
void CCommandoPirate::x5410_CreateEGrenade(CStateManager& mgr) {
  const CTransform4f launchXf = GetLctrTransform(mLocatorMidLaunch);  // 0x00005434
  const CVector3f origin = launchXf.GetTranslation();                 // 0x00005448..0x0000546C
  float angle = 0.34906584f;                                          // rodata_4B4, 0x00005470
  float speed = mData.mGrenade.mGrenadeMinLaunchSpeed;                // guest this+0x890, 0x00005474
  const CActor* target = static_cast< const CActor* >(mgr.GetObjectById(xbca_));  // 0x00005484
  if (target == nullptr) {                                            // 0x00005488..0x0000548C
    return;
  }
  const CVector3f aim = x52BC_GetGrenadeTarget(mgr, *target);         // 0x000054A0
  x5008_SolveGrenadeLaunch(aim, origin, angle, speed);                // 0x000054D0
  CVector3f flat = aim - origin;                                      // 0x000054D4..0x00005510
  flat.SetZ(0.f);                                                     // rodata_494, 0x00005514
  const CVector3f forward = GetTransform().GetColumn(kDY);            // 0x00005518..0x0000552C
  CVector3f heading = flat.CanBeNormalized() ? flat.AsNormalized() : forward;  // 0x00005530..0x00005570
  if (CVector3f::GetAngleDiff(forward, heading) > 0.5235988f) {       // rodata_4C8, 0x00005574..0x00005584
    heading = CVector3f::Slerp(forward, heading, CRelAngle::FromRadians(0.5235988f));  // 0x0000559C
  }
  const CVector3f launchDir =
      CVector3f::Slerp(heading, CVector3f::Up(), CRelAngle::FromRadians(angle));  // 0x000055D4
  const CTransform4f grenadeXf =
      CTransform4f::LookAt(origin, origin + launchDir, CVector3f::Up());  // 0x0000561C
  CCommandoPirateEGrenade* grenade = rs_new CCommandoPirateEGrenade(  // 0x410 bytes, 0x0000564C
      mgr.AllocateUniqueId(), rstl::string_l("Commando E-Grenade"),  // rodata 0x8be, 0x000056D4
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true),  // 0x000056AC
      grenadeXf, CModelData(), CActorParameters(), GetUniqueId(), mData.mGrenade,
      speed);                                                         // fn_9_F418, 0x00005708
  if (grenade == nullptr) {                                           // 0x00005748..0x0000574C
    return;
  }
  mgr.AddObject(grenade);                                             // 0x00005758
  xc63_ &= ~0x02;                                                     // 0x0000575C..0x00005768
}
