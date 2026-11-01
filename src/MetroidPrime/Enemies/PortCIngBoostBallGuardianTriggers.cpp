// The Ing half of the Boost Guardian's state machine: the triggers, the code functions and the
// shared helpers the states use. The behaviour is the Ing's, ported from the Ing host body (branch
// decomp/ing, src/MetroidPrime/Enemies/PortCIng.cpp).
//
// This module (IngBoostBallGuardian) carries its own copy of the Ing code, so every `fn_29_`
// address in the comments below is the Ing REL's address, not this module's; the module's own
// addresses are in docs/module-analysis/BoostGuardian_fsm.tsv.

#include "MetroidPrime/Enemies/CIngBoostBallGuardian.hpp"

#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Particles/CGenDescription.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/BodyState/CBodyStateCmdMgr.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CAxisAngle.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CCollisionActorManager.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CGameLight.hpp"
#include "MetroidPrime/Enemies/CCollisionTracker.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CKnockBackInfo.hpp"
#include "MetroidPrime/Player/CMorphBall.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "MetroidPrime/CSafeZoneManager.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Enemies/CGenericFSM2.hpp"
#include "MetroidPrime/Enemies/CPatternedInfo.hpp"
#include "MetroidPrime/Enemies/CTeamAiRole.hpp"
#include "MetroidPrime/Enemies/PortBoostGuardianShared.hpp"
#include "MetroidPrime/HUD/CHUDMemoParms.hpp"
#include "MetroidPrime/HUD/CSamusHud.hpp"
#include "MetroidPrime/PathFinding/CPathFindArea.hpp"
#include "MetroidPrime/Player/CEnvironmentVariable.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrIng.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CHUDBillboardEffect.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAIWaypoint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSafeZone.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTeamAiMgr.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSpecialFunction.hpp"
#include "MetroidPrime/Enemies/PortCBoostBallGuardianBall.hpp"

#include <stdio.h>
#include <stdlib.h>

namespace {

// The Ing's file-local solid filter (the damage filter that only takes solid material) is shared
// with the other files of this module: PortBoostGuardianShared.hpp has SolidFilter().

} // namespace

// ---- the collision actors --------------------------------------------------------------------

// fn_30_4E90: the body manager (guest 0xD60), the seven joint spheres of lbl_30_rodata_0, built
// in kSM_Create (0x0000DF18) before the ing-spot manager; the Ing REL's fn_29_3190 with two more
// materials (0x00005018: kMT_Unknown54 and kMT_ProjectilePassthrough) and two more exclusions in
// the filter (lbl_30_data_9A0..9B0: CollisionActor, AIPassthrough, Player, Projectile, Bomb).
// @ fn_30_4E90
void CIngBoostBallGuardian::CreateCollisionActorManager(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(7); // 0x00004EC0: fn_30_10F9C, the vector's reserve
  const CAnimData* anim = GetModelData()->GetAnimationData();
  for (uint i = 0; i < 7; ++i) {
    const SIngJointSphere& sphere = skIngJointSpheres[i];
    const CSegId seg = anim->GetLocatorSegId(rstl::string_l(sphere.mLocator));
    // 0x00004F54/0x00004F78: the description's copy and push_back_unsafe
    joints.push_back_unsafe(CJointCollisionDescription::SphereCollision( // fn_30_4C58, fn_30_4BD8
        seg, CVector3f::Zero(), sphere.mRadius, rstl::string_l(sphere.mLocator), kIngJointMass));
  }
  CCollisionActorManager* manager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, false);
  mCollisionActorManager0 = manager; // single_ptr: deletes the previous one
  SetupManagerActors(mgr, mCollisionActorManager0.get());
  AddMaterial(kMT_Unknown54, kMT_ProjectilePassthrough, mgr);
  // 0x000050F4: the vector's destructor runs at scope end
  SetMaterialFilter(CMaterialFilter::MakeIncludeExclude( // fn_30_4D74
      CMaterialList(kMT_Solid), // lbl_30_data_99C
      CMaterialList(kMT_CollisionActor, kMT_AIPassthrough, kMT_Player, kMT_Projectile, kMT_Bomb)));
}

// fn_30_4A38: the ing-spot manager (guest 0xD64), built in kSM_Create after the body manager. The
// vector helpers are rstl inlines: fn_30_10F9C reserve, fn_30_4BD8 push_back_unsafe, fn_30_4C58
// the description's copy, fn_30_4D74 the vector's destructor.
// @ fn_30_4A38
void CIngBoostBallGuardian::CreateIngSpotManager(CStateManager& mgr) {
  rstl::vector< CJointCollisionDescription > joints;
  joints.reserve(1); // 0x00004A70: fn_30_10F9C
  const CSegId root = GetModelData()->GetAnimationData()->GetLocatorSegId(rstl::string_l("Root"));
  // 0x00004B0C/0x00004B2C: the description's copy and push_back_unsafe
  joints.push_back_unsafe(CJointCollisionDescription::AABoxCollision( // fn_30_4C58, fn_30_4BD8
      root, CVector3f(5.f, 5.f, 1.5f), rstl::string_l("PuddleCollisionActor"), 1000.f));
  CCollisionActorManager* manager =
      rs_new CCollisionActorManager(mgr, GetUniqueId(), GetCurrentAreaId(), joints, false);
  mCollisionActorManager1 = manager; // single_ptr: deletes the previous one
  // 0x00004BB8: the vector's destructor runs at scope end
  SetupManagerActors(mgr, mCollisionActorManager1.get()); // fn_30_4D74
}

// fn_30_4924: the Ing REL's fn_29_3014 for a given manager; the vulnerability call is the
// non-virtual CPatterned one.
// @ fn_30_4924
void CIngBoostBallGuardian::SetupManagerActors(CStateManager& mgr,
                                               CCollisionActorManager* const& manager) {
  for (uint i = 0; i < manager->GetNumCollisionActors(); ++i) {
    const TUniqueId id = manager->GetCollisionDescFromIndex(i).GetCollisionActorId();
    if (CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id))) {
      const CHealthInfo health = *GetHealthInfo();
      actor->SetDamageVulnerability(*CPatterned::GetDamageVulnerability());
      *actor->HealthInfo() = health;
    }
  }
}

// @ fn_30_648C
TUniqueId CIngBoostBallGuardian::CreateBlobTracker(CStateManager& mgr,
                                                   const TLockedToken< CGenDescription >& desc) {
  const TUniqueId id = mgr.AllocateUniqueId();
  if (id != kInvalidUniqueId) {
    // 0x00006568: GeomBlobV2's CCollisionTracker constructor
    CCollisionTracker* tracker = rs_new CCollisionTracker( // fn_25_4290
        desc, id, GetCurrentAreaId(), true, rstl::string_l("IngBlobEffect"),
        CTransform4f::Translate(GetTranslation()), GetUniqueId(), 0,
        CCollisionTracker::skDefaultExtents);
    tracker->SetParticleEmissionRateScalar(0.f); // 0x0000658C: fn_25_256C
    tracker->SetNextDrawNode(GetUniqueId()); // 0x000065A4: CActor 0x12A
    mgr.AddObject(tracker);
  }
  return id;
}

// @ fn_30_67B4
void CIngBoostBallGuardian::CreateHitLight(CStateManager& mgr) {
  const TUniqueId id = mgr.AllocateUniqueId();
  CGameLight* light = rs_new CGameLight(
      id, GetAreaIdForPersistence(), GetActive(), rstl::string_l("Ing Light"),
      CTransform4f::Identity(), GetUniqueId(), CLight::BuildPoint(CVector3f::Zero(), CColor::Black()),
      0, 0, 0.f);
  if (light != nullptr) {
    mHitLightId = id;
    mgr.AddObject(light);
  }
}

// @ fn_30_510C
void CIngBoostBallGuardian::LeaveTeam(CStateManager& mgr) {
  if (mTeamId == kInvalidUniqueId) { // guest 0x1086
    return;
  }
  if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamId))) {
    if (team->IsPartOfTeam(GetUniqueId())) {
      team->QuitTeam(GetUniqueId());
      mTeamId = kInvalidUniqueId;
    }
  }
}

