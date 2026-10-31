#include "MetroidPrime/CStateManager.hpp"

#ifdef TARGET_PC
#include <stdlib.h>  // getenv: the port's opt-in diagnostics
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h> // strcasecmp
#endif
#include <cctype>
#include <string>
#include <utility>
#include <vector>
#include <chrono>
#include <thread>
#include "Kyoto/CPakFile.hpp"
#include "Kyoto/CFrameDelayedKiller.hpp"
#include "Kyoto/CResLoader.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetroidPrime/CDummyWorld.hpp"
#include "MetroidPrime/CMapArea.hpp"
#include "MetroidPrime/CMapWorld.hpp"
#include "MetroidPrime/ScriptObjects/CScriptAreaProperties.hpp"
#include "port_discord.h"
#endif
#include "Collision/CMRay.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Collision/CollisionUtil.hpp"
#include "Kyoto/Basics/CCast.hpp"

#include "Kyoto/CMemoryCardSys.hpp"
#include "MetroidPrime/BodyState/CBodyController.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CActorModelParticles.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CCollisionActor.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDecalManager.hpp"
#include "MetroidPrime/CEchoEmitter.hpp"
#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/CEnvFxManager.hpp"
#include "MetroidPrime/CExplosion.hpp"
#include "MetroidPrime/CFluidPlaneManager.hpp"
#include "MetroidPrime/CGameCollision.hpp"
#include "MetroidPrime/CGameHint.hpp"
#include "MetroidPrime/CGameLight.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CHintManager.hpp"
#include "MetroidPrime/CInGameGuiManager.hpp"
#include "MetroidPrime/CInGameGuiManagerSet.hpp"
#include "MetroidPrime/CKnockBackInfo.hpp"
#include "MetroidPrime/CMain.hpp"
#include "MetroidPrime/CMapWorld.hpp"
#include "MetroidPrime/CMapWorldInfo.hpp"
#include "MetroidPrime/CMemoryCard.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include "MetroidPrime/CPortalArea.hpp"
#include "MetroidPrime/CPortalTransition.hpp"
#include "MetroidPrime/CProjectedShadow.hpp"
#include "MetroidPrime/CRumbleManager.hpp"
#include "MetroidPrime/CSafeZoneManager.hpp"
#include "MetroidPrime/CSaveGameScreen.hpp"
#include "MetroidPrime/CScopedProfiler.hpp"
#include "MetroidPrime/CScriptMailbox.hpp"
#include "MetroidPrime/CSortedLists.hpp"
#include "MetroidPrime/CStateManagerContainer.hpp"
#include "MetroidPrime/CWeaponMgr.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/CWorldLayerState.hpp"
#include "MetroidPrime/Cameras/CCinematicCamera.hpp"
#include "MetroidPrime/Cameras/CGameCamera.hpp"
#include "MetroidPrime/Enemies/CMetroid.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Enemies/CSnakeWeedSwarm.hpp"
#include "MetroidPrime/Enemies/CSwarmBasics.hpp"
#include "MetroidPrime/GameObjectLists.hpp"
#include "MetroidPrime/HUD/CHUDMemoParms.hpp"
#include "MetroidPrime/HUD/CSamusHud.hpp"
#include "MetroidPrime/Player/CGameMode.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CGameStateEnvVarManager.hpp"
#include "MetroidPrime/Player/CMorphBall.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerGun.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/Player/CPlayerTargeting.hpp"
#include "MetroidPrime/Player/CWorldState.hpp"
#include "MetroidPrime/Player/CWorldTransManager.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDock.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/ScriptObjects/CScriptDamageableTrigger.hpp"
#include "port_pipeline_context.h"
#endif
#include "MetroidPrime/ScriptObjects/CScriptDoor.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDynamicLight.hpp"
#include "MetroidPrime/ScriptObjects/CScriptEffect.hpp"
#include "MetroidPrime/ScriptObjects/CScriptForgottenObject.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPlatform.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSpawnPoint.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/ScriptObjects/CScriptWorldTeleporter.hpp"
#endif
#include "MetroidPrime/ScriptObjects/CScriptSpecialFunction.hpp"
#include "MetroidPrime/ScriptObjects/CScriptWater.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Tweaks/CTweakGame.hpp"
#include "MetroidPrime/Tweaks/CTweakGui.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayer.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayerGun.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayerRes.hpp"
#include "MetroidPrime/Weapons/CWeapon.hpp"
#include "Weapons/CCollisionResponseData.hpp"

#include "Kyoto/Audio/CAudioGroupSet.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Audio/CStreamAudioManager.hpp"
#include "Kyoto/Basics/CStopwatch.hpp"
#include "Kyoto/Basics/RAssertDolphin.hpp"
#include "Kyoto/CARAMManager.hpp"
#include "Kyoto/CARAMToken.hpp"
#include "Kyoto/CDependencyGroup.hpp"
#include "Kyoto/CFrameDelayedKiller.hpp"
#include "Kyoto/CResFactory.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/CTimeProvider.hpp"
#include "Kyoto/Graphics/CCubeModel.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Graphics/CGraphicsPalette.hpp"
#include "Kyoto/Graphics/CModel.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "Kyoto/Math/CQuaternion.hpp"
#include "Kyoto/Math/CRelAngle.hpp"
#include "Kyoto/Math/CloseEnough.hpp"
#include "Kyoto/PVS/CPVSVisSet.hpp"
#include "Kyoto/Particles/CElementGen.hpp"
#include "Kyoto/Particles/CParticleElectric.hpp"
#include "Kyoto/Particles/CParticleSpawnSystem.hpp"
#include "Kyoto/Particles/CSortedParticleSystem.hpp"
#include "Kyoto/Text/CStringTable.hpp"
#include "MetaRender/AmbientLightScale.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#ifdef TARGET_PC
#include "port_collision_view.h"
#endif
#include "MetroidPrime/CSimpleShadow.hpp"
#include "Weapons/CDecal.hpp"
#include "Weapons/CProjectileWeapon.hpp"
#include "WorldFormat/CPVSAreaSet.hpp"

#include "rstl/algorithm.hpp"
#include "rstl/math.hpp"
#include "rstl/vector.hpp"

#ifdef TARGET_PC
#include "port_harness.h"
#include "port_entry.h"
#include "port_hold_toggle.h"
#include "port_touch.h"
#include "port_pointer.h"
#include "port_mouse.h"
#include "port_livesplit.h"
#include "port_overlay.h"
#include "port_savestate.h"
#include "port_warp.h"
#include "MetroidPrime/PortScanTarget.hpp"
#ifdef TARGET_PC
#include "port_console.h"
#endif
#include "port_freecam.h"
#endif

#include <alloca.h>
#include <float.h>

const int gkPVSEnabled = 1;

static s64 sPreRenderStepTime;

// Prime-correlated lazy initialization; Echoes schedules rumble rather than camera shakes.
static float sNextEscapeRumble;
static char sEscapeRumbleInitialized;

// Prime-correlated name; native underwater ranges for the two bomb attributes.
static const float skBombUnderwaterRanges[2] = {2.f, 4.f};

static const char* const skAudioGroupDependencies[3] = {"audio_groups_single_player_DGRP",
                                                        "audio_groups_front_end_DGRP",
                                                        "audio_groups_multi_player_DGRP"};
static const char* const skSinglePlayerAnimController = "SinglePlayerAnimCtrl";
static const char* const skMultiplayerAnimController = "PlayerAnimCtrl";
static const char* const skUnusedViewportTexture = "TXTR_Metroid2LogoSm";

// Both retained release hooks contain only a return instruction. Their sole known
// callers pass this manager; no exported name or body establishes a semantic name.
extern "C" void fn_8003FF1C(CStateManager*) {}
extern "C" void fn_8003FF20(CStateManager*) {}

bool CStateManager::CanCreateProjectile(TUniqueId owner, EWeaponType type, int maxAllowed) {
  return mWeaponMgr->GetNumActive(owner, type) < maxAllowed;
}

CStateManagerContainer::CStateManagerContainer()
: mCameraManager0(kInvalidUniqueId, CPlayerState::kPI_Player1)
, mCameraManager1(kInvalidUniqueId, CPlayerState::kPI_Player2)
, mCameraManager2(kInvalidUniqueId, CPlayerState::kPI_Player3)
, mCameraManager3(kInvalidUniqueId, CPlayerState::kPI_Player4)
, mRumbleManager0(CPlayerState::kPI_Player1,
                  static_cast< EIOPort >(
                      gpGameState->GetPlayerState(CPlayerState::kPI_Player1)->GetPlayerSelection()))
, mRumbleManager1(CPlayerState::kPI_Player2,
                  static_cast< EIOPort >(
                      gpGameState->GetPlayerState(CPlayerState::kPI_Player2)->GetPlayerSelection()))
, mRumbleManager2(CPlayerState::kPI_Player3,
                  static_cast< EIOPort >(
                      gpGameState->GetPlayerState(CPlayerState::kPI_Player3)->GetPlayerSelection()))
, mRumbleManager3(
      CPlayerState::kPI_Player4,
      static_cast< EIOPort >(
          gpGameState->GetPlayerState(CPlayerState::kPI_Player4)->GetPlayerSelection())) {}

namespace {
// Reconstructed name for the native sentinel outside all valid player indices.
const int kInvalidRenderPlayerIndex = 2000000;

// Guessed local predicate names; priorities and equal-priority intensities descend.
struct CLightPredicate {
  bool operator()(const CLight& a, const CLight& b) const {
    if (a.GetPriority() > b.GetPriority()) {
      return true;
    }
    if (a.GetPriority() == b.GetPriority()) {
      return a.GetIntensity() > b.GetIntensity();
    }
    return false;
  }
};

struct CActorLightPredicate {
  bool operator()(const rstl::pair< TUniqueId, CLight >& a,
                  const rstl::pair< TUniqueId, CLight >& b) const {
    return CLightPredicate()(a.second, b.second);
  }
};

// Guessed local type/member names, correlated with Prime's area-ordering predicate.
class area_sorter {
public:
  area_sorter(const CVector3f& reference, TAreaId visibleAreaId)
  : mReference(reference), mVisibleAreaId(visibleAreaId) {}

  bool operator()(const CGameArea* a, const CGameArea* b) const;

private:
  CVector3f mReference;
  TAreaId mVisibleAreaId;
};
CHECK_SIZEOF(area_sorter, 0x10)

bool area_sorter::operator()(const CGameArea* a, const CGameArea* b) const {
  const TAreaId aId = a->GetId();
  const TAreaId bId = b->GetId();
  if (aId == bId) {
    return false;
  }
  if (aId == mVisibleAreaId) {
    return false;
  }
  if (bId == mVisibleAreaId) {
    return true;
  }

  const float aDot = CVector3f::Dot(mReference, a->GetAABB().GetCenterPoint());
  const float bDot = CVector3f::Dot(mReference, b->GetAABB().GetCenterPoint());
  return aDot > bDot;
}
} // namespace

CLight::CLight(const CLight& other)
: mPos(other.mPos)
, mDir(other.mDir)
, mColor(other.mColor)
, mType(other.mType)
, mSpotCutoff(other.mSpotCutoff)
, mDistC(other.mDistC)
, mDistL(other.mDistL)
, mDistQ(other.mDistQ)
, mAngleC(other.mAngleC)
, mAngleL(other.mAngleL)
, mAngleQ(other.mAngleQ)
, mPriority(other.mPriority)
, mLightId(other.mLightId)
, mCachedRadius(other.mCachedRadius)
, mCachedIntensity(other.mCachedIntensity)
, mIntensityDirty(other.mIntensityDirty)
, mRadiusDirty(other.mRadiusDirty) {}

bool CStateManager::IsActorVisible(const CActor& actor) const {
  if (actor.UsesPortalVisibility()) {
    const CPortalArea* portals =
        mWorld->GetAreaAlways(actor.GetCurrentAreaId()).GetPostConstructed()->mPortalArea.get();
    if (portals != nullptr) {
      return portals->GetVisibleActors().GetObjectById(actor.GetUniqueId()) != nullptr;
    }
  }

  for (rstl::reserved_vector< rstl::pair< int, CFrustumPlanes >, 10 >::const_iterator it =
           mAreaFrusta.begin();
       it != mAreaFrusta.end(); ++it) {
    if (it->first == actor.GetCurrentAreaId().Value()) {
      if (!it->second.BoxInFrustumPlanes(actor.GetOtherBounds())) {
        return false;
      }
      break;
    }
  }

  return mPlanes.BoxInFrustumPlanes(actor.GetOtherBounds());
}

void CStateManager::AddDarkWorldSphereToRenderer(
    const CVector3f& pos, const CVector3f& scale, uchar mix, uchar alpha, bool inside, float lod,
    const CVector2f& scroll1, const CVector2f& scroll2, const CVector2f& texScale1,
    const CVector2f& texScale2, const CTexture& environment, const CTexture& cloud1,
    const CTexture& cloud2, CColor color, CColor additiveColor, bool cylinder) const {
  const CPlayerState::EPlayerVisor visor = mPlayerState->GetActiveVisor(*this);
  if (mIsDarkWorld && visor != CPlayerState::kPV_Echo) {
    gpRender->DrawDarkWorldVolume(pos, scale, mix, alpha, inside, lod, scroll1, scroll2, texScale1,
                                  texScale2, environment, cloud1, cloud2, color, additiveColor,
                                  cylinder, visor == CPlayerState::kPV_Dark);
  }
}

ushort CStateManager::ReturnFirstIfSingleElseSecond(uint single, uint multi) const {
  return IsMultiplayer() ? multi : single;
}

void CStateManager::AddDrawableActor(const CActor& actor, const CVector3f& pos,
                                     const CAABox& bounds) const {
  actor.SetAddedToken(mObjectDrawToken + 1);
  gpRender->AddDrawable(&actor, pos, bounds, 0,
                        actor.UsesAlphaSorting() ? IRenderer::kDS_AlphaSortedCallback
                                                 : IRenderer::kDS_SortedCallback);
}

void CStateManager::AddDrawableActorPlane(const CActor& actor, const CPlane& plane,
                                          const CAABox& bounds) const {
  actor.SetAddedToken(mObjectDrawToken + 1);
  gpRender->AddPlaneObject(&actor, bounds, plane, 0);
}

void CStateManager::CalculatePlayerViewport(int viewportIndex, int* left, int* bottom, int* width,
                                            int* height) const {
  int numPlayers = GetNumPlayers();
  if (mCameraManagers[0]->IsInFullScreenCinematic()) {
    numPlayers = 1;
  }

  const int fullWidth = CGraphics::GetRenderMode().fbWidth;
  const int fullHeight = CGraphics::GetRenderMode().xfbHeight;
  int viewportLeft = 0;
  int viewportBottom = 0;
  int viewportWidth = fullWidth / 2;
  int viewportHeight = fullHeight / 2;

  if (numPlayers == 1) {
    viewportWidth = fullWidth;
    viewportHeight = fullHeight;
    viewportLeft = 0;
    viewportBottom = 0;
  } else if (numPlayers == 2) {
    viewportWidth = fullWidth;
    viewportLeft = 0;
    switch (viewportIndex) {
    case 0:
      viewportBottom = viewportHeight;
      break;
    case 1:
      viewportBottom = 0;
      break;
    }
  } else {
    switch (viewportIndex) {
    case 0:
      viewportLeft = 0;
      viewportBottom = viewportHeight;
      break;
    case 1:
      viewportLeft = viewportWidth;
      viewportBottom = viewportHeight;
      break;
    case 2:
      viewportLeft = 0;
      viewportBottom = 0;
      break;
    case 3:
      viewportLeft = viewportWidth;
      viewportBottom = 0;
      break;
    }
  }

  if (left != nullptr) {
    *left = viewportLeft;
  }
  if (bottom != nullptr) {
    *bottom = viewportBottom;
  }
  if (width != nullptr) {
    *width = viewportWidth;
  }
  if (height != nullptr) {
    *height = viewportHeight;
  }
}

void CStateManager::SetupPlayerViewport(uint playerIndex) {
  mCurrentRenderPlayerIndex = playerIndex;

  int left, bottom, width, height;
  CalculatePlayerViewport(mNumPlayers > 2u ? mPlayerStates[playerIndex]->GetPlayerSelection()
                                           : playerIndex,
                          &left, &bottom, &width, &height);
  CGraphics::SetViewport(left, bottom, width, height);
  CGraphics::SetScissor(left, bottom, width, height);

  const CViewport& viewport = CGraphics::GetViewport();
  const float pixelAspect = CGraphics::GetPixelAspectRatio();
  const float viewportAspect = float(viewport.mWidth) / float(viewport.mHeight);
  mCameraManagers[mCurrentRenderPlayerIndex]->SetAspectRatio(pixelAspect * viewportAspect, *this);

  mCurrentRenderPlayer = mPlayers[playerIndex];
  mPlayerState = mPlayerStates[playerIndex];
  mCameraManager = mCameraManagers[playerIndex];
}

void CStateManager::DrawUnusedViewport(int viewportIndex) {
  int left, bottom, width, height;
  CalculatePlayerViewport(viewportIndex, &left, &bottom, &width, &height);
  CGraphics::SetViewport(left, bottom, width, height);
  CGraphics::SetScissor(left, bottom, width, height);

  const CTexture* texture = mUnusedViewportTexture->GetObject();
  if (texture != nullptr) {
    CGraphics::SetTevOp(kTS_Stage0, CGraphics::kEnvModulate);
    CGraphics::SetTevOp(kTS_Stage1, CGraphics::kEnvPassthru);
    gpRender->SetBlendMode_Replace();
    gpRender->SetDepthReadWrite(false, false);
    CGraphics::Render2D(*texture, 0, 0, width, height, CColor::White().WithAlphaOf(1.f));
  }
}

int CStateManager::GetViewportLayoutIndex() const {
  if (mNumPlayers == 1u) {
    return 0;
  }

  int layout = 2;
  if (mNumPlayers == 2u) {
    layout = 1;
  }
  return layout;
}

void CStateManager::AddProjectedShadow(CProjectedShadow* shadow) {
  shadow->SetNextShadow(mProjectedShadows);
  mProjectedShadows = shadow;
}

void CStateManager::BuildDynamicLightListForWorld() {
  if (mRenderVisorMode != kRVM_Normal || mNumPlayers >= 3u) {
    mDynamicLights = rstl::vector< CLight >();
    mDynamicActorLights = rstl::vector< rstl::pair< TUniqueId, CLight > >();
    return;
  }

  const CObjectList& list = GetObjectListById(kOL_GameLight);
  const int listSize = list.size();
  if (listSize == 0) {
    return;
  }

  if (mDynamicLights.capacity() != listSize) {
    mDynamicLights = rstl::vector< CLight >();
    mDynamicLights.reserve(listSize);
    mDynamicActorLights = rstl::vector< rstl::pair< TUniqueId, CLight > >();
    mDynamicActorLights.reserve(listSize);
  } else {
    mDynamicLights.clear();
    mDynamicActorLights.clear();
  }

  for (int idx = list.GetFirstObjectIndex(); idx != -1; idx = list.GetNextObjectIndex(idx)) {
    const CGameLight* light = static_cast< const CGameLight* >(list[idx]);
    if (light && light->GetActive()) {
#ifdef TARGET_PC
      // Port: lights follow their actor's presented pose, so moving lights
      // (the morph ball's glow, projectile lights) glide between ticks.
      CLight value = light->GetLight();
      CTransform4f toCur = CTransform4f::Identity();
      if (light->PortPresentedView(CTransform4f::Identity(), toCur)) {
        const CTransform4f toPresented = toCur.GetQuickInverse();
        if (value.GetType() != kLT_Directional)
          value.SetPosition(toPresented * value.GetPosition());
        if (value.GetType() != kLT_Point)
          value.SetDirection(toPresented.Rotate(value.GetDirection()).AsNormalized());
      }
#else
      const CLight& value = light->GetLight();
#endif
      if (value.GetIntensity() > FLT_EPSILON && value.GetRadius() > FLT_EPSILON) {
        if (const CScriptDynamicLight* dynamicLight =
                TCastToConstPtr< CScriptDynamicLight >(light)) {
          if (dynamicLight->UsesLayerOne() || dynamicLight->UsesLayerTwo()) {
            mDynamicActorLights.push_back_unsafe(
                rstl::pair< TUniqueId, CLight >(dynamicLight->GetUniqueId(), value));
          }
          if (dynamicLight->UsesWorld()) {
            mDynamicLights.push_back_unsafe(value);
          }
        } else {
          mDynamicActorLights.push_back_unsafe(
              rstl::pair< TUniqueId, CLight >(kInvalidUniqueId, value));
          mDynamicLights.push_back_unsafe(value);
        }
      }
    }
  }

  rstl::sort(mDynamicLights.begin(), mDynamicLights.end(), CLightPredicate());
  rstl::sort(mDynamicActorLights.begin(), mDynamicActorLights.end(), CActorLightPredicate());
}

void CStateManager::UpdateObjectInLists(CEntity& entity) {
  for (rstl::reserved_vector< CObjectList*, 8 >::iterator it = mDynamicObjectLists.begin();
       it != mDynamicObjectLists.end(); ++it) {
    const bool contained =
        static_cast< const CObjectList* >(*it)->GetObjectById(entity.GetUniqueId()) != nullptr;
    if (contained && !(*it)->IsQualified(entity)) {
      (*it)->RemoveObject(entity.GetUniqueId());
    } else if (!contained) {
      (*it)->AddObject(entity);
    }
  }

  for (rstl::reserved_vector< CFilteredObjectList*, 6 >::iterator it =
           mDynamicFilteredObjectLists.begin();
       it != mDynamicFilteredObjectLists.end(); ++it) {
    CFilteredObjectList* list = *it;
    const bool contained = list->Contains(entity);
    if (contained && !list->IsQualified(entity)) {
      list->RemoveObject(entity);
    } else if (!contained) {
      list->AddObject(entity);
    }
  }
}

CRayCastResult CStateManager::RayWorldIntersection(
    TUniqueId& idOut, const CVector3f& position, const CVector3f& direction, float length,
    const CMaterialFilter& filter, const rstl::reserved_vector< TUniqueId, 1024 >& nearList) const {
  return CGameCollision::RayWorldIntersection(*this, idOut, position, direction, length, filter,
                                              nearList);
}

CRayCastResult CStateManager::RayStaticIntersection(const CVector3f& position,
                                                    const CVector3f& direction, float length,
                                                    const CMaterialFilter& filter) const {
  return CGameCollision::RayStaticIntersection(*this, position, direction, length, filter);
}

bool CStateManager::RayCollideWorld(const CVector3f& start, const CVector3f& end,
                                    const rstl::reserved_vector< TUniqueId, 1024 >& nearList,
                                    const CMaterialFilter& filter,
                                    const CActor* ignoreActor) const {
  return RayCollideWorldInternal(start, end, filter, nearList, ignoreActor);
}

bool CStateManager::RayCollideWorld(const CVector3f& start, const CVector3f& end,
                                    const CMaterialFilter& filter, const CActor* ignoreActor) {
  rstl::reserved_vector< TUniqueId, 1024 > nearList;
  CVector3f direction = end - start;
  const float length = direction.Magnitude();
  direction *= 1.f / length;

  BuildNearList(nearList, start, direction, length, filter, ignoreActor);
  return RayCollideWorldInternal(start, end, filter, nearList, ignoreActor);
}

const bool CStateManager::RayCollideWorldInternal(
    const CVector3f& start, const CVector3f& end, const CMaterialFilter& filter,
    const rstl::reserved_vector< TUniqueId, 1024 >& nearList, const CActor* ignoreActor) const {
  CVector3f direction = end - start;
  bool visible = true;
  if (direction.CanBeNormalized()) {
    const float length = direction.Magnitude();
    direction *= 1.f / length;
    visible = CGameCollision::RayStaticLineOfSightTest(*this, start, direction, length, filter);
    if (visible) {
      visible = CGameCollision::RayDynamicLineOfSightTest(*this, start, direction, length, filter,
                                                          nearList, ignoreActor);
    }
  }
  return visible;
}

void CStateManager::AddObject(CEntity* entity) {
  if (entity != nullptr) {
    AddObject(*entity);
  }
}

void CStateManager::BuildNearList(rstl::reserved_vector< TUniqueId, 1024 >& nearList,
                                  const CAABox& bounds, const CMaterialFilter& filter,
                                  const CActor* ignoreActor) const {
  mSortedListManager->BuildNearList(nearList, bounds, filter, ignoreActor);
}

void CStateManager::BuildColliderList(rstl::reserved_vector< TUniqueId, 1024 >& nearList,
                                      const CActor& actor, const CAABox& bounds) const {
  mSortedListManager->BuildNearList(nearList, actor, bounds);
}

void CStateManager::BuildNearList(rstl::reserved_vector< TUniqueId, 1024 >& nearList,
                                  const CVector3f& position, const CVector3f& direction,
                                  float length, const CMaterialFilter& filter,
                                  const CActor* ignoreActor) const {
  mSortedListManager->BuildNearList(nearList, position, direction, length, filter, ignoreActor);
}

void CStateManager::AreaLoaded(TAreaId area) {
#ifdef TARGET_PC
  // PORT: a save state's room state carries what the mailbox's messages did (RoomRestoreOwnsArea).
  if (!PortSaveState::RoomRestoreOwnsArea(*this, area.Value()))
#endif
  mMailbox->SendMsgs(area, *this);
  mEnvFxManager->AreaLoaded();
}

void CStateManager::PrepareAreaUnload(TAreaId area) {
  const rstl::list< CEntity* >& doors = GetDoorList();
  for (rstl::list< CEntity* >::const_iterator it = doors.begin(); it != doors.end(); ++it) {
    CScriptDoor* door = static_cast< CScriptDoor* >(*it);
    if (door->IsConnectedToArea(*this, area)) {
      door->ForceClosed(*this);
    }
  }

  ScriptObjectLoaderHelper().FreeScriptObjects(area, *this);
}

void CStateManager::AreaUnloaded(TAreaId area) {}

CEntity* CStateManager::ObjectById(TUniqueId uid) {
  return mObjectLists[kOL_All]->GetObjectById(uid);
}

void CStateManager::DeleteObjectRequest(TUniqueId uid) {
  SendScriptMsg(uid, kInvalidUniqueId, kSM_Delete, kInvalidUniqueId);
}

void CStateManager::SetCurrentAreaId(TAreaId area) {
  if (mNextAreaId != area) {
    mPreviousAreaId = mNextAreaId;
    mNextAreaId = area;
  }

  const TAreaId& currentArea = area;
  if (currentArea != kInvalidAreaId) {
    if (!mMapWorldInfo->IsAreaVisited(currentArea)) {
      mMapWorldInfo->SetAreaVisited(currentArea, true);
      CMapWorldInfo* mapInfo = mMapWorldInfo.GetPtr();
      CWorld* world = mWorld.get();
      CMapWorld* mapWorld = world->GetMapWorld();
      mapWorld->RecalculateWorldSphere(*mapInfo, *world);
    }
  }
}

void CStateManager::FrameEnd() {
  CModel::FrameDone();
  gpSimplePool->Flush();
}

bool CStateManager::GetVisSetForArea(const TAreaId area, const TAreaId visibleArea,
                                     CPVSVisSet& visibility) const {
  if (visibleArea == kInvalidAreaId) {
    return false;
  }

  CVector3f closestDockPoint = CGraphics::GetViewMatrix().GetTranslation();
  const CVector3f viewPoint = closestDockPoint;
  bool hasClosestDock = false;
  if (area == visibleArea) {
    hasClosestDock = true;
  } else {
    const CGameArea* visArea = mWorld->GetArea(visibleArea);
    if (visArea->IsLoaded()) {
      const int dockCount = visArea->GetDockCount();
      for (int i = 0; i < dockCount; ++i) {
        const IGameArea::Dock& dock = visArea->GetDock(i);
        const int connectionCount = dock.GetDockRefs().size();
        for (int connection = 0; connection < connectionCount; ++connection) {
          if (dock.GetConnectedAreaId(connection) != area) {
            continue;
          }

          const rstl::reserved_vector< CVector3f, 4 >& vertices = dock.GetPlaneVertices();
          const CVector3f center = 0.25f * (vertices[0] + vertices[1] + vertices[2] + vertices[3]);
          if (hasClosestDock &&
              !((center - viewPoint).MagSquared() < (closestDockPoint - viewPoint).MagSquared())) {
            continue;
          }

          closestDockPoint = center;
          hasClosestDock = true;
        }
      }
    }
  }

  int setState = 0;
  if (hasClosestDock) {
    setState = 1;
    const CGameArea* targetArea = mWorld->GetArea(area);
    const CPVSAreaSet* areaSet = targetArea->GetPostConstructed()->mPvs.get();
    if (areaSet != nullptr) {
      setState = 2;
      CPVSVisOctree& octree = areaSet->GetVisOctree();
      const CTransform4f& inverseTransform =
          mWorld->GetArea(area)->GetPostConstructed()->mInverseTransform;
      const CVector3f localPoint = inverseTransform * closestDockPoint;
      CPVSVisSet set = octree.GetVisSet(localPoint);
      if (set.GetState() == kVSS_NodeFound) {
        setState = 3;
        visibility = set;
      }
    }
  }

#ifdef TARGET_PC
  {
    // Port diagnostic (MP2_DEBUG_VIS=1): a vis set the octree could not fill leaves the area's
    // geometry culled, which looks like a blank screen with the HUD drawn on it - the state that
    // made a boot-time `MP2_TELEPORT` render nothing (docs/STATUS.md). 0/1/2 mean no vis set at all
    // (no dock found, or the area has no PVS), 3 is a real node.
    static const bool sLogVis = std::getenv("MP2_DEBUG_VIS") != nullptr;
    static int sLastArea = -1;
    static int sLastState = -1;
    if (sLogVis && (sLastArea != area.Value() || sLastState != setState)) {
      sLastArea = area.Value();
      sLastState = setState;
      std::fprintf(stderr, "[vis] area %d from visible area %d: setState %d\n", area.Value(),
                   visibleArea.Value(), setState);
    }
  }
#endif
  return setState == 3;
}

void CStateManager::Touch() {
  TouchSky();
  TouchPlayerActor();

  for (uint i = 0; i < mNumPlayers; ++i) {
    SetupPlayerViewport(i);
    const CPlayer* const player = mPlayers[i];
    bool touchModel = false;
    bool touchBall = false;
    bool touchGun = false;
    switch (player->GetMorphballTransitionState()) {
    case CPlayer::kMS_Unmorphed:
      touchGun = true;
      break;
    case CPlayer::kMS_Morphed:
      touchBall = true;
      break;
    case CPlayer::kMS_Morphing:
      touchBall = true;
      touchModel = true;
      break;
    case CPlayer::kMS_Unmorphing:
      touchGun = true;
      touchModel = true;
      break;
    default:
      break;
    }

    if (touchGun) {
      player->GetPlayerGun()->TouchModel(*this);
    }
    if (touchModel) {
      player->GetModelData()->Touch(*this, 0);
    }
    if (touchBall) {
      player->GetMorphBall()->TouchModel(*this);
    }
  }

  EndPlayerRender();
}

#ifdef TARGET_PC
namespace {
float sPortWorldTimeDt = 0.f;
uint sPortWorldTimeGeneration = 0;

// Port: material UV animations and world shaders read the world clock (1 - t) of a tick behind
// the last tick between ticks (smooth frames), like the fluid and GUI clocks.
float PortPresentedWorldTime(float time) {
  const float t = CCameraManager::GetPresentationInterpolation();
  if (t < 0.f || t >= 1.f || sPortWorldTimeGeneration != CActor::PortTickGeneration() ||
      !PortOverlay::ParticleInterpolation())
    return time;
  time -= (1.f - t) * sPortWorldTimeDt;
  return time < 0.f ? time + 900.f : time;
}
} // namespace
#endif

void CStateManager::PreRender(uint playerIndex) {
  #ifdef TARGET_PC
  // A named local: CTimeProvider keeps a `const float&`, so a temporary would dangle once this
  // declaration ends and every material UV animation would read a dead stack slot (issue #21).
  const float presentedTime = PortPresentedWorldTime(mCurTimeMod900);
  CTimeProvider timeProvider(presentedTime);
#else
  CTimeProvider timeProvider(mCurTimeMod900);
#endif
  SetupPlayerViewport(playerIndex);
  if (!mReadyToRender) {
    return;
  }

  CStopwatch timer;
  switch (mPlayerState->GetActiveVisor(*this)) {
  case CPlayerState::kPV_Combat:
  case CPlayerState::kPV_Scan:
    mRenderVisorMode = kRVM_Normal;
    break;
  case CPlayerState::kPV_Echo:
    mRenderVisorMode = kRVM_Echo;
    break;
  case CPlayerState::kPV_Dark:
    mRenderVisorMode = kRVM_Dark;
    break;
  default:
    break;
  }

  mStateManagerContainer->mRenderBeforeAreas.clear();
  mStateManagerContainer->mRenderFirstSorted.clear();
  mStateManagerContainer->mRenderLast.clear();
  mStateManagerContainer->mRenderLastUnderGun.clear();
  mStateManagerContainer->mRenderLastAfterCameraFilters.clear();
  mProjectedShadows = nullptr;

  mWorld->PreRender();
  BuildDynamicLightListForWorld();
  const CGameCamera* camera = mCameraManager->GetCurrentCamera(*this, true);
  const CTransform4f cameraTransform = mCameraManager->GetCurrentCameraTransform(*this, true);
  CFrustumPlanes frustum(cameraTransform, CRelAngle::FromDegrees(camera->GetFov()).AsRadians(),
                         camera->GetAspectRatio(), camera->GetNearClipDistance(), false, 100.f);
  mPlanes = frustum;
  SetupAreaFrusta();

  for (CGameArea::CChainIterator it = GetWorld()->ChainHead(CWorld::kC_Alive);
       it != CWorld::AliveAreasEnd(); ++it) {
    CGameArea& area = *it;
    if (area.GetOcclusionState() != CGameArea::kOS_Visible) {
      continue;
    }
    CObjectList* const objects = area.GetPostConstructed()->mAreaObjectList.get();
    CObjectList* const visibleActors = area.GetPostConstructed()->mVisibleActorList.get();
    if (playerIndex == 0u) {
      visibleActors->Clear();
      CScopedProfiler profile(rstl::string_l("*PreRender:PreRenderAllViewports"), true);
      for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
        CActor* actor = TCastToPtr< CActor >((*objects)[i]);
        if (actor != nullptr && actor->GetActive() && actor->GetDrawEnabled()) {
          visibleActors->AddObject(*actor);
          actor->PreRenderAllViewports(*this);
        }
      }
    }

    {
      CScopedProfiler profile(rstl::string_l("*PreRender:Portals"), true);
      if (CPortalArea* portal = area.GetPostConstructed()->mPortalArea.get()) {
        const CCameraManager* cameraManager = mCameraManager;
        portal->PreRender(*this, *cameraManager->GetCurrentCamera(*this, true),
                          cameraManager->GetCurrentCameraTransform(*this, true));
      }
    }
    {
      CScopedProfiler profile(rstl::string_l("*PreRender:Actors"), true);
      for (int i = visibleActors->GetFirstObjectIndex(); i != -1;
           i = visibleActors->GetNextObjectIndex(i)) {
        static_cast< CActor* >((*visibleActors)[i])->PreRender(*this);
      }
    }
  }

  if (!gkWorldOnlyReflection) {
    CacheReflection();
  }
  mCameraManagers[playerIndex]->UpdateFogState(*this);
  sPreRenderStepTime = timer.GetElapsedMicros();
}

