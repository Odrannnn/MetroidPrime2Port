// Port of the Dark Samus REL's (module 10) CScattershotProjectile: the code at 0x0..0x2E44 with the
// vtable lbl_10_data_40. CDarkSamus::FireScatterShotProjectile (fn_10_12F60) spawns one and adds a
// shot to it per scatter shot while it lives. Layout and the shot record are in
// include/MetroidPrime/Enemies/CScattershotProjectile.hpp; rodata: lbl_10_rodata_0 = 1.0f,
// _4 = 2.0f, _8 = 0.0f, _C = 0.2f, _10 = 100.0f, _18 = 1e-5f, _1C = 0.017453292f, _20 = 0.5f,
// _24 = FLT_EPSILON, _28 = 1.75f, _2C = 0.67f, _30 = -1.0f, _34 = 5.0f, _38 = 10.0f.
// The inlined helpers (record copies, vector pushes, ray result copies: fn_10_2C8, 2040, 2098,
// 20EC, A08, A60, ABC, B04, B28, DC8, E10, 2E44) have no body of their own here.
#include "MetroidPrime/Enemies/CScattershotProjectile.hpp"

#include "Collision/CCollidableAABox.hpp"
#include "Collision/CCollisionPrimitive.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CRandom16.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Graphics/CTevCombiners.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "Kyoto/Math/CPlane.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CUnitVector3f.hpp"
#include "Kyoto/Particles/CParticleGlobals.hpp"
#include "Kyoto/Particles/IElement.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CEntityInfo.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "Weapons/CCollisionResponseData.hpp"
#include "Weapons/CWeaponDescription.hpp"

#include <math.h>

namespace {
const int kScattershotTypeId = 21; // TypesMatch.cpp's "21: class not declared yet (DarkSamus REL)"
const int kMaxShots = 300;         // `cmpwi r9, 0x12c` at 0x00000CA8

// The filter the shot sweeps and the player cast use (lbl_10_data_20/_24 and _38/_3C): solid in,
// projectile passthrough out (type 3 = kFT_IncludeExclude).
CMaterialFilter SolidFilter() {
  return CMaterialFilter(CMaterialList(kMT_Solid), CMaterialList(kMT_ProjectilePassthrough),
                         CMaterialFilter::kFT_IncludeExclude);
}
} // namespace

// TypesMatch for id 21 with CWeapon as the parent (TYPES_MATCH_IMPL's logic; the class is not in
// TypesMatch.cpp because the DarkSamus REL owns it).
CEntity* CScattershotProjectile::TypesMatch(int typeId) const {
  if (typeId == kScattershotTypeId) {
    return const_cast< CScattershotProjectile* >(this);
  }
  if (typeId > kScattershotTypeId) {
    return nullptr;
  }
  return CWeapon::TypesMatch(typeId);
}

template <>
CScattershotProjectile* TCastToPtr< CScattershotProjectile >(CEntity* entity) {
  return static_cast< CScattershotProjectile* >(TryCast(entity, kScattershotTypeId));
}

template <>
CScattershotProjectile* TCastToPtr< CScattershotProjectile >(CEntity& entity) {
  return static_cast< CScattershotProjectile* >(entity.TypesMatch(kScattershotTypeId));
}

// fn_10_2AD0 (0x00002AD0, 0x374 bytes): the CWeapon base is named "Scattershot Projectile"
// (lbl_10_rodata_3C + 0x25), active, without attributes or a model; its filter takes solid, player
// and non-solid damageable (lbl_10_data_0/_4/_8) and leaves out projectile passthrough, collision
// actors, characters and projectiles (_C/_10/_14/_18), type 3; its material is projectile
// passthrough (_1C). The box 0x1D8 starts inverted, the lifetime 0x1F0 at 0.0f, the alive byte 0x1F4
// set, the render offsets 0x1F8..0x218 zero, the sizes 0x21C/0x220/0x224 at 0.5f (lbl_10_rodata_20)
// and the trail length 0x228 at 2.0f (lbl_10_rodata_4), no shots (0x00002DCC). The first shot
// leaves from the transform's translation along its second column (fn_10_C98 at 0x00002DE8), the
// actor goes there (0x00002E0C), the box takes the position (0x00002E18) and the render parameters
// are read (fn_10_2798 at 0x00002E20).
// @ fn_10_2AD0
CScattershotProjectile::CScattershotProjectile(const TToken< CWeaponDescription >& desc,
                                               EWeaponType type, const CTransform4f& xf,
                                               const CDamageInfo& damage, TUniqueId uid,
                                               TAreaId area, TUniqueId owner, float speed)
