// Port of the Dark Samus REL (module 10) helpers: the stateless functions of the module that are
// not FSM handlers (the ones below 0xA000). The function map, the caller and the arm tables are
// in docs/module-analysis/DarkSamus_helpers.md; the bodies marked `// @ fn_10_` were run in the
// Sanctuary 10_cliff_rooftop fight (boss u04f7).
#include "MetroidPrime/Enemies/CDarkSamus.hpp"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerGun.hpp"
#include "MetroidPrime/ScriptObjects/CHUDBillboardEffect.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayerGun.hpp"
#include "Collision/CCollidableAABox.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Math/CCylinder.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/Weapons/CFreezeBeamProjectile.hpp"
#include "MetroidPrime/Weapons/CImpactVisorEffect.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CAnimPlaybackParms.hpp"
#include "MetroidPrime/CAnimRes.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include <stdio.h>
#include "MetroidPrime/Weapons/CEnergyProjectile.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Particles/CParticleSwoosh.hpp"
#include "Kyoto/Particles/CSwooshDescription.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Enemies/CDarkSamusBattleStage.hpp"
#include "MetroidPrime/Enemies/CDarkSamusBoostBall.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "Kyoto/Graphics/CGX.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/Collision/CJointCollisionDescription.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"

// fn_10_9624 (0x00009624, 0x20 bytes): is the boost ball's uid (guest 0xE38) a real one? The
// compare is the "subf/or/srwi" idiom for `uid != kInvalidUniqueId` (0x00009630..0x00009640).
// No branch in retail or port; runs in every rooftop fight (PreRender, 0x00018B38).
// @ fn_10_9624
bool CDarkSamus::HasBoostBall() const {
  return mBoostBallUid != kInvalidUniqueId;
}

// fn_10_4868 (0x00004868, 0x60 bytes): the actor whose uid is at guest 0xF78 is deleted
// (0x000048A4) and the uid cleared (0x000048B0); a kInvalidUniqueId uid is left alone
// (0x00004890).
// @ fn_10_4868
void CDarkSamus::DeleteGuestActor(CStateManager& mgr) {
  if (mGuestF78 == kInvalidUniqueId) { // 0x00004890
    return;
  }
  mgr.DeleteObjectRequest(mGuestF78); // 0x000048A4
  mGuestF78 = kInvalidUniqueId;       // 0x000048B0
}

// fn_10_48C8 (0x000048C8, 0x44 bytes): the actor at guest 0xF78 is deleted when the timestamp at
// 0xF7C is older than the elapsed time (0x000048F0 compares it with guest 0x8B0).
// @ fn_10_48C8
void CDarkSamus::DeleteGuestActorIfExpired(CStateManager& mgr) {
  if (mGuestF78 == kInvalidUniqueId) { // 0x000048E4
    return;
  }
  if (!(mGuestF7C < mElapsedTime)) {  // 0x000048F0
    return;
  }
  DeleteGuestActor(mgr); // 0x000048F8
}

// fn_10_812C (0x0000812C, 0x4C bytes): the sfx handle at guest 0xF60 is removed (0x00008158)
// and cleared (0x00008160); a null handle is left alone (0x00008148).
// Arms: the live handle forced (the rooftop's beam sound is -1, so no emitter;
// darksamus_ds40_force.py).
// @ fn_10_812C
void CDarkSamus::StopGuestSfx() {
  if (mGuestF60 == CSfxHandle()) { // 0x00008148
    return;
  }
  CSfxManager::RemoveEmitter(mGuestF60); // 0x00008158
  mGuestF60 = CSfxHandle();               // 0x00008160
}

// fn_10_2E44 (0x00002E44, 0x20 bytes): the CModelData default constructor (0x00002E50).
// Class H (ds41): the REL's copy of the header inline CModelData::CModelDataNull()
// (include/MetroidPrime/CModelData.hpp:158), a lone `bl __ct__10CModelDataFv`; its callers
// (fn_10_2AD0 at 0x00002B24, fn_10_50B8 at 0x00005544) build the host's CModelData directly.
// This host function stays unreferenced.
void CDarkSamus::InitGuestModelData(CModelData* model) { new (model) CModelData(); }

// fn_10_8698 (0x00008698, 0xCC bytes): the taunt command over a state's messages. Activate
// (0x000086D8) stores 1 (kAS_Ready) to the animation state 0x6B4 and delivers the command
// (0x000086F0); Update (0x00008708) re-delivers it when the DOL's ShouldRetry (0x00008710, the
// host's CAnimationState::CanIssueCommand) answers true; Deactivate (0x00008744) stores 0
// (kAS_NotReady). The guest's 0x6B4 is the base's animation state, which the host keeps in
// CPatterned::mAnimationState.
// Arms: Activate, Update with ShouldRetry false and true, Deactivate, natural
// (tools/drivers/darksamus_ds39.con).
// @ fn_10_8698
void CDarkSamus::RunTauntCmd(EStateMsg msg, pas::ETauntType type) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);      // 0x000086E0
    BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(type)); // 0x000086F0
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Taunt)) { // 0x00008710
      BodyController()->CommandMgr().DeliverCmd(CBCTauntCmd(type));           // 0x0000872C
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x00008750
    break;
  default:
    // Dead: EStateMsg has three values (StateMachineCommon.hpp) and every caller passes its
    // state's message; retail's out-of-range exits are the 0x000086C8 (`b`, below 0) and
    // 0x000086D0 (`bge`, 3 and up).
    break;
  }
}

// fn_10_22020 (0x00022020, 0xCC bytes): the getup command over a state's messages, the same
// shape as fn_10_8698 with pas::kAS_Getup. Activate (`cmpwi r4, 0x0` / `bge` at 0x0002204C)
// stores 1 (kAS_Ready) to 0x6B4 (0x00022068) and delivers the command (0x00022078); Update
// (`cmpwi r4, 0x1` / `beq` at 0x00022028/0x00022040) re-delivers it when ShouldRetry
// (fn_801B73E4 at 0x00022098, the host's CAnimationState::CanIssueCommand) answers true
// (`clrlwi.` / `beq` at 0x0002209C); Deactivate (2, 0x0002205C) stores 0 (kAS_NotReady,
// 0x000220D0). Its only caller is the GetUp state fn_10_18EAC (0x00018F00).
// Arms: statecall u04f7 GetUp 0/1/2, Update with CanIssueCommand true and false (ds43).
// @ fn_10_22020
void CDarkSamus::RunGetupCmd(EStateMsg msg, const CBCGetupCmd& cmd) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready); // 0x00022068
    BodyController()->CommandMgr().DeliverCmd(cmd);       // 0x00022078
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_Getup)) { // 0x00022098
      BodyController()->CommandMgr().DeliverCmd(cmd);                         // 0x000220B4
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady); // 0x000220D0
    break;
  default:
    // Dead: EStateMsg has three values and the GetUp state passes its own message; retail's
    // out-of-range exits are 0x00022050 (`b`, below 0) and 0x00022058 (`bge`, 3 and up).
    break;
  }
}

// fn_10_6F90 (0x00006F90, 0xC4 bytes): the path find to the destination. The path find itself is
// fn_10_1C778 (the host's SetPathFindDestination, 0x00006FC0); when the path is shagged
// (0x00006FE0) the navigation goes back to the last good destination (guest 0x1048, 0x00006FF8)
// and the path find runs again (0x00007010); otherwise the destination is kept in 0x1048
// (0x00007018..0x0000702C).
// Called from the EnragedTravel state (fn_10_6E38 at 0x00006EBC and 0x00006F10). Arms: both natural
// (tools/drivers/darksamus_ds39.con).
// @ fn_10_6F90
void CDarkSamus::FindPathToDestination(CStateManager& mgr, const CVector3f& destination, float dt) {
  SetPathFindDestination(mgr, destination, dt); // fn_10_1C778 at 0x00006FC0
  if (PathShagged(mgr, CTriggerData(0.f))) {    // 0x00006FE0
    // 0x00006FF8: back to the last destination that was not shagged (guest 0x1048, a member of
    // its own: fn_10_1C778 writes 0x8B4, not 0x1048).
    mPathFindNavigation.SetDestination(CVector3f(mGuest1048, mGuest104C, mGuest1050));
    mPathFindNavigation.PathFind(mgr, kStateMsg_Activate, dt, *this); // 0x00007010
  } else {
    mGuest1048 = destination.GetX(); // 0x00007018..0x0000702C
    mGuest104C = destination.GetY();
    mGuest1050 = destination.GetZ();
  }
}

// fn_10_69C0 (0x000069C0, 0x80 bytes): the uids at guest 0x934 are deleted from the state manager
// (0x000069FC..0x00006A0C) and the slot cleared (0x00006A14); a kInvalidUniqueId slot is left
// alone (0x000069F4). The loop runs over the count at guest 0x930 (0x00006A20).
// @ fn_10_69C0
void CDarkSamus::DeleteGuestUids(CStateManager& mgr) {
  for (int i = 0; i < mGuest930; ++i) { // 0x00006A20
    if (mGuest934[i] == kInvalidUniqueId.value) { // 0x000069F4
      continue;
    }
    mgr.DeleteObjectRequest(TUniqueId(mGuest934[i])); // 0x00006A0C
    mGuest934[i] = kInvalidUniqueId.value;           // 0x00006A14
  }
}

// fn_10_6374 (0x00006374, 0xEC bytes): the path find along the waypoints. With the message 0
// (`mr. r31, r5` at 0x00006394) the path find starts toward the module's vector at guest 0xEE4
// (fn_10_1C778 at 0x000063AC); otherwise the path find runs with that message (0x000063BC), then
// the separation (fn_10_1DD14, 0x000063C8) and, when the path is shagged (0x000063F4) or the actor
// is on the last waypoint (the waypoint count 0x7C4 and the current one 0x888, 0x000063F8..
// 0x00006408), the destination is set again (0x0000641C). Every message ends with the additive
// animation (fn_10_1D860, 0x0000642C) and the glide emitter (fn_10_11334, 0x00006438).
// Arms: Activate and the shagged path natural; a path not shagged on its last waypoint and before
// it forced (the search's result, waypoint and count, tools/cov/specs/darksamus_ds39_force.py).
// @ fn_10_6374
void CDarkSamus::FindPathToWaypoint(CStateManager& mgr, float dt, EStateMsg msg) {
  if (msg == kStateMsg_Activate) {
    SetPathFindDestination(mgr, mGuestEE4, dt); // fn_10_1C778 at 0x000063AC
  } else {
    mPathFindNavigation.PathFind(mgr, msg, dt, *this); // 0x000063BC
    Separate(mgr);                                     // fn_10_1DD14 at 0x000063C8
    if (PathShagged(mgr, CTriggerData(0.f)) ||         // 0x000063E8
        mPathFindSearch.GetCurrentWaypoint() >=
            static_cast< int >(mPathFindSearch.GetWaypoints().size()) - 1) {
      SetPathFindDestination(mgr, mGuestEE4, dt); // fn_10_1C778 at 0x0000641C
    }
  }
  UpdateAdditiveAnimation(mgr, msg); // fn_10_1D860 at 0x0000642C
  UpdateGlideEmitter(msg);           // fn_10_11334 at 0x00006438
}

// fn_10_21104 (0x00021104): the visor effect's particle token at guest 0xF6C, made from the asset id
// at 0xF68 when it is a real one (`addis r0, r4, 0x1; cmplwi r0, 0xffff` at 0x00021114), else empty.
// Run: the empty arm forced (the id kInvalidAssetId, darksamus_phazonbeam_invalid.py).
// @ fn_10_21104
rstl::optional_object< TToken< CGenDescription > > CDarkSamus::PhazonVisorToken(CAssetId id) {
  if (id == kInvalidAssetId) {
    return rstl::optional_object< TToken< CGenDescription > >();
  }
  return rstl::optional_object< TToken< CGenDescription > >(
      gpSimplePool->GetObj(SObjectTag('PART', id)));
}

// fn_10_452C (0x0000452C, 0xCC bytes), called on the 0xEF0 sub-object: the phazon attack's state
// reset. 0xF50 = 0 (`stfs f2, 0x60(r31)`), the shots 0xF48 and 0xF4C = 0, 0xF5C bits 0x80 and 0x40
// cleared (0x00004564/0x00004570), 0xF14 = 0xF20 = the zero vector, 0xF54 = lbl_10_rodata_140 =
// -1000.0f, 0xF10 = 0; the emitter 0xF60 removed when there is one (0x000045CC..0x000045DC, the
// body of fn_10_812C).
// @ fn_10_452C
void CDarkSamus::ResetPhazonAttack() {
  mGuestF50 = 0.f;
  mGuestF48 = 0;
  mGuestF4C = 0;
  mGuestF5C80 = false;
  mGuestF5C40 = false;
  mGuestF14 = CVector3f::Zero();
  mGuestF54 = -1000.f;
  mGuestF10 = 0.f;
  mGuestF20 = CVector3f::Zero();
  StopGuestSfx(); // 0x000045C4
}