bool CStateManager::SetupFogForDraw() const {
  switch (mPlayerState->GetActiveVisor(*this)) {
  case CPlayerState::kPV_Echo: {
    const CTweakGui* tweak = gpTweakGui.get();
    gpRender->SetWorldFog(tweak->GetEchoFogMode(), tweak->GetEchoFogNearZ(),
                          tweak->GetEchoFogFarZ(), CColor::White());
    return true;
  }
  default:
    return false;
  case CPlayerState::kPV_Combat:
  case CPlayerState::kPV_Scan:
  case CPlayerState::kPV_Dark: {
    const CGameArea::CAreaFog* fog = &mCameraManager->GetFog();
    if (fog->IsFogDisabled()) {
      return false;
    }
    fog->SetCurrent();
    return true;
  }
  }
}

void CStateManager::SetupFogForArea(const CGameArea& area) const {
  if (!SetupFogForDraw()) {
    area.GetAreaFog()->SetCurrent();
  }
}

void CStateManager::SetParticleAlphaUpdate(bool disable) {
  CDecal::SetDisableAlphaUpdate(disable);
  CElementGen::sMoveRedToAlphaBuffer = disable;
  CProjectileWeapon::SetDisableAlphaUpdates(disable);
}

void CStateManager::SetAreaClipPlane(TAreaId area, const CPlane& plane) {
  rstl::reserved_vector< rstl::pair< int, CFrustumPlanes >, 10 >::iterator it = mAreaFrusta.begin();
  for (; it != mAreaFrusta.end(); ++it) {
    if (it->first == area.Value()) {
      CFrustumPlanes& frustum = it->second;
      if (frustum.GetPlanes().size() < 6u) {
        frustum.AddPlane(plane);
      }
      break;
    }
  }

  if (it == mAreaFrusta.end()) {
    CFrustumPlanes frustum = CFrustumPlanes();
    frustum.AddPlane(plane);
    mAreaFrusta.push_back(rstl::pair< int, CFrustumPlanes >(area.Value(), frustum));
  }
}

void CStateManager::SetupAreaFrusta() {
  mVisAreaId = GetVisAreaId();
  mAreaFrusta.clear();
  const CTransform4f cameraTransform = mCameraManager->GetCurrentCameraTransform(*this, true);

  for (CGameArea::CConstChainIterator it = GetWorld()->GetChainHead(CWorld::kC_Alive);
       it != CWorld::GetAliveAreasEnd(); ++it) {
    const CGameArea& area = *it;
    if (area.GetOcclusionState() != CGameArea::kOS_Visible || area.GetId() == mVisAreaId) {
      continue;
    }

    bool foundDock = false;
    const IGameArea::Dock* selectedDock = nullptr;
    for (int i = 0; i < area.GetDockCount(); ++i) {
      const IGameArea::Dock& dock = area.GetDock(i);
      if (dock.GetConnectedAreaId(dock.GetReferenceCount()) == mVisAreaId) {
        if (!foundDock) {
          selectedDock = &dock;
          foundDock = true;
        } else {
          foundDock = false;
          break;
        }
      }
    }
    if (!foundDock) {
      continue;
    }

    const CVector3f position = cameraTransform.GetTranslation();
    const CVector3f* vertices = selectedDock->GetPlaneVertices().data();
    const CVector3f normal = CVector3f::Cross(vertices[1] - vertices[0], vertices[2] - vertices[0]);
    const CVector3f delta = position - vertices[0];
    if (CVector3f::Dot(delta, normal) > 0.f) {
      CFrustumPlanes frustum;
      for (int i = 0; i < 4; ++i) {
        frustum.AddPlane(CPlane(position, vertices[i], vertices[(i + 1) % 4]));
      }
      mAreaFrusta.push_back(rstl::pair< int, CFrustumPlanes >(area.GetId().Value(), frustum));
    }
  }
}

CGameArea::CConstChainIterator CWorld::GetAliveAreasEnd() { return skGlobalEnd; }

void CStateManager::GatherVisibleAreas(TVisibleAreas& areas, TAreaVisibility& visibility) {
  for (CGameArea::CConstChainIterator it = mWorld->GetChainHead(CWorld::kC_Alive);
       it != CWorld::GetAliveAreasEnd() && areas.size() != areas.capacity(); ++it) {
    if (it->GetOcclusionState() == CGameArea::kOS_Visible) {
      areas.push_back(&*it);
    }
  }

  rstl::sort(areas.begin(), areas.end(),
             area_sorter(CGraphics::GetViewMatrix().GetForward(), mVisAreaId));

  for (TVisibleAreas::iterator it = areas.begin(); it != areas.end(); ++it) {
    CPVSVisSet set(kVSS_OutOfBounds);
    GetVisSetForArea((*it)->GetId(), mVisAreaId, set);
    visibility.push_back(set);
  }
}

void CStateManager::PrepareWorldRendering(const TVisibleAreas& areas,
                                          const TAreaVisibility& visibility) {
  rstl::reserved_vector< rstl::pair< int, const CPVSVisSet* >, 5 > pvsSets;
  for (int i = 0; i < areas.size(); ++i) {
    pvsSets.push_back(
        rstl::pair< int, const CPVSVisSet* >(areas[i]->GetId().Value(), &visibility[i]));
  }

  const rstl::reserved_vector< CSafeZoneManager::SZone, 64 >& zones = mSafeZoneManager->GetZones();
  rstl::reserved_vector< rstl::pair< int, float >, 64 > ambientLights;
  const int count = rstl::min_val(zones.capacity(), zones.size());
  for (int i = 0; i < count; ++i) {
    const TEditorId editorId = GetEditorIdForUniqueId(zones[i].mId);
    if (editorId != kInvalidEditorId) {
      ambientLights.push_back(MakeAmbientLightScale(editorId.value, zones[i].mScaleFactor));
    }
  }

  gpRender->PrepareWorldRendering(
      pvsSets.data(), pvsSets.size(), mPlanes, &mAreaFrusta, mDynamicLights,
      !ambientLights.empty() ? ambientLights.data() : nullptr, ambientLights.size());
}

void CStateManager::GetWorldGeometryMasks(uint& mask, uint& targetMask,
                                          CPlayerState::EPlayerVisor visor) {
  int bit = 1;
  if (visor == CPlayerState::kPV_Dark) {
    bit = 2;
  }
  mask = 1u << bit;
  targetMask = 0;
}

void CStateManager::RenderActorQueue(rstl::reserved_vector< TUniqueId, 20 >& queue,
                                     const rstl::string& profileName) {
  if (!queue.empty()) {
    CScopedProfiler profile(profileName, true);
    for (rstl::reserved_vector< TUniqueId, 20 >::const_iterator it = queue.begin();
         it != queue.end(); ++it) {
#ifdef TARGET_PC
      const CActor* queued = static_cast< const CActor* >(GetObjectById(*it));
      CPortActorRenderScope presented(*queued);
      queued->Render(*this);
#else
      static_cast< const CActor* >(GetObjectById(*it))->Render(*this);
#endif
    }
  }
}

void CStateManager::DrawUnsortedGeometry(const TVisibleAreas& areas, uint mask, uint targetMask,
                                         CPlayerState::EPlayerVisor visor) {
  CScopedProfiler profile(rstl::string_l("*UnsortedStaticGeom"), true);
  const CPlayer* const player = mCurrentRenderPlayer;
  const CPlayerTargeting* const targeting = player->GetTargeting();

  for (int i = areas.size() - 1; i >= 0; --i) {
    const CGameArea& area = *areas[i];
    const TAreaId id = area.GetId();
    SetupFogForArea(area);
    gpRender->SetWorldLightFadeLevel(area.GetPostConstructed()->mWorldLightingLevel);
    targeting->PrepareStaticGeometry(*this, id);

    switch (visor) {
    case CPlayerState::kPV_Echo:
      gpRender->DrawEchoVisorGeometry(
          player->GetEchoPulsePhase(), gpTweakGui->GetEchoBigRingScale(),
          gpTweakGui->GetEchoBigRingFadeStart(), gpTweakGui->GetEchoAuraSmallSize(),
          gpTweakGui->GetEchoAuraBigSize(), id.Value());
      break;
    case CPlayerState::kPV_Scan:
      gpRender->DrawUnsortedGeometryAlpha(id.Value());
      break;
    default:
      gpRender->DrawUnsortedGeometry(id.Value());
      break;
    }
  }
}

void CStateManager::DrawSky(const TVisibleAreas& areas, CPlayerState::EPlayerVisor visor) {
  mWorld->TouchSky();
  if (visor == CPlayerState::kPV_Echo) {
    return;
  }

  bool fogEnabled = false;
  CScopedProfiler profile(rstl::string_l("*Sky"), true);
  if (visor == CPlayerState::kPV_Scan) {
    gpRender->SetDestinationAlpha(0);
  }
  if (mCameraManager->IsFogEnabled()) {
    gpRender->SetWorldFog(kRFM_PerspLin, 0.f, 0.05f, mCameraManager->GetFog().GetColor());
    fogEnabled = true;
  } else {
    gpRender->SetWorldFog(kRFM_None, 0.f, 1.f, CColor::Black());
  }
  mWorld->DrawSky(CTransform4f::Translate(CGraphics::GetViewMatrix().GetTranslation()), fogEnabled);
  if (!areas.empty()) {
    SetupFogForArea(*areas[areas.size() - 1]);
  }
}

void CStateManager::RenderAreaActors(bool& deferPlayerRender, CEchoEmitter*& emitters,
                                     const CGameArea& area, const CPVSVisSet& visibility) {
  CScopedProfiler profile(rstl::string_l("*AllActorRender"), true);
  const CObjectList* const actors = area.GetPostConstructed()->mVisibleActorList.get();
  CPlayer* const player = mCurrentRenderPlayer;
  for (int index = actors->GetFirstObjectIndex(); index != -1;
       index = actors->GetNextObjectIndex(index)) {
    const CActor* actor = static_cast< const CActor* >((*actors)[index]);
    if (actor->GetPvsIndex() != -1 &&
        visibility.GetVisible(actor->GetPvsIndex()) == kVSS_EndOfTree) {
      continue;
    }
    if (actor == player) {
      if (player->GetMorphballTransitionState() == CPlayer::kMS_Morphed ||
          player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed) {
        CScopedProfiler playerProfile(rstl::string_l("*1stPlayerRender"), true);
        player->AddToRenderer(*this);
      } else {
        deferPlayerRender = true;
      }
    } else {
#ifdef TARGET_PC
      {
        CPortActorRenderScope presented(*actor);
        actor->AddToRenderer(*this);
      }
#else
      actor->AddToRenderer(*this);
#endif
      if (CEchoEmitter* emitter = actor->EchoEmitter()) {
        emitter->SetNextEmitter(emitters);
        emitters = emitter;
      }
    }
  }
}

void CStateManager::DrawSpecialGeometry(TAreaId area, CPlayerState::EPlayerVisor visor, uint,
                                        uint) {
  switch (visor) {
  case CPlayerState::kPV_Echo:
    break;
  case CPlayerState::kPV_Scan:
    gpRender->DrawSpecialGeometryAlpha(area.Value());
    break;
  default:
    gpRender->DrawSpecialGeometry(area.Value());
    break;
  }
}

void CStateManager::DrawDarkWorldEffects(CPlayerState::EPlayerVisor visor) {
  if (mIsDarkWorld) {
    switch (visor) {
    case CPlayerState::kPV_Dark:
      mSafeZoneManager->Render(*this);
      break;
    case CPlayerState::kPV_Combat:
    case CPlayerState::kPV_Scan:
      mSafeZoneManager->Render(*this);
      gpRender->DrawDarkWorldFilter(mSafeZoneManager->GetDarkWorldFilterAmount(
          mCameraManager->GetCurrentCamera(*this, true)->GetTransform()));
      break;
    default:
      break;
    }
  }
}

void CStateManager::DrawDarkVisor(const CInGameGuiManagerSet& gui) {
  RenderForgottenObjects();
  const uchar alpha =
      CCast::ToUint8(CMath::Clamp(0.f, 255.f * gpTweakGui->GetDarkVisorBlurSpeed(), 255.f));
  int x;
  int y;
  int width;
  int height;
  if (IsMultiplayer()) {
    const CViewport& viewport = CGraphics::GetViewport();
    x = viewport.mLeft;
    y = viewport.mTop;
    width = viewport.mWidth;
    height = viewport.mHeight;
    y = 0;
    x = 0;
  } else {
    gpTweakGui->GetDarkVisorFrame(&x, &y, &width, &height);
  }
  gui.GetPlayerGuiManager(mCurrentRenderPlayerIndex).DrawDarkVisorMask(*this);
  const CPlayer* player = mCurrentRenderPlayer;
  gpRender->DrawDarkWorldTransition(
      gpTweakGui->GetDarkVisorStaticColor(), gpTweakGui->GetDarkWorldBaseColor(),
      gpTweakGui->GetDarkVisorPaletteModulate(), CColor(uchar(0), uchar(0), uchar(0), alpha),
      player->GetScreenPosition(), CVector2i(x, y), CVector2i(width, height));
}

void CStateManager::DrawDarkWorldCloud(CPlayerState::EPlayerVisor visor) {
  if (mDarkWorldCloud.mTime > 0.f) {
    switch (visor) {
    case CPlayerState::kPV_Combat:
    case CPlayerState::kPV_Scan:
      gpRender->DrawDarkWorldCloud(mDarkWorldCloud.mTime, mDarkWorldCloud.mScale,
                                   mDarkWorldCloud.mColor);
      break;
    default:
      break;
    }
  }
}

void CStateManager::RenderEchoEmitters(const CEchoEmitter* emitters) const {
  CEchoEmitter::RenderEmitters(*this, emitters);
}

void CStateManager::SetupViewForDraw(const CViewport& viewport) {
  const CPlayer* player = mCurrentRenderPlayer;
  const CGameCamera* camera = mCameraManager->GetCurrentCamera(*this, true);
#ifdef TARGET_PC
  // Port: the free camera draws the world from a viewpoint of its own.
  const CTransform4f cameraTransform =
      PortFreeCam::View(mCameraManager->GetCurrentCameraTransform(*this, true));
#else
  const CTransform4f cameraTransform = mCameraManager->GetCurrentCameraTransform(*this, true);
#endif
  gpRender->SetWorldViewpoint(cameraTransform);
  CCubeModel::SetNewPlayerPositionAndTime(player->GetTranslation(),
                                          CStopwatch::GetGlobalTimerObj());

  float widthScale = player->GetViewportScaleX();
  float heightScale = player->GetViewportScaleY();
  for (int i = 0; i < 11; ++i) {
    const CCameraFilterPass& filter = mCameraFilterPasses[mCurrentRenderPlayerIndex][i];
    const float filterWidth = filter.GetWidthScale();
    if (filterWidth < widthScale) {
      widthScale = filterWidth;
    }
    const float filterHeight = filter.GetHeightScale();
    if (filterHeight < heightScale) {
      heightScale = filterHeight;
    }
  }

  const float scaledWidth = widthScale * static_cast< float >(viewport.mWidth);
  const float scaledHeight = heightScale * static_cast< float >(viewport.mHeight);
  const int width = static_cast< int >(scaledWidth);
  const int height = (static_cast< int >(scaledHeight) / 2) * 2;
  const int left = viewport.mLeft + (viewport.mWidth - width) / 2;
  const bool splitScreen = IsMultiplayer() && !mCameraManagers[0]->IsInFullScreenCinematic();
  const float topScale = splitScreen ? 0.25f : 0.5f;
  const int top = viewport.mTop + static_cast< int >(topScale * (viewport.mHeight - height));
  const float aspect = (widthScale * camera->GetAspectRatio()) / heightScale;
#ifdef TARGET_PC
  // Port: photo mode's field of view (0 keeps the camera's own).
  const float fovDegrees =
      PortFreeCam::FovOverride() > 0.f ? PortFreeCam::FovOverride() : camera->GetRenderFov();
#endif
#ifdef TARGET_PC
  const float tangent = static_cast< float >(tan(CMath::Deg2Rad(0.5f * fovDegrees)));
#else
  const float tangent = static_cast< float >(tan(CMath::Deg2Rad(0.5f * camera->GetFov())));
#endif
  const float fieldOfView = 2.f * static_cast< float >(atan(tangent * heightScale));

  gpRender->SetViewport(left, top, width, height);
  CGraphics::SetDepthRange(0.125f, 1.f);
  gpRender->SetPerspective(360.f * CMath::Rad2Rev(fieldOfView), scaledWidth, scaledHeight,
                           camera->GetNearClipDistance(), camera->GetFarClipDistance());
  mPlanes = CFrustumPlanes(cameraTransform, fieldOfView, aspect, camera->GetNearClipDistance(),
                           false, 100.f);
  gpRender->PrimColor(CColor::White());
  gpRender->SetModelMatrix(CTransform4f::Identity());
  mFluidPlaneManager->StartFrame(false);
  gpRender->SetDebugOption(IRenderer::kDO_PVSState, 1);
}

void CStateManager::DrawDebugStuff() const {}

void CStateManager::DrawWorld(const CInGameGuiManagerSet& gui) {
  CScopedProfiler profile(rstl::string_l("*TotalDrawWorld"), true);
  SetRendererWorkspace(alloca(GetRendererWorkspaceSize()));

  const CPlayerState::EPlayerVisor visor = mPlayerState->GetActiveVisor(*this);
  #ifdef TARGET_PC
  // Named for the reason PreRender gives: CTimeProvider holds a reference (issue #21).
  const float presentedTime = PortPresentedWorldTime(mCurTimeMod900);
  CTimeProvider timeProvider(presentedTime);
#else
  CTimeProvider timeProvider(mCurTimeMod900);
#endif
  CViewport viewport = CGraphics::GetViewport();
  viewport.mTop = CGraphics::GetViewportTop(viewport.mTop);
  SetupViewForDraw(viewport);
  const CTransform4f viewMatrix(CGraphics::GetViewMatrix());

  TVisibleAreas areas;
  TAreaVisibility visibility;
  GatherVisibleAreas(areas, visibility);
  CPlayer* const player = mCurrentRenderPlayer;
  PrepareWorldRendering(areas, visibility);
  uint mask = 0;
  uint targetMask = 0;
  GetWorldGeometryMasks(mask, targetMask, visor);
  gpRender->SetRequestedMaterialMode(visor == CPlayerState::kPV_Dark ? 1 : 0);
  SetupParticleDrawMask();
  SetParticleAlphaUpdate(visor == CPlayerState::kPV_Echo);

#ifdef TARGET_PC
  // F1 > Debug > Rendering > Collision "Only": nothing of the world is drawn but Samus and the
  // collision view (port_collision_view.h).
  const bool portCollisionOnly = PortCollisionView::Only();
  if (!portCollisionOnly) {
#endif
  RenderActorQueue(mStateManagerContainer->mRenderBeforeAreas,
                   rstl::string_l("*RenderBeforeAreas"));
  DrawUnsortedGeometry(areas, mask, targetMask, visor);
  DrawSky(areas, visor);
  RenderActorQueue(mStateManagerContainer->mRenderFirstSorted,
                   rstl::string_l("*renderFirstSorted"));
#ifdef TARGET_PC
  }
#endif

  bool deferPlayerRender = false;
  CEchoEmitter* emitters = nullptr;
  for (int i = 0; i < areas.size(); ++i) {
    const CGameArea& area = *areas[i];
    SetupFogForArea(area);
    gpRender->SetWorldLightFadeLevel(area.GetPostConstructed()->mWorldLightingLevel);
#ifdef TARGET_PC
    if (portCollisionOnly) {
      continue;
    }
#endif
    RenderAreaActors(deferPlayerRender, emitters, area, visibility[i]);
    DrawSpecialGeometry(area.GetId(), visor, mask, targetMask);
    ++mObjectDrawToken;

    if (area.GetId() == mVisAreaId) {
      CScopedProfiler decalProfile(rstl::string_l("*Decal+ActorParticleStragglers"), true);
      CDecalManager::AddToRenderer(*this);
      mActorModelParticles->AddStragglersToRenderer(*this);
      if (mProjectedShadows) {
        CScopedProfiler shadowProfile(rstl::string_l("*Projected"), true);
        for (const CProjectedShadow* shadow = mProjectedShadows; shadow;
             shadow = shadow->GetNextShadow()) {
          shadow->Render(*this);
        }
      }
    }
    if (area.GetId() == mCurrentRenderPlayer->GetCurrentAreaId()) {
      CScopedProfiler shadowProfile(rstl::string_l("*IDBasedShadows"), true);
      player->GetMorphBall()->DrawBallShadow(*this);
    }

    CScopedProfiler sortedProfile(rstl::string_l("*AllSortedGeometry"), true);
    const int mode = visor == CPlayerState::kPV_Scan ? 1 : 0;
    if (visor != CPlayerState::kPV_Echo) {
      gpRender->DrawSortedGeometry(mode, area.GetId().Value());
    } else {
      gpRender->DrawSortedGeometry(mode, -2);
    }
  }

#ifdef TARGET_PC
  if (!portCollisionOnly)
#endif
  {
    CScopedProfiler envProfile(rstl::string_l("*EnvFxManager"), true);
    mEnvFxManager->Render(*this);
  }
#ifdef TARGET_PC
  if (portCollisionOnly) {
    // The areas' actors are skipped, so Samus is drawn on her own, over the collision view.
    CPortActorRenderScope presented(*player);
    CPortPipelineContextScope globalPipelines(AURORA_PIPELINE_CONTEXT_GLOBAL);
    player->Render(*this);
  }
  PortCollisionView::Draw(*this, areas.begin(), static_cast< int >(areas.size()));
#endif
  if (deferPlayerRender) {
    CScopedProfiler playerProfile(rstl::string_l("*2ndPlayerRender"), true);
#ifdef TARGET_PC
    CPortActorRenderScope presented(*player);
    CPortPipelineContextScope globalPipelines(AURORA_PIPELINE_CONTEXT_GLOBAL);
#endif
    player->Render(*this);
  }
  if (visor == CPlayerState::kPV_Scan) {
    CScopedProfiler visorProfile(rstl::string_l("*GameVisor"), true);
    CapturePlayerTextures();
    mCurrentRenderPlayer->GetTargeting()->Draw(*this, gui);
  }
  {
    CScopedProfiler fogProfile(rstl::string_l("*PostRenderFogs"), true);
    gpRender->PostRenderFogs();
  }
  DrawDarkWorldEffects(visor);
  {
    CScopedProfiler fluidProfile(rstl::string_l("*FluidPlaneManagerEndFrame"), true);
    mFluidPlaneManager->EndFrame();
    gpRender->SetWorldFog(kRFM_None, 0.f, 1.f, CColor::Black());
  }
  {
    CScopedProfiler gunProfile(rstl::string_l("*PlayerGun"), true);
#ifdef TARGET_PC
    CPortPipelineContextScope globalPipelines(AURORA_PIPELINE_CONTEXT_GLOBAL);
    // Port (MP1 5fb236a91): the arm cannon is drawn at retail's FOV whatever the FOV setting, like
    // a view-model FOV in other shooters; otherwise a wide view shrinks it and pushes it into the
    // corner. Morph ball and scripted cameras keep their own FOVs.
    const CGameCamera& gunCam = *mCameraManager->GetCurrentCamera(*this, true);
    const bool gunFov = mCameraManager->IsInFPCamera() &&
                        gunCam.GetFov() != CCameraManager::sFirstPersonFOV;
    const CGraphics::CProjectionState gunProj = CGraphics::GetProjectionState();
    if (gunFov) {
      const CViewport& gunViewport = CGraphics::GetViewport();
      gpRender->SetPerspective(CCameraManager::sFirstPersonFOV,
                               static_cast< float >(gunViewport.mWidth),
                               static_cast< float >(gunViewport.mHeight),
                               gunCam.GetNearClipDistance(), gunCam.GetFarClipDistance());
    }
#endif
    player->RenderGun(*this, mCameraManager->GetGlobalCameraTranslation(*this, true));
#ifdef TARGET_PC
    if (gunFov) {
      CGraphics::SetProjectionState(gunProj);
    }
#endif
  }
  RenderActorQueue(mStateManagerContainer->mRenderLastUnderGun,
                   rstl::string_l("*RenderLastUnderGun"));
  DrawDarkWorldCloud(visor);
  if (!mStateManagerContainer->mRenderLast.empty()) {
    CGraphics::SetDepthRange(4.f / 256.f, 8.f / 256.f);
    RenderActorQueue(mStateManagerContainer->mRenderLast, rstl::string_l("*RenderLast"));
    CGraphics::SetDepthRange(0.125f, 1.f);
  }
  if (gkWorldOnlyReflection) {
    CScopedProfiler reflectionProfile(rstl::string_l("*PlayerReflection"), true);
    CacheReflection();
  }

  SetParticleAlphaUpdate(false);
  CParticleGen::sDrawFlags = 0;
  CParticleGen::sDrawMask = 0;
  gpRender->SetRequestedMaterialMode(0);
  switch (visor) {
  case CPlayerState::kPV_Dark:
    RenderForgottenObjects();
    DrawDarkVisor(gui);
    break;
  case CPlayerState::kPV_Echo:
    gpRender->DrawScreenFilter(gpTweakGui->GetEchoBaseColor(), gpTweakGui->GetEchoOutlineColor(),
                               gpTweakGui->GetEchoRingColor());
    RenderEchoEmitters(emitters);
    break;
  default:
    break;
  }

  ResetViewAfterDraw(viewport, viewMatrix);
  {
    CScopedProfiler filterProfile(rstl::string_l("*DrawAdditionalFilters"), true);
    DrawAdditionalFilters();
  }
  RenderActorQueue(mStateManagerContainer->mRenderLastAfterCameraFilters,
                   rstl::string_l("*RenderLastAfterCameraFilters"));
  ReleaseRendererWorkspace();
}

void CStateManager::ResetViewAfterDraw(const CViewport& viewport, const CTransform4f& viewMatrix) {
  gpRender->SetViewport(viewport.mLeft, viewport.mTop, viewport.mWidth, viewport.mHeight);
  const CGameCamera* camera = mCameraManager->GetCurrentCamera(*this, true);
  CFrustumPlanes frustum(viewMatrix, CRelAngle::FromDegrees(camera->GetFov()).AsRadians(),
                         camera->GetAspectRatio(), camera->GetNearClipDistance(), false, 100.f);
  mPlanes = frustum;

  const float height = CGraphics::GetViewport().mHeight;
  const float width = CGraphics::GetViewport().mWidth;
#ifdef TARGET_PC
  gpRender->SetPerspective(camera->GetRenderFov(), width, height, camera->GetNearClipDistance(),
                           camera->GetFarClipDistance());
#else
  gpRender->SetPerspective(camera->GetFov(), width, height, camera->GetNearClipDistance(),
                           camera->GetFarClipDistance());
#endif
}

void CStateManager::DrawAdditionalFilters() {
  for (int i = 0; i < 11; ++i) {
    mCameraBlurPasses[mCurrentRenderPlayerIndex][i].Draw();
    mCameraFilterPasses[mCurrentRenderPlayerIndex][i].Draw();
  }

  if (gpGameState->GetEscapeTime() < 1.f && gpGameState->GetEscapeTime() > 0.f &&
      !mCameraManager->IsInCinematicCamera()) {
    const float escapeTime = gpGameState->GetEscapeTime();
    const CColor color = CColor::White().WithAlphaOf(1.f - escapeTime);
    CCameraFilterPass::DrawFilter(CCameraFilterPass::kFT_Add, CCameraFilterPass::kFS_Fullscreen,
                                  color, nullptr, 1.f);
  }
}

void CStateManager::ReflectionDrawer(void* context, const CVector3f& point) {
  CStateManager* manager = static_cast< CStateManager* >(context);
  manager->DrawReflection(point);
}

void CStateManager::CacheReflection() {
  if (mRenderVisorMode == kRVM_Normal && !IsMultiplayer()) {
    gpRender->CacheReflection(ReflectionDrawer, this, !gkWorldOnlyReflection);
  }
}

void CStateManager::DrawReflection(const CVector3f& point) {
  CPlayer* player = mPlayers[mCurrentRenderPlayerIndex];
  CAABox playerBounds = player->GetBoundingBox();
  CVector3f playerPosition = playerBounds.GetCenterPoint();
  const CVector3f viewPosition =
      playerPosition - 3.5f * CVector3f(playerPosition.GetX() - point.GetX(),
                                        playerPosition.GetY() - point.GetY(),
                                        playerPosition.GetZ() - playerPosition.GetZ())
                                  .AsNormalized();
  CTransform4f reflectionTransform =
      CTransform4f::LookAt(viewPosition, playerPosition, CVector3f(0.f, 0.f, -1.f));
  const CTransform4f backupView = CGraphics::GetViewMatrix();
  CGraphics::SetViewPointMatrix(reflectionTransform);

  const CGameCamera& camera = *mCameraManager->GetCurrentCamera(*this, true);
  const CViewport& viewport = CGraphics::GetViewport();
  const float height = static_cast< float >(viewport.mHeight);
  const float width = static_cast< float >(viewport.mWidth);
  const CGraphics::CProjectionState backupProjection = CGraphics::GetProjectionState();
#ifdef TARGET_PC
  gpRender->SetPerspective(camera.GetRenderFov(), width, height, camera.GetNearClipDistance(),
                           camera.GetFarClipDistance());
#else
  gpRender->SetPerspective(camera.GetFov(), width, height, camera.GetNearClipDistance(),
                           camera.GetFarClipDistance());
#endif
  player->RenderReflectedPlayer(*this);

  CGraphics::SetViewPointMatrix(backupView);
  CGraphics::SetProjectionState(backupProjection);
}

void CStateManager::DrawSpaceWarp(const CVector3f& position, float strength) const {
  switch (mPlayerState->GetActiveVisor(*this)) {
  case CPlayerState::kPV_Echo:
  case CPlayerState::kPV_Scan:
    return;
  default:
    break;
  }

  const CGameCamera* camera = mCameraManager->GetCurrentCamera(*this, true);
#ifdef TARGET_PC
  // The scene is drawn from the presented camera: carry the point into the sim camera's frame as
  // the presented one sees it, so the warp stays on it between ticks. Identity when not blended.
  const CVector3f simPosition =
      mCameraManager->GetSimulationCameraTransform(*this, true) *
      (mCameraManager->GetCurrentCameraTransform(*this, true).GetQuickInverse() * position);
  gpRender->DrawSpaceWarp(camera->ConvertToScreenSpace(simPosition), strength);
#else
  gpRender->DrawSpaceWarp(camera->ConvertToScreenSpace(position), strength);
#endif
}

void CStateManager::TouchSky() { GetWorld()->TouchSky(); }

void CStateManager::TouchPlayerActor() {
  if (mPlayerActorHead != kInvalidUniqueId) {
    const CEntity* entity = GetObjectById(mPlayerActorHead);
    if (entity != nullptr) {
      PlayerActor_TouchModels(*const_cast< CEntity* >(entity), *this);
    }
  }
}

// Guessed local names. The target excludes these materials when testing dock visibility.
static EMaterialTypes VisAreaExcludeMaterial1 = kMT_ProjectilePassthrough;
static EMaterialTypes VisAreaExcludeMaterial2 = kMT_CameraPassthrough;
static EMaterialTypes VisAreaIncludeMaterial = kMT_Solid;

TAreaId CStateManager::GetVisAreaId() const {
  const TAreaId currentArea = GetWorld()->GetCurrentAreaId();
  if (IsMultiplayer()) {
    return currentArea;
  }

  const CWorld* world = GetWorld();
  int visibleAreas = 0;
  for (CGameArea::CConstChainIterator it = world->GetChainHead(CWorld::kC_Alive);
       it != CWorld::skGlobalEnd; ++it) {
    if (it->GetOcclusionState() == CGameArea::kOS_Visible) {
      ++visibleAreas;
    }
  }
  if (visibleAreas == 1) {
    return currentArea;
  }

  const CGameCamera* camera = mCameraManagers[0]->GetCurrentCamera(*this, true);
  bool checkDocks = false;
  if (mCameraManagers[0]->IsInBallCamera()) {
    checkDocks = true;
  }
  if (mCameraManagers[0]->GetHintManager()->HasHint(*this) &&
      mCameraManagers[0]->GetHintManager()->GetCurrentHint(*this)->GetAcrossAreas()) {
    checkDocks = true;
  }
  if (mCameraManagers[0]->IsInCinematicCamera()) {
    checkDocks = false;
  }
  if (!checkDocks) {
    return currentArea;
  }

  const CVector3f position = camera->GetTranslation();
  CAABox bounds(position, position);
  const rstl::optional_object< CAABox >& playerBounds = mPlayers[0]->GetTouchBounds();
  bounds.AccumulateBounds(playerBounds->GetMinPoint());
  bounds.AccumulateBounds(playerBounds->GetMaxPoint());

  const rstl::list< CEntity* >& docks = GetDockList();
  for (rstl::list< CEntity* >::const_iterator it = docks.begin(); it != docks.end(); ++it) {
    const CScriptDock* dock = static_cast< const CScriptDock* >(*it);
    if (dock == nullptr || dock->GetAreaId() != currentArea) {
      continue;
    }
    const CAABox dockBounds = dock->GetBoundingBox();
    if (!bounds.DoBoundsOverlap(dockBounds)) {
      continue;
    }
    const TAreaId connectedArea = dock->GetCurrentConnectedAreaId(*this);
    if (world->GetArea(connectedArea)->GetOcclusionState() != CGameArea::kOS_Visible ||
        !dock->HasPointCrossedDock(*this, position)) {
      continue;
    }

    const CGameArea& currentAreaObject = *world->GetArea(currentArea);
    const CVector3f delta = dockBounds.GetCenterPoint() - position;
    const float distance = delta.Magnitude();
    if (distance < FLT_EPSILON) {
      return connectedArea;
    }
    const CVector3f direction = (1.f / distance) * delta;
    const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
        CMaterialList(VisAreaIncludeMaterial),
        CMaterialList(VisAreaExcludeMaterial1, VisAreaExcludeMaterial2));
    if (CGameCollision::RayStaticLineOfSightTest(currentAreaObject, position, direction, distance,
                                                 filter)) {
      return connectedArea;
    }
  }
  return currentArea;
}