: CWeapon(uid, area, true, owner, type, rstl::string_l("Scattershot Projectile"), xf,
          CMaterialFilter(CMaterialList(kMT_Solid, kMT_Player, kMT_NonSolidDamageable),
                          CMaterialList(kMT_ProjectilePassthrough, kMT_CollisionActor,
                                        kMT_Character, kMT_Projectile),
                          CMaterialFilter::kFT_IncludeExclude),
          CMaterialList(kMT_ProjectilePassthrough), damage, kPA_None, CModelData())
, mDescToken(desc)
, mDesc(mDescToken.GetT())
, mSpeed(speed)
, mBounds(CAABox::MakeMaxInvertedBox())
, mLifetime(0.f)
, mAlive(true)
, mB1Offset(0.f, 0.f, 0.f)
, mB2Offset(0.f, 0.f, 0.f)
, mTrailOffset(0.f, 0.f, 0.f)
, mB1Size(0.5f)
, mB2Size(0.5f)
, mTrailSize(0.5f)
, mTrailLength(2.f) {
  const CVector3f pos = xf.GetTranslation();
  AddShot(pos, xf.GetColumn(kDY));
  SetTranslation(pos);
  mBounds.AccumulateBounds(GetTranslation());
  InitRenderParams();
}

// fn_10_29FC (0x000029FC, 0xD4 bytes): the member teardown (the shot vector and the token).
CScattershotProjectile::~CScattershotProjectile() {}

// fn_10_2918 (0x00002918, 0xC4 bytes): the base's Think, then nothing for a dt at or below 0.0f
// (`cror eq, lt, eq` at 0x00002950). The lifetime 0x1F0 counts dt; the shots move (fn_10_2254), new
// ones are swept (fn_10_1C08) and checked against the player (fn_10_2C); past 10.0f
// (lbl_10_rodata_38, `ble` at 0x000029A0) the actor deletes itself.
// @ fn_10_2918
void CScattershotProjectile::Think(float dt, CStateManager& mgr) {
  CWeapon::Think(dt, mgr);
  if (dt <= 0.f) { // 0x00002950
    return;
  }
  mLifetime += dt;
  UpdateShots(mgr, dt);
  CheckNewShots(mgr);
  CheckPlayerHit(mgr);
  if (mLifetime > 10.f) { // `ble` at 0x000029A0 (taken on a NaN: a NaN lifetime is kept)
    mgr.DeleteObjectRequest(GetUniqueId());
  }
}

// fn_10_29DC (0x000029DC, 0x20 bytes): CActor's.
// @ fn_10_29DC
void CScattershotProjectile::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  CActor::AcceptScriptMsg(mgr, msg);
}

// fn_10_E5C (0x00000E5C, 0x20 bytes): EnsureRendered only.
// @ fn_10_E5C
void CScattershotProjectile::AddToRenderer(const CStateManager& mgr) const {
  EnsureRendered(mgr);
}

// fn_10_C5C (0x00000C5C, 0x3C bytes): the box 0x1D8, always.
// @ fn_10_C5C
rstl::optional_object< CAABox > CScattershotProjectile::GetTouchBounds() const {
  return mBounds;
}

// fn_10_C98 (0x00000C98, 0x130 bytes): with fewer than 300 shots (`cmpwi r9, 0x12c` at 0x00000CA8)
// one more starts at pos, aimed 100.0f (lbl_10_rodata_10) along dir, not travelled, needing its
// sweep (flags 4). Every 17th (count % 17 == 0 at 0x00000D38) plays the hit sound, every 13th
// (0x00000D68) spawns the hit explosion and every 5th (0x00000D9C) flies slow; the record goes in
// through fn_10_DC8/fn_10_E10.
// @ fn_10_C98
void CScattershotProjectile::AddShot(const CVector3f& pos, const CVector3f& dir) {
  const int count = mShots.size();
  if (count >= kMaxShots) { // 0x00000CA8
    return;
  }
  SShot shot;
  shot.mPos = pos;
  shot.mTarget = pos + 100.f * dir;
  shot.mDistance = 0.f;
  shot.mFlags = SShot::kF_NeedsSweep;
  if (count % 17 == 0) { // 0x00000D38
    shot.mFlags |= SShot::kF_HitSfx;
  }
  if (count % 13 == 0) { // 0x00000D68
    shot.mFlags |= SShot::kF_HitEffect;
  }
  if (count % 5 == 0) { // 0x00000D9C
    shot.mFlags |= SShot::kF_Slow;
  }
  mShots.push_back(shot);
}

