#include "MetroidPrime/ScriptObjects/CScriptPickup.hpp"

#include "Kyoto/Graphics/CColor.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CAnimData.hpp"
#include "MetroidPrime/CAnimPlaybackParms.hpp"
#include "MetroidPrime/CEchoEmitter.hpp"
#include "MetroidPrime/CEntityInfo.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/SEchoParameters.hpp"

#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/Cameras/CFirstPersonCamera.hpp"
#include "MetroidPrime/HUD/CSamusHud.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Tweaks/CTweakGame.hpp"

#include "MetroidPrime/HUD/CHUDMemoParms.hpp"

#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrPickup.hpp"

#include "Kyoto/CResFactory.hpp"
#include "Kyoto/Math/CAbsAngle.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/Player/CEnvironmentVariable.hpp"

#include "rstl/math.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/ScriptObjects/CScriptHUDMemo.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCamera.hpp"
#include "MetroidPrime/ScriptObjects/CScriptControlHint.hpp"
#include "MetroidPrime/ScriptObjects/CScriptMemoryRelay.hpp"
#include "MetroidPrime/ScriptObjects/CScriptRelay.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSequenceTimer.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTimer.hpp"
#include "port_overlay.h"
#include "port_pickup_presentation.h"
#include "port_randomizer.h"
#include "port_rando_models.h"
#include <set>
#include <string>
#include <vector>
#endif
#include <float.h>

static TUniqueId skHomingPickupId = kInvalidUniqueId;
static float skDrawInDistance = 30.f;
static float skMultiplayerDrawInDistance = 12.f;

CScriptPickup::CScriptPickup(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                             const CTransform4f& xf, const CModelData& modelData,
                             const CActorParameters& aParams, const SEchoParameters& echo,
                             const CAABox& aabb, CPlayerState::EItemType itemType, int amount,
                             int capacityIncrease, int itemPercentageIncrease,
                             CAssetId pickupEffect, bool absoluteValue, bool canHomeByDefault,
                             bool autoSpin, bool blinkOut, float lifeTime, float respawnTime,
                             float fadeTime, float activateDelay, float pickupEffectLifetime,
                             float autoHomeRange, float delayUntilHome, float homingSpeed,
                             const CVector3f& orbitOffset)
: CActor(uid, name, info, 0, xf, modelData, CMaterialList(), aParams, kInvalidUniqueId)
, mItemType(itemType)
, mAmount(amount)
, mCapacity(capacityIncrease)
, mPercentageIncrease(itemPercentageIncrease)
, mLifeTime(lifeTime)
, mRespawnTime(respawnTime)
, mRespawnTimer(0.f)
, mFadeTime(fadeTime)
, mCurTime(0.0f)
, mPickupEffectLifetime(pickupEffectLifetime)
, mActivateDelay(activateDelay)
, mAutoHomeRange(autoHomeRange)
, mDelayUntilHome(delayUntilHome)
, mHomingSpeed(homingSpeed)
, mTransformZ(xf.GetTranslation().GetZ())
, mPickupParticleDesc()
, mTouchBounds(aabb)
, mFramesSinceLastSeen(0)
, mHomingPlayerIndex(0)
, mOrbitOffset(orbitOffset)
, mCanHomeByDefault(canHomeByDefault)
, mInTractor(false)
, mEnableTractorTest(false)
, mAbsoluteValue(absoluteValue)
, mSuppressDeathScriptMsgs(false)
, mAutoSpin(autoSpin)
, mRenderedThisFrame(true)
, mSuppressBobbing(false)
, mBlinkOut(blinkOut)
#ifdef TARGET_PC
, mPortTicked(false)
#endif
{
  static bool homingInitialized = false;
  if (!homingInitialized) {
    homingInitialized = true;
    skHomingPickupId = kInvalidUniqueId;
  }

  if (pickupEffect != kInvalidAssetId) {
    mPickupParticleDesc = gpSimplePool->GetObj(SObjectTag('PART', pickupEffect));
    mPickupParticleDesc->Lock();
  }

  if (HasAnimation()) {
    AnimationData()->SetAnimation(CAnimPlaybackParms(0, -1, 1.f, true), false);
  }

  if (mFadeTime) {
    SetModelFlags(CModelFlags::AlphaBlended(0.f).DepthCompareUpdate(true, false));
  }

  AllocateEchoEmitter(true, CAABox(GetTranslation(), GetTranslation()), echo);
}