void CStateManager::SetActorAreaId(CActor& actor, const TAreaId area) {
  const int oldArea = actor.GetCurrentAreaId().Value();
  if (oldArea != area.Value()) {
    CWorld* world = mWorld.get();
    if (oldArea != kInvalidAreaId.Value()) {
      CGameArea* oldAreaObject = world->Area(TAreaId(oldArea));
      if (oldAreaObject->GetPhase() > CGameArea::kP_FinishScriptObjects) {
        oldAreaObject->ObjectList()->RemoveObject(actor.GetUniqueId());
        if (oldAreaObject->GetPostConstructed()->mPortalArea.get() != nullptr) {
          oldAreaObject->GetPostConstructed()->mPortalArea->RemoveActor(*this, actor.GetUniqueId());
        }
      }
    }

    actor.SetCurrentAreaId(area);
    if (area != kInvalidAreaId) {
      CGameArea* newAreaObject = world->Area(area);
      if (newAreaObject->IsLoaded()) {
        if (newAreaObject->GetObjectList()->GetObjectById(actor.GetUniqueId()) == nullptr) {
          newAreaObject->ObjectList()->AddObject(actor);
        }
      }
    }
  }
}

void CStateManager::SetupParticleHook(const CActor& actor) const {
  mActorModelParticles->SetupHook(actor.GetUniqueId());
}

void CStateManager::ResetEscapeSequenceTimer(float time) {
  gpGameState->SetEscapeTime(time);
  mEscapeTotalTime = time;
}

float CStateManager::GetEscapeSequenceTimer() const { return gpGameState->GetEscapeTime(); }

void CStateManager::UpdateEscapeSequenceTimer(float dt) {
  if (close_enough(mEscapeTotalTime, 0.f)) {
    mEscapeTotalTime = gpGameState->GetEscapeTime();
  }
  const float totalTime = mEscapeTotalTime;
  if (gpGameState->GetEscapeTime() > 0.f) {
    gpGameState->SetEscapeTime(rstl::max_val(FLT_EPSILON, gpGameState->GetEscapeTime() - dt));
    if (gpGameState->GetEscapeTime() <= FLT_EPSILON && mPlayerStates[0]->IsPlayerAlive()) {
      KillPlayer(0.f, mPlayers[0]->GetUniqueId(), kInvalidUniqueId);
    }

    if (!sEscapeRumbleInitialized) {
      sEscapeRumbleInitialized = true;
      sNextEscapeRumble = 0.f;
    }
    sNextEscapeRumble -= dt;
    if (sNextEscapeRumble < 0.f) {
      const float factor = 1.f - gpGameState->GetEscapeTime() / totalTime;
      mRumbleManagers[0]->Rumble(*this, kRFX_PlayerBump, 0.75f, kRP_One);
      sNextEscapeRumble = -12.f * (factor * factor) + 15.f;
    }
  }
}

void CStateManager::UpdateHintState(float dt) {
  CHintOptions& hintOptions = gpGameState->HintOptions();
  hintOptions.Update(dt, *this);

  int nextHintIdx = -1;
  int hintPeriods = -1;
  const CHintOptions::SHintState* currentHint = hintOptions.GetCurrentDisplayedHint();
  if (currentHint != nullptr) {
    const CGameHintInfo::CGameHint& nextHint =
        gpMemoryCard->GetHints()[hintOptions.GetNextHintIdx()];
    const rstl::vector< CGameHintInfo::SHintLocation >& locations = nextHint.GetLocations();
    for (int i = 0; i < static_cast< int >(locations.size()); ++i) {
      const CGameHintInfo::SHintLocation& location = locations[i];
      const int areaId = location.mAreaId.Value();
      const CAssetId worldId = location.mMlvlId;
      CWorldState& worldState = gpGameState->StateForWorld(worldId);
      rstl::rc_ptr< CMapWorldInfo > mapWorldInfo = worldState.MapWorldInfo();
      mapWorldInfo->SetIsMapped(TAreaId(areaId), true);
    }

    if (currentHint->mTime < nextHint.GetTextTime()) {
      nextHintIdx = hintOptions.GetNextHintIdx();
      hintPeriods = static_cast< int >(currentHint->mTime / CGameHintInfo::skHintTextTime);
    }
  }

  if (nextHintIdx != mHintIdx || hintPeriods != static_cast< int >(mHintPeriods)) {
    if (nextHintIdx == -1) {
      CSamusHud::DisplayHudMemo(rstl::wstring_l(L""),
                                CHUDMemoParms(0.f, true, true, true, 15, true));
    } else {
      const CAssetId stringId = gpMemoryCard->GetHints()[nextHintIdx].GetStringId();
      CSamusHud::DeferHintMemo(stringId, hintPeriods,
                               CHUDMemoParms(0.f, true, false, true, 15, true));
    }

    mHintIdx = nextHintIdx;
    mHintPeriods = hintPeriods;
  }
}

void CStateManager::AddWeaponId(TUniqueId owner, EWeaponType type) {
  mWeaponMgr->IncrCount(owner, type);
}

void CStateManager::RemoveWeaponId(TUniqueId owner, EWeaponType type) {
  mWeaponMgr->DecrCount(owner, type);
}

int CStateManager::GetWeaponIdCount(TUniqueId owner, EWeaponType type) {
  return mWeaponMgr->GetNumActive(owner, type);
}

bool CStateManager::RenderLastHUD(const TUniqueId& uid) {
  CStateManagerContainer* container = mStateManagerContainer.get();
  if (container->mRenderLast.size() == container->mRenderLast.capacity()) {
    return false;
  }
  container->mRenderLast.push_back(uid);
  return true;
}

bool CStateManager::RenderLastAfterCameraFilters(TUniqueId uid) {
  CStateManagerContainer* container = mStateManagerContainer.get();
  if (container->mRenderLastAfterCameraFilters.size() ==
      container->mRenderLastAfterCameraFilters.capacity()) {
    return false;
  }
  container->mRenderLastAfterCameraFilters.push_back(uid);
  return true;
}

bool CStateManager::RenderLast(TUniqueId uid) {
  CStateManagerContainer* container = mStateManagerContainer.get();
  if (container->mRenderLastUnderGun.size() == container->mRenderLastUnderGun.capacity()) {
    return false;
  }
  container->mRenderLastUnderGun.push_back(uid);
  return true;
}

bool CStateManager::RenderLastOverlay(const TUniqueId& uid) {
  CStateManagerContainer* container = mStateManagerContainer.get();
  if (container->mRenderBeforeAreas.size() == container->mRenderBeforeAreas.capacity()) {
    return false;
  }
  container->mRenderBeforeAreas.push_back(uid);
  return true;
}

bool CStateManager::RenderFirstSorted(const TUniqueId& uid) {
  CStateManagerContainer* container = mStateManagerContainer.get();
  if (container->mRenderFirstSorted.size() == container->mRenderFirstSorted.capacity()) {
    return false;
  }
  container->mRenderFirstSorted.push_back(uid);
  return true;
}

int CStateManager::SpecialSkipCinematic() {
  int result = 0;
  if (mSpecialFunctionId != kInvalidUniqueId) {
    CEntity* entity = ObjectById(TUniqueId(mSpecialFunctionId));
    if (entity == nullptr) {
      SetSkipCinematicSpecialFunction(kInvalidUniqueId);
    } else if (CScriptSpecialFunction* special = TCastToPtr< CScriptSpecialFunction >(entity)) {
      const bool randomWasAvailable = IsRandomAvailable();
      mRandomAvailable = true;

      if (special->GetFunction() == CScriptSpecialFunction::kSF_CinematicSkip) {
        mCameraManagers[0]->StopCinematics(*this);
        result = 1;
      } else {
        result = 2;
      }
      special->SkipCinematic(*this);
      mRandomAvailable = randomWasAvailable;
    }
  }
  return result;
}

void CStateManager::DoCollisionResponse(const CCollisionResponseData& responseData,
                                        const CRayCastResult& rayCast, const TUniqueId& uid,
                                        const CWeaponMode& weaponMode, bool flag) {
  if (!rayCast.IsValid()) {
    return;
  }

  CTransform4f xf = CTransform4f::LookAt(CVector3f::Zero(), rayCast.GetPlane().GetNormal());
  xf.SetTranslation(rayCast.GetPoint());

  EWeaponCollisionResponseTypes type = kWCR_Default;
  int area = CSfxManager::kAllAreas;
  if (uid == kInvalidUniqueId) {
    type = CCollisionResponseData::GetWorldCollisionResponseType(
        CMaterialList::BitPosition(rayCast.GetMaterial().GetValue() & 0x00e3fffe));
  } else if (const CActor* actor = TCastToConstPtr< CActor >(GetObjectById(uid))) {
    area = actor->GetCurrentAreaId().Value();
    type = actor->GetCollisionResponseType(rayCast.GetPoint(), rayCast.GetPlane().GetNormal(),
                                           weaponMode, 0);
  }

  const rstl::optional_object< TLockedToken< CGenDescription > > particleDesc =
      responseData.GetParticleDescription(type);
  if (particleDesc.valid()) {
    CExplosion* explosion = rs_new CExplosion(
        *particleDesc, AllocateUniqueId(),
        CEntityInfo(kInvalidAreaId, CEntity::NullConnectionList, true, kInvalidEditorId),
        rstl::string_l("Proj col resp"), xf, flag ? 1u : 0u, CVector3f(1.f, 1.f, 1.f),
        CColor::White(), -1);
    AddObject(*explosion);
  }

  const int sfxId = responseData.GetSoundEffectId(type);
  if (sfxId >= 0) {
    const float range = responseData.GetAudibleRange();
    const float fallOff = responseData.GetAudibleFallOff();
    CAudioSys::C3DEmitterParmData parms(range, fallOff, 1, CAudioSys::kMaxVolume, 0x14);
    parms.mPos = rayCast.GetPoint();
    parms.mSfxId = sfxId;
    CSfxManager::AddEmitter(parms, area, true, false, CSfxManager::kMedPriority);
  }
}

#ifdef TARGET_PC
// Port: retail's CStateManager only has the CWeaponMode form above (DOL 0x800372C0). The Wii SEL
// build takes the damage info instead, and upstream declares that overload (CStateManager.hpp) when
// it decompiles the caller, so gun turrets call one nobody defines. Forward: the collision response
// only depends on the weapon mode; dealing the damage stays the caller's job (ApplyDamage).
void CStateManager::DoCollisionResponse(const CCollisionResponseData& data,
                                        const CRayCastResult& result, TUniqueId id,
                                        const CDamageInfo& damage, bool unknown) {
  DoCollisionResponse(data, result, id, damage.GetWeaponMode(), unknown);
}
#endif

void CStateManager::GetCharacterRenderMaskAndTarget(uint& mask, uint& target) const {
  const CPlayerState::EPlayerVisor visor = mPlayerState->GetActiveVisor(*this);
  uint renderMask = 0;
  uint renderTarget = 0;
  switch (visor) {
  case CPlayerState::kPV_Combat:
  case CPlayerState::kPV_Scan:
    renderMask = 0x1000;
    renderTarget = 0;
    break;
  case CPlayerState::kPV_Echo:
    renderMask = 0x800;
    renderTarget = 0;
    break;
  case CPlayerState::kPV_Dark:
    renderMask = 0x400;
    renderTarget = 0;
    break;
  }
  mask = renderMask;
  target = renderTarget;
}

void CStateManager::UpdatePlayerLineOfSight(float dt) {
  static const uchar skLastPairIndex[] = {0, 2, 5};
  static const uchar skPlayerPairs[][2] = {{0, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3}, {2, 3}};

  if (mNumPlayers < 2) {
    return;
  }

  if (mNextPlayerLineOfSightPair > skLastPairIndex[mNumPlayers - 2]) {
    mNextPlayerLineOfSightPair = 0;
  }

  const uchar first = skPlayerPairs[mNextPlayerLineOfSightPair][0];
  const uchar second = skPlayerPairs[mNextPlayerLineOfSightPair][1];
  const CVector3f start = mPlayers[first]->GetOrbitPosition(*this);
  const CVector3f end = mPlayers[second]->GetOrbitPosition(*this);
  CVector3f direction = end - start;
  bool visible = false;
  if (direction.CanBeNormalized()) {
    const float length = direction.Magnitude();
    direction *= 1.f / length;
    const CMaterialFilter filter = CMaterialFilter::MakeIncludeExclude(
        CMaterialList(kMT_Solid), CMaterialList(kMT_SeeThrough));
    if (CGameCollision::RayStaticLineOfSightTest(*this, start, direction, length, filter)) {
      visible = true;
    }
  }

  if (visible) {
    mPlayerLineOfSightPairs |= 1 << mNextPlayerLineOfSightPair;
  } else {
    mPlayerLineOfSightPairs &= ~(1 << mNextPlayerLineOfSightPair);
  }
  ++mNextPlayerLineOfSightPair;
}

bool CStateManager::CanEnterMapScreen() {
  const CHintOptions::SHintState* hint = gpGameState->HintOptions().GetCurrentDisplayedHint();
  if (hint != nullptr && !hint->CanContinue()) {
    return false;
  }
  if (TCastToConstPtr< CCinematicCamera >(*mCameraManagers[0]->GetCurrentCamera(*this, true)) !=
      nullptr) {
    return false;
  }
  return true;
}

void CStateManager::DeleteSaveGameScreen() {
  mInSaveUI = mSaveGameScreen->GetMessageReturn() == CIOWin::kMR_Exit;
  mSaveGameScreen = nullptr;
}

void CStateManager::CreateSaveGameScreen() {
#ifdef TARGET_PC
  // Only the FrontEnd screen calls this; kSC_InGame made A on a slot rebuild it from the FrontEnd game state
  mSaveGameScreen = rs_new CSaveGameScreen(kSC_FrontEnd, gpGameState->GetCardSerial());
#else
  mSaveGameScreen = rs_new CSaveGameScreen(kSC_InGame, gpGameState->GetCardSerial());
#endif
}

void CStateManager::SetGameState(EGameState state) {
  if (mGameState == state) {
    return;
  }

  if (mGameState == kGS_SoftPaused) {
    mWorld->SetLoadPauseState(false);
  }

  switch (state) {
  case kGS_Running:
    for (uint i = 0; i < mNumPlayers; ++i) {
      if (mRumbleManagers[i]->GetDisabled()) {
        mRumbleManagers[i]->SetDisabled(false);
      }
    }
    if (CSfxManager::GetChannel() == CSfxManager::kSC_SoftPaused) {
      CSfxManager::KillAll(CSfxManager::kSC_SoftPaused);
    }
    CSfxManager::SetChannel(CSfxManager::kSC_Game);
    break;
  case kGS_SoftPaused:
    for (uint i = 0; i < mNumPlayers; ++i) {
      if (!mRumbleManagers[i]->GetDisabled()) {
        mRumbleManagers[i]->SetDisabled(true);
      }
    }
    CSfxManager::SetChannel(CSfxManager::kSC_SoftPaused);
    mWorld->SetLoadPauseState(true);
    break;
  default:
    break;
  }

  mGameState = state;
}

void CStateManager::QueueMessage(int frameCount, CAssetId msg, float delay) {
  mPausedHudMemoFrameCount = frameCount;
  mPausedHudMemoAssetId = msg;
  mQueuedHudMemoDismissalDelay = delay;
}

void CStateManager::SetBossParams(TUniqueId bossId, float maxEnergy, uint stringIdx) {
  mBossId = bossId;
  mBossHealth = maxEnergy;
  mBossLanguageTableIndex = stringIdx;
}

void CStateManager::DeliverScriptMsg(const CScriptMsg& msg) {
  if (CEntity* entity = ObjectById(msg.GetId())) {
    entity->AcceptScriptMsg(*this, msg);
  }
}

void CStateManager::SendScriptMsg(CEntity* target, TUniqueId sender, EScriptObjectMessage message,
                                  TUniqueId actor) {
  if (target != nullptr) {
    SendScriptMsg(CScriptMsg(sender, target->GetUniqueId(), message, actor));
  }
}

void CStateManager::SendScriptMsg(TUniqueId target, TUniqueId sender, EScriptObjectMessage message,
                                  TUniqueId actor) {
  SendScriptMsg(CScriptMsg(sender, target, message, actor));
}

float CStateManager::IntegrateVisorFog(float fog) const {
  const CPlayerState* playerState = mPlayerState;
  if (playerState->GetActiveVisor(*this) == CPlayerState::kPV_Scan) {
    return fog * (1.f - playerState->GetVisorTransitionFactor());
  }
  return fog;
}

uint CStateManager::MaskUIdNumPlayers(TUniqueId id) const {
  const uint index = id.Value();
  return index < static_cast< uint >(mNumPlayers) ? index : 0;
}

void CStateManager::SetupParticleDrawMask() {
  const CPlayerState::EPlayerVisor visor = mPlayerState->GetActiveVisor(*this);
  uint flags = 0;
  uint mask = 8;
  switch (visor) {
  case CPlayerState::kPV_Echo:
    flags |= 8;
    break;
  case CPlayerState::kPV_Combat:
  case CPlayerState::kPV_Scan:
    mask |= 1;
    break;
  case CPlayerState::kPV_Dark:
    mask |= 2;
    break;
  default:
    break;
  }

  uint drawMask = mask | 0x10;
  if (mIsDarkWorld) {
    drawMask = mask | 4;
  }
  CParticleGen::sDrawFlags = flags;
  CParticleGen::sDrawMask = drawMask;
}

void CStateManager::CapturePlayerTextures() {
  uint textureWidth = kScanTargetTextureWidth;
  uint textureHeight = kScanTargetTextureHeight;
  if (IsMultiplayer()) {
    textureHeight /= 2;
  }
  if (mNumPlayers >= 3u) {
    textureWidth /= 2;
  }

  int left, bottom, width, height;
  if (!gpRender->IsRGBA6Current()) {
    const uint textureSize = textureWidth * textureHeight;
    for (uint i = 0; i < mNumPlayers; ++i) {
      CalculatePlayerViewport(mNumPlayers > 2u ? mPlayerStates[i]->GetPlayerSelection() : i, &left,
                              &bottom, &width, &height);
      CBasics::ZeroMemory(mPlayers[i]->GetScanTargetIdTextureData(), textureSize);
      CBasics::ZeroMemory(mPlayers[i]->GetDepthHighTextureData(), textureSize);
      CBasics::ZeroMemory(mPlayers[i]->GetDepthLowTextureData(), textureSize);
    }
  } else {
    for (uint i = 0; i < mNumPlayers; ++i) {
      CalculatePlayerViewport(mNumPlayers > 2u ? mPlayerStates[i]->GetPlayerSelection() : i, &left,
                              &bottom, &width, &height);
#ifdef TARGET_PC
      // Port: pointer aim (lock-on free aim included) in the Scan Visor picks the scan target
      // under the pointer, as MP1 and Trilogy do, so the id strip is read there, not at the
      // centre. Same offset as the lens (TriDrawScanLens) and the orbit box (PortScanPointerNdc),
      // kept inside the viewport.
      // The console's 32x64 strip is 5% x 14% of its 640x448 frame. The logical viewport is 448
      // high at any window size but as wide as the aspect needs, so keep the same share of the
      // screen by widening the read-back region; FillBuffers resamples it to 32x64.
      int pickWidth = (width * static_cast< int >(textureWidth) + 320) / 640;
      int pickHeight = (height * static_cast< int >(textureHeight) + 224) / 448;
      pickWidth = pickWidth < static_cast< int >(textureWidth) ? static_cast< int >(textureWidth)
                  : pickWidth > 128                            ? 128
                                                               : pickWidth;
      pickHeight = pickHeight < static_cast< int >(textureHeight) ? static_cast< int >(textureHeight)
                   : pickHeight > 128                             ? 128
                                                                  : pickHeight;
      int portShiftX = 0;
      int portShiftY = 0;
      // The pointer's offset from the viewport's centre (the lens's, unclamped) when it aims.
      int lensX = 0;
      int lensY = 0;
      const bool scanVisor = mPlayerState->GetCurrentVisor() == CPlayerState::kPV_Scan;
      {
        float px = 0.f, py = 0.f;
        if (scanVisor && PortMouse::PointerAimPosition(px, py)) {
          lensX = static_cast< int >(px * 0.5f * width);
          lensY = static_cast< int >(py * 0.5f * height);
        }
      }
      // Trilogy's Scan visor picks inside its lens: the region is the lens's bounding square
      // (the circle is cut out in PortScanTarget), not the console's strip.
      const bool lensPick = scanVisor && port::entry::IsTrilogy(port::entry::CurrentDiscVersion());
      float lensRadius = 0.f;
      if (lensPick) {
        lensRadius = PortScanTarget::LensRadius(width, height);
        int side = static_cast< int >(2.f * lensRadius + 0.999f);
        side = side > 256 ? 256 : side > width ? width : side > height ? height : side;
        pickWidth = side;
        pickHeight = side;
      }
      {
        const int maxX = (width - pickWidth) / 2;
        const int maxY = (height - pickHeight) / 2;
        portShiftX = lensX < -maxX ? -maxX : lensX > maxX ? maxX : lensX;
        portShiftY = lensY < -maxY ? -maxY : lensY > maxY ? maxY : lensY;
      }
      const int textureLeft = left + width / 2 - textureWidth / 2 + portShiftX;
      const int textureTop =
          CGraphics::GetViewportTop(bottom) + height / 2 - textureHeight / 2 + portShiftY;
      const int pickLeft = left + width / 2 - pickWidth / 2 + portShiftX;
      const int pickTop = CGraphics::GetViewportTop(bottom) + height / 2 - pickHeight / 2 + portShiftY;
      PortScanTarget::Pick portPick;
      portPick.viewportWidth = width;
      portPick.viewportHeight = height;
      portPick.regionWidth = pickWidth;
      portPick.regionHeight = pickHeight;
      portPick.regionLeft = static_cast< float >(pickLeft - (left + width / 2));
      portPick.regionTop =
          static_cast< float >(pickTop - (CGraphics::GetViewportTop(bottom) + height / 2));
      // The lens centre in region pixels (the region can sit off it where the lens hangs over the
      // viewport's edge).
      portPick.centreX = static_cast< float >(pickWidth) * 0.5f + static_cast< float >(lensX - portShiftX);
      portPick.centreY = static_cast< float >(pickHeight) * 0.5f + static_cast< float >(lensY - portShiftY);
      portPick.radius = lensRadius;
#else
      const int textureLeft = left + width / 2 - textureWidth / 2;
      const int textureTop = CGraphics::GetViewportTop(bottom) + height / 2 - textureHeight / 2;
#endif
#ifdef TARGET_PC
      // Aurora's GXCopyTex resolves into a texture, so the main-memory destination below never
      // gets written; ask for a CPU read-back of the same region first (it has to be issued
      // before the copies, which seal the pass holding the frame's scene draws) and then fill
      // the player's buffers from the previous frame's read-back. See PortScanTarget.
      if (PortScanTarget::Request(pickLeft, pickTop, pickWidth, pickHeight)) {
        PortScanTarget::SetPick(portPick);
      }
#endif
      gpRender->CopyTextureRegion(mPlayers[i]->GetScanTargetIdTextureData(), 0, textureLeft,
                                  textureTop, textureWidth, textureHeight);
#ifndef TARGET_PC
      // The two depth copies only ever wrote the player's depth buffers. On the port those come from
      // PortScanTarget::FillBuffers (the same read-back that fills the id buffer), so the console
      // copies resolve into textures nothing samples - and a scaled depth copy is not something
      // Aurora can resolve (see gfx/encoding.cpp), so they are not issued at all.
      gpRender->CopyTextureRegion(mPlayers[i]->GetDepthHighTextureData(), 1, textureLeft,
                                  textureTop, textureWidth, textureHeight);
      gpRender->CopyTextureRegion(mPlayers[i]->GetDepthLowTextureData(), 2, textureLeft, textureTop,
                                  textureWidth, textureHeight);
#endif
#ifdef TARGET_PC
      PortScanTarget::FillBuffers(mPlayers[i]->GetScanTargetIdTextureData(),
                                  mPlayers[i]->GetDepthHighTextureData(),
                                  mPlayers[i]->GetDepthLowTextureData(), textureWidth, textureHeight);
#endif
    }
  }
}

void CStateManager::RenderForgottenObjects() {
  const float depthFar = CGraphics::GetDepthFar();
  const float depthNear = CGraphics::GetDepthNear();
  const CFilteredObjectList* list = mFilteredObjectLists[kFOL_ForgottenObject].get();

  CGraphics::SetDepthRange(1.f / 256.f, 2.5f / 256.f);
  for (rstl::list< CEntity* >::const_iterator it = list->GetObjects().begin();
       it != list->GetObjects().end(); ++it) {
    TCastToPtr< CScriptForgottenObject >(*it)->RenderDepthOnly(*this);
  }

  CGraphics::SetDepthRange(2.5f / 256.f, 4.f / 256.f);
  for (rstl::list< CEntity* >::const_iterator it = list->GetObjects().begin();
       it != list->GetObjects().end(); ++it) {
    TCastToPtr< CScriptForgottenObject >(*it)->RenderAlphaMask(*this);
  }
  CGraphics::SetDepthRange(depthNear, depthFar);
}

void CStateManager::UpdateDynamicLayers() {
  for (CGameArea::CChainIterator it = mWorld->ChainHead(CWorld::kC_Alive);
       it != CWorld::AliveAreasEnd(); ++it) {
    it->UpdateDynamicLayers(*this);
  }
}

bool CStateManager::HasPendingLayerLoads() const {
  for (CGameArea::CConstChainIterator it = mWorld->GetChainHead(CWorld::kC_Alive);
       it != CWorld::skGlobalEnd; ++it) {
    if (it->HasPendingLayerLoads()) {
      return true;
    }
  }
  return false;
}

void CStateManager::SetPortalTransition(rstl::single_ptr< CPortalTransition >& transition) {
  mPortalTransition = transition;
}

rstl::single_ptr< CPortalTransition >& CStateManager::TakePortalTransition() {
  return mPortalTransition;
}

CScriptObjectLoaderHelper& CStateManager::ScriptObjectLoaderHelper() {
  return mStateManagerContainer->mScriptObjectLoader;
}

const CScriptObjectLoaderHelper& CStateManager::GetScriptObjectLoaderHelper() const {
  return mStateManagerContainer->mScriptObjectLoader;
}

CScopedProfiler::CScopedProfiler(const rstl::string&, bool) {}

CStateManager::CStateManager(
    const rstl::ncrc_ptr< CScriptMailbox >& mailbox,
    const rstl::ncrc_ptr< CMapWorldInfo >& mapWorldInfo,
    const rstl::reserved_vector< rstl::ncrc_ptr< CPlayerState >, 4 >& playerStates,
    const rstl::ncrc_ptr< CWorldTransManager >& worldTransManager,
    const rstl::ncrc_ptr< CWorldLayerState >& worldLayerState)
: mNextFreeIndex(0)
, mObjectIndexArray(0)
, mObjectLists(rstl::auto_ptr< CObjectList >())
, mFilteredObjectLists(rstl::auto_ptr< CFilteredObjectList >())
, mAllocatedObjectIndices(kMaxObjects, false)
, mArchQueue(nullptr)
, mNumPlayers(0)
, mForceTriggerIds(kInvalidUniqueId)
, mCurrentRenderPlayer(nullptr)
, mPlayerState(nullptr)
, mCameraManager(nullptr)
, mWorld(nullptr)
, mStateManagerContainer(rs_new CStateManagerContainer())
, mSortedListManager(&mStateManagerContainer->mSortedListManager)
, mWeaponMgr(&mStateManagerContainer->mWeaponManager)
, mFluidPlaneManager(&mStateManagerContainer->mFluidPlaneManager)
, mEnvFxManager(&mStateManagerContainer->mEnvFxManager)
, mActorModelParticles(&mStateManagerContainer->mActorModelParticles)
, mSafeZoneManager(&mStateManagerContainer->mSafeZoneManager)
, mAudioGroupDependencies(static_cast< CDependencyGroup* >(nullptr))
, mPlayerStateOwners(playerStates)
, mMailbox(mailbox)
, mMapWorldInfo(mapWorldInfo)
, mWorldTransManager(worldTransManager)
, mCurrentWorldLayerState(worldLayerState)
, mNextAreaId(0)
, mPreviousAreaId(kInvalidAreaId)
, mRenderFrameIndex(0)
, mUpdateFrameIdx(0)
, mObjectDrawToken(0)
, mUnknown0x16b4(0)
, mShadowTex(gpSimplePool->GetObj("DefaultShadow"))
, mRandom(0)
, mRandomAvailable(false)
, mGameState(kGS_Running)
, mInitPhase(kIP_LoadAudioGroups)
, mCameraFilterPasses(4, rstl::reserved_vector< CCameraFilterPass, 11 >(11, CCameraFilterPass()))
, mCameraBlurPasses(4, rstl::reserved_vector< CCameraBlurPass, 11 >(11, CCameraBlurPass()))
, mHintIdx(-1)
, mHintPeriods(0)
, mPauseHudMessage(kInvalidAssetId)
, mEscapeTotalTime(0.f)
, mCurTimeMod900(0.f)
, mBossId(kInvalidUniqueId)
, mBossHealth(0.f)
, mBossLanguageTableIndex(0)
, mRenderVisorMode(kRVM_Normal)
, mSpecialFunctionId(kInvalidUniqueId)
, mPlayerActorHead(kInvalidUniqueId)
, mHudMessageTime(0.f)
, mProjectedShadows(nullptr)
, mHudMessageFrameCount(0)
, mPausedHudMemoFrameCount(-1)
, mPausedHudMemoAssetId(kInvalidAssetId)
, mQueuedHudMemoDismissalDelay(0.f)
, mMapTeleportWorldId(kInvalidAssetId)
, mDeferredTransition(kSMT_InGame)
, mPlayerLineOfSightPairs(0)
, mNextPlayerLineOfSightPair(0)
, mPlanes()
, mCurrentRenderPlayerIndex(kInvalidRenderPlayerIndex)
, mVisAreaId(-1)
, mPendingDockArea(kInvalidAreaId)
, mPendingDock(0)
, mDarkWorldCloud()
, mReadyToRender(false)
, mQuitGame(false)
, mUnkFlagA3(true)
, mInMapScreen(false)
, mInSaveUI(false)
, mCinematicPause(false)
, mIsFullThreat(false)
, mIsDarkWorld(false)
, mShowSoftTransition(true)
, mTearingDown(false)
, mDispatchingScriptMessages(false)
, mLayerRestartPending(false)
, mLightAmmoDepletedPlayers(0)
, mDarkAmmoDepletedPlayers(0) {
  mRumbleManagers[0] = &mStateManagerContainer->mRumbleManager0;
  mRumbleManagers[1] = &mStateManagerContainer->mRumbleManager1;
  mRumbleManagers[2] = &mStateManagerContainer->mRumbleManager2;
  mRumbleManagers[3] = &mStateManagerContainer->mRumbleManager3;

  mObjectLists[kOL_All] = rs_new CObjectList(kOL_All, false);
  mObjectLists[kOL_Actor] = rs_new CActorList();
  mObjectLists[kOL_PhysicsActor] = rs_new CPhysicsActorList();
  mObjectLists[kOL_GameLight] = rs_new CGameLightList();
  mObjectLists[kOL_ListeningAi] = rs_new CListeningAiList();
  mObjectLists[kOL_AiWaypoint] = rs_new CAiWaypointList();
  mObjectLists[kOL_Platform] = rs_new CPlatformList();
  mObjectLists[kOL_Trigger] = rs_new CTriggerList();

  mFilteredObjectLists[kFOL_Dock] = rs_new CFilteredDockList();
  mFilteredObjectLists[kFOL_Door] = rs_new CFilteredDoorList();
  mFilteredObjectLists[kFOL_Type124] = rs_new CFilteredType124List();
  mFilteredObjectLists[kFOL_ForgottenObject] = rs_new CFilteredForgottenObjectList();
  mFilteredObjectLists[kFOL_GameCamera] = rs_new CFilteredGameCameraList();
  mFilteredObjectLists[kFOL_GrapplePoint] = rs_new CFilteredGrapplePointList();

  for (int i = 0; i < mObjectLists.size(); ++i) {
    CObjectList* list = mObjectLists[i].get();
    if (list->IsDynamic()) {
      mDynamicObjectLists.push_back(list);
    }
  }
  for (int i = 0; i < mFilteredObjectLists.size(); ++i) {
    CFilteredObjectList* list = mFilteredObjectLists[i].get();
    if (list->IsDynamic()) {
      mDynamicFilteredObjectLists.push_back(list);
    }
  }

  gpRender->SetDrawableCallback(RendererDrawCallback, this);
  CMemory::SetOutOfMemoryCallback(MemoryAllocatorAllocationFailedCallback, this);
  CGameCollision::InitCollision(this);
  CMemory::OffsetFakeStatics(mObjectLists.size() * sizeof(CObjectList) +
                             mFilteredObjectLists.size() * sizeof(CFilteredObjectList) + 0x12c);
  mShadowTex.Lock();
  gpMain->SetThirtyFps(gpGameState->GetGameMode().GetNumPlayers() == 2);
}

CStateManager::~CStateManager() {
#ifdef TARGET_PC
  if (gpGameState != nullptr) {
    PortLiveSplit::GameSessionEnd(gpGameState->GetTotalPlayTime());
    PortOverlay::ClearGameTime();
  }
  PortSaveState::Forget(this);
#endif
  mTearingDown = true;
  CMemory::OffsetFakeStatics(-(mObjectLists.size() * sizeof(CObjectList) +
                               mFilteredObjectLists.size() * sizeof(CFilteredObjectList) + 0x12c));
  for (uint i = 0; i < mNumPlayers; ++i) {
    mRumbleManagers[i]->HardStopAll();
  }
  mEnvFxManager->Cleanup();
  mRandomAvailable = true;

  CObjectList& objects = *mObjectLists[kOL_All];
  ClearGraveyard();
  for (int i = 0; i != kMaxObjects; ++i) {
    CEntity* entity = objects[i];
    if (entity != nullptr && TCastToPtr< CPlayer >(entity) == nullptr &&
        TCastToPtr< CGameCamera >(entity) == nullptr) {
      DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, entity->GetUniqueId(), kSM_Delete));
    }
  }
  for (int i = 0; i != kMaxObjects; ++i) {
    CEntity* entity = objects[i];
    if (entity != nullptr && TCastToPtr< CPlayer >(entity) == nullptr &&
        TCastToPtr< CGameCamera >(entity) == nullptr) {
      RemoveObject(entity->GetUniqueId());
      delete entity;
    }
  }
  ClearGraveyard();

  const CFilteredObjectList cameras(*mFilteredObjectLists[kFOL_GameCamera]);
  const rstl::list< CEntity* >& cameraObjects = cameras.GetObjects();
  for (rstl::list< CEntity* >::const_iterator it = cameraObjects.begin(); it != cameraObjects.end();
       ++it) {
    if (CGameCamera* camera = TCastToPtr< CGameCamera >(*it)) {
      DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, camera->GetUniqueId(), kSM_Delete));
      RemoveObject(camera->GetUniqueId());
      delete camera;
    }
  }
  for (uint i = 0; i < mNumPlayers; ++i) {
    if (CPlayer* player = mPlayers[i]) {
      DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, player->GetUniqueId(), kSM_Delete));
      RemoveObject(player->GetUniqueId());
      delete player;
    }
  }

  CGameCollision::UninitializeCollision();
  CMemory::SetOutOfMemoryCallback(nullptr, nullptr);
  gpMain->SetThirtyFps(false);
  CAudioGrpSetLoc::sInSinglePlayer = false;
}