// fn_10_490C (0x0000490C, 0x1BC bytes): the big phazon visor effect. With no effect actor 0xF78
// (0x00004930) a CHUDBillboardEffect "Dark Samus Big Phazon Visor Effect" (lbl_10_rodata_1F0 + 7)
// is made from the token 0xF6C, with a new uid kept in 0xF78 (0x00004988), the near clip distance
// and the POV scale of player 0, white, scale one, offset zero (0x00004A30), and added
// (0x00004A70). Always: 0xF7C = the elapsed time + duration (0x00004A80), when fn_10_48C8 deletes it.
// Run (darksamus_phazon.con): made on a first contact, kept on a later one; the allocation's
// null arm (`mr. r28, r3` at 0x000049B4) has no host counterpart (rs_new does not return null).
// @ fn_10_490C
void CDarkSamus::StartPhazonVisorEffect(CStateManager& mgr, float duration) {
  if (mGuestF78 == kInvalidUniqueId) {
    const float nearClip = CHUDBillboardEffect::GetNearClipDistance(mgr, 0);
    const CVector3f scale = CHUDBillboardEffect::GetScaleForPOV(mgr);
    mGuestF78 = mgr.AllocateUniqueId();
    CHUDBillboardEffect* effect = rs_new CHUDBillboardEffect(
        mGuestF6C, rstl::optional_object_null(), mGuestF78, true,
        rstl::string_l("Dark Samus Big Phazon Visor Effect"), nearClip, scale, 0, CColor::White(),
        CVector3f::One(), CVector3f::Zero(), false);
    mgr.AddObject(effect);
  }
  mGuestF7C = mElapsedTime + duration;
}

// fn_10_7304 (0x00007304, 0x168 bytes): the model scale (CModelData::SetScale at 0x0000732C) and
// the collision box that follows it: k = lbl_10_rodata_1B4 = 2.1 * (scale.z / lbl_10_rodata_1B8 =
// 5.0); the box is the base box 0x102C with min * k and max * k, max.z also raised by the base
// box's height (0x000073A0..0x000073B8); it is the bounding box and the CCollidableAABox with the
// actor's materials (0x68), the transform is dirty and the sorted lists updated (0x00007450).
// @ fn_10_7304
void CDarkSamus::SetPhazonScale(const CVector3f& scale, CStateManager& mgr) {
  ModelData()->SetScale(scale);
  const float k = 2.1f * (scale.GetZ() / 5.f);
  const CVector3f& min = mGuest102C.GetMinPoint();
  const CVector3f& max = mGuest102C.GetMaxPoint();
  const CAABox box(min * k, CVector3f(max.GetX() * k + 0.f, max.GetY() * k + 0.f,
                                      max.GetZ() * k + (max.GetZ() - min.GetZ())));
  SetBoundingBox(box);
  SetCollisionPrimitive(CCollidableAABox(box, GetMaterialList()));
  SetTransformDirty();
  mgr.UpdateActorInSortedLists(this);
}

// fn_10_746C (0x0000746C, 0xCC bytes): the phazon shrink. Nothing for dt <= 0 (0x00007480) or a
// z scale already at most 0xFAC (0x000074A8); else the scale less one * lbl_10_rodata_178 = 2.2 *
// dt, all of it 0xFAC once its z is below 0xFAC (0x000074F0), into fn_10_7304 (0x00007524).
// @ fn_10_746C
void CDarkSamus::ShrinkPhazonScale(CStateManager& mgr, float dt) {
  if (dt <= 0.f) {
    return;
  }
  const CVector3f scale = ModelData()->GetScale();
  if (scale.GetZ() <= mGuestFAC) {
    return;
  }
  CVector3f next = scale - CVector3f::One() * (2.2f * dt);
  if (next.GetZ() < mGuestFAC) {
    next = CVector3f::One() * mGuestFAC;
  }
  SetPhazonScale(next, mgr);
}

// fn_10_7538 (0x00007538, 0xD8 bytes): the phazon growth. Nothing for dt <= 0 (0x0000754C); the
// first time (0xFAC still lbl_10_rodata_13C = 0, 0x00007558) 0xFAC keeps the z scale; nothing past
// an x scale of lbl_10_rodata_1B8 = 5.0 (0x000075AC); else the scale plus one *
// lbl_10_rodata_16C = 0.8 * dt into fn_10_7304 (0x000075FC).
// @ fn_10_7538
void CDarkSamus::GrowPhazonScale(CStateManager& mgr, float dt) {
  if (dt <= 0.f) {
    return;
  }
  if (mGuestFAC == 0.f) {
    mGuestFAC = ModelData()->GetScale().GetZ();
  }
  const CVector3f scale = ModelData()->GetScale();
  if (scale.GetX() > 5.f) {
    return;
  }
  SetPhazonScale(scale + CVector3f::One() * (0.8f * dt), mgr);
}

// fn_10_7780 (0x00007780, 0x38 bytes): the enrage sphere's position, guest 0x8FC..0x904 (mRootPos)
// plus (lbl_10_rodata_13C = 0, 0, lbl_10_rodata_1C8 = -1).
// @ fn_10_7780
CVector3f CDarkSamus::GetPhazonSpherePos() const {
  return CVector3f(mRootPos.GetX() + 0.f, mRootPos.GetY() + 0.f, mRootPos.GetZ() + -1.f);
}

// fn_10_7610 (0x00007610, 0x170 bytes): the enrage sphere's frame. Nothing for dt <= 0 (0x00007638)
// or no sphere 0x1028 (0x00007650); else its orientation the actor's rotation (vtable 0x14), its
// global translation fn_10_7780 (0x20), and its global scale the model scale, pulsed for
// lbl_10_rodata_1BC = 1.1 s after the volley start 0xFC8 (0x000076D0) by 1 + 0.7 *
// sin(pi * (elapsed - 0xFC8) / 1.1) (0x00007724) (0x24); then its Update(dt) (0xC).
// @ fn_10_7610
void CDarkSamus::UpdatePhazonSphere(float dt) {
  if (dt <= 0.f || mGuest1028.get() == nullptr) {
    return;
  }
  CElementGen* gen = mGuest1028.get();
  gen->SetOrientation(GetTransform().GetRotation());
  gen->SetGlobalTranslation(GetPhazonSpherePos());
  CVector3f scale = ModelData()->GetScale();
  if (1.1f + mGuestFC8 > mElapsedTime) {
    const float pulse =
        static_cast< float >(sin(3.1415927f * ((mElapsedTime - mGuestFC8) / 1.1f)));
    scale = scale * (0.70000005f * pulse + 1.f);
  }
  mGuest1028->SetGlobalScale(scale);
  mGuest1028->Update(dt);
}

// fn_10_786C (0x0000786C, 0x1A0 bytes): the enrage sphere, made once (0x00007894): a CElementGen
// (0x338 bytes) of the PART 0x1020 (mData.mPhazonEnrageSphere), normal orientation, optional
// system flags 1 (0x00007918), into the auto_ptr 0x1024/0x1028 (0x00007970), and it emits
// (vtable 0x2C with 1, 0x000079EC).
// @ fn_10_786C
void CDarkSamus::CreatePhazonSphere() {
  if (mGuest1028.get() != nullptr) {
    return;
  }
  mGuest1028 = rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', mData.mPhazonEnrageSphere)),
                                  CElementGen::kMOT_Normal, CElementGen::kOSF_One);
  mGuest1028->SetParticleEmission(true);
}

// fn_10_7D00 (0x00007D00, 0x298 bytes): the enrage sphere's contact. Nothing within
// lbl_10_rodata_1AC = 0.5 s of the last contact 0x101C (0x00007D28) or with no player touch
// bounds (vtable 0x48, 0x00007DC8); else the box's centre and its half diagonal (0x00007E10), the
// sphere's global scale's length (vtable 0x4C, 0x00007E24), and a contact when the centre is
// nearer fn_10_7780 than their sum (0x00007E78): the push (fn_10_13750 with lbl_10_rodata_174 = 22
// and 0, 0x00007E90), the damage 0x1000 (mData.mPhazonEnrageSphereContactDamage) with the solid
// include filter (1 << lbl_10_data_1A0 = 59, type 3, 0x00007F1C), the visor effect for 0.5 s when
// the player is unmorphed (0x38C == 0, 0x00007F48), and 0x101C = the elapsed time (0x00007F50).
// Run (darksamus_phazon.con): gated, near unmorphed and morphed, far. The no-touch-bounds return
// cannot be taken against the player: CPlayer::GetTouchBounds returns a box in both its arms.
// @ fn_10_7D00
void CDarkSamus::TouchPhazonSphere(CStateManager& mgr) {
  if (0.5f + mGuest101C > mElapsedTime) {
    return;
  }
  CPlayer* player = mgr.GetPlayer(0);
  const rstl::optional_object< CAABox > bounds = player->GetTouchBounds();
  if (!bounds) {
    return;
  }
  // Host-only guard, not a retail arm: retail reads the pointer 0x1028 (the sphere's vtable)
  // unchecked, since the enrage made the sphere first; a forced phazon state with no sphere would
  // crash here.
  if (mGuest1028.get() == nullptr) {
    return;
  }
  const CVector3f center = bounds.data().GetCenterPoint();
  const float boxRadius = (bounds.data().GetMaxPoint() - center).Magnitude();
  const float sphereRadius = mGuest1028->GetGlobalScale().Magnitude();
  // 0x7E84/0x7E88 fcmpo, bge to the return (taken on a NaN too).
  if (!((center - GetPhazonSpherePos()).Magnitude() < sphereRadius + boxRadius)) {
    return;
  }
  PushPlayer(mgr, 22.f, 0.f);
  mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(),
                  mData.mPhazonEnrageSphereContactDamage,
                  CMaterialFilter(CMaterialList(kMT_Solid), CMaterialList(),
                                  CMaterialFilter::kFT_IncludeExclude),
                  CVector3f::Zero());
  if (player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed) {
    StartPhazonVisorEffect(mgr, 0.5f);
  }
  mGuest101C = mElapsedTime;
}

// fn_10_7F98 (0x00007F98, 0x16C bytes): the phazon enrage's frame, every message: the attack
// bookkeeping with 0x800000 (fn_10_1B478), the collision failsafe, the ground collision, the
// growth fn_10_7538, the sphere fn_10_7610, its contact fn_10_7D00 and the visor effect's expiry
// fn_10_48C8 (0x00008024). Update (`cmpwi r31, 0x1` at 0x00008028): toward the phazon waypoint of
// type 8 (fn_10_65CC) by lbl_10_rodata_1D8 = 0.6981317 rad unless it is the zero vector
// (0x000080AC); with the player gun tweak (0x000080BC) and the absorbed phazon shots (the gun's
// 0x7FC) at least its maximum (0x000080DC), 0xFCC loses lbl_10_rodata_1DC = 3.5 * dt (0x000080E8).
// Forced (darksamus_phazon.py): the absorbed shots at the maximum, and gpTweakPlayerGun null.
// @ fn_10_7F98
void CDarkSamus::UpdatePhazonEnrage(CStateManager& mgr, EStateMsg msg, float dt) {
  UpdateCurrentAttack(0x800000, mgr, msg);
  RunCollisionFailsafe(mgr);
  UpdateGroundCollision(mgr, msg);
  GrowPhazonScale(mgr, dt);
  UpdatePhazonSphere(dt);
  TouchPhazonSphere(mgr);
  DeleteGuestActorIfExpired(mgr);
  if (msg != kStateMsg_Update) {
    return;
  }
  const CVector3f waypoint = GetPhazonWaypoint(mgr, 8);
  if (!(waypoint == CVector3f::Zero())) {
    TurnTowardPoint(dt, waypoint, 0.6981317f);
  }
  if (gpTweakPlayerGun != nullptr &&
      mgr.GetPlayer(0)->GetPlayerGun()->GetAbsorbedPhazonShots() >=
          gpTweakPlayerGun->GetMaxAbsorbedPhazonShots()) {
    mGuestFCC -= 3.5f * dt;
  }
}

// fn_10_45F8's token at 0x00004644: the beam's hit effect at guest 0xF00, locked, from the asset id
// at 0xEFC when it is a real one (`addis r0, r5, 0x1` at 0x00004604, `cmplwi r0, 0xffff` at
// 0x00004608, the `beq` at 0x00004640), else empty.
rstl::optional_object< TLockedToken< CGenDescription > > CDarkSamus::PhazonBeamHitToken(CAssetId id) {
  if (id == kInvalidAssetId) {
    return rstl::optional_object< TLockedToken< CGenDescription > >();
  }
  return rstl::optional_object< TLockedToken< CGenDescription > >(
      TLockedToken< CGenDescription >(gpSimplePool->GetObj(SObjectTag('PART', id))));
}

// fn_10_45F8 (0x000045F8, 0x270 bytes): the 0xEF0 sub-object's constructor, at 0x000201B4 of the
// actor's. Its id/token/damage/sound members (0xEF0 = r4, 0xEFC = r5 and the locked token 0xF00,
// 0xF2C..0xF46 = *r6, 0xF5E = r7) are mData's in the host and PhazonBeamHitToken (the member
// initializer of mGuestF00); the beam 0xEF4/0xEF8 starts empty (0x00004634/0x00004638). The
// scalars: 0xF14 and 0xF20 the zero vector (0x000047BC..0x000047F8), 0xF58 = lbl_10_rodata_13C
// = 0.0f, the emitter 0xF60 none and 0xF64 = lbl_10_rodata_140 = -1000.0f (0x0000483C..0x00004848),
// then fn_10_452C (0x0000484C). Run: every actor's constructor; the token's invalid-id arm
// forced (darksamus_phazonbeam_invalid.py).
// @ fn_10_45F8
void CDarkSamus::ConstructPhazonBeam() {
  mGuestF14 = CVector3f::Zero();
  mGuestF20 = CVector3f::Zero();
  mGuestF58 = 0.f;
  mGuestF60 = CSfxHandle();
  mGuestF64 = -1000.f;
  ResetPhazonAttack();
}

