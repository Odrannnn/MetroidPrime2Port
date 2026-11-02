// Host body for the SandBoss REL (module 55, Amorbis), slice A: the loader, the constructor, the
// vtable overrides, collision and armour, the sibling helpers and the FSM tables. The analysis is
// docs/module-analysis/SandBoss.md; the retail function is named on each body. Functions marked
// `// @ fn_55_X` were checked against retail's behaviour; the triggers, states and code functions
// live in PortCSandBossStates.cpp, the attack helpers in PortCSandBossAttacks.cpp.
#include "MetroidPrime/Enemies/CSandBoss.hpp"

#include "Kyoto/Animation/CInt32POINode.hpp"
#include "Kyoto/Animation/CSkinnedModel.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CActorLights.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/CParticleDatabase.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/Factories/CScannableObjectInfo.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCoverPoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWaypoint.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include <stdio.h>
#include <string.h>

// Entity type id 129 (TypesMatch.cpp lists it as the SandBoss REL's; there is no kET_ name).
static const int kSandBossTypeId = 129;

// Shared with PortCSandBossStates.cpp / PortCSandBossAttacks.cpp.
CSandBoss* CastSandBoss(CEntity* entity) {
  return entity != nullptr ? static_cast< CSandBoss* >(entity->TypesMatch(kSandBossTypeId))
                           : nullptr;
}

const CSandBoss* CastSandBoss(const CEntity* entity) {
  return CastSandBoss(const_cast< CEntity* >(entity));
}

namespace {

// lbl_55_rodata_48: the eight armour locators, in armour-slot order.
const char* const skArmorLocators[8] = {"Head_1",  "Spine_6", "Spine_5", "Spine_4",
                                        "Spine_3", "Spine_2", "Spine_1", "Skeleton_Root"};

// lbl_55_rodata_10: the seven collision spheres (locator, radius).
struct SJointSphere {
  const char* mName;
  float mRadius;
};
const SJointSphere skJointSpheres[7] = {
    {"Head_1", 2.7f},  {"Spine_6", 2.8f}, {"Spine_5", 2.9f}, {"Spine_4", 3.0f},
    {"Spine_3", 3.1f}, {"Spine_2", 3.2f}, {"Spine_1", 3.3f},
};

bool NameIs(const rstl::string& name, const char* other) { return strcmp(name.c_str(), other) == 0; }

} // namespace

// @ fn_55_E170
CSandBoss::CSandBoss(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                     const CTransform4f& xf, const CModelData& modelData,
                     const CActorParameters& actParms, const CPatternedInfo& pInfo,
                     const SLdrSandBossData& data)
// EPatternedAI 0x31, flavor 0, movement 0, collider 1, body 1.
: CPatterned(static_cast< EPatternedAI >(0x31), uid, name, kFT_Zero, info, xf, modelData, pInfo,
             kMT_Ground, kCT_One, kBT_BiPedal, actParms)
, mData(data)
, mCollisionManager(nullptr)
, mBoneTracking(*GetModelData()->GetAnimationData(), rstl::string_l("Head_1"), 1.0471976f,
                1.5707964f, 1)
, mRound(0)
, mPInfoTurnSpeed(mTurnSpeed) // retail copies pInfo+8, the turn speed CPatterned also stores
, mScanInfo(nullptr)
, mWaitMode(0)
, mCinematicState(0)
, mGroupCinematicState(0)
, mChargeBeamPhase(-1)
, mDarkBeamInfo(data.darkBeamProjectile, LdrToDamageInfo(data.darkBeamDamage))
, mChargeBeamInfo(data.unknown_0x7619e561.chargeBeamInfo.weaponSystem, CDamageInfo())
, mIS02Target(kInvalidUniqueId)
, mIS01Target(kInvalidUniqueId)
, mWaypoint(kInvalidUniqueId)
, mBossInfoSource(kInvalidUniqueId)
, mSafeZoneTarget(kInvalidUniqueId)
, mSphere(kInvalidUniqueId)
, mHeadCollider(kInvalidUniqueId)
, mStampedePoint(kInvalidUniqueId)
, mCoverPoint(kInvalidUniqueId)
, mScriptEffect(kInvalidUniqueId)
, mEFC(8)
, mFlashColor(CColor::Black())
, mFlashTime(0.f)
, mChargeBeamPhaseTimer(0.f)
, mChargeBeamTimer(data.unknown_0x7619e561.doubleCharge.minChargeBeamAttackTime)
, mDarkBeamTimer(data.minDarkBeamAttackTime)
, mF20(0.f)
, mStampedeHP(data.stampedeProperties.breakStampedeHP)
, mF28(0)
, mHeadArmorHP(data.headArmorHP)
, mF30(0)
, mF34(0.f)
, mChargeBeamSweepPhase(0.f)
, mChargeBeamSweepDir(1)
, mChargeBeamRetargetTimer(0.f)
, mChargeBeamCooldown(0)
, mF48(0)
, mNormalSkinnedModel(GetModelData()->GetAnimationData()->GetModelData())
, mTailArmorSkinnedModel(rs_new CSkinnedModel(
      gpSimplePool->GetObj(SObjectTag('CMDL', data.withTailArmorModel)),
      gpSimplePool->GetObj(SObjectTag('CSKR', data.withTailArmorSkinRules)),
      GetModelData()->GetAnimationData()->GetModelData()->GetLayoutInfo()))