CScriptPickup::~CScriptPickup() {}

void CScriptPickup::PreRenderAllViewports(CStateManager& mgr) {
  CActor::PreRenderAllViewports(mgr);
  if (mRenderedThisFrame) {
    mRenderedThisFrame = false;
    mFramesSinceLastSeen = 0;
  } else {
#ifdef TARGET_PC
    // Count unseen frames per tick, not per drawn frame: uncapped rendering would
    // otherwise stop the off-screen animation after one tick instead of ~5.
    if (mPortTicked) {
      mFramesSinceLastSeen += 1;
    }
#else
    mFramesSinceLastSeen += 1;
#endif
  }
#ifdef TARGET_PC
  mPortTicked = false;
#endif
}

void CScriptPickup::PreRender(CStateManager& mgr) {
  CActor::PreRender(mgr);
  if (!GetPreRenderClipped()) {
    mRenderedThisFrame = true;
  }
}

bool CScriptPickup::IsVisible() const {
  if (mActivateDelay >= 0.0f) {
    return false;
  }
  return !(mRespawnTimer > 0.0f);
}

void CScriptPickup::Think(float dt, CStateManager& mgr) {
  CActor::Think(dt, mgr);
#ifdef TARGET_PC
  mPortTicked = true;
#endif
  if (!GetActive()) {
    return;
  }

  if (EchoEmitter() && HasModelData()) {
    EchoEmitter()->SetBounds(GetModelData()->GetBounds(GetTransform()));
  }
  if (mActivateDelay >= 0.f) {
    mActivateDelay -= dt;
    return;
  }
  if (mRespawnTimer >= 0.f) {
    mRespawnTimer -= dt;
    return;
  }
  if (mDelayUntilHome >= 0.f) {
    mDelayUntilHome -= dt;
  }

  bool cinematic = false;
  if (!mgr.IsMultiplayer() && mgr.CameraManager(0)->IsInCinematicCamera()) {
    cinematic = true;
  }
  if (!cinematic || mCurTime < mFadeTime) {
    mCurTime += dt;
  }
  if (mInTractor && mEnableTractorTest && mLifeTime - mCurTime < 2.f) {
    mCurTime = rstl::max_val(mLifeTime - 2.f - FLT_EPSILON, mCurTime - 2.f * dt);
  }

  CModelFlags drawFlags = CModelFlags::Normal();
  if (mFadeTime) {
    if (mCurTime < mFadeTime) {
      drawFlags = CModelFlags::AlphaBlended(mCurTime / mFadeTime).DepthCompareUpdate(true, false);
    } else {
      mFadeTime = 0.f;
    }
  } else if (mLifeTime) {
    const float elapsed = rstl::min_val(mLifeTime, mCurTime);
    float alpha = 1.f;
    if (mLifeTime < 2.f) {
      alpha = 1.f - mLifeTime / elapsed;
    } else if (mLifeTime - elapsed < 2.f) {
      alpha = (mLifeTime - elapsed) / 2.f;
    }
    if (mBlinkOut && uint(mgr.GetUpdateFrameIdx()) % 6 > 2) {
      alpha = 1.f;
    }
    drawFlags = CModelFlags::AlphaBlended(alpha).DepthCompareUpdate(true, false);
  }
  SetModelFlags(drawFlags);

  if (mFramesSinceLastSeen < 5) {
    if (HasAnimation()) {
      CAdvancementDeltas deltas = UpdateAnimation(dt, mgr, true);
      CVector3f position =
          GetTransform().GetTranslation() + GetTransform().Rotate(deltas.GetOffsetDelta());
      CTransform4f xf =
          GetTransform().MultiplyIgnoreTranslation(deltas.GetOrientationDelta().BuildTransform4f());
      xf.SetTranslation(position);
      SetTransform(xf);
    }
    if (mAutoSpin && !mInTractor) {
      const bool renderBoundsDirty = GetRenderBoundsDirty();
      CTransform4f xf = CTransform4f::RotateZ(
          CRelAngle::FromDegrees(CMath::FastFmod(mCurTime, 2.f) / 2.f * 360.f));
      xf.SetTranslation(GetTranslation());
      if (!mSuppressBobbing) {
        const float z =
            mTransformZ +
            (0.25f * CMath::FastSinR(M_PIF * (CMath::FastFmod(mCurTime, 4.f) / 4.f)) - 0.125f);
        xf.SetTranslation(CVector3f(GetTranslation().GetX(), GetTranslation().GetY(), z));
      }
      SetTransform(xf);
      if (!renderBoundsDirty) {
        SetRenderBoundsDirty(false);
      }
    }
  } else if (HasAnimation()) {
    UpdateSfxEmitters(mgr);
  }

  if (mInTractor && !cinematic) {
    CPlayer* player = mgr.GetPlayer(mHomingPlayerIndex);
    CPlayerState* playerState = player->GetPlayerState();
    const CVector3f delta = GetTranslation() - player->GetTranslation();
    if (GetCurrentAreaId() != mgr.GetNextAreaId() || !playerState->IsPlayerAlive() ||
        (!mEnableTractorTest && CVector3f::Dot(delta, delta) > mAutoHomeRange * mAutoHomeRange)) {
      mInTractor = false;
    } else {
      CVector3f offset = CVector3f::Up() * 2.f;
      if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed) {
        offset = CVector3f::Up() * 1.f;
      }
      CVector3f velocity = player->GetTranslation() + offset - GetTranslation();
      mTractorTime += dt;
      const float halfTractorTime = rstl::min_val(mTractorTime, 2.f) * 0.5f;
      velocity = velocity.AsNormalized() * (mHomingSpeed * halfTractorTime);
      if (!mEnableTractorTest && !close_enough(velocity.GetZ(), 0.f)) {
        velocity += CVector3f(0.f, 0.f, velocity.GetZ() * 0.5f);
      }
      if (mEnableTractorTest &&
          playerState->GetChargeBeamFactor() < playerState->GetChargeAnimStart()) {
        mEnableTractorTest = false;
        mInTractor = false;
        velocity = CVector3f::Zero();
      }
      SetTranslation(GetTranslation() + dt * velocity);
    }
  }
  if (mLifeTime && mCurTime > mLifeTime) {
    mgr.DeleteObjectRequest(GetUniqueId());
    return;
  }

  if (skHomingPickupId == GetUniqueId()) {
    skHomingPickupId = kInvalidUniqueId;
    return;
  }
  if (skHomingPickupId != kInvalidUniqueId) {
    return;
  }
  skHomingPickupId = GetUniqueId();

  if (mgr.IsMultiplayer() && mCanHomeByDefault && mAutoHomeRange > 0.f && mDelayUntilHome <= 0.f) {
    float closestDistance = FLT_MAX;
    int closestPlayer = 0;
    for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {
      CPlayer* player = mgr.GetPlayer(i);
      const CVector3f delta = GetTranslation() - player->GetTranslation();
      const float distance = CVector3f::Dot(delta, delta);
      if (distance < closestDistance && player->GetPlayerState()->IsPlayerAlive()) {
        closestDistance = distance;
        closestPlayer = i;
      }
    }
    if (closestDistance < mAutoHomeRange * mAutoHomeRange) {
      mHomingPlayerIndex = closestPlayer;
      if (!mInTractor) {
        mInTractor = true;
        SendScriptMsgs(kSS_MaxReached, mgr, GetUniqueId(), kSM_None);
        mTractorTime = 0.f;
      }
    } else {
      mInTractor = false;
    }
  }

  if (mInTractor || !mCanHomeByDefault || !(mDelayUntilHome <= 0.f) ||
      GetCurrentAreaId() != mgr.GetNextAreaId()) {
    return;
  }
  for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {
    const CVector3f delta = GetTranslation() - mgr.GetPlayer(i)->GetTranslation();
    if (CVector3f::Dot(delta, delta) < mAutoHomeRange * mAutoHomeRange) {
      mInTractor = true;
      mHomingPlayerIndex = i;
    }
  }

  if (!mInTractor) {
    float closestDistance = FLT_MAX;
    for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {
      const float drawInDistance =
          mgr.IsMultiplayer() ? skMultiplayerDrawInDistance : skDrawInDistance;
      const float drawInDistanceSquared = drawInDistance * drawInDistance;
      CPlayer* player = mgr.GetPlayer(i);
      const CPlayerState* playerState = player->GetPlayerState();
      if (playerState->GetChargeBeamFactor() > playerState->GetChargeAnimStart()) {
        const CFirstPersonCamera* camera = player->GetCameraManager()->GetFirstPersonCamera();
        const CVector3f posDelta = GetTranslation() - camera->GetTransform().GetTranslation();
        const CVector3f cameraFront = camera->GetTransform().GetForward();
        const float dot = CVector3f::Dot(cameraFront, posDelta.AsNormalized());
        const float fovCos = cosine(CAbsAngle::FromDegrees(camera->GetFov()));
        if (dot > fovCos && posDelta.MagSquared() < drawInDistanceSquared &&
            posDelta.MagSquared() < closestDistance) {
          mEnableTractorTest = true;
          mInTractor = true;
          mHomingPlayerIndex = i;
          closestDistance = posDelta.MagSquared();
        }
      }
    }
  }
  if (mInTractor) {
    SendScriptMsgs(kSS_MaxReached, mgr, GetUniqueId(), kSM_None);
    mTractorTime = 0.f;
  }
}