// fn_10_6118 (0x00006118, 0x19C bytes): the phazon beam. A CElementGen of the asset 0xEF0
// (mData.mPart908B06E9) with the orientation kMOT_Normal and the system flags 1 (0x000061B8)
// replaces the auto_ptr 0xEF4/0xEF8 (the old one deleted, 0x0000620C), and it emits (vtable 0x2C
// with 1, 0x00006290). The kUE_EventStart event of the 0x400000 attack calls it (0x0001C270).
// Run: `uevent u04f7 27` (darksamus_phazonbeam.con), a beam made and one replaced.
// @ fn_10_6118
void CDarkSamus::CreatePhazonBeam() {
  mGuestEF8 = rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', mData.mPart908B06E9)),
                                 CElementGen::kMOT_Normal, CElementGen::kOSF_One);
  mGuestEF8->SetParticleEmission(true);
}

// fn_10_58C0 (0x000058C0, 0xB4 bytes): an empty auto_ptr into 0xEF4/0xEF8, which deletes the beam
// (0x00005918). The kUE_EventStop event calls it (0x0001C2A4). Run: `uevent u04f7 28`.
// @ fn_10_58C0
void CDarkSamus::ReleasePhazonBeam() {
  mGuestEF8 = rstl::auto_ptr< CElementGen >();
}

// fn_10_8178 (0x00008178, 0x5C bytes): the beam's sound. The emitter 0xF60 is stopped
// (fn_10_812C) and a new one of the sfx 0xF5E (mData.mSound, kInternalInvalidSfxId for -1: the
// ctor's 0x000201A8) at the actor's position, volume 0x7F, the actor's area, no acoustics, not
// looped, kMedPriority (0x000081B4) is kept in 0xF60. Run: both sound arms forced (the data's -1
// in darksamus_phazonbeam_invalid.py, 1 in darksamus_phazonbeam.py).
// @ fn_10_8178
void CDarkSamus::StartPhazonBeamSfx() {
  StopGuestSfx();
  const ushort sfx = mData.mSound == -1 ? CSfxManager::kInternalInvalidSfxId
                                        : static_cast< ushort >(mData.mSound);
  mGuestF60 = CSfxManager::AddEmitter(sfx, GetTranslation(), 0x7F, GetCurrentAreaId().Value(),
                                      false, false, CSfxManager::kMedPriority);
}

// fn_10_5974 (0x00005974, 0x7A4 bytes): the phazon beam's update, PhazonAttack's Update
// (0x00008340). Without a beam 0xEF8 nothing (0x000059C0). The beam sits at the gun locator 0x8CC
// (vtable 0x20, 0x000059EC). The player's offset from the actor, its z the height
// lbl_10_rodata_14C = 1.0f + 0xF58 less the actor's z, must be normalizable (0x00005A48); the
// angle between it and the actor's forward is kept. The beam's reach 0xF10: within
// lbl_10_rodata_170 = 20 degrees (0x00005A9C) it follows the flat distance to the player by at most
// lbl_10_rodata_174 = 22 * dt either way (0x00005AC0..0x00005ADC). The beam looks from the locator
// at the actor's position + forward * reach at that height (LookAt at 0x00005B64); 0xF14 = its
// forward (0x00005B8C), the generator gets its rotation (0x1C), scale one (0x24) and dt (0x0C).
// With 0xF14 normalizable (0x00005BF8): the player's position, eye or aim position (vtable 0x54,
// dt 0) inside the cylinder of radius lbl_10_rodata_178 = 2.2 along it (0x00005C58/0x00005C7C/
// 0x00005CBC) and the angle under lbl_10_rodata_17C = 90 degrees (0x00005CD8): the damage 0xF2C
// with its amount * dt goes to the player (solid include-exclude filter, 0x00005DA4), 0xF54 = the
// elapsed time and the visor effect for lbl_10_rodata_180 = 0.25 s (fn_10_490C). Then 0xF50 gains
// dt and every lbl_10_rodata_184 = 0.07 s of it (0x000060D8) casts a ray of lbl_10_rodata_18C =
// 100 along 0xF14 from a random point on the beam's 2.2 ring (Range(0, 2 pi), 0x00005E54) against
// solid and AI-block (0x00005EFC); a hit sets the emitter position 0xF20 (the hit less the ring
// offset, 0x00005F58) and adds a "Phazon Attack Collision" CExplosion of 0xF00 at the hit, facing
// the player's eye (0x00006098).
// Run (darksamus_phazonbeam.py, _invalid.py): every arm by a keyed force (the zero offset, the
// turn, the reach clamps, each cylinder test, a NaN locator for the unnormalizable 0xF14, a ray
// too short to hit). The hit token 0xF00 is read unchecked, as retail does (0x00006098); the
// fights' data always carries pART_0x494de4a4. The CExplosion allocation's null/throw edges have no
// host counterpart (rs_new does not return null).
// @ fn_10_5974
void CDarkSamus::UpdatePhazonBeam(CStateManager& mgr, float dt) {
  if (mGuestEF8.get() == nullptr) {
    return;
  }
  mGuestEF8->SetGlobalTranslation(mGunLctrXf.GetTranslation());
  CTransform4f beamXf = mGunLctrXf;
  CPlayer* player = mgr.GetPlayer(0);
  const float height = 1.f + mGuestF58;
  const CVector3f& pos = GetTranslation();
  CVector3f toPlayer(player->GetTranslation().GetX() - pos.GetX(),
                     player->GetTranslation().GetY() - pos.GetY(), height - pos.GetZ());
  if (!toPlayer.CanBeNormalized()) {
    return;
  }
  const CVector3f fwd = GetTransform().GetForward();
  const float angle = CVector3f::GetAngleDiff(fwd, toPlayer.AsNormalized());
  toPlayer.SetZ(0.f);
  float reach = mGuestF10;
  if (angle < 0.34906584f) {
    const float dist = toPlayer.Magnitude();
    const float step = 22.f * dt;
    const float lo = mGuestF10 - step;
    const float hi = mGuestF10 + step;
    if (lo > dist) {
      reach = lo;
    } else if (hi < dist) {
      reach = hi;
    } else {
      reach = dist;
    }
  }
  CVector3f target = fwd * reach + pos;
  target.SetZ(height);
  beamXf = CTransform4f::LookAt(beamXf.GetTranslation(), target, CVector3f::Up());
  mGuestF10 = reach;
  mGuestF14 = beamXf.GetForward();
  mGuestEF8->SetGlobalOrientation(beamXf.GetRotation());
  mGuestEF8->SetGlobalScale(CVector3f::One());
  mGuestEF8->Update(dt);
  if (!mGuestF14.CanBeNormalized()) {
    return;
  }
  mGuestF14.Normalize();
  const CVector3f axis = mGuestF14.AsNormalized();
  const CCylinder beam(CLine(mGunLctrXf.GetTranslation(),
                             CUnitVector3f(axis.GetX(), axis.GetY(), axis.GetZ())),
                       2.2f);
  if ((beam.PointInside(player->GetTranslation()) || beam.PointInside(player->GetEyePosition()) ||
       beam.PointInside(player->GetAimPosition(mgr, 0.f))) &&
      angle < 1.5707964f) {
    CDamageInfo damage(mData.mDamageInfo);
    damage.SetDamage(dt * mData.mDamageInfo.GetDamage());
    mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), damage,
                    CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList()),
                    CVector3f::Zero());
    mGuestF54 = mElapsedTime;
    StartPhazonVisorEffect(mgr, 0.25f);
  }
  mGuestF50 += dt;
  while (mGuestF50 > 0.07f) {
    mGuestF50 -= 0.07f;
    CVector3f origin = beamXf.GetTranslation();
    const float a = mgr.Random()->Range(0.f, 6.2831855f);
    const float ringY = 2.2f * CMath::FastSinR(a);
    const float ringX = 2.2f * CMath::FastCosR(a);
    origin = CVector3f(origin.GetX() + ringX, origin.GetY() + ringY, origin.GetZ() + 0.f);
    const CRayCastResult hit = mgr.RayStaticIntersection(
        origin, mGuestF14, 100.f,
        CMaterialFilter::MakeInclude(CMaterialList(kMT_Solid, kMT_AIBlock)));
    if (!hit.IsValid()) {
      continue;
    }
    const CVector3f& point = hit.GetPoint();
    mGuestF20 = CVector3f(point.GetX() - ringX, point.GetY() - ringY, point.GetZ() - 0.f);
    const CTransform4f xf = CTransform4f::LookAt(point, player->GetEyePosition(), CVector3f::Up());
    const rstl::string name = rstl::string_l("Phazon Attack Collision");
    const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId);
    const TUniqueId uid = mgr.AllocateUniqueId();
    mgr.AddObject(rs_new CExplosion(*mGuestF00, uid, info, name, xf, 0, CVector3f::One() * 1.f,
                                    CColor::White(), -1));
  }
}

// fn_10_99B8 (0x000099B8, 0xDC bytes): the boost ball's old particle effects are deleted and
// forgotten: each swoosh handle at 0xE3C and 0xE4C that is set (0x000099D0, 0x000099FC) is
// cleared (0x000099DC, 0x00009A08) and its object deleted (the vtable's word 2 with the argument
// 1, 0x000099F4 and 0x00009A20), and the element generator's auto_ptr at 0xE60/0xE64 takes an
// empty one (0x00009A74/0x00009A7C), deleting the old generator (0x00009A68).
// The asm's four calls are the deleting destructors (`delete` here, the auto_ptr's two
// included) and its other conditions the auto_ptr assignment's (self, valid byte, null).
// Runs (darksamus_boostfx.py, ds895c b1/f1/f2): no effects (the first spawn) and both swooshes.
// @ fn_10_99B8
void CDarkSamus::StopBoostBallParticles() {
  if (mBoostBallSwooshE3C != nullptr) { // 0x000099D0
    CParticleSwoosh* swoosh = mBoostBallSwooshE3C;
    mBoostBallSwooshE3C = nullptr; // 0x000099DC
    delete swoosh;                 // 0x000099F4
  }
  if (mBoostBallSwooshE4C != nullptr) { // 0x000099FC
    CParticleSwoosh* swoosh = mBoostBallSwooshE4C;
    mBoostBallSwooshE4C = nullptr; // 0x00009A08
    delete swoosh;                 // 0x00009A20
  }
  mBoostBallElementGen = nullptr; // 0x00009A68..0x00009A7C
}

// fn_10_895C's swoosh tokens (the header's mBoostBallSwooshTokenE40/E50): an invalid id leaves
// the optional empty (`cmplwi r0, 0xffff` at 0x00008A58 / 0x00008B74), a valid one is the pool's
// 'SWHC' (0x00008AE4 / 0x00008B84), locked (0x00008B6C / 0x00008C0C).
rstl::optional_object< TLockedToken< CSwooshDescription > >
CDarkSamus::BoostBallSwooshToken(CAssetId id) {
  if (id == kInvalidAssetId) {
    return rstl::optional_object< TLockedToken< CSwooshDescription > >();
  }
  return rstl::optional_object< TLockedToken< CSwooshDescription > >(
      TLockedToken< CSwooshDescription >(gpSimplePool->GetObj(SObjectTag('SWHC', id))));
}

// fn_10_9B24 (0x00009B24, 0x2DC bytes), SpawnBoostBall's last call (0x0000A03C): the boost ball's
// effects. With the trail swoosh token (0xE48 == 1, 0x00009B48) a CParticleSwoosh (`new 0x218`,
// length 0, 0x00009B8C) replaces 0xE3C (the old one deleted, the vtable's word 2 at 0x00009BB0;
// the store at 0x00009BB8) and emits (the vtable's word 0x2C = SetParticleEmission(true),
// 0x00009BE0); the same for the second token (0xE58 == 1, 0x00009BEC) into 0xE4C (0x00009C2C,
// 0x00009C50, 0x00009C58, 0x00009C80). Then, untested, a CElementGen (`new 0x338`) of the
// glow 'PART' 0xE5C (`lis r5, 0x5041` at 0x00009CAC, `addi r0, r5, 0x5254` at 0x00009CB8)
// with kMOT_Normal (`li r5, 0x0` at 0x00009CFC)
// and kOSF_One (`li r6, 0x1` at 0x00009D00) goes into the auto_ptr at 0xE60/0xE64 (the old
// deleted at 0x00009D58, the stores at 0x00009D64 / 0x00009D6C) and emits (0x00009DDC).
// The asm's other conditions are the two `new` null tests, the token temporaries' flags and
// the auto_ptr's self-assignment test; an old 0xE3C/0xE4C is never there (the only caller,
// SpawnBoostBall, runs fn_10_99B8 first at 0x00009E30).
// Runs (darksamus_boostfx.py, ds895c b1/f1/f2): both tokens (natural), the trail token absent
// (forced, DSB_MODE=1) and the second token absent (forced, DSB_MODE=2).
// @ fn_10_9B24
void CDarkSamus::CreateBoostBallEffects() {
  if (mBoostBallSwooshTokenE40.valid() == true) { // 0x00009B48
    CParticleSwoosh* swoosh = rs_new CParticleSwoosh(*mBoostBallSwooshTokenE40, 0); // 0x00009B8C
    delete mBoostBallSwooshE3C;                    // 0x00009BB0
    mBoostBallSwooshE3C = swoosh;                  // 0x00009BB8
    mBoostBallSwooshE3C->SetParticleEmission(true); // 0x00009BE0
  }
  if (mBoostBallSwooshTokenE50.valid() == true) { // 0x00009BEC
    CParticleSwoosh* swoosh = rs_new CParticleSwoosh(*mBoostBallSwooshTokenE50, 0); // 0x00009C2C
    delete mBoostBallSwooshE4C;                    // 0x00009C50
    mBoostBallSwooshE4C = swoosh;                  // 0x00009C58
    mBoostBallSwooshE4C->SetParticleEmission(true); // 0x00009C80
  }
  mBoostBallElementGen = rs_new CElementGen(gpSimplePool->GetObj(SObjectTag('PART', mBoostBallGlowPart)),
                                            CElementGen::kMOT_Normal, CElementGen::kOSF_One); // 0x00009D04
  mBoostBallElementGen->SetParticleEmission(true); // 0x00009DDC
}