void CStateManager::ClearGraveyard() {
  for (rstl::list< rstl::reserved_vector< CEntity*, 32 > >::iterator it = mGraveyard.begin();
       it != mGraveyard.end(); ++it) {
    rstl::reserved_vector< CEntity*, 32 >& batch = *it;
    for (rstl::reserved_vector< CEntity*, 32 >::iterator entity = batch.begin();
         entity != batch.end(); ++entity) {
      delete *entity;
    }
  }
  mGraveyard.clear();
}

void CStateManager::FrameBegin(uint frame) {
  mRenderFrameIndex = frame;
  CTexture::sCurrentFrameCount = mRenderFrameIndex;
  CGraphicsPalette::sCurrentFrameCount = mRenderFrameIndex;
  SwapOutTexturesToARAM(2, 0x180000);
}

void CStateManager::SwapOutTexturesToARAM(int, uint) {}

const bool CStateManager::MemoryAllocatorAllocationFailedCallback(const void* context, uint) {
  return static_cast< CStateManager* >(const_cast< void* >(context))->SwapOutAllPossibleMemory();
}

bool CStateManager::SwapOutAllPossibleMemory() {
  CFrameDelayedKiller::StallAndFlushAllAllocations();
  CARAMManager::WaitForAllDMAsToComplete();
  CARAMToken::UpdateAllDMAs();
  return true;
}

void CStateManager::RecursiveDrawTree(TUniqueId uid) const {
  CActor* actor = TCastToPtr< CActor >(const_cast< CEntity* >(GetObjectById(uid)));
  if (actor != nullptr && mObjectDrawToken != actor->GetDrawToken()) {
    const TUniqueId nextNode = actor->GetDrawParent();
    if (nextNode != kInvalidUniqueId) {
      RecursiveDrawTree(nextNode);
    }
    if (mObjectDrawToken == actor->GetAddedToken()) {
#ifdef TARGET_PC
      CPortActorRenderScope presented(*actor);
#endif
      actor->Render(*this);
    }
    actor->SetDrawToken(mObjectDrawToken);
  }
}

void CStateManager::RendererDrawCallback(const void* drawable, const void* context, int type) {
  const CStateManager& mgr = *static_cast< const CStateManager* >(context);
  switch (type) {
  case 0: {
    const CActor& actor = *static_cast< const CActor* >(drawable);
    if (mgr.mObjectDrawToken == actor.GetDrawToken()) {
      break;
    }
    const TUniqueId nextNode = actor.GetDrawParent();
    if (nextNode != kInvalidUniqueId) {
      mgr.RecursiveDrawTree(nextNode);
    }
#ifdef TARGET_PC
    {
      CPortActorRenderScope presented(actor);
      actor.Render(mgr);
    }
#else
    actor.Render(mgr);
#endif
    actor.SetDrawToken(mgr.mObjectDrawToken);
    break;
  }
  case 1:
    static_cast< const CSimpleShadow* >(drawable)->Render(mgr.mShadowTex.GetObject());
    break;
  case 2:
    static_cast< const CDecal* >(drawable)->Render();
    break;
  }
}

TUniqueId CStateManager::AllocateUniqueId() {
  ushort lastIndex = mNextFreeIndex;
  ushort ourIndex;
  do {
    ourIndex = mNextFreeIndex;
    mNextFreeIndex = (ourIndex + 1) % 1024;
    if (mNextFreeIndex == lastIndex) {
#ifdef TARGET_PC
      // Port: report it once. The do/while below cannot find a free index and would spin here
      // printing this every wrap, so a run would flood its log instead of showing the one fact
      // that matters (see PortWarpSettle, which keeps a warp from exhausting the pool).
      static bool sReportedFull = false;
      if (!sReportedFull) {
        sReportedFull = true;
        rs_debugger_printf("Object list full!");
      }
      // Nothing here can find a free index, so the do/while would spin forever. Say what happened
      // - including who holds the pool - and stop: a run that dies naming the cause is worth more
      // than one that hangs.
      std::fprintf(stderr, "[ids] the object id pool is exhausted (1024 live objects)\n");
      PortLogIdPressure();
      std::fflush(stderr);
      std::abort();
#else
      rs_debugger_printf("Object list full!");
#endif
    }
  } while (mAllocatedObjectIndices[ourIndex]);

  mObjectIndexArray[ourIndex] = (mObjectIndexArray[ourIndex] + 1) & 0x3f;
  if (TUniqueId(mObjectIndexArray[ourIndex], ourIndex) == kInvalidUniqueId) {
    mObjectIndexArray[ourIndex] = 0;
  }

  mAllocatedObjectIndices[ourIndex] = true;

  return TUniqueId(mObjectIndexArray[ourIndex], ourIndex);
}

void CStateManager::CreateStandardGameObjects() {
  mNumPlayers = gpGameState->GetGameMode().GetNumPlayers();
  CTweakPlayerGun* playerGunTweak = gpTweakPlayerGunSingle.get();
  if (mNumPlayers > 1u) {
    playerGunTweak = gpTweakPlayerGunMulti.get();
  }
  gpTweakPlayerGun = playerGunTweak;

  mCameraManagers[0] = &mStateManagerContainer->mCameraManager0;
  mCameraManagers[1] = &mStateManagerContainer->mCameraManager1;
  mCameraManagers[2] = &mStateManagerContainer->mCameraManager2;
  mCameraManagers[3] = &mStateManagerContainer->mCameraManager3;
  for (uint i = 0; i < mNumPlayers; ++i) {
    const TUniqueId uid = AllocateUniqueId();
    const CVector3f position(5.f * (i & 1), 0.f, 5.f * (i & 2));
    const CRelAngle angle = CRelAngle::FromDegrees(129.6f);
    const CMatrix3f matrix =
        CQuaternion::AxisAngle(CUnitVector3f(0.f, 0.f, 1.f, CUnitVector3f::kN_Yes), angle)
            .BuildTransform();
    const CTransform4f transform = CTransform4f::FromColumns(
        matrix.GetColumn(kDX), matrix.GetColumn(kDY), matrix.GetColumn(kDZ), position);

    CTweakPlayer* tweak = gpTweakPlayerA.get();
    const int controlScheme = mPlayerStateOwners[i]->GetControlScheme();
    if (controlScheme == 1) {
      tweak = gpTweakPlayerB.get();
    }
    const float stepUp = tweak->GetStepUpHeight();
    const float stepDown = tweak->GetStepDownHeight();
    const float height = tweak->GetPlayerHeight();
    const float radius = tweak->GetPlayerRadius();
    const float ballRadius = tweak->GetBallRadius();
    const CAABox bounds(CVector3f(-radius, -radius, 0.f), CVector3f(radius, radius, height));

    const SObjectTag* animationController = gpResourceFactory->GetResourceIdByName(
        IsMultiplayer() ? skMultiplayerAnimController : skSinglePlayerAnimController);
    const CAssetId stateMachine = animationController ? animationController->id : kInvalidAssetId;
    mPlayerStates[i] = mPlayerStateOwners[i].GetPtr();
    int characterIndex = 3;
    if (IsMultiplayer()) {
      characterIndex = 0;
    }
    mPlayers[i] = rs_new CPlayer(
        uid, transform, bounds, gpTweakPlayerRes->GetBallTransitionANCSId(), stateMachine, 200.f,
        stepUp, stepDown, ballRadius,
        CMaterialList(kMT_Player, kMT_Solid, kMT_GroundCollider, kMT_Target, kMT_NoPlayerCollision),
        mPlayerStates[i], mCameraManagers[i], mNumPlayers > 1u, i,
        mPlayerStateOwners[i]->GetControlScheme(), characterIndex);
  }

  for (uint i = 0; i < mNumPlayers; ++i) {
    AddObject(*mPlayers[i]);
    if (!gkDisablePlayerTargeting) {
      mPlayers[i]->AddMaterial(kMT_Orbit, kMT_Scannable, *this);
    }
  }

  float fieldOfView = gpTweakGame->GetFieldOfView();
  if (mNumPlayers == 2u) {
    fieldOfView = gpTweakGame->GetTwoPlayerFieldOfView();
  }
  for (uint i = 0; i < mNumPlayers; ++i) {
    mCameraManagers[i]->SetFirstPersonFOV(fieldOfView);
    mCameraManagers[i]->CreateCameras(*this);
  }
  mEnvFxManager->AsyncLoadResources(*this);
}

void CStateManager::SpawnPlayer(CScriptSpawnPoint& spawnPoint, uint playerIndex) {
  CPlayer& player = *mPlayers[playerIndex];
  CPlayerState& state = *player.GetPlayerState();
  const CVector3f position = spawnPoint.GetTransform().GetTranslation();
  CVector3f forward = spawnPoint.GetTransform().GetForward();
  forward.SetZ(0.f);
  if (forward.CanBeNormalized()) {
    player.Teleport(CTransform4f::LookAt(position, position + forward, CVector3f::Up()), *this,
                    true);
    player.SetSpawnedMorphBallState(
        spawnPoint.IsMorphed() ? CPlayer::kMS_Morphed : CPlayer::kMS_Unmorphed, *this);
    player.ResetPlayerState(*this, 1);
  }

  if (gpGameState->GetGameMode().IsMultiplayer() || gpGameState->GetInitPowerupsAtFirstSpawn()) {
    gpGameState->SetDeferPowerupInit(false);
    for (int i = CPlayerState::kIT_PowerBeam; i < CPlayerState::kIT_Max; ++i) {
      const CPlayerState::EItemType item = static_cast< CPlayerState::EItemType >(i);
      if (state.GetItemCapacity2(item) < spawnPoint.GetItemCapacity(item)) {
        state.AddPowerUp(item, spawnPoint.GetItemCapacity(item) - state.GetItemCapacity2(item));
      }
      if (state.GetItemAmount(item, true) < spawnPoint.GetItemAmount(item)) {
        state.IncrPickUp(item, spawnPoint.GetItemAmount(item) - state.GetItemAmount(item, true));
      }
    }
  }

  const uint spawnedPlayerIndex = player.GetPlayerIndex();
  const CGameState& gameState = *gpGameState;
  gameState.GetGameMode().OnPlayerSpawned(*this, spawnedPlayerIndex);
  spawnPoint.SendSpawnMessage(*this, player);
  player.AsyncLoadSuit(*this);
}

void CStateManager::InitializeState(CAssetId worldId, TAreaId areaId, CAssetId mreaId) {
  const bool randomWasAvailable = IsRandomAvailable();
  mRandomAvailable = true;
  if (mInitPhase == kIP_LoadAudioGroups) {
    if (!IsMultiplayer()) {
      CAudioGrpSetLoc::sInSinglePlayer = true;
    }
    if (gpGameState->AudioGroups().empty()) {
      const int gameMode = gpGameState->GetGameMode().GetGameModeType();
      int dependencyIndex = 2;
      if (gameMode == 'SNGL') {
        dependencyIndex = 0;
      }
      if (gameMode == 'FRND') {
        dependencyIndex = 1;
      }
      mAudioGroupDependencies = TToken< CDependencyGroup >(
          gpSimplePool->GetObj(skAudioGroupDependencies[dependencyIndex]));
      mAudioGroupDependencies.Lock();
    }
    mInitPhase = kIP_LoadWorld;
  }

  if (mInitPhase == kIP_LoadWorld) {
    if (gpGameState->AudioGroups().empty()) {
      if (!mAudioGroupDependencies.IsLoaded()) {
        return;
      }
      CDependencyGroup& dependencies = **mAudioGroupDependencies;
      rstl::vector< TCachedToken< CAudioGrpSetLoc > >& audioGroups = gpGameState->AudioGroups();
      const rstl::vector< SObjectTag >& tags = dependencies.GetObjectTagVector();
      audioGroups.reserve(tags.size());
      for (rstl::vector< SObjectTag >::const_iterator it = tags.begin(); it != tags.end(); ++it) {
        if (it->type == 'AGSC') {
          audioGroups.push_back_unsafe(TCachedToken< CAudioGrpSetLoc >(gpSimplePool->GetObj(*it)));
        }
      }
      mAudioGroupDependencies.Unlock();
      for (rstl::vector< TCachedToken< CAudioGrpSetLoc > >::iterator it = audioGroups.begin();
           it != audioGroups.end(); ++it) {
        it->Lock();
      }
    }
    CreateStandardGameObjects();
    mWorld = rs_new CWorld(*gpSimplePool, *gpResourceFactory, worldId);
    mInitPhase = kIP_LoadFirstArea;
  }

  if (mInitPhase == kIP_LoadFirstArea) {
    if (!mShadowTex.TryCache()) {
      return;
    }
    if (mNumPlayers == 3u && mUnusedViewportTexture.null()) {
      mUnusedViewportTexture =
          rs_new TCachedToken< CTexture >(gpSimplePool->GetObj(skUnusedViewportTexture));
      mUnusedViewportTexture->Lock();
      return;
    }
    if (!mUnusedViewportTexture.null() && !mUnusedViewportTexture->TryCache()) {
      return;
    }
    if (!mWorld->CheckWorldComplete(this, areaId, mreaId)) {
      return;
    }
    mNextAreaId = mWorld->GetCurrentAreaId();
    CGameArea* area = mWorld->Area(GetNextAreaId());
    if (mWorld->ScheduleAreaToLoad(area, *this)) {
      area->StartStreamIn(*this);
      return;
    }
    mInitPhase = kIP_Done;
  }

  SetCurrentAreaId(mNextAreaId);
  gpGameState->CurrentWorldState().SetAreaId(mNextAreaId);
  mWorld->TravelToArea(mNextAreaId, *this, CWorld::kATT_SkipAdjacent);
  CObjectList* allObjects = mObjectLists[kOL_All].get();
  for (int i = allObjects->GetFirstObjectIndex(); i != -1; i = allObjects->GetNextObjectIndex(i)) {
    DeliverScriptMsg(
        CScriptMsg(kInvalidUniqueId, (*allObjects)[i]->GetUniqueId(), kSM_WorldLoaded));
  }

  if (mPendingDockArea != kInvalidAreaId) {
    CObjectList* objects = mObjectLists[kOL_All].get();
    for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
      CActor& actor = *static_cast< CActor* >((*objects)[i]);
      const TAreaId actorArea = actor.GetCurrentAreaId();
      SetActorAreaId(actor, kInvalidAreaId);
      SetActorAreaId(actor, actorArea);
    }
  }

  int nextSpawn = 0;
  if (mPendingDockArea != kInvalidAreaId) {
    CScriptDock* dock = nullptr;
    CObjectList* objects = mObjectLists[kOL_All].get();
    for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
      if (CScriptDock* candidate = TCastToPtr< CScriptDock >((*objects)[i])) {
        if (candidate->GetDockId() == mPendingDock) {
          dock = candidate;
          break;
        }
      }
    }
    CScriptSpawnPoint* spawn = nullptr;
    if (dock != nullptr) {
      spawn = TCastToPtr< CScriptSpawnPoint >(
          ObjectById(dock->FindConnectedObject(*this, kSS_MaxReached, kSM_Activate)));
    }
    if (spawn != nullptr) {
      SpawnPlayer(*spawn, 0);
      nextSpawn = 1;
    }
  }

  if (!IsMultiplayer()) {
    if (nextSpawn == 0) {
      CObjectList* objects = mObjectLists[kOL_All].get();
      for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
        if (CScriptSpawnPoint* spawn = TCastToPtr< CScriptSpawnPoint >((*objects)[i])) {
          if (spawn->GetActive() && spawn->IsFirstSpawn()) {
            SpawnPlayer(*spawn, nextSpawn);
            break;
          }
        }
      }
    }
  } else {
    rstl::vector< TUniqueId > spawnPoints;
    spawnPoints.reserve(mNumPlayers);
    CObjectList* objects = mObjectLists[kOL_All].get();
    for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
      if (CScriptSpawnPoint* spawn = TCastToPtr< CScriptSpawnPoint >((*objects)[i])) {
        if (spawn->GetActive() && spawn->IsFirstSpawn()) {
          spawnPoints.reserve(spawnPoints.size() + 1);
          spawnPoints.push_back_unsafe(spawn->GetUniqueId());
        }
      }
    }
    while (!spawnPoints.empty() && spawnPoints.size() < spawnPoints.capacity()) {
      spawnPoints.push_back_unsafe(spawnPoints.front());
    }
    CRandom16 random(CStopwatch::GetGlobalMicros());
    rstl::random_shuffle(spawnPoints.begin(), spawnPoints.end(), random);
    for (uint i = 0; i < mNumPlayers; ++i) {
      const int startingSpawn = nextSpawn;
      do {
        if (CScriptSpawnPoint* spawn =
                TCastToPtr< CScriptSpawnPoint >(ObjectById(spawnPoints[nextSpawn]))) {
          SpawnPlayer(*spawn, i);
          nextSpawn = (nextSpawn + 1) % spawnPoints.size();
          break;
        }
        nextSpawn = (nextSpawn + 1) % spawnPoints.size();
      } while (nextSpawn != startingSpawn);
    }
  }
  mRandomAvailable = randomWasAvailable;
}

bool CStateManager::PrepareAreaTransition(TAreaId areaId) {
  TAreaId areaToKeep = areaId;
  if (!mWorld->UnloadAllAreasExcept(*this, areaToKeep)) {
    return false;
  }
  for (uint i = 0; i < mNumPlayers; ++i) {
    mRumbleManagers[i]->HardStopAll();
  }
  mPendingDockArea = areaId;
  mInitPhase = kIP_LoadFirstArea;

  CObjectList* objects = mObjectLists[kOL_All].get();
  for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
    CEntity& entity = *(*objects)[i];
    if (entity.GetCurrentAreaId() != kInvalidAreaId) {
      entity.SetCurrentAreaId(areaId);
    }
    if (TCastToPtr< CWeapon >(&entity) != nullptr) {
      DeleteObjectRequest(entity.GetUniqueId());
    }
  }

  const bool randomWasAvailable = IsRandomAvailable();
  mRandomAvailable = true;
  mPlayers[0]->GetPlayerGun()->Reset(*this);
  mRandomAvailable = randomWasAvailable;
  mPlayers[0]->StopSounds();
  SetIsDarkWorld(!mIsDarkWorld);
  mDarkWorldCloud = SDarkWorldCloud();
  return true;
}

const CEntity* CStateManager::GetObjectById(TUniqueId uid) const {
  return GetObjectListById(kOL_All).GetObjectById(uid);
}

void CStateManager::SetIsDarkWorld(bool b) {
  mIsDarkWorld = b;
  gpGameState->SetIsDarkWorld(mIsDarkWorld);
}

bool CStateManager::HasWorld() const { return !mWorld.null(); }

void CStateManager::UpdateActorInSortedLists(CActor* actor) {
  if (actor->GetTransformDirty()) {
    actor->SetTransformDirty(false);
    if (actor->GetUseInSortedLists()) {
      const rstl::optional_object< CAABox > bounds = CalculateObjectBounds(*actor);
      const bool actorInLists = mSortedListManager->ActorInLists(actor);
      const uint hasBounds = bounds.valid();
      if (actorInLists || hasBounds) {
        if (actorInLists) {
          if (!actor->GetActive() || !hasBounds) {
            mSortedListManager->Remove(actor);
          } else {
            mSortedListManager->Move(actor, *bounds);
          }
        } else if (actor->GetActive() && hasBounds) {
          mSortedListManager->Insert(actor, *bounds);
        }
      }
    }
  }
}

void CStateManager::UpdateSortedLists() {
  if (mWorld.get() == nullptr) {
    return;
  }

  CObjectList* actorList = mObjectLists[kOL_Actor].get();
  for (int i = actorList->GetFirstObjectIndex(); i != -1; i = actorList->GetNextObjectIndex(i)) {
    CActor* actor = static_cast< CActor* >((*actorList)[i]);
    if (actor != nullptr) {
      UpdateActorInSortedLists(actor);
    }
  }
}

rstl::optional_object< CAABox > CStateManager::CalculateObjectBounds(CActor& actor) {
  CPhysicsActor* physicsActor = TCastToPtr< CPhysicsActor >(actor);
  rstl::optional_object< CAABox > touchBounds = actor.GetTouchBounds();
  if (touchBounds.valid()) {
    CAABox bounds = *touchBounds;
    if (physicsActor != nullptr) {
      bounds.Include(physicsActor->GetBoundingBox());
    }
    return bounds;
  }
  if (physicsActor != nullptr) {
    return physicsActor->GetBoundingBox();
  }
  return rstl::optional_object_null();
}

void CStateManager::AddObject(CEntity& entity) {
  const TUniqueId id = entity.GetUniqueId();
  if (entity.GetEditorId() != kInvalidEditorId) {
    mScriptIdMap.insert(rstl::pair< TEditorId, TUniqueId >(entity.GetEditorId(), id));
  }

  for (rstl::reserved_vector< rstl::auto_ptr< CObjectList >, 8 >::iterator it =
           mObjectLists.begin();
       it != mObjectLists.end(); ++it) {
    (*it)->AddObject(entity);
  }
  for (rstl::reserved_vector< rstl::auto_ptr< CFilteredObjectList >, 6 >::iterator it =
           mFilteredObjectLists.begin();
       it != mFilteredObjectLists.end(); ++it) {
    (*it)->AddObject(entity);
  }
  mNewObjectIds.push_back(id);

  if (entity.GetCurrentAreaId() == kInvalidAreaId && TCastToPtr< CPlayer >(entity) == nullptr) {
    entity.SetCurrentAreaId(mPlayers[0]->GetCurrentAreaId());
  }

  CActor* actor = TCastToPtr< CActor >(entity);
  const TAreaId areaId = entity.GetCurrentAreaId();
  if (areaId != kInvalidAreaId) {
    CGameArea* area = mWorld->Area(areaId);
    if (area->GetPhase() > CGameArea::kP_FinishDependencies) {
      area->ObjectList()->AddObject(entity);
    }
  }
  if (actor != nullptr) {
    UpdateActorInSortedLists(actor);
  }

  DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, entity.GetUniqueId(), kSM_Create));
  if (entity.GetCurrentAreaId() != kInvalidAreaId && HasWorld()) {
    CGameArea* area = mWorld->Area(entity.GetCurrentAreaId());
    if (area->GetPhase() > CGameArea::kP_FinishDependencies &&
        area->GetPostConstructed()->mScriptObjectsInitialized) {
      DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, entity.GetUniqueId(), kSM_AreaLoaded));
    }
  }
}

void CStateManager::RemoveObject(TUniqueId id) {
  CEntity* entity = mObjectLists[kOL_All]->GetObjectById(id);
  if (entity != nullptr) {
    const TEditorId editorId = entity->GetEditorId();
    if (editorId != kInvalidEditorId) {
      const rstl::pair< TIdList::iterator, TIdList::iterator > range =
          mScriptIdMap.equal_range(editorId);
      TIdList::iterator it = range.first;
      while (it != range.second) {
        if (it->second == id) {
          it = mScriptIdMap.erase(it);
        } else {
          ++it;
        }
      }
    }

    const TAreaId areaId = entity->GetCurrentAreaId();
    if (areaId != kInvalidAreaId) {
      CGameArea* area = mWorld->Area(areaId);
      if (area->IsLoaded()) {
        area->ObjectList()->RemoveObject(id);
        if (area->GetPostConstructed()->mPortalArea.get() != nullptr) {
          area->GetPostConstructed()->mPortalArea->RemoveActor(*this, id);
        }
      }
    }

    if (CActor* actor = TCastToPtr< CActor >(entity)) {
      mSortedListManager->Remove(actor);
      actor->SetUseInSortedLists(false);
    }
  }

  for (int i = 0; i < mObjectLists.size(); ++i) {
    mObjectLists[i]->RemoveObject(id);
  }
  for (int i = 0; i < mFilteredObjectLists.size(); ++i) {
    mFilteredObjectLists[i]->RemoveObject(id);
  }
  mAllocatedObjectIndices[id.Value()] = false;
#ifdef TARGET_PC
  PortSaveState::NoteRemoved(*this, id.value);
#endif
}

void CStateManager::SendDamageScriptMsgs(CActor& damagee, TUniqueId source,
                                         const CDamageInfo& damage) {
  damagee.SendScriptMsgs(kSS_Damage, *this, kSM_None);
  EScriptObjectState state = kSS_InvalidState;
  switch (damage.GetWeaponMode1()) {
  case kWT_Power:
    state = kSS_PowerDamage;
    break;
  case kWT_Dark:
    state = kSS_DarkDamage;
    break;
  case kWT_Light:
    state = kSS_LightDamage;
    break;
  case kWT_Annihilator:
    state = kSS_AnnihilatorDamage;
    break;
  case kWT_Bomb:
    state = kSS_BombDamage;
    break;
  case kWT_PowerBomb:
    state = kSS_PowerBombDamage;
    break;
  case kWT_Missile:
    state = kSS_MissileDamage;
    break;
  case kWT_BoostBall:
    state = kSS_BoostBallDamage;
    break;
  case kWT_CannonBall:
    state = kSS_CannonBallDamage;
    break;
  case kWT_ScrewAttack:
    state = kSS_ScrewAttackDamage;
    break;
  case kWT_Phazon:
    state = kSS_PhazonDamage;
    break;
  case kWT_AI:
    state = kSS_AIDamage;
    break;
  case kWT_PoisonWater1:
    state = kSS_PoisonWaterDamage;
    break;
  case kWT_PoisonWater2:
    state = kSS_PoisonWaterDamage;
    break;
  case kWT_Lava:
    state = kSS_LavaDamage;
    break;
  case kWT_Heat:
    state = kSS_HeatDamage;
    break;
  case kWT_Unused1:
    state = kSS_ColdDamage;
    break;
  case kWT_AreaDark:
    state = kSS_AreaDarkDamage;
    break;
  case kWT_AreaLight:
    state = kSS_AreaLightDamage;
    break;
  case kWT_UnknownSource:
    state = kSS_UnknownSourceDamage;
    break;
  case kWT_SafeZone:
    state = kSS_InvalidState;
    break;
  default:
    break;
  }
  if (state != kSS_InvalidState) {
    damagee.SendScriptMsgs(state, *this, kSM_None);
  }
}

void CStateManager::ApplyDamage(TUniqueId damagerId, TUniqueId damageeId, TUniqueId owner,
                                const CDamageInfo& damage, const CMaterialFilter& filter,
                                const CVector3f& knockbackDirection) {
#ifdef TARGET_PC
  // Doors open by taking damage, so a shot that never reaches one is worth seeing in the log.
  if (TCastToConstPtr< CScriptDoor >(GetObjectById(damageeId)) != nullptr) {
    std::fprintf(stderr, "[door] %04x took a hit (damage %.1f)\n", damageeId.Value(),
                 static_cast< double >(damage.GetDamage()));
  }
#endif
  // The native comparison uses the raw unsigned halfword, not the signed enum getter.
  if (damage.GetWeaponMode().GetRawType() == kWT_None) {
    return;
  }

  const CDamageInfo info = GetModifiedDamageInfo(damagerId, owner, damageeId, damage);
  const CEntity* damagerEntity = GetObjectById(damagerId);
  CEntity* damageeEntity = ObjectById(damageeId);
  const CActor* const damager = TCastToConstPtr< CActor >(damagerEntity);
  CActor* const damagee = TCastToPtr< CActor >(damageeEntity);
  const bool isPlayer = TCastToConstPtr< CPlayer >(damageeEntity) != nullptr;
  if (damagee == nullptr) {
    return;
  }

  if (damagee->GetHealthInfo() != nullptr) {
    CVector3f position(0.f, 0.f, 0.f);
    CVector3f direction(1.f, 0.f, 0.f);
    if (damager != nullptr) {
      position = damager->GetTransform().GetTranslation();
      direction = damager->GetTransform().GetForward();
    }

    const bool useWeaponDirection = damager != nullptr || isPlayer;
    const CDamageVulnerability* vulnerability =
        useWeaponDirection ? damagee->GetDamageVulnerability(position, direction, info)
                           : damagee->GetDamageVulnerability();
#ifdef TARGET_PC
    if (const char* dbg = std::getenv("MP2_DEBUG_BALL"); dbg != nullptr) {
      {
        std::fprintf(stderr,
                     "[ball] applydamage to %04x eid %04x: weapon %d hits %d damage %.1f "
                     "(power %d/%.2f missile %d/%.2f cannon %d/%.2f)\n",
                     damageeId.Value(), damagee->GetEditorId().Value(),
                     int(info.GetWeaponMode().GetRawType()),
                     int(vulnerability->WeaponHits(info.GetWeaponMode(), 0)),
                     static_cast< double >(info.GetDamage(*vulnerability)),
                     int(vulnerability->WeaponHits(CWeaponMode(kWT_Power), 0)),
                     static_cast< double >(vulnerability->GetVulnerability(CWeaponMode(kWT_Power))
                                               .mDamageMultiplier),
                     int(vulnerability->WeaponHits(CWeaponMode(kWT_Missile), 0)),
                     static_cast< double >(vulnerability->GetVulnerability(CWeaponMode(kWT_Missile))
                                               .mDamageMultiplier),
                     int(vulnerability->WeaponHits(CWeaponMode(kWT_CannonBall), 0)),
                     static_cast< double >(vulnerability->GetVulnerability(CWeaponMode(kWT_CannonBall))
                                               .mDamageMultiplier));
      }
    }
#endif
    if (info.GetWeaponMode().GetRawType() == kWT_None ||
        vulnerability->WeaponHits(info.GetWeaponMode(), 0)) {
      const float localDamage = info.GetDamage(*vulnerability);
      if (localDamage > 0.f) {
        ApplyLocalDamage(position, direction, *damagee, localDamage, damagerId, owner, info, 0);
      }
      SendDamageScriptMsgs(*damagee, damagerId, info);
      SendScriptMsg(damagee, damagerId, kSM_Damage);
    } else {
      damagee->SendScriptMsgs(kSS_ResistedDamage, *this, kInvalidUniqueId, kSM_None);
      SendScriptMsg(damagee, damagerId, kSM_ResistedDamage);
    }

    float knockbackX = knockbackDirection.GetX();
    float knockbackY = knockbackDirection.GetY();
    if (damager != nullptr && info.GetKnockBackPower(*vulnerability, 0.f) > 0.f) {
      const CVector3f defaultDirection =
          damagee->GetTransform().GetTranslation() - damager->GetTransform().GetTranslation();
      const CVector3f& useDirection =
          knockbackDirection.IsNonZero() ? knockbackDirection : defaultDirection;
      knockbackX = useDirection.GetX();
      knockbackY = useDirection.GetY();
    }
    const CVector3f knockback(knockbackX, knockbackY, 0.0001f);
    ApplyKnockBack(*damagee, damagerId, owner, info, *vulnerability, knockback.AsNormalized(), 0.f);
  }

  if (damager != nullptr && info.GetRadius() > 0.f) {
    ProcessRadiusDamage(*damager, *damagee, owner, info, filter);
  }
  CSwarmBasics* swarm = TCastToPtr< CSwarmBasics >(damageeEntity);
  if (swarm != nullptr && damager != nullptr) {
    swarm->ApplyRadiusDamage(damager->GetTranslation(), info, *this);
  }
}

void CStateManager::KillPlayer(float previousHealth, TUniqueId victim, TUniqueId killer) {
  CPlayer* player = TCastToPtr< CPlayer >(ObjectById(victim));
  if (player != nullptr) {
    PlayerState(MaskUIdNumPlayers(victim))->SetPlayerAlive(false);

    if (previousHealth >= 0.f) {
      const CGameState& gameState = *gpGameState;
      CGameMode& gameMode = gameState.GetGameMode();
      gameMode.OnPlayerKilled(*this, victim, killer);
    }

    if (!IsMultiplayer()) {
      CSfxManager::KillAll(CSfxManager::kSC_Game);
      CStreamAudioManager::FadeOutSoftwareAudio(CStreamAudioManager::kSC_Default, 0.5f);
    }
  }
}

CDamageInfo CStateManager::GetModifiedDamageInfo(TUniqueId damager, TUniqueId owner,
                                                 TUniqueId damagee,
                                                 const CDamageInfo& damage) const {
  const CPatterned* patterned = TCastToConstPtr< CPatterned >(GetObjectById(damager));
  if (patterned == nullptr) {
    patterned = TCastToConstPtr< CPatterned >(GetObjectById(owner));
  }

  if (patterned != nullptr) {
    if (patterned->IsIngPossessed()) {
      CDamageInfo result = damage;
      result.SetDamage(damage.GetDamage() * patterned->GetIngPossessedDamageMultiplier());
      return result;
    }
    return damage;
  }

  if (gpGameState->GetHardModeEnabled() &&
      TCastToConstPtr< CPlayer >(GetObjectById(owner)) != nullptr) {
    bool aiDamage = false;
    if (TCastToConstPtr< CPatterned >(GetObjectById(damagee)) != nullptr) {
      aiDamage = true;
    } else if (const CCollisionActor* collision =
                   TCastToConstPtr< CCollisionActor >(GetObjectById(damagee))) {
      if (TCastToConstPtr< CPatterned >(GetObjectById(collision->GetOwnerId())) != nullptr) {
        aiDamage = true;
      }
    }
    if (aiDamage) {
      return NGunUtils::DifficultyModifyDamageInfo(damage);
    }
  }
  return damage;
}

void CStateManager::RecordDamageSource(CActor& damagee, TUniqueId source, const CDamageInfo& info,
                                       bool lethal, bool radiusDamage) {
  CHealthInfo* const health = damagee.HealthInfo();
  if (health == nullptr) {
    return;
  }

  bool frozen = false;
  if (lethal) {
    CPatterned* const patterned = TCastToPtr< CPatterned >(&damagee);
    if (patterned != nullptr) {
      frozen = patterned->BodyController()->IsFrozen();
    } else {
      CPlayer* const player = TCastToPtr< CPlayer >(&damagee);
      if (player != nullptr) {
        frozen = player->GetFrozenState();
      }
    }
  }

  const CWeapon* const weapon = TCastToConstPtr< CWeapon >(GetObjectById(source));
  if (weapon != nullptr) {
    health->SetLastDamageWeapon(weapon->GetCurrentDamageInfo().GetWeaponMode(),
                                weapon->GetOwnerId(), source, radiusDamage);
    if (lethal) {
      health->SetCauseOfDeathWeapon(weapon->GetCurrentDamageInfo().GetWeaponMode(),
                                    weapon->GetOwnerId(), source, frozen, radiusDamage);
    }
  } else {
    health->SetLastDamageWeapon(info.GetWeaponMode(), source, source, radiusDamage);
    if (lethal) {
      health->SetCauseOfDeathWeapon(info.GetWeaponMode(), source, source, frozen, radiusDamage);
    }
  }
}