// fn_30_30E4: every collision actor of the guest's second manager (0xD64) takes the boost-ball
// vulnerability (0x1178) or the ing spot's (0xA5C) (0x0000310C).
// Both arms ran in area 28; every collision actor id resolves there, so the failed cast at
// 0x00003150 is data-limited.
// @ fn_30_30E4
void CIngBoostBallGuardian::SetIngSpotManagerVulnerability(CStateManager& mgr, bool boost) {
  const CDamageVulnerability& vuln = boost ? mVulnerability : mData.mIngSpotVulnerability;
  for (uint i = 0; i < mCollisionActorManager1->GetNumCollisionActors(); ++i) {
    const TUniqueId id =
        mCollisionActorManager1->GetCollisionDescFromIndex(i).GetCollisionActorId();
    if (CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id))) {
      actor->SetDamageVulnerability(vuln); // 0x00003158
    }
  }
}

// fn_30_3190: records the reflect flag, then every collision actor of the body manager (guest
// 0xD60) takes the reflect vulnerability or CPatterned's own
// (0x000031C8 / 0x000031D0, the non-virtual call).
// Both arms ran in area 28; as in fn_30_30E4 the failed cast is data-limited.
// @ fn_30_3190
void CIngBoostBallGuardian::SetBodyManagerReflect(CStateManager& mgr, bool reflect) {
  mReflectVulnerability = reflect; // 0x000031BC: 0x11EE 0x02
  const CDamageVulnerability& vuln = reflect ? CDamageVulnerability::ReflectVulnerabilty()
                                             : *CPatterned::GetDamageVulnerability();
  for (uint i = 0; i < mCollisionActorManager0->GetNumCollisionActors(); ++i) {
    const TUniqueId id =
        mCollisionActorManager0->GetCollisionDescFromIndex(i).GetCollisionActorId();
    if (CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(id))) {
      actor->SetDamageVulnerability(vuln); // 0x00003214
    }
  }
}

// fn_30_3068: entering (when allowed) raises the boost, the puddle form and the puddle generate
// bits and zeroes 0x11D4; anything else leaves the puddle form, zeroes 0x11D8/0x11DC and reloads
// the puddle mode time from the current phase.
// @ fn_30_3068
void CIngBoostBallGuardian::SetBoostPuddleMode(bool enter) {
  if (enter && mBoostPuddleAllowed) { // 0x00003068; 0x00003074: 0x11EF 0x20
    mShouldBoost = true;              // 0x00003084: 0x11EE 0x10 = 1
    mBoostPuddleForm = true;          // 0x00003098: 0x11EE 0x08 = 1
    mBoostPuddleGenerate = true;      // 0x000030A4: 0x11EE 0x04 = 1
    mBoost11D4 = 0.f;                 // 0x000030AC
    return;
  }
  mBoostPuddleForm = false; // 0x000030C0: 0x11EE 0x08 = 0
  mBoost11D8 = 0.f;         // 0x000030CC
  mBoostTimer = 0.f;        // 0x000030D0: 0x11DC
  mBoostPuddleModeTimer = CurrentPhase().mBoostPuddleModeTime; // 0x000030DC: *(0x11D0)+0
}

// fn_30_47E8 (this module).
// Ing body fn_29_2ED8 (kSM_HitObject): when a collision actor that last touched the player reports a hit
// and the contact damage is off cooldown, the player takes the Ing's contact damage.
// @ fn_30_47E8
void CIngBoostBallGuardian::CollisionActorHitPlayer(CStateManager& mgr, TUniqueId sender) {
  const CCollisionActor* actor = TCastToConstPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (actor == nullptr) {
    return;
  }
  const CPlayer* player = mgr.GetPlayer(0);
  if (player == nullptr || actor->GetLastTouchedObject() != player->GetUniqueId()) {
    return;
  }
  // xE5C_1 (0xe5c bit 1) suppresses the contact damage; fn_29_6A88 owns that bit.
  if (mCurDamageRemTime > 0.f || xE5C_1) {
    return;
  }
  mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), GetUniqueId(), GetContactDamage(),
                  SolidFilter(), CVector3f::Zero());
  mCurDamageRemTime = mDamageWaitTime;
}

// @ fn_30_3CB0
void CIngBoostBallGuardian::SpawnSpotHitEffect(CStateManager& mgr, float damage) {
  // The ing-spot effects while the ing blob tracker (0x1074) has live particles, else the boost's.
  bool emitting = false;
  CEntity* ent = mgr.ObjectById(mIngBlobTrackerId);
  if (ent != nullptr) {
    CCollisionTracker* tracker = static_cast< CCollisionTracker* >(ent);
    if (tracker->GetParticleSystem() != nullptr &&
        tracker->GetParticleSystem()->GetParticleCount() > 0) {
      emitting = true;
    }
  }
  CEntity* explosion = nullptr;
  const bool heavy = damage >= 25.f; // 0x00003D40 fcmpo, cror eq,gt,eq: NaN is not heavy
  const rstl::optional_object< TLockedToken< CGenDescription > >& effect =
      heavy ? (emitting ? mHeavyHitEffect : mBoostHeavyHitEffect)
            : (emitting ? mNormalHitEffect : mBoostNormalHitEffect);
  if (effect) {
    explosion = rs_new CExplosion(*effect, mgr.AllocateUniqueId(),
                                  CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true,
                                              kInvalidEditorId),
                                  rstl::string_l("Ing Damage Fx"), GetTransform(), 0,
                                  GetModelData()->GetScale(), CColor::White(), -1);
  }
  CSfxManager::AddEmitter(heavy ? mData.mIngSpot.GetHeavyHitSound()
                                : mData.mIngSpot.GetNormalHitSound(), // guest 0x7F8 / 0x7F4
                          GetTranslation(), 0x7f, GetCurrentAreaId().Value(), true, false,
                          CSfxManager::kMedPriority);
  if (explosion != nullptr) {
    mgr.AddObject(explosion);
  }
}

// @ fn_30_4030
void CIngBoostBallGuardian::AccumulatePuddleDamage(CStateManager& mgr, float hp, float damage) {
  mBoost11D4 += damage;
  if (mBoost11D4 >= hp * mBoost.mUnknownB0E8) { // guest 0xEC0
    SetBoostPuddleMode(true);
  }
}

// @ fn_30_4078
void CIngBoostBallGuardian::BodyColliderDamage(CStateManager& mgr, TUniqueId sender) {
  if (!mAlive) { // CPatterned 0x420 bit 6
    return;
  }
  CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (actor == nullptr) {
    return;
  }
  if (mCollisionActorManager0->GetCollisionDescIndexFromUniqueId(sender) == -1) { // guest 0xD60
    return;
  }
  const TUniqueId touched = actor->GetLastTouchedObject();
  CHealthInfo* actorHealth = actor->HealthInfo();
  CHealthInfo* health = HealthInfo();
  const float initial = health->GetInitialHP();
  if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(touched))) {
    const float damage = initial - actorHealth->GetHP();
    const float hp = health->GetHP();
    if (!mBoostPuddleForm) {
      AccumulatePuddleDamage(mgr, initial, damage);
    }
    // A hit that would kill leaves 1 HP and ends puddle mode for good, except while hits are
    // lethal (outside the body-projectile form).
    bool survive = false;
    if ((mForm == kForm_BodyProjectile || !mBodyHitsLethal) && hp >= 0.f && hp <= damage) {
      survive = true;
    }
    if (survive) {
      health->SetHP(1.f);
      SetBoostPuddleMode(false);
      mBoostPuddleAllowed = false; // 0x00004204: 0x11EF 0x20 = 0
    } else {
      health->SetHP(hp - damage);
    }
    const CVector3f direction = weapon->GetTransform().GetForward();
    TakeDamage(direction, damage); // 0xa4
    if (hp <= damage && !survive) {
      Death(mgr, direction, kSS_DeathRattle); // 0x98, 0x5241544C
      mgr.RecordDamageSource(*this, weapon->GetUniqueId(), weapon->GetCurrentDamageInfo(), true,
                             false);
    }
    KnockBack(mgr, CKnockBackInfo(direction, touched, weapon->GetOwnerId(),
                                  weapon->GetCurrentDamageInfo(), true)); // 0x9c
  }
  actorHealth->SetHP(initial);
}