// fn_10_8870 (0x00008870, 0xE0 bytes): the loop attack command over a state's messages. The
// function is generic: the message is in r4, the animation state for the DOL's ShouldRetry in r5
// (0x000088F8) and the command in r6 - the id at cmd+4 (0x000088B4, 0x00008908) is delivered
// and cmd+4..+0x10 is copied into the command manager's slot (0x000088D4..0x000088E8,
// 0x0000891C..0x00008934). Activate (0x000088B0) stores 1 (kAS_Ready) to the animation state
// 0x6B4, Update (0x000088F0) re-delivers the command while ShouldRetry answers true, and
// Deactivate (0x0000893C) stores 0 (kAS_NotReady).
// @ fn_10_8870
void CDarkSamus::RunLoopAttackCmd(EStateMsg msg, const CBCLoopAttackCmd& attack) {
  switch (msg) {
  case kStateMsg_Activate:
    mAnimationState.SetState(CAnimationState::kAS_Ready);
    BodyController()->CommandMgr().DeliverCmd(attack);
    break;
  case kStateMsg_Update:
    if (mAnimationState.CanIssueCommand(*BodyController(), pas::kAS_LoopAttack)) {
      BodyController()->CommandMgr().DeliverCmd(attack);
    }
    break;
  case kStateMsg_Deactivate:
    mAnimationState.SetState(CAnimationState::kAS_NotReady);
    break;
  }
}

// fn_10_77B8 (0x000077B8, 0xB4 bytes): the enrage sphere's generator (the auto_ptr at guest
// 0x1024/0x1028) is replaced by an empty one built on the stack (0x000077D8..0x000077E0): the old
// generator, if owned, is deleted through its vtable (0x00007810).
// @ fn_10_77B8
void CDarkSamus::ClearEnrageSphere() { mGuest1028 = rstl::auto_ptr< CElementGen >(); }

// fn_10_13998 (0x00013998, 0x11C bytes): the melee hit, once per swing (the record's hit byte
// 0x9DE, 0x000139B4; set at 0x00013A98): the record's damage (meleeAttackDamage, 0x9A4) on the
// player from the actor, filtered include kMT_Solid (lbl_10_data_200 = 59) exclude nothing with
// type 3 (0x000139DC), at the zero vector (0x00013A3C); unless the player is a morph ball (vtable
// 0x1CC = fn_10_112E8 with a 0.0f trigger, 0x00013A68) fn_10_13750 pushes it with
// lbl_10_rodata_4BC = 25.0f across and lbl_10_rodata_3E0 = 2.0f up (0x00013A90).
// @ fn_10_13998
void CDarkSamus::ApplyMeleeDamage(CStateManager& mgr) {
  if (mMeleeContact.mHit == 1) {
    return;
  }
  mgr.ApplyDamage(GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(),
                  mData.mMeleeAttackDamage,
                  CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList()),
                  CVector3f::Zero());
  if (!PlayerIsMorphball(mgr, CTriggerData(0))) {
    PushPlayer(mgr, 25.f, 2.f);
  }
  mMeleeContact.mHit = 1;
}

// fn_10_13AB4 (0x00013AB4, 0x388 bytes): the melee pulse. From 1.0f above the actor
// (lbl_10_rodata_3E8, 0x00013AFC..0x00013B10) a 20.0f (lbl_10_rodata_4C0) ray straight down finds
// the floor (BuildNearList 0x00013C28, RayWorldIntersection 0x00013C68) through the static filter
// lbl_10_bss_F8 built once (0x00013B30..0x00013BF0): include kMT_Solid / kMT_Platform (data CA0 /
// CA4 = 59 / 49), exclude kMT_Character / kMT_Player / kMT_ProjectilePassthrough (CA8 / CAC /
// CB0 = 33 / 32 / 20), type 3. The explosion "Dark Samus Melee Pulse" (lbl_10_rodata_560 + 0x53A)
// takes the actor's transform moved to the root bone 0x8FC/0x900 at the hit's height (the root's
// own when the ray misses, 0x00013C7C) plus lbl_10_rodata_4B8 = 1.9f (0x00013CAC), the model's
// scale (0x60, 0x00013CF8), white and player -1 (0x00013D98); attack 2 takes the record's pART
// token 0x9D0, the rest meleeAttackFX 0x9C4 (0x00013D7C). Its uid goes to the record's 0x9DC
// (0x00013DFC). Port limit: a token whose asset was invalid is never built, so no explosion.
// Compiler copies, no code of their own here: fn_10_2C8 (0x00013C74) is the module's
// CRayCastResult copy constructor, fn_10_2040 (0x00013DD8) and the string's
// internal_dereference (0x00013DE8) destroy the new-expression's temporaries.
// Run (darksamus_attacks.py): every arm, the ray's miss with key 21 (the actor's position 0x54
// moved 1000 up and 100000 across; ALL-ARMS on bec660618, dsfsm2-runs/s11/a1).
// @ fn_10_13AB4
void CDarkSamus::SpawnMeleePulse(CStateManager& mgr) {
  const CVector3f& pos = GetTranslation();
  const CVector3f start(pos.GetX() + 0.f, pos.GetY() + 0.f, pos.GetZ() + 1.f);
  const float rootX = mRootPos.GetX();
  const float rootY = mRootPos.GetY();
  float floorZ = mRootPos.GetZ();
  // The filter is built each call: retail's once-built static holds the same value.
  const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid, kMT_Platform),
      CMaterialList(kMT_Character, kMT_Player, kMT_ProjectilePassthrough));
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, start, CVector3f::Down(), 20.f, filter, nullptr);
  TUniqueId hitId = kInvalidUniqueId;
  const CRayCastResult hit =
      mgr.RayWorldIntersection(hitId, start, CVector3f::Down(), 20.f, filter, nearList);
  if (hit.IsValid()) {
    floorZ = hit.GetPoint().GetZ();
  }
  CTransform4f xf = GetTransform();
  xf.SetTranslation(CVector3f(rootX + 0.f, rootY + 0.f, floorZ + 1.9f));
  const rstl::optional_object< TLockedToken< CGenDescription > >& token =
      mCurrentAttack == 2 ? mMeleePartToken : mMeleeAttackFXToken;
  if (!token) {
    return;
  }
  const CVector3f scale = GetModelData()->GetScale();
  const rstl::string name = rstl::string_l("Dark Samus Melee Pulse");
  const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId);
  const TUniqueId uid = mgr.AllocateUniqueId();
  CExplosion* explosion =
      rs_new CExplosion(*token, uid, info, name, xf, 0, scale, CColor::White(), -1);
  mgr.AddObject(explosion);
  mMeleeContact.mHitUid = explosion->GetUniqueId();
}

// fn_10_1353C (0x0001353C, 0x140 bytes): the missile. The player's aim position (its vtable
// 0x54) led by a random time in [0, distance / 25.0f] (fn_10_17CEC, lbl_10_rodata_4BC, Range at
// 0x00013580); the gun locator 0x8CC turned to look at it with its own up (0x00013604); then
// LaunchProjectile with 5 at most, no attributes, not homing, an empty visor effect (fn_10_4EB4)
// and scale 1.0f (0x00013650). The info is ProjectileInfo's for the attack (0xA0C).
// @ fn_10_1353C
void CDarkSamus::FireMissile(CStateManager& mgr) {
  CPlayer* player = mgr.GetPlayer(0);
  const float lead = mgr.Random()->Range(0.f, GetDistanceToPlayer(mgr) / 25.f);
  const CVector3f aim = player->GetAimPosition(mgr, lead);
  CTransform4f xf = mGunLctrXf;
  xf = CTransform4f::LookAt(xf.GetTranslation(), aim, xf.GetUp());
  LaunchProjectile(xf, mgr, 5, 0, false, CImpactVisorEffect::None(), CVector3f(1.f, 1.f, 1.f));
}

// fn_10_133D0 (0x000133D0, 0x12C bytes): the super missile, as fn_10_1353C but aimed at the
// player's position, 1.9f (lbl_10_rodata_4B8) higher unless it is morphed (0x38C == 1,
// 0x0001340C); the info is 0xA4C.
// @ fn_10_133D0
void CDarkSamus::FireSuperMissile(CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer(0);
  CVector3f aim = player->GetTranslation();
  if (player->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
    aim = CVector3f(aim.GetX() + 0.f, aim.GetY() + 0.f, aim.GetZ() + 1.9f);
  }
  CTransform4f xf = mGunLctrXf;
  xf = CTransform4f::LookAt(xf.GetTranslation(), aim, xf.GetUp());
  LaunchProjectile(xf, mgr, 5, 0, false, CImpactVisorEffect::None(), CVector3f(1.f, 1.f, 1.f));
}

// fn_10_9164 (0x00009164, 0x1B0 bytes): the freeze beam, only with a token (0xA90, 0x0000918C).
// Aimed as fn_10_133D0 (lbl_10_bss_124 = 1.9f, set by the static init fn_10_CDB8) from the gun
// locator; a CFreezeBeamProjectile (0x000092DC) of the token 0xA88, weapon type 10 (kWT_Phazon,
// `li r5, 0xa`), excluding kMT_ProjectilePassthrough (`li r7, 0x14`), the damage 0xA94
// (freezeBeamDamage), a new uid, the actor's area, owned by the actor, freeze 5.0f / ice bound
// 4.0f (lbl_10_rodata_274 / 278), the sound 0xEE0 and particle 0xEDC (iceSpreadSound /
// iceSpreadFX, the constructor at 0x00020174) and the steam texture 0xAB4 (freezeBeamVisorTexture).
// @ fn_10_9164
void CDarkSamus::FireFreezeBeam(CStateManager& mgr) {
  if (!mFreezeBeamToken) {
    return;
  }
  const CPlayer* player = mgr.GetPlayer(0);
  CVector3f aim = player->GetTranslation();
  if (player->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
    aim = CVector3f(aim.GetX() + 0.f, aim.GetY() + 0.f, aim.GetZ() + 1.9f);
  }
  CTransform4f xf = mGunLctrXf;
  xf = CTransform4f::LookAt(xf.GetTranslation(), aim, xf.GetUp());
  const ushort sfx = mData.mIceSpreadSound == -1 ? CSfxManager::kInternalInvalidSfxId
                                                 : static_cast< ushort >(mData.mIceSpreadSound);
  // The new's null check (0x000092E4) is left out: the host's new does not answer null.
  mgr.AddObject(rs_new CFreezeBeamProjectile(
      *mFreezeBeamToken, kWT_Phazon, xf, kMT_ProjectilePassthrough, mData.mFreezeBeamDamage,
      mgr.AllocateUniqueId(), GetCurrentAreaId(), GetUniqueId(), 5.f, 4.f, sfx, mData.mIceSpreadFX,
      mData.mFreezeBeamVisorTexture));
}

// fn_10_1863C (0x0001863C, 0x2D0 bytes): the flash quad, one unit in front of the camera (the
// view's translation plus its forward column, 0x000186CC..0x00018710), its right and up columns
// scaled by size * alpha (0x000186BC); identity model matrix, additive blend (One / One, 0x00018728),
// Tev stage 0 modulate and stage 1 passthru, depth test off with kE_Always and no writes
// (0x0001875C), the colour (alpha, alpha, alpha, alpha) and a triangle fan (0xA0) from
// (0,0) at C-R+U, (1,0) at C-R-U, (1,1) at C+R-U, (0,1) at C+R+U.
// Run (darksamus_uevent.py, dsfsm2-runs/s10 u4+u9+u10): every line (key 55's flash window under
// `visor dark`, through Render 0x00018A98 and RenderFlash).
// @ fn_10_1863C
void CDarkSamus::DrawFlashQuad(float alpha, float size) const {
  const CTransform4f& view = CGraphics::GetViewMatrix();
  const float scale = size * alpha;
  const CVector3f center(view.Get03() + view.Get01(), view.Get13() + view.Get11(),
                         view.Get23() + view.Get21());
  const CVector3f right(scale * view.Get00(), scale * view.Get10(), scale * view.Get20());
  const CVector3f up(scale * view.Get02(), scale * view.Get12(), scale * view.Get22());
  CGraphics::SetModelMatrix(CTransform4f::Identity());
  CGraphics::SetBlendMode(kBM_Blend, kBF_One, kBF_One, kLO_Clear);
  CGraphics::SetTevOp(kTS_Stage0, CGraphics::kEnvModulate);
  CGraphics::SetTevOp(kTS_Stage1, CGraphics::kEnvPassthru);
  CGraphics::SetDepthWriteMode(false, kE_Always, false);
  CGraphics::StreamColor(CColor(alpha, alpha, alpha, alpha));
  CGraphics::StreamBegin(kP_TriangleFan);
  CGraphics::StreamTexcoord(0.f, 0.f);
  CGraphics::StreamVertex(center - right + up);
  CGraphics::StreamTexcoord(1.f, 0.f);
  CGraphics::StreamVertex(center - right - up);
  CGraphics::StreamTexcoord(1.f, 1.f);
  CGraphics::StreamVertex(center + right - up);
  CGraphics::StreamTexcoord(0.f, 1.f);
  CGraphics::StreamVertex(center + right + up);
  CGraphics::StreamEnd();
}