void CScriptPickup::Touch(CActor& act, CStateManager& mgr) {
  if (!GetActive() || !IsVisible()) {
    return;
  }
  if (CPlayer* player = TCastToPtr< CPlayer >(act)) {
    int playerIndex = mgr.MaskUIdNumPlayers(player->GetUniqueId());
    CPlayerState* playerState = mgr.PlayerState(playerIndex);
    if (!playerState->IsPlayerAlive())
      return;

    CPlayerState::EItemType itemType = mItemType;

    if (mPickupParticleDesc) {
      mgr.AddObject(rs_new CExplosion(
          TLockedToken< CGenDescription >(*mPickupParticleDesc), mgr.AllocateUniqueId(),
          CEntityInfo(GetCurrentAreaId(), CEntity::NullConnectionList, true, kInvalidEditorId),
          rstl::string_l("Explosion - Pickup Effect"), GetTransform(), 0, CVector3f(1.f, 1.f, 1.f),
          CColor::White(), -1));
    }

    int previousAmount = playerState->GetItemAmount(itemType);
#ifdef TARGET_PC
    // Seamless pickups' Repeats mode: the player already had this item (a later expansion or tank).
    const bool repeatPickup = playerState->HasPowerUp(itemType);
#endif
    playerState->AddPowerUp(CPlayerState::kIT_ItemPercentage, mPercentageIncrease);
    playerState->IncrPickUp(CPlayerState::kIT_ItemPercentage, mPercentageIncrease);
    if (!mAbsoluteValue) {
      int grantCapacity = mCapacity;
      int grantAmount = mAmount;
#ifdef TARGET_PC
      // Port: Beam ammo per expansion scales what a Dark/Light capacity pickup grants (N / 50).
      // Ammo drops carry no capacity and stay as they are.
      if ((itemType == CPlayerState::kIT_DarkAmmo || itemType == CPlayerState::kIT_LightAmmo) &&
          mCapacity > 0) {
        const int per = PortOverlay::BeamAmmoPerExpansion();
        grantCapacity = mCapacity * per / PortOverlay::kBeamAmmoPerExpansionDefault;
        grantAmount = mAmount * per / PortOverlay::kBeamAmmoPerExpansionDefault;
      }
#endif
      playerState->AddPowerUp(itemType, grantCapacity);
      playerState->IncrPickUp(itemType, grantAmount);
    } else {
      playerState->ReInitializePowerUp(itemType, mCapacity);
      playerState->SetItemAmount(itemType, mAmount);
    }
    playerState->SetTimeLeft(itemType, mPickupEffectLifetime);
#ifdef TARGET_PC
    // Randomized location: its script chain ends in a MEMO with the vanilla item's text. Mark
    // the MEMOs the Arrived connections reach, directly or through relays and timers (which
    // deliver later), to drop their next display, and show ours.
    std::string randoText;
    bool randoMemo = false;
    if (mgr.GetWorld() != nullptr) {
      randoMemo = PortRandomizer::AcquisitionText(
          static_cast< uint32_t >(mgr.GetWorld()->IGetWorldAssetId()),
          static_cast< uint32_t >(
              mgr.GetWorld()->IGetAreaAlways(GetCurrentAreaId())->IGetAreaAssetId()),
          static_cast< uint32_t >(GetEditorId().value), static_cast< int >(itemType), randoText);
    }
    // Seamless pickups mark the chain's presentation the same way: the control hint it increments,
    // the message box it zeroes and the cinematic cameras it activates, which it reaches through
    // memory relays and sequence timers as well (the cinematic and its end relay; the per-target
    // rule and the disc check are in port_pickup_presentation.h).
    const int seamlessMode = PortOverlay::SeamlessPickups();
    const bool seamless =
        !mgr.IsMultiplayer() &&
        (seamlessMode == PortOverlay::kSeamlessPickups_All ||
         (seamlessMode == PortOverlay::kSeamlessPickups_Repeats && repeatPickup));
    if (randoMemo || seamless) {
      std::set< uint16_t > visited;
      std::vector< const CEntity* > pending;
      pending.push_back(this);
      for (bool first = true; !pending.empty(); first = false) {
        const CEntity* cur = pending.back();
        pending.pop_back();
        for (rstl::vector< SConnection >::const_iterator it = cur->GetConnectionList().begin();
             it != cur->GetConnectionList().end(); ++it) {
          if (first && it->state != kSS_Arrived) {
            continue;
          }
          CStateManager::TIdListResult found = mgr.GetIdListForScript(it->objId);
          for (CStateManager::TIdList::const_iterator c = found.first; c != found.second; ++c) {
            const CEntity* next = mgr.GetObjectById(c->second);
            if (next == nullptr) {
              continue;
            }
            const uint16_t uid = next->GetUniqueId().Value();
            // dynamic_cast: host-only, TCastTo has no visitor for these types. Targets are marked
            // per message (a control hint gets both Increment and Decrement); only relays and
            // timers are visited once.
            if (dynamic_cast< const CScriptHUDMemo* >(next) != nullptr) {
              if (randoMemo) {
                PortRandomizer::SuppressMemoOnce(uid);
              } else if (it->msg == kSM_SetToZero) {
                PortPickupPresentation::Mark(PortPickupPresentation::kMemo, uid);
              }
            } else if (dynamic_cast< const CScriptControlHint* >(next) != nullptr) {
              if (seamless && it->msg == kSM_Increment) {
                PortPickupPresentation::Mark(PortPickupPresentation::kControlHint, uid);
              }
            } else if (dynamic_cast< const CScriptCamera* >(next) != nullptr) {
              if (seamless && it->msg == kSM_Activate) {
                PortPickupPresentation::Mark(PortPickupPresentation::kCamera, uid);
              }
            } else if (dynamic_cast< const CScriptRelay* >(next) != nullptr ||
                       dynamic_cast< const CScriptTimer* >(next) != nullptr ||
                       (seamless && (dynamic_cast< const CScriptMemoryRelay* >(next) != nullptr ||
                                     dynamic_cast< const CScriptSequenceTimer* >(next) != nullptr))) {
              if (visited.insert(uid).second) {
                pending.push_back(next);
              }
            }
          }
        }
      }
    }
#endif
    SendScriptMsgs(kSS_Arrived, mgr, player->GetUniqueId(), kSM_None);
#ifdef TARGET_PC
    if (randoMemo) {
      // wstring_l only borrows its buffer (the memo text outlives this call), so build an owning string.
      std::wstring wide(randoText.begin(), randoText.end());
      CSamusHud::DisplayHudMemo(rstl::wstring(wide.c_str()),
                                CHUDMemoParms(5.f, true, false, false, 1 << playerIndex, true));
    }
#endif
    if (mRespawnTime == 0.0f) {
      mSuppressDeathScriptMsgs = true;
      mgr.DeleteObjectRequest(GetUniqueId());
    } else {
      SetModelFlags(CModelFlags::AlphaBlended(0.f).DepthCompareUpdate(true, false));
      mRespawnTimer = mRespawnTime;
      mCurTime = 0.f;
      mFadeTime = 0.25f;
    }

#ifdef TARGET_PC
    if (mgr.GetWorld() != nullptr) {
      PortRandomizer::RecordCheck(
          static_cast< uint32_t >(mgr.GetWorld()->IGetWorldAssetId()),
          static_cast< uint32_t >(
              mgr.GetWorld()->IGetAreaAlways(GetCurrentAreaId())->IGetAreaAssetId()),
          static_cast< uint32_t >(GetEditorId().value), static_cast< int >(itemType));
    }
#endif
    if (previousAmount < playerState->GetItemAmount(itemType)) {
      ShowAllKeysCollectedAlert(mgr, playerState, itemType);
    }

    if (mPercentageIncrease > 0) {
      int total = playerState->GetTotalPickupCount();
      int colRate = playerState->CalculateItemCollectionRate();
      if (colRate == total) {
        CAssetId id = gpResourceFactory->GetResourceIdByName("STRG_AllPickupsFound_2")->id;

        mgr.QueueMessage(mgr.GetHUDMessageFrameCount() + 1, id, 0.f);
        gpGameState->SystemOptions().EnvVars().FindEnvironmentVariable("AllPickupsFound")->Set(1);
      }
    }

    if (!mgr.IsMultiplayer() && itemType == CPlayerState::kIT_Powerbomb && mCapacity == 0) {
      CPersistentOptions& opts = gpGameState->SystemOptions();
      if (opts.EnvVars().FindEnvironmentVariable("PowerbombPickupMessages")->GetValue() == 0) {
        opts.EnvVars().FindEnvironmentVariable("PowerbombPickupMessages")->Set(1);
        CSamusHud::DisplayHudMemo(rstl::wstring_l(gpStringTable->GetString("FirstPowerBombPickup")),
                                  CHUDMemoParms(5.f, true, false, false, 1 << playerIndex, true));
      }
    }
    switch (itemType) {
    case CPlayerState::kIT_SwitchVisorCombat:
      playerState->StartTransitionToVisor(CPlayerState::kPV_Combat);
      break;
    case CPlayerState::kIT_SwitchVisorScan:
      playerState->StartTransitionToVisor(CPlayerState::kPV_Scan);
      break;
    case CPlayerState::kIT_SwitchVisorDark:
      playerState->StartTransitionToVisor(CPlayerState::kPV_Dark);
      break;
    case CPlayerState::kIT_SwitchVisorEcho:
      playerState->StartTransitionToVisor(CPlayerState::kPV_Echo);
      break;
    }
  }
}