// @ fn_30_4324
void CIngBoostBallGuardian::PuddleColliderResistedDamage(CStateManager& mgr, TUniqueId sender) {
  if (!mAlive) {
    return;
  }
  CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (actor == nullptr) {
    return;
  }
  if (mCollisionActorManager1->GetCollisionDescIndexFromUniqueId(sender) == -1) { // guest 0xD64
    return;
  }
  const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(actor->GetLastTouchedObject()));
  if (weapon == nullptr || weapon->GetType() != kWT_Bomb) {
    return;
  }
  mBoostHitTimer = CPatterned::skDamageHitTime; // 0x000043E4: 0x1080
  mHitByPlayerProjectile = true;                // 0x000043F0: 0x420 bit 7
  if (DebugBg()) { // host trace
    fprintf(stderr, "[ibbg] %04x puddle resisted bomb %.1f: puddle form %d boost over %d 11D8 %.1f\n",
            GetUniqueId().Value(), weapon->GetCurrentDamageInfo().GetDamage(),
            int(mCurrentlyInBoostPuddleForm), int(mBoostOver), mBoost11D8);
  }
  if (!mCurrentlyInBoostPuddleForm || !mBoostOver) {
    return;
  }
  mBoost11D8 += weapon->GetCurrentDamageInfo().GetDamage();
  mBoostTimer += weapon->GetCurrentDamageInfo().GetDamage(); // guest 0x11DC
  const CIngBoostBallGuardianPhase& phase = CurrentPhase();
  if (mBoost11D8 >= phase.mBreakBoostPuddleModeDamage) {
    SetBoostPuddleMode(false);
  } else if (mBoostTimer > phase.mUnknownABE9 * phase.mBreakBoostPuddleModeDamage) {
    mShouldBoost = true; // 0x0000447C: 0x11EE 0x10 = 1
    mBoostTimer = 0.f;
  }
}

// @ fn_30_44A4
void CIngBoostBallGuardian::PuddleColliderDamage(CStateManager& mgr, TUniqueId sender) {
  if (!mAlive) {
    return;
  }
  CCollisionActor* actor = TCastToPtr< CCollisionActor >(mgr.ObjectById(sender));
  if (actor != nullptr &&
      mCollisionActorManager1->GetCollisionDescIndexFromUniqueId(sender) != -1) { // guest 0xD64
    const TUniqueId touched = actor->GetLastTouchedObject();
    CHealthInfo* actorHealth = actor->HealthInfo();
    CHealthInfo* health = HealthInfo();
    const float initial = health->GetInitialHP();
    if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(mgr.GetObjectById(touched))) {
      const float damage = initial - actorHealth->GetHP();
      if (DebugBg()) { // host trace
        const CWeaponTypeVulnerability rate =
            actor->GetDamageVulnerability()->GetVulnerability(weapon->GetCurrentDamageInfo().GetWeaponMode());
        fprintf(stderr,
                "[ibbg] %04x puddle damage %.1f (weapon type %d, x%.2f effect %d), hp %.1f, boost "
                "puddle form %d\n",
                GetUniqueId().Value(), damage, int(weapon->GetType()), rate.mDamageMultiplier,
                int(rate.mEffect), health->GetHP(), int(mCurrentlyInBoostPuddleForm));
      }
      const float hp = health->GetHP();
      AccumulatePuddleDamage(mgr, GetHealthInfo()->GetHP(), damage);
      // The puddle never kills: a lethal hit leaves 1 HP and ends puddle mode for good.
      if (hp >= 0.f && hp <= damage) {
        health->SetHP(1.f);
        SetBoostPuddleMode(false);
        mBoostPuddleAllowed = false; // 0x00004610: 0x11EF 0x20 = 0
      } else {
        health->SetHP(hp - damage);
      }
      const CVector3f direction = weapon->GetTransform().GetForward();
      TakeDamage(direction, damage); // 0xa4
      KnockBack(mgr, CKnockBackInfo(direction, touched, weapon->GetOwnerId(),
                                    weapon->GetCurrentDamageInfo(), true)); // 0x9c
      SpawnSpotHitEffect(mgr, damage); // fn_30_3CB0
    }
    actorHealth->SetHP(initial);
    return;
  }
  // 0x000046B8: the boost ball relays what hit it (its 0x4F6, fn_30_13B7C).
  CEntity* ent = mgr.ObjectById(sender);
  CBoostBallGuardianBall* ball =
      ent != nullptr
          ? static_cast< CBoostBallGuardianBall* >(ent->TypesMatch(CBoostBallGuardianBall::kTypeId))
          : nullptr; // fn_800979F0
  if (ball == nullptr) {
    return;
  }
  const CEntity* hitter = mgr.GetObjectById(ball->GetLastSender());
  if (TCastToConstPtr< CScriptSpecialFunction >(hitter)) {
    mCollidedWithCrystal = true; // 0x00004714: 0x11EE 0x01
  } else if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(hitter)) {
    if (weapon->GetType() == kWT_Bomb) {
      mBoost11E0 += weapon->GetCurrentDamageInfo().GetDamage();
      if (mBoost11E0 >= CurrentPhase().mBreakBoostModeDamage) {
        mCollidedWithCrystal = true; // 0x0000476C
      }
    }
  }
}

// @ fn_30_479C
void CIngBoostBallGuardian::ColliderDamage(CStateManager& mgr, TUniqueId sender) {
  if (mForm == kForm_IngSpot) {
    PuddleColliderDamage(mgr, sender);
  } else {
    BodyColliderDamage(mgr, sender);
  }
}

// ---- helpers ----------------------------------------------------------------------------------

void CIngBoostBallGuardian::LogState(const char* name) {
  mStateTime = 0.f;
  if (DebugBg() && mLastLoggedState != name) {
    mLastLoggedState = name;
    fprintf(stderr, "[ibbg] %04x state %s form %d\n", GetUniqueId().Value(), name, mForm);
  }
}

// Area 28 has no CScriptTeamAiMgr: GetAssociatedTeamId stays invalid there, so the JoinTeam call
// (0x00005244) never runs; a preset id that is no team leaves at the cast (0x00005230).
// @ fn_30_51B4
void CIngBoostBallGuardian::JoinTeam(CStateManager& mgr) {
  if (mTeamId == kInvalidUniqueId) {
    mTeamId = CScriptTeamAiMgr::GetAssociatedTeamId(*this, mgr);
  }
  if (mTeamId != kInvalidUniqueId) {
    if (CScriptTeamAiMgr* team = TCastToPtr< CScriptTeamAiMgr >(mgr.ObjectById(mTeamId))) {
      // 0x00005238: r5 = 1, r6 = 2, r7 = -1.
      team->JoinTeam(*this, CTeamAiRole::kTAR_Melee, CTeamAiRole::kTAR_Projectile,
                     CTeamAiRole::kTAR_Invalid);
    }
  }
}

// fn_30_53D0 (this module).
// The DOL's surface move shared with MinorIng (fn_44_5384): drop the part of the move along the
// surface normal when it is under 0.6 of the move (0x00005478: rodata_D8), step speed * dt along
// the normalised move and keep that direction (0x1110), else zero it; then Stop().
// @ fn_30_53D0
void CIngBoostBallGuardian::MoveOnSurface(const CVector3f& moveIn, float speed, float dt) {
  const CVector3f pos = GetTranslation();
  const CVector3f normal = mSurfaceAlignment.GetSurface().GetNormal();
  CVector3f move = moveIn;
  const float along = CVector3f::Dot(move, normal);
  if (along * along < move.MagSquared() * 0.6f) {
    move = move - normal * along;
  }
  if (!move.IsMagnitudeSafe()) {
    mSurfaceMoveDir = CVector3f::Zero();
  } else {
    const CVector3f dir = move.AsNormalized();
    SetTranslation(pos + dir * (speed * dt));
    mSurfaceMoveDir = dir;
  }
  Stop();
}