// fn_10_1890C (0x0001890C, 0x58 bytes): with the flash texture (0xDFC, 0x00018920) load it on
// GX_TEXMAP0, repeating (0x00018938), and draw the quad at the fade factor (fn_10_185CC) and size
// 0xE00 (0x0001894C); without it nothing.
// Run (darksamus_uevent.py, dsfsm2-runs/s11/u8): every arm (key 68 no texture).
// @ fn_10_1890C
void CDarkSamus::RenderFlash() const {
  if (!mFlashTexture) {
    return;
  }
  (*mFlashTexture)->Load(GX_TEXMAP0, CTexture::kCM_Repeat);
  DrawFlashQuad(GetFadeFactor(), mFlashSize);
}

// ---- dsfsm2 slice 9: the phazon volley ----

// fn_10_5840 (0x00005840, 0x80 bytes): one missile of the phazon volley while 0xFB4 has any left
// (0x0000585C, counted down at 0x00005864): 0xFB8 0 the phazon shot fn_10_50B8, which starts the
// volley's clock 0xFCC at the elapsed time 0x8B0 if it is below 0.0f (0x00005898); 1 the phazon
// super missile fn_10_4C40; any other value nothing.
// @ fn_10_5840
void CDarkSamus::FirePhazonVolley(CStateManager& mgr) {
  if (mGuestFB4 <= 0) {
    return;
  }
  --mGuestFB4;
  switch (mGuestFB8) {
  case 0:
    FirePhazonShot(mgr);
    if (mGuestFCC < 0.f) {
      mGuestFCC = mElapsedTime;
    }
    break;
  case 1:
    FirePhazonSuperMissile(mgr);
    break;
  default:
    break;
  }
}

// fn_10_50B8 (0x000050B8, 0x788 bytes): a phazon shot. The dummy-target list 0x934 is padded with
// kInvalidUniqueId to 12 (0x00005110). The shot leaves the root bone 0x8FC moved by a random
// (x, y) in [-1.2, 1.2] (lbl_10_rodata_154 / 158, 0x000051E8 / 0x00005204) and up by 1.2 less
// that offset's length (0x00005334), turned to look along the actor's flat forward plus 1.5 the
// offset's direction plus 3 up (lbl_10_rodata_14C / 160 / 15C, LookAt from the zero vector at
// 0x0000530C, SetRotation 0x00005318; the actor's own transform with the translation 1.3 times the
// model's z scale up, 0x0000513C, is overwritten). It homes on the player unless the player orbits
// the actor (its 0x3B0, 0x00005378); then the first free slot of 0x934 gets a new "DS3 Dummy
// Target" CActor (lbl_10_rodata_1F0 + 0x2A, material kMT_Target = lbl_10_data_1AC 40, no model) at
// the player moved -4..4 (lbl_10_rodata_164 / 168) across the flat direction from the actor
// ((1, 0, 0) when that has no length, 0x0000543C) and 0.8 (lbl_10_rodata_16C) up, and the shot
// homes on the list's first entry (0x00005648). Then, as CPatterned::LaunchProjectile: the info
// cached (0x00005698), CanCreateProjectile kWT_AI with 14 (0x000056D4), a CEnergyProjectile
// excluding kMT_Character, scale one, an empty visor effect (0x000057EC). Port limit: an info whose
// asset was invalid is not built on the host, so no shot. The new's null check is left out.
// Run (darksamus_attacks.py keys 10..28): every arm; forced: the orbit (16), the full dummy list
// (17), the flat forward (18), the player on the actor (25), the zero side vector (26, loc:), the
// uncached (27) and absent (28) info, and CanCreateProjectile's limit (16 shots in one frame).
// @ fn_10_50B8
void CDarkSamus::FirePhazonShot(CStateManager& mgr) {
  while (mGuest930 < 12) {
    mGuest934[mGuest930] = kInvalidUniqueId.value;
    ++mGuest930;
  }
  CTransform4f xf = GetTransform();
  CVector3f forward(xf.Get01(), xf.Get11(), 0.f);
  if (forward.CanBeNormalized() == true) {
    forward.Normalize();
  }
  const float offsetX = mgr.Random()->Range(-1.2f, 1.2f);
  const float offsetY = mgr.Random()->Range(-1.2f, 1.2f);
  CVector3f side(offsetX, offsetY, 0.f);
  if (side.CanBeNormalized() == true) {
    side.Normalize();
  }
  const CVector3f target = forward * 1.f + side * 1.5f + CVector3f::Up() * 3.f;
  xf.SetRotation(CTransform4f::LookAt(CVector3f::Zero(), target, CVector3f::Up()));
  const float rise = 1.2f - CVector3f(offsetX, offsetY, 0.f).Magnitude();
  xf.SetTranslation(CVector3f(mRootPos.GetX() + offsetX, mRootPos.GetY() + offsetY,
                              mRootPos.GetZ() + rise));

  TUniqueId homingTarget = kInvalidUniqueId;
  CPlayer* player = mgr.GetPlayer(0);
  if (player->GetOrbitTargetId() != GetUniqueId()) {
    homingTarget = player->GetUniqueId();
  } else {
    for (int i = 0; i < mGuest930; ++i) {
      if (mGuest934[i] != kInvalidUniqueId.value) {
        continue;
      }
      const TUniqueId uid = mgr.AllocateUniqueId();
      mGuest934[i] = uid.value;
      CTransform4f dummyXf = player->GetTransform();
      const CVector3f& pos = GetTranslation();
      CVector3f dir(dummyXf.Get03() - pos.GetX(), dummyXf.Get13() - pos.GetY(), 0.f);
      if (dir.CanBeNormalized() == true) {
        dir.Normalize();
      } else {
        dir = CVector3f(1.f, 0.f, 0.f);
      }
      const float across = mgr.Random()->Range(-4.f, 4.f);
      dummyXf.SetTranslation(CVector3f(dummyXf.Get03() + across * dir.GetY(),
                                       dummyXf.Get13() + across * -dir.GetX(),
                                       dummyXf.Get23() + (across * 0.f + 0.8f)));
      const rstl::string name = rstl::string_l("DS3 Dummy Target");
      const CEntityInfo info(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                             kInvalidEditorId);
      const CModelData model;
      const CMaterialList material(kMT_Target);
      const CActorParameters params;
      mgr.AddObject(rs_new CActor(uid, name, info, 0, dummyXf, model, material, params,
                                  kInvalidUniqueId));
      break;
    }
    // Retail tests the target for kInvalidUniqueId here (0x00005640), which it always is.
    homingTarget = TUniqueId(mGuest934[0]);
  }

  CProjectileInfo* info = ProjectileInfo();
  if (info == nullptr || !info->Token().TryCache()) {
    return;
  }
  if (!mgr.CanCreateProjectile(GetUniqueId(), kWT_AI, 14)) {
    return;
  }
  const CImpactVisorEffect visorEffect = CImpactVisorEffect::None();
  const CVector3f scale(1.f, 1.f, 1.f);
  const TUniqueId uid = mgr.AllocateUniqueId();
  mgr.AddObject(rs_new CEnergyProjectile(true, info->Token(), kWT_AI, xf, kMT_Character,
                                         info->GetDamage(), uid, GetCurrentAreaId(), GetUniqueId(),
                                         homingTarget, 0, false, scale, visorEffect, false, true,
                                         false, 1.f, 4.f, 4.f));
}

// fn_10_4C40 (0x00004C40, 0x15C bytes): the phazon super missile, from 5.3 (lbl_10_rodata_144)
// above the root bone 0x8FC, looking at the player's aim position (its vtable 0x54 with no lead,
// 0x00004CD8) 0.5 lower (lbl_10_rodata_148) with the world's up (0x00004D28); LaunchProjectile with
// 14 at most, no attributes, homing, an empty visor effect and scale 1.0f (0x00004D74). The info
// is ProjectileInfo's 0x1064.
// @ fn_10_4C40
void CDarkSamus::FirePhazonSuperMissile(CStateManager& mgr) {
  const CVector3f from(mRootPos.GetX() + 0.f, mRootPos.GetY() + 0.f, mRootPos.GetZ() + 5.3f);
  const CVector3f aim = mgr.GetPlayer(0)->GetAimPosition(mgr, 0.f);
  const CVector3f to(aim.GetX() + 0.f, aim.GetY() + 0.f, aim.GetZ() + -0.5f);
  CTransform4f xf = GetTransform();
  xf = CTransform4f::LookAt(from, to, CVector3f::Up());
  LaunchProjectile(xf, mgr, 14, 0, true, CImpactVisorEffect::None(), CVector3f(1.f, 1.f, 1.f));
}

// ---- dsfsm2 slice 9: the boost ball's spawn ----

namespace {

// fn_10_1B24 (0x00001B24, 0x40 bytes): the cross product of two vectors - the x is
// a.y * b.z - b.y * a.z (the `fmsubs` at 0x00001B44), the y a.z * b.x - b.z * a.x (0x00001B4C)
// and the z a.x * b.y - b.x * a.y (0x00001B54); the host's CVector3f::Cross is the same
// formula.
// Class H (ds41): the REL's out-of-line copy of the header inline CVector3f::Cross
// (include/Kyoto/Math/CVector3f.hpp:60), called from two classes' bodies (fn_10_10A64 at
// 0x00010B34, the scatter shot's fn_10_E7C at 0x00001934); the host's inline is the body.
void CrossProduct(CVector3f& out, const CVector3f& a, const CVector3f& b) {
  out = CVector3f::Cross(a, b);
}



} // namespace


// ---- dsball: the module's boost ball actor (vtable lbl_10_data_104, 0x358 bytes) ----

namespace {
// lbl_10_bss_5C: the module's name, built by its static initializer fn_10_44D0 (0x000044EC) from
// lbl_10_rodata_A4 + 0x2A; the ball's CRELFileToken (0x34C) holds it.
const rstl::string skRELName = rstl::string_l("DarkSamus.rel");

// fn_10_4368 (0x00004368, 0x40 bytes): default actor parameters (the CActorParameters ctor at
// 0x00004380) hot in thermal (`li r5, 0x1`, HotInThermal at 0x00004390). Branch-free; the
// boost ball's two ctors call it (fn_10_3BC4 at 0x00003D5C, fn_10_3FD8 at 0x00004160).
// @ fn_10_4368
CActorParameters BoostBallActorParameters() { return CActorParameters().HotInThermal(true); }
} // namespace

// fn_10_3FD8 (0x00003FD8, 0x390 bytes) and fn_10_3BC4 (0x00003BC4, 0x3AC bytes): the
// CPhysicsActor "Dark Samus BoostBall" (lbl_10_rodata_A4 + 0x15) with the materials kMT_Character /
// Target / Orbit / RadarObject (lbl_10_data_E4..F0), the box (-0.2, 0.2) (lbl_10_rodata_9C / 98),
// mass 0.75 (lbl_10_rodata_A0, fn_10_3F70), the actor parameters hot in thermal (fn_10_4368) and
// the default step data. The Dark Samus's touch bounds go to 0x2D0, the hit sound to 0x2F0, the
// damage info to 0x2F4, the vulnerability to 0x31C (fn_10_3744, the copy ctor); 0x310 and 0x314 are
// 0.0f, 0x354 null and the bit 0x40 of 0x318 clear (0x00003ED0 / 0x000042D0). fn_10_3FD8 has the
// colour Black and the fade 0.0f (lbl_10_rodata_7C) with the bit 0x80 clear (0x000042C4);
// fn_10_3BC4 takes the fade and the colour and sets the bit 0x80 (the `rlwimi` at 0x00003EC4).
// The CRELFileToken at 0x34C takes "DarkSamus.rel" (lbl_10_bss_5C) with the mode 0 (0x00003EEC /
// 0x000042EC). The helpers: fn_10_4368 (BoostBallActorParameters) is
// CActorParameters().HotInThermal(true), fn_10_3F70 the SMoverData constructor (CAxisAngle::Identity at 0x00003D60 / 0x00004164 is its
// angular velocity), fn_10_2040 / fn_10_2098 the CEntityInfo temporary's destructor (its
// connection vector) and internal_dereference the name string's.
// Both end with the default animation set looping (0x00003F40 / 0x00004340) and EnableLooping.
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): both ctors (the
// fn_10_9E00 0xD14 key 1 and 0, the 0.5 forced on the first spawn in run 3).
// @ fn_10_3FD8
// @ fn_10_3BC4
CDarkSamusBoostBall::CDarkSamusBoostBall(TUniqueId uid, TAreaId area, const CTransform4f& xf,
                                         const CModelData& model, int defaultAnim,
                                         const CAABox& bounds, ushort hitSfx,
                                         const CDamageInfo& damage,
                                         const CDamageVulnerability& vulnerability,
                                         bool silhouette, float fade, const CColor& color)
: CPhysicsActor(uid, rstl::string_l("Dark Samus BoostBall"),
                CEntityInfo(area, CEntity::NullConnectionList, true, kInvalidEditorId), 0, xf,
                model, CMaterialList(kMT_Character, kMT_Target, kMT_Orbit, kMT_RadarObject),
                CAABox(CVector3f(-0.2f, -0.2f, -0.2f), CVector3f(0.2f, 0.2f, 0.2f)),
                SMoverData(0.75f),                      // fn_10_3F70
                BoostBallActorParameters(),             // fn_10_4368
                CPhysicsActor::skDefaultStepData)       // fn_10_2040, fn_10_2098
, mBounds(bounds)
, mSilhouetteColor(silhouette ? color : CColor::Black())
, mSilhouetteFade(silhouette ? fade : 0.f)
, mHitSfx(hitSfx)
, mDamage(damage)
, mHitCooldown(0.f)
, mDamageTaken(0.f)
, mSilhouette(silhouette) // 0x318 0x80: `rlwimi` at 0x00003EC4 sets it, 0x000042C4 clears it
, mDamaged(false)        // 0x318 0x40: cleared at 0x00003ED0 / 0x000042D0
, mVulnerability(vulnerability) // fn_10_3744
, mRelToken(skRELName, 0)       // 0x00003EEC / 0x000042EC
, mCollisionManager(nullptr) {
  if (CAnimData* anim = AnimationData()) {
    anim->SetAnimation(CAnimPlaybackParms(defaultAnim, -1, 1.f, true), false); // 0x00004340
    ModelData()->EnableLooping(true);                                           // 0x0000434C
  }
}

// fn_10_2E64 (0x00002E64, 0x98 bytes): the collision manager is deleted (its deleting destructor
// at 0x00002EA8; the two `addic.` at 0x00002E88 / 0x00002E98 test the member's address, inlined
// from its owning pointer's destructor), then the CRELFileToken (0x00002EB4), the vulnerability
// and the base. The other two conditions (this null at 0x00002E80, the delete flag at 0x00002ED0)
// and CMemory::Free (0x00002EDC) are the compiler's deleting-destructor wrapper.
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): 18 hits, no
// branch.
// @ fn_10_2E64
CDarkSamusBoostBall::~CDarkSamusBoostBall() { delete mCollisionManager; }