rstl::optional_object< CAABox > CScriptPickup::GetTouchBounds() const {
  const CVector3f& off = GetTranslation();
  return CAABox(mTouchBounds.GetMinPoint() + off, mTouchBounds.GetMaxPoint() + off);
}

void CScriptPickup::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  switch (msg.GetMessage()) {
  case kSM_Activate:
    mTransformZ = GetTranslation().GetZ();
    break;
  case kSM_Create:
    if (mgr.GetScriptObjectLoaderHelper().IsGeneratingObject()) {
      mSuppressBobbing = true;
    }
    break;
  case kSM_Delete:
    if (!mSuppressDeathScriptMsgs) {
      SendScriptMsgs(kSS_Dead, mgr, kInvalidUniqueId, kSM_None);
    }
    skHomingPickupId = kInvalidUniqueId;
    break;
  }
  CActor::AcceptScriptMsg(mgr, msg);
}

void CScriptPickup::AddToRenderer(const CStateManager& mgr) const {
  if (IsVisible()) {
    CActor::AddToRenderer(mgr);
  }
}

CPlayerState::EItemType CScriptPickup::GetItem() const { return mItemType; }

void CScriptPickup::SetWasGenerated(CStateManager& mgr) {
  if (!mgr.IsMultiplayer()) {
    mCanHomeByDefault = true;
  }
  mSuppressBobbing = true;
}