, m14A0(CTransform4f::Identity())
, mSpawnRotation(CQuaternion::FromMatrix(xf.GetRotation()))
, mDoubleChargeDamage(LdrToDamageInfo(data.unknown_0x7619e561.doubleCharge.damage))
, mTripleChargeDamage(LdrToDamageInfo(data.unknown_0x7619e561.tripleCharge.damage))
, mStampedeDamage(LdrToDamageInfo(data.stampedeProperties.stampedeDamage))
, mSnapJawDamage(LdrToDamageInfo(data.snapJawDamage))
, mSpitOutDamage(LdrToDamageInfo(data.spitOutDamage))
, mNormalVulnerability(LdrToDamageVulnerability(data.damageVulnerability))
, mStampedeVulnerability(LdrToDamageVulnerability(data.stampedeVulnerability))
, mSuckAirVulnerability(LdrToDamageVulnerability(data.suckAirVulnerability))
, mReflectVulnerability0(CDamageVulnerability::ReflectVulnerabilty())
, mReflectVulnerability1(CDamageVulnerability::ReflectVulnerabilty())
, mUnderGround(false)
, mBeginStampede(false)
, mSnapJawContact(false)
, mStampedeActive(false)
, mDarkBeamResult(false)
, m165C_b2(false)
, mDoubleChargeResult(false)
, mTripleChargeResult(false)
, mAttachedToSphere(false)
, mArmorDestroyed(false)
, mNotRecovered(false)
, mSuckAir(false)
, mHoldingBall(false)
, mSkipParticleRender(false)
, m165D_b1(true)
, mSphereLocked(false)
, mProbingDoubleCharge(false)
, mProbingTripleCharge(false)
, mProbingDarkBeam(false)
, mSyncAttacksAllowed(false) {
  // The three optional PART tokens (0xE50 / 0xE60 / 0xE70), each only when its asset is set.
  if (data.headArmorExplosion != kInvalidAssetId) {
    mHeadArmorExplosion = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', data.headArmorExplosion)));
  }
  if (data.stampedeProperties.stampedeArmorExplosion != kInvalidAssetId) {
    mStampedeArmorExplosion = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', data.stampedeProperties.stampedeArmorExplosion)));
  }
  if (data.stampedeProperties.stampedeSandFountainFx != kInvalidAssetId) {
    mStampedeSandFountain = TLockedToken< CGenDescription >(
        gpSimplePool->GetObj(SObjectTag('PART', data.stampedeProperties.stampedeSandFountainFx)));
  }
  for (int i = 0; i < 8; ++i) {
    mArmorState[i] = 0; // lbl_55_data_740
    mArmorLocators[i] = CSegId(0xff);
  }
  mHeadLocator = CSegId(0xff);

  mDarkBeamInfo.Token().Lock();
  mIdEntries.reserve(0x10); // fn_55_10FA8
  mKnockBackController.EnableKnockBackPhysics(false);
  mKnockBackController.EnableAllAnimReactions(false);

  const CAnimData* animData = GetModelData()->GetAnimationData();
  for (int i = 0; i < 8; ++i) {
    mArmorLocators[i] = animData->GetLocatorSegId(rstl::string_l(skArmorLocators[i]));
  }
  mHeadLocator = animData->GetLocatorSegId(rstl::string_l("Head_1"));

  BuildArmorModels();
  SetDrawShadow(false);

  if (data.scannableInfo1 != kInvalidAssetId) {
    mScanInfo = rs_new TCachedToken< CScannableObjectInfo >(
        gpSimplePool->GetObj(SObjectTag('SCAN', data.scannableInfo1)), true);
  }
}

// @ fn_55_DB60
// The members clean themselves up; retail's body is the generated member teardown (it deletes
// the collision manager and the SCAN token like the single_ptrs here do).
CSandBoss::~CSandBoss() {}

// TypesMatch for id 129 with CPatterned as the parent (TYPES_MATCH_IMPL's logic; the class is not
// in TypesMatch.cpp because the SandBoss REL owns it).
CEntity* CSandBoss::TypesMatch(int typeId) const {
  if (typeId == kSandBossTypeId) {
    return const_cast< CSandBoss* >(this);
  }
  if (typeId > kSandBossTypeId) {
    return nullptr;
  }
  return CPatterned::TypesMatch(typeId);
}

// ---------------------------------------------------------------------------------------------
// One-line accessors at the head of .text.

// @ fn_55_0
// 0x0 li r3, 0x1: true (CScanDisplay asks it while the scan visor targets the worm).
bool CSandBoss::IsScanVisorSelfRender() const { return true; }

// @ fn_55_8
bool CSandBoss::IsListening() const { return true; }

// @ fn_55_10
rstl::optional_object< CAABox > CSandBoss::GetTouchBounds() const { return GetBoundingBox(); }

// @ fn_55_4C
void CSandBoss::TakeDamage(const CVector3f&, float) { mDamageCooldownTimer = skDamageHitTime; }

// @ fn_55_5C
uchar CSandBoss::GetModelAlphau8(const CStateManager&) const { return mColor.GetAlphau8(); }

// fn_55_64 (ported, not exercised yet)
CPathFindSearch* CSandBoss::GetSearchPath() { return nullptr; }

// fn_55_6C (ported, not exercised yet)
CPathFindPointSearch* CSandBoss::GetPointSearchPath() { return nullptr; }

// fn_55_74 (ported, not exercised yet)
CRagDoll* CSandBoss::GetRagDoll() const { return nullptr; }

// fn_55_7C (ported, not exercised yet)
TUniqueId CSandBoss::GetAttackTarget() const { return kInvalidUniqueId; }

// fn_55_8C (ported, not exercised yet)
// IsOnGround: the REL's body is the DOL's own CPatterned::IsOnGround
// (IsOnGround__10CPatternedCFv, the three instructions at the head of CPatterned.s) copied
// verbatim - `lbz r0, 0x34c(r3); extrwi r3, r0, 1, 28` (0x8C, 0x90) - so it reads mask 0x08 of
// the byte at 0x34C (PPC bit 28 of the zero-extended register: (byte >> 3) & 1). The DOL's
// CPatterned::AcceptScriptMsg sets that bit in its kSM_Landed branch (the one next to the
// zeroed momentum and the added ground-collider material) and clears it in its kSM_OffGround
// branch, and the DOL's Landed trigger reads the same bit. The host's CPatterned::mOnGround is
// written in exactly those two branches (src/MetroidPrime/Enemies/CPatterned.cpp:284 and
// :291) and read by CPatterned::IsOnGround()
// (include/MetroidPrime/Enemies/CPatterned.hpp:184), so this override returns the same flag
// [proven]. The DOL's exact addresses are in docs/module-analysis/SandBoss.md section 7.6.
bool CSandBoss::IsOnGround() const { return CPatterned::IsOnGround(); }

// fn_55_98 (ported, not exercised yet)
float CSandBoss::GetGravityConstant() const { return kDefaultGravityAccel; }

// fn_55_A4 (ported, not exercised yet)
const rstl::optional_object< TCachedToken< CGenDescription > >&
CSandBoss::GetDeathExplosionParticle() const {
  return mDeathExplosionParticle;
}

// fn_55_AC (ported, not exercised yet)
bool CSandBoss::CanBeShot(const CStateManager&, int) { return true; }