bool CStateManager::ApplyLocalDamage(const CVector3f& pos, const CVector3f& dir, CActor& damagee,
                                     float damage, TUniqueId source, TUniqueId owner,
                                     const CDamageInfo& damageInfo, bool radiusDamage) {
  CHealthInfo* healthInfo = damagee.HealthInfo();
  if (!healthInfo || damage < 0.0f) {
    return false;
  }

  const float oldHp = healthInfo->GetHP();
  float hp = oldHp;
  if (oldHp <= 0.0f) {
    RecordDamageSource(damagee, source, damageInfo, false, radiusDamage);
    return true;
  }

  float useDamage = damage;
  CPlayer* player = TCastToPtr< CPlayer >(damagee);
  CPatterned* patterned = TCastToPtr< CPatterned >(damagee);
  CScriptDoor* door = TCastToPtr< CScriptDoor >(damagee);

  if (player && player->GetTurretState() != CPlayer::kTS_None) {
    if (player->GetTurretState() != CPlayer::kTS_Active) {
      return false;
    }
    // These inherited turret helpers forward the damage position and test its direction.
    // Their complete interfaces remain unresolved; see the radius-damage research notes.
    player->fn_8000d3ac(pos, *this);
    if (!player->fn_8000d40c(dir, *this)) {
      return false;
    }
  }
  TUniqueId playerId = player ? player->GetUniqueId() : kInvalidUniqueId;
  if (player) {
    int playerIndex = MaskUIdNumPlayers(playerId);
    CPlayerState& playerState = *PlayerState(playerIndex);

    if (GetCameraManager(playerIndex)->IsInCinematicCamera()) {
      return false;
    }

#ifdef TARGET_PC
    // Port: the MP2_GODMODE cheat (F1 > Cheats). Only a player is spared; every other actor
    // still takes its damage. Without it the "[player] killed" line below names the death.
    if (PortOverlay::GodMode()) {
      return false;
    }
#endif
    if (gpGameState->GetHardModeEnabled()) {
      switch (damageInfo.GetWeaponMode().GetRawType()) {
      case kWT_Phazon:
      case kWT_PoisonWater1:
      case kWT_PoisonWater2:
      case kWT_Lava:
      case kWT_Heat:
      case kWT_Unused1:
      case kWT_AreaDark:
      case kWT_AreaLight:
        break;
      default:
        useDamage *= gpGameState->GetHardModeDamageMultiplier();
        break;
      }
    }

    float damageReduction = 0.0f;
    const EWeaponType weaponType = EWeaponType(damageInfo.GetWeaponMode().GetRawType());
    const bool lightDamage = weaponType == kWT_Light;
    const bool darkDamage = weaponType == kWT_Dark;

    if (playerState.HasPowerUp(CPlayerState::kIT_VariaSuit)) {
      damageReduction = player->GetTweakPlayer()->GetVariaSuitDamageReduction();
    }
    if (playerState.HasPowerUp(CPlayerState::kIT_DarkSuit)) {
      float reduction = player->GetTweakPlayer()->GetDarkSuitDamageReduction();
      damageReduction = rstl::max_val(reduction, damageReduction);
    }
    if (playerState.HasPowerUp(CPlayerState::kIT_LightSuit)) {
      float reduction = player->GetTweakPlayer()->GetLightSuitDamageReduction();
      damageReduction = rstl::max_val(reduction, damageReduction);
    }
    if (playerState.GetItemAmount(CPlayerState::kIT_AbsorbAttack, true) != 0) {
      float reduction = 1.5f;
      damageReduction = rstl::max_val(reduction, damageReduction);
    }
    if (playerState.GetItemAmount(CPlayerState::kIT_LightShield, true) != 0 && !darkDamage) {
      float reduction = 0.75f;
      damageReduction = rstl::max_val(reduction, damageReduction);
    }
    if (playerState.GetItemAmount(CPlayerState::kIT_DarkShield, true) != 0 && !lightDamage) {
      float reduction = 0.75f;
      damageReduction = rstl::max_val(reduction, damageReduction);
    }
    hp = playerState.CalculateHealth();
    useDamage = -(damageReduction * useDamage - useDamage);
  }

  const float damagedHp = oldHp - useDamage;
  if (damagedHp < hp) {
    hp = damagedHp;
  }
  healthInfo->SetHP(hp);
  const bool significant = hp < oldHp;
  RecordDamageSource(damagee, source, damageInfo, hp <= 0.f, radiusDamage);

  if (player != nullptr) {
    if (damageInfo.GetWeaponMode().IsInstantKill()) {
      useDamage = hp;
      hp = 0.f;
      healthInfo->SetHP(hp);
    }
    // The native caller snapshots the complete damage record before player reactions.
    player->TakeDamage(significant, pos, useDamage, source, owner, CDamageInfo(damageInfo), *this);
    const CGameState& gameState = *gpGameState;
    gameState.GetGameMode().OnPlayerDamaged(*this, playerId, owner, useDamage);
    if (hp <= 0.f) {
#ifdef TARGET_PC
      // Port: name the death. A dead single player takes CMFGame's own game-over path (PlayerDied
      // -> EndGame -> kRM_EndMovie2 -> CMainFlow's CPlayMovie(kWM_LoseGame) and its "continue from
      // last save" screen), which removes CMFGame from the IO win stack: from here on no game
      // update runs, and without this line a test run reads that as an unexplained stall.
      std::fprintf(stderr,
                   "[player] killed in area %d at update %d by weapon type %d%s (%g energy before)\n",
                   player->GetCurrentAreaId().Value(), mUpdateFrameIdx,
                   int(damageInfo.GetWeaponMode().GetRawType()),
                   damageInfo.GetWeaponMode().GetRawType() == kWT_AreaDark
                       ? " - the dark-world atmosphere: the area is flagged dark and the player is "
                         "in no safe zone"
                       : "",
                   oldHp);
#endif
      KillPlayer(oldHp, playerId, owner);
    }
  } else if (patterned != nullptr) {
    if (significant) {
      patterned->TakeDamage(dir, useDamage);
      hp = patterned->GetHealthInfo()->GetHP();
    }
    if (hp <= 0.f) {
      patterned->Death(*this, dir, kSS_DeathRattle);
    }
  } else if (door != nullptr && significant && hp <= 0.f) {
    door->SetBurnOrigin(pos);
  }
  return significant;
}

void CStateManager::TestBombHittingWater(const CActor& source, const CVector3f& position,
                                         CActor& damagee) {
  int index = 0;
  if (const CWeapon* weapon = TCastToConstPtr< CWeapon >(source)) {
    const int attributes = weapon->GetAttribField();
    if ((attributes & (CWeapon::kPA_TriggerBomb | CWeapon::kPA_PowerBombs)) != 0) {
      if ((attributes & CWeapon::kPA_PowerBombs) != 0) {
        index = 1;
      }
      if (CScriptWater* const water = TCastToPtr< CScriptWater >(damagee)) {
        const CVector3f hitPosition(position.GetX(), position.GetY(),
                                    water->GetTriggerBoundsWR().GetMaxPoint().GetZ());
        const float depth = -water->GetWRSurfacePlane().GetHeight(position);
        if (depth <= skBombUnderwaterRanges[index] && depth > 0.f) {
          const float splashFactor = 1.f - depth / skBombUnderwaterRanges[index];
          if (index == 0) {
            mFluidPlaneManager->CreateSplash(source.GetUniqueId(), *this, *water, hitPosition,
                                             splashFactor, true);
          }
        }
      }
    }
  }
}

const bool CStateManager::MultiRayCollideWorld(const CMRay& ray,
                                               const CMaterialFilter& filter) const {
  CVector3f offset2 =
      CVector3f(ray.GetDirection().GetY(), -ray.GetDirection().GetZ(), ray.GetDirection().GetX());
  CVector3f offset = CVector3f::Cross(offset2, ray.GetDirection()).AsNormalized();
  offset2 = 0.35355338f * CVector3f::Cross(ray.GetDirection(), offset);
  offset *= 0.35355338f;

  bool visible = false;
  for (int i = 0; i < 4; ++i) {
    const CVector3f start =
        ray.GetStart() + ((i & 1) ? offset : -offset) + ((i & 2) ? -offset2 : offset2);
    visible = CGameCollision::RayStaticLineOfSightTest(*this, start, ray.GetDirection(),
                                                       ray.GetLength(), filter);
    if (visible) {
      break;
    }
  }
  return visible;
}

CPlane CScriptWater::GetWRSurfacePlane() const {
  return CPlane(GetTriggerBoundsWR().GetMaxPoint().GetZ(),
                CUnitVector3f(0.f, 0.f, 1.f, CUnitVector3f::kN_Yes));
}

const bool
CStateManager::TestRayDamage(const CVector3f& position, const CActor& damagee,
                             const rstl::reserved_vector< TUniqueId, 1024 >& nearList) const {
  if (damagee.GetHealthInfo() == nullptr) {
    return false;
  }

  static const CMaterialList include = CMaterialList(kMT_Solid);
  static const CMaterialList exclude =
      CMaterialList(kMT_ProjectilePassthrough, kMT_Player, kMT_Occluder, kMT_Character);
  static const CMaterialFilter filter =
      CMaterialFilter(include, exclude, CMaterialFilter::kFT_IncludeExclude);

  const rstl::optional_object< CAABox > bounds = damagee.GetTouchBounds();
  if (!bounds) {
    return false;
  }

  const CVector3f center = bounds->GetCenterPoint();
  CVector3f direction = center - position;
  if (direction.CanBeNormalized()) {
    const float length = direction.Magnitude();
    direction *= 1.f / length;
    if (RayCollideWorld(position, center, nearList, filter, &damagee)) {
      return true;
    }

    const CMRay ray = CMRay(position, direction, length);
    if (!MultiRayCollideWorld(ray, filter)) {
      return false;
    }

    float depth;
    CVector3f normal = CVector3f::Zero();
    const int count = CollisionUtil::RayAABoxIntersection(ray, *bounds, normal, depth);
    if (count == 0) {
      return true;
    }
    if (count == 1) {
      return true;
    }
    return CGameCollision::RayDynamicLineOfSightTest(*this, position, direction, depth * length,
                                                     filter, nearList, &damagee);
  }
  return true;
}

void CStateManager::ApplyRadiusDamage(const CActor& source, const CVector3f& position,
                                      CActor& damagee, TUniqueId owner, const CDamageInfo& damage) {
  const CDamageInfo info(
      GetModifiedDamageInfo(source.GetUniqueId(), owner, damagee.GetUniqueId(), damage));
  CVector3f delta = damagee.GetTranslation() - position;
  if (!(delta.MagSquared() < info.GetRadius() * info.GetRadius())) {
    if (!damagee.GetTouchBounds()) {
      return;
    }
    if (!CCollidableSphere::Sphere_AABox_Bool(CSphere(position, info.GetRadius()),
                                              *damagee.GetTouchBounds())) {
      return;
    }
  }

  float radius = info.GetRadius();
  radius = radius > FLT_EPSILON ? delta.Magnitude() / radius : 0.f;
  radius = rstl::min_val(radius, 1.f);
  if (radius > 0.f) {
    delta.Normalize();
  }

  const CDamageVulnerability* vulnerability =
      radius > 0.f ? damagee.GetDamageVulnerability(position, delta, info)
                   : damagee.GetDamageVulnerability();
  if (vulnerability->WeaponHits(info.GetWeaponMode(), 1)) {
    const float localDamage = info.GetRadiusDamage(*vulnerability);
    if (localDamage > 0.f) {
      ApplyLocalDamage(position, delta, damagee, localDamage, source.GetUniqueId(), owner, info, 1);
    }
    SendDamageScriptMsgs(damagee, source.GetUniqueId(), info);
    SendScriptMsg(&damagee, source.GetUniqueId(), kSM_Damage, kInvalidUniqueId);
  } else {
    damagee.SendScriptMsgs(kSS_ResistedDamage, *this, kInvalidUniqueId, kSM_None);
    SendScriptMsg(&damagee, source.GetUniqueId(), kSM_ResistedDamage, kInvalidUniqueId);
  }

  const CVector3f knockbackDelta =
      damagee.GetTransform().GetTranslation() - source.GetTransform().GetTranslation();
  const CVector3f knockbackDirection(knockbackDelta.GetX(), knockbackDelta.GetY(), 0.0001f);
  ApplyKnockBack(damagee, source.GetUniqueId(), owner, info, *vulnerability,
                 knockbackDirection.AsNormalized(), radius);
}

void CStateManager::ProcessRadiusDamage(const CActor& source, CActor& damagee, TUniqueId owner,
                                        const CDamageInfo& damage, const CMaterialFilter& filter) {
  CMaterialFilter localFilter = filter;
  const TUniqueId sourceId = source.GetUniqueId();
  const TUniqueId damageeId = damagee.GetUniqueId();
  const float radius = damage.GetRadius();
  const CVector3f position(source.GetTranslation());
  const float negativeRadius = -radius;
  const CAABox bounds(position + CVector3f(negativeRadius, negativeRadius, negativeRadius),
                      position + CVector3f(radius, radius, radius));

  rstl::reserved_vector< TUniqueId, 1024 > nearList;
  BuildNearList(nearList, bounds, localFilter, nullptr);
  for (rstl::reserved_vector< TUniqueId, 1024 >::iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    CActor* actor = static_cast< CActor* >(ObjectById(*it));
    if (actor != nullptr) {
      const TUniqueId actorId = actor->GetUniqueId();
      if (sourceId != actorId && owner != actorId && damageeId != actorId) {
        TestBombHittingWater(source, position, *actor);
        if (TestRayDamage(position, *actor, nearList)) {
          ApplyRadiusDamage(source, position, *actor, owner, damage);
        }
      }
    }
  }
}

void CStateManager::ApplyDamageToWorld(TUniqueId owner, CActor& projectile,
                                       const CVector3f& position, const CDamageInfo& info,
                                       const CMaterialFilter& filter) {
  const CMaterialFilter useFilter = filter;
  const float radius = info.GetRadius();
  const float negativeRadius = -radius;
  const CAABox bounds(position + CVector3f(negativeRadius, negativeRadius, negativeRadius),
                      position + CVector3f(radius, radius, radius));

  const CWeapon* const weapon = TCastToConstPtr< CWeapon >(&projectile);
  bool bomb = false;
  if (weapon != nullptr) {
    bomb =
        weapon->HasAttrib(CWeapon::kPA_TriggerBomb) || weapon->HasAttrib(CWeapon::kPA_PowerBombs);
  }

  rstl::reserved_vector< TUniqueId, 1024 > nearList;
  BuildNearList(nearList, bounds, useFilter, &projectile);
  for (rstl::reserved_vector< TUniqueId, 1024 >::iterator it = nearList.begin();
       it != nearList.end(); ++it) {
    CActor* const actor = static_cast< CActor* >(ObjectById(*it));
    CPlayer* const player = TCastToPtr< CPlayer >(actor);
    CSnakeWeedSwarm* const snakeWeed = TCastToPtr< CSnakeWeedSwarm >(actor);
    CSwarmBasics* const swarm = TCastToPtr< CSwarmBasics >(actor);

    if (bomb && player != nullptr && actor->GetUniqueId() == weapon->GetOwnerId()) {
      if (player->GetFrozenState()) {
        CEnvironmentVariable* const freezeInstructions =
            gpGameState->SystemOptions().EnvVars().FindEnvironmentVariable(
                "FreezeInstructionsMorphBall");
        freezeInstructions->Set(freezeInstructions->GetValue() + 1);
        const int playerIndex = MaskUIdNumPlayers(player->GetUniqueId());
        CSamusHud::DisplayHudMemo(rstl::wstring_l(L""),
                                  CHUDMemoParms(0.f, true, true, true, 1 << playerIndex, true));
        player->BreakFrozenState(*this, CPlayer::kBFS_BreakWithEffects, false);
      } else if (weapon->HasAttrib(CWeapon::kPA_TriggerBomb)) {
        player->BombJump(position, *this);
      }
    } else if (actor != nullptr && actor->GetUniqueId() != owner) {
      TestBombHittingWater(projectile, position, *actor);
      if (TestRayDamage(position, *actor, nearList)) {
        ApplyRadiusDamage(projectile, position, *actor, owner, info);
      }
    }

    if (snakeWeed != nullptr) {
      SnakeWeed_ApplyRadiusDamage(*snakeWeed, position, info, *this);
    }
    if (swarm != nullptr) {
      swarm->ApplyRadiusDamage(position, info, *this);
    }
  }
}

void CStateManager::ApplyKnockBack(CActor& actor, TUniqueId source, TUniqueId owner,
                                   const CDamageInfo& damage,
                                   const CDamageVulnerability& vulnerability,
                                   const CVector3f& direction, float dampen) {
  const CWeaponTypeVulnerability weaponVulnerability =
      vulnerability.GetVulnerability(damage.GetWeaponMode());
  if (!weaponVulnerability.WeaponHurts()) {
    return;
  }

  const CHealthInfo* health = actor.GetHealthInfo();
  if (health == nullptr) {
    return;
  }

  const float power = (1.f - dampen) * damage.GetKnockBackPower();
  const float resistance = health->GetKnockBackResistance();
  CPlayer* player = TCastToPtr< CPlayer >(actor);
  CPatterned* patterned = TCastToPtr< CPatterned >(actor);
  const bool alive = health->GetHP() > 0.f;
  const CKnockBackInfo info(direction, source, owner, damage, dampen == 0.f);
  if (player != nullptr) {
    player->GetKnockBackManager().KnockBack(*this, *player, info);
    return;
  }

  if (patterned == nullptr && !alive) {
    if (power > resistance) {
      if (CPhysicsActor* const physics = TCastToPtr< CPhysicsActor >(actor)) {
        const CVector3f impulse = direction * (1.5f * ((power - resistance) * physics->GetMass()));
        if (!physics->GetMaterialList().HasMaterial(kMT_Immovable) &&
            physics->GetMaterialList().HasMaterial(kMT_Solid)) {
          physics->ApplyImpulseWR(impulse, CAxisAngle::Identity());
        }
      }
    }
  } else if (patterned != nullptr) {
    patterned->KnockBack(*this, info);
  }
}

void CStateManager::InformListeners(const CVector3f& position, EListenNoiseType type) {
  CObjectList* list = mObjectLists[kOL_ListeningAi].get();
  for (int i = list->GetFirstObjectIndex(); i != -1; i = list->GetNextObjectIndex(i)) {
    CPatterned* patterned = TCastToPtr< CPatterned >((*list)[i]);
    if (patterned != nullptr && patterned->GetActive()) {
      CGameArea* area = mWorld->Area(patterned->GetCurrentAreaId());
      if (area->GetOcclusionState() != CGameArea::kOS_Occluded) {
        patterned->Listen(*this, position, type);
      }
    }
  }
}

CStateManager::TIdListResult CStateManager::GetIdListForScript(TEditorId editorId) const {
  const TIdListResult range = mScriptIdMap.equal_range(editorId);
  return range;
}

TUniqueId CStateManager::GetIdForScript(TEditorId editorId) const {
  TIdList::const_iterator it = mScriptIdMap.find(editorId);
  if (it != mScriptIdMap.end()) {
    return it->second;
  }
  return kInvalidUniqueId;
}

TEditorId CStateManager::GetEditorIdForUniqueId(TUniqueId uid) const {
  const CEntity* entity = GetObjectById(uid);
  if (entity != nullptr) {
    return entity->GetEditorId();
  }
  return kInvalidEditorId;
}

void CStateManager::EndPlayerRender() {
  mCurrentRenderPlayerIndex = kInvalidRenderPlayerIndex;
  mCurrentRenderPlayer = nullptr;
  mPlayerState = nullptr;
  mCameraManager = nullptr;
}

void CStateManager::AddToGraveyard(CEntity* entity) {
  if (mGraveyard.empty()) {
    rstl::reserved_vector< CEntity*, 32 > batch;
    mGraveyard.push_back(batch);
  } else if (mGraveyard.back().size() == mGraveyard.back().capacity()) {
    rstl::reserved_vector< CEntity*, 32 > batch;
    mGraveyard.push_back(batch);
  }

  mGraveyard.back().push_back(entity);
}

void CStateManager::DispatchScriptMessages() {
  while (!mScriptMsgs.empty()) {
    CScriptMsg msg = mScriptMsgs.Dequeue();
    CEntity* ent = ObjectById(msg.GetId());
    if (ent) {
      const bool wasActive = ent->GetActive();
      ent->AcceptScriptMsg(*this, msg);
#ifdef TARGET_PC
      if (msg.GetState() != kSS_InvalidState) {
        PortSaveState::NoteScriptMsg(*this, ent->GetUniqueId().value, msg.GetSenderId().value,
                                     static_cast< uint32_t >(msg.GetMessage()));
      }
#endif
      if (wasActive != ent->GetActive()) {
        if (CActor* actor = TCastToPtr< CActor >(ent)) {
          UpdateActorInSortedLists(actor);
        }
      }

      if (msg.GetMessage() == kSM_Delete) {
        AddToGraveyard(ent);
        RemoveObject(ent->GetUniqueId());
      }
    }
  }
}

void CStateManager::ThinkNewObjects(float dt) {
  for (;;) {
    DispatchScriptMessages();
    if (mNewObjectIds.empty()) {
      return;
    }

    rstl::list< TUniqueId >::iterator it = mNewObjectIds.begin();
    while (it != mNewObjectIds.end()) {
      if (CEntity* entity = ObjectById(*it)) {
        ThinkEntity(dt, *entity);
      }
      it = mNewObjectIds.erase(it);
    }
  }
}

void CStateManager::DeferStateTransition(EStateManagerTransition t) {
  if (!IsMultiplayer()) {
    if (t == kSMT_InGame) {
      if (mDeferredTransition != kSMT_InGame) {
        mWorld->SetLoadPauseState(false);
        mDeferredTransition = kSMT_InGame;
      }
    } else if (mDeferredTransition == kSMT_InGame) {
      mWorld->SetLoadPauseState(true);
      mDeferredTransition = t;
      if (mDeferredTransition == kSMT_SaveGame) {
        mSaveGameScreen = rs_new CSaveGameScreen(kSC_InGame, gpGameState->GetCardSerial());
      }
    }
  }
}

void CStateManager::ShowPausedHUDMemo(CAssetId strg, float time) {
  mHudMessageTime = time;
  mPauseHudMessage = strg;
  DeferStateTransition(kSMT_MessageScreen);
}

void CStateManager::SendScriptMsg(const CScriptMsg& msg) {
#ifdef TARGET_PC
  if (std::getenv("MP2_DEBUG_BALL") != nullptr && msg.GetMessage() == kSM_SetToZero) {
    std::fprintf(stderr, "[ball] queue SetToZero -> %04x from %04x (depth %d)\n",
                 msg.GetId().Value(), msg.GetSenderId().Value(), int(mScriptMsgs.GetCount()));
  }
#endif
  mScriptMsgs.Append(msg);
  int v = mScriptMsgs.GetCount();
  if (0x80 < v && !mDispatchingScriptMessages) {
    mDispatchingScriptMessages = true;
    DispatchScriptMessages();
    mDispatchingScriptMessages = false;
  }
}

#ifdef TARGET_PC
// Console `mpforce`: makes IsMultiplayer report true so the multiplayer arms run in single player.
bool gPortForceMultiplayer = false;
#endif

bool CStateManager::IsMultiplayer() const {
#ifdef TARGET_PC
  if (gPortForceMultiplayer) {
    return true;
  }
#endif
  int v = gpGameState->GetGameMode().GetGameModeType();
  return v != 'SNGL' && v != 'FRND';
}

void CStateManager::MovePlatforms(float dt) {
  CObjectList* platformList = mObjectLists[kOL_Platform].get();
  for (int i = platformList->GetFirstObjectIndex(); i != -1;
       i = platformList->GetNextObjectIndex(i)) {
    CPhysicsActor* actor = static_cast< CPhysicsActor* >((*platformList)[i]);
    if (actor != nullptr && actor->GetActive() && actor->GetMass() != 0.f) {
      CGameCollision::Move(*this, *actor, dt, nullptr);
    }
  }
}

void CStateManager::MoveActors(float dt) {
  CObjectList* physicsList = mObjectLists[kOL_PhysicsActor].get();
  for (int i = physicsList->GetFirstObjectIndex(); i != -1;
       i = physicsList->GetNextObjectIndex(i)) {
    CPhysicsActor* actor = static_cast< CPhysicsActor* >((*physicsList)[i]);
    if (actor == nullptr || !actor->GetActive() || actor->GetMass() == 0.f ||
        (!actor->GetUpdateDuringCinematicSkip() && gpMain->IsMaxSpeed())) {
      continue;
    }

    if (!actor->GetUpdateWhileOccluded() && actor->GetCurrentAreaId() != kInvalidAreaId) {
      const CGameArea& area = mWorld->GetAreaAlways(actor->GetCurrentAreaId());
      const float occludedTime = area.IsLoaded() ? area.GetPostConstructed()->mOccludedTime : 0.f;
      if (occludedTime > 5.f) {
        continue;
      }
    }

    CPatterned* patterned = TCastToPtr< CPatterned >(actor);
    if (patterned != nullptr && !ShouldUpdatePatterned(*patterned)) {
      SendScriptMsg(patterned->GetUniqueId(), kInvalidUniqueId, kSM_AIUpdateDisabled,
                    kInvalidUniqueId);
      continue;
    }

    if (TCastToPtr< CPlayer >(actor) == nullptr &&
        TCastToPtr< CScriptPlatform >(actor) == nullptr) {
      CGameCollision::Move(*this, *actor, dt, nullptr);
    }
  }
}

void CStateManager::CrossTouchActors() {
  CObjectList* actorList = mObjectLists[kOL_Actor].get();
  bool visits[kMaxObjects];
  memset(visits, 0, sizeof(visits));

  for (int i = actorList->GetFirstObjectIndex(); i != -1; i = actorList->GetNextObjectIndex(i)) {
    CActor* actor = static_cast< CActor* >((*actorList)[i]);
    if (actor != nullptr && actor->GetActive() && actor->GetCallTouch()) {
      const rstl::optional_object< CAABox > touchBounds = actor->GetTouchBounds();
      if (!touchBounds) {
        continue;
      }
      if (!actor->GetUpdateDuringCinematicSkip() && gpMain->IsMaxSpeed()) {
        continue;
      }

      rstl::reserved_vector< TUniqueId, kMaxObjects > nearList;
      const CMaterialFilter filter = actor->GetMaterialList().HasMaterial(kMT_Trigger) &&
                                             TCastToPtr< CMetroid >(actor) == nullptr
                                         ? CMaterialFilter::MakeExclude(CMaterialList(kMT_Trigger))
                                         : CMaterialFilter::GetPassEverything();
      BuildNearList(nearList, *touchBounds, filter, actor);

      for (const TUniqueId* uid = nearList.begin(); uid != nearList.end(); ++uid) {
        CActor* other = static_cast< CActor* >(ObjectById(*uid));
        if (other != nullptr) {
          const rstl::optional_object< CAABox > otherBounds = other->GetTouchBounds();
          if (!other->GetActive() || !otherBounds) {
            continue;
          }

          if (!visits[other->GetUniqueId().Value()]) {
            if (touchBounds->DoBoundsOverlap(*otherBounds)) {
              actor->Touch(*other, *this);
              other->Touch(*actor, *this);
            }
            visits[actor->GetUniqueId().Value()] = true;
          }
        }
      }
    }
  }
}

void CStateManager::ThinkEntity(float dt, CEntity& entity) { entity.Think(dt, *this); }

bool CStateManager::ShouldUpdatePatterned(const CPatterned& actor) {
  bool update = !mCinematicPause;
  if (update && actor.GetCurrentAreaId() != kInvalidAreaId) {
    const CGameArea& area = mWorld->GetAreaAlways(actor.GetCurrentAreaId());
    const float occludedTime = area.IsLoaded() ? area.GetPostConstructed()->mOccludedTime : 0.f;
    if (occludedTime > 5.f) {
      update = false;
    }
  }
  return update;
}

void CStateManager::Think(float dt) {
  if (!IsMultiplayer() && mPlayers[0]->GetDeathTime() > 0.f) {
    mPlayers[0]->DoThink(dt, *this);
    return;
  }

  CObjectList* allList = mObjectLists[kOL_All].get();
  if (mGameState == kGS_SoftPaused) {
    for (int i = allList->GetFirstObjectIndex(); i != -1; i = allList->GetNextObjectIndex(i)) {
      CScriptEffect* effect = TCastToPtr< CScriptEffect >((*allList)[i]);
      if (effect != nullptr) {
        effect->Think(dt, *this);
      }
    }
  } else {
    for (long i = allList->GetFirstObjectIndex(); i != -1; i = allList->GetNextObjectIndex(i)) {
      CEntity* entity = (*allList)[i];
      if (entity == nullptr || (!entity->GetUpdateDuringCinematicSkip() && gpMain->IsMaxSpeed())) {
        continue;
      }

      if (!entity->GetUpdateWhileOccluded() && entity->GetCurrentAreaId() != kInvalidAreaId) {
        const CGameArea& area = mWorld->GetAreaAlways(entity->GetCurrentAreaId());
        const float occludedTime = area.IsLoaded() ? area.GetPostConstructed()->mOccludedTime : 0.f;
        if (occludedTime > 5.f) {
          continue;
        }
      }

      CPatterned* patterned = TCastToPtr< CPatterned >((*allList)[i]);
      if (patterned != nullptr && !ShouldUpdatePatterned(*patterned)) {
        continue;
      }
      if (TCastToPtr< CGameCamera >(entity) == nullptr) {
        ThinkEntity(dt, *entity);
      }
    }
  }
}

void CStateManager::PreThinkObjects(float dt) {
  if (!IsMultiplayer() && mPlayers[0]->GetDeathTime() > 0.f) {
    mPlayers[0]->DoPreThink(dt, *this);
    return;
  }

  CObjectList* allList = mObjectLists[kOL_All].get();
  if (mGameState == kGS_SoftPaused) {
    for (int i = allList->GetFirstObjectIndex(); i != -1; i = allList->GetNextObjectIndex(i)) {
      CScriptEffect* effect = TCastToPtr< CScriptEffect >((*allList)[i]);
      if (effect != nullptr) {
        effect->PreThink(dt, *this);
      }
    }
  } else {
    for (int i = allList->GetFirstObjectIndex(); i != -1; i = allList->GetNextObjectIndex(i)) {
      CEntity* entity = (*allList)[i];
      if (entity != nullptr && TCastToPtr< CGameCamera >(entity) == nullptr) {
        entity->PreThink(dt, *this);
      }
    }
  }
}

void CStateManager::PostUpdatePlayer(float dt) {
  for (uint i = 0; i < mNumPlayers; ++i) {
    mPlayers[i]->PostUpdate(dt, *this);
  }
}

void CStateManager::ProcessPlayerInput() {
  if (!gpMain->IsMaxSpeed() && !mCameraManagers[0]->IsInFullScreenCinematic()) {
    for (uint i = 0; i < mNumPlayers; ++i) {
      const uint controller = gpGameState->GetPlayerState(i)->GetPlayerSelection();
      mPlayers[i]->ProcessInput(mFinalInputs[controller], *this);
    }
  }
}
#ifdef TARGET_PC
// Toggle lock-on and sticky charge (F1 > Gameplay). Edges come from the combined digital/analog
// state, since a GameCube L passes the analog threshold and clicks on different ticks and would
// toggle twice. There is one local player's pad, so the state machines are file statics; a second
// controller that owns no player is ignored rather than allowed to reset them.
static void ApplyHoldToggle(CFinalInput& input, CStateManager& mgr, uint controller, bool enabled) {
  static PortHoldToggle::Toggle sLockOn;
  static PortHoldToggle::Sticky sCharge;
  static bool sPrevL = false;

  // A controller index is not a player slot: ProcessPlayerInput maps a slot's selection the other
  // way round, so it is the selection that finds the player this pad belongs to.
  const CPlayer* player = nullptr;
  for (uint i = 0; i < mgr.GetNumPlayers(); ++i) {
    if (mgr.GetPlayerState(i)->GetPlayerSelection() == controller) {
      player = mgr.GetPlayer(i);
      break;
    }
  }
  if (player == nullptr) {
    return;
  }

  // Only while gameplay is running: the scan display's soft pause (CPlayer sets kGS_SoftPaused)
  // still reaches ProcessInput, and a beam that kept charging into it would be held by a button
  // nobody is holding when play resumes. The pause and map screens stop game updates altogether,
  // so nothing runs there and a latch resumes with the game - the next press lets go.
  const bool unmorphed = enabled && mgr.GetGameState() == CStateManager::kGS_Running &&
                         player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed;

  const bool heldL = input.DL() || input.DLTrigger();
  const bool pressL = heldL && !sPrevL;
  sPrevL = heldL;
  // Only a lock on an object counts: a killed target leaves the player orbiting its carcass, and
  // a lost look angle a fixed point, both of which hold the aim in place while L is held.
  const CPlayer::EPlayerOrbitState orbit = player->GetOrbitState();
  const bool locked = orbit == CPlayer::kOS_OrbitObject ||
                      orbit == CPlayer::kOS_ForcedOrbitObject || orbit == CPlayer::kOS_Grapple;
  const PortHoldToggle::Output outL = sLockOn.Update(
      unmorphed && PortOverlay::LockOnToggle(), heldL, pressL, locked, input.DeltaTime());
  if (sLockOn.Latched() || outL.held != heldL) {
    input.PortSetL(outL.held, outL.pressed);
  }

  const bool heldA = input.DA();
  const bool pressA = input.PA();
  const CPlayerState* const state = player->GetPlayerState();
  const bool charge = unmorphed && PortOverlay::StickyCharge() && state != nullptr &&
                      state->GetItemAmount(CPlayerState::kIT_ChargeBeam, true) != 0;
  const PortHoldToggle::Output outA = sCharge.Update(charge, heldA, pressA, input.DeltaTime());
  if (outA.held != heldA || outA.pressed != pressA) {
    input.PortSetA(outA.held, outA.pressed);
  }
}
#endif

void CStateManager::ProcessInput(const CFinalInput& input) {
  uint player = input.ControllerNumber();
  if (player < 4) {
    bool disableInput = false;
    if (CMemoryCardSys::mIsCardBusy) {
      disableInput = true;
    }
    if (!mSaveGameScreen.null() &&
        mSaveGameScreen->GetUIType() != CSaveGameScreen::kUIT_SaveReady &&
        mSaveGameScreen->GetUIType() != CSaveGameScreen::kUIT_Empty) {
      disableInput = true;
    }
    if (disableInput) {
      mFinalInputs[player] = CFinalInput();
    } else {
      mFinalInputs[player] = input;
    }
#ifdef TARGET_PC
    ApplyHoldToggle(mFinalInputs[player], *this, player, !disableInput);
#endif

    if (mNumPlayers == 1) {
      player = 0;
      if (gpGameState->ControlMapper().GetPressInput(CControlMapper::kC_MapScreen, input) &&
          input.ControllerNumber() == 0 && CanEnterMapScreen()) {
        DeferStateTransition(kSMT_MapScreen);
      }
#ifdef TARGET_PC
      // Port: a tap or click on the HUD minimap opens the map as the button does.
      else if (input.ControllerNumber() == 0 && PortMinimap::TakeTap() && CanEnterMapScreen()) {
        DeferStateTransition(kSMT_MapScreen);
      }
#endif
    }
    if (mNumPlayers == 2 && player >= mNumPlayers) {
      player -= 2;
    }
    if (player < mNumPlayers) {
      mCameraManagers[player]->ProcessInput(input, *this);
    }
  }
}