// fn_10_2EFC (0x00002EFC, 8 bytes): the float at 0x314 (`lfs f1, 0x314(r3)`), the damage the
// ball's collision actor took (fn_10_3998's kSM_Damage arm).
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): read once per
// damaged roll (50 in run 3, 3000 in run 4).
// @ fn_10_2EFC
float CDarkSamusBoostBall::GetDamageTaken() const { return mDamageTaken; }

// fn_10_2F04 (0x00002F04, 0xC bytes): the bit 0x40 of the byte 0x318 (the `lbz r0, 0x318(r3)`
// at 0x00002F04, then `extrwi r3, r0, 1, 25` at 0x00002F08: bit 25 = 1u << 6), set by
// fn_10_3998's kSM_Damage arm.
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): ret 0 and ret 1.
// @ fn_10_2F04
bool CDarkSamusBoostBall::HasBeenDamaged() const { return mDamaged; }

// fn_10_2F10 (0x00002F10, 0xF8 bytes): the touch damage. Nothing while the cooldown 0x310 is above
// 0.0f (`bgt` at 0x00002F3C, lbl_10_rodata_7C); else the hit sound 0x2F0 at the ball (the area at
// +0x4, AddEmitter at 0x00002F60), the damage info 0x2F4 from the ball to the player (mgr+0x14FC)
// with the include list kMT_Solid (lbl_10_data_E0 = 0x3B) and type 3 (kFT_IncludeExclude), and
// the cooldown becomes 1.0f (lbl_10_rodata_80, 0x00002FF0).
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): the cooldown key 1
// and 0 (the ball rolled into the player).
// @ fn_10_2F10
void CDarkSamusBoostBall::ApplyTouchDamage(CStateManager& mgr) {
  if (mHitCooldown > 0.f) { // 0x00002F3C
    return;
  }
  CSfxManager::AddEmitter(mHitSfx, GetTranslation(), GetCurrentAreaId().Value(), false, false,
                          CSfxManager::kMedPriority); // 0x00002F60
  mgr.ApplyDamage(GetUniqueId(), mgr.GetPlayer(0)->GetUniqueId(), GetUniqueId(), mDamage,
                  CMaterialFilter(CMaterialList(kMT_Solid), CMaterialList(),
                                  CMaterialFilter::kFT_IncludeExclude),
                  CVector3f::Zero()); // 0x00002FE4
  mHitCooldown = 1.f;                 // 0x00002FF0
}

// fn_10_3008 (0x00003008, 0x16C bytes): the silhouette pass runs with the bit 0x80 of 0x318
// (`extrwi r0, r0, 1, 24` at 0x0000302C, `bne` at 0x00003034); the pass: the screen to the spare
// buffer (CopyScreenTex(3, true, mpSpareBuffer, 4 = GX_TF_RGB565, false)), the destination alpha on
// at 0xFF, the model matrix the transform times the model's scale (+0x60), the base's Render, then
// the renderer's silhouette at the fade 0x2EC with the colour 0x2E8, no texture and (0.0f, 0.0f,
// 0.0f, White) (lbl_10_rodata_7C). The base's Render runs again after it, with or without the pass
// (0x0000314C). Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): the
// silhouette key 0 and 1.
// @ fn_10_3008
void CDarkSamusBoostBall::Render(const CStateManager& mgr) const {
  if (mSilhouette) { // 0x00003034
    CCubeRenderer* render = gpRender;
    render->CopyScreenTex(3, true, CGraphics::GetDolphinSpareBuffer(), GX_TF_RGB565, false);
    CGX::SetDstAlpha(true, 0xFF);
    CTransform4f xf = GetTransform();
    const CVector3f& scale = GetModelData()->GetScale();
    xf = xf * CTransform4f::Scale(scale.GetX(), scale.GetY(), scale.GetZ());
    gpRender->SetModelMatrix(xf);
    CPhysicsActor::Render(mgr);
    render->RenderSilhouette(mSilhouetteFade, mSilhouetteColor,
                             rstl::optional_object< TCachedToken< CTexture > >(), 0.f, 0.f, 0.f,
                             CColor::White());
  }
  CPhysicsActor::Render(mgr); // 0x0000314C
}

// fn_10_3174 (0x00003174, 0x70 bytes): with the bit 0x80 of 0x318 and the combat visor
// (GetActiveVisor == 0) the renderer's phazon suit mask texture is allocated; then the base's
// PreRender.
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): no silhouette, the
// silhouette with the combat visor and with the scan one (run 5: 167 hits, 79 allocations).
// @ fn_10_3174
void CDarkSamusBoostBall::PreRender(CStateManager& mgr) {
  if (mSilhouette && mgr.GetPlayerState()->GetActiveVisor(mgr) == CPlayerState::kPV_Combat) {
    gpRender->AllocatePhazonSuitMaskTexture();
  }
  CActor::PreRender(mgr);
}

// fn_10_31E4 (0x000031E4, 0x304 bytes): the ball's collision actor. Its vulnerability is the
// reflect one (ImmuneRicochetVulnerability) with the charged and the combo vulnerability of the
// four beams set to (1.0f, kE_Normal, false) (`cmpwi r28, 0x4` at 0x00003270). One sphere
// (reserve 1) at the locator "Skeleton_Root" with the radius 2.3f (lbl_10_rodata_84) and the mass
// 100000.0f (lbl_10_rodata_88); the manager is owned by the ball (0x0000333C) and replaces 0x354.
// The actor's knockback resistance and health are 100000.0f (+0x8 / +0x4, 0x000033E0), it takes
// the vulnerability and goes to the ball's position plus -10.0f (lbl_10_rodata_8C) times the
// transform's second column (0x28/0x38/0x48); the manager is set active (0x00003434) and the
// actor's filter excludes kMT_Platform as well (`lis r0, 0x2` OR'd into the exclude list's high
// word at 0x00003464, 1 << 49).
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): one per ball, no
// branch but the loop.
// @ fn_10_31E4
void CDarkSamusBoostBall::CreateCollisionActors(CStateManager& mgr) {
  // ImmuneRicochetVulnerability (0x00003208) is the retail name of ReflectVulnerabilty (both
  // 0x800DBB40); the copy is fn_10_3744.
  CDamageVulnerability vuln(CDamageVulnerability::ReflectVulnerabilty()); // fn_10_3744
  const CWeaponTypeVulnerability normal(1.f, CWeaponTypeVulnerability::kE_Normal, false);
  for (int i = 0; i < 4; ++i) { // 0x00003270
    vuln.SetChargedVulnerability(i, normal);
    vuln.SetComboVulnerability(i, normal);
  }
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(1); // fn_10_43A8
  // push_back is fn_10_34E8, the description's copy in it fn_10_3568.
  joints.push_back(CJointCollisionDescription::SphereCollision( // fn_10_34E8, fn_10_3568
      GetModelData()->GetAnimationData()->GetLocatorSegId(rstl::string_l("Skeleton_Root")),
      CVector3f::Zero(), 2.3f, rstl::string_l("Skeleton_Root"), 100000.f));
  CCollisionActorManager* manager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, true);
  // The two "Skeleton_Root" strings' destructors are internal_dereference (0x000032C0).
  delete mCollisionManager;
  mCollisionManager = manager;
  // The cast's result is used unchecked (0x000033C0): the manager always makes the actor.
  CCollisionActor* colAct = TCastToPtr< CCollisionActor >(
      mgr.ObjectById(manager->GetCollisionDescFromIndex(0).GetCollisionActorId()));
  colAct->HealthInfo()->SetKnockbackResistance(100000.f); // 0x000033DC (vtable 0x38)
  colAct->HealthInfo()->SetHP(100000.f);                  // 0x000033E0
  colAct->SetDamageVulnerability(vuln);
  const CTransform4f& xf = GetTransform();
  const CVector3f& pos = GetTranslation();
  colAct->SetTranslation(CVector3f(pos.GetX() + -10.f * xf.Get01(),
                                   pos.GetY() + -10.f * xf.Get11(),
                                   pos.GetZ() + -10.f * xf.Get21())); // 0x00003424
  mCollisionManager->SetActive(mgr, true);                             // 0x00003434
  CMaterialFilter filter = colAct->GetMaterialFilter();
  filter.ExcludeList().Add(kMT_Platform); // 0x00003464
  colAct->SetMaterialFilter(filter); // then fn_10_3628, the joints' destructor (0x000034B8)
}

// fn_10_3B08 (0x00003B08, 0xBC bytes): the base's Think; the collision actor is built on the first
// one (0x354 null, 0x00003B3C); the manager updates in object space (0), the animation advances at
// twice the rate (lbl_10_rodata_94 = 2.0f) and the hit cooldown 0x310 counts down to 0.0f (`bge`
// at 0x00003B94).
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): the manager null
// and set, the cooldown above 0 and clamped.
// @ fn_10_3B08
void CDarkSamusBoostBall::Think(float dt, CStateManager& mgr) {
  CActor::Think(dt, mgr);
  if (mCollisionManager == nullptr) { // 0x00003B3C
    CreateCollisionActors(mgr);
  }
  mCollisionManager->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  UpdateAnimation(2.f * dt, mgr, true);
  mHitCooldown -= dt;
  if (mHitCooldown < 0.f) { // 0x00003B94 `bge` to the fmr of the value (a NaN is kept)
    mHitCooldown = 0.f;
  }
}

// fn_10_3998 (0x00003998, 0x170 bytes): kSM_Delete (0x000039E0) destroys and deletes the collision
// manager when there is one; kSM_HitObject (0x000039F4) runs the touch damage fn_10_2F10 when the
// sender is a collision actor whose last touched object is the player (mgr+0x14FC, 0x00003A60);
// kSM_Damage (0x000039D0) sets the bit 0x40 of 0x318 (0x00003A80) and, for a collision actor
// sender, adds 100000.0f (lbl_10_rodata_88) minus its health (vtable 0x3C, +0x4) to 0x314
// (0x00003AD4). The base's AcceptScriptMsg follows every message (0x00003AE4).
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): kSM_Delete with
// the manager and without (forced once: FORCE "3998 delete null"), kSM_HitObject from the player's
// touch, from another toucher and from no sender (`send u0702 HitObject`), kSM_Damage from the
// collision actor (`damage u0703 <n> darkburst`) and from no sender (`send u0702 Damage`), other
// messages.
// @ fn_10_3998
void CDarkSamusBoostBall::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  switch (msg.GetMessage()) {
  case kSM_Delete: // 0x000039E0
    if (mCollisionManager != nullptr) {
      mCollisionManager->Destroy(mgr);
      delete mCollisionManager;
      mCollisionManager = nullptr;
    }
    break;
  case kSM_HitObject: // 0x000039F4
    if (CCollisionActor* colAct =
            TCastToPtr< CCollisionActor >(mgr.ObjectById(msg.GetSenderId()))) {
      if (colAct->GetLastTouchedObject() == mgr.GetPlayer(0)->GetUniqueId()) { // 0x00003A60
        ApplyTouchDamage(mgr); // fn_10_2F10, 0x00003A70
      }
    }
    break;
  case kSM_Damage: // 0x000039D0
    mDamaged = true; // 0x00003A80
    if (CCollisionActor* colAct =
            TCastToPtr< CCollisionActor >(mgr.ObjectById(msg.GetSenderId()))) {
      mDamageTaken += 100000.f - colAct->GetHealthInfo()->GetHP(); // 0x00003AD4 (vtable 0x3C)
    }
    break;
  default:
    break;
  }
  CActor::AcceptScriptMsg(mgr, msg); // 0x00003AE4
}