// @ fn_55_B4
bool CSandBoss::Listen(CStateManager&, const CVector3f&, EListenNoiseType) { return false; }

// fn_55_BC (ported, not exercised yet)
CVector3f CSandBoss::GetOrigin(const CStateManager&, const CTeamAiRole&, const CVector3f&) const {
  return GetTranslation();
}

// @ fn_55_D8
const CHealthInfo* CSandBoss::GetHealthInfo() const {
  return const_cast< CSandBoss* >(this)->HealthInfo();
}

// @ fn_55_CDF0
CProjectileInfo* CSandBoss::ProjectileInfo() { return &mDarkBeamInfo; }

// @ fn_55_CF6C
// The body itself is pass-through; shots are taken by the collision actors, which get
// CPatterned's vulnerability (fn_55_724C / fn_55_70D0).
const CDamageVulnerability* CSandBoss::GetDamageVulnerability() const {
  return &CDamageVulnerability::PassThroughVulnerabilty();
}

// @ fn_55_CDB0
CScannableObjectInfo* CSandBoss::GetScannableObjectInfo() const {
  if (!mStampedeActive && mScanInfo.get() != nullptr) {
    return mScanInfo->GetObject();
  }
  return CPatterned::GetScannableObjectInfo();
}

// ---------------------------------------------------------------------------------------------
// Think / messages.

// @ fn_55_D6BC
void CSandBoss::PreThink(float dt, CStateManager& mgr) {
  if (IsAttachedLeader(mgr)) {
    // The leader carries the sphere on its locator 1, centred on the sphere's model bounds.
    if (CActor* sphere = TCastToPtr< CActor >(mgr.ObjectById(mSphere))) {
      CTransform4f xf = GetLctrTransform(CSegId(1));
      xf.AddTranslation(xf.Rotate(-sphere->GetModelData()->GetBounds().GetCenterPoint()));
      sphere->SetTransform(xf);
    }
    // ... and turns the other attached siblings with it, each keeping its spawn-relative rotation.
    const CQuaternion delta =
        CQuaternion::FromMatrix(GetTransform()) * mSpawnRotation.BuildInverted();
    for (int i = 0; i < mSiblings.size(); ++i) {
      CSandBoss* sibling = CastSandBoss(mgr.ObjectById(mSiblings[i]));
      if (sibling != nullptr && sibling != this && sibling->mAttachedToSphere) {
        sibling->SetTransform(
            (sibling->mSpawnRotation * delta).BuildTransform4f(sibling->GetTranslation()));
      }
    }
  }
  mBoneTracking.PreThink(*AnimationData());
  CPatterned::PreThink(dt, mgr);
}

// @ fn_55_D620
void CSandBoss::Think(float dt, CStateManager& mgr) {
  if (!GetActive()) {
    return;
  }
  CPatterned::Think(dt, mgr);
  mCollisionManager->Update(dt, mgr, CCollisionActorManager::kUO_ObjectSpace);
  mBoneTracking.Think(dt);
  UpdateTimers(dt);
  UpdateChargeBeams(mgr, dt);
  UpdateGroupCinematicState(mgr);
}

// @ fn_55_D8BC
void CSandBoss::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const bool wasActive = GetActive();
  CPatterned::AcceptScriptMsg(mgr, msg);
  switch (msg.GetMessage()) {
  case kSM_AIUpdateDisabled:
    if (mCollisionManager.get() != nullptr) {
      mCollisionManager->SetPhysicsActive(mgr, false);
    }
    break;
  case kSM_Increment:
    if (msg.GetSenderId() == GetUniqueId() || mWaitMode == 0) {
      ++mRound;
    }
    break;
  case kSM_Deactivate:
    if (wasActive && mCollisionManager.get() != nullptr) {
      mCollisionManager->SetActive(mgr, false);
    }
    break;
  case kSM_Activate:
    if (!wasActive) {
      if (mCollisionManager.get() != nullptr) {
        mCollisionManager->SetActive(mgr, true);
      }
      if (mRound == 0 && mData.commandIndex == 0) {
        // Retail calls GetHealthInfo through the entity's vtable without a cast; the object
        // linked MDFY/ACTV is an actor.
        if (const CActor* source = TCastToConstPtr< CActor >(mgr.ObjectById(mBossInfoSource))) {
          // Retail loads CHealthInfo +0 (initial HP), fn_55_D8BC 0x0000DB14 `lfs f31, 0x0(r3)`.
          mgr.SetBossParams(source->GetUniqueId(), source->GetHealthInfo()->GetInitialHP(),
                            gpStringTable->GetStringIndex("BossSandBoss"));
        }
      }
    }
    break;
  case kSM_AreaLoaded:
    FindConnections(mgr);
    break;
  case kSM_Damage:
    HandleDamage(mgr, msg.GetSenderId());
    break;
  case kSM_Delete:
    // Retail does not test the pointer; the host does.
    if (mCollisionManager.get() != nullptr) {
      mCollisionManager->Destroy(mgr);
    }
    break;
  case kSM_Create:
    BodyController()->SetLocomotionType(pas::kLT_Crouch); // li r4, 0 at 0xD9AC: buried
    BodyController()->Activate(mgr, static_cast< pas::EAnimationState >(-1));
    SetupCollisionActors(mgr);
    break;
  case kSM_HitObject:
    HandleHitObject(mgr, msg.GetSenderId());
    break;
  default:
    break;
  }
}

// @ fn_55_86D0
void CSandBoss::UpdateTimers(float dt) {
  mDarkBeamTimer -= dt;
  mChargeBeamTimer -= dt;
  UpdateFlashColor(dt);
  mF20 += dt;
  mF34 += dt;
}

// @ fn_55_7BB0
void CSandBoss::UpdateFlashColor(float dt) {
  if (mFlashTime > 0.f) {
    float t = mFlashTime - dt;
    if (t < 0.f) {
      t = 0.f;
    }
    mFlashTime = t;
    float blend = mFlashTime / 0.33f;
    if (blend > 1.f) {
      blend = 1.f;
    }
    mFlashColor = CColor::Lerp(CColor::Black(), mDamageColor, blend);
  }
}

