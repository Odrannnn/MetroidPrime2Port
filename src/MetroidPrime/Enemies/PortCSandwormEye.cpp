// Retail Sandworm REL (module 56): CSandwormEye (WP2 eyes). Bodies land here per
// docs/module-analysis/Sandworm_functions.tsv.
#include "MetroidPrime/Enemies/CSandworm.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Animation/CPASAnimParmData.hpp"
#include "Kyoto/Animation/CPASDatabase.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CEntityInfo.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Collision/CJointCollisionDescription.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"

namespace {
// lbl_56_bss_C: the static init fn_56_14FA8 builds it with string_l(lbl_56_rodata_250 + 0x489),
// which is "Sandworm.rel".
const rstl::string& SandwormRelName() {
  static const rstl::string skRelName = rstl::string_l("Sandworm.rel");
  return skRelName;
}

// lbl_56_data_0 = 0x3B (kMT_Solid); include-only filter (type 3) used by every eye/contact hit.
CMaterialFilter SolidFilter() {
  return CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList());
}

const float skBombThresholds[3] = {120.f, 80.f, 40.f}; // lbl_56_rodata_E8
} // namespace

// ---------------------------------------------------------------------------------------------
// CSandwormEye

// @ fn_56_21F0
void CSandwormEye::SetDead() { x2F4_flags |= 0x40; }

// @ fn_56_2204
bool CSandwormEye::IsDead() const { return (x2F4_flags & 0x40) != 0; }

// class T (fn_56_2210): the copy of x2E8_pos itself (lfs/stfs of 0x2E8..0x2F0). Unreferenced in
// retail - no 'bl fn_56_2210' in the module's asm and no table entry or relocation for it (its
// neighbour SetX2E8 has two, at 0x4944 and 0x498C), so nothing calls it. No marker
// (Sandworm_functions.tsv, class T).
CVector3f CSandwormEye::GetX2E8() const { return x2E8_pos; }

// @ fn_56_222C
void CSandwormEye::SetX2E8(const CVector3f& pos) { x2E8_pos = pos; }

// @ fn_56_2248
void CSandwormEye::SetX2F4Bit7() { x2F4_flags |= 0x80; }

// @ fn_56_225C
bool CSandwormEye::GetX2F4Bit7() const { return (x2F4_flags & 0x80) != 0; }

// @ fn_56_22A0
void CSandwormEye::SetTargetable(CStateManager& mgr, bool targetable) {
  if (targetable) {
    AddMaterial(kMT_Orbit, kMT_SeekerTarget, mgr);
  } else {
    RemoveMaterial(kMT_Orbit, kMT_SeekerTarget, mgr);
  }
  SetActive(targetable);
}

// @ fn_56_231C
void CSandwormEye::SetTouchBounds(const CAABox& box) { x2D0_touchBounds = box; }

// @ fn_56_2350
rstl::optional_object< CAABox > CSandwormEye::GetTouchBounds() const {
  return rstl::optional_object< CAABox >(x2D0_touchBounds);
}

// @ fn_56_238C
CVector3f CSandwormEye::GetAimPosition(const CStateManager&, float) const {
  return GetTranslation();
}

// @ fn_56_23A8
CVector3f CSandwormEye::GetOrbitPosition(const CStateManager&) const { return x2E8_pos; }

// @ fn_56_23C4
CSandwormEye::~CSandwormEye() {}

// @ fn_56_2434
CSandwormEye::CSandwormEye(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                           const CTransform4f& xf)
: CPhysicsActor(uid, name, info, 0, xf, CModelData::CModelDataNull(),
                CMaterialList(kMT_Orbit, kMT_Target, kMT_SeekerTarget), CAABox::MakeNullBox(),
                SMoverData(0.f), CActorParameters(), CPhysicsActor::skDefaultStepData)
, x2D0_touchBounds(CAABox::MakeNullBox())
, x2E8_pos(CVector3f::Zero())
, x2F4_flags(0)
, x2F8_relToken(SandwormRelName(), 0) {}

// ---------------------------------------------------------------------------------------------
// CSandworm: eyes