void CIngBoostBallGuardian::ApplySeparation(CStateManager& mgr) {
  CVector3f total = CVector3f::Zero();
  const CObjectList& list = mgr.GetObjectListById(kOL_ListeningAi);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    const CPatterned* other = TCastToConstPtr< CPatterned >(list[i]);
    if (other != nullptr && other != this && other->GetCurrentAreaId() == GetCurrentAreaId()) {
      const CVector2f separation = mSteeringBehaviors.Separation2D(
          *this, CVector2f(other->GetTranslation().GetX(), other->GetTranslation().GetY()),
          mModelScale.GetX() * 15.f);
      const CVector3f separation3(separation.GetX(), separation.GetY(), 0.f);
      if (separation3.IsMagnitudeSafe()) {
        total += separation3;
      }
    }
  }
  if (total != CVector3f::Zero()) {
    BodyController()->CommandMgr().DeliverCmd(CBCLocomotionCmd(total, CVector3f::Zero(), 0.5f));
  }
}

float CIngBoostBallGuardian::DistSqToTarget(const CStateManager& mgr) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  if (target == nullptr) {
    return FLT_MAX;
  }
  return (target->GetTranslation() - GetTranslation()).MagSquared();
}

// fn_30_60C0 (this module). Retail reads the cover hint id (0x108E) itself; the host takes the id
// so ReleaseCoverHint can share it.
// @ fn_30_60C0
CScriptAIHint* CIngBoostBallGuardian::GetCoverHint(CStateManager& mgr, TUniqueId id) const {
  if (id == kInvalidUniqueId) {
    return nullptr;
  }
  return TCastToPtr< CScriptAIHint >(mgr.ObjectById(id));
}

void CIngBoostBallGuardian::ReleaseCoverHint(CStateManager& mgr, TUniqueId& id) {
  if (CScriptAIHint* hint = GetCoverHint(mgr, id)) {
    hint->SetInUse(false);
    hint->SetTimeRemaining(0.f);
  }
  id = kInvalidUniqueId;
}

// @ fn_30_6064
void CIngBoostBallGuardian::ReleaseOwnCoverHint(CStateManager& mgr) {
  if (CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId)) { // fn_30_60C0
    hint->SetInUse(false);
    hint->SetTimeRemaining(0.f); // 0x0000609C: hint 0x170
    mCoverHintId = kInvalidUniqueId;
  }
}

// @ fn_30_5D90: the module's own timers. The Ing's ball cooldown and body-projectile time are not
// in it; the boost countdowns are: in the boost puddle form (0x11EF 0x80, 0x11EE 0x08 and 0x20,
// ing spot) 0x11E8 runs down to ShouldBoost; otherwise, outside the ing spot form, 0x11E4 runs down
// (outside cinematics) to the boost puddle mode (fn_30_3068).
void CIngBoostBallGuardian::UpdateTimers(float dt, CStateManager& mgr) {
  mHeardShotTime += dt;
  mArmSwipeTime += dt;
  mFormTime += dt;
  mStateTime += dt; // [port] the host-only state clock
  if (mForm == kForm_Corporeal && PathShagged(mgr, CTriggerData(0.f))) {
    mFrustrationTimer += dt;
  } else {
    mFrustrationTimer = 0.f;
  }
  if (mForm == kForm_IngSpot && mCurDamageRemTime > 0.f) {
    mCurDamageRemTime -= dt;
  }
  if (mHitByPlayerProjectile) {
    mUnderFireTime = 0.f;
    mHitByPlayerProjectile = false;
  } else {
    mUnderFireTime += dt;
  }
  if (mCurrentlyInBoostPuddleForm && mBoostPuddleForm && mBoostOver && mForm == kForm_IngSpot) {
    if (mBoostRandomFloat > 0.f) {
      mBoostRandomFloat -= dt;
      if (mBoostRandomFloat <= 0.f) {
        mShouldBoost = true;
      }
    }
  } else if (!mBoostPuddleForm && mForm != kForm_IngSpot &&
             !mgr.GetCameraManager(0)->IsInCinematicCamera()) {
    mBoostPuddleModeTimer -= dt;
    if (mBoostPuddleModeTimer <= 0.f) {
      SetBoostPuddleMode(true);
    }
  }
}

// fn_30_5CD0: forms 0/1 are never targets; the ing-spot and evaporate forms (3/5) only in the
// dark visor or with the always-targetable data flag; every other form is.
// @ fn_30_5CD0
void CIngBoostBallGuardian::UpdateValidTarget(CStateManager& mgr) {
  switch (mForm) {
  case 0:
  case 1:
    SetValidTarget(0, false);
    break;
  case kForm_IngSpot:
  case kForm_Evaporate:
    SetValidTarget(0, mgr.GetPlayerState(0)->GetActiveVisor(mgr) == CPlayerState::kPV_Dark ||
                          (mData.mFlags210 & kFlag_AlwaysTargetable) != 0); // 0x00005D58
    break;
  default:
    SetValidTarget(0, true);
    break;
  }
}

// fn_30_3AF8: the ing-spot loops. Only in the ing-spot form with the boost over (0x11EE bit 5):
// the move loop (0x107c, data 0x7f0) while moving (0x11ED bit 2), else the idle loop (0x1078, data
// 0x7ec). A loop is added at the actor's position (0x54) with the volume 0x7f, or, when it already
// plays, follows the position and the transform's forward column (0x28/0x38/0x48). Every loop that
// is not wanted this frame is removed (0x00003C3C/0x00003C68), the move loop first.
// @ fn_30_3AF8
void CIngBoostBallGuardian::UpdateLoopSfx() {
  bool removeMove = true;
  bool removeIdle = true;
  if (mForm == kForm_IngSpot && mBoostOver) {
    if (mMoving) {
      removeMove = false;
      if (!mLoopSfx[1]) {
        mLoopSfx[1] = CSfxManager::AddEmitter(mData.mIngSpot.GetMoveSound(), GetTranslation(), 0x7f,
                                              GetCurrentAreaId().Value(), true, true,
                                              CSfxManager::kMedPriority);
      } else {
        CSfxManager::UpdateEmitter(mLoopSfx[1], GetTranslation(), GetTransform().GetForward(), 0x7f);
      }
    } else {
      removeIdle = false;
      if (!mLoopSfx[0]) {
        mLoopSfx[0] = CSfxManager::AddEmitter(mData.mIngSpot.GetIdleSound(), GetTranslation(), 0x7f,
                                              GetCurrentAreaId().Value(), true, true,
                                              CSfxManager::kMedPriority);
      } else {
        CSfxManager::UpdateEmitter(mLoopSfx[0], GetTranslation(), GetTransform().GetForward(), 0x7f);
      }
    }
  }
  if (removeMove && mLoopSfx[1]) {
    CSfxManager::RemoveEmitter(mLoopSfx[1]);
    mLoopSfx[1] = CSfxHandle();
  }
  if (removeIdle && mLoopSfx[0]) {
    CSfxManager::RemoveEmitter(mLoopSfx[0]);
    mLoopSfx[0] = CSfxHandle();
  }
}

// fn_30_3A60: the body projectile's (0x1070), the two loops' (0x1078/0x107C) and the suck
// sound's (0xFFC) emitters are removed (0x00003A7C/0x00003A98/0x00003AB4/0x00003AD0: each only
// when set); retail leaves the handles as they are.
// @ fn_30_3A60
void CIngBoostBallGuardian::RemoveSfxEmitters() {
  if (mBodyProjectileSfx) {
    CSfxManager::RemoveEmitter(mBodyProjectileSfx);
  }
  if (mLoopSfx[0]) {
    CSfxManager::RemoveEmitter(mLoopSfx[0]);
  }
  if (mLoopSfx[1]) {
    CSfxManager::RemoveEmitter(mLoopSfx[1]);
  }
  if (mSuckSfx) {
    CSfxManager::RemoveEmitter(mSuckSfx);
  }
}