// fn_10_3934 (0x00003934, 0x64 bytes) / fn_10_38D0 (0x000038D0, 0x64 bytes): the base's orbit /
// aim position raised by 0.8f (lbl_10_rodata_90) in z.
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): no branch.
// @ fn_10_3934
CVector3f CDarkSamusBoostBall::GetOrbitPosition(const CStateManager& mgr) const {
  return CPhysicsActor::GetOrbitPosition(mgr) + CVector3f(0.f, 0.f, 0.8f);
}

// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): no branch.
// @ fn_10_38D0
CVector3f CDarkSamusBoostBall::GetAimPosition(const CStateManager& mgr, float dt) const {
  return CPhysicsActor::GetAimPosition(mgr, dt) + CVector3f(0.f, 0.f, 0.8f);
}

// fn_10_9E00 (0x00009E00, 0x26C bytes): the old ball deleted (fn_10_9AC4, 0x00009E24) and its
// effects stopped (fn_10_99B8, 0x00009E30), a new uid to 0xE38 (0x00009E4C), and the ball built at
// the actor's transform with the z column set to (0, 0, 1) (lbl_10_rodata_25C / 264, 0x00009E6C)
// from the CAnimRes of the boost ball model (the 0xE10 sub-object's 0xE2C / 0xE30 / 0xE34: the
// data's mBoostBallModel), the actor's model scale and looping (0x00009EB8). The ball takes the
// actor's touch bounds (vtable 0x48, 0x00009F0C / 0x00009FA4), the hit sound 0xE6A (`lhz`), the
// damage info 0xE10 and CPatterned's vulnerability (0x00009F2C / 0x00009FC0). 0xD14 == 0.0f picks
// the ctor fn_10_3FD8 (`bne` at 0x00009ED0), else fn_10_3BC4 with the fade 0.5f
// (lbl_10_rodata_330) and fn_10_181E0's colour. Then lighting on (0x0000A024), AddObject
// (0x0000A030) and the swooshes fn_10_9B24 (0x0000A03C).
// Run (dsball, tools/cov/specs/darksamus_ball.py, drivers/darksamus_ball.con): the 0xD14 key 1
// and 0.
// @ fn_10_9E00
void CDarkSamus::SpawnBoostBall(CStateManager& mgr) {
  DeleteBoostBall(mgr); // fn_10_9AC4, 0x00009E24
  StopBoostBallParticles(); // fn_10_99B8, 0x00009E30
  mBoostBallUid = mgr.AllocateUniqueId();
  CTransform4f xf = GetTransform();
  xf.SetColumn(kDZ, CVector3f(0.f, 0.f, 1.f));
  const CAnimationParameters& res = mData.mBoostBallModel;
  const CModelData model(CAnimRes(res.GetACSFile(), res.GetCharacter(), GetModelData()->GetScale(),
                                  res.GetInitialAnimation(), true));
  // The six floats are copied out of the optional unchecked (0x00009F10..); CDarkSamus's
  // GetTouchBounds (fn_10_F5C0) always has a box.
  const CAABox bounds = *GetTouchBounds();
  CDarkSamusBoostBall* ball;
  if (mGuestD14 == 0.f) { // 0x00009ED0
    ball = rs_new CDarkSamusBoostBall(mBoostBallUid, GetCurrentAreaId(), xf, model,
                                      res.GetInitialAnimation(), bounds, mBoostBallHitPlayerSfxId,
                                      mData.mBoostBallDamage,
                                      *CPatterned::GetDamageVulnerability(), false, 0.f,
                                      CColor::Black()); // fn_10_3FD8
  } else {
    ball = rs_new CDarkSamusBoostBall(mBoostBallUid, GetCurrentAreaId(), xf, model,
                                      res.GetInitialAnimation(), bounds, mBoostBallHitPlayerSfxId,
                                      mData.mBoostBallDamage,
                                      *CPatterned::GetDamageVulnerability(), true, 0.5f,
                                      GetSilhouetteColor()); // fn_10_3BC4
  }
  ball->SetCalculateLighting(true);
  mgr.AddObject(ball);
  CreateBoostBallEffects(); // fn_10_9B24
}

// fn_10_10A64 (0x00010A64, 0x158 bytes), Think's (0x0001B330): the head's yaw toward the player.
// The forward (the transform's second column, 0x28/0x38/0x48) and the player's offset from the
// position 0x54 (the player 0x14FC, 0x00010ABC..0x00010ACC), both with z = 0.0f (0x00010ADC,
// 0x00010AE0), normalized when they can be (0x00010AF0, 0x00010B0C); the angle between them
// (GetAngleDiff at 0x00010B20) is negative when their cross's z is below 0.0f (fn_10_1B24 at
// 0x00010B34, `bge` at 0x00010B48, lbl_10_rodata_3EC = -1.0f); 0xD40 is it in degrees
// (lbl_10_rodata_458 = 57.295776f, 0x00010B6C) clamped to lbl_10_rodata_45C = -30.0f (0x00010B78)
// and lbl_10_rodata_460 = 30.0f (0x00010B94).
// Run (darksamus_uevent.py, dsfsm2-runs/s11/u8): every arm (keys 69, 71, 72: no forward and on the
// player, the forward turned both ways).
// @ fn_10_10A64
void CDarkSamus::UpdateHeadAim(CStateManager& mgr) {
  CVector3f forward = GetTransform().GetForward();
  forward.SetZ(0.f);
  const CVector3f& playerPos = mgr.GetPlayer(0)->GetTranslation();
  CVector3f toPlayer(playerPos.GetX() - GetTranslation().GetX(),
                     playerPos.GetY() - GetTranslation().GetY(), 0.f);
  if (forward.CanBeNormalized()) { // 0x00010AF0
    forward.Normalize();
  }
  if (toPlayer.CanBeNormalized()) { // 0x00010B0C
    toPlayer.Normalize();
  }
  float angle = CVector3f::GetAngleDiff(forward, toPlayer);   // 0x00010B20
  CVector3f cross;
  CrossProduct(cross, forward, toPlayer);                      // fn_10_1B24, 0x00010B34
  if (cross.GetZ() < 0.f) {                                    // 0x00010B48
    angle *= -1.f;                                             // 0x00010B54
  }
  mHeadYaw = 57.295776f * angle; // 0x00010B6C
  if (mHeadYaw < -30.f) {        // 0x00010B78
    mHeadYaw = -30.f;
  } else if (mHeadYaw > 30.f) {  // 0x00010B94
    mHeadYaw = 30.f;
  }
}

// fn_10_10864 (0x00010864, 0x17C bytes), PreRender's (0x00018C7C): the head twist. Alive only
// (0x420 bit 0x40, `extrwi.` at 0x00010884) and once per elapsed time (0x8B0 == 0xD48, `fcmpu` at
// 0x00010894): the "Head_1" node's rotation in the pose builder (the anim data 0x2F4, fn_10_10A20
// at 0x000108B8) times an X rotation of lbl_10_rodata_450 = 0.2617994f (15 degrees, 0x000108F4)
// times a Z rotation of 0xD40 degrees (lbl_10_rodata_454 = 0.017453292f, 0x00010938) goes back
// into the node (the products at 0x00010968/0x00010978, fn_10_109E0 at 0x000109A8); the anim
// data's pose-built bit (0x2AC bit 0x2) is cleared (0x000109B4) and 0xD48 takes 0x8B0 (0x000109C0).
// Run (darksamus_uevent.py, dsfsm2-runs/s11/u8): every arm (key 73 dead, 74 the same elapsed time).
// @ fn_10_10864
void CDarkSamus::TwistHead() {
  if (!GetAlive()) { // 0x00010888
    return;
  }
  if (mElapsedTime == mHeadTwistTime) { // 0x00010898
    return;
  }
  CAnimData* anim = ModelData()->AnimationData();
  CHierarchyPoseBuilder& builder = anim->PoseBuilder();
  const CQuaternion rot = builder.GetSegRotation(mHeadSegId);
  const CQuaternion pitch = CQuaternion::XRotation(CRelAngle::FromRadians(0.2617994f));
  const CQuaternion yaw = CQuaternion::ZRotation(CRelAngle::FromRadians(0.017453292f * mHeadYaw));
  builder.Insert(mHeadSegId, rot * pitch * yaw);
  anim->SetPoseBuilt(false);
  mHeadTwistTime = mElapsedTime;
}

// fn_10_12DD0 (0x00012DD0, 0xC0 bytes), PreRender's (0x00018D04, dt 0.0f): the melee pulse
// explosion 0x9DC follows the root 0x8FC/0x900 at its own height (0x5C; SetTranslation at
// 0x00012E5C); a uid that is no longer a CExplosion (TCastToPtr at 0x00012E24) is cleared
// (0x00012E38). Then the boost ball's effects (fn_10_8F50 at 0x00012E6C).
// Run (darksamus_uevent.py, dsfsm2-runs/s11/u8): every arm (the melee pulse followed, then gone).
// @ fn_10_12DD0
void CDarkSamus::UpdateMeleePulseAndEffects(CStateManager& mgr, float dt) {
  if (mMeleeContact.mHitUid != kInvalidUniqueId) { // 0x00012E0C
    CExplosion* explosion = TCastToPtr< CExplosion >(mgr.ObjectById(mMeleeContact.mHitUid));
    if (explosion == nullptr) {                    // 0x00012E2C
      mMeleeContact.mHitUid = kInvalidUniqueId;    // 0x00012E38
    } else {
      explosion->SetTranslation(CVector3f(mRootPos.GetX(), mRootPos.GetY(),
                                          explosion->GetTranslation().GetZ())); // 0x00012E5C
    }
  }
  UpdateBoostBallEffects(mgr, dt);
}

// fn_10_8F50 (0x00008F50, 0x214 bytes): the boost ball's effects at the position plus
// (lbl_10_rodata_25C = 0.0f, 0.0f, lbl_10_rodata_268 = 0.625f) (0x00008F88..0x00008FA4). The
// generator 0xE64 (when set, 0x00008FB0) is turned to the transform's rotation, moved there and
// updated (vtable 0x14/0x18/0xC, 0x00008FD4..0x00009004). With the swoosh 0xE3C (0x00009010):
// alive (0x420 bit 0x40, 0x0000902C) and without 0xEBC bit 0x40 (0x00009038) it and the swoosh
// 0xE4C, when set (0x00009058), move there (vtable 0x18, 0x0000904C/0x0000906C); 0xE3C is scaled by
// lbl_10_rodata_26C = 1.25f (vtable 0x24, 0x000090B4) and updated (0x000090CC); 0xE4C, when set
// (0x000090D8), is scaled by (lbl_10_rodata_270 = 0.2f, 0.2f, 1.25f) (0x00009108), tinted white
// (vtable 0x30, 0x00009128) and updated (0x00009140).
// Runs (darksamus_boostfx.py, ds895c b1/f1/f2): no effects; generator and both swooshes; the
// swooshes with 0xEBC bit 0x40 (BoostBallFire); 0xE3C without 0xE4C alive and dead (forced,
// DSB_MODE=2, then `kill`).
// @ fn_10_8F50
void CDarkSamus::UpdateBoostBallEffects(CStateManager& mgr, float dt) {
  const CVector3f pos = GetTranslation() + CVector3f(0.f, 0.f, 0.625f);
  if (CElementGen* gen = mBoostBallElementGen.get()) { // 0x00008FB0
    gen->SetOrientation(GetTransform().GetRotation());
    gen->SetTranslation(pos);
    gen->Update(dt);
  }
  if (mBoostBallSwooshE3C == nullptr) { // 0x00009010
    return;
  }
  if (GetAlive() && !mBoostBallFlag40) { // 0x0000902C, 0x00009038
    mBoostBallSwooshE3C->SetTranslation(pos);
    if (mBoostBallSwooshE4C != nullptr) { // 0x00009058
      mBoostBallSwooshE4C->SetTranslation(pos);
    }
  }
  mBoostBallSwooshE3C->SetGlobalScale(CVector3f::One() * 1.25f); // 0x000090B4
  mBoostBallSwooshE3C->Update(dt);                               // 0x000090CC
  if (mBoostBallSwooshE4C != nullptr) {                          // 0x000090D8
    mBoostBallSwooshE4C->SetGlobalScale(CVector3f(0.2f, 0.2f, 1.25f)); // 0x00009108
    mBoostBallSwooshE4C->SetModulationColor(CColor::White());          // 0x00009128
    mBoostBallSwooshE4C->Update(dt);                                   // 0x00009140
  }
}

// fn_10_1D6CC (0x0001D6CC, 0xC8 bytes), Think's (0x0001B3E4): the visibility 0xDE8. Fading (0xDEC
// == 1, 0x0001D6E0) and alive (0x420 bit 0x40, 0x0001D6FC) it drops by dt / lbl_10_rodata_248 =
// 2.0f (0x0001D714), else it grows by dt / lbl_10_rodata_24C = 3.0f (0x0001D730); clamped to
// [lbl_10_rodata_3FC = 0.0f, lbl_10_rodata_3E8 = 1.0f] (0x0001D744, 0x0001D758, the store at
// 0x0001D764); the shadow is drawn while it is above 0.0f (SetDrawShadow at 0x0001D780).
// Run (darksamus_uevent.py, dsfsm2-runs/s11/u8): every arm (key 75 fading, 76 dead while fading).
// @ fn_10_1D6CC
void CDarkSamus::UpdateFade(float dt) {
  if (mFadeOut == 1 && GetAlive()) { // 0x0001D6E0, 0x0001D6FC
    mGuestDE8 -= dt / 2.f;           // 0x0001D714
  } else {
    mGuestDE8 += dt / 3.f;           // 0x0001D730
  }
  const float v = mGuestDE8;
  // 0x0001D744 ble and 0x0001D758 bge to the fmr of v: a NaN v is kept (CMath::Clamp's shape).
  mGuestDE8 = 0.f > v ? 0.f : (1.f < v ? 1.f : v);
  SetDrawShadow(mGuestDE8 > 0.f);                    // 0x0001D774, 0x0001D780
}