#ifdef TARGET_PC
namespace {
// Unload every area except the two a warp has to spare: the one the player is in and the one being
// streamed. `CWorld::UnloadAllAreasExcept` spares a single area, and using it with the current area
// alone would invalidate the *destination* mid-stream - which is not merely wasteful, it is the
// opposite of what the call is for: `CGameArea::Invalidate` finishes a load that has reached its
// script objects and then throws the objects away, so the warp would cancel and restart its own
// stream. Retries the areas that cannot be invalidated yet (`Invalidate` returns false while their
// transactions or dependencies are still in flight).
bool PortUnloadAreasExcept(CStateManager& mgr, CWorld& world, TAreaId keepA, TAreaId keepB) {
  CFrameDelayedKiller::StallAndFlushAllAllocations();
  bool failed = false;
  for (int i = 0; i < world.IGetAreaCount(); ++i) {
    const TAreaId id(i);
    CGameArea* area = world.Area(id);
    if (area == nullptr || area->GetId() == keepA || area->GetId() == keepB) {
      continue;
    }
    if (area->GetCurChain() == CWorld::kC_Deallocated) {
      continue;
    }
    if (area->Invalidate(&mgr)) {
      world.MoveToChain(area, CWorld::kC_Deallocated);
    } else {
      failed = true;
    }
  }
  return !failed;
}
} // namespace
#endif

#ifdef TARGET_PC
// Discord Rich Presence (platform/port_discord.cpp): the area on top, energy, missiles and item
// percentage below, so it is fed from the game's own names, not the disc's editor labels.
namespace {
struct SPortDiscordNames {
  CAssetId worldId = kInvalidAssetId;
  CAssetId areaId = kInvalidAssetId;
  std::string world;
  std::string area;
  TToken< CStringTable >* worldToken = nullptr;
  TToken< CStringTable >* areaToken = nullptr;
  int wait = 0;
  int loading = 0;
};

SPortDiscordNames* sPortDiscordNames = nullptr;

// Starts loading `id` into `token` when it changed; fills `name` once loaded. Returns false while
// it is still loading.
bool PortDiscordName(CAssetId id, CAssetId& loadedId, TToken< CStringTable >*& token,
                     std::string& name) {
  if (id != loadedId) {
    loadedId = id;
    name.clear();
    delete token;
    token = nullptr;
    if (id != kInvalidAssetId && gpSimplePool != nullptr) {
      token = rs_new TToken< CStringTable >(gpSimplePool->GetObj(SObjectTag('STRG', id)));
      token->Lock();
    }
  }
  if (token == nullptr) {
    return true;
  }
  if (!token->IsLoaded()) {
    return false;
  }
  const CStringTable* table = **token;
  if (name.empty() && table != nullptr && table->GetStringCount() > 0) {
    name = PortDiscord::GameTextToUtf8(table->GetString(0));
  }
  return true;
}

void PortDiscordTick(const CStateManager& mgr) {
  if (!PortDiscord::Enabled()) {
    return;
  }
  if (sPortDiscordNames == nullptr) {
    sPortDiscordNames = new SPortDiscordNames;
  }
  SPortDiscordNames& names = *sPortDiscordNames;
  // Twice a second is plenty: Discord takes an update every few seconds at most.
  if (names.wait-- > 0) {
    return;
  }
  names.wait = 30;
  const CWorld* world = mgr.GetWorld();
  const CPlayerState* playerState = mgr.GetPlayerState();
  if (world == nullptr || playerState == nullptr) {
    return;
  }
  const TAreaId areaId = world->GetCurrentAreaId();
  const IGameArea* area = areaId.Value() >= 0 ? world->IGetAreaAlways(areaId) : nullptr;
  const bool worldReady =
      PortDiscordName(world->IGetStringTableAssetId(), names.worldId, names.worldToken, names.world);
  const bool areaReady = PortDiscordName(
      area != nullptr ? area->IGetStringTableAssetId() : kInvalidAssetId, names.areaId,
      names.areaToken, names.area);
  // Keep the last line up while a new name loads, but not forever.
  if (!(worldReady && areaReady) && names.loading++ < 10) {
    return;
  }
  names.loading = 0;
  PortDiscord::GameInfo info;
  info.world = names.world;
  info.area = names.area;
  info.worldId = static_cast< uint32_t >(world->IGetWorldAssetId());
  info.percent = playerState->GetItemPercentageRatio();
  info.hard = gpGameState != nullptr && gpGameState->GetHardModeEnabled();
  // Rounded up, so a sliver of energy doesn't read as 0.
  const float hp = playerState->GetHealthInfo().GetHP();
  info.energy = static_cast< int >(hp);
  if (static_cast< float >(info.energy) < hp) {
    ++info.energy;
  }
  if (playerState->GetItemCapacity(CPlayerState::kIT_Missile) > 0) {
    info.missiles = playerState->GetItemAmount(CPlayerState::kIT_Missile);
  }
  PortDiscord::SetGame(info);
}
} // namespace
#endif

void CStateManager::Update(float inputDt, CArchitectureQueue& queue) {
  mArchQueue = &queue;
#ifdef TARGET_PC
  // Port: the free camera can hold the simulation still while it flies.
  if (PortFreeCam::Frozen()) {
    return;
  }
#endif
  float dt = inputDt;
  float cameraDt = inputDt;
  if (mCameraManagers[0]->IsInCinematicCamera()) {
    const CCinematicCamera* camera = mCameraManagers[0]->GetCinematicCamera();
    if ((camera->GetFlags() & 0x200) != 0) {
      dt *= camera->GetSlowMotionScale();
    }
    if (gpMain->IsMaxSpeed()
#ifdef TARGET_PC
        // Port: Rotating rooms = Fast runs the energy controller rooms' cutscene at the
        // game's own double speed.
        || (PortOverlay::RotatingRooms() == PortOverlay::kRotatingRooms_Fast &&
            PortOverlay::RotatingSequenceActive(mUpdateFrameIdx))
#endif
    ) {
      dt *= 2.f;
      cameraDt *= 2.f;
    }
  }

  CElementGen::SetGlobalSeed(mUpdateFrameIdx);
  CParticleElectric::SetGlobalSeed(mUpdateFrameIdx);
  CParticleSpawnSystem::SetGlobalSeed(mUpdateFrameIdx);
  CSortedParticleSystem::SetGlobalSeed(mUpdateFrameIdx);
  CDecal::SetGlobalSeed(mUpdateFrameIdx);
  CProjectileWeapon::SetGlobalSeed(mUpdateFrameIdx);
#ifdef TARGET_PC
  // A warp request from the platform (env, hotkey or MP2_INPUT token) is served here, once
  // the world exists; a world reload ends the update early through QuitGame.
  PortWarpTick();
  if (PortSaveState::ConsumeSkipUpdate()) {
    // a room state was just applied: the world does not advance this update; only the new objects
    // get their first Think and the dead are freed
    ThinkNewObjects(dt);
    ClearGraveyard();
    return;
  }
  PortWarpSettle();
  PortLayerTick();
  PortPlaceArrivalTick();
  PortDarkWorldTick();
  // MP2_FRAMES: a test run ends itself after so many game updates, so a load test costs seconds
  // instead of a timeout (see platform/include/port_harness.h). The report names where the run got
  // to, which is what a log's last line should say.
  if (PortHarness::GameUpdate()) {
    const CWorld* harnessWorld = World();
    fprintf(stderr, "[harness] the frame limit is up at %.1f s: world %08x, area %d of %d\n",
            PortHarness::Seconds(),
            gpGameState != nullptr ? gpGameState->CurrentWorldAssetId() : 0,
            harnessWorld != nullptr ? harnessWorld->GetCurrentAreaId().Value() : -1,
            harnessWorld != nullptr ? harnessWorld->IGetAreaCount() : 0);
  }
  // MP2_SKIP_INTRO: playing = running, the boot warp served and landed, no cinematic.
  PortHarness::NotePlaying(mGameState == kGS_Running && !PortWarp::Pending() &&
                           mPortPendingWarpArea == kInvalidAreaId &&
                           mPortSpawnAtArea == kInvalidAreaId &&
                           mPortWarpQueuedArea == kInvalidAreaId &&
                           !mCameraManagers[0]->IsInCinematicCamera());
  // The input contexts read the scan text's freeze from here (issue #11).
  PortTouch::SetScanTextUp(mGameState == kGS_SoftPaused);
  if (mPortDropAreasAtFrame != 0 && mUpdateFrameIdx >= mPortDropAreasAtFrame) {
    mPortDropAreasAtFrame = 0;
    if (CWorld* dropWorld = World(); dropWorld != nullptr) {
      const TAreaId keep = dropWorld->GetCurrentAreaId();
      // Spare the pending area too: a request the player made while this drop was waiting may have
      // started streaming it, and dropping it would cancel that stream (see PortUnloadAreasExcept).
      if (PortUnloadAreasExcept(*this, *dropWorld, keep, mPortPendingWarpArea)) {
        std::fprintf(stderr, "[warp] dropped the areas around %d (%d object ids free)\n",
                     keep.Value(), PortFreeObjectIds());
      }
    }
  }
  // Live object and unique-id counts, opt-in with MP2_DEBUG_IDS=1. The id pool holds 1024 and
  // AllocateUniqueId reports "Object list full!" once it is exhausted, so a run that walks many
  // areas needs to show whether objects are freed as areas unload.
  {
    static const bool sLogIds = std::getenv("MP2_DEBUG_IDS") != nullptr;
    if (sLogIds && (mUpdateFrameIdx % 300) == 0) {
      int liveIds = 0;
      for (int i = 0; i < mAllocatedObjectIndices.size(); ++i) {
        if (mAllocatedObjectIndices[i]) {
          ++liveIds;
        }
      }
      std::fprintf(stderr, "[ids] frame %u: %d/%d ids allocated, %d objects\n", mUpdateFrameIdx,
                   liveIds, static_cast< int >(mAllocatedObjectIndices.size()),
                   mObjectLists[kOL_All]->size());
    }
  }
  // The debug overlay's readout (world, area), cheap enough for every update. The log line on
  // each change is what makes a transition test readable: it says whether a door or a dock
  // actually moved the player to another area.
  if (CWorld* w = World(); w != nullptr) {
    const int areaNow = w->GetCurrentAreaId().Value();
    static int sLastArea = -2;
    if (areaNow != sLastArea) {
      sLastArea = areaNow;
      std::fprintf(stderr, "[area] %d\n", areaNow);
    }
    PortOverlay::SetGameState(gpGameState->CurrentWorldAssetId(), areaNow, w);
    PortOverlay::SetAreaLayers(mCurrentWorldLayerState.GetPtr(), areaNow);
    // Discord Rich Presence: the area, energy, missiles and item percentage shown on the player's
    // profile while the desktop Discord client runs (platform/port_discord.cpp).
    PortDiscordTick(*this);
  } else {
    // A world that is gone (a world teleport's restart) must not leave the overlay drawing the
    // room list of a freed world: the publish clears its snapshot.
    PortOverlay::SetGameState(0u, -1, nullptr);
  }
  // Actor transform smoothing: record every actor's transform before this tick moves it
  // (CActor::PortPresentedView blends from it). After the free camera's freeze, so a frozen world
  // keeps its snapshots stale and draws unblended.
  {
    CObjectList* actors = mObjectLists[kOL_Actor].get();
    for (int idx = actors->GetFirstObjectIndex(); idx != -1;
         idx = actors->GetNextObjectIndex(idx)) {
      if (CActor* act = static_cast< CActor* >((*actors)[idx]))
        act->PortSnapshotRenderTransform();
    }
    // And each held gun's pose relative to its camera (CPlayerGun::PortGunView blends from it).
    for (uint i = 0; i < mNumPlayers; ++i) {
      if (mPlayers[i] != nullptr && mPlayers[i]->GetPlayerGun() != nullptr)
        mPlayers[i]->GetPlayerGun()->PortSnapshotPresentedPose(*this);
    }
  }
#endif
#ifdef TARGET_PC
  sPortWorldTimeDt = dt;
  sPortWorldTimeGeneration = CActor::PortTickGeneration();
#endif
  mCurTimeMod900 += dt;
  if (mCurTimeMod900 > 900.f) {
    mCurTimeMod900 -= 900.f;
  }
  mPauseHudMessage = kInvalidAssetId;
  mLightAmmoDepletedPlayers = 0;
  mDarkAmmoDepletedPlayers = 0;
  CScriptEffect::ResetParticleCounts();
  fn_8003FF1C(this);
  fn_8003FF20(this);

  const bool playerDead = !IsMultiplayer() && mPlayers[0]->GetDeathTime() > 0.f;
  if (mGameState == kGS_Running) {
    if (!IsMultiplayer() && !mCameraManagers[0]->IsInCinematicCamera()) {
      gpGameState->SetTotalPlayTime(dt + gpGameState->GetTotalPlayTime());
      UpdateHintState(dt);
    }
#ifdef TARGET_PC
    // Port: the F1 in-game timer and Return to title (PortOverlay), and the speedrun timer's
    // LiveSplit feed (game time, upgrade splits).
    PortOverlay::NoteGameTime(gpGameState->GetTotalPlayTime());
    if (PortOverlay::ConsumeReturnToTitleRequest()) {
      gpMain->SetRestartMode(CMain::kRM_Default);
      QuitGame();
    }
    if (!IsMultiplayer() && mPlayerStates[0] != nullptr) {
      static_assert(PortLiveSplit::kItemCount == CPlayerState::kIT_Max, "item count");
      int capacities[PortLiveSplit::kItemCount];
      for (int i = 0; i < PortLiveSplit::kItemCount; ++i) {
        capacities[i] = mPlayerStates[0]->GetItemCapacity(static_cast< CPlayerState::EItemType >(i));
      }
      PortLiveSplit::GameTick(gpGameState->GetTotalPlayTime(), capacities);
    }
#endif

    for (int player = 0; player < 4; ++player) {
      for (int pass = 0; pass < 11; ++pass) {
        mCameraFilterPasses[player][pass].Update(dt);
        mCameraBlurPasses[player][pass].Update(dt);
      }
    }

#ifdef TARGET_PC
    // Hacker Mode (scan virus) is a multiplayer power-up: in the campaign it would force the Scan
    // visor and block the morph ball, so drop it where it cannot belong.
    if (!IsMultiplayer() && mPlayerStates[0] != nullptr) {
      CPlayerState::CPowerUp& virus = mPlayerStates[0]->PowerUp(CPlayerState::kIT_ScanVirus);
      if (virus.mAmount != 0 || virus.mCapacity != 0 || virus.mTimeLeft != 0.f) {
        virus.mTimeLeft = 0.f;
        virus.mAmount = 0;
        virus.mCapacity = 0;
        mPlayerStates[0]->StartTransitionToVisor(CPlayerState::kPV_Combat);
      }
    }
#endif

    for (int item = 0; item < CPlayerState::kIT_Max; ++item) {
      for (uint player = 0; player < mNumPlayers; ++player) {
        const CPlayerState::EItemType type = static_cast< CPlayerState::EItemType >(item);
        CPlayerState::CPowerUp& powerUp = mPlayerStates[player]->PowerUp(type);
        if (powerUp.mTimeLeft > 0.f) {
          powerUp.mTimeLeft -= dt;
          if (powerUp.mTimeLeft < 0.f) {
            powerUp.mTimeLeft = 0.f;
            powerUp.mAmount = 0;
            if (item >= CPlayerState::kIT_SuperMissile && item <= CPlayerState::kIT_SonicBoom) {
              powerUp.mCapacity = 0;
            }
            switch (type) {
            case CPlayerState::kIT_SwitchVisorCombat:
            case CPlayerState::kIT_SwitchVisorScan:
            case CPlayerState::kIT_SwitchVisorDark:
            case CPlayerState::kIT_SwitchVisorEcho:
              mPlayerStates[player]->StartTransitionToVisor(CPlayerState::kPV_Combat);
              break;
            default:
              break;
            }
            if (item == CPlayerState::kIT_ScanVirus) {
              mPlayerStates[player]->StartTransitionToVisor(CPlayerState::kPV_Combat);
#ifdef TARGET_PC
              // A virus carried out of a multiplayer map must not take the campaign's Scan Visor.
              if (IsMultiplayer())
#endif
              mPlayerStates[player]->ReInitializePowerUp(CPlayerState::kIT_ScanVisor, 0);
            }
            DisplayAlertAboutOutOfAmmo(*mPlayers[player], type);
          }
        }
      }
    }
    mSafeZoneManager->Update(dt, *this);
  }

  if (mGameState != kGS_Paused && dt > FLT_EPSILON) {
    PreThinkObjects(dt);
    mFluidPlaneManager->Update(dt);
  }
  if (mGameState == kGS_Running) {
    if (!playerDead) {
      CDecalManager::Update(dt, *this);
    }
    UpdateSortedLists();
    if (dt > FLT_EPSILON && !playerDead) {
      MovePlatforms(dt);
      MoveActors(dt);
    }
    UpdatePlayerLineOfSight(dt);
    ProcessPlayerInput();
    if (mGameState != kGS_SoftPaused) {
      for (uint player = 0; player < mNumPlayers; ++player) {
        CGameCollision::Move(*this, *mPlayers[player], dt, nullptr);
      }
    }
    UpdateSortedLists();
    if (!playerDead) {
      CrossTouchActors();
    }
  } else {
    ProcessPlayerInput();
  }
  if (!playerDead && mGameState == kGS_Running) {
    mActorModelParticles->Update(dt, *this);
  }
  if ((mGameState == kGS_Running || mGameState == kGS_SoftPaused) && dt > FLT_EPSILON) {
    Think(dt);
  }

  if (mPausedHudMemoFrameCount == mHudMessageFrameCount) {
    ShowPausedHUDMemo(mPausedHudMemoAssetId, mQueuedHudMemoDismissalDelay);
    --mPausedHudMemoFrameCount;
    mPausedHudMemoAssetId = kInvalidAssetId;
  }
  if (!playerDead && mGameState == kGS_Running && !IsMultiplayer() &&
      !mCameraManagers[0]->IsInCinematicCamera()) {
    UpdateEscapeSequenceTimer(dt);
  }
  mWorld->Update(dt);
  UpdateDynamicLayers();
  for (uint player = 0; player < mNumPlayers; ++player) {
    mRumbleManagers[player]->Update(dt);
  }
  if (!playerDead) {
    mEnvFxManager->Update(dt, *this);
  }
  UpdateAreaSounds();
  mWorld->Area(GetNextAreaId())->UpdateDocks(*this);
  mReadyToRender = true;

  if (mInMapScreen) {
    CHintOptions& hintOptions = gpGameState->HintOptions();
    const CHintOptions::SHintState* hint = hintOptions.GetCurrentDisplayedHint();
    if (hint != nullptr && hint->CanContinue()) {
      hintOptions.DismissDisplayedHint();
    }
    mInMapScreen = false;
  }
  const CGameState& gameState = *gpGameState;
  gameState.GetGameMode().Update(dt, *this);
  ThinkNewObjects(dt);
  if (mGameState != kGS_SoftPaused) {
    for (uint player = 0; player < mNumPlayers; ++player) {
      mCameraManagers[player]->Update(cameraDt, *this);
    }
  }
  ThinkNewObjects(dt);
  if (mGameState != kGS_Paused) {
    PostUpdatePlayer(dt);
  }
  gpGameState->CurrentWorldState().SetAreaId(mNextAreaId);
  mWorld->TravelToArea(mNextAreaId, *this, CWorld::kATT_LoadAdjacent);
  ClearGraveyard();
  ++mUpdateFrameIdx;
  mArchQueue = nullptr;
}

void CStateManager::UpdateAreaSounds() {
  rstl::reserved_vector< int, 10 > areaIds;
  areaIds.clear();
  for (CGameArea::CConstChainIterator area = mWorld->GetChainHead(CWorld::kC_Alive);
       area != CWorld::GetAliveAreasEnd(); ++area) {
    if (area->GetOcclusionState() == CGameArea::kOS_Visible) {
      areaIds.push_back(area->GetId().Value());
    }
  }
  CSfxManager::SetActiveAreas(areaIds, mNextAreaId.Value());
}

void CStateManager::DisplayAlertAboutOutOfAmmo(const CPlayer& player,
                                               CPlayerState::EItemType type) {
  CObjectList* allList = mObjectLists[kOL_All].get();
  for (int i = allList->GetFirstObjectIndex(); i != -1; i = allList->GetNextObjectIndex(i)) {
    CScriptSpecialFunction* const special = TCastToPtr< CScriptSpecialFunction >((*allList)[i]);
    if (special != nullptr && special->GetFunction() == CScriptSpecialFunction::kSF_ItemDepletion) {
      special->OnItemDepleted(*this, player.GetPlayerIndex(), type);
    }
  }

  if (mPlayerStates[player.GetPlayerIndex()]->GetItemAmount(type, true) != 0) {
    return;
  }
  const uint playerIndex = MaskUIdNumPlayers(player.GetUniqueId());
  CHUDMemoParms memoInfo(3.f, true, false, false, 1 << playerIndex, true);
  switch (type) {
  case CPlayerState::kIT_LightAmmo:
    if (mDarkAmmoDepletedPlayers & (1 << player.GetPlayerIndex())) {
      CSamusHud::DisplayHudMemo(rstl::wstring(gpStringTable->GetString("BothAmmoDepleted")),
                                memoInfo);
    } else {
      CSamusHud::DisplayHudMemo(rstl::wstring(gpStringTable->GetString("LightAmmoDepleted")),
                                memoInfo);
    }
    mLightAmmoDepletedPlayers |= 1 << player.GetPlayerIndex();
    break;
  case CPlayerState::kIT_DarkAmmo:
    if (mLightAmmoDepletedPlayers & (1 << player.GetPlayerIndex())) {
      CSamusHud::DisplayHudMemo(rstl::wstring(gpStringTable->GetString("BothAmmoDepleted")),
                                memoInfo);
    } else {
      CSamusHud::DisplayHudMemo(rstl::wstring(gpStringTable->GetString("DarkAmmoDepleted")),
                                memoInfo);
    }
    mDarkAmmoDepletedPlayers |= 1 << player.GetPlayerIndex();
    break;
  default:
    break;
  }
}

void CScopedProfiler::BeginFrame() {}

#ifdef TARGET_PC
namespace {

// A warp names an asset ("FrontEnd", "TempleGrounds", ...) or gives a hex asset id, so the
// request text needs no table on the platform side; the names live in the resource table.
CAssetId ResolveWarpAsset(const char* text, bool report = true) {
  if (text == nullptr || *text == '\0') {
    return kInvalidAssetId;
  }
  // An asset id is written as eight hex digits in the logs and symbol maps, and `strtoul` reads
  // that as decimal only up to the first letter ("863fcd72" stops at the 'f'). The hex test comes
  // first: with base 0 an all-digit id like "69802220" (FrontEnd04) parsed as decimal and the hex
  // branch was never reached, so `MP2_WORLD=69802220` booted the save's own world instead.
  if (strlen(text) == 8 && strspn(text, "0123456789abcdefABCDEF") == 8) {
    return static_cast< CAssetId >(std::strtoul(text, nullptr, 16));
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 0);
  if (end != nullptr && end != text && *end == '\0') {
    return static_cast< CAssetId >(value);
  }
  const SObjectTag* tag =
      gpResourceFactory != nullptr ? gpResourceFactory->GetResourceIdByName(text) : nullptr;
  if (tag == nullptr) {
    if (report) {
      std::fprintf(stderr, "[warp] no asset named '%s'\n", text);
    }
    return kInvalidAssetId;
  }
  return tag->id;
}

// Defined with the other MLVL readers further down.
CAssetId PortFindAreaInMlvl(CAssetId worldId, const char* name, int* index);
CAssetId PortFindWorldByName(const char* text);
bool PortFindAreaInAnyWorld(const char* name, CAssetId* world, CAssetId* area);

bool IsHexAssetId(const char* text) {
  return strlen(text) == 8 && strspn(text, "0123456789abcdefABCDEF") == 8;
}

// An area of `worldId` from request text: a hex id wins, then the MLVL area name, then the
// resource table. A name is reported when it matches nothing.
CAssetId ResolveWarpArea(CAssetId worldId, const char* text) {
  if (text == nullptr || *text == '\0') {
    return kInvalidAssetId;
  }
  if (IsHexAssetId(text)) {
    return ResolveWarpAsset(text);
  }
  int index = -1;
  const CAssetId byName = PortFindAreaInMlvl(worldId, text, &index);
  if (byName != kInvalidAssetId) {
    std::fprintf(stderr, "[warp] area '%s' is area %d (%08x) of world %08x\n", text, index, byName,
                 worldId);
    return byName;
  }
  return ResolveWarpAsset(text);
}

// The world and area a MP2_WORLD request names. The world is a hex id, a resource name or a part
// of an MLVL name; when it is empty or names no world, the area name alone picks the world (the
// first MLVL with an area of that name), and so does a world text that is itself an area name.
void ResolveWarpWorld(const char* worldText, const char* areaText, CAssetId* world,
                      CAssetId* area) {
  *world = kInvalidAssetId;
  *area = kInvalidAssetId;
  const bool haveWorld = worldText != nullptr && *worldText != '\0';
  const bool haveArea = areaText != nullptr && *areaText != '\0';
  if (haveWorld) {
    *world = ResolveWarpAsset(worldText, false);
    if (*world == kInvalidAssetId) {
      *world = PortFindWorldByName(worldText);
    }
  }
  if (*world != kInvalidAssetId) {
    *area = ResolveWarpArea(*world, areaText);
    return;
  }
  const char* areaName = haveArea ? areaText : (haveWorld ? worldText : nullptr);
  if (areaName == nullptr) {
    return;
  }
  if (haveWorld && haveArea) {
    std::fprintf(stderr, "[warp] no world matches '%s'; looking for an area '%s' instead\n",
                 worldText, areaText);
  }
  PortFindAreaInAnyWorld(areaName, world, area);
}

} // namespace

void CStateManager::PortWarpTick() {
#ifdef TARGET_PC
  // The debug console (MP2_CONSOLE=<port>, platform/port_console.cpp) runs its commands here: this
  // is the game thread, with the world and every object pointer live.
  PortConsole::Tick(*this);
#endif
  CWorld* world = World();
  if (world == nullptr) {
    // No world yet: leave any request alone; it is served as soon as one is there.
    return;
  }

  // A layer is streamed in over frames (PortLayerTick, further down): tell the queue whether the
  // last one is still running, so a second `layer` command in the same tick waits its turn instead
  // of being merged into the first (PortWarp::Layer queues them).
  PortWarp::SetLayerBusy(mPortLayerArea != kInvalidAreaId);

  // One line per world, so a sweep can see which area slots are real (a slot with an invalid MREA
  // is not an area the game can travel to) and so a test can wait for the game world without
  // needing a pending request: "the front end's world, then mine".
  //
  // The slots listed below are the *loaded* CWorld's, so the id in the line has to be that CWorld's
  // too. `gpGameState->CurrentWorldAssetId()` changes when the game state is told to travel, a frame
  // or more before the new CWorld replaces this one: keying only on it printed the outgoing world's
  // slot list under the incoming world's id (the all-worlds sweep, `/tmp/opencode/reports/sweep1.md`
  // item 4, whose report had to take the slot count from the disc instead). So wait until the two
  // agree, and skip the world in between.
  static CAssetId sLoggedWorld = kInvalidAssetId;
  const CAssetId loadedWorld = world->IGetWorldAssetId();
  if (gpGameState != nullptr && sLoggedWorld != loadedWorld &&
      loadedWorld == gpGameState->CurrentWorldAssetId()) {
    sLoggedWorld = loadedWorld;
    std::fprintf(stderr, "[warp] world %08x areas:", sLoggedWorld);
    for (int i = 0; i < world->IGetAreaCount(); ++i) {
      // The MLVL carries every area's internal name, loaded or not (an unloaded area's slot is a
      // CDummyGameArea), which is what makes `MP2_TELEPORT=<slot>` usable from a room name
      // ("the Hive Access Tunnel is area 7").
      const IGameArea* a = world->IGetAreaAlways(TAreaId(i));
      // The MLVL name is the one a warp request matches (retail drops it for an area that has a
      // name STRG, so IGetInternalAreaName() is empty for most).
      std::fprintf(stderr, " %d=%08x:%s", i, a != nullptr ? a->IGetAreaAssetId() : 0,
                   a != nullptr ? a->PortGetMlvlAreaName().data() : "");
    }
    std::fprintf(stderr, "\n");
  }

  if (!PortWarp::Pending()) {
    return;
  }

  // The front end has its own world (and its own player preview), whose area ids have nothing to
  // do with the save's: a boot request served there lands nowhere. Hold every request until the
  // game mode has left the front end, so `MP2_TELEPORT=<area>` means "in my save's world, as soon
  // as it loads" - which is what lets a test boot straight into the area it is about.
  if (gpGameState != nullptr && gpGameState->GetGameMode().GetGameModeType() == 'FRND') {
    return;
  }

  // A boot request waits for gameplay, not just for the world: served while the save's area is
  // still coming up, the destination's scripted camera-filter keyframe could be left applying its
  // flash - an additive white full-screen quad for the rest of the run, with the HUD drawn over it
  // (docs/STATUS.md). The same warp issued once the game is running is clean, which is the state a
  // console `warp` or an `MP2_INPUT` token runs in, so the two agree now.
  if (PortWarp::PendingIsBoot() && PortHarness::GameUpdates() < 120u) {
    return;
  }

  PortWarp::SRequest request;
  if (!PortWarp::Consume(request)) {
    return;
  }
  // Only the request that asked for it keeps the player's place; any other warp spawns as usual.
  mPortWarpKeepPlace = false;
  if (mUpdateFrameIdx > 0) {
    // A boot request is served late by design (see above): say when, so a sweep's timeline is
    // readable - later tokens are counted in game updates from the same clock.
    std::fprintf(stderr, "[warp] serving a request at frame %u\n", mUpdateFrameIdx);
  }

  switch (request.kind) {
  case PortWarp::EKind::kArea: {
    // A plain number is an area *index* (that is what `[warp] area N` prints and what the sweeps
    // use), anything else - a name or a hex asset id - is resolved through the resource table.
    // The whole text must be digits: "0k_temple_hall" is a name, not area 0.
    char* indexEnd = nullptr;
    const long parsedIndex = std::strtol(request.area, &indexEnd, 10);
    const int index = indexEnd != request.area && *indexEnd == '\0' ? static_cast< int >(parsedIndex) : -1;
    const CGameArea* byIndex =
        index >= 0 && index < world->IGetAreaCount() ? world->Area(TAreaId(index)) : nullptr;
    if (byIndex != nullptr && byIndex->GetAreaAssetId() != kInvalidAssetId) {
      PortWarpToArea(TAreaId(index));
    } else if (!IsHexAssetId(request.area)) {
      // An MLVL area name ("0b_sand_hall"), case-insensitive, before the resource table.
      TAreaId named = kInvalidAreaId;
      for (int i = 0; i < world->IGetAreaCount(); ++i) {
        const IGameArea* a = world->IGetAreaAlways(TAreaId(i));
        if (a != nullptr && a->IGetAreaAssetId() != kInvalidAssetId &&
            strcasecmp(a->PortGetMlvlAreaName().data(), request.area) == 0) {
          named = TAreaId(i);
          break;
        }
      }
      if (named != kInvalidAreaId) {
        std::fprintf(stderr, "[warp] area '%s' is area %d\n", request.area, named.Value());
        PortWarpToArea(named);
      } else {
        const CAssetId byAsset = ResolveWarpAsset(request.area);
        if (byAsset == kInvalidAssetId || world->IGetAreaId(byAsset) == kInvalidAreaId) {
          std::fprintf(stderr, "[warp] no area '%s' in world %08x\n", request.area,
                       gpGameState != nullptr ? gpGameState->CurrentWorldAssetId() : 0u);
        }
        PortWarpToArea(world->IGetAreaId(byAsset));
      }
    } else {
      PortWarpToArea(world->IGetAreaId(ResolveWarpAsset(request.area)));
    }
    break;
  }
  case PortWarp::EKind::kCycleArea: {
    const int count = world->IGetAreaCount();
    int area = world->GetCurrentAreaId().Value();
    for (int step = 0; step < count && count > 0; ++step) {
      area = (area + request.delta + count) % count;
      const TAreaId candidate(area);
      const CGameArea* candidateArea = world->Area(candidate);
      if (candidateArea != nullptr && candidateArea->GetAreaAssetId() != kInvalidAssetId) {
        PortWarpToArea(candidate);
        break;
      }
    }
    break;
  }
  case PortWarp::EKind::kDimension: {
    PortWarpToDimension();
    break;
  }
  case PortWarp::EKind::kDock: {
    PortWarpToDock();
    break;
  }
  case PortWarp::EKind::kDoor: {
    PortWarpToDoor();
    break;
  }
  case PortWarp::EKind::kStation: {
    PortWarpToStation();
    break;
  }
  case PortWarp::EKind::kWorldPortal: {
    PortWarpToWorldPortal();
    break;
  }
  case PortWarp::EKind::kKill: {
    PortWarpToDeath();
    break;
  }
  case PortWarp::EKind::kHeal: {
    PortWarpToHeal();
    break;
  }
  case PortWarp::EKind::kLayer: {
    PortWarpToLayer(world->GetCurrentAreaId(), request.delta);
    break;
  }
  case PortWarp::EKind::kLayerOff: {
    const TAreaId aid = world->GetCurrentAreaId();
    if (!mCurrentWorldLayerState.IsNull() && aid.Value() >= 0 &&
        aid.Value() < static_cast< int >(mCurrentWorldLayerState->GetAreaLayers().size()) &&
        request.delta >= 0 && request.delta < mCurrentWorldLayerState->GetLayerCount(aid)) {
      mCurrentWorldLayerState->SetLayerActive(aid, TLayerId(request.delta), false);
      std::fprintf(stderr, "[layer] area %d layer %d marked inactive for the next load\n",
                   aid.Value(), request.delta);
    }
    break;
  }
  case PortWarp::EKind::kWorld: {
    CAssetId worldId = kInvalidAssetId;
    CAssetId areaId = kInvalidAssetId;
    ResolveWarpWorld(request.world, request.area, &worldId, &areaId);
    PortWarpToWorld(worldId, areaId);
    break;
  }
  case PortWarp::EKind::kCycleWorld: {
    if (gpMemoryCard == nullptr) {
      std::fprintf(stderr, "[warp] no memory card yet; no world table\n");
      break;
    }
    const rstl::vector< CMemoryCard::MemoryWorld >& worlds = gpMemoryCard->GetMemoryWorlds();
    const CAssetId current = gpGameState->CurrentWorldAssetId();
    std::fprintf(stderr, "[warp] world table: %d worlds, current %08x:",
                 static_cast< int >(worlds.size()), current);
    for (size_t i = 0; i < worlds.size(); ++i) {
      std::fprintf(stderr, " %zu=%08x", i, worlds[i].first);
    }
    std::fprintf(stderr, "\n");
    if (worlds.empty()) {
      break;
    }
    int index = 0;
    for (int i = 0; i < static_cast< int >(worlds.size()); ++i) {
      if (worlds[i].first == current) {
        index = i;
        break;
      }
    }
    const int count = static_cast< int >(worlds.size());
    index = (index + request.delta + count) % count;
    PortWarpToWorld(worlds[index].first, kInvalidAssetId);
    break;
  }
  case PortWarp::EKind::kNone:
  default:
    break;
  }
}