// The Ing's StopSfx (the Ing REL's fn_29_2194), for the Ing half's Death.
void CIngBoostBallGuardian::StopSfx() {
  for (int i = 0; i < 2; ++i) {
    if (mLoopSfx[i]) {
      CSfxManager::SfxStop(mLoopSfx[i]);
      mLoopSfx[i] = CSfxHandle();
    }
  }
}

// ---- triggers --------------------------------------------------------------------------------

// Ing body fn_29_B1F4
// @ fn_30_C6B8
bool CIngBoostBallGuardian::StateOver(CStateManager&, const CTriggerData&) const {
  return mAnimationState.IsOver();
}

// fn_30_C6AC (the Ing's fn_29_B1E8): 0x11ED 0x08.
// @ fn_30_C6AC
bool CIngBoostBallGuardian::IsAlert(CStateManager&, const CTriggerData&) const { return mAlert; }

// fn_30_C68C (this module).
// fn_29_B1C8. The asm tests mHostId (0xcdc) against kInvalidUniqueId -
// the lhz/subf/or/srwi-31 idiom - and nothing else; it does not look at the form. That is what lets
// SuckEnergy, which sets form 0 and then clears the host once its suck time runs out, take the
// !IsControllingHost trigger into SetExitHostDest and on into ExitHost.
// @ fn_30_C68C
bool CIngBoostBallGuardian::IsControllingHost(CStateManager&, const CTriggerData&) const {
  return mHostId != kInvalidUniqueId; // 0xcdc
}

// Ing body fn_29_B0E0
// @ fn_30_C5A4
bool CIngBoostBallGuardian::IsFacingTarget(CStateManager& mgr, const CTriggerData&) const {
  // 0x0000C5E8: the target's aim position (fn_30_5F6C, prediction rodata_70 = 0.0) into a zeroed
  // vector, not its translation; no target counts as facing it (0x0000C674).
  CVector3f aim = CVector3f::Zero();
  if (!TargetAimPosition(mgr, aim)) {
    return true;
  }
  CVector3f delta = aim - GetTranslation();
  delta = CVector3f(delta.GetX(), delta.GetY(), 0.f); // 0x0000C638: rodata_70
  return CVector3f::GetAngleDiff(GetTransform().GetForward(), delta) <= kFacingAngle;
}

// Ing body fn_29_B0A4
// @ fn_30_C568
bool CIngBoostBallGuardian::HasTarget(CStateManager& mgr, const CTriggerData&) const {
  return mgr.GetObjectById(mTargetId) != nullptr;
}

// Ing body fn_29_B04C
// @ fn_30_C510
bool CIngBoostBallGuardian::HasNewTarget(CStateManager& mgr, const CTriggerData& data) const {
  return HasTarget(mgr, data) && mPrevTargetId != mTargetId;
}

// Ing body fn_29_B01C
// @ fn_30_C4E0
bool CIngBoostBallGuardian::HasLineOfSight(CStateManager&, const CTriggerData&) const {
  return mLineOfSight.HasLineOfSight() && mLineOfSight.GetClearTime() > 0.5f;
}

// fn_30_C4C4 (this module).
// Ing body fn_29_B000
// @ fn_30_C4C4
bool CIngBoostBallGuardian::UnderFire(CStateManager&, const CTriggerData&) const {
  return mUnderFireTime < 0.75f;
}

// Ing body fn_29_AFE4
// @ fn_30_C4A8
bool CIngBoostBallGuardian::HeardShot(CStateManager&, const CTriggerData&) const {
  return mHeardShotTime < 1.5f;
}

// fn_29_AE68 (not yet exercised in a run)
// @ fn_30_C32C
bool CIngBoostBallGuardian::AreaClear(CStateManager& mgr, const CTriggerData&) const {
  const CVector3f half = mModelScale * 10.f;
  const CVector3f pos = GetTranslation();
  rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
  mgr.BuildNearList(nearList, CAABox(pos - half, pos + half),
                    CMaterialFilter::MakeInclude(CMaterialList(kMT_Player, kMT_Character)), this);
  return nearList.empty();
}

// Ing body fn_29_ADE8
// @ fn_30_C2AC
bool CIngBoostBallGuardian::CoverLeash(CStateManager& mgr, const CTriggerData&) const {
  const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId);
  return hint == nullptr || (hint->GetTranslation() - GetTranslation()).MagSquared() >
                                mData.mUnknown5D0D * mData.mUnknown5D0D;
}

// Ing body fn_29_ADBC
// @ fn_30_C280
bool CIngBoostBallGuardian::HasCoverPoint(CStateManager& mgr, const CTriggerData&) const {
  return GetCoverHint(mgr, mCoverHintId) != nullptr;
}

// Ing body fn_29_AD7C
// @ fn_30_C240
bool CIngBoostBallGuardian::HasWallCoverPoint(CStateManager& mgr, const CTriggerData&) const {
  const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId);
  return hint != nullptr && hint->GetHintType() == static_cast< CScriptAIHint::EHintType >(4);
}

// fn_30_C22C (this module).
// fn_29_AD68
// @ fn_30_C22C
bool CIngBoostBallGuardian::HasMiniPortals(CStateManager&, const CTriggerData&) const {
  return mMiniPortalCount != 0;
}

// Ing body fn_29_AD54
// @ fn_30_C218
bool CIngBoostBallGuardian::IsCorporeal(CStateManager&, const CTriggerData&) const {
  return mForm == kForm_Corporeal;
}

// Ing body fn_29_AD40
// @ fn_30_C204
bool CIngBoostBallGuardian::IsIngSpot(CStateManager&, const CTriggerData&) const {
  return mForm == kForm_IngSpot;
}

// Ing body fn_29_AD2C
// @ fn_30_C1F0
bool CIngBoostBallGuardian::IsBodyProjectile(CStateManager&, const CTriggerData&) const {
  return mForm == kForm_BodyProjectile;
}

// fn_29_AD14 (not yet exercised in a run)
// @ fn_30_C1D8
bool CIngBoostBallGuardian::IsFrustrated(CStateManager&, const CTriggerData&) const {
  return mData.mFrustrationTime < mFrustrationTimer;
}

// @ fn_30_C1B0 (the Ing's fn_29_ACEC)
bool CIngBoostBallGuardian::IsAggressive(CStateManager&, const CTriggerData&) const {
  // 0x11EC 0x20, or the XENZ/XEXZ count at 0x115C.
  return mAggressive || mSafeZoneCount > 0;
}

// Ing body fn_29_ACE0
// @ fn_30_C1A4
bool CIngBoostBallGuardian::UseProjectileFSMEntry(CStateManager&, const CTriggerData&) const {
  return mProjectileFSMEntry;
}

// fn_30_C14C (this module).
// fn_29_AC20
// @ fn_30_C14C
bool CIngBoostBallGuardian::TargetInSafeZone(CStateManager& mgr, const CTriggerData&) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  return target != nullptr && mgr.GetSafeZoneManager()->IsObjectInSafeZone(*target, mgr);
}

// Ing body fn_29_ABC8
// @ fn_30_C0F4
bool CIngBoostBallGuardian::TargetInLightSafeZone(CStateManager& mgr, const CTriggerData&) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  return target != nullptr && mgr.GetSafeZoneManager()->IsObjectInHurtfulSafeZone(*target, mgr);
}

// Ing body fn_29_AAF8
// @ fn_30_C070
bool CIngBoostBallGuardian::CanChangeForm(CStateManager&, const CTriggerData&) const {
  // 0x0000C0A8 (corporeal) / 0x0000C0D0 (ing spot): extrwi 1,28 = data 0x08.
  const bool locked = (mData.mFlags210 & kFlag_NoFormChange) != 0;
  switch (mForm) {
  case kForm_IngSpot:
    return !locked;
  case kForm_Corporeal:
    return !locked && mData.mUnknownC620 < mFormTime;
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
    return false;
  default:
    return true;
  }
}