// fn_10_2C (0x0000002C, 0x2F8 bytes): every live shot (flag 1 clear, 0x0000005C) with a direction
// to its target (0x000000A0) casts a 2.0f (lbl_10_rodata_4) ray from 1.0f behind it against the
// player's collision primitive (solid in, projectile passthrough out). On a hit (0x000001E4) the
// shot is hit and dead, its sound (flag 8, 0x00000204) and explosion (flag 0x10, 0x0000021C) play,
// and the player takes the current damage 0x19C from the weapon (owner 0x15C, filter 0x168) along
// the shot's direction (0x0000029C).
// @ fn_10_2C
void CScattershotProjectile::CheckPlayerHit(CStateManager& mgr) {
  CPlayer* player = mgr.GetPlayer(0);
  for (int i = 0; i < mShots.size(); ++i) {
    SShot& shot = mShots[i];
    if ((shot.mFlags & SShot::kF_Dead) != 0) { // 0x00000060
      continue;
    }
    CVector3f dir = shot.mTarget - shot.mPos;
    if (!dir.CanBeNormalized()) { // 0x000000A0
      continue;
    }
    dir.Normalize();
    const CVector3f start = shot.mPos - 1.f * dir;
    const CRayCastResult result = player->GetCollisionPrimitive()->CastRay(
        start, dir, 2.f, SolidFilter(), player->GetPrimitiveTransform());
    if (!result.IsValid()) { // 0x000001E4
      continue;
    }
    shot.mFlags |= SShot::kF_Hit;
    shot.mFlags |= SShot::kF_Dead;
    if ((shot.mFlags & SShot::kF_HitSfx) != 0) { // 0x00000208
      PlayHitSfx(shot.mPos);
    }
    if ((shot.mFlags & SShot::kF_HitEffect) != 0) { // 0x00000220
      SpawnHitExplosion(mgr, shot);
    }
    mgr.ApplyDamage(GetUniqueId(), player->GetUniqueId(), mOwnerId, mCurDamageInfo, mFilter, dir);
  }
}

// fn_10_324 (0x00000324, 0x6A4 bytes): the first hit from `from` to `to`. No direction
// (0x000003EC): no hit. The world goes first in five 0.2f (lbl_10_rodata_C) steps of the length
// (t from 0.0f while t <= 1.0f, `cror eq, lt, eq` at 0x0000054C) against the weapon's filter 0x168;
// the first step with a hit keeps it and its distance along the whole sweep (0x000004FC). Then
// every actor of the near list the shot can hit (fn_10_B78): a physics actor's primitive (solid in,
// projectile passthrough out) and otherwise its touch box (none: skipped, 0x0000074C; pass
// everything) replace the hit when nearer (`bge` at 0x00000698 / 0x00000824). A box that does not
// stop the ray but holds `from` (0x00000888) ends the search with a hit there, facing back along
// the ray (0x0000096C).
// @ fn_10_324
CRayCastResult CScattershotProjectile::Sweep(
    const CVector3f& from, const CVector3f& to,
    const rstl::reserved_vector< TUniqueId, kMaxObjects >& nearList, CStateManager& mgr) const {
  CRayCastResult best;
  const CVector3f delta = to - from;
  if (!delta.CanBeNormalized()) { // 0x000003EC
    return best;
  }
  const float length = delta.Magnitude();
  const CVector3f dir = delta.AsNormalized();
  const float step = 0.2f * length;
  float bestDist = length;
  for (float t = 0.f; t <= 1.f; t += 0.2f) { // 0x0000054C
    const float along = length * t;
    const CRayCastResult result =
        CGameCollision::RayStaticIntersection(mgr, from + along * dir, dir, step, mFilter);
    if (result.IsValid()) { // 0x000004EC
      bestDist = along + result.GetTime();
      best = result;
      break;
    }
  }

  for (int i = 0; i < nearList.size(); ++i) {
    CEntity* ent = mgr.ObjectById(nearList[i]);
    if (ent == nullptr) { // 0x0000057C
      continue;
    }
    const CActor& actor = static_cast< const CActor& >(*ent);
    if (!CanHit(actor)) { // 0x00000594
      continue;
    }
    if (const CPhysicsActor* physics = TCastToPtr< CPhysicsActor >(ent)) { // 0x000005A8
      const CRayCastResult result = physics->GetCollisionPrimitive()->CastRay(
          from, dir, length, SolidFilter(), physics->GetPrimitiveTransform());
      if (result.IsValid() && result.GetTime() < bestDist) { // 0x0000068C, 0x00000698
        bestDist = result.GetTime();
        best = result;
      }
      continue;
    }
    const rstl::optional_object< CAABox > bounds = actor.GetTouchBounds();
    if (!bounds.valid()) { // 0x0000074C
      continue;
    }
    const CCollidableAABox box(*bounds, actor.GetMaterialList());
    const CRayCastResult result = box.CastRay(from, dir, length, CMaterialFilter::GetPassEverything(),
                                              CTransform4f::Identity());
    if (result.IsValid()) { // 0x00000818
      if (result.GetTime() < bestDist) { // 0x00000824
        bestDist = result.GetTime();
        best = result;
      }
      continue;
    }
    if (bounds->PointInside(from)) { // 0x00000888
      const CUnitVector3f normal(-dir);
      best = CRayCastResult(0.f, from, CPlane(CVector3f::Dot(from, normal), normal),
                            actor.GetMaterialList());
      break; // 0x0000096C
    }
  }
  return best;
}