CVector3f CScriptPickup::GetOrbitPosition(const CStateManager& mgr) const {
  return GetTransform().Rotate(mOrbitOffset) + GetTranslation();
}

void CScriptPickup::ShowAllKeysCollectedAlert(CStateManager& mgr, CPlayerState* playerState,
                                              CPlayerState::EItemType itemType) {
  const char* message = nullptr;
  switch (itemType) {
  case CPlayerState::kIT_TempleKey1:
  case CPlayerState::kIT_TempleKey2:
  case CPlayerState::kIT_TempleKey3:
  case CPlayerState::kIT_TempleKey4:
  case CPlayerState::kIT_TempleKey5:
  case CPlayerState::kIT_TempleKey6:
  case CPlayerState::kIT_TempleKey7:
  case CPlayerState::kIT_TempleKey8:
  case CPlayerState::kIT_TempleKey9:
    if (playerState->GetItemAmount(CPlayerState::kIT_TempleKey1) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TempleKey2) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TempleKey3) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TempleKey4) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TempleKey5) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TempleKey6) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TempleKey7) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TempleKey8) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TempleKey9) > 0) {
      message = "STRG_AllTempleKeysFound";
    }
    break;
  case CPlayerState::kIT_AgonKey1:
  case CPlayerState::kIT_AgonKey2:
  case CPlayerState::kIT_AgonKey3:
    if (playerState->GetItemAmount(CPlayerState::kIT_AgonKey1) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_AgonKey2) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_AgonKey3) > 0) {
      message = "STRG_AllSandKeysFound";
    }
    break;
  case CPlayerState::kIT_TorvusKey1:
  case CPlayerState::kIT_TorvusKey2:
  case CPlayerState::kIT_TorvusKey3:
    if (playerState->GetItemAmount(CPlayerState::kIT_TorvusKey1) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TorvusKey2) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_TorvusKey3) > 0) {
      message = "STRG_AllSwampKeysFound";
    }
    break;
  case CPlayerState::kIT_HiveKey1:
  case CPlayerState::kIT_HiveKey2:
  case CPlayerState::kIT_HiveKey3:
    if (playerState->GetItemAmount(CPlayerState::kIT_HiveKey1) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_HiveKey2) > 0 &&
        playerState->GetItemAmount(CPlayerState::kIT_HiveKey3) > 0) {
      message = "STRG_AllCliffsKeysFound";
    }
    break;
  }

  if (message) {
    const CAssetId id = gpResourceFactory->GetResourceIdByName(message)->id;
    mgr.QueueMessage(mgr.GetHUDMessageFrameCount() + 1, id, 0.f);
  }
}