// @ fn_30_C05C (the Ing's fn_29_AA94)
bool CIngBoostBallGuardian::ShouldFleeSafeZone(CStateManager&, const CTriggerData&) const {
  return mSafeZoneCount > 0; // fn_30_C05C: 0x115C
}

// fn_30_C014 (the Ing's fn_29_AA4C): the desired form (0xAE8) is 2 (0x0000C02C) and the form is
// not corporeal yet (fn_30_C218, 0x0000C034).
// @ fn_30_C014
bool CIngBoostBallGuardian::ShouldBecomeCorporeal(CStateManager& mgr, const CTriggerData& data) const {
  return mDesiredForm == kForm_Corporeal && !IsCorporeal(mgr, data);
}

// fn_30_BFCC (the Ing's fn_29_AA04): the desired form (0xAE8) is 3 (0x0000BFE4) and the form is
// not the ing spot yet (fn_30_C204, 0x0000BFEC).
// @ fn_30_BFCC
bool CIngBoostBallGuardian::ShouldBecomeIngSpot(CStateManager& mgr, const CTriggerData& data) const {
  return mDesiredForm == kForm_IngSpot && !IsIngSpot(mgr, data);
}

// Ing body fn_29_A8CC. Area 28 has no team (0x1086 stays invalid), so CanStartAttack only runs
// when the Ing is in one; mMinAttackRange is 0 there, so the near bound never refuses.
// @ fn_30_BE94
bool CIngBoostBallGuardian::ShouldArmSwipe(CStateManager& mgr, const CTriggerData&) const {
  // 0x0000BEB4 form == 2, 0x0000BEC0 data 0x40.
  if (mForm != kForm_Corporeal || (mData.mFlags210 & kFlag_NoArmSwipe) != 0 ||
      !(mArmSwipeTime > 1.f)) {
    return false;
  }
  // The team gate only applies inside a team (retail skips it when mTeamId is kInvalid, as
  // fn_29_A8CC's branch at 0x0000A924 does).
  if (mTeamId != kInvalidUniqueId &&
      !CScriptTeamAiMgr::CanStartAttack(CScriptTeamAiMgr::kAT_Melee, mgr, mTeamId,
                                        GetUniqueId())) {
    return false;
  }
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  if (target == nullptr) {
    return false;
  }
  const CVector3f delta = target->GetTranslation() - GetTranslation();
  const float distSq = delta.MagSquared();
  return mMinAttackRange * mMinAttackRange <= distSq &&
         distSq <= mMaxAttackRange * mMaxAttackRange && fabsf(delta.GetZ()) < 3.f;
}

// Ing body fn_29_A8C0
// @ fn_30_BE88
bool CIngBoostBallGuardian::ShouldTaunt(CStateManager&, const CTriggerData&) const {
  return mTaunt;
}

// Ing body fn_29_A780; area 28 has no team (the CanStartAttack call runs only inside one).
// @ fn_30_BD48
bool CIngBoostBallGuardian::ShouldBodyProjectile(CStateManager& mgr, const CTriggerData&) const {
  // The team gate only applies inside a team: retail compares mTeamId with kInvalidUniqueId
  // and skips CanStartAttack when they are equal (asm 0x0000A8A4, the branch that lands at
  // .L_0000A7FC). An unconditional gate keeps the state off for every Ing placed without a team.
  // 0x0000BD68 (data 0x20), 0x0000BD70 (data 0x08), 0x0000BD7C (0x11EC 0x40).
  if ((mData.mFlags210 & (kFlag_NoBodyProjectile | kFlag_NoFormChange)) != 0 ||
      !mBodyProjectileAllowed ||
      (mTeamId != kInvalidUniqueId &&
       !CScriptTeamAiMgr::CanStartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamId,
                                         GetUniqueId()))) {
    return false;
  }
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  const CVector3f delta = target != nullptr ? target->GetTranslation() - GetTranslation()
                                            : CVector3f::Zero();
  const float distSq = delta.MagSquared();
  const float minDist = mData.mBodyProjectile.mMinAttackDist; // 0x0000BDE8: 0x87c
  const float maxDist = mData.mBodyProjectile.mMaxAttackDist; // 0x0000BDFC: 0x880
  // 0x0000BE44 rodata_110 (-15) <= dz, 0x0000BE58 dz <= rodata_A4 (3).
  const bool inRange = target != nullptr && minDist * minDist <= distSq &&
                       distSq <= maxDist * maxDist && -15.f <= delta.GetZ() &&
                       delta.GetZ() <= 3.f;
  static int sTick = 0;
  if (BgLogTick(sTick)) {
    fprintf(stderr,
            "[ibbg] %04x body proj trigger: target %04x dist %.1f [%.1f..%.1f] dz %.1f -> %d\n",
            GetUniqueId().Value(), mTargetId.Value(), CMath::SqrtF(distSq), minDist, maxDist,
            delta.GetZ(), inRange ? 1 : 0);
  }
  return inRange;
}

// Ing body fn_29_A664; area 28 has no team (the CanStartAttack call runs only inside one).
// @ fn_30_BC2C
bool CIngBoostBallGuardian::ShouldOpenMiniPortal(CStateManager& mgr, const CTriggerData&) const {
  // Same team gate as fn_29_A780/A8CC: retail asks CanStartAttack only when mTeamId is set
  // (asm 0x0000A788, the compare that skips to .L_0000A6E0).
  // 0x0000BC4C: data 0x10.
  if ((mData.mFlags210 & kFlag_NoMiniPortal) != 0 || mFormTime < mData.mUnknownC620 ||
      (mTeamId != kInvalidUniqueId &&
       !CScriptTeamAiMgr::CanStartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamId,
                                         GetUniqueId()))) {
    return false;
  }
  const float distSq = DistSqToTarget(mgr);
  const float minDist = mData.mMiniPortal.mMinAttackDist;
  const float maxDist = mData.mMiniPortal.mMaxAttackDist;
  return minDist * minDist <= distSq && distSq <= maxDist * maxDist;
}

// @ fn_30_BC20 (the Ing's fn_29_A658): 0x11ED 0x01, raised by CollidedWith
bool CIngBoostBallGuardian::ProjectileSplat(CStateManager&, const CTriggerData&) const {
  return mSplat;
}

// fn_29_A4EC. The box is the model scale * 2.5 (rodata_E0, 0x0000BB50) around the Ing.
// @ fn_30_BAB4
bool CIngBoostBallGuardian::ProjectileHitTarget(CStateManager& mgr, const CTriggerData&) const {
  const CEntity* entity = mgr.GetObjectById(mTargetId);
  const CPhysicsActor* target = TCastToConstPtr< CPhysicsActor >(entity);
  if (target == nullptr) {
    return false;
  }
  if (const CPlayer* player = TCastToConstPtr< CPlayer >(entity)) {
    const CPlayer::EPlayerMorphBallState state = player->GetMorphballTransitionState();
    if (state != CPlayer::kMS_Unmorphed && state != CPlayer::kMS_Unmorphing) {
      return false;
    }
  }
  const CVector3f half = mModelScale * 2.5f;
  const CVector3f pos = GetTranslation();
  return CAABox(pos - half, pos + half).DoBoundsOverlap(target->GetBoundingBox());
}

// fn_30_BAA8 (the Ing's fn_29_A4E0): 0x11EC 0x10. Area 28's FSM2 asset never evaluates this
// trigger; both arms were forced from a hook.
// @ fn_30_BAA8
bool CIngBoostBallGuardian::FoundMovementPos(CStateManager&, const CTriggerData&) const {
  return mFoundMovementPos;
}