// fn_10_B78 (0x00000B78, 0xE4 bytes): an actor the current weapon mode passes through
// (vulnerability effect 2 = kE_PassThrough, `cmpwi r0, 0x2` at 0x00000BC4) is never hit; otherwise
// only players and platforms (lbl_10_data_28/_2C) are (0x00000C2C).
// @ fn_10_B78
bool CScattershotProjectile::CanHit(const CActor& actor) const {
  if (actor.GetDamageVulnerability()->GetVulnerability(mCurDamageInfo.GetWeaponMode()).mEffect ==
      CWeaponTypeVulnerability::kE_PassThrough) { // 0x00000BC4
    return false;
  }
  return (actor.GetMaterialList().GetValue() &
          CMaterialList(kMT_Player, kMT_Platform).GetValue()) != 0; // 0x00000C2C
}

// fn_10_1C08 (0x00001C08, 0x184 bytes): with the alive byte 0x1F4 set (0x00001C2C), every shot
// that still needs its sweep (flag 4) gathers what is near its path (solid in, projectile
// passthrough out, not itself), stops at the first hit (fn_10_324) and is swept (flag 4 off).
// @ fn_10_1C08
void CScattershotProjectile::CheckNewShots(CStateManager& mgr) {
  if (!mAlive) { // 0x00001C2C
    return;
  }
  for (int i = 0; i < mShots.size(); ++i) {
    SShot& shot = mShots[i];
    if ((shot.mFlags & SShot::kF_NeedsSweep) == 0) {
      continue;
    }
    CAABox box = CAABox::MakeMaxInvertedBox();
    box.AccumulateBounds(shot.mPos);
    box.AccumulateBounds(shot.mTarget);
    rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
    mgr.BuildNearList(nearList, box, SolidFilter(), this);
    const CRayCastResult result = Sweep(shot.mPos, shot.mTarget, nearList, mgr);
    if (result.IsValid()) {
      shot.mTarget = result.GetPoint();
    }
    shot.mFlags -= SShot::kF_NeedsSweep;
  }
}

// fn_10_1D8C (0x00001D8C, 0x2A0 bytes): only for a shot that flew 2.0f (lbl_10_rodata_4, `blt` at
// 0x00001DB8) and with a collision response (description +0xD8). The response's generator for type 1
// faces the shot's reversed direction (LookAt from zero at +0xC, up) at its position; the explosion
// "Projectile collision response" (lbl_10_rodata_3C + 7) is 1.75f (lbl_10_rodata_28) big, white,
// flags 2, in this actor's area.
// @ fn_10_1D8C
void CScattershotProjectile::SpawnHitExplosion(CStateManager& mgr, const SShot& shot) const {
  if (shot.mDistance < 2.f) { // 0x00001DB8
    return;
  }
  if (!mDesc->mCOLR.valid()) { // 0x00001DC4
    return;
  }
  const rstl::optional_object< TLockedToken< CGenDescription > > gen =
      (*mDesc->mCOLR)->GetParticleDescription(EWeaponCollisionResponseTypes(1));
  if (!gen.valid()) { // 0x00001E58
    return;
  }
  CTransform4f xf = CTransform4f::LookAt(CVector3f::Zero(), shot.mTarget, CVector3f::Up());
  xf.SetTranslation(shot.mPos);
  CExplosion* explosion = rs_new CExplosion(
      *gen, mgr.AllocateUniqueId(),
      CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId),
      rstl::string_l("Projectile collision response"), xf, 2, 1.75f * CVector3f::One(),
      CColor::White(), -1);
  if (explosion != nullptr) {
    mgr.AddObject(explosion);
  }
}