// @ fn_55_84A8
// The sibling with the lowest command index decides the group's cinematic state from the states
// the siblings are in (priority 6, 5, 4, 2, 3) and announces it once; the others copy it.
void CSandBoss::UpdateGroupCinematicState(CStateManager& mgr) {
  const CSandBoss* leader = nullptr;
  int count2 = 0;
  int count3 = 0;
  int count4 = 0;
  int count5 = 0;
  int count6 = 0;
  for (int i = 0; i < mSiblings.size(); ++i) {
    const CSandBoss* sibling = CastSandBoss(mgr.ObjectById(mSiblings[i]));
    if (sibling == nullptr) {
      continue;
    }
    if (leader == nullptr || sibling->mData.commandIndex < leader->mData.commandIndex) {
      leader = sibling;
    }
    switch (sibling->mCinematicState) {
    case 2: ++count2; break;
    case 3: ++count3; break;
    case 4: ++count4; break;
    case 5: ++count5; break;
    case 6: ++count6; break;
    default: break;
    }
  }
  if (leader == this) {
    if (count6 > 0) {
      if (mGroupCinematicState != 6) {
        mGroupCinematicState = 6;
      }
    } else if (count5 > 0) {
      if (mGroupCinematicState != 5) {
        mGroupCinematicState = 5;
      }
    } else if (count4 > 0) {
      if (mGroupCinematicState != 4) {
        SendScriptMsgs(kSS_AILogicState3, mgr, GetUniqueId(), kSM_None);
        mGroupCinematicState = 4;
      }
    } else if (count2 > 0) {
      if (mGroupCinematicState != 2) {
        SendScriptMsgs(kSS_AILogicState1, mgr, GetUniqueId(), kSM_None);
        mGroupCinematicState = 2;
      }
    } else if (count3 > 0) {
      if (mGroupCinematicState != 3) {
        SendScriptMsgs(kSS_AILogicState2, mgr, GetUniqueId(), kSM_None);
        mGroupCinematicState = 3;
      }
    }
  } else if (leader != nullptr) {
    mGroupCinematicState = leader->mGroupCinematicState;
  }
}

// @ fn_55_6EA0
// AreaLoaded: resolve the script links, collect every CSandBoss in this area (self included) as
// the sibling list, and draw after the sibling with the next-higher command index.
void CSandBoss::FindConnections(CStateManager& mgr) {
  mSphere = FindConnectedObject(mgr, kSS_Connect, kSM_Attach);
  // The PLAY connection is the charge-beam script effect (the 0xEF8 target fn_55_2750 scales).
  mScriptEffect = FindConnectedObject(mgr, kSS_Play, kSM_Play);
  mBossInfoSource = FindConnectedObject(mgr, kSS_Modify, kSM_Activate);
  mIS02Target = FindConnectedObject(mgr, kSS_InternalState2, kSM_Follow);
  mIS01Target = FindConnectedObject(mgr, kSS_InternalState1, kSM_Follow);
  mWaypoint = FindConnectedObject(mgr, kSS_InternalState0, kSM_Follow);

  CObjectList& list = mgr.ObjectListById(kOL_ListeningAi);
  for (int idx = list.GetFirstObjectIndex(); idx != -1; idx = list.GetNextObjectIndex(idx)) {
    CSandBoss* boss = CastSandBoss(list[idx]);
    if (boss != nullptr && boss->GetCurrentAreaId() == GetCurrentAreaId() &&
        mSiblings.size() < mSiblings.capacity()) {
      mSiblings.push_back(boss->GetUniqueId());
    }
  }

  int nextIndex = 0x7fffffff;
  TUniqueId next = kInvalidUniqueId;
  for (int i = 0; i < mSiblings.size(); ++i) {
    const CSandBoss* sibling = CastSandBoss(mgr.ObjectById(mSiblings[i]));
    if (sibling != nullptr && sibling != this) {
      const int index = sibling->mData.commandIndex;
      if (mData.commandIndex < index && index < nextIndex) {
        next = sibling->GetUniqueId();
        nextIndex = index;
      }
    }
  }
  if (next != kInvalidUniqueId) {
    SetNextDrawNode(next);
  }
}

// ---------------------------------------------------------------------------------------------
// Collision and armour.

// @ fn_55_724C
void CSandBoss::SetupCollisionActors(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > descriptions;
  descriptions.reserve(7);
  const CAnimData* animData = GetModelData()->GetAnimationData();
  for (int i = 0; i < 7; ++i) {
    descriptions.push_back(CJointCollisionDescription::SphereCollision(
        animData->GetLocatorSegId(rstl::string_l(skJointSpheres[i].mName)), CVector3f::Zero(),
        skJointSpheres[i].mRadius, rstl::string_l(skJointSpheres[i].mName), 1000.f));
  }
  mCollisionManager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), descriptions, false);

  for (uint i = 0; i < mCollisionManager->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = mCollisionManager->GetCollisionDescFromIndex(i);
    const TUniqueId id = desc.GetCollisionActorId();
    if (CCollisionActor* colAct = TCastToPtr< CCollisionActor >(mgr.ObjectById(id))) {
      CMaterialFilter filter = colAct->GetMaterialFilter();
      filter.ExcludeList().Add(kMT_Immovable);
      colAct->SetMaterialFilter(filter);
      if (NameIs(desc.GetName(), "Head_1")) {
        mHeadCollider = id;
      }
      colAct->SetDamageVulnerability(*CPatterned::GetDamageVulnerability());
    }
  }
  SyncCollisionHealth(mgr);
  UpdateArmorResponseTypes(mgr);
  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid),
      CMaterialList(kMT_CollisionActor, kMT_Character, kMT_AIPassthrough, kMT_Player)));
}

// @ fn_55_70D0
void CSandBoss::SyncCollisionHealth(CStateManager& mgr) {
  for (uint i = 0; i < mCollisionManager->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = mCollisionManager->GetCollisionDescFromIndex(i);
    if (CCollisionActor* colAct =
            TCastToPtr< CCollisionActor >(mgr.ObjectById(desc.GetCollisionActorId()))) {
      const CHealthInfo health = *GetHealthInfo();
      colAct->SetDamageVulnerability(*CPatterned::GetDamageVulnerability());
      *colAct->HealthInfo() = health;
    }
  }
}