// Ing body fn_29_A498
// @ fn_30_BA60
bool CIngBoostBallGuardian::PathShagged(CStateManager& mgr, const CTriggerData& data) const {
  return mRecalcPath || CPatterned::PathShagged(mgr, data);
}

// Ing body fn_29_A450
// @ fn_30_BA18
bool CIngBoostBallGuardian::PathOver(CStateManager& mgr, const CTriggerData& data) const {
  return !mFoundMovementPos || CPatterned::PathOver(mgr, data);
}

// fn_29_A428 (not yet exercised in a run)
// @ fn_30_B9F0
bool CIngBoostBallGuardian::PointPathOver(CStateManager&, const CTriggerData&) const {
  return mIngSpotPathFindNavigation.IsPathOver(*this);
}

// fn_29_A400 (not yet exercised in a run)
// @ fn_30_B9C8
bool CIngBoostBallGuardian::FoundPointPath(CStateManager&, const CTriggerData&) const {
  return mIngSpotPathFindNavigation.HasPath(*this);
}

// Ing body fn_29_A3C8
// @ fn_30_B990
bool CIngBoostBallGuardian::IsOffPath(CStateManager&, const CTriggerData&) const {
  return mPathFindSearch.OnPath(GetTranslation()) != CPathFindSearch::kR_Success;
}

// Ing body fn_29_A370
// @ fn_30_B938
bool CIngBoostBallGuardian::HasAttackPattern(CStateManager& mgr, const CTriggerData&) const {
  return const_cast< CIngBoostBallGuardian* >(this)->GetConnectedObject(mgr, kSS_Attack,
                                                                        kSM_Follow) !=
         kInvalidUniqueId;
}

// fn_30_B908 (this module): over once the attack path is being followed (0x0000B910: 0x11ED
// 0x10) and the waypoint navigation has no destination left (0x0000B924: this+0x6c0 against
// kInvalidUniqueId). Unlike the Ing half's copy it stores nothing.
// @ fn_30_B908
bool CIngBoostBallGuardian::AttackPatternOver(CStateManager&, const CTriggerData&) const {
  return mFollowingAttackPath && mWaypointNavigation.GetDestination() == kInvalidUniqueId;
}

// fn_30_B8A0 (this module). Area 28's FSM2 asset never evaluates this trigger; its arms were
// forced from a hook.
// Ing body fn_29_A2D8: the target is a ball that was not spawned as one - asm 0x0000A2E0 tests
// 0x390 (spawned state) first and only reads 0x38c (transition state) when it is 0.
// @ fn_30_B8A0
bool CIngBoostBallGuardian::TargetIsBall(CStateManager& mgr, const CTriggerData&) const {
  const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mTargetId));
  const bool isBall = player != nullptr &&
                      player->GetSpawnedMorphballState() == CPlayer::kMS_Unmorphed &&
                      player->GetMorphballTransitionState() == CPlayer::kMS_Morphed;
  static int sTick = 0;
  if (BgLogTick(sTick)) {
    fprintf(stderr, "[ibbg] %04x target is ball: target %04x player %d morph %d -> %d\n",
            GetUniqueId().Value(), mTargetId.Value(), player != nullptr ? 1 : 0,
            player != nullptr ? int(player->GetMorphballTransitionState()) : -1,
            isBall ? 1 : 0);
  }
  return isBall;
}

// fn_30_B83C (this module).
// fn_29_A1C0
// @ fn_30_B83C
bool CIngBoostBallGuardian::HasPathToTarget(CStateManager& mgr, const CTriggerData&) const {
  const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId));
  return target != nullptr &&
         const_cast< CPathFindSearch& >(mPathFindSearch)
                 .PathExists(GetTranslation(), target->GetTranslation()) ==
             CPathFindSearch::kR_Success;
}

// fn_30_B7E0: a target player with nothing attached to it (0x0000B818 compares the player's
// 0x2e4 with kInvalidUniqueId). Unlike the Ing's fn_29_A098 it reads no data flag.
// @ fn_30_B7E0
bool CIngBoostBallGuardian::CanGrappleTarget(CStateManager& mgr, const CTriggerData&) const {
  const CPlayer* player = TCastToConstPtr< CPlayer >(mgr.GetObjectById(mTargetId));
  return player != nullptr && player->GetAttachedActorId() == kInvalidUniqueId;
}

// ---- code functions (the FSM2 runs them: CGenericFSM2State::SetCodeFunctions) -------------------

// Ing body fn_29_5B60: destination = the Ing's own position (0x000072EC..0x00007304), 0x11EC bit 0x10
// (mFoundMovementPos) cleared (0x00007310), then the target's translation and the bit set when the
// target exists (0x00007324..0x0000734C). SetDestination, the face target (0x718) at kInvalidUniqueId
// and fn_30_51B4 (JoinTeam) run either way (0x00007358..0x00007374). mTargetPos (0x10F8) is not written.
// @ fn_30_72C8
void CIngBoostBallGuardian::SetTargetDest(CStateManager& mgr, float) {
  LogState("SetTargetDest");
  CVector3f dest = GetTranslation();
  mFoundMovementPos = false;
  if (const CActor* target = TCastToConstPtr< CActor >(mgr.GetObjectById(mTargetId))) {
    dest = target->GetTranslation();
    mFoundMovementPos = true;
  }
  mPathFindNavigation.SetDestination(dest);
  mPathFindNavigation.SetFaceTarget(kInvalidUniqueId);
  JoinTeam(mgr);
}

// Ing body fn_29_5AC4: destination = the cover hint's translation when there is one, else the Ing's own
// position; mFoundMovementPos only in the first case; then SetDestination and the face target at
// kInvalidUniqueId (0x718), asm 0x00005B00-0x00005B48.
// @ fn_30_722C
void CIngBoostBallGuardian::SetCoverDest(CStateManager& mgr, float) {
  LogState("SetCoverDest");
  CVector3f dest = GetTranslation();
  mFoundMovementPos = false;
  if (const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId)) {
    dest = hint->GetTranslation();
    mFoundMovementPos = true;
  }
  mPathFindNavigation.SetDestination(dest);
  mPathFindNavigation.SetFaceTarget(kInvalidUniqueId);
}

// Ing body fn_29_59D0: two closest-point queries from the Ing's own position - filter (20, 0, -1) then
// (20, 1, from), asm 0x000059E0 and 0x00005A44 - and a point-graph search between the two results
// (0x00005A94). The second filter's connected point is the *first* query's index (the asm keeps it
// in r5 from 0x00005A2C), so the second point is one the graph connects to the first - without
// that the pair is unrelated and the search below fails. It sets no destination:
// mFoundMovementPos is what IngSpotPathFind reads.
// @ fn_30_7138
void CIngBoostBallGuardian::SetRecoverDest(CStateManager&, float) {
  LogState("SetRecoverDest");
  mFoundMovementPos = false;
  mPointPathFindSearch.ClearWaypoints(); // 0xacc = 0, asm 0x00005A08
  int from = -1;
  if (mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), from,
                                                    CPathFindPointSearchFilter(20.f, 0, -1)) !=
          CPathFindPointSearch::kCPR_Success ||
      from == -1) {
    return;
  }
  int to = -1;
  if (mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), to,
                                                    CPathFindPointSearchFilter(20.f, 1, from)) !=
          CPathFindPointSearch::kCPR_Success ||
      to == -1) {
    return;
  }
  if (mPathArea != nullptr && BgPointSearch(mPointPathFindSearch, *mPathArea, from, to)) {
    mFoundMovementPos = true;
  }
}