// fn_10_213C (0x0000213C, 0x118 bytes): with a collision response (description +0xD8) its type 1
// sound (when not negative, 0x00002198) plays once at pos, with the response's range and falloff.
// @ fn_10_213C
void CScattershotProjectile::PlayHitSfx(const CVector3f& pos) const {
  if (!mDesc->mCOLR.valid()) { // 0x00002170
    return;
  }
  const CCollisionResponseData* response = **mDesc->mCOLR;
  const int sfx = response->GetSoundEffectId(EWeaponCollisionResponseTypes(1));
  if (sfx < 0) { // 0x00002198
    return;
  }
  CAudioSys::C3DEmitterParmData parms(response->GetAudibleRange(), response->GetAudibleFallOff(),
                                      1, CAudioSys::kMaxVolume, 0x14);
  parms.mPos = pos;
  parms.mSfxId = sfx;
  CSfxManager::AddEmitter(parms, GetCurrentAreaId().Value(), true, false,
                          CSfxManager::kMedPriority);
}

// fn_10_2254 (0x00002254, 0x544 bytes): every live shot (0x000022E8) moves dt * speed 0x1D4 (0.67f
// of it when slow, lbl_10_rodata_2C at 0x00002384) toward its target; a swept one (flag 4 clear,
// 0x00002404) that would pass it (0x000023FC) stops there, hit and dead, its target becoming the
// reversed direction (lbl_10_rodata_30 = -1.0f), and plays its sound and explosion. Every live shot
// widens the box and dies past 100.0f travelled (0x000024B8). After 1.0f (lbl_10_rodata_0) without
// live shots the actor deletes itself and drops the alive byte (0x00002518). Otherwise it sits at
// the old box's centre, 5.0f (lbl_10_rodata_34) along the shots' mean direction, or toward the
// player's eye by the new box's half size; the box is the new one grown by 5.0f.
// @ fn_10_2254
void CScattershotProjectile::UpdateShots(CStateManager& mgr, float dt) {
  CAABox box = CAABox::MakeNullBox();
  CVector3f meanDir = CVector3f::Zero();
  bool anyLive = false;
  for (int i = 0; i < mShots.size(); ++i) {
    SShot& shot = mShots[i];
    if ((shot.mFlags & SShot::kF_Dead) != 0) { // 0x000022E8
      continue;
    }
    anyLive = true;
    CVector3f dir = shot.mTarget - shot.mPos;
    if (dir.CanBeNormalized()) {
      const float left = dir.Magnitude();
      dir.Normalize();
      meanDir += dir;
      float step = dt * mSpeed;
      if ((shot.mFlags & SShot::kF_Slow) != 0) { // 0x00002384
        step *= 0.67f;
      }
      const CVector3f move = step * dir;
      shot.mPos += move;
      // The pass test compares the move's own magnitude (Magnitude at 0x000023E8, r3 = the move at
      // sp+0xAC; `fcmpo cr0, f1, f31` at 0x000023F0 with f31 = the distance left), not the step.
      const float moved = move.Magnitude();
      shot.mDistance += moved;                                          // 0x000023F8
      if (moved > left && (shot.mFlags & SShot::kF_NeedsSweep) == 0) { // 0x000023FC, 0x00002404
        shot.mPos = shot.mTarget;
        shot.mTarget = -1.f * dir;
        shot.mFlags |= SShot::kF_Hit;
        shot.mFlags |= SShot::kF_Dead;
        if ((shot.mFlags & SShot::kF_HitSfx) != 0) {
          PlayHitSfx(shot.mPos);
        }
        if ((shot.mFlags & SShot::kF_HitEffect) != 0) {
          SpawnHitExplosion(mgr, shot);
        }
      }
    }
    box.AccumulateBounds(shot.mPos);
    if (shot.mDistance > 100.f) { // 0x000024B8
      shot.mFlags |= SShot::kF_Dead;
    }
  }

  if (mLifetime > 1.f && !anyLive) { // 0x00002518
    mgr.DeleteObjectRequest(GetUniqueId());
    mAlive = false;
    return;
  }
  if (meanDir.CanBeNormalized()) {
    meanDir.Normalize();
  }
  const CVector3f oldCenter = mBounds.GetCenterPoint();
  SetTranslation(oldCenter + 5.f * meanDir);
  const CVector3f center = box.GetCenterPoint();
  CVector3f toEye = mgr.GetPlayer(0)->GetEyePosition() - center;
  if (toEye.CanBeNormalized()) {
    toEye.Normalize();
    const float halfSize = (box.GetMaxPoint() - center).Magnitude();
    SetTranslation(oldCenter + halfSize * toEye);
  }
  mBounds = CAABox(box.GetMinPoint() - 5.f * CVector3f::One(),
                   box.GetMaxPoint() + 5.f * CVector3f::One());
}