// @ fn_56_5060
void CSandworm::CreateEyes(CStateManager& mgr) {
  x14C4_headEye = mgr.AllocateUniqueId();
  CSandwormEye* head = rs_new CSandwormEye(
      x14C4_headEye, rstl::string_l("Sandworm Eye Head"),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId),
      GetTransform());
  x14C6_tailEye = mgr.AllocateUniqueId();
  CSandwormEye* tail = rs_new CSandwormEye(
      x14C6_tailEye, rstl::string_l("Sandworm Eye Tail"),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId),
      GetTransform());
  mgr.AddObject(head);
  mgr.AddObject(tail);
}

// @ fn_56_E134
CSandwormEye* CSandworm::GetTailEye(CStateManager& mgr) {
  return TCastToPtr< CSandwormEye >(mgr.ObjectById(x14C6_tailEye));
}

// @ fn_56_E168
CSandwormEye* CSandworm::GetHeadEye(CStateManager& mgr) {
  return TCastToPtr< CSandwormEye >(mgr.ObjectById(x14C4_headEye));
}

// @ fn_56_E19C
const CSandwormEye* CSandworm::GetTailEye(const CStateManager& mgr) const {
  return TCastToConstPtr< CSandwormEye >(mgr.GetObjectById(x14C6_tailEye));
}

// @ fn_56_E1D0
const CSandwormEye* CSandworm::GetHeadEye(const CStateManager& mgr) const {
  return TCastToConstPtr< CSandwormEye >(mgr.GetObjectById(x14C4_headEye));
}

// Retail compares TCastToPtr<CCollisionActor>(ObjectById(desc id)) with a CCollisionActor*
// argument; the header passes the actor's id instead, which gives the same answer.
// @ fn_56_566C
bool CSandworm::HitIsOnHeadEye(CStateManager& mgr, TUniqueId collisionActor) const {
  const TUniqueId id = x998_collisionActors->GetCollisionDescFromIndex(0xE).GetCollisionActorId();
  CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
  return actor != nullptr && actor->GetUniqueId() == collisionActor;
}

// @ fn_56_56D4
bool CSandworm::HitIsOnTailEye(CStateManager& mgr, TUniqueId collisionActor) const {
  const TUniqueId id = x998_collisionActors->GetCollisionDescFromIndex(0).GetCollisionActorId();
  CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id));
  return actor != nullptr && actor->GetUniqueId() == collisionActor;
}

// @ fn_56_CAF0
void CSandworm::SetEyeCollisionVulnerability(CStateManager& mgr, int eye, bool vulnerable) {
  if (!x998_collisionActors.get()) {
    return;
  }
  const TUniqueId id =
      x998_collisionActors->GetCollisionDescFromIndex(eye == 0 ? 0 : 0xE).GetCollisionActorId();
  if (CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id))) {
    actor->SetDamageVulnerability(vulnerable ? *GetDamageVulnerability()
                                             : CDamageVulnerability::ReflectVulnerabilty());
  }
}

// @ fn_56_2C30
CVector3f CSandworm::GetOrbitPosition(const CStateManager& mgr) const {
  if ((x1520_flags & 0x80) == 0 || (x8C1_flags & 0x40) != 0) {
    return x1608_residuePos;
  }
  if (const CSandwormEye* eye = GetHeadEye(mgr)) {
    return eye->GetOrbitPosition(mgr);
  }
  return CPatterned::GetOrbitPosition(mgr);
}

// @ fn_56_321C
CVector3f CSandworm::GetAimPosition(const CStateManager& mgr, float dt) const {
  if ((x8C0_flags & 0x10) != 0 && !IsPossessedBombGuardian()) {
    return (x8C0_flags & 0x02) != 0 ? x1394_snatchStart : GetTranslation();
  }
  const CSandwormEye* eye = GetHeadEye(mgr);
  if (eye && !eye->IsDead() && eye->GetMaterialList().HasMaterial(kMT_Orbit)) {
    return eye->GetAimPosition(mgr, dt);
  }
  eye = GetTailEye(mgr);
  if (eye && !eye->IsDead() && eye->GetMaterialList().HasMaterial(kMT_Orbit)) {
    return eye->GetAimPosition(mgr, dt);
  }
  return x1388_;
}

// @ fn_56_36A0
bool CSandworm::AnyEyeDead(CStateManager& mgr) const {
  if (IsPossessedBombGuardian()) {
    return false;
  }
  const CSandwormEye* head = GetHeadEye(mgr);
  const CSandwormEye* tail = GetTailEye(mgr);
  if (!head || !tail) {
    return true;
  }
  return head->IsDead() || tail->IsDead();
}