// fn_30_6F88 (Ing body fn_29_5820): the point-graph route to the cover hint. Filter (20, 0, -1)
// from the Ing's own position (0x00006FFC), then the nearest point with flag bit 0 (0x00007048)
// that PointConnectionsTest links to it (0x00007058), under 400 (rodata_F8, 0x00007094) from the
// hint and with a mesh path to it (0x000070A8), then the point search between the two (0x000070F8).
// Area 28's path area has 4 points, all with bit 0 set, all found from anywhere and searchable:
// the flag-0 skip, no-closest-point, no-mesh-path and failed-search exits never run there.
// @ fn_30_6F88
void CIngBoostBallGuardian::SetPointCoverDest(CStateManager& mgr, float) {
  LogState("SetPointCoverDest");
  mFoundMovementPos = false;
  mPointPathFindSearch.ClearWaypoints();
  const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId);
  if (hint == nullptr || mPathArea == nullptr) {
    return;
  }
  int from = -1;
  mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), from,
                                                CPathFindPointSearchFilter(20.f, 0, -1));
  if (from == -1) {
    return;
  }
  const CVector3f cover = hint->GetTranslation();
  float bestDistSq = 400.f; // rodata_F0
  int best = -1;
  for (int i = 0; i < mPathArea->GetNumPoints(); ++i) {
    const CPFPoint& point = mPathArea->GetPoint(i);
    // asm 0x000058E0: only points whose flags word has bit 0 set are cover points.
    if ((point.GetFlags() & 1) == 0 || !mPathArea->PointPathExists(from, i)) {
      continue;
    }
    const float distSq = (cover - point.GetPosition()).MagSquared();
    if (distSq < bestDistSq &&
        mPathFindSearch.PathExists(point.GetPosition(), cover) == CPathFindSearch::kR_Success) {
      best = i;
      bestDistSq = distSq;
    }
  }
  if (best == -1) {
    return;
  }
  if (BgPointSearch(mPointPathFindSearch, *mPathArea, from, best)) {
    mFoundMovementPos = true;
  }
}

// fn_29_5714. In area 28 (4 path points) both closest-point queries always find a point and the
// search always succeeds, so only the no-hint and found exits run there.
// @ fn_30_6E7C
void CIngBoostBallGuardian::SetWallPointCoverDest(CStateManager& mgr, float) {
  LogState("SetWallPointCoverDest");
  mFoundMovementPos = false;
  mPointPathFindSearch.ClearWaypoints(); // 0xacc = 0, asm 0x0000573C
  const CScriptAIHint* hint = GetCoverHint(mgr, mCoverHintId); // fn_29_45BC, id at 0xd72
  // Retail tests the hint only (fn_29_45BC returns null when the id is not a CScriptAIHint); the
  // host can run without a path area, so the point queries are skipped when mPathArea is null.
  if (hint == nullptr || mPathArea == nullptr) {
    return;
  }
  int from = -1;
  mPointPathFindSearch.FindClosestPhysicalPoint(GetTranslation(), from,
                                                CPathFindPointSearchFilter(20.f, 0, -1));
  if (from == -1) {
    return;
  }
  int to = -1;
  // asm 0x00005778: the second filter's connected point is the index the first query found, so only
  // points the graph connects to the Ing's own point are candidates.
  mPointPathFindSearch.FindClosestPhysicalPoint(hint->GetTranslation(), to,
                                                CPathFindPointSearchFilter(20.f, 0, from));
  if (to != -1 && BgPointSearch(mPointPathFindSearch, *mPathArea, from, to)) {
    mFoundMovementPos = true;
  }
}

// fn_30_6C40 (this module).
// Ing body fn_29_54E8. Where the exit host lands: the AI hint that best matches
// the way the player is facing. Retail starts from the player's own position (uid +8, transform
// +0x24) and takes mgr+0x14fc as the player with no null test, which the port keeps as a guard.
// The score is (GetAngleDiff(forward, hint - Ing) + 1) * |hint - Ing|^2, and the comparison pair
// keeps a 400.0f (rodata_F0, i.e. 20 units squared) petal around zero: under it the *smaller*
// non-negative score wins, over it the larger one does. Retail writes mTargetPos (0xdc8) and
// nothing else - in particular it does not touch mFoundMovementPos, so neither does this.
// @ fn_30_6C40
void CIngBoostBallGuardian::SetExitHostDest(CStateManager& mgr, float) {
  LogState("SetExitHostDest");
  const CPlayer* player = mgr.GetPlayer(0); // mgr+0x14fc
  if (player == nullptr) {
    return;
  }
  const CVector3f forward = player->GetTransform().GetForward();
  float best = 0.f; // rodata_6C
  CVector3f bestPos = player->GetTranslation();
  CObjectList& list = mgr.ObjectListById(kOL_AiWaypoint);
  for (int i = list.GetFirstObjectIndex(); i != -1; i = list.GetNextObjectIndex(i)) {
    CScriptAIHint* hint = TCastToPtr< CScriptAIHint >(list[i]);
    // Retail compares the object's own area word against the Ing's (there is no active/in-use test)
    // and skips hint types 4, 14 and 15 (asm 0x00006D20..0x00006D30), types with no name in
    // CScriptAIHint::EHintType.
    if (hint == nullptr || hint->GetCurrentAreaId() != GetCurrentAreaId()) {
      continue;
    }
    const int hintType = static_cast< int >(hint->GetHintType());
    if (hintType == 4 || hintType == 14 || hintType == 15) {
      continue;
    }
    const CVector3f delta = hint->GetTranslation() - GetTranslation();
    const float score = (CVector3f::GetAngleDiff(forward, delta) + 1.f) * delta.MagSquared();
    if ((score < best && score >= 400.f) || (score > best && best <= 400.f)) {
      best = score;
      bestPos = hint->GetTranslation();
    }
  }
  mTargetPos = bestPos; // 0xdc8
}

// fn_30_6C00 (this module).
// fn_29_5190
// @ fn_30_6C00
void CIngBoostBallGuardian::StartRangedAttack(CStateManager& mgr, float) {
  CScriptTeamAiMgr::StartAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamId, GetUniqueId());
}

// fn_30_6BBC (this module).
// fn_29_514C
// @ fn_30_6BBC
void CIngBoostBallGuardian::EndRangedAttack(CStateManager& mgr, float) {
  CScriptTeamAiMgr::EndAttack(CScriptTeamAiMgr::kAT_Projectile, mgr, mTeamId, GetUniqueId(),
                              false);
}

// @ fn_30_6980 (the Ing's fn_29_4F14): the body projectile hit the mesh, so the Ing splats back
// into its ing-spot form: form 3, the scripted body state aborted, the splat-wall sound once at the
// Ing's own position (its handle is discarded), then - when the splat token at 0xd48 is engaged - an
// explosion on the Ing's transform at the model's scale ("Ing Splat Wall Fx").
void CIngBoostBallGuardian::SplatOntoMesh(CStateManager& mgr, float) {
  LogState("SplatOntoMesh");
  mForm = kForm_IngSpot; // 0x9d4 = 3
  BodyController()->CommandMgr().DeliverCmd(CBodyStateCmd(kBSC_AbortScripted)); // id word 13
  // 0x000069D8: the contact normal CollidedWith kept (0x1104); rodata_64 is 1.0f.
  mSurfaceAlignment.OrientToSurfaceNormal(*this, mSplatNormal, 1.f);
  // 0x852: the body projectile's splat-wall sound. Retail stores no handle, so neither do we.
  CSfxManager::AddEmitter(mData.mBodyProjectile.mSoundSplatWall, GetTranslation(), 0x7f,
                          GetCurrentAreaId().Value(), true, false, CSfxManager::kMedPriority);
  const bool splatFx = static_cast< bool >(mSplatEffect); // 0xd54, the token's engaged byte
  if (DebugBg()) {
    fprintf(stderr, "[ibbg] %04x splat: normal (%.2f, %.2f, %.2f), splat fx %s\n",
            GetUniqueId().Value(), mSplatNormal.GetX(), mSplatNormal.GetY(), mSplatNormal.GetZ(),
            splatFx ? "explosion" : "token not loaded");
  }
  if (splatFx) {
    mgr.AddObject(rs_new CExplosion(*mSplatEffect, mgr.AllocateUniqueId(),
                                    CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList,
                                                true, kInvalidEditorId),
                                    rstl::string_l("Ing Splat Wall Fx"), GetTransform(), 0,
                                    GetModelData()->GetScale(), CColor::White(), -1));
  }
}