// @ fn_55_6698
// Collision response per sphere: 0x2f by default; a locator whose armour slot shows the attached
// set answers 0x4d (head) or 0x6b (body).
void CSandBoss::UpdateArmorResponseTypes(CStateManager& mgr) {
  for (uint i = 0; i < mCollisionManager->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = mCollisionManager->GetCollisionDescFromIndex(i);
    if (CCollisionActor* colAct =
            TCastToPtr< CCollisionActor >(mgr.ObjectById(desc.GetCollisionActorId()))) {
      colAct->SetResponseType(static_cast< EWeaponCollisionResponseTypes >(0x2f));
      for (int slot = 0; slot < 8; ++slot) {
        if (NameIs(desc.GetName(), skArmorLocators[slot])) {
          if (slot == 0) {
            if (mArmorState[0] == 2) {
              colAct->SetResponseType(static_cast< EWeaponCollisionResponseTypes >(0x4d));
            }
          } else if (mArmorState[slot] == 2) {
            colAct->SetResponseType(static_cast< EWeaponCollisionResponseTypes >(0x6b));
          }
          break;
        }
      }
    }
  }
}

// @ fn_55_6960
// Activate / Deactivate anim events: "Skeleton_Root" swaps the skinned model (tail armour on/off);
// any other armour locator sets its slot to the attached set (on) or none (off). Ignored while
// the stampede runs.
void CSandBoss::SetArmorPieceState(const rstl::string& locator, bool on) {
  if (mStampedeActive) {
    return;
  }
  if (NameIs(locator, "Skeleton_Root")) {
    AnimationData()->SetSkinnedModel(on ? mTailArmorSkinnedModel : mNormalSkinnedModel);
    return;
  }
  for (int slot = 0; slot < 8; ++slot) {
    if (NameIs(locator, skArmorLocators[slot])) {
      mArmorState[slot] = on ? 2 : 0;
      return;
    }
  }
}

// @ fn_55_6A68
void CSandBoss::BuildArmorModels() {
  const CVector3f scale = GetModelData()->GetScale();
  const SLdrSandBossArmorData& attached = mData.attachedArmor;
  const CAssetId attachedIds[7] = {attached.headArmor,   attached.armorPiece2, attached.armorPiece3,
                                   attached.armorPiece4, attached.armorPiece5, attached.armorPiece6,
                                   attached.armorPiece7};
  for (int i = 0; i < 7; ++i) {
    mAttachedArmorModels[i] = CModelData(CStaticRes(attachedIds[i], scale));
  }
  const SLdrSandBossArmorData& stampede = mData.stampedeArmor;
  const CAssetId stampedeIds[8] = {stampede.headArmor,   stampede.armorPiece2, stampede.armorPiece3,
                                   stampede.armorPiece4, stampede.armorPiece5, stampede.armorPiece6,
                                   stampede.armorPiece7, stampede.tailArmor};
  for (int i = 0; i < 8; ++i) {
    mStampedeArmorModels[i] = CModelData(CStaticRes(stampedeIds[i], scale));
  }
}

// @ fn_55_783C
void CSandBoss::SetCollisionVulnerabilities(CStateManager& mgr, const CDamageVulnerability& head,
                                            const CDamageVulnerability& body) {
  for (uint i = 0; i < mCollisionManager->GetNumCollisionActors(); ++i) {
    const CJointCollisionDescription& desc = mCollisionManager->GetCollisionDescFromIndex(i);
    const TUniqueId id = desc.GetCollisionActorId();
    if (CCollisionActor* colAct = TCastToPtr< CCollisionActor >(mgr.ObjectById(id))) {
      colAct->SetDamageVulnerability(id == mHeadCollider ? head : body);
    }
  }
}

// @ fn_55_79B0
// HitObject from a collision actor that touched the player: snap-jaw damage while the jaws are
// closing, otherwise contact damage unless the player is the morph ball being sucked in.
void CSandBoss::HandleHitObject(CStateManager& mgr, TUniqueId sender) {
  const CCollisionActor* colAct = TCastToConstPtr< CCollisionActor >(mgr.GetObjectById(sender));
  if (colAct == nullptr) {
    return;
  }
  const CPlayer* player = mgr.GetPlayer(0);
  if (colAct->GetLastTouchedObject() != player->GetUniqueId() || mCurDamageRemTime > 0.f) {
    return;
  }
  const CMaterialFilter filter =
      CMaterialFilter::MakeIncludeExclude(CMaterialList(kMT_Solid), CMaterialList());
  if (mSnapJawContact) {
    mIsMakingBigStrike = true;
    mDamageDuration = 1.f;
    mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), mSnapJawDamage, filter,
                    CVector3f::Zero());
    mCurDamageRemTime = mDamageWaitTime;
  } else if (!mNotRecovered || mHoldingBall ||
             player->GetMorphballTransitionState() != CPlayer::kMS_Morphed) {
    // player+0x38c == 1 in retail [inferred: the morph-ball state].
    mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), GetContactDamage(),
                    filter, CVector3f::Zero());
    mCurDamageRemTime = mDamageWaitTime;
  }
}

// ---------------------------------------------------------------------------------------------
// Siblings and FSM probes.

// @ fn_55_595C
int CSandBoss::CountAttachedSiblings(const CStateManager& mgr) const {
  int count = 0;
  for (int i = 0; i < mSiblings.size(); ++i) {
    const CSandBoss* sibling = CastSandBoss(mgr.GetObjectById(mSiblings[i]));
    if (sibling != nullptr && sibling->mAttachedToSphere) {
      ++count;
    }
  }
  return count;
}

// @ fn_55_59E4
bool CSandBoss::NoSiblingHasSphereLocked(const CStateManager& mgr) const {
  for (int i = 0; i < mSiblings.size(); ++i) {
    const CSandBoss* sibling = CastSandBoss(mgr.GetObjectById(mSiblings[i]));
    if (sibling != nullptr && sibling->mSphereLocked) {
      return false;
    }
  }
  return true;
}

// @ fn_55_5A84
bool CSandBoss::IsAttachedLeader(const CStateManager& mgr) const {
  if (!mAttachedToSphere) {
    return false;
  }
  for (int i = 0; i < mSiblings.size(); ++i) {
    const CSandBoss* sibling = CastSandBoss(mgr.GetObjectById(mSiblings[i]));
    if (sibling != nullptr && sibling != this && sibling->mAttachedToSphere &&
        sibling->mData.commandIndex < mData.commandIndex) {
      return false;
    }
  }
  return true;
}