// fn_10_2798 (0x00002798, 0x180 bytes): the description's sizes, offsets and trail length at frame
// 0, under the global random CRandom16(0x63): B1SE/B1PO with B1TX, B2SE/B2PO with B2TX,
// TSZE/TLEN/TLPO with TTEX; each element is read only when present.
// @ fn_10_2798
void CScattershotProjectile::InitRenderParams() {
  CRandom16 random(0x63);
  CGlobalRandom globalRandom(random);
  CParticleGlobals::SetEmitterTime(0);
  if (mDesc->mB1TX != nullptr) {
    if (mDesc->mB1SE != nullptr) {
      mDesc->mB1SE->GetValue(0, mB1Size);
    }
    if (mDesc->mB1PO != nullptr) {
      mDesc->mB1PO->GetValue(0, mB1Offset);
    }
  }
  if (mDesc->mB2TX != nullptr) {
    if (mDesc->mB2SE != nullptr) {
      mDesc->mB2SE->GetValue(0, mB2Size);
    }
    if (mDesc->mB2PO != nullptr) {
      mDesc->mB2PO->GetValue(0, mB2Offset);
    }
  }
  if (mDesc->mTTEX != nullptr) {
    if (mDesc->mTSZE != nullptr) {
      mDesc->mTSZE->GetValue(0, mTrailSize);
    }
    if (mDesc->mTLEN != nullptr) {
      mDesc->mTLEN->GetValue(0, mTrailLength);
    }
    if (mDesc->mTLPO != nullptr) {
      mDesc->mTLPO->GetValue(0, mTrailOffset);
    }
  }
}

namespace {
// One camera-facing quad (Render's billboard): `right`/`up` are the view's first and third
// columns times the size; the corners go (+r +u), (-r +u), (-r -u), (+r -u) with the UVs' max/min.
void StreamBillboard(const CColor& color, const SUVElementSet& uv, const CVector3f& right,
                     const CVector3f& up) {
  CGraphics::StreamBegin(kP_Quads); // `li r3, 0x80`
  CGraphics::StreamColor(color);
  CVector3f v = 0.5f * up;
  CGraphics::StreamTexcoord(uv.xMax, uv.yMax);
  v += 0.5f * right;
  CGraphics::StreamVertex(v);
  CGraphics::StreamTexcoord(uv.xMin, uv.yMax);
  v -= right;
  CGraphics::StreamVertex(v);
  CGraphics::StreamTexcoord(uv.xMin, uv.yMin);
  v -= up;
  CGraphics::StreamVertex(v);
  CGraphics::StreamTexcoord(uv.xMax, uv.yMin);
  v += right;
  CGraphics::StreamVertex(v);
  CGraphics::StreamEnd();
}

// The billboard axes: the view's columns, turned about its Y by `degrees` when that is not
// 0.0f (fabs < 1e-5f, lbl_10_rodata_18; lbl_10_rodata_1C = pi / 180).
void BillboardAxes(float size, float degrees, CVector3f& right, CVector3f& up) {
  CTransform4f view = CGraphics::GetViewMatrix();
  if (!(fabsf(degrees - 0.f) < 1e-5f)) {
    view.RotateLocalY(CRelAngle::FromRadians(0.017453292f * degrees));
  }
  right = size * view.GetColumn(kDX);
  up = size * view.GetColumn(kDZ);
}
} // namespace