// fn_10_1D5C4 (0x0001D5C4, 0x108 bytes), Think's (0x0001B438): the orbit, target and seeker
// materials (0x29, 0x28, 0x3F) are added (0x0001D690) unless (RemoveMaterial at 0x0001D6AC) the
// attack 0x980 is 0x8000 (0x0001D5F0), or the visibility 0xDE8 is 0.0f (0x0001D60C) with neither
// visor bit 0xDED 0x80 (0x0001D618) nor 0x40 (0x0001D620), or - the visibility above 0.0f or a
// visor bit set - the dark visor bit 0x80 (0x0001D638) with a stage (fn_10_17E14, 0x0001D650)
// whose fourth bool +0x33 is set (0x0001D664).
// Run (darksamus_uevent.py, dsfsm2-runs/s11/u8): every arm (key 23 the attack 0x8000, 54/75
// invisible, the dark visor with 77 / natural +0x33).
// @ fn_10_1D5C4
void CDarkSamus::UpdateTargetable(CStateManager& mgr) {
  bool targetable = true;
  if (mCurrentAttack == 0x8000) { // 0x0001D5F0
    targetable = false;
  } else if (mGuestDE8 == 0.f && !mDarkVisorDED80 && !mEchoVisorDED40) { // 0x0001D610..0x0001D624
    targetable = false;
  } else if (mDarkVisorDED80 == true && GetBattleStage(mgr) != nullptr &&
             GetBattleStage(mgr)->GetUnknown53FDCB5B() == true) { // 0x0001D63C, 0x0001D650, 0x0001D668
    targetable = false;
  }
  if (targetable == true) { // 0x0001D678
    AddMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
  } else {
    RemoveMaterial(kMT_Orbit, kMT_Target, kMT_SeekerTarget, mgr);
  }
}

// fn_10_6DAC (0x00006DAC, 0x8C bytes): the shield flag 0xCD0 and the direction it points.
// Deactivate (0x00006DB4) clears the flag (0x00006DCC). The other messages read the health
// (the vtable's slot 0x3C at 0x00006DD4, the float at its +4 at 0x00006DE8) and, when it is
// at most lbl_10_rodata_14C = 1.0f (`fcmpo` at 0x00006DF0, `cror eq, lt, eq` at 0x00006DF4,
// `bne` skip at 0x00006DF8: a NaN health skips too, so the test is `<=`), set the flag
// (0x00006E08), 0xD24 = 1.0f (0x00006DFC) and the direction 0xD18..0xD20 to lbl_10_bss_128 (
// 0x00006E10..0x00006E20). lbl_10_bss_128 is the module's static vector: the module's data
// initialiser fn_10_221C0 writes it at 0x0002224C and the constructor copies it into the
// actor's 0xD18 (0x0000E5C8), = (0.3, 0.6, 1.0): rodata 46C/424/3E8 at 0x0002224C..0x000222A8.
// Arms: Deactivate and the health above 1.0f natural; the health at most 1.0f forced (0.5,
// tools/cov/specs/darksamus_ds39_force.py).
// @ fn_10_6DAC
void CDarkSamus::UpdateShieldFromHealth(EStateMsg msg) {
  if (msg == kStateMsg_Deactivate) {
    mGuestCD0 = 0; // 0x00006DCC
    return;
  }
  const float health = GetHealthInfo()->GetHP(); // 0x00006DE0
  if (health <= 1.f) {                            // 0x00006DF0..0x00006DF8
    mGuestD24 = 1.f;                              // 0x00006DFC
    mGuestCD0 = 1;                                // 0x00006E08
    mGuestD18 = CVector3f(0.3f, 0.6f, 1.f);       // lbl_10_bss_128, 0x00006E0C..0x00006E20
  }
}

// fn_10_A154 (0x0000A154, 0x6C bytes): the boost ball's roll: the actor's position (0x54/0x58) is
// measured against the ball's target (0xE98/0xE9C) in the XY plane (the squares at 0x0000A180
// and 0x0000A184, the sum at 0x0000A188) and compared with lbl_10_rodata_278 = 4 (0x0000A18C);
// when the actor is close enough (0x0000A1B0) the roll's time 0xEA4 grows by dt (0x0000A1B4),
// else the ball's target becomes the actor's position (0x0000A194..0x0000A1A4) and the time is
// reset to 0.0f (0x0000A1A8).
// @ fn_10_A154
void CDarkSamus::RollBoostBallToPosition(float dt) {
  const CVector3f pos = GetTranslation();
  const float dx = pos.GetX() - mBoostBallPos.GetX();
  const float dy = pos.GetY() - mBoostBallPos.GetY();
  const float distSq = dx * dx + dy * dy; // 0x0000A180..0x0000A188
  if (!(distSq > 4)) {                  // 0x0000A18C (`ble`: NaN takes the close arm)
    mGuestEA4 += dt;                    // 0x0000A1B4
  } else {
    mBoostBallPos = pos;                // 0x0000A194..0x0000A1A4
    mGuestEA4 = 0.f;                    // 0x0000A1A8
  }
}


// fn_10_1ACA0 (0x0001ACA0, 0x168 bytes): the dive's contact with the player. Only while the dive
// is launched (0xB20 bit 0x40, `extrwi. r0, r0, 1, 25` at 0x0001ACC0) and the actor has touch
// bounds (vtable 0x48 at 0x0001ACDC, its valid byte at 0x0001AD28): the player's touch bounds
// (the same slot at 0x0001AD40; retail reads the box without its valid byte) grown by
// lbl_10_rodata_41C = 0.1f on every side (0x0001AD5C..0x0001AD84) must overlap the actor's
// (DoBoundsOverlap at 0x0001ADAC). Then the first contact hits the player (the dive's hit bit
// 0x20 clear, `extrwi. r0, r3, 1, 26` at 0x0001ADC0: fn_10_138A4), and a later one after a
// bounce (bit 0x02, `extrwi r0, r3, 1, 30` at 0x0001ADD8) pushes it (fn_10_13868).
// Every arm: runs ds895c u4 (the first hit) and u6 (tools/drivers/darksamus_divecontact.con,
// keys 86/96, 40 and 87: the hit made without and with a bounce, no touch bounds).
// @ fn_10_1ACA0
void CDarkSamus::UpdateDiveContact(CStateManager& mgr) {
  if (!mDive.mLaunched) {
    return;
  }
  const rstl::optional_object< CAABox > bounds = GetTouchBounds();
  if (!bounds.valid()) {
    return;
  }
  const rstl::optional_object< CAABox > playerBounds = mgr.GetPlayer(0)->GetTouchBounds();
  const CAABox& box = playerBounds.data();
  const CAABox grown(CVector3f(box.GetMinPoint().GetX() - 0.1f, box.GetMinPoint().GetY() - 0.1f,
                               box.GetMinPoint().GetZ() - 0.1f),
                     CVector3f(box.GetMaxPoint().GetX() + 0.1f, box.GetMaxPoint().GetY() + 0.1f,
                               box.GetMaxPoint().GetZ() + 0.1f));
  if (!bounds.data().DoBoundsOverlap(grown)) {
    return;
  }
  if (!mDive.mHitPlayer) {
    HitPlayerWithDive(mgr); // 0x0001ADD0
  } else if (mDive.mBounced) {
    PushPlayerAfterBounce(mgr); // 0x0001ADEC
  }
}

// fn_10_13868 (0x00013868, 0x3C bytes): unless the player is morphed (its 0x38C, `cmpwi r0, 0x1`
// at 0x0001387C), fn_10_13750 pushes it with the dive's horizontal impulse 0xB10 and no vertical
// one (lbl_10_rodata_3FC = 0.0f, 0x00013884..0x0001388C).
// @ fn_10_13868
void CDarkSamus::PushPlayerAfterBounce(CStateManager& mgr) {
  if (mgr.GetPlayer(0)->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
    PushPlayer(mgr, mDive.mImpulseHorizontal, 0.f);
  }
}

// fn_10_F710 (0x0000F710, 0xF0 bytes): the silhouette fade 0xD14. It rises at
// lbl_10_rodata_420 = 1.6 per second (0x0000F780) while the stage's first bool (its info's +0x30,
// `lbz r0, 0x30(r3)` at 0x0000F758) is 1 and the stage changed without a reaction (0xD59, read at
// 0x0000F764); otherwise, also with no stage (0x0000F748), it falls at lbl_10_rodata_424 = 0.6
// (0x0000F7A0). Then it is clamped to [0, 1] (0x0000F7B4 / 0x0000F7C8).
// @ fn_10_F710
void CDarkSamus::UpdateSilhouetteFade(CStateManager& mgr, float dt) {
  uchar rise = 0;
  if (GetBattleStage(mgr) != nullptr && GetBattleStage(mgr)->GetStageInfo().unknown_0x3ff87a8c == 1) {
    rise = mStageChanged;
  }
  if (rise == 1) {
    mGuestD14 += 1.6f * dt;
  } else {
    mGuestD14 -= 0.6f * dt;
  }
  mGuestD14 = CMath::Clamp(0.f, mGuestD14, 1.f);
}

// fn_10_F654 (0x0000F654, 0xBC bytes): the invulnerable model's fade 0xD24. It rises at
// lbl_10_rodata_420 = 1.6 per second (0x0000F6B0) while the 0xCD0 sub-object is on (its first
// byte 1, 0x0000F658) and its vector 0xD18 is not the zero vector (the three fcmpu at
// 0x0000F670..0x0000F690); otherwise it falls at lbl_10_rodata_424 = 0.6 (0x0000F6CC). Then it is
// clamped to [0, 1] (0x0000F6E4 / 0x0000F6F8).
// @ fn_10_F654
void CDarkSamus::UpdateInvulnerableFade(float dt) {
  if (mGuestCD0 == 1 && mGuestD18 != CVector3f::Zero()) {
    mGuestD24 += 1.6f * dt;
  } else {
    mGuestD24 -= 0.6f * dt;
  }
  mGuestD24 = CMath::Clamp(0.f, mGuestD24, 1.f);
}

// fn_10_1AE08 (0x0001AE08, 0xB8 bytes): the melee hit. While the melee window 0xC00 is open
// (above lbl_10_rodata_3FC = 0.0f, `cror eq, lt, eq` at 0x0001AE3C), the contact has not hit yet
// (0x9DE, `cmplwi r0, 0x1` at 0x0001AE48) and the player is within the melee reach (fn_10_17CEC
// against fn_10_1BDF0, `bgt` at 0x0001AE6C), the attacks 1, 2 and 4 (the switch at
// 0x0001AE74..0x0001AE90: 3, >= 5 and < 1 leave) apply the melee damage (fn_10_13998).
// @ fn_10_1AE08
void CDarkSamus::UpdateMeleeHit(CStateManager& mgr) {
  if (mGuestC00 <= 0.f) {
    return;
  }
  if (mMeleeContact.mHit == 1) {
    return;
  }
  const float reach = GetMeleeReach();
  if (GetDistanceToPlayer(mgr) > reach) {
    return;
  }
  const int attack = static_cast< int >(mCurrentAttack);
  if (attack == 1 || attack == 2 || attack == 4) {
    ApplyMeleeDamage(mgr); // 0x0001AE9C
  }
}

// fn_10_1B0E0 (0x0001B0E0, 0x74 bytes): while the stage's fourth bool (its info's +0x33,
// `lbz r0, 0x33(r3)` at 0x0001B114) is 1, the flash window restarts every Think: fn_10_18628
// with lbl_10_rodata_3FC = 0.0f and lbl_10_rodata_3E8 = 1.0f (0x0001B12C..0x0001B138). No stage
// (0x0001B104) does nothing.
// @ fn_10_1B0E0
void CDarkSamus::UpdateStageFlash(CStateManager& mgr) {
  if (GetBattleStage(mgr) != nullptr && GetBattleStage(mgr)->GetUnknown53FDCB5B() == 1) {
    SetGuestE04(0.f, 1.f);
  }
}

// fn_10_1B154 (0x0001B154, 0xA8 bytes): once (0x10A4, set at 0x0001B1E0), when the threshold
// 0x10A0 is above lbl_10_rodata_3FC = 0.0f (0x0001B180) and the health (vtable 0x3C, its +4 at
// 0x0001B1A0) drops below it (`bge` at 0x0001B1AC), the actor sends kSS_AILogicState1
// ('AIS1', `addi r4, r4, 0x5331` at 0x0001B1C8) with kInvalidUniqueId and kSM_None (-1).
// @ fn_10_1B154
void CDarkSamus::CheckHealthThreshold(CStateManager& mgr) {
  const float threshold = mData.mUnknown72EDEB7D;
  if (!(threshold > 0.f) || mHealthThresholdSent) { // 0x1B180 ble to the return (a NaN too)
    return;
  }
  if (!(GetHealthInfo()->GetHP() < threshold)) { // 0x1B1AC bge to the return (a NaN too)
    return;
  }
  SendScriptMsgs(kSS_AILogicState1, mgr, kInvalidUniqueId, kSM_None);
  mHealthThresholdSent = true;
}