// @ fn_55_8CB4
// The probes run one FSM update with a "probing" bit set; the attack triggers record their answer
// in the result bit instead of acting.
bool CSandBoss::ProbeDarkBeam(CStateManager& mgr) {
  mProbingDarkBeam = true;
  mStateMachine->Update(mgr, *this, 0.f);
  mProbingDarkBeam = false;
  return mDarkBeamResult;
}

// @ fn_55_8D24
bool CSandBoss::ProbeTripleCharge(CStateManager& mgr) {
  mProbingTripleCharge = true;
  mStateMachine->Update(mgr, *this, 0.f);
  mProbingTripleCharge = false;
  return mTripleChargeResult;
}

// @ fn_55_8D94
bool CSandBoss::ProbeDoubleCharge(CStateManager& mgr) {
  if (mSyncAttacksAllowed) {
    mProbingDoubleCharge = true;
    mStateMachine->Update(mgr, *this, 0.f);
    mProbingDoubleCharge = false;
  }
  return mDoubleChargeResult;
}

// ---------------------------------------------------------------------------------------------
// Rendering.

// @ fn_55_D290
void CSandBoss::PreRender(CStateManager& mgr) {
  CPatterned::PreRender(mgr);
  mBoneTracking.PreRender(mgr, *AnimationData(), GetTransform(), GetModelData()->GetScale(),
                          *BodyController());
}

// @ fn_55_D08C
void CSandBoss::PreRenderAllViewports(CStateManager& mgr) {
  CPatterned::PreRenderAllViewports(mgr);
  const rstl::optional_object< CAABox > particleBounds =
      AnimationData()->GetParticleDB().GetTotalBounds();
  if (particleBounds) {
    CAABox bounds = GetOtherBounds();
    bounds.AccumulateBounds(particleBounds->GetMinPoint());
    bounds.AccumulateBounds(particleBounds->GetMaxPoint());
    SetOtherBounds(bounds);
    SetRenderBounds(bounds);
  }
}

// @ fn_55_D208
void CSandBoss::AddToRenderer(const CStateManager& mgr) const {
  CPatterned::AddToRenderer(mgr);
}

// @ fn_55_D308
// Retail's Render draws only the particle systems; the body and the armour are drawn by
// fn_55_D208 (CPatterned::AddToRenderer, then CPatterned::Render + fn_55_8A8C unless 0x165C b7).
// The host draws them here instead, from the sorted Render pass CPatterned::AddToRenderer queues,
// so the result matches without drawing during the sort. The vtable (lbl_55_data_760) holds D208 at
// slot 0x40 (AddToRenderer) and D308 at 0x44 (Render), as CSwampBossStage2's A59C/A78C; D208 drawing
// directly is the retail AddToRenderer's own choice.
void CSandBoss::Render(const CStateManager& mgr) const {
  if (!mUnderGround) {
    CPatterned::Render(mgr);
    RenderArmor(mgr, GetTransform(), GetModelFlags(),
                CModelFlags(CModelFlags::kT_Two, 0,
                            static_cast< CModelFlags::EFlags >(CModelFlags::kF_DepthCompare |
                                                               CModelFlags::kF_DepthUpdate),
                            mFlashColor));
  }
  if (!mSkipParticleRender) {
    uint mask = 0;
    uint target = 0;
    if (mDrawParticles) {
      mgr.GetCharacterRenderMaskAndTarget(mask, target);
    }
    CPatterned::RenderSystemsToBeDrawnLast(mgr, mask, target);
  }
}

// @ fn_55_D304
void CSandBoss::RenderSystemsToBeDrawnLast(const CStateManager&, uint, uint) const {}

// @ fn_55_8A8C
// Armour slot i draws the attached set (state 2) or the stampede set (state 1) at locator i;
// the head (slot 0) uses headFlags, the body pieces bodyFlags.
void CSandBoss::RenderArmor(const CStateManager& mgr, const CTransform4f& xf,
                            const CModelFlags& bodyFlags, const CModelFlags& headFlags) const {
  for (int i = 0; i < 8; ++i) {
    const rstl::optional_object< CModelData >* model = nullptr;
    if (mArmorState[i] == 2) {
      model = &mAttachedArmorModels[i];
    } else if (mArmorState[i] == 1) {
      model = &mStampedeArmorModels[i];
    }
    if (model != nullptr && *model) {
      (*model)->Render(mgr, xf * GetScaledLocatorTransform(mArmorLocators[i]), GetActorLights(),
                       i == 0 ? headFlags : bodyFlags);
    }
  }
}

// @ fn_55_89E8
void CSandBoss::RenderWithArmor(const CStateManager& mgr, const CTransform4f& xf,
                                const CModelFlags& flags) const {
  if (!GetModelData()->IsNull()) {
    GetModelData()->Render(CModelData::kWM_Normal, xf, nullptr, flags);
  }
  RenderArmor(mgr, xf, flags, flags);
}

// @ fn_55_88D0
void CSandBoss::RenderSphere(const CStateManager& mgr, const CTransform4f& xf,
                             const CModelFlags& flags) const {
  if (const CActor* sphere = TCastToConstPtr< CActor >(mgr.GetObjectById(mSphere))) {
    CTransform4f sphereXf = xf * GetScaledLocatorTransform(CSegId(1));
    sphereXf.AddTranslation(
        sphereXf.Rotate(-sphere->GetModelData()->GetBounds().GetCenterPoint()));
    sphere->GetModelData()->Render(mgr, sphereXf, sphere->GetActorLights(), flags);
  }
}

// @ fn_55_8740
CAABox CSandBoss::GetArmoredBounds() const {
  return GetAnimationData()->CalcBoundingBoxFromModelVerts().GetTransformedAABox(
      CTransform4f::Translate(-GetTranslation()) * GetTransform() *
      CTransform4f::Scale(GetModelData()->GetScale()));
}

// @ fn_55_CB30
void CSandBoss::ScanVisorRender(const CStateManager& mgr, const CTransform4f& xf,
                                const CModelFlags& flags) const {
  if (!mAttachedToSphere) {
    RenderWithArmor(mgr, xf, flags);
    return;
  }
  RenderSphere(mgr, xf, flags);
  const CQuaternion inverse = CQuaternion::FromMatrix(GetTransform()).BuildInverted();
  for (int i = 0; i < mSiblings.size(); ++i) {
    const CSandBoss* sibling = CastSandBoss(mgr.GetObjectById(mSiblings[i]));
    if (sibling != nullptr && sibling->mAttachedToSphere) {
      sibling->RenderWithArmor(
          mgr, xf * (CQuaternion::FromMatrix(sibling->GetTransform()) * inverse).BuildTransform4f(),
          flags);
    }
  }
}