// @ fn_56_E340
bool CSandworm::IsEyeVisible(const CStateManager& mgr) const {
  return AnyEyeDead(const_cast< CStateManager& >(mgr)) && x14D4_ == 0 &&
         (!IsPossessedBombGuardian() || (x19A8_bombPhase.xc_flags & 0x20) != 0);
}

// @ fn_56_E204
bool CSandworm::ShouldRenderHeadEye(const CStateManager& mgr) const {
  if ((x8C0_flags & 0x10) != 0 && ((x8C0_flags & 0x08) != 0 || IsPossessedBombGuardian())) {
    return false;
  }
  if (IsEyeVisible(mgr)) {
    const CSandwormEye* eye = GetHeadEye(mgr);
    return !eye->IsDead();
  }
  const CSandwormEye* eye = GetTailEye(mgr);
  return !eye->IsDead();
}

// @ fn_56_E2C0
bool CSandworm::ShouldRenderTailEye(const CStateManager& mgr) const {
  if (IsEyeVisible(mgr)) {
    const CSandwormEye* eye = GetTailEye(mgr);
    return !eye->IsDead();
  }
  const CSandwormEye* eye = GetHeadEye(mgr);
  return !eye->IsDead();
}

// @ fn_56_441C
void CSandworm::UpdateEyes(CStateManager& mgr, float dt) {
  UpdateEyeVulnerability(mgr, dt);
  CSandwormEye* eyeA = GetHeadEye(mgr); // fn_56_E168: x14C4
  CSandwormEye* eyeB = GetTailEye(mgr); // fn_56_E134: x14C6

  if (!GetAlive() || !GetActive() || (x1520_flags & 0x80) == 0) {
    if (eyeA) {
      eyeA->SetTargetable(mgr, false);
    }
    if (eyeB) {
      eyeB->SetTargetable(mgr, false);
    }
    return;
  }

  CVector3f posA = x1608_residuePos;
  CVector3f posB = x1614_residuePosHighHealth;
  CVector3f aimA = x1394_snatchStart;
  CVector3f aimB = x13A0_snatchEnd;

  if (x14D8_charge.x0_ != CVector3f::Zero()) {
    const CVector3f dir = (x14D8_charge.x1c_ - x14D8_charge.x0_).AsNormalized();
    posA = x14D8_charge.x0_ + (x1608_residuePos - x14D8_charge.x0_).Magnitude() * dir;
    posB = x14D8_charge.x0_ + (x1614_residuePosHighHealth - x14D8_charge.x0_).Magnitude() * dir;
  }

  if (eyeA && eyeB) {
    if (!eyeA->GetX2F4Bit7()) {
      eyeA->SetX2F4Bit7();
      eyeB->SetX2F4Bit7();
    } else if ((posB - eyeA->GetTranslation()).MagSquared() <
               (posA - eyeA->GetTranslation()).MagSquared()) {
      CSandwormEye* tmp = eyeA;
      eyeA = eyeB;
      eyeB = tmp;
    }
  }

  if ((posA - aimA).Magnitude() > (posA - aimB).Magnitude()) {
    const CVector3f tmp = aimA;
    aimA = aimB;
    aimB = tmp;
  }

  bool targetA;
  bool targetB;
  if (IsBeingSnatched()) {
    targetA = false;
    targetB = false;
  } else if (x1490_attacks.x4_current == 7) {
    targetA = true;
    targetB = false;
  } else if (!AnyEyeDead(mgr)) {
    if (IsPossessedBombGuardian()) {
      targetA = false;
      targetB = true;
    } else {
      targetA = true;
      targetB = false;
    }
  } else {
    targetA = true;
    targetB = false;
  }

  UpdateBoundsHelper();
  const CAABox box = x1A0C_bounds;

  if (eyeA) {
    eyeA->SetTargetable(mgr, targetA);
    eyeA->SetTranslation(posA);
    eyeA->SetTouchBounds(box);
    if (x1490_attacks.x4_current != 3) {
      eyeA->SetX2E8(aimA);
    }
  }
  if (eyeB) {
    eyeB->SetTargetable(mgr, targetB);
    eyeB->SetTranslation(posB);
    eyeB->SetTouchBounds(box);
    if (x1490_attacks.x4_current != 3) {
      eyeB->SetX2E8(aimB);
    }
  }

  if (AnyEyeDead(mgr) && x14C8_ == 0) {
    x14C8_ = 1;
    const CSandwormEye* head = GetHeadEye(static_cast< const CStateManager& >(mgr));
    SpawnResidue(mgr, head != nullptr && head->IsDead() ? 0 : 1); // [port null check]
  }
}