CAABox LoadCAABox(CStateManager& mgr, const TAreaId& areaId, const CVector3f& collisionSize,
                  const CVector3f& collisionOffset);

rstl::optional_object< CModelData > LdrToModelData(const CVector3f&, CAssetId asset,
                                                   const SLdrAnimationSet&, bool);

CEntity* LoadPickup(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrPickup sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrPickup.inc"

#ifdef TARGET_PC
  if (mgr.GetWorld() != nullptr) {
    // Randomizer: rewrite (or, in dump mode, log) the item this location gives.
    const int originalItem = sldrThis.itemToGive.value;
    int randoItem = originalItem;
    const bool applied = PortRandomizer::ApplyPickup(
        static_cast< uint32_t >(mgr.GetWorld()->IGetWorldAssetId()),
        static_cast< uint32_t >(mgr.GetWorld()->IGetAreaAlways(info.GetAreaId())->IGetAreaAssetId()),
        static_cast< uint32_t >(info.GetEditorId().value), randoItem, sldrThis.capacityIncrease,
        sldrThis.amount);
    sldrThis.itemToGive.value = randoItem;
    if (applied && randoItem != originalItem) {
      // Look like the new item: borrow a vanilla pickup's model, but only if its assets are
      // resolvable now (they live in a world pak). Otherwise keep the original model.
      const uint32_t world = static_cast< uint32_t >(mgr.GetWorld()->IGetWorldAssetId());
      const PortRandoModels::Donor* donor = PortRandoModels::ForItem(randoItem, world);
      if (donor != nullptr && gpResourceFactory->GetResourceTypeById(CAssetId(donor->model)) != 0 &&
          (donor->anim[0] == PortRandoModels::kInvalidAsset ||
           gpResourceFactory->GetResourceTypeById(CAssetId(donor->anim[0])) != 0)) {
        sldrThis.model = CAssetId(donor->model);
        sldrThis.animationInformation.ancs = CAssetId(donor->anim[0]);
        sldrThis.animationInformation.character_index = static_cast< int >(donor->anim[1]);
        sldrThis.animationInformation.initial_anim = static_cast< int >(donor->anim[2]);
      }
    }
  }
#endif
  rstl::optional_object< CModelData > modelData(
      LdrToModelData(sldrThis.editorProperties.transform.scale, sldrThis.model,
                     sldrThis.animationInformation, true));
  if (!modelData) {
    return nullptr;
  }

  CAABox box = LoadCAABox(mgr, info.GetAreaId(), sldrThis.collisionSize, sldrThis.collisionOffset);
  if (sldrThis.collisionSize == CVector3f::Zero()) {
    box = modelData->GetBounds(LdrToTransform4f(sldrThis.editorProperties).GetRotation());
  }
  return rs_new CScriptPickup(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      *modelData, LdrToActorParameters(sldrThis.actorInformation),
      LdrToEchoParameters(sldrThis.echoInformation), box,
      CPlayerState::EItemType(sldrThis.itemToGive.value), sldrThis.amount,
      sldrThis.capacityIncrease, sldrThis.itemPercentageIncrease, sldrThis.pickupEffect,
      sldrThis.absoluteValue, sldrThis.canHomeByDefault, sldrThis.autoSpin, sldrThis.blinkOut,
      sldrThis.lifetime, sldrThis.respawnTime, sldrThis.fadetime, sldrThis.activationDelay,
      sldrThis.pickupEffectLifetime, sldrThis.autoHomeRange, sldrThis.delayUntilHome,
      sldrThis.homingSpeed, CVector3f(sldrThis.orbitOffset));
}