void CStateManager::PortWarpToArea(TAreaId aid) {
  CWorld* world = World();
  if (world == nullptr || !world->DoesAreaExist(aid)) {
    std::fprintf(stderr, "[warp] area %d does not exist in this world\n", aid.Value());
    return;
  }
  if (mPortSpawnAtArea != kInvalidAreaId) {
    // The game's own transition is still carrying out an earlier hop, and it owns the world until it
    // is done. Hold the request and take it when that arrival is placed (PortPlaceArrivalTick).
    mPortWarpQueuedArea = aid;
    std::fprintf(stderr, "[warp] area %d queued behind the game's own transition to area %d\n",
                 aid.Value(), mPortSpawnAtArea.Value());
    return;
  }
  if (aid == world->GetCurrentAreaId()) {
    return;
  }
  CGameArea* area = world->Area(aid);
  if (area == nullptr || area->GetAreaAssetId() == kInvalidAssetId) {
    std::fprintf(stderr, "[warp] area %d is empty in this world\n", aid.Value());
    return;
  }

  // The current area id must not point at an area that is not loaded: the update calls
  // Area(GetNextAreaId())->UpdateDocks every frame, which segfaults on an area whose stream is
  // still coming up. A dock transition has the same rule - it starts the destination loading
  // when the player touches the dock, and only moves the id once the area says it is loaded -
  // so the warp loads first and settles the move in PortWarpSettle.
  if (!area->IsLoaded()) {
    if (mPortPendingWarpArea != aid) {
      // A request that lands while another area is still streaming must not displace it: nothing
      // else pumps a stream that has started, so the abandoned area would hold the ids it has
      // already taken while the new one waits for room that can never come - the same deadlock this
      // warp was just fixed for. Hold the new area and take it when the running one settles.
      if (mPortWarpStreamStarted && mPortPendingWarpArea != kInvalidAreaId) {
        mPortWarpQueuedArea = aid;
        std::fprintf(stderr, "[warp] area %d queued behind area %d (it is still streaming)\n",
                     aid.Value(), mPortPendingWarpArea.Value());
        return;
      }
      std::fprintf(stderr, "[warp] area %d: loading\n", aid.Value());
      mPortPendingWarpArea = aid;
      mPortWarpWaitingForIds = false;
      mPortWarpDroppedAreas = false;
      mPortWarpStreamStarted = false;
    }
    // Hold the world's own area loading off until this warp has arrived. The per-frame travel
    // preloads the current area's dock neighbours (CWorld::TravelToArea with kATT_LoadAdjacent), and
    // a whole preloaded neighbour - measured 368 objects in world 1baa96c2 - is what pushed a warp's
    // stream over the 1024-id pool together with the destination's own script objects. Paused here,
    // before the destination is scheduled, so the destination itself is not on the loading chain and
    // is therefore not paused: this warp still calls its StartStreamIn directly.
    if (!mPortWarpLoadPaused) {
      mPortWarpLoadPaused = true;
      world->SetLoadPauseState(true);
      std::fprintf(stderr, "[warp] area %d: holding the world's own area loading until it arrives\n",
                   aid.Value());
    }
    // PortWarpSettle keeps the load moving from here (see its comment).
    return;
  }
  mPortPendingWarpArea = aid;
  mPortWarpStreamStarted = false;
}

namespace {
// A warp holds off starting an area's stream while fewer than this many object ids are free. The
// pool is 1024 (kMaxObjects) and a busy area's script objects run to several hundred, so a warp
// started against a nearly-full pool is what exhausted it (a save load comes up at ~1008 and
// settles near ~350 a few hundred frames later).
constexpr int kPortWarpMinFreeIds = 256;
// Fewer free ids than this after the pre-stream drop means the direct route may not fit: the
// destination needs about as many ids as it owns objects, and the area the player is in holds its
// own until it is unloaded. Measured in world 1baa96c2: area 10 alone is 602 objects, area 11's
// stream needs ~590, and 1190 > 1024. So a hop this tight is handed to the game's own transition,
// which unloads the source first (see PortWarpSettle). Temple Grounds' areas are ~390 objects and
// keep ~900 ids free after the drop, so ordinary sweeps keep using the direct route.
constexpr int kPortWarpComfortIds = 700;
// Frames between a warp settling and dropping the areas it left behind: enough for the HUD and the
// camera to have rebuilt themselves against the new area (dropping immediately segfaulted the HUD's
// dynamic lights), and short enough that a sweep's next hop still starts with a free pool.
constexpr uint kPortDropAreasDelay = 30;

// MP2_WARP_HANDED=1 forces every hop down the game's own transition, the route a tight pool takes
// (see below). A test that has to exercise that route - it is the one that runs
// CStateManager::InitializeState, and where CBallCamera::FindDesiredPosition crashed on the null
// reference GetLastCameraTransform used to return - otherwise needs ten hops of accumulated core to
// make the pool tight enough, which is minutes of run per reproduction. This makes it one hop.
bool PortForceHandedWarp() {
  static const bool sForce = std::getenv("MP2_WARP_HANDED") != nullptr;
  return sForce;
}
} // namespace

int CStateManager::PortFreeObjectIds() {
  int free = 0;
  for (int i = 0; i < static_cast< int >(mAllocatedObjectIndices.size()); ++i) {
    if (!mAllocatedObjectIndices[i]) {
      ++free;
    }
  }
  return free;
}

// Who is holding the pool when it runs out. "1024 live objects" on its own cannot say whether the
// area being streamed is simply too big or an area that should have been dropped is still there, and
// those two need different fixes - so name the areas on the alive chain and what each one owns.
void CStateManager::PortLogIdPressure() {
  CWorld* world = World();
  if (world == nullptr) {
    std::fprintf(stderr, "[ids] no world\n");
    return;
  }
  int aliveCount = 0;
  for (CGameArea::CConstChainIterator it = world->GetChainHead(CWorld::kC_Alive);
       it != CWorld::GetAliveAreasEnd(); ++it) {
    ++aliveCount;
  }
  std::fprintf(stderr, "[ids] current area %d, pending warp %d, %d areas alive:\n",
               world->GetCurrentAreaId().Value(), mPortPendingWarpArea.Value(), aliveCount);
  for (CGameArea::CConstChainIterator area = world->GetChainHead(CWorld::kC_Alive);
       area != CWorld::GetAliveAreasEnd(); ++area) {
    int owned = 0;
    CObjectList* objects = mObjectLists[kOL_All].get();
    for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
      if ((*objects)[i]->GetCurrentAreaId() == area->GetId()) {
        ++owned;
      }
    }
    std::fprintf(stderr, "[ids]   area %d: %d objects\n", area->GetId().Value(), owned);
  }
}

// Retail decides the dark-world flag per area: the area's AreaAttributes object hands its flag to
// SetIsDarkWorld on kSM_Create (CScriptAreaProperties::AcceptScriptMsg, 0x8013C658 'XCRT' ->
// 0x8013C668 lbz r0,0x24; extrwi r4,r0,1,25; bl SetIsDarkWorld), and a portal crossing flips it
// (PrepareAreaTransition's SetIsDarkWorld(!mIsDarkWorld)). A warp is neither: its Create can come
// before the handed flow's flip, or from a neighbour preloaded on the other side, so after a warp
// the flag is taken from the destination's attributes (registered on kSM_AreaLoaded,
// 0x8013C69C SetAreaAttributes).
void CStateManager::PortSetDarkWorldFromArea(TAreaId aid) {
  CWorld* world = World();
  if (world == nullptr || !world->DoesAreaExist(aid)) {
    return;
  }
  const CGameArea* area = world->Area(aid);
  const CScriptAreaProperties* attributes =
      area != nullptr && area->GetPostConstructed() != nullptr
          ? area->GetPostConstructed()->mAreaAttributes
          : nullptr;
  bool dark;
  const char* source;
  if (attributes != nullptr) {
    dark = attributes->PortIsDarkWorld();
    source = "its area attributes";
  } else {
    // Some areas have no AreaAttributes object on any layer (Temple Grounds 11 01_temple_hive_04).
    // Retail never needs one there: such an area is only entered from its own side, so the flag
    // carries over. A warp can come from either side, so it asks the area's map instead: the
    // automapper's own light/dark split (CMapArea flag bit 0, CMapWorld::DrawAreas'
    // IsInDarkWorld colours).
    const CMapWorld* mapWorld = world->IGetMapWorld();
    const CMapArea* mapArea = mapWorld != nullptr ? mapWorld->GetMapArea(aid.Value()) : nullptr;
    if (mapArea == nullptr) {
      // TravelToArea has only just asked for it (CWorld::TravelToArea -> SetWhichMapAreasLoaded
      // around the new area), so PortDarkWorldTick asks again until it has streamed in.
      if (mPortDarkFlagArea != aid) {
        mPortDarkFlagArea = aid;
        mPortDarkFlagUntil = mUpdateFrameIdx + 600;
        std::fprintf(stderr,
                     "[warp] area %d has no area attributes and its map area is still loading; "
                     "dark-world flag %d until it is in\n",
                     aid.Value(), mIsDarkWorld ? 1 : 0);
      }
      return;
    }
    dark = mapArea->IsInDarkWorld();
    source = "its map area";
  }
  mPortDarkFlagArea = kInvalidAreaId;
  // One line per arrival, so a sweep can check the flag against the area's side.
  std::fprintf(stderr, "[warp] area %d: dark-world flag %d (%s; was %d)\n", aid.Value(),
               dark ? 1 : 0, source, mIsDarkWorld ? 1 : 0);
  if (dark != mIsDarkWorld) {
    SetIsDarkWorld(dark);
  }
}

// The arrival area of a direct boot (PortWarp::DirectBoot, at the end of this file), until the first
// world has loaded it.
static CAssetId sPortDirectBootArea = kInvalidAssetId;

// Finishes a PortSetDarkWorldFromArea that had to wait for the area's map area. Also resolves the
// flag for a direct boot's arrival: that route skips the warp, so nothing else takes the flag from
// the destination - the state manager starts light (mIsDarkWorld(false)) and only an AreaAttributes
// object's Create sets it, which areas without one never send.
void CStateManager::PortDarkWorldTick() {
  if (sPortDirectBootArea != kInvalidAssetId) {
    CWorld* bootWorld = World();
    // Fully initialized: the init phase has streamed the desired area in (kIP_Done) - where a warp
    // resolves its arrival too (PortWarpSettle, PortPlaceArrivalTick).
    if (bootWorld == nullptr || !IsFullyInitialized()) {
      return;
    }
    const TAreaId bootArea = bootWorld->IGetAreaId(sPortDirectBootArea);
    sPortDirectBootArea = kInvalidAssetId;
    std::fprintf(stderr, "[warp] direct boot arrived in area %d\n", bootArea.Value());
    PortSetDarkWorldFromArea(bootArea);
    return;
  }
  if (mPortDarkFlagArea == kInvalidAreaId) {
    return;
  }
  CWorld* world = World();
  // mNextAreaId, not the world's current area: SetCurrentAreaId only sets the former, and the
  // world follows on its next TravelToArea.
  if (world == nullptr || mNextAreaId != mPortDarkFlagArea) {
    mPortDarkFlagArea = kInvalidAreaId;
    return;
  }
  if (mUpdateFrameIdx > mPortDarkFlagUntil) {
    std::fprintf(stderr, "[warp] area %d: its map area never loaded; dark-world flag stays %d\n",
                 mPortDarkFlagArea.Value(), mIsDarkWorld ? 1 : 0);
    mPortDarkFlagArea = kInvalidAreaId;
    return;
  }
  PortSetDarkWorldFromArea(mPortDarkFlagArea);
}

// The game's own transition carries a hop the direct route was too tight for. This places the player
// once it is done: CMFGame clears mPendingDockArea when its transition finishes, and it never spawns
// the player itself (a dock crossing keeps the player where they were standing), so a warp has to do
// it - the area the player came from is unloaded, so their position means nothing here.
// A warp's arrival (the debug menu's area list, the console's `area`, the sweep's NEXTAREA): put
// the player inside `target`. The old scan took the first active first-spawn point of *any* loaded
// area, and a room without one left the player at the old coordinates, outside the new room. Order:
// the target's own first spawn point (SpawnPlayer: it also applies the spawn's inventory, as the
// game's own arrival does), any active, then any spawn point of the area (Teleport only, looking
// along its forward with Z flattened as the MP1 port's debug teleport does), then a doorway of the
// area, then the area's AABB centre; the last two stand on the floor found by a ray down.
void CStateManager::PortPlaceAtArrival(TAreaId target) {
  CPlayer* player = GetPlayer(0);
  CWorld* world = World();
  if (player == nullptr || world == nullptr || !world->DoesAreaExist(target)) {
    return;
  }
  CScriptSpawnPoint* firstSpawn = nullptr;
  CScriptSpawnPoint* activeSpawn = nullptr;
  CScriptSpawnPoint* anySpawn = nullptr;
  CScriptDock* dock = nullptr;
  CObjectList* objects = mObjectLists[kOL_All].get();
  for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
    CEntity* entity = (*objects)[i];
    if (CScriptSpawnPoint* spawn = TCastToPtr< CScriptSpawnPoint >(entity)) {
      if (spawn->GetCurrentAreaId() != target) {
        continue;
      }
      if (spawn->GetActive() && spawn->IsFirstSpawn() && firstSpawn == nullptr) {
        firstSpawn = spawn;
      }
      if (spawn->GetActive() && activeSpawn == nullptr) {
        activeSpawn = spawn;
      }
      if (anySpawn == nullptr) {
        anySpawn = spawn;
      }
    } else if (CScriptDock* d = TCastToPtr< CScriptDock >(entity)) {
      if (dock == nullptr && !d->IsVirtual() && d->GetCurrentAreaId() == target) {
        dock = d;
      }
    }
  }
  const char* kind = "first spawn";
  CScriptSpawnPoint* plain = activeSpawn != nullptr ? activeSpawn : anySpawn;
  if (firstSpawn != nullptr) {
    SpawnPlayer(*firstSpawn, 0);
  } else if (plain != nullptr) {
    kind = activeSpawn != nullptr ? "active spawn" : "inactive spawn";
    const CVector3f position = plain->GetTransform().GetTranslation();
    CVector3f look = plain->GetTransform().GetForward();
    look.SetZ(0.f);
    if (!look.CanBeNormalized()) {
      look = CVector3f(0.f, 1.f, 0.f);
    }
    player->Teleport(CTransform4f::LookAt(position, position + look, CVector3f::Up()), *this,
                     true);
  } else {
    CVector3f point = CVector3f::Zero();
    CVector3f look = CVector3f(0.f, 1.f, 0.f);
    const CAABox& bounds = world->GetArea(target)->GetAABB();
    if (dock != nullptr) {
      kind = "dock";
      // A dock's forward points to the area it connects to, so standing back along it is inside.
      const CVector3f forward = dock->GetTransform().GetForward();
      point = dock->GetTranslation() - forward * 5.f;
      look = forward;
      look.SetZ(0.f);
      if (!look.CanBeNormalized()) {
        look = CVector3f(0.f, 1.f, 0.f);
      }
    } else {
      kind = "area centre";
      point = bounds.GetCenterPoint();
    }
    // Floor under the point: from a little above it, then from the top of the area's bounds.
    CRayCastResult hit = RayStaticIntersection(point + CVector3f(0.f, 0.f, 1.5f),
                                               CVector3f::Down(), 300.f, CMaterialFilter());
    if (!hit.IsValid()) {
      hit = RayStaticIntersection(CVector3f(point.GetX(), point.GetY(), bounds.GetMaxPoint().GetZ()),
                                  CVector3f::Down(), 600.f, CMaterialFilter());
    }
    if (hit.IsValid()) {
      point = hit.GetPoint() + CVector3f(0.f, 0.f, 1.f);
    }
    player->Teleport(CTransform4f::LookAt(point, point + look, CVector3f::Up()), *this, true);
  }
  std::fprintf(stderr, "[warp] placed via %s in area %d\n", kind, target.Value());
  mPortArrivalCheckArea = target;
  mPortArrivalCheckFrame = mUpdateFrameIdx + 110;
  mPortArrivalStage = 0;
  {
    const CVector3f q = player->GetTranslation();
    std::fprintf(stderr, "[warp] placed at %.1f %.1f %.1f\n", q.GetX(), q.GetY(), q.GetZ());
  }
}

// About two seconds after a warp's arrival: is the player inside the area's bounds and grounded?
void CStateManager::PortArrivalCheckTick() {
  if (mPortArrivalCheckArea == kInvalidAreaId || mUpdateFrameIdx < mPortArrivalCheckFrame) {
    return;
  }
  const TAreaId area = mPortArrivalCheckArea;
  CWorld* world = World();
  CPlayer* player = GetPlayer(0);
  if (world == nullptr || player == nullptr || !world->DoesAreaExist(area)) {
    mPortArrivalCheckArea = kInvalidAreaId;
    return;
  }
  if (mPortArrivalStage == 0) {
    // Settled means the height no longer changes over the next ten updates.
    mPortArrivalStage = 1;
    mPortArrivalZ = player->GetTranslation().GetZ();
    mPortArrivalCheckFrame = mUpdateFrameIdx + 10;
    return;
  }
  mPortArrivalCheckArea = kInvalidAreaId;
  const float dz = player->GetTranslation().GetZ() - mPortArrivalZ;
  const CAABox& b = world->GetArea(area)->GetAABB();
  const CVector3f p = player->GetTranslation();
  const bool inside = p.GetX() >= b.GetMinPoint().GetX() - 1.f &&
                      p.GetX() <= b.GetMaxPoint().GetX() + 1.f &&
                      p.GetY() >= b.GetMinPoint().GetY() - 1.f &&
                      p.GetY() <= b.GetMaxPoint().GetY() + 1.f &&
                      p.GetZ() >= b.GetMinPoint().GetZ() - 1.f &&
                      p.GetZ() <= b.GetMaxPoint().GetZ() + 1.f;
  std::fprintf(stderr,
               "[warp] arrival check area %d: %s grounded=%d current=%d dz=%.2f pos %.1f %.1f %.1f bounds "
               "%.1f %.1f %.1f / %.1f %.1f %.1f\n",
               area.Value(), inside ? "inside" : "OUTSIDE",
               (player->IsOnGround() || (dz > -0.05f && dz < 0.05f)) ? 1 : 0,
               world->GetCurrentAreaId().Value(), dz, p.GetX(), p.GetY(), p.GetZ(),
               b.GetMinPoint().GetX(), b.GetMinPoint().GetY(), b.GetMinPoint().GetZ(),
               b.GetMaxPoint().GetX(), b.GetMaxPoint().GetY(), b.GetMaxPoint().GetZ());
}

void CStateManager::PortPlaceArrivalTick() {
  PortArrivalCheckTick();
  if (mPortSpawnAtArea == kInvalidAreaId) {
    return;
  }
  if (mPendingDockArea != kInvalidAreaId || !IsFullyInitialized()) {
    // The flow is still fading, unloading or loading. Say so once if it takes suspiciously long, so
    // a hang in the game's own transition is visible instead of silent.
    if (mUpdateFrameIdx > mPortSpawnAtFrame + 600 && !mPortSpawnWarned) {
      mPortSpawnWarned = true;
      std::fprintf(stderr, "[warp] the game's transition to area %d has not finished after 600 "
                           "frames (pending dock area %d)\n",
                   mPortSpawnAtArea.Value(), mPendingDockArea.Value());
    }
    return;
  }
  CWorld* world = World();
  if (world == nullptr || !world->DoesAreaExist(mPortSpawnAtArea)) {
    mPortSpawnAtArea = kInvalidAreaId;
    return;
  }
  const TAreaId area = mPortSpawnAtArea;
  mPortSpawnAtArea = kInvalidAreaId;
  mPortSpawnWarned = false;
  std::fprintf(stderr, "[warp] the game's own transition arrived in area %d\n", area.Value());
  // A warp is a teleport, not a portal crossing. The hand-over above starts the game's own portal
  // transition, and CWorldTransManager draws that as an additive full-screen quad that begins white
  // (mPortalFade 0) and only advances once CPortalTransition is ready. At boot it never becomes
  // ready - the destination's portal model is not loading while the port still holds the world's
  // own area loading - so the quad stayed up for the rest of the run: the world was drawn (normal
  // draw-call counts) with the HUD over a white screen (docs/STATUS.md). The hop has arrived, so the
  // transition has nothing left to do.
  if (!gpGameState->WorldTransitionManager().IsNull() &&
      gpGameState->WorldTransitionManager()->GetTransType() == CWorldTransManager::kTT_Portal) {
    std::fprintf(stderr, "[warp] ending the portal transition the hand-over left running\n");
    gpGameState->WorldTransitionManager()->DisableTransition();
  }
  PortSetDarkWorldFromArea(area);

  if (!PortPlaceKeptTransform()) {
    PortPlaceAtArrival(area);
  }
  for (uint i = 0; i < mNumPlayers; ++i) {
    mCameraManagers[i]->ResetCameras(*this);
  }
  if (mPortWarpQueuedArea != kInvalidAreaId) {
    const TAreaId queued = mPortWarpQueuedArea;
    mPortWarpQueuedArea = kInvalidAreaId;
    std::fprintf(stderr, "[warp] taking the queued area %d\n", queued.Value());
    PortWarpToArea(queued);
  }
}

// Places the player in the area a warp asked for, once that area has finished loading.
void CStateManager::PortWarpSettle() {
  if (mPortPendingWarpArea == kInvalidAreaId) {
    return;
  }
  CWorld* world = World();
  if (world == nullptr || !world->DoesAreaExist(mPortPendingWarpArea)) {
    mPortPendingWarpArea = kInvalidAreaId;
    if (world != nullptr && mPortWarpLoadPaused) {
      mPortWarpLoadPaused = false;
      world->SetLoadPauseState(false);
    }
    return;
  }
  CGameArea* area = world->Area(mPortPendingWarpArea);
  if (area == nullptr) {
    mPortPendingWarpArea = kInvalidAreaId;
    if (mPortWarpLoadPaused) {
      mPortWarpLoadPaused = false;
      world->SetLoadPauseState(false);
    }
    return;
  }
  if (!area->IsLoaded()) {
    // The initial world load drives an area in by calling both of these every frame until
    // ScheduleAreaToLoad reports it is done, so do the same: it only sets up the chain, and
    // StartStreamIn is what advances the stream.
    // A save load allocates nearly the whole 1024-entry id pool before it purges (it comes up at
    // ~1008 and settles near ~350 a few hundred frames later), so a warp landing in that window
    // would need more ids than are left and the area's script objects would exhaust the pool.
    // Wait for room instead of starting the load: the area is not in anyone's way while loading.
    // The wait only guards the *start*: an area's script objects allocate as its stream runs, so
    // once it has begun the warp has to keep pumping it. Refusing to advance a running stream
    // stalls the load forever and keeps the ids it has already taken - a walk of world 1baa96c2
    // died there, with the half-loaded area holding 600 of the 1024 ids and free stuck at 173
    // against a 256 floor.
    if (!mPortWarpStreamStarted) {
      // Drop the areas this warp has left behind *before* it starts streaming, whether or not the
      // pool looks tight. Only they can give the destination's script objects room (the player's own
      // area and the destination are the two that must stay), and waiting until the count is already
      // low is too late: the stream is what fills the pool, so by then the leftover area is part of
      // the peak. Measured in world 1baa96c2, a warp from area 9 to area 10 died with area 9 (183
      // objects) and a *preloaded neighbour* - the per-frame travel keeps one loaded, area 5 with 368
      // - alive while area 10's stream asked for ~470 more; the same numbers ended a sweep run in
      // Temple Grounds (184 + a 545-object neighbour). This is the call the game's own area
      // transition makes, and it frees a whole area's objects at once.
      if (!mPortWarpDroppedAreas) {
        const TAreaId keep = world->GetCurrentAreaId();
        if (PortUnloadAreasExcept(*this, *world, keep, mPortPendingWarpArea)) {
          mPortWarpDroppedAreas = true;
          std::fprintf(stderr,
                       "[warp] area %d: dropped the areas around %d before streaming (%d free)\n",
                       mPortPendingWarpArea.Value(), keep.Value(), PortFreeObjectIds());
        } else {
          // An area could not be invalidated yet (its transactions or dependencies are still in
          // flight), so the room this stream needs is not there yet either. Try again next frame.
          return;
        }
      }
      if (PortFreeObjectIds() < kPortWarpMinFreeIds) {
        if (!mPortWarpWaitingForIds) {
          mPortWarpWaitingForIds = true;
          std::fprintf(stderr, "[warp] area %d: waiting, only %d of %d object ids free\n",
                       mPortPendingWarpArea.Value(), PortFreeObjectIds(), kMaxObjects);
        }
        return;
      }
      if (PortFreeObjectIds() < kPortWarpComfortIds || PortForceHandedWarp()) {
        // Too tight for the direct route, but the game has a route that fits: fading out and calling
        // PrepareAreaTransition unloads the area the player is in *before* the destination's script
        // objects are created, which is exactly the peak that does not fit here. CMFGame's
        // kFS_InGame branch picks mPendingDockArea up, fades, calls PrepareAreaTransition (unload all
        // but the destination, mInitPhase = kIP_LoadFirstArea) and then kFS_PortalTransition drives
        // InitializeState until the area is there - and, crucially, that flow does not call
        // CStateManager::Update while the current area is gone, which is why the port cannot simply
        // unload it here.
        std::fprintf(stderr,
                     "[warp] area %d: %d ids free is too tight for the direct route, handing the hop "
                     "to the game's own transition%s\n",
                     mPortPendingWarpArea.Value(), PortFreeObjectIds(),
                     PortForceHandedWarp() ? " (forced by MP2_WARP_HANDED)" : "");
        mPendingDockArea = mPortPendingWarpArea;
        mPortSpawnAtArea = mPortPendingWarpArea;
        mPortSpawnAtFrame = mUpdateFrameIdx;
        // PrepareAreaTransition ends with SetIsDarkWorld(!mIsDarkWorld), which is what a light<->dark
        // portal crossing does. This hop is not a portal crossing, so PortPlaceArrivalTick sets the
        // flag from the destination's own area attributes once the flow is done.
        mPortPendingWarpArea = kInvalidAreaId;
        mPortWarpStreamStarted = false;
        mPortWarpWaitingForIds = false;
        mPortWarpDroppedAreas = false;
        if (mPortWarpLoadPaused) {
          mPortWarpLoadPaused = false;
          world->SetLoadPauseState(false);
        }
        return;
      }
      mPortWarpWaitingForIds = false;
      mPortWarpStreamStarted = true;
      std::fprintf(stderr, "[warp] area %d: streaming (%d of %d object ids free)\n",
                   mPortPendingWarpArea.Value(), PortFreeObjectIds(), kMaxObjects);
    }
    // A dock's kSM_Load (CScriptDock::AcceptScriptMsg) preloads the area behind a door but pauses
    // its validation until the door opens, so the area stops at kP_WaitForValidation. A warp never
    // opens that door: Temple Grounds 52 sends it for 53, and a hop from 52 after that script had
    // run streamed 53 to that phase and then waited for the rest of the run. The warp is the opening.
    if (area->GetPhase() == CGameArea::kP_WaitForValidation) {
      std::fprintf(stderr, "[warp] area %d: its validation was held by a dock; releasing it\n",
                   mPortPendingWarpArea.Value());
    }
    area->SetValidationPaused(false);
    if (world->ScheduleAreaToLoad(area, *this)) {
      area->StartStreamIn(*this);
    }
    // A stream that is running allocates its script objects as it goes, so the parts it has not
    // reached yet still need room. Failing areas can come back while the stream runs (the world's
    // own travel is paused, but an area that was already mid-load when the warp arrived can finish),
    // so try the same drop once more while the free count is at the floor.
    if (PortFreeObjectIds() < kPortWarpMinFreeIds && (mUpdateFrameIdx % 60) == 0) {
      const TAreaId keep = world->GetCurrentAreaId();
      if (PortUnloadAreasExcept(*this, *world, keep, mPortPendingWarpArea)) {
        std::fprintf(stderr, "[warp] dropped the areas around %d while area %d streams (%d free)\n",
                     keep.Value(), mPortPendingWarpArea.Value(), PortFreeObjectIds());
      }
    }
    return;
  }
  mPortWarpWaitingForIds = false;
  mPortWarpStreamStarted = false;
  if (mPortWarpLoadPaused) {
    mPortWarpLoadPaused = false;
    world->SetLoadPauseState(false);
  }
  const TAreaId target = mPortPendingWarpArea;
  mPortPendingWarpArea = kInvalidAreaId;
  mPortWarpDroppedAreas = false;

  // Loaded now, so the move is safe. This is also what makes the world update travel here
  // instead of back to the old area.
  if (world->GetCurrentAreaId() != target) {
    SetCurrentAreaId(target);
    gpGameState->CurrentWorldState().SetAreaId(target);
    if (mCameraManagers[0]->IsInCinematicCamera()) {
      mCameraManagers[0]->StopCinematics(*this);
    }
    std::fprintf(stderr, "[warp] moved to area %d\n", target.Value());
  }
  PortSetDarkWorldFromArea(target);

  if (!PortPlaceKeptTransform()) {
    PortPlaceAtArrival(target);
  }
  for (uint i = 0; i < mNumPlayers; ++i) {
    mCameraManagers[i]->ResetCameras(*this);
  }

  // Dropping the areas around the new one *here* crashes: other subsystems still hold the area the
  // player came from for a frame or more (the HUD builds its dynamic lights against it, and the run
  // segfaulted in CActorLights::BuildAreaLightList 200 log lines later). So the drop is scheduled a
  // short while after arrival, once the game has rebuilt its view of the new area - the areas a
  // sweep has left behind otherwise stay loaded and hold their objects, and two areas' worth plus a
  // third area's stream is what exhausts the 1024-id pool (a walk of world 1baa96c2 died there with
  // 558 ids live).
  // A request that arrived while this one was streaming is taken now, so a sweep's next hop is not
  // lost. It sets mPortPendingWarpArea again, and that is also what keeps the scheduled drop below
  // from throwing away the area this warp is about to stream.
  if (mPortWarpQueuedArea != kInvalidAreaId) {
    const TAreaId queued = mPortWarpQueuedArea;
    mPortWarpQueuedArea = kInvalidAreaId;
    std::fprintf(stderr, "[warp] taking the queued area %d\n", queued.Value());
    PortWarpToArea(queued);
  }
  mPortDropAreasAtFrame = mUpdateFrameIdx + kPortDropAreasDelay;
}

// The area in the other dimension that overlays `aid`. Dark Aether's rooms are built on their Light
// twins' coordinates (a portal crossing keeps the player's position), so the twin is the area whose
// bounds contain the player and overlap this area's the most. The automapper's light/dark flag (CMapArea
// bit 0) rules out a neighbour on the same side when both map areas are loaded; a neighbour whose bounds
// merely touch is ruled out by the overlap floor.
TAreaId CStateManager::PortDimensionCounterpart(TAreaId aid, const CVector3f& pos) {
  CWorld* world = World();
  if (world == nullptr || !world->DoesAreaExist(aid) || world->Area(aid) == nullptr) {
    return kInvalidAreaId;
  }
  const CAABox& from = world->Area(aid)->GetAABB();
  const auto volume = [](const CVector3f& lo, const CVector3f& hi) {
    const float x = hi.GetX() - lo.GetX();
    const float y = hi.GetY() - lo.GetY();
    const float z = hi.GetZ() - lo.GetZ();
    return x > 0.f && y > 0.f && z > 0.f ? x * y * z : 0.f;
  };
  const float fromVolume = volume(from.GetMinPoint(), from.GetMaxPoint());
  const CMapWorld* mapWorld = world->IGetMapWorld();
  const CMapArea* fromMap = mapWorld != nullptr ? mapWorld->GetMapArea(aid.Value()) : nullptr;
  TAreaId best = kInvalidAreaId;
  float bestScore = 0.5f; // intersection over union; a twin is close to 1
  for (int i = 0; i < world->IGetAreaCount(); ++i) {
    const TAreaId other(i);
    if (other == aid || !world->DoesAreaExist(other)) {
      continue;
    }
    const CGameArea* area = world->Area(other);
    if (area == nullptr || area->GetAreaAssetId() == kInvalidAssetId) {
      continue;
    }
    const CAABox& to = area->GetAABB();
    const float slack = 1.f;
    if (pos.GetX() < to.GetMinPoint().GetX() - slack || pos.GetX() > to.GetMaxPoint().GetX() + slack ||
        pos.GetY() < to.GetMinPoint().GetY() - slack || pos.GetY() > to.GetMaxPoint().GetY() + slack ||
        pos.GetZ() < to.GetMinPoint().GetZ() - slack || pos.GetZ() > to.GetMaxPoint().GetZ() + slack) {
      continue;
    }
    if (fromMap != nullptr) {
      const CMapArea* toMap = mapWorld->GetMapArea(i);
      if (toMap != nullptr && toMap->IsInDarkWorld() == fromMap->IsInDarkWorld()) {
        continue;
      }
    }
    const CVector3f lo(rstl::max_val(from.GetMinPoint().GetX(), to.GetMinPoint().GetX()),
                       rstl::max_val(from.GetMinPoint().GetY(), to.GetMinPoint().GetY()),
                       rstl::max_val(from.GetMinPoint().GetZ(), to.GetMinPoint().GetZ()));
    const CVector3f hi(rstl::min_val(from.GetMaxPoint().GetX(), to.GetMaxPoint().GetX()),
                       rstl::min_val(from.GetMaxPoint().GetY(), to.GetMaxPoint().GetY()),
                       rstl::min_val(from.GetMaxPoint().GetZ(), to.GetMaxPoint().GetZ()));
    const float both = volume(lo, hi);
    const float either = fromVolume + volume(to.GetMinPoint(), to.GetMaxPoint()) - both;
    const float score = either > 0.f ? both / either : 0.f;
    std::fprintf(stderr, "[warp] dimension: area %d overlaps %d by %.2f\n", i, aid.Value(), score);
    if (score > bestScore) {
      bestScore = score;
      best = other;
    }
  }
  return best;
}

void CStateManager::PortWarpToDimension() {
  CWorld* world = World();
  CPlayer* player = mPlayers[0];
  if (world == nullptr || player == nullptr) {
    return;
  }
  const TAreaId here = world->GetCurrentAreaId();
  const CTransform4f place = player->GetTransform();
  const TAreaId twin = PortDimensionCounterpart(here, place.GetTranslation());
  if (twin == kInvalidAreaId) {
    std::fprintf(stderr, "[warp] dimension: area %d has no twin in the other dimension\n",
                 here.Value());
    PortWarp::NoteDimension(false);
    return;
  }
  std::fprintf(stderr, "[warp] dimension: area %d -> %d\n", here.Value(), twin.Value());
  PortWarp::NoteDimension(true);
  PortWarpToArea(twin);
  if (mPortPendingWarpArea == twin || mPortWarpQueuedArea == twin) {
    mPortWarpKeepPlace = true;
    mPortWarpPlace = place;
  }
}