// @ fn_55_CC84
CAABox CSandBoss::GetScanVisorRenderBounds(const CStateManager& mgr) const {
  CAABox bounds = GetArmoredBounds();
  if (mAttachedToSphere) {
    for (int i = 0; i < mSiblings.size(); ++i) {
      const CSandBoss* sibling = CastSandBoss(mgr.GetObjectById(mSiblings[i]));
      if (sibling != nullptr && sibling != this && sibling->mAttachedToSphere) {
        const CAABox other = sibling->GetArmoredBounds();
        bounds.AccumulateBounds(other.GetMinPoint());
        bounds.AccumulateBounds(other.GetMaxPoint());
      }
    }
  }
  return bounds;
}

// @ fn_55_CF8C
CAABox CSandBoss::GetSortingBounds(const CStateManager&) const {
  const CAABox bounds = GetModelData()->GetBounds(GetTransform());
  const CVector3f center = bounds.GetCenterPoint();
  const CVector3f extent = (bounds.GetMaxPoint() - bounds.GetMinPoint()) * 0.25f;
  return CAABox(center - extent, center + extent);
}

// @ fn_55_CDF8
CVector3f CSandBoss::GetAimPosition(const CStateManager&, float dt) const {
  CVector3f predicted = CVector3f::Zero();
  if (dt > 0.f) {
    predicted = PredictMotion(dt).GetTranslation();
  }
  CVector3f aim = predicted + GetLctrTransform(mArmorLocators[0]).GetTranslation();
  if (mStampedeActive && !(GetTranslation().GetZ() < aim.GetZ())) { // 0xCEB8 bge: NaN raises
    aim.SetZ(GetTranslation().GetZ());
  }
  return aim;
}

// @ fn_55_D384
void CSandBoss::DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node,
                                EUserEventType type, float dt) {
  const rstl::string& locator = node.GetLocatorName();
  bool handled = false;
  switch (type) {
  case kUE_Projectile:
    if (mDarkBeamResult) {
      FireDarkBeam(mgr, dt);
    }
    handled = true;
    break;
  case kUE_DamageOn:
    CrackSphere(mgr);
    handled = true;
    break;
  case kUE_Landing:
    HandleGroundImpact(mgr, locator);
    break;
  case kUE_TakeOff:
    if (CScriptCoverPoint* cover = GetCoverPoint(mgr, mCoverPoint)) {
      cover->SendScriptMsgs(kSS_Arrived, mgr, kInvalidUniqueId, kSM_None);
    }
    HandleGroundImpact(mgr, locator);
    break;
  case kUE_ScreenShake:
    HandleScreenShake(mgr, locator);
    break;
  case kUE_BeginAction:
    if (mNotRecovered) {
      mSuckAir = true;
    } else if (IsAttachedLeader(mgr)) {
      if (mDoubleChargeResult) {
        StartDoubleChargeBeam(mgr, locator);
      } else if (mTripleChargeResult) {
        StartTripleChargeBeam(mgr, locator);
      }
    }
    handled = true;
    break;
  case kUE_IkLock:
    mAttachedToSphere = true;
    handled = true;
    break;
  case kUE_IkRelease:
    mAttachedToSphere = false;
    handled = true;
    break;
  case kUE_ObjectPickUp:
    if (mHoldingBall) {
      if (CScriptWaypoint* waypoint = TCastToPtr< CScriptWaypoint >(mgr.ObjectById(mWaypoint))) {
        CTransform4f xf = GetTransform();
        xf.SetTranslation(GetLctrTransform(locator).GetTranslation());
        waypoint->SetTransform(xf);
        SendScriptMsgs(kSS_InternalState0, mgr, GetUniqueId(), kSM_None);
      }
    }
    handled = true;
    break;
  case kUE_ObjectDrop:
    HandleObjectDrop(mgr);
    handled = true;
    break;
  case kUE_Activate:
    SetArmorPieceState(locator, true);
    UpdateArmorResponseTypes(mgr);
    break;
  case kUE_Deactivate:
    SetArmorPieceState(locator, false);
    UpdateArmorResponseTypes(mgr);
    break;
  default:
    break;
  }
  if (!handled) {
    CPatterned::DoUserAnimEvent(mgr, node, type, dt);
  }
}

// ---------------------------------------------------------------------------------------------
// FSM tables (lbl_55_data_100 / 340 / 5AC, filled by the static initialiser fn_55_11184).