// @ fn_56_4AD8
void CSandworm::UpdateEyeVulnerability(CStateManager& mgr, float dt) {
  bool vuln0 = false;
  bool vuln1 = false;
  if (GetAlive() && GetActive()) {
    if (5.f + x19A8_bombPhase.x24_ > x8B0_activeTime) {
      vuln0 = false;
      vuln1 = false;
    } else if (x1490_attacks.x4_current == 7) {
      if ((x19A8_bombPhase.xc_flags & 0x40) == 0) {
        vuln0 = true;
        vuln1 = false;
      }
    } else if (!AnyEyeDead(mgr)) {
      if (IsPossessedBombGuardian()) {
        vuln0 = false;
        vuln1 = x1490_attacks.x4_current == 0;
      } else {
        vuln0 = true;
        vuln1 = false;
      }
    } else {
      vuln0 = IsPossessedBombGuardian() ? false : x14D4_ != 0;
      vuln1 = false;
    }
  }

  if (vuln0) {
    RemoveAdditiveFlinch(mgr, 0);
  } else {
    AddAdditiveFlinch(mgr, 0);
  }
  SetEyeCollisionVulnerability(mgr, 0, vuln0);
  if (vuln1) {
    RemoveAdditiveFlinch(mgr, 1);
  } else {
    AddAdditiveFlinch(mgr, 1);
  }
  SetEyeCollisionVulnerability(mgr, 1, vuln1);

  CElementGen* gen0 = x19D0_glow.x4_gen0;
  CElementGen* gen1 = x19D0_glow.xc_gen1;
  CElementGen* gen2 = x19D0_glow.x14_gen2;
  CElementGen* gen3 = x19D0_glow.x1c_gen3;

  if (IsBeingSnatched() || (!vuln0 && !vuln1)) {
    if (gen0) {
      gen0->SetParticleEmission(false);
    }
    if (gen1) {
      gen1->SetParticleEmission(false);
    }
    if (gen2) {
      gen2->SetParticleEmission(false);
    }
    if (gen3) {
      gen3->SetParticleEmission(false);
    }
    return;
  }

  CElementGen* off;
  CElementGen* on;
  if (IsPossessedBombGuardian()) {
    on = vuln0 ? gen2 : gen3;
    off = gen0;
  } else {
    on = gen0;
    off = gen2;
  }
  if (off) {
    off->SetParticleEmission(false);
  }
  if (on) {
    on->SetParticleEmission(true);
    on->Update(dt);
    on->SetGlobalScale(x1770_pincers.x8_scale * GetModelData()->GetScale());
    on->SetGlobalTranslation(vuln0 ? x1608_residuePos : x1614_residuePosHighHealth);
  }

  if (!gen1) {
    return;
  }
  if ((x8C0_flags & 0x10) != 0) {
    gen1->SetParticleEmission(false);
  } else if (1.f + x134C_ > x8B0_activeTime && x14D4_ == 0) {
    gen1->SetParticleEmission(true);
    gen1->Update(dt);
    const float t = 0.8f * (1.f - (x8B0_activeTime - x134C_)) + 0.1f;
    gen1->SetGlobalScale(x1770_pincers.x8_scale * (t * GetModelData()->GetScale()));
    gen1->SetGlobalTranslation(vuln0 ? x1614_residuePosHighHealth : x1608_residuePos);
  } else {
    gen1->SetParticleEmission(false);
  }
}

// ---------------------------------------------------------------------------------------------
// CSandworm: additive flinches

// @ fn_56_11DF8
void CSandworm::RemoveAdditiveFlinch(CStateManager&, int eye) {
  int& slot = eye != 0 ? x14D0_ : x14CC_;
  if (slot != -1) {
    AnimationData()->DelAdditiveAnimation(slot);
    slot = -1;
  }
}