// A dimension warp's arrival: the player stays where they stood on the other side. False for any
// other warp, which then takes the area's first spawn point.
bool CStateManager::PortPlaceKeptTransform() {
  if (!mPortWarpKeepPlace) {
    return false;
  }
  mPortWarpKeepPlace = false;
  mPlayers[0]->Teleport(mPortWarpPlace, *this, true);
  std::fprintf(stderr, "[warp] kept the player's place across the dimensions\n");
  return true;
}

// Stand the player in the nearest doorway of this area, facing through it, so a scripted walk
// exercises the real transition path (door damage, the dock's touch/crossing state machine,
// the next area's load) instead of the warp's direct travel.
void CStateManager::PortWarpToDock() {
  CWorld* world = World();
  CPlayer* player = GetPlayer(0);
  if (world == nullptr || player == nullptr) {
    return;
  }
  const TAreaId area = world->GetCurrentAreaId();
  const CVector3f from = player->GetTranslation();
  CScriptDock* nearest = nullptr;
  float best = 1e30f;
  CObjectList* objects = mObjectLists[kOL_All].get();
  for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
    CScriptDock* dock = TCastToPtr< CScriptDock >((*objects)[i]);
    if (dock == nullptr || dock->IsVirtual() || dock->GetCurrentAreaId() != area) {
      continue;
    }
    const float distance = (dock->GetTranslation() - from).Magnitude();
    if (distance < best) {
      best = distance;
      nearest = dock;
    }
  }
  if (nearest == nullptr) {
    std::fprintf(stderr, "[warp] no dock in area %d\n", area.Value());
    return;
  }
  // A dock's own area id is the area it connects to, so its forward is the way through.
  const CTransform4f dockXf = nearest->GetTransform();
  const CVector3f forward = dockXf.GetForward();
  const CVector3f stand = nearest->GetTranslation() - forward * 5.f;
  std::fprintf(stderr, "[warp] dock in area %d -> area %d (%d m away)\n", area.Value(),
               nearest->GetAreaId().Value(), static_cast< int >(best));
  player->Teleport(CTransform4f::LookAt(stand, stand + forward, CVector3f::Up()), *this, true);
}

// Stand in front of the nearest door of this area, facing it: a scripted shot then tests the
// door's damage path, which is what opens doors in this game.
void CStateManager::PortWarpToDoor() {
  CWorld* world = World();
  CPlayer* player = GetPlayer(0);
  if (world == nullptr || player == nullptr) {
    return;
  }
  const TAreaId area = world->GetCurrentAreaId();
  const CVector3f from = player->GetTranslation();
  CScriptDoor* nearest = nullptr;
  float best = 1e30f;
#ifdef TARGET_PC
  // Port diagnostic (temporary): MP2_DEBUG_DOORS=1 lists the area's doors, MP2_WARP_DOOR=<id>
  // picks one by its script id instead of the nearest - a crash report names the door, not where
  // the player stood.
  static const bool sLogDoors = std::getenv("MP2_DEBUG_DOORS") != nullptr;
  static const char* sDoorId = std::getenv("MP2_WARP_DOOR");
  const unsigned wanted = sDoorId != nullptr ? std::strtoul(sDoorId, nullptr, 16) : ~0u;
#endif
  CObjectList* objects = mObjectLists[kOL_All].get();
  for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
    CScriptDoor* door = TCastToPtr< CScriptDoor >((*objects)[i]);
    if (door == nullptr || door->GetCurrentAreaId() != area) {
      continue;
    }
    const float distance = (door->GetTranslation() - from).Magnitude();
#ifdef TARGET_PC
    if (sLogDoors) {
      std::fprintf(stderr, "[warp] door %04x in area %d: %d m away\n", door->GetUniqueId().Value(),
                   area.Value(), static_cast< int >(distance));
    }
    if (wanted != ~0u && door->GetUniqueId().Value() == wanted) {
      nearest = door;
      best = distance;
      continue;
    }
#endif
    if (distance < best) {
      best = distance;
      nearest = door;
    }
  }
  if (nearest == nullptr) {
    std::fprintf(stderr, "[warp] no door in area %d\n", area.Value());
    return;
  }
  // Stand on the side the player is already on and look straight at the door: the door's own
  // forward is not a reliable "outward" (it depends on how the level author rotated it).
  const CVector3f toDoor = (nearest->GetTranslation() - from);
  CVector3f dir = toDoor;
  if (!dir.CanBeNormalized()) {
    dir = CVector3f(0.f, 1.f, 0.f);
  } else {
    dir.Normalize();
  }
  const CVector3f stand = nearest->GetTranslation() - dir * 6.f;
  std::fprintf(stderr, "[warp] door %04x in area %d (%d m away), standing 6 m out\n",
               nearest->GetUniqueId().Value(), area.Value(), static_cast< int >(best));
  player->Teleport(CTransform4f::LookAt(stand, nearest->GetTranslation(), CVector3f::Up()), *this,
                   true);
}

// Stand on the nearest save station of this area, so a scripted A press exercises saving.
void CStateManager::PortWarpToStation() {
  CWorld* world = World();
  CPlayer* player = GetPlayer(0);
  if (world == nullptr || player == nullptr) {
    return;
  }
  const TAreaId area = world->GetCurrentAreaId();
  const CVector3f from = player->GetTranslation();
  CScriptSpecialFunction* nearest = nullptr;
  float best = 1e30f;
  CObjectList* objects = mObjectLists[kOL_All].get();
  for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
    CScriptSpecialFunction* special = TCastToPtr< CScriptSpecialFunction >((*objects)[i]);
    if (special == nullptr || special->GetCurrentAreaId() != area ||
        special->GetFunction() != CScriptSpecialFunction::kSF_SaveStation) {
      continue;
    }
    const float distance = (special->GetTranslation() - from).Magnitude();
    if (distance < best) {
      best = distance;
      nearest = special;
    }
  }
  if (nearest == nullptr) {
    std::fprintf(stderr, "[warp] no save station in area %d\n", area.Value());
    return;
  }
  const CVector3f at = nearest->GetTranslation();
  std::fprintf(stderr, "[warp] save station %04x in area %d (%d m away)\n",
               nearest->GetUniqueId().Value(), area.Value(), static_cast< int >(best));
  player->Teleport(CTransform4f::LookAt(at, at + CVector3f(0.f, 1.f, 0.f), CVector3f::Up()), *this,
                   true);
  // The station's own trigger volume sends this when the player steps onto the pad; teleporting
  // onto it does not necessarily land inside that volume, so send it the same message.
  DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, nearest->GetUniqueId(), kSM_Action));
}

// Kill the player, which is the only way to reach the death and respawn path without an enemy or a
// hazard to walk into. `CPlayer::TakeDamage` only accumulates damage for the frame's own damage
// step (its PreThink clears the accumulator), so a request that lands outside that window is
// dropped - go through the state manager's kill, which is what a fatal hit uses and which also
// tells the game mode, so the death sequence and respawn run.
void CStateManager::PortWarpToDeath() {
  CPlayer* player = GetPlayer(0);
  if (player == nullptr) {
    return;
  }
  const float health = PlayerState(0)->GetHealthInfo().GetHP();
  std::fprintf(stderr, "[warp] killing the player (health %f)\n", health);
  KillPlayer(health, player->GetUniqueId(), kInvalidUniqueId);
}

// Refill the player's energy, so a sweep can survive an atmosphere that damages the player every
// frame (the dark worlds') instead of dying in it. The game's own refill is a health pickup: it
// runs `CPlayerState::IncrPickUp(kIT_HealthRefill, n)`, which adds through `IncrementHealth` and
// clamps to `CalculateHealth()` (base health plus 100 per energy tank) - the same call an energy
// tank pickup makes. Asking for the maximum therefore fills the meter exactly.
void CStateManager::PortWarpToHeal() {
  CPlayerState* state = PlayerState(0);
  if (state == nullptr) {
    return;
  }
  const float before = state->GetHealthInfo().GetHP();
  const float maximum = state->CalculateHealth();
  state->IncrPickUp(CPlayerState::kIT_HealthRefill, static_cast< int >(maximum));
  std::fprintf(stderr, "[warp] healed to %f energy (was %f, max %f)\n",
               state->GetHealthInfo().GetHP(), before, maximum);
}

// Activate a layer of an area. This is the script's own path (CScriptLayerController's kSM_Load and
// then, once the layer reports ready, its kSM_Play), because a layer's script objects - and with
// them the actors of any module that lives in that layer - only exist after ActivateLayerDynamic.
// The layer's *data* streams in first (GetLayerPhase kLP_Inactive -> kLP_Loading -> kLP_Ready), so
// the request is finished over frames by PortLayerTick.
void CStateManager::PortWarpToLayer(TAreaId aid, int layer) {
  CWorld* world = World();
  if (world == nullptr || !world->DoesAreaExist(aid)) {
    std::fprintf(stderr, "[layer] area %d does not exist in this world\n", aid.Value());
    return;
  }
  CGameArea* area = world->Area(aid);
  if (area == nullptr) {
    std::fprintf(stderr, "[layer] area %d has no area object\n", aid.Value());
    return;
  }
  if (mCurrentWorldLayerState.IsNull()) {
    std::fprintf(stderr, "[layer] no world layer state yet\n");
    return;
  }
  CWorldLayerState& layers = *mCurrentWorldLayerState;
  // GetLayerCount/IsLayerActive index mAreaLayers without a range check (only GetLayerName checks),
  // so an area id the layer state does not have must not reach them.
  if (aid.Value() < 0 || aid.Value() >= static_cast< int >(layers.GetAreaLayers().size())) {
    std::fprintf(stderr, "[layer] area %d is not in the world layer state\n", aid.Value());
    return;
  }
  const TLayerId id(layer);
  // Name every layer of the area (and say which are active), so a run can pick the one a module's
  // actors live in instead of guessing: ElitePirate, for instance, appears in no area's layer 0.
  const int count = layers.GetLayerCount(aid);
  std::fprintf(stderr, "[layer] area %d has %d layers:", aid.Value(), count);
  for (int i = 0; i < count; ++i) {
    std::fprintf(stderr, " %d='%s'%s", i, layers.GetLayerName(aid, TLayerId(i)).c_str(),
                 layers.IsLayerActive(aid, TLayerId(i)) ? " active" : "");
  }
  std::fprintf(stderr, "\n");
  if (!area->IsValidLayerNumber(*this, id)) {
    std::fprintf(stderr, "[layer] layer %d is not one of them\n", layer);
    return;
  }
  if (layers.IsLayerActive(aid, id) && area->GetLayerPhase(id) == CGameArea::kLP_Active) {
    std::fprintf(stderr, "[layer] layer %d of area %d is already active\n", layer, aid.Value());
    return;
  }
  layers.SetLayerActive(aid, id, true);
  // An area that is not loaded has no layer phases yet (LoadLayerDynamic indexes them unchecked);
  // the active flag is enough there, the area's own load streams its active layers in.
  if (!area->IsLoaded()) {
    std::fprintf(stderr, "[layer] area %d is not loaded: layer %d marked active for its load\n",
                 aid.Value(), layer);
    return;
  }
  area->LoadLayerDynamic(*this, id);
  mPortLayerArea = aid;
  mPortLayer = id;
  mPortLayerRetries = 0;
  std::fprintf(stderr, "[layer] area %d layer %d: phase %d after the load request\n", aid.Value(),
               layer, static_cast< int >(area->GetLayerPhase(id)));
}

bool CStateManager::PortLayerFlagOn(TAreaId aid, int layer) const {
  if (mCurrentWorldLayerState.IsNull() || aid.Value() < 0 ||
      aid.Value() >= static_cast< int >(mCurrentWorldLayerState->GetAreaLayers().size()) ||
      layer < 0 || layer >= mCurrentWorldLayerState->GetLayerCount(aid)) {
    return false;
  }
  return mCurrentWorldLayerState->IsLayerActive(aid, TLayerId(layer));
}

void CStateManager::PortLayerTick() {
  if (mPortLayerArea == kInvalidAreaId) {
    return;
  }
  CWorld* world = World();
  CGameArea* area =
      world != nullptr && world->DoesAreaExist(mPortLayerArea) ? world->Area(mPortLayerArea) : nullptr;
  if (area == nullptr || !area->IsLoaded()) {
    // The area was unloaded (the player moved on, or a world change replaced it). Its layer phases
    // survive that but its post-construction data does not, and ActivateLayerDynamic dereferences
    // that - so a request whose area is gone is dropped rather than acted on.
    if (area != nullptr) {
      std::fprintf(stderr, "[layer] area %d is no longer loaded; dropping the layer request\n",
                   mPortLayerArea.Value());
    }
    mPortLayerArea = kInvalidAreaId;
    return;
  }
  switch (area->GetLayerPhase(mPortLayer)) {
  case CGameArea::kLP_Ready:
    area->ActivateLayerDynamic(*this, mPortLayer);
    std::fprintf(stderr, "[layer] area %d layer %d activated (%d object ids free)\n",
                 mPortLayerArea.Value(), mPortLayer.Value(), PortFreeObjectIds());
    mPortLayerArea = kInvalidAreaId;
    break;
  case CGameArea::kLP_Active:
    std::fprintf(stderr, "[layer] area %d layer %d was already active\n", mPortLayerArea.Value(),
                 mPortLayer.Value());
    mPortLayerArea = kInvalidAreaId;
    break;
  case CGameArea::kLP_Inactive:
    // Nothing is loading it: ask again a few times, then give up with one line. A layer the disc
    // does not carry never leaves this phase, and a run should not be left guessing whether the
    // request was even seen.
    if ((mUpdateFrameIdx % 120) == 0) {
      ++mPortLayerRetries;
      if (mPortLayerRetries > 5) {
        std::fprintf(stderr, "[layer] area %d layer %d never started loading; giving up\n",
                     mPortLayerArea.Value(), mPortLayer.Value());
        mPortLayerArea = kInvalidAreaId;
        break;
      }
      area->LoadLayerDynamic(*this, mPortLayer);
      std::fprintf(stderr, "[layer] area %d layer %d: still inactive, asked again (%d)\n",
                   mPortLayerArea.Value(), mPortLayer.Value(), mPortLayerRetries);
    }
    break;
  default:
    break;
  }
}

// Stand on the nearest elevator of this area and tell it to go, which is the game's own world
// transition (CScriptWorldTeleporter: set the world, desired area, restart through CMainFlow).
void CStateManager::PortWarpToWorldPortal() {
  CWorld* world = World();
  if (world == nullptr) {
    return;
  }
  // A world teleporter is a logical entity with no transform of its own (its elevator is a
  // separate actor), so there is nothing to stand on: take the area's first one and tell it to go.
  // That is the game's own world transition (CScriptWorldTeleporter::AcceptScriptMsg sets the
  // world and the desired area, then restarts through CMainFlow).
  const TAreaId area = world->GetCurrentAreaId();
  CObjectList* objects = mObjectLists[kOL_All].get();
  for (int i = objects->GetFirstObjectIndex(); i != -1; i = objects->GetNextObjectIndex(i)) {
    CScriptWorldTeleporter* portal = TCastToPtr< CScriptWorldTeleporter >((*objects)[i]);
    if (portal == nullptr || portal->GetCurrentAreaId() != area || !portal->GetActive()) {
      continue;
    }
    std::fprintf(stderr, "[warp] elevator %04x in area %d: going through it\n",
                 portal->GetUniqueId().Value(), area.Value());
    DeliverScriptMsg(CScriptMsg(kInvalidUniqueId, portal->GetUniqueId(), kSM_SetToZero));
    return;
  }
  std::fprintf(stderr, "[warp] no elevator in area %d\n", area.Value());
}

// A world change has to name a *real* destination area. The game's own world change (an elevator)
// always has one from its script; a request that names only a world does not, and the loader then
// hands a null area to `CWorld::ScheduleAreaToLoad` (or asks the resource loader for a world whose
// pak was never mounted) and segfaults. The save knows which areas it has seen in each world, so
// take the first of those.
//
// The save's table only lists worlds the player has *visited*, so it cannot answer for a world the
// save has never walked to (only an elevator reaches one today) - which is what keeps every area of
// such a world out of reach of a direct boot. When the save has no area of the world, the disc
// does: fall back to the world's own MLVL.
namespace {
// Reads the world's MLVL (`worldId`, which its pak must own) into a `CDummyWorld` and hands it to
// `fn(world, loader)`; false when the MLVL could not be read. The MLVL is parsed by `CDummyWorld`,
// the same reader `CWorld` and the data network use, so the format is not parsed twice.
//
// A world pak is normally stashed in ARAM while the game is in another world, and a stashed pak's
// resource table cannot be read (an MLVL in a stashed pak has size 0), so the pak that owns the id
// is unstashed first - and only that one: `CMain::EnsureWorldPakReady` stashes every pak that does
// not own the id, which must not happen before the id is known to name a world (a mistyped
// `MP2_WORLD` would leave every world pak stashed).
template < typename F >
bool PortWithMlvl(CAssetId worldId, F fn) {
  if (gpResourceFactory == nullptr || gpGameState == nullptr) {
    return false;
  }
  CResLoader& loader = gpResourceFactory->GetResLoader();
  CPakFile* owner = nullptr;
  const int pakCount = loader.GetPakCount();
  for (int i = 0; i < pakCount; ++i) {
    CPakFile* pak = loader.GetPakFile(i);
    if (pak == nullptr) {
      continue;
    }
    // Every pak's list is searched, not only the world paks': the loader only lists paks that are
    // loaded, and a non-world pak is never stashed, so this cannot pick an unreadable one.
    const rstl::vector< rstl::pair< rstl::string, SObjectTag > >& names =
        pak->GetStringToObjectList();
    for (rstl::vector< rstl::pair< rstl::string, SObjectTag > >::const_iterator it = names.begin();
         it != names.end(); ++it) {
      if (it->second.GetId() == worldId) {
        owner = pak;
        break;
      }
    }
    if (owner != nullptr) {
      break;
    }
  }
  if (owner == nullptr) {
    // The name list survives stashing, so a pak that is still loading is the only reason a real
    // world is missing from the list here (the loader lists a pak once it has loaded) - say which,
    // because that one resolves on a later request.
    std::fprintf(stderr,
                 "[warp] %08x: none of the %d loaded paks owns it (all paks loaded: %d), so the "
                 "disc cannot name an area of it either\n",
                 worldId, pakCount, loader.AreAllPaksLoaded() ? 1 : 0);
    return false;
  }
  if (!owner->IsCompletelyLoaded()) {
    std::fprintf(stderr, "[warp] %08x: the pak that owns it is still loading, so its areas are "
                         "not readable yet\n",
                 worldId);
    return false;
  }
  if (owner->IsWorldPak()) {
    // A world pak is stashed in ARAM while the game is in another world, and a stashed pak has no
    // readable resource table; a non-world pak is never stashed and needs nothing.
    owner->EnsureWorldPakReady();
  }
  if (loader.ResourceSize(SObjectTag('MLVL', worldId)) == 0) {
    std::fprintf(stderr, "[warp] %08x: the pak that owns it has no MLVL for it\n", worldId);
    return false;
  }

  CDummyWorld world(worldId, false);
  // The read is asynchronous, so wait for it here: the parse is of a few hundred KB and the DVD
  // thread is doing the work. The cap is a hang guard in wall-clock time, so a busy machine (smoke
  // runs in parallel) can't run it out the way a spin count did; the yield gives the DVD thread
  // the core.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!world.ICheckWorldComplete()) {
    if (std::chrono::steady_clock::now() > deadline) {
      std::fprintf(stderr, "[warp] %08x: its MLVL never finished loading\n", worldId);
      return false;
    }
    std::this_thread::yield();
  }

  fn(world, loader);
  return true;
}

// The first area of the world's MLVL that names an MREA.
CAssetId PortFirstAreaFromMlvl(CAssetId worldId) {
  CAssetId result = kInvalidAssetId;
  PortWithMlvl(worldId, [&](CDummyWorld& world, CResLoader& loader) {
    const int areaCount = world.IGetAreaCount();
    CAssetId firstUnbackedArea = kInvalidAssetId;
    int firstUnbackedIndex = -1;
    for (int i = 0; i < areaCount; ++i) {
      const IGameArea* area = world.IGetAreaAlways(TAreaId(i));
      if (area == nullptr) {
        continue;
      }
      const CAssetId areaId = area->IGetAreaAssetId();
      if (areaId == kInvalidAssetId) {
        // An area slot with no MREA at all: the game cannot travel to it (the map skips it for the
        // same reason).
        continue;
      }
      if (loader.ResourceExists(SObjectTag('MREA', areaId))) {
        std::fprintf(stderr,
                     "[warp] %08x: the save knows no area of it; the disc's MLVL has %d areas, taking "
                     "area %d (%08x)\n",
                     worldId, areaCount, i, areaId);
        result = areaId;
        return;
      }
      // An MREA the loader cannot see is served as *empty bytes*, so landing in such an area would
      // only move the failure: remember the first one for the report and keep looking.
      if (firstUnbackedArea == kInvalidAssetId) {
        firstUnbackedArea = areaId;
        firstUnbackedIndex = i;
      }
    }

    if (firstUnbackedArea != kInvalidAssetId) {
      std::fprintf(stderr,
                   "[warp] %08x: the save knows no area of it; the disc's MLVL has %d areas but no "
                   "MREA of it is in a loaded pak (first is area %d, %08x; all paks loaded: %d)\n",
                   worldId, areaCount, firstUnbackedIndex, firstUnbackedArea,
                   loader.AreAllPaksLoaded() ? 1 : 0);
      return;
    }

    std::fprintf(stderr, "[warp] %08x: the disc's MLVL has %d areas and none of them names an MREA\n",
                 worldId, areaCount);
    return;
  });
  return result;
}

// The area of `worldId`'s MLVL whose MLVL name is `name` (case-insensitive). Returns its MREA id,
// and its index in `index`. An area slot without an MREA is not an area the game can travel to, so
// a name that only matches one is reported as no match.
CAssetId PortFindAreaInMlvl(CAssetId worldId, const char* name, int* index) {
  CAssetId result = kInvalidAssetId;
  PortWithMlvl(worldId, [&](CDummyWorld& world, CResLoader&) {
    for (int i = 0; i < world.IGetAreaCount(); ++i) {
      const IGameArea* area = world.IGetAreaAlways(TAreaId(i));
      if (area != nullptr && area->IGetAreaAssetId() != kInvalidAssetId &&
          strcasecmp(area->PortGetMlvlAreaName().data(), name) == 0) {
        result = area->IGetAreaAssetId();
        *index = i;
        return;
      }
    }
  });
  return result;
}

// Every MLVL named in a loaded pak's name list, as (name, id). The lists survive a pak being
// stashed, so this needs no unstashing.
void PortCollectMlvls(std::vector< std::pair< std::string, CAssetId > >& out) {
  if (gpResourceFactory == nullptr) {
    return;
  }
  CResLoader& loader = gpResourceFactory->GetResLoader();
  for (int i = 0; i < loader.GetPakCount(); ++i) {
    CPakFile* pak = loader.GetPakFile(i);
    if (pak == nullptr) {
      continue;
    }
    const rstl::vector< rstl::pair< rstl::string, SObjectTag > >& names =
        pak->GetStringToObjectList();
    for (rstl::vector< rstl::pair< rstl::string, SObjectTag > >::const_iterator it = names.begin();
         it != names.end(); ++it) {
      if (it->second.GetType() == 'MLVL') {
        out.push_back(std::make_pair(std::string(it->first.data()), it->second.GetId()));
      }
    }
  }
}

// A world named by a case-insensitive substring of an MLVL resource name ("sand_world" for
// `03_Sand_World_20041018`). Display names such as "Agon Wastes" are not resolved: they live in
// STRGs, not in the name lists.
CAssetId PortFindWorldByName(const char* text) {
  std::vector< std::pair< std::string, CAssetId > > mlvls;
  PortCollectMlvls(mlvls);
  std::string needle(text);
  for (size_t i = 0; i < needle.size(); ++i) {
    needle[i] = static_cast< char >(std::tolower(static_cast< unsigned char >(needle[i])));
  }
  CAssetId found = kInvalidAssetId;
  int matches = 0;
  for (size_t i = 0; i < mlvls.size(); ++i) {
    std::string hay = mlvls[i].first;
    for (size_t c = 0; c < hay.size(); ++c) {
      hay[c] = static_cast< char >(std::tolower(static_cast< unsigned char >(hay[c])));
    }
    if (hay.find(needle) == std::string::npos) {
      continue;
    }
    if (matches == 0) {
      found = mlvls[i].second;
      std::fprintf(stderr, "[warp] world '%s' is %s (%08x)\n", text, mlvls[i].first.c_str(), found);
    }
    ++matches;
  }
  if (matches > 1) {
    std::fprintf(stderr, "[warp] world '%s' also matches %d other MLVL names; took the first\n",
                 text, matches - 1);
  }
  return found;
}

// The world (and area) that owns an area called `name` in its MLVL, searching every MLVL the paks
// name. Reading an MLVL unstashes its pak (see PortWithMlvl); the warp that follows restashes the
// others.
bool PortFindAreaInAnyWorld(const char* name, CAssetId* world, CAssetId* area) {
  std::vector< std::pair< std::string, CAssetId > > mlvls;
  PortCollectMlvls(mlvls);
  for (size_t i = 0; i < mlvls.size(); ++i) {
    int index = -1;
    const CAssetId areaId = PortFindAreaInMlvl(mlvls[i].second, name, &index);
    if (areaId != kInvalidAssetId) {
      std::fprintf(stderr, "[warp] area '%s' is area %d (%08x) of world %s (%08x)\n", name, index,
                   areaId, mlvls[i].first.c_str(), mlvls[i].second);
      *world = mlvls[i].second;
      *area = areaId;
      return true;
    }
  }
  std::fprintf(stderr, "[warp] no area named '%s' in any of the %d MLVLs\n", name,
               static_cast< int >(mlvls.size()));
  return false;
}

// The world pak that owns `worldId`, made readable again if it is stashed.
//
// `CGameState::SetCurrentWorldId` (`CGameState.cpp:748`) calls `CMain::EnsureWorldPakReady`, which
// stashes every world pak that does not own the new world - so from the moment a world change is
// applied the *outgoing* world's pak is unreadable, while the outgoing world is still the live one
// (the state manager's own update finishes this frame, and the in-game GUI updates after it) and
// still asks the loader for its own resources. In /tmp/elite5.log the request that came after the
// stash was the auto-mapper's area-name STRG (`CAutoMapper.cpp:1901` -> `GetObj('STRG', n)`, the
// only by-id STRG request a live area makes); the loader answered "in no loaded pak" with empty
// bytes and `CStringTable::Load` faulted on them.
//
// An elevator never shows that window: it applies the world change seconds into its ride, by which
// time the area it is leaving has asked for and cached its own resources, and it refuses the change
// outright unless the destination's MLVL is already readable (`CScriptWorldTeleporter.cpp:103`). A
// by-id change happens in the same frame the world it is leaving became live, so it has to keep the
// outgoing pak readable itself. Nothing re-stashes it: the restart keeps the game state
// (`CMainFlow::SetGameState`, `kRM_None`), so the cost is one world pak's resource table in memory.
void PortKeepWorldPakReadable(CAssetId worldId) {
  if (worldId == kInvalidAssetId || gpResourceFactory == nullptr) {
    return;
  }
  CResLoader& loader = gpResourceFactory->GetResLoader();
  for (int i = 0; i < loader.GetPakCount(); ++i) {
    CPakFile* pak = loader.GetPakFile(i);
    if (pak == nullptr || !pak->IsWorldPak() || !pak->IsCompletelyLoaded()) {
      continue;
    }
    const rstl::vector< rstl::pair< rstl::string, SObjectTag > >& names =
        pak->GetStringToObjectList();
    for (rstl::vector< rstl::pair< rstl::string, SObjectTag > >::const_iterator it = names.begin();
         it != names.end(); ++it) {
      if (it->second.GetId() != worldId) {
        continue;
      }
      // A pak that is not stashed needs nothing (and `EnsureWorldPakReady` does nothing then).
      pak->EnsureWorldPakReady();
      std::fprintf(stderr,
                   "[warp] %08x: the outgoing world's pak stays readable until the restart "
                   "replaces the world\n",
                   worldId);
      return;
    }
  }
  std::fprintf(stderr, "[warp] %08x: no loaded world pak owns it, so nothing to keep readable\n",
               worldId);
}
} // namespace

CAssetId CStateManager::PortFirstAreaOfWorld(CAssetId worldId) const {
  if (worldId == kInvalidAssetId) {
    return kInvalidAssetId;
  }
  if (gpMemoryCard != nullptr) {
    const rstl::vector< CMemoryCard::MemoryWorld >& worlds = gpMemoryCard->GetMemoryWorlds();
    for (rstl::vector< CMemoryCard::MemoryWorld >::const_iterator it = worlds.begin();
         it != worlds.end(); ++it) {
      if (it->first != worldId) {
        continue;
      }
      // `CSaveWorldIntermediate` fills this list from the world's MLVL with each area's *save* id
      // (`IGetAreaSaveId`), and an area the game keeps no save data for is listed as invalid - an
      // invalid entry would hand the restart an invalid desired-area id (the old logs' "area
      // ffffffff"), so skip them. These ids are not MREA asset ids, and the restart matches the
      // desired id against each area's *asset* id (`CWorld::CheckWorldComplete`), so a world change
      // through this list lands in area 0 of the destination rather than in the area it names.
      const rstl::vector< uint >& savedAreas = it->second.GetAreaIds();
      for (rstl::vector< uint >::const_iterator area = savedAreas.begin(); area != savedAreas.end();
           ++area) {
        if (*area == static_cast< uint >(kInvalidAssetId)) {
          continue;
        }
        const CAssetId areaId = CAssetId(*area);
        std::fprintf(stderr, "[warp] %08x: the save knows %d areas of it, taking area %08x\n",
                     worldId, static_cast< int >(savedAreas.size()), areaId);
        return areaId;
      }
      break;
    }
  }
  return PortFirstAreaFromMlvl(worldId);
}

void CStateManager::PortWarpToWorld(CAssetId worldId, CAssetId areaId) {
  if (worldId == kInvalidAssetId) {
    return;
  }
  if (areaId == kInvalidAssetId) {
    areaId = PortFirstAreaOfWorld(worldId);
    if (areaId == kInvalidAssetId) {
      std::fprintf(stderr,
                   "[warp] %08x: neither the save nor the disc's MLVL names an area of that world, "
                   "so there is nowhere to land\n",
                   worldId);
      return;
    }
  }
  if (gpResourceFactory != nullptr && gpResourceFactory->GetResourceTypeById(worldId) != 'MLVL') {
    // The resource factory only knows resources whose paks are loaded, so a world the game is not
    // in looks unknown here (the save's own table is the authority, and its ids come from the
    // MLVL-filtered resource list). Warn and go, exactly as the elevator does.
    std::fprintf(stderr, "[warp] %08x does not look like a loaded world; going anyway\n", worldId);
  }
  std::fprintf(stderr, "[warp] world %08x area %08x\n", worldId, areaId);

  // The same restart CScriptWorldTeleporter performs: the world is unloaded and CMainFlow
  // loads the new one from the game state, so nothing here touches the outgoing world's
  // scripts or keeps its old area id.
  if (CWorld* world = World(); world != nullptr) {
    world->SetLoadPauseState(true);
  }
  // The destination world's pak must be mounted before the restart asks the resource loader for it:
  // the elevator gets that for free (the game streams the pak during its fade), an immediate request
  // does not, and the loader dereferences the pak it could not find.
  if (gpMain != nullptr) {
    gpMain->EnsureWorldPakReady(worldId);
  }
  const CAssetId outgoingWorld = gpGameState->CurrentWorldAssetId();
  gpGameState->SetCurrentWorldId(worldId);
  // `SetCurrentWorldId` stashes the outgoing world's pak (see PortKeepWorldPakReadable): put it
  // back, because the outgoing world is still live for the rest of this frame and until CMainFlow
  // has restarted. This is the by-id path's half of what an elevator gets from the ride's fade.
  PortKeepWorldPakReadable(outgoingWorld);
  // Mirror CScriptWorldTeleporter::AcceptScriptMsg's kSM_SetToZero case, which is the world change
  // that is known to work: it sets the *desired area* and leaves the area id alone. Clearing the
  // area id here instead restarted the game as if it were new (the intro movie played, and the
  // resource loader was asked for a world it had not mounted).
  gpGameState->CurrentWorldState().SetDesiredAreaAssetId(areaId);
  gpMain->SetRestartMode(CMain::kRM_None);
  QuitGame();
}

// The direct boot (PortWarp::DirectBoot, asked by PortSkipFrontEnd::StartGame once the card's save
// is the running state, before any world is loaded). A boot request for a world and an area
// (MP2_WORLD="<world>,<area>") is written into the game state as the world and its desired area -
// what PortWarpToWorld does at frame 120 of the save's world - so the first world the game loads is
// the destination: no save-world load, no 120-update wait, no restart. The update count starts at
// 120 (PortHarness::StartCountAt) so the run's frame numbers keep their meaning. A request that does
// not resolve to an area of the world here (no area, MP2_TELEPORT, an area index) stays pending for
// PortWarpTick, the old route. MP2_DIRECT_BOOT=0 turns it off.
bool PortWarp::DirectBoot() {
  const char* env = getenv("MP2_DIRECT_BOOT");
  if (env != nullptr && env[0] == '0') {
    return false;
  }
  PortWarp::SRequest request;
  if (!PortWarp::PeekBoot(request) || request.kind != PortWarp::EKind::kWorld ||
      request.area[0] == '\0' || gpGameState == nullptr || gpMain == nullptr) {
    return false;
  }
  CAssetId worldId = kInvalidAssetId;
  CAssetId areaId = kInvalidAssetId;
  ResolveWarpWorld(request.world, request.area, &worldId, &areaId);
  bool inWorld = false;
  if (worldId != kInvalidAssetId && areaId != kInvalidAssetId) {
    PortWithMlvl(worldId, [&](CDummyWorld& world, CResLoader&) {
      inWorld = world.IGetAreaId(areaId) != kInvalidAreaId;
    });
  }
  if (!inWorld) {
    std::fprintf(stderr, "[warp] direct boot: '%s,%s' names no area of a world here; the request "
                         "waits for the save's world (the old route)\n",
                 request.world, request.area);
    return false;
  }
  PortWarp::Consume(request);
  sPortDirectBootArea = areaId;
  gpMain->EnsureWorldPakReady(worldId);
  const CAssetId saveWorld = gpGameState->CurrentWorldAssetId();
  gpGameState->SetCurrentWorldId(worldId);
  gpGameState->CurrentWorldState().SetDesiredAreaAssetId(areaId);
  // The first spawn now happens at the destination, so a new-game card (init-powerups flag set) gets
  // the destination spawn point's inventory, not the save world's start loadout (the old route's).
  std::fprintf(stderr, "[warp] direct boot: world %08x area %08x instead of the save's world %08x\n",
               worldId, areaId, saveWorld);
  PortHarness::StartCountAt(PortHarness::kDirectBootUpdates);
  return true;
}
#endif