namespace {

// Own members are cast the way CSpacePirate does it (the pointer-to-member types differ only by
// the class), which is fine for non-virtual members of a single-inheritance class.
#define SB_TRIGGER(name)                                                                           \
  { #name, reinterpret_cast< CPatterned::StateMachine::TriggerFunc >(&CSandBoss::name) }
#define SB_STATE(name)                                                                             \
  { #name, reinterpret_cast< CPatterned::StateMachine::StateFunc >(&CSandBoss::name) }
#define SB_CODE(label, name)                                                                       \
  { label, static_cast< CPatterned::StateMachine::CodeFunc >(&CSandBoss::name) }

// @ fn_55_11184
// The module's static initialiser: it fills the 21 trigger, 20 state and 25 code name tables
// (the name strings in lbl_55_rodata_144..554) and the three pointer arrays
// (lbl_55_data_100 / lbl_55_data_340 / lbl_55_data_5AC) that fn_55_CAA4 hands to the state
// machine (0xCAB8..0xCB18). The tables below are the same 66 functions in the same order and
// carry the same names, so the port's static tables are the initialiser's result.
CPatterned::StateMachine::STriggerFunction skTriggers[] = {
    SB_TRIGGER(StateOver),          SB_TRIGGER(IsUnderGround),
    SB_TRIGGER(IsRecovered),        SB_TRIGGER(CanStartNewRound),
    SB_TRIGGER(BeginStampede),      SB_TRIGGER(FoundStampedePoint),
    SB_TRIGGER(StampedeOver),       SB_TRIGGER(ShouldDarkBeamAttack),
    SB_TRIGGER(ShouldRepeaterAttack), SB_TRIGGER(FoundSafeZoneTarget),
    SB_TRIGGER(ShouldTripleCharge), SB_TRIGGER(ShouldDoubleCharge),
    SB_TRIGGER(ShouldSnapJaws),     SB_TRIGGER(ShouldDestroySphere),
    SB_TRIGGER(IsFacingPlayer),     SB_TRIGGER(IsBallInSuckRange),
    SB_TRIGGER(IsLastStage),        SB_TRIGGER(HeadArmorDestroyed),
    SB_TRIGGER(ArmorDestroyed),     SB_TRIGGER(SyncStampede),
    SB_TRIGGER(SyncAttachToSphere),
};

CPatterned::StateMachine::SStateFunction skStates[] = {
    SB_STATE(Recover),        SB_STATE(UnderGround),    SB_STATE(Dead),
    SB_STATE(Deactivate),     SB_STATE(AttachToSphere), SB_STATE(RotateToPlayer),
    SB_STATE(SuckAir),        SB_STATE(SuckBall),       SB_STATE(BallInMouth),
    SB_STATE(DestroySphere),  SB_STATE(JumpOffSphere),  SB_STATE(SpitOutBall),
    SB_STATE(DarkBeamAttack), SB_STATE(RepeaterAttack), SB_STATE(DoubleCharge),
    SB_STATE(TripleCharge),   SB_STATE(SnapJaws),       SB_STATE(SelectStampedePoint),
    SB_STATE(SeekStampedePoint), SB_STATE(Stampede),
};

CPatterned::StateMachine::SCodeFunction skCodes[] = {
    SB_CODE("SetTripleChargeArmor", SetTripleChargeArmor),
    SB_CODE("SetDoubleChargeArmor", SetDoubleChargeArmor),
    SB_CODE("SetStampedeArmor", SetStampedeArmor),
    SB_CODE("SetAttachedArmor", SetAttachedArmor),
    SB_CODE("SetSuckAirArmor", SetSuckAirArmor),
    SB_CODE("RemovePlayerCollision", RemovePlayerCollision),
    SB_CODE("RestorePlayerCollision", RestorePlayerCollision),
    SB_CODE("SelectSafeZoneTarget", SelectSafeZoneTarget),
    SB_CODE("WaitStampede", WaitStampede),
    SB_CODE("WaitAttachToSphere", WaitAttachToSphere),
    SB_CODE("LockSphere", LockSphere),
    SB_CODE("UnLockSphere", UnLockSphere),
    SB_CODE("AllowSyncAttacks", AllowSyncAttacks),
    SB_CODE("PreventSyncAttacks", PreventSyncAttacks),
    SB_CODE("BeginStampede", BeginStampedeCode),
    SB_CODE("EndStampede", EndStampede),
    SB_CODE("PlayHeadArmorExplosion", PlayHeadArmorExplosion),
    SB_CODE("ResetHeadArmorHP", ResetHeadArmorHP),
    SB_CODE("ResetAttackTimes", ResetAttackTimes),
    SB_CODE("SetCinematicStateUnderGround", SetCinematicStateUnderGround),
    SB_CODE("SetCinematicStateStampede", SetCinematicStateStampede),
    SB_CODE("SetCinematicStateNormal", SetCinematicStateNormal),
    SB_CODE("SetCinematicStateStunned", SetCinematicStateStunned),
    SB_CODE("SetCinematicStateChoking", SetCinematicStateChoking),
    SB_CODE("SetCinematicStateExitSphere", SetCinematicStateExitSphere),
};

#undef SB_TRIGGER
#undef SB_STATE
#undef SB_CODE

} // namespace

// @ fn_55_CAA4
void CSandBoss::SetupStateMachine(CStateManager& mgr) {
  CPatterned::SetupStateMachine(mgr);
  StateMachine* stateMachine = mStateMachine.get();
  stateMachine->SetTriggerFunctions(skTriggers, ARRAY_SIZE(skTriggers));
  stateMachine->SetStateFunctions(skStates, ARRAY_SIZE(skStates));
  stateMachine->SetCodeFunctions(skCodes, ARRAY_SIZE(skCodes));
}

// ---------------------------------------------------------------------------------------------
// Loader and module entry points.

// @ fn_55_178
static CEntity* PortLoadSandBoss(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrSandBoss sldrThis;
  // Retail's loader defaults that the generated SLdrSandBoss constructor does not apply.
  SLdrSandBossData& props = sldrThis.sandBossProperties;
  props.attachedArmor.sound_ArmorImpact = -1;
  props.stampedeArmor.sound_ArmorImpact = -1;
  props.stampedeProperties.sound_StampedeArmorExplode = -1;
  props.stampedeProperties.sound_StampedeArmorExplodePain = -1;
  props.unknown_0x7619e561.doubleCharge.sound_ChargeBeam = -1;
  props.unknown_0x7619e561.tripleCharge.sound_ChargeBeam = -1;
  SLdrPlasmaBeamInfo& beam = props.unknown_0x7619e561.chargeBeamInfo;
  beam.travelSpeed = 150.f;
  beam.unknown_0xff713aad = 0;
  beam.innerColor = CColor(0.5f, 0.5f, 0.5f, 0.5f);
  beam.outerColor = CColor(0.6f, 0.6f, 0.f, 0.5f);
  sldrThis.actorInformation.lighting.ambientColor = CColor(1.f, 1.f, 1.f, 1.f);
  sldrThis.actorInformation.visor.visorFlags = 0xf;
#include "MetroidPrime/ScriptLoader/SLdrSandBoss.inc"

  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, kInvalidAssetId,
                     sldrThis.patterned.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  return rs_new CSandBoss(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                          LdrToEntityInfo(info, sldrThis.editorProperties),
                          LdrToTransform4f(sldrThis.editorProperties), *modelData,
                          LdrToActorParameters(sldrThis.actorInformation),
                          LdrToPatternedInfo(sldrThis.patterned, nullptr),
                          sldrThis.sandBossProperties);
}

static SSandBoss_FuncPtrs REL_loader_SandBoss;

void RELMain() {
  REL_loader_SandBoss.mLoader = PortLoadSandBoss;
  SetSSandBoss_FuncPtrs(&REL_loader_SandBoss);
}

void RELExit() { SetSSandBoss_FuncPtrs(nullptr); }