// @ fn_56_11E54
void CSandworm::AddAdditiveFlinch(CStateManager&, int eye) {
  if (x1770_pincers.x8_scale < 1.f && eye == 0) {
    return;
  }
  int& slot = eye != 0 ? x14D0_ : x14CC_;
  if (slot != -1) {
    return;
  }
  const int state = eye != 0 ? 10 : 9;
  const CPASAnimParmData parms(static_cast< pas::EAnimationState >(0x18),
                               CPASAnimParm::FromEnum(state));
  const rstl::pair< float, int > best =
      GetModelData()->GetAnimationData()->GetPASDatabase().FindBestAnimation(parms, -1);
  if (best.first > 0.f) {
    slot = best.second;
    AnimationData()->AddAdditiveAnimation(slot, 1.f, false, false);
  }
}

// Angle between the weapon's forward column and the tail segment.
// @ fn_56_11CF8
float CSandworm::AngleBetween(const CTransform4f& xf) const {
  const CVector3f dir = xf.GetColumn(kDY);
  const CVector3f a = GetSegmentPosition(12);
  const CVector3f b = GetSegmentPosition(11);
  const CVector3f body(b.GetX() - a.GetX(), b.GetY() - a.GetY(), 0.f);
  const CVector3f weapon(dir.GetX(), dir.GetY(), 0.f);
  if (!body.CanBeNormalized() || !weapon.CanBeNormalized()) {
    return 6.2831855f; // lbl_56_rodata_230
  }
  return CVector3f::GetAngleDiff(weapon.AsNormalized(), body.AsNormalized());
}

// ---------------------------------------------------------------------------------------------
// CSandworm: script messages

// @ fn_56_11330
void CSandworm::AcceptScriptMsgHelper(CStateManager& mgr) {
  CPFArea* area = mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetPostConstructed()->mPathArea;
  x7C4_search.SetArea(area);
  x1858_search2.SetArea(area);
}