// fn_10_E7C (0x00000E7C, 0xCA8 bytes): the colours B1CL/B2CL/TSCL/TECL (grey by default) and the
// rotations B1RT/B2RT at frame 0, UVs (0, 0, 1, 1) by default, read only with their texture. The
// TEV pass (texture times raster colour and alpha, colour scaled by 2: 0x30 = 1 at 0x000011F8) is
// built once (lbl_10_bss_0/_4, fn_10_1B64); stage 1 passes through, depth test LEqual without write,
// no culling, alpha > 0. Every live shot draws, at its position plus the offset turned by the
// actor's rotation with the shot's direction as its second column: the B1 and B2 billboards
// (blend source alpha to one with the description's RB1A / RB2A bit of +0x66, else inverse source
// alpha) and the trail (RTLA): a quad
// TSZE * 0.5f wide across the view (the cross of the view ray and the direction, fn_10_1B24) and
// TLEN * TSZE long behind the shot, TSCL at the head and TECL at the tail. Culling back to front
// and the normal matrix on at the end (0x00001AD0).
// asmcheck: x24c_01 is the shot's kF_Dead flag; fn_10_1B64 (the TEV pass's ctor) is sPass below.
// @ fn_10_E7C
void CScattershotProjectile::Render(const CStateManager& mgr) const {
  CParticleGlobals::SetEmitterTime(0);
  CParticleGlobals::SetParticleLifetime(0);
  CParticleGlobals::UpdateParticleLifetimeTweenValues(0);
  CColor b1Color = CColor::Grey();
  CColor b2Color = CColor::Grey();
  CColor trailStartColor = CColor::Grey();
  CColor trailEndColor = CColor::Grey();
  float b1Rot = 0.f;
  float b2Rot = 0.f;
  SUVElementSet b1Uv = {0.f, 0.f, 1.f, 1.f};
  SUVElementSet b2Uv = {0.f, 0.f, 1.f, 1.f};
  SUVElementSet trailUv = {0.f, 0.f, 1.f, 1.f};
  const CWeaponDescription* desc = mDesc;
  if (desc->mB1TX != nullptr) {
    desc->mB1TX->GetValueUV(0, b1Uv);
    if (desc->mB1CL != nullptr) {
      desc->mB1CL->GetValue(0, b1Color);
    }
    if (desc->mB1RT != nullptr) {
      desc->mB1RT->GetValue(0, b1Rot);
    }
  }
  if (desc->mB2TX != nullptr) {
    desc->mB2TX->GetValueUV(0, b2Uv);
    if (desc->mB2CL != nullptr) {
      desc->mB2CL->GetValue(0, b2Color);
    }
    if (desc->mB2RT != nullptr) {
      desc->mB2RT->GetValue(0, b2Rot);
    }
  }
  if (desc->mTTEX != nullptr) {
    desc->mTTEX->GetValueUV(0, trailUv);
    if (desc->mTSCL != nullptr) {
      desc->mTSCL->GetValue(0, trailStartColor);
    }
    if (desc->mTECL != nullptr) {
      desc->mTECL->GetValue(0, trailEndColor);
    }
  }
  // 0x00001098..0x000010D0: every colour's alpha byte becomes 127.5f (lbl_10_rodata_14) through
  // qr2, whatever the elements gave, on every path.
  b1Color.SetAlpha(0.5f);
  b2Color.SetAlpha(0.5f);
  trailStartColor.SetAlpha(0.5f);
  trailEndColor.SetAlpha(0.5f);
  CGraphics::SetUseNormalMatrix(false);

  // lbl_10_bss_4: colour (zero, raster, texture, zero), alpha (zero, raster, texture, zero).
  static CTevCombiners::CTevPass sPass(
      CTevCombiners::ColorPass(CTevCombiners::kCS_Zero, CTevCombiners::kCS_RasterColor,
                               CTevCombiners::kCS_TextureColor, CTevCombiners::kCS_Zero),
      CTevCombiners::AlphaPass(CTevCombiners::kAS_Zero, CTevCombiners::kAS_RasterAlpha,
                               CTevCombiners::kAS_TextureAlpha, CTevCombiners::kAS_Zero));
  sPass.SetColorScale(CTevCombiners::kTS_Scale2); // 0x000011F8
  CGraphics::SetTevOp(kTS_Stage0, sPass);
  CGraphics::SetTevOp(kTS_Stage1, CGraphics::kEnvPassthru);
  CGraphics::SetDepthWriteMode(true, kE_LEqual, false);
  CGraphics::SetCullMode(kCM_None);
  CGraphics::SetAlphaCompare(kAF_Greater, 0, kAO_And, kAF_Always, 0);

  const CTransform4f& viewMtx = CGraphics::GetViewMatrix();
  for (int i = 0; i < mShots.size(); ++i) {
    const SShot& shot = mShots[i];
    if ((shot.mFlags & SShot::kF_Dead) != 0) { // 0x00001258
      continue;
    }
    CVector3f dir = shot.mTarget - shot.mPos;
    if (dir.CanBeNormalized()) { // 0x00001298
      dir.Normalize();
    }
    // The actor's rotation with the shot's direction as its second column, at the shot.
    const CVector3f colX = GetTransform().GetColumn(kDX);
    const CVector3f colZ = GetTransform().GetColumn(kDZ);
    const CTransform4f xf(colX.GetX(), dir.GetX(), colZ.GetX(), shot.mPos.GetX(), colX.GetY(),
                          dir.GetY(), colZ.GetY(), shot.mPos.GetY(), colX.GetZ(), dir.GetZ(),
                          colZ.GetZ(), shot.mPos.GetZ());

    if (desc->mB1TX != nullptr) {
      CGraphics::SetModelMatrix(CTransform4f::Translate(shot.mPos + xf.Rotate(mB1Offset)));
      CGraphics::SetBlendMode(kBM_Blend, kBF_SrcAlpha,
                              (desc->mRB1A ? kBF_One : kBF_InvSrcAlpha), kLO_Clear); // +0x66 0x4: 0x00001370
      CVector3f right, up;
      BillboardAxes(mB1Size, b1Rot, right, up);
      TLockedToken< CTexture > tex = desc->mB1TX->GetValueTexture(0);
      (*tex)->Load(GX_TEXMAP0, CTexture::kCM_Clamp);
      StreamBillboard(b1Color, b1Uv, right, up);
    }
    if (desc->mB2TX != nullptr) {
      CGraphics::SetModelMatrix(CTransform4f::Translate(shot.mPos + xf.Rotate(mB2Offset)));
      CGraphics::SetBlendMode(kBM_Blend, kBF_SrcAlpha,
                              (desc->mRB2A ? kBF_One : kBF_InvSrcAlpha), kLO_Clear); // +0x66 0x2: 0x00001614
      CVector3f right, up;
      BillboardAxes(mB2Size, b2Rot, right, up);
      TLockedToken< CTexture > tex = desc->mB2TX->GetValueTexture(0);
      (*tex)->Load(GX_TEXMAP0, CTexture::kCM_Clamp);
      StreamBillboard(b2Color, b2Uv, right, up);
    }
    if (desc->mTTEX != nullptr) {
      CGraphics::SetBlendMode(kBM_Blend, kBF_SrcAlpha,
                              (desc->mRTLA ? kBF_One : kBF_InvSrcAlpha), kLO_Clear); // +0x66 0x1: 0x00001858
      CGraphics::SetModelMatrix(CTransform4f::Translate(shot.mPos + xf.Rotate(mTrailOffset)));
      {
        TLockedToken< CTexture > tex = desc->mTTEX->GetValueTexture(0);
        (*tex)->Load(GX_TEXMAP0, CTexture::kCM_Clamp);
      }
      CVector3f side = CVector3f::Cross(shot.mPos - viewMtx.GetTranslation(), dir); // fn_10_1B24
      const float sideSq = CVector3f::Dot(side, side);
      if (sideSq > 1.1920929e-7f) { // lbl_10_rodata_24, `ble` at 0x0000195C
        side *= 0.5f * mTrailSize / sqrtf(sideSq);
      }
      const CVector3f length = mTrailLength * (mTrailSize * dir);
      CGraphics::StreamBegin(kP_Quads);
      CGraphics::StreamColor(trailStartColor);
      CGraphics::StreamTexcoord(trailUv.xMax, trailUv.yMax);
      CVector3f v = 0.5f * side;
      CGraphics::StreamVertex(v);
      CGraphics::StreamTexcoord(trailUv.xMin, trailUv.yMax);
      CGraphics::StreamColor(trailEndColor);
      v -= length;
      CGraphics::StreamVertex(v);
      CGraphics::StreamTexcoord(trailUv.xMin, trailUv.yMin);
      v -= side;
      CGraphics::StreamVertex(v);
      CGraphics::StreamTexcoord(trailUv.xMax, trailUv.yMin);
      CGraphics::StreamColor(trailStartColor);
      v += length;
      CGraphics::StreamVertex(v);
      CGraphics::StreamEnd();
    }
  }
  CGraphics::SetCullMode(kCM_Front);
  CGraphics::SetUseNormalMatrix(true);
}