// @ fn_56_11374
void CSandworm::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const TUniqueId sender = msg.GetSenderId();
  switch (msg.GetMessage()) {
  case kSM_Create:
    BodyController()->SetLocomotionType(
        (x8C0_flags & 0x10) != 0 ? static_cast< pas::ELocomotionType >(1)
                                 : static_cast< pas::ELocomotionType >(7));
    BodyController()->Activate(mgr, static_cast< pas::EAnimationState >(-1));
    RemoveMaterial(kMT_Orbit, mgr);
    CreateEyes(mgr);
    x1A00_ = GetBodyController()->GetTurnSpeed();
    break;

  case kSM_Activate:
    if ((x8C0_flags & 0x80) != 0) {
      x8C0_flags |= 0x01;
    }
    break;

  case kSM_Deactivate:
    if (x998_collisionActors.get()) {
      x998_collisionActors->SetActive(mgr, false);
    }
    break;

  case kSM_Delete:
    if (x998_collisionActors.get()) {
      x998_collisionActors->Destroy(mgr);
      x998_collisionActors = nullptr;
    }
    if (x14C4_headEye != kInvalidUniqueId) {
      mgr.DeleteObjectRequest(x14C4_headEye);
      x14C4_headEye = kInvalidUniqueId;
    }
    if (x14C6_tailEye != kInvalidUniqueId) {
      mgr.DeleteObjectRequest(x14C6_tailEye);
      x14C6_tailEye = kInvalidUniqueId;
    }
    StopMoveSfx();
    break;

  case kSM_AreaLoaded:
    AcceptScriptMsgHelper(mgr);
    while (x1670_.x0_.size() < 13) {
      x1670_.x0_.push_back(CQuaternion::NoRotation());
    }
    break;

  case kSM_ReflectedDamage:
  case kSM_ResistedDamage:
    if (TCastToPtr< CCollisionActor >(mgr.ObjectById(sender))) {
      SetHitByPlayerProjectile(true);
    }
    break;

  case kSM_InternalMessage0:
    if ((x8C0_flags & 0x10) == 0) {
      x8C1_flags |= 0x80;
    }
    break;

  case kSM_HitObject:
    if (CCollisionActor* col = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender))) {
      const TUniqueId playerId = mgr.Player(0)->GetUniqueId();
      if (col->GetLastTouchedObject() == playerId) {
        if (mCurDamageRemTime <= 0.f) {
          mgr.ApplyDamage(GetUniqueId(), playerId, GetUniqueId(), GetContactDamage(),
                          SolidFilter(), CVector3f::Zero());
          mCurDamageRemTime = mDamageWaitTime;
        }
        if (x1490_attacks.EndsWith(4, 1)) {
          ApplyChargeDamage(mgr);
        }
      }
    }
    break;

  case kSM_Damage: {
    SetHitByPlayerProjectile(true);
    if ((x1520_flags & 0x80) == 0) {
      return; // retail skips CPatterned::AcceptScriptMsg here
    }
    const float hp = GetHealthInfo()->GetHP();
    CCollisionActor* col = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
    if (!col) {
      break;
    }

    bool onEye = false;
    if (x1490_attacks.x4_current == 7) {
      if ((x19A8_bombPhase.xc_flags & 0x40) == 0) {
        onEye = HitIsOnTailEye(mgr, col->GetUniqueId());
      }
    } else if (!AnyEyeDead(mgr)) {
      onEye = IsPossessedBombGuardian() ? HitIsOnHeadEye(mgr, col->GetUniqueId())
                                        : HitIsOnTailEye(mgr, col->GetUniqueId());
    } else if (!IsPossessedBombGuardian()) {
      onEye = HitIsOnTailEye(mgr, col->GetUniqueId());
    }
    if (!onEye) {
      break;
    }

    CHealthInfo* colHealth = col->HealthInfo();
    const TUniqueId lastTouched = col->GetLastTouchedObject();
    if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(lastTouched))) {
      CDamageInfo dinfo = weapon->GetCurrentDamageInfo();
      dinfo.SetRadius(0.f);
      if (IsPossessedBombGuardian()) {
        if (x1490_attacks.x4_current == 7) {
          mgr.ApplyDamage(lastTouched, GetUniqueId(), lastTouched, dinfo, SolidFilter(),
                          CVector3f::Zero());
        } else if (AngleBetween(weapon->GetTransform()) < 1.3089969f &&
                   x1490_attacks.x4_current == 0) {
          x19A8_bombPhase.x1c_ += dinfo.GetDamage(*GetDamageVulnerability());
          x19A8_bombPhase.x20_ = CPatterned::skDamageHitTime;
          if (x19A8_bombPhase.x1c_ > x19A8_bombPhase.x18_) {
            x1490_attacks.x0_picked = 7;
          }
        }
      } else {
        if (!AnyEyeDead(mgr) && hp - dinfo.GetDamage(*GetDamageVulnerability()) <= 1.f) {
          dinfo.SetDamage(hp - 1.f);
        }
        mgr.ApplyDamage(lastTouched, GetUniqueId(), lastTouched, dinfo, SolidFilter(),
                        CVector3f::Zero());
      }
      x1824_ = x8B0_activeTime;

      if (!AnyEyeDead(mgr) && !IsPossessedBombGuardian() &&
          GetHealthInfo()->GetHP() <= 0.5f * GetHealthInfo()->GetInitialHP()) {
        // Retail calls SetDead on fn_56_E134's result without a null check. [port null check]
        if (CSandwormEye* eye = GetTailEye(mgr)) {
          eye->SetDead();
        }
        if (x1490_attacks.x4_current == 7) {
          x19A8_bombPhase.xc_flags |= 0x20;
        }
      }
    } else {
      TakeDamage(CVector3f::Zero(), 10000.f - colHealth->GetHP());
      x1824_ = x8B0_activeTime;
    }
    colHealth->SetHP(10000.f);

    if (x1490_attacks.x4_current == 7 && (x19A8_bombPhase.xc_flags & 0x40) == 0) {
      for (int i = 0; i < 3; ++i) {
        if (x19A8_bombPhase.x14_ >= skBombThresholds[i] &&
            GetHealthInfo()->GetHP() < skBombThresholds[i]) {
          x19A8_bombPhase.xc_flags |= 0x80;
          x19A8_bombPhase.x24_ = x8B0_activeTime;
          DeleteHeldBomb(mgr);
          break;
        }
      }
    }
  } break;

  default:
    break;
  }
  CPatterned::AcceptScriptMsg(mgr, msg);
}
