#include "MetroidPrime/CMFGame.hpp"

#ifdef TARGET_PC
#include <stdio.h>

#include "port_console.h"
#include "port_freecam.h"
#include "port_harness.h"
#include "port_radial.h"
#include "port_overlay.h"
#include "port_photo.h"
#include "port_savestate.h"
#include "port_touch.h"
#include "port_pipeline_context.h"
#endif

#include "Kyoto/Audio/CMidiManager.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Audio/CStreamAudioManager.hpp"
#include "Kyoto/Basics/CBasics.hpp"
#include "Kyoto/CFrameDelayedKiller.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Input/IController.hpp"
#include "Kyoto/Math/CMath.hpp"
#include "MetroidPrime/CArchitectureQueue.hpp"
#include "MetroidPrime/CAutoMapper.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CDecalManager.hpp"
#include "MetroidPrime/CGameGlobalObjects.hpp"
#include "MetroidPrime/CGameResultsScreen.hpp"
#include "MetroidPrime/CInGameGuiManager.hpp"
#include "MetroidPrime/CInGameGuiManagerSet.hpp"
#include "MetroidPrime/CMain.hpp"
#include "MetroidPrime/CPortalTransition.hpp"
#include "MetroidPrime/CSaveGameScreen.hpp"
#include "MetroidPrime/CScopedProfiler.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Cameras/CCameraFilterPass.hpp"
#include "MetroidPrime/Cameras/CCinematicCamera.hpp"
#include "MetroidPrime/Decode.hpp"
#include "MetroidPrime/Player/CGameMode.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/Player/CWorldTransManager.hpp"
#ifdef TARGET_PC
#include "MetroidPrime/ScriptObjects/CScriptStreamedMovie.hpp"
#endif
#include "MetroidPrime/TCastTo.hpp"
#include "rstl/math.hpp"

bool CMFGame::mMultiplayerGuiActive;

static const char* const skMultiplayerEndAudio = "/Audio/multi-defbgm-speed-doon32.dsp";

#ifdef TARGET_PC
namespace {
// MP2_SKIP_INTRO asked for the current cinematic's skip (see the timer tick below): the run wants
// the intro over with, so the kFS_CinematicSkip case treats it like the debug setting - the scene's
// own skip first, and never "stop the camera and leave the sequence playing".
bool sHarnessCinematicSkip = false;
// The player asked for the current cinematic's skip with START or A (kAM_UserInput); the
// kFS_CinematicSkip case takes the same path as for the debug setting. Cleared on leaving it.
bool sPortUserCinematicSkip = false;
// A skip waits out a stopped movie scene's gap at normal speed behind black (kFS_CinematicSkip).
bool sPortSkipGapHold = false;
// The credits (or the end-game results) have started: no port skip runs or starts after this, or
// the max-speed loop (one draw a second, CMain) makes them scroll in jerks. Reset with the game flow.
bool sPortGameEnded = false;
// How long a cinematic camera has been current in kFS_InGame; A only skips past kPortSkipAGuard.
float sPortCinematicTime = 0.f;
const float kPortSkipAGuard = 0.5f;
// When the current skip started (PortHarness::Seconds), for the "[skip] done" line.
double sPortSkipStart = 0.0;
} // namespace
#endif

CMFGame::CMFGame(rstl::ncrc_ptr< CStateManager > stateManager,
                 const rstl::ncrc_ptr< CInGameGuiManagerSet >& guiManager,
                 CArchitectureQueue& architectureQueue)
: CIOWin(rstl::string_l("CMFGame"))
, mStateManager(stateManager)
, mGuiManager(guiManager)
, mFlowState(kFS_Zero)
, mFlowTime(0.f)
, mMultiplayerEndFadeTime(0.f)
, mSkippedCineCam(kInvalidUniqueId)
, mPortalTransition(nullptr)
, mTransitionPhase(kTP_None)
, mTransitionFadeTime(1.f)
, mInitialized(false)
, mPlayerAlive(true)
, mEndGameFrameCaptured(false)
, mTransitionFromDarkWorld(false) {
  gpMain->SetGameFlowBuilt(true);
#ifdef TARGET_PC
  sPortGameEnded = false;
#endif
  mMultiplayerGuiActive = false;
  gpGameState->PreviousGameResults().mShowResults = false;
}

CMFGame::~CMFGame() {
  gpMain->SetGameFlowBuilt(false);
  gpMain->SetMaxSpeed(false);
#ifdef TARGET_PC
  sPortGameEnded = false;
#endif
  CSfxManager::SetMuted(false);
  CDecalManager::Reinitialize();
#ifdef TARGET_PC
  // Port: back in the front end there is no game flow and no player, so the touch controls
  // (and anything else reading these) stop seeing the last in-game state: the Ball page on the title.
  PortHarness::GameFlowState(-1, false);
  PortTouch::SetMorphed(false);
  PortTouch::SetScanTextUp(false);
  // Port: the render mode is widened by the aspect setting.
  CGraphics::SetViewport(0, 0, CGraphics::GetRenderMode().fbWidth,
                         CGraphics::GetRenderMode().efbHeight);
  CGraphics::SetScissor(0, 0, CGraphics::GetRenderMode().fbWidth,
                        CGraphics::GetRenderMode().efbHeight);
#else
  CGraphics::SetViewport(0, 0, 640, 448);
  CGraphics::SetScissor(0, 0, 640, 448);
#endif
}

#ifdef TARGET_PC
void CMFGame::PortMarkGameEnded() {
  // The credits' window takes the updates, so kFS_CinematicSkip's update case may never run again
  // to end the skip: drop max speed here, and the draw case below keeps it off.
  sPortGameEnded = true;
  sPortSkipGapHold = false;
  gpMain->SetMaxSpeed(false);
}
#endif

void CMFGame::SetFlowState(EFlowState state) {
  switch (mFlowState) {
  case kFS_CinematicSkip:
    gpMain->SetMaxSpeed(false);
    mGuiManager->StartFadeIn();
    mSkippedCineCam = kInvalidUniqueId;
    CSfxManager::SetMuted(false);
#ifdef TARGET_PC
    std::fprintf(stderr, "[skip] done: %.1f s of game time in %.2f s\n", mFlowTime,
                 PortHarness::Seconds() - sPortSkipStart);
    sPortUserCinematicSkip = false;
    sPortSkipGapHold = false;
    sPortCinematicTime = 0.f;
    PortOverlay::SkipConfirmDisarm();
#endif
    break;
  default:
    break;
  }

  mFlowState = state;
#ifdef TARGET_PC
  // MP2_SKIP_INTRO counts gameplay only in kFS_InGame (see platform/include/port_harness.h).
  PortHarness::GameFlowState(int(state), state == kFS_InGame);
  if (state != kFS_InGame) PortRadial::Cancel();
#endif
  switch (state) {
  case kFS_CinematicSkip:
    CSfxManager::SetMuted(true);
#ifdef TARGET_PC
    sPortSkipStart = PortHarness::Seconds();
#endif
    break;
  case kFS_PortalTransition:
    mStateManager->DeferStateTransition(kSMT_InGame);
    break;
  case kFS_MultiplayerEndFade:
    CSfxManager::SetChannel(CSfxManager::kSC_PauseScreen);
    CStreamAudioManager::FadeOutSoftwareAudio(CStreamAudioManager::kSC_Default, 0.5f);
    CStreamAudioManager::PlaySoftwareAudio(CStreamAudioManager::kSC_OneShot,
                                           rstl::string_l(skMultiplayerEndAudio), 0.01f, 0.01f, 90,
                                           true);
    break;
  default:
    break;
  }
}

CIOWin::EMessageReturn CMFGame::OnMessage(const CArchitectureMessage& message,
                                          CArchitectureQueue& queue) {
  switch (message.GetType()) {
  case kAM_FrameBegin:
    mStateManager->FrameBegin(MakeMsg::GetParmFrameBegin(message).GetInt32());
#ifdef TARGET_PC
    // The console's `hold` runs a frame with no ticks at all, so the timer tick below never comes;
    // serving the queue here keeps the console answering while the simulation is stopped.
    PortConsole::IdleTick(*mStateManager);
#endif
    break;
  case kAM_TimerTick: {
    const bool wasInitialized = mInitialized;
    mInitialized = true;
    const float dt = MakeMsg::GetParmTimerTick(message).GetReal();
#ifdef TARGET_PC
    // The console normally runs inside the state manager's update; this serves it on frames
    // where the flow state skipped that update (portal transition, cinematic skip, pause).
    PortConsole::IdleTick(*mStateManager);
    // A mods reload asked for while paused (Apply now, `reloadmods`): no update runs to service it.
    if (mFlowState == kFS_Paused && PortSaveState::ServicePausedModReload()) {
      mGuiManager->GetPlayerGuiManager(0).PortReloadPauseModels();
    }
#endif
    if (!mStateManager->mSaveGameScreen.null() &&
        mStateManager->mSaveGameScreen->Update(dt) != kMR_Normal && mFlowState == kFS_Paused) {
      mStateManager->DeleteSaveGameScreen();
    }

    if (mTransitionPhase != kTP_None) {
      if (mStateManager->GetShowSoftTransition()) {
        mTransitionFadeTime = rstl::max_val(0.f, mTransitionFadeTime - dt);
      } else {
        mTransitionFadeTime = 0.f;
      }
      switch (mTransitionPhase) {
      case kTP_FadeOut:
        if (mTransitionFadeTime == 0.f) {
          mTransitionPhase = kTP_Load;
          mTransitionFadeTime = 1.f;
        }
        break;
      case kTP_Load:
        if (!mPortalTransition.null() && !mPortalTransition->IsReady()) {
          mTransitionFadeTime = 1.f;
        } else if (mTransitionFadeTime == 0.f) {
          mTransitionPhase = kTP_Play;
        }
        break;
      case kTP_FadeIn:
        if (mTransitionFadeTime != 0.f) {
          break;
        }
        mTransitionPhase = kTP_Complete;
        mTransitionFadeTime = 1.f;
        // Fall through.
      case kTP_Complete:
        if (mTransitionFadeTime == 0.f) {
          mTransitionPhase = kTP_None;
          mTransitionFadeTime = 1.f;
        }
        break;
      }
    }
    if (mFlowState == kFS_Zero) {
      SetFlowState(kFS_InGame);
    }
#ifdef TARGET_PC
    {
      // Port: photo mode resolves its enter/exit requests once per update, and leaves when the
      // game it was entered in goes away.
      const CCinematicCamera* const photoCine = TCastToConstPtr< CCinematicCamera >(
          *mStateManager->GetCameraManager(0)->GetCurrentCamera(*mStateManager, true));
      PortPhoto::Tick(*mStateManager, mFlowState == kFS_InGame && !photoCine &&
                                          mTransitionPhase == kTP_None);
    }
#endif
#ifdef TARGET_PC
    // MP2_SKIP_INTRO: a cinematic before gameplay is skipped the way START skips it in kAM_UserInput
    // (MP2_SKIP_CINEMATICS still decides whether one the player has not seen may be skipped).
    if (mFlowState == kFS_InGame && !sPortGameEnded && !mStateManager->IsMultiplayer() &&
        gpGameState->GetGameMode().GetGameModeType() != 'FRND') {
      const CCinematicCamera* const introCam = TCastToConstPtr< CCinematicCamera >(
          *mStateManager->GetCameraManager(0)->GetCurrentCamera(*mStateManager, true));
      sPortCinematicTime = introCam ? sPortCinematicTime + dt : 0.f;
      if (!introCam) {
        PortOverlay::SkipConfirmDisarm();
      } else if (PortOverlay::TakeSkipConfirmTap() && PortOverlay::SkipConfirmArmed()) {
        // A tap on the "Skip" prompt is the second press.
        PortOverlay::SkipConfirmDisarm();
        CMidiManager::StopAll();
        mSkippedCineCam = introCam->GetScriptCameraId();
        sPortUserCinematicSkip = true;
        SetFlowState(kFS_CinematicSkip);
        mFlowTime = 0.f;
      }
      if (introCam && PortHarness::IntroSkipCinematic(introCam->GetScriptCameraId().Value())) {
        CMidiManager::StopAll();
        mSkippedCineCam = introCam->GetScriptCameraId();
        sHarnessCinematicSkip = true;
        SetFlowState(kFS_CinematicSkip);
        mFlowTime = 0.f;
      }
    }
#endif

    switch (mFlowState) {
    case kFS_PortalTransition:
      for (uint i = 0; i < uint(mStateManager->GetNumPlayers()); ++i) {
        mGuiManager->Update(*mStateManager, dt, queue, IsCameraActiveFlow(), i);
      }
      mPortalTransitionTime += dt;
      if (mTransitionPhase >= kTP_Load && mTransitionPhase <= kTP_FadeIn &&
          !mPortalTransition.null()) {
#ifdef TARGET_PC
        // Port: "Portal transition > Fast" plays the ride at double speed.
        mPortalTransition->Update(PortOverlay::PortalTransition() == PortOverlay::kPortalTransition_Fast
                                      ? dt * 2.f
                                      : dt);
#else
        mPortalTransition->Update(dt);
#endif
      }
      if (mStateManager->IsFullyInitialized()) {
        if (mPortalTransition.null() || mPortalTransition->IsFinished()) {
          if (mTransitionPhase == kTP_Play) {
            mTransitionPhase = kTP_FadeIn;
            mTransitionFadeTime = 1.f;
          }
          if (mTransitionPhase == kTP_Complete) {
            mStateManager->SetRandomAvailable(true);
            mStateManager->Update(dt, queue);
            mStateManager->SetRandomAvailable(false);
            mStateManager->SetPendingDockArea(kInvalidAreaId);
            SetFlowState(kFS_InGame);
#ifdef TARGET_PC
            std::fprintf(stderr, "[portal] ride over after %.2f s (mode %d)\n",
                         mPortalTransitionTime, PortOverlay::PortalTransition());
#endif
            mPortalTransition = nullptr;
          }
        }
        return kMR_Exit;
      }
      mStateManager->InitializeState(mStateManager->GetWorld()->GetWorldAssetId(),
                                     mStateManager->GetPendingDockArea(), kInvalidAssetId);
      return kMR_Exit;
    case kFS_State8:
      mFlowTime += dt;
      mStateManager->UpdateDynamicLayers();
      if (!mStateManager->HasPendingLayerLoads() && mFlowTime >= 1.f / 60.f) {
        mStateManager->mLayerRestartPending = false;
        SetFlowState(kFS_InGame);
      }
      return kMR_Exit;
    case kFS_CinematicSkip: {
      mFlowTime += dt;
      const CCinematicCamera* cineCam = TCastToConstPtr< CCinematicCamera >(
          *mStateManager->GetCameraManager(0)->GetCurrentCamera(*mStateManager, true));
      bool finished = true;
      if (cineCam) {
        finished = false;
        const bool canSkip = (cineCam->GetFlags() & 0x800) ||
                             ((cineCam->GetFlags() & 8) && cineCam->CanSkip(*mStateManager));
#ifdef TARGET_PC
        bool portSkipActive = false;
        // Port: the debug setting skips cinematics the player has not seen as well, which is
        // what makes a test run reach gameplay in seconds instead of minutes. Stopping the
        // cinematics is what SpecialSkipCinematic does on its way out, and the flow state has
        // to see the camera manager leave the cinematic camera or it comes back next frame.
        // MP2_SKIP_INTRO's request takes the same path (sHarnessCinematicSkip): a test run wants the
        // intro over with, and "over with" means the scene ended, not only the camera.
        if (PortOverlay::SkipCinematics() || sHarnessCinematicSkip || sPortUserCinematicSkip) {
          portSkipActive = true;
          // The retail skip first: SpecialSkipCinematic also fires the scene's skip connections
          // (CScriptSpecialFunction::SkipCinematic), which jump the sequence to its end, and it is
          // what the `canSkip` branch below counts as the skip having happened (result 1). Result 0
          // means the scene has no skip function *yet*, which is not the same as having none, so it
          // is asked again for a few frames before giving up.
          //
          // The fallback - stopping the camera alone - is not the same end state, and that is what it
          // costs: the sequence keeps running underneath, seen through the first-person camera with
          // the HUD and visor drawn over it, and the geometry it brings with it (the wedge beside the
          // intro's camera) is drawn until the player walks into the next room. Reach for it only for
          // a scene whose skip function never appears.
          //
          // Which skip is in flight: the camera it belongs to, how many frames the scene's own skip
          // has been asked for, and whether that ask has been answered for that camera. A skip's
          // callers reset mFlowTime when one starts, but the camera id is what identifies the
          // sequence, and a scene that has already been skipped leaves its cinematic camera current
          // for a frame or two: that re-entry is the camera the skip left behind, not a new sequence,
          // so it is not retried and reported again (and asking is pointless anyway - the one id
          // SpecialSkipCinematic asks is cleared when the scene's skip fires). The budget is a handful
          // of frames, because the skip function is registered by a script
          // (CScriptSpecialFunction::AcceptCinematicSkip, on kSM_Increment) that may not have run on
          // the frame the skip is decided.
          static unsigned sSkipCamera = ~0u;
          static unsigned sSkipAttempts = 0;
          static bool sSkipAnswered = false;
          static bool sSkipReported = false;
          const unsigned kMaxSkipAttempts = 8;
          // How long the flow stays here waiting for a sequence with no skip function to play itself
          // out at max speed (see below), in game time. Generous: nothing is drawn while it runs, so
          // a sequence runs at ~200x (the Emperor Ing chain's 29 s took 0.13 s), and a long one that
          // hit a short cap would fall back to stopping the camera alone, the glitch this avoids. The
          // cap only exists so that a scene which never ends cannot hold a run.
          const float kFastForwardCap = 600.f;
          const unsigned portCamId = cineCam->GetScriptCameraId().Value();
          if (sSkipCamera != portCamId) {
            sSkipCamera = portCamId;
            sSkipAttempts = 0;
            sSkipAnswered = false;
            sSkipReported = false;
          }
          const int portResult = mStateManager->SpecialSkipCinematic();
          ++sSkipAttempts;
          if (portResult == 1 || portResult == 2) {
            // The scene skipped itself. Result 1 stopped the camera too; result 2 is a skip function
            // of another type (kSF_CinematicSkipSignal), which only sends the scene's skip messages,
            // and retail's canSkip branch only counts result 1 as done, so the port stops the camera.
            if (portResult != 1) {
              mStateManager->CameraManager(0)->StopCinematics(*mStateManager);
            }
            if (!sSkipAnswered) {
              if (portResult == 2) {
                std::fprintf(stderr,
                             "[skip] cinematic %04x: the scene's skip signal, camera stopped\n",
                             portCamId & 0xffffu);
              } else if (sSkipAttempts > 1) {
                std::fprintf(stderr, "[skip] cinematic %04x: the scene's own skip (attempt %u)\n",
                             portCamId & 0xffffu, sSkipAttempts);
              } else {
                std::fprintf(stderr, "[skip] cinematic %04x: the scene's own skip\n",
                             portCamId & 0xffffu);
              }
              sSkipAnswered = true;
            }
            finished = true;
          } else if (sSkipAnswered) {
            mStateManager->CameraManager(0)->StopCinematics(*mStateManager);
            finished = true;
          } else if (mFlowTime < kFastForwardCap) {
            // No skip function registered (yet): stay in this flow state. Past a second of flow time
            // the draw case above runs the game at max speed without drawing anything, so the
            // sequence plays itself out in a fraction of its wall time and the camera leaves the
            // cinematic on its own - the only ending that leaves no scene running behind the
            // player's control. (Stopping the camera instead hands over control mid-sequence, which
            // is what drew cutscene geometry over the first-person view.)
            if (sSkipAttempts == kMaxSkipAttempts && !sSkipReported) {
              sSkipReported = true;
              std::fprintf(stderr,
                           "[skip] cinematic %04x: no skip function; fast-forwarding the sequence\n",
                           portCamId & 0xffffu);
            }
          } else {
            mStateManager->CameraManager(0)->StopCinematics(*mStateManager);
            std::fprintf(stderr,
                         "[skip] cinematic %04x: camera stopped only (the sequence never ended in "
                         "%.0f s)\n",
                         portCamId & 0xffffu, kFastForwardCap);
            sSkipAnswered = true;
            finished = true;
          }
        } else if (canSkip && mFlowTime >= 1.f && mStateManager->SpecialSkipCinematic() == 1) {
          finished = true;
        }
#else
        if (canSkip && mFlowTime >= 1.f && mStateManager->SpecialSkipCinematic() == 1) {
          finished = true;
        }
#endif
#ifdef TARGET_PC
        // Port: a skip that is fast-forwarding a sequence (no skip function, above) must not end at
        // the first chained camera that has flag 0x10 and another id than the skipped one: retail
        // gets there only after the scene's skip jumped it to its end, but here the sequence is
        // still running, and ending the flow hands control back with the next camera playing in
        // front of the first-person view and the HUD. The camera leaving the cinematic ends it.
        if (!portSkipActive)
#endif
        if ((cineCam->GetFlags() & 0x10) && mSkippedCineCam != cineCam->GetScriptCameraId()) {
          finished = true;
        }
      }
#ifdef TARGET_PC
      sPortSkipGapHold =
          !cineCam &&
          (PortOverlay::SkipCinematics() || sHarnessCinematicSkip || sPortUserCinematicSkip) &&
          mFlowTime < 600.f && mStateManager->mPendingDockArea == kInvalidAreaId &&
          CScriptStreamedMovie::PortSkippedMovieLive(*mStateManager);
      if (sPortSkipGapHold) {
        // Port: a skipped movie scene (the ending's planet) has no camera of its own: retail plays
        // the movie through there, drawn over the HUD. The skip stopped the movie, so returning to
        // the game now shows the player's view and HUD for the movie's length before the next
        // sequence starts. Stay here, black and at normal speed (the sequence timer's later
        // connections do not fire at max speed, CScriptSequenceTimer::ApplyTime), until the scene
        // moves on (the next cinematic camera, a dock load) or the same cap as above runs out.
        // (The draw is not called at max speed, so the speed is dropped here.)
        gpMain->SetMaxSpeed(false);
        finished = false;
      }
      // Port: the credits started under the skip: it ends here (SetFlowState drops max speed), the
      // credits and the results after them run at normal speed. The draw case below only sets max
      // speed while this state is current.
      if (sPortGameEnded) {
        sPortSkipGapHold = false;
        finished = true;
      }
#endif
      if (finished) {
        SetFlowState(kFS_InGame);
        break;
      }
    }
    // Fall through.
    case kFS_InGame:
      if (mStateManager->mPendingDockArea != kInvalidAreaId) {
        if (mTransitionPhase == kTP_None) {
          mTransitionPhase = kTP_FadeOut;
          mTransitionFromDarkWorld = mStateManager->GetIsDarkWorld();
        }
        if (mTransitionPhase == kTP_Load) {
          mStateManager->SetRandomAvailable(true);
          if (mStateManager->PrepareAreaTransition(mStateManager->GetPendingDockArea())) {
            SetFlowState(kFS_PortalTransition);
            mPortalTransition = mStateManager->TakePortalTransition();
#ifdef TARGET_PC
            // Port: a warp handed this hop over, so it is not a dock crossing. Its portal transition
            // can never become ready while the port holds the destination's stream (see
            // CStateManager::PortPlaceArrivalTick), and kTP_Load then waits on it forever: a Temple
            // Grounds sweep hung entering the ending area this way. Drop it, as the arrival does.
            if (mStateManager->PortIsWarpHandoff() && !mPortalTransition.null()) {
              std::fprintf(stderr, "[warp] the game's transition is a warp's: dropping its portal "
                                   "transition\n");
              mPortalTransition = nullptr;
            }
            // "Portal transition > Skip": no ride, so the hop is a dock crossing without one (kTP_Load
            // then waits for nothing and the fade-in follows the load).
            if (PortOverlay::PortalTransition() == PortOverlay::kPortalTransition_Skip) {
              mPortalTransition = nullptr;
            }
#endif
            mPortalTransitionTime = 0.f;
            if (!mGuiManager.IsNull()) {
              mGuiManager->StopSounds(*mStateManager);
            }
          } else {
#ifdef TARGET_PC
            // Port: the other silent way this flow can stall - PrepareAreaTransition refuses while
            // an area's transactions are still in flight, and the retry prints nothing. Name it once
            // per 120 attempts so a hung run says which of the two paths it is on.
            static unsigned sPortPrepareRetries = 0;
            if ((++sPortPrepareRetries % 120u) == 1u) {
              std::fprintf(stderr, "[warp] the game's transition cannot prepare area %d yet (retry "
                                   "%u)\n",
                           mStateManager->GetPendingDockArea().Value(), sPortPrepareRetries);
            }
#endif
            mTransitionFadeTime = 1.f;
          }
          mStateManager->SetRandomAvailable(false);
        }
        break;
      }

      mStateManager->SetRandomAvailable(true);
      switch (mStateManager->mDeferredTransition) {
      case kSMT_InGame:
        mStateManager->Update(dt, queue);
        if (mStateManager->mQuitGame) {
          CGraphics::SetIsBeginSceneClearFb(false);
        }
        break;
      case kSMT_MapScreen:
        EnterMapScreen();
        break;
      case kSMT_PauseGame:
        PauseGame();
        break;
      case kSMT_Unk:
        EnterLogBook();
        break;
      case kSMT_LogBook:
        SaveGame();
        break;
      case kSMT_SaveGame:
        EnterPauseScreenState5();
        break;
      case kSMT_MessageScreen:
        EnterMessageScreen(mStateManager->mHudMessageTime);
        break;
      }
      if (gpGameState->GetGameMode().IsGameOver()) {
        if (!mStateManager->IsMultiplayer()) {
          EndGame(queue);
        } else {
          SetFlowState(kFS_MultiplayerEndFade);
        }
      }
      if (!mStateManager->IsMultiplayer() && mPlayerAlive &&
          !mStateManager->GetPlayerState(0)->IsPlayerAlive()) {
        PlayerDied();
      }
      mStateManager->SetRandomAvailable(false);
      break;
    case kFS_Paused:
      if (!mGuiManager->IsInPausedState()) {
        UnpauseGame();
        if (mStateManager->GetPauseHUDMessage() != kInvalidAssetId) {
          mStateManager->IncrementHUDMessageFrameCounter();
        }
      }
      break;
    case kFS_PlayerDied:
      if (gpGameState->GetGameMode().IsGameOver()) {
        EndGame(queue);
      } else {
        mStateManager->SetRandomAvailable(true);
        mStateManager->Update(dt, queue);
        mStateManager->SetRandomAvailable(false);
      }
      break;
    case kFS_MultiplayerEndFade:
      if (mMultiplayerEndFadeTime < 1.f) {
        mMultiplayerEndFadeTime = rstl::min_val(mMultiplayerEndFadeTime + dt, 1.f);
        if (mMultiplayerEndFadeTime == 1.f) {
          FinishMultiplayerGame(queue);
        }
      }
      break;
    case kFS_MultiplayerResults:
      if (mEndGameFrameCaptured) {
        CIOWin* screen = rs_new CGameResultsScreen(*gpGameState);
        queue.Push(MakeMsg::CreateCreateIOWin(kAMT_IOWinManager, 9, 10, screen));
        queue.Push(MakeMsg::CreateQuitGameplay(kAMT_Game));
      }
      break;
    }
    for (uint i = 0; i < uint(mStateManager->GetNumPlayers()); ++i) {
      mGuiManager->Update(*mStateManager, dt, queue, IsCameraActiveFlow(), i);
    }
    if (IsCameraActiveFlow()) {
      mGuiManager->UpdateMultiplayerGui(dt, *mStateManager);
    }
    if (!wasInitialized) {
      gpGameState->WorldTransitionManager()->EndTransition();
    }
    return kMR_Exit;
  }
  case kAM_UserInput: {
    if (!mInitialized || (mTransitionPhase != kTP_None && mTransitionPhase != kTP_Complete)) {
      break;
    }
    CArchMsgParmUserInput parm = MakeMsg::GetParmUserInput(message);
    CFinalInput input = parm.GetUserInput();
    bool stopRumble = true;
    if (static_cast< int >(input.ControllerNumber()) == 0 &&
        !mStateManager->mSaveGameScreen.null()) {
      mStateManager->mSaveGameScreen->ProcessUserInput(input);
    }
    if (mFlowState == kFS_InGame) {
      if (mMultiplayerGuiActive) {
        queue.Push(MakeMsg::CreateQuitGameplay(kAMT_Game));
        mMultiplayerGuiActive = false;
      }
      if (mStateManager->mLayerRestartPending) {
        SetFlowState(kFS_State8);
        mFlowTime = 0.f;
        break;
      }
      const CCinematicCamera* const cineCam = TCastToConstPtr< CCinematicCamera >(
          *mStateManager->GetCameraManager(0)->GetCurrentCamera(*mStateManager, true));
#ifdef TARGET_PC
      // Port: Start leaves photo mode (PortPhoto::Input), it does not pause.
      const bool start = input.PStart() && !PortPhoto::Active();
#else
      const bool start = input.PStart();
#endif
      if (start && !cineCam && gpGameState->GetGameMode().GetGameModeType() != 'FRND' &&
          gpGameState->GetGameMode().GetGameModeType() != 'SNGL') {
        for (int i = 0; i < mStateManager->GetNumPlayers(); ++i) {
          if (static_cast< int >(mStateManager->GetPlayerState(i)->GetPlayerSelection()) ==
              static_cast< int >(input.ControllerNumber())) {
            mGuiManager->GetPlayerGuiManager(i).PauseGame(*mStateManager, kIGGS_QuitGame);
            SetFlowState(kFS_Paused);
            break;
          }
        }
      }
#ifdef TARGET_PC
      // Port: with the "Skip cinematics" setting on, START or A skips any cinematic, seen or not
      // (off, retail: START, and only one the player has seen). A counts once the cinematic has been
      // on for a moment, so the press that walked the player into its trigger does not skip it. The
      // skip takes the robust path in kFS_CinematicSkip (sPortUserCinematicSkip).
      // The first press only arms the "Skip [A]" prompt; a second one while it shows skips
      // (PortOverlay::SkipConfirm*). The retail START path below follows the same rule.
      bool portSkipArmedNow = false;
      if (PortOverlay::SkipCinematics() && !sPortGameEnded &&
          static_cast< int >(input.ControllerNumber()) == 0 && cineCam &&
          (input.PStart() || (input.PA() && sPortCinematicTime >= kPortSkipAGuard)) &&
          !mStateManager->IsMultiplayer() &&
          gpGameState->GetGameMode().GetGameModeType() != 'FRND' &&
          !(portSkipArmedNow = !PortOverlay::SkipConfirmArmed())) {
        PortOverlay::SkipConfirmDisarm();
        CMidiManager::StopAll();
        mSkippedCineCam = cineCam->GetScriptCameraId();
        sPortUserCinematicSkip = true;
        SetFlowState(kFS_CinematicSkip);
        mFlowTime = 0.f;
        break;
      }
      if (portSkipArmedNow) {
        PortOverlay::SkipConfirmArm();
      }
#endif
      if (static_cast< int >(input.ControllerNumber()) == 0 && input.PStart()
#ifdef TARGET_PC
          && !PortPhoto::Active() && !portSkipArmedNow
#endif
      ) {
        if (cineCam) {
          const bool canSkip = (cineCam->GetFlags() & 0x800) ||
                               ((cineCam->GetFlags() & 8) && cineCam->CanSkip(*mStateManager));
          if (canSkip && !mStateManager->IsMultiplayer() &&
              gpGameState->GetGameMode().GetGameModeType() != 'FRND') {
#ifdef TARGET_PC
            // Port: a first START only arms the prompt (see above).
            if (!PortOverlay::SkipConfirmArmed()) {
              PortOverlay::SkipConfirmArm();
              break;
            }
            PortOverlay::SkipConfirmDisarm();
#endif
            CMidiManager::StopAll();
            mSkippedCineCam = cineCam->GetScriptCameraId();
            SetFlowState(kFS_CinematicSkip);
            mFlowTime = 0.f;
            break;
          }
        }
        if (!cineCam && gpGameState->GetGameMode().GetGameModeType() != 'FRND') {
          mStateManager->DeferStateTransition(kSMT_Unk);
        }
      }
      mStateManager->SetRandomAvailable(true);
#ifdef TARGET_PC
      // Port: the free camera takes controller 0's input while it is on.
      if (!(static_cast< int >(input.ControllerNumber()) == 0 && PortPhoto::Input(input)))
#endif
      mStateManager->ProcessInput(input);
      mStateManager->SetRandomAvailable(false);
      stopRumble = false;
    } else if (mFlowState != kFS_PlayerDied && mFlowState != kFS_MultiplayerEndFade &&
               mFlowState != kFS_MultiplayerResults && mFlowState != kFS_CinematicSkip &&
               mFlowState != kFS_State8) {
      stopRumble = true;
    }
#ifdef TARGET_PC
    // Port: photo mode keeps the pad away from the HUD and the menus.
    if (!(static_cast< int >(input.ControllerNumber()) == 0 && PortPhoto::Active()))
#endif
    mGuiManager->ProcessControllerInput(*mStateManager, input, queue);
    if (stopRumble) {
      gpController->SetMotorState(kIOP_Player1, kMS_Stop);
      gpController->SetMotorState(kIOP_Player2, kMS_Stop);
      gpController->SetMotorState(kIOP_Player3, kMS_Stop);
      gpController->SetMotorState(kIOP_Player4, kMS_Stop);
    }
    break;
  }
  case kAM_FrameEnd:
    mStateManager->FrameEnd();
    if (mStateManager->mQuitGame) {
      queue.Push(MakeMsg::CreateQuitGameplay(kAMT_Game));
    }
    break;
  case kAM_QuitGameplay:
#ifdef TARGET_PC
    // A held step can load a save state before the console client's subsequent `hold 0`
    // arrives. Once this window is removed there is nobody to service that command, and
    // holding the architecture's timer ticks also prevents MainFlow from restarting it.
    // A simulation hold belongs to the outgoing game, not its loader or the main flow;
    // `state load ... hold` will take a new hold when the room restore finishes.
    PortOverlay::SetTickHold(false);
#endif
    RecordMultiplayerResults();
    CFrameDelayedKiller::StallAndFlushAllAllocations();
    return kMR_RemoveIOWin;
  }
  return kMR_Normal;
}

// Unused and dead-stripped in the target; only its format strings survive in the pool
// (between DrawWorld's and OnMessage's). Name and body are guesses.
void CMFGame_DebugStringize(int a, int b, int c) {
  CBasics::Stringize("%d ", a);
  CBasics::Stringize("<S>:%d", b);
  CBasics::Stringize("<E>:%d", c);
}

void CMFGame::DrawWorld(bool singleViewport) const {
  uint playerCount = mStateManager->GetNumPlayers();
  if (singleViewport) {
    playerCount = 1;
  }
  if (mGuiManager->GetIsGameDraw()) {
    gpMain->SetGameFrameDrawn(true);
    CScopedProfiler::BeginFrame();
    bool usedViewports[4] = {false, false, false, false};
    for (uint i = 0; i < playerCount; ++i) {
      const uint selection = gpGameState->GetPlayerState(i)->GetPlayerSelection();
      usedViewports[selection] = true;
      CScopedProfiler totalProfile(rstl::string(CBasics::Stringize("*TotalPlayer%d", i)), true);
      CScopedProfiler prerenderProfile(rstl::string_l("*SM_Prerender"), true);
      mStateManager->PreRender(i);
      mGuiManager->PrepareScanDisplay(*mStateManager, i);
      mStateManager->DrawWorld(*mGuiManager);
      mStateManager->EndPlayerRender();
    }
    if (playerCount == 3) {
      for (int i = 0; i < 4; ++i) {
        if (!usedViewports[i]) {
          mStateManager->DrawUnusedViewport(i);
        }
      }
    }
  }
}

void CMFGame::DrawGui(bool singleViewport) const {
  if (!singleViewport) {
    for (uint i = 0; i < uint(mStateManager->GetNumPlayers()); ++i) {
      mStateManager->SetupPlayerViewport(i);
      CScopedProfiler prerenderProfile(rstl::string_l("*GUI_Prerender"), true);
      mGuiManager->PreDraw(*mStateManager, IsCameraActiveFlow());
      CScopedProfiler drawProfile(rstl::string_l("*GUI_Draw"), true);
#ifdef TARGET_PC
      CPortPipelineContextScope globalPipelines(AURORA_PIPELINE_CONTEXT_GLOBAL);
#endif
      mGuiManager->Draw(*mStateManager, i);
      mStateManager->EndPlayerRender();
    }
  }
  mStateManager->DrawDebugStuff();
#ifdef TARGET_PC
  // Port: the render mode is widened by the aspect setting.
  CGraphics::SetViewport(0, 0, CGraphics::GetRenderMode().fbWidth,
                         CGraphics::GetRenderMode().efbHeight);
  CGraphics::SetScissor(0, 0, CGraphics::GetRenderMode().fbWidth,
                        CGraphics::GetRenderMode().efbHeight);
#else
  CGraphics::SetViewport(0, 0, 640, 448);
  CGraphics::SetScissor(0, 0, 640, 448);
#endif
  if (mGuiManager->GetIsGameDraw() && mStateManager->IsMultiplayer() && !singleViewport) {
    mGuiManager->DrawMultiplayerGui();
  }
  if (mStateManager->mSaveGameScreen.get()) {
    mStateManager->mSaveGameScreen->Draw();
  }
}

void CMFGame::DrawTransitionFilter() const {
  if (mStateManager->GetShowSoftTransition()) {
    const bool fadeOut = mTransitionPhase == kTP_Load || mTransitionPhase == kTP_Complete;
    const float amount = fadeOut ? 1.f - mTransitionFadeTime : mTransitionFadeTime;
    CCameraFilterPass::DrawFilter(CCameraFilterPass::kFT_Add, CCameraFilterPass::kFS_Fullscreen,
                                  CColor::Lerp(CColor::White(), CColor::Black(), amount), nullptr,
                                  1.f);
  }
}

void CMFGame::Draw() const {
#ifdef TARGET_PC
  {
    // Port diagnostic (MP2_DEBUG_TRANS=1): a flow or transition state that stops the world from
    // being drawn leaves the HUD on a blank frame - what a boot-time `MP2_TELEPORT` looked like
    // (docs/STATUS.md). Print the state every 240 draws and on any change.
    static const bool sLogTrans = std::getenv("MP2_DEBUG_TRANS") != nullptr;
    static int sLast = -1;
    static unsigned sCalls = 0;
    const int key = static_cast< int >(mFlowState) * 16 + static_cast< int >(mTransitionPhase);
    if (sLogTrans && ((++sCalls % 240u) == 0u || sLast != key)) {
      sLast = key;
      std::fprintf(stderr,
                   "[trans] flow %d phase %d fade %.2f soft %d dock %d time %.2f portal %s\n",
                   static_cast< int >(mFlowState), static_cast< int >(mTransitionPhase),
                   mTransitionFadeTime, mStateManager->GetShowSoftTransition() ? 1 : 0,
                   mStateManager->mPendingDockArea.Value(), mFlowTime,
                   mPortalTransition.null() ? "none" : "set");
    }
  }
#endif
  if (!mStateManager.IsNull()) {
    mStateManager->Touch();
  }
#ifdef TARGET_PC
  // Pipelines the world draws are tagged with the camera's area (issue #187 in MP1); a change of
  // area asks a lazy pipeline cache to build the new area's recorded pipelines in the background.
  uint64_t pipelineContext = AURORA_PIPELINE_CONTEXT_FRONTEND;
  if (!mStateManager.IsNull()) {
    if (const CWorld* world = mStateManager->GetWorld();
        world != nullptr && mStateManager->GetNextAreaId() != kInvalidAreaId) {
      pipelineContext = PortPipelineAreaContext(static_cast< uint32_t >(world->GetWorldAssetId()),
                                                mStateManager->GetNextAreaId().Value());
    }
  }
  {
    static uint64_t sLastContext = AURORA_PIPELINE_CONTEXT_FRONTEND;
    static AuroraPipelineStats sLastStats{};
    if (pipelineContext != sLastContext) {
      AuroraPipelineStats stats{};
      aurora_get_pipeline_stats(&stats);
      if (sLastContext != AURORA_PIPELINE_CONTEXT_FRONTEND) {
        std::fprintf(stderr,
                     "MP2 pipeline context %llx left: %llu skipped draws, %llu new, %llu known, %llu late misses\n",
                     static_cast< unsigned long long >(sLastContext),
                     static_cast< unsigned long long >(stats.skippedDraws - sLastStats.skippedDraws),
                     static_cast< unsigned long long >(stats.newMisses - sLastStats.newMisses),
                     static_cast< unsigned long long >(stats.knownMisses - sLastStats.knownMisses),
                     static_cast< unsigned long long >(stats.lateMisses - sLastStats.lateMisses));
      }
      sLastStats = stats;
      sLastContext = pipelineContext;
      if (pipelineContext != AURORA_PIPELINE_CONTEXT_FRONTEND) {
        aurora_pipeline_prefetch(pipelineContext);
      }
    }
  }
  CPortPipelineContextScope pipelineScope(pipelineContext);
#endif
  const bool singleViewport = mStateManager->IsMultiplayer() &&
                              mStateManager->GetCameraManager(0)->IsInFullScreenCinematic();

  switch (mFlowState) {
  case kFS_InGame:
  case kFS_Paused:
  case kFS_PlayerDied:
    CGraphics::SetUseStreamVertexDelay(mFlowState == kFS_Paused);
    if (mStateManager->mPendingDockArea == kInvalidAreaId || mTransitionPhase != kTP_Load) {
      DrawWorld(singleViewport);
      DrawGui(singleViewport);
    }
    if (mTransitionPhase == kTP_FadeOut || mTransitionPhase == kTP_Complete) {
      DrawTransitionFilter();
    }
    CGraphics::SetUseStreamVertexDelay(false);
    return;
  case kFS_CinematicSkip: {
#ifdef TARGET_PC
    // Port: the port's skips (see the timer tick) fade out in a quarter second instead of retail's
    // second before the fast-forward: with the 60 FPS cap lifted at max speed the rest of the
    // sequence then runs in a fraction of a second, so the fade is most of the skip.
    const float fadeTime =
        PortOverlay::SkipCinematics() || sHarnessCinematicSkip || sPortUserCinematicSkip ? 0.25f
                                                                                         : 1.f;
    if (sPortSkipGapHold) {
      // The sequence plays on at normal speed behind a black screen (see the update case).
      gpMain->SetMaxSpeed(false);
      DrawWorld(singleViewport);
      DrawGui(singleViewport);
      CCameraFilterPass::DrawFilter(CCameraFilterPass::kFT_Multiply,
                                    CCameraFilterPass::kFS_Fullscreen,
                                    CColor(0.f, 0.f, 0.f, 1.f), nullptr, 1.f);
      return;
    }
    if (mFlowTime >= fadeTime && !sPortGameEnded) {
      gpMain->SetMaxSpeed(true);
      return;
    }
    DrawWorld(singleViewport);
    DrawGui(singleViewport);
    const float intensity = CMath::Clamp(0.f, 1.f - mFlowTime / fadeTime, 1.f);
#else
    if (mFlowTime >= 1.f) {
      gpMain->SetMaxSpeed(true);
      return;
    }
    DrawWorld(singleViewport);
    DrawGui(singleViewport);
    const float intensity = CMath::Clamp(0.f, 1.f - mFlowTime, 1.f);
#endif
    CCameraFilterPass::DrawFilter(CCameraFilterPass::kFT_Multiply,
                                  CCameraFilterPass::kFS_Fullscreen,
                                  CColor(intensity, intensity, intensity, 1.f), nullptr, 1.f);
    return;
  }
  case kFS_State8: {
    if (mFlowTime >= 1.f / 60.f) {
      return;
    }
    DrawWorld(singleViewport);
    DrawGui(singleViewport);
    const float intensity = rstl::min_val(1.f, (1.f / 60.f - mFlowTime) / (1.f / 60.f));
    CCameraFilterPass::DrawFilter(CCameraFilterPass::kFT_Multiply,
                                  CCameraFilterPass::kFS_Fullscreen,
                                  CColor(intensity, intensity, intensity, 1.f), nullptr, 1.f);
  }
  // Fall through.
  case kFS_MultiplayerEndFade:
  case kFS_MultiplayerResults:
    if (!mEndGameFrameCaptured) {
      mEndGameFrameCaptured = true;
      DrawWorld(singleViewport);
      DrawGui(singleViewport);
      CGraphics::SetViewport(0, 0, CGraphics::GetRenderMode().fbWidth,
                             CGraphics::GetRenderMode().xfbHeight);
      CGraphics::SetScissor(0, 0, CGraphics::GetRenderMode().fbWidth,
                            CGraphics::GetRenderMode().xfbHeight);
      CCameraBlurPass::GetFbCopy(GX_TF_RGB565);
      CGraphics::SetIsBeginSceneClearFb(false);
    } else {
      CGraphics::SetViewport(0, 0, CGraphics::GetRenderMode().fbWidth,
                             CGraphics::GetRenderMode().xfbHeight);
      CGraphics::SetScissor(0, 0, CGraphics::GetRenderMode().fbWidth,
                            CGraphics::GetRenderMode().xfbHeight);
      const float amount = 8.f * (mMultiplayerEndFadeTime * mMultiplayerEndFadeTime) + 1.f;
      const float rate = CGraphics::Is50Hz() ? 50.f : 60.f;
      CCameraFilterPass::DrawFilter(CCameraFilterPass::kFT_Add, CCameraFilterPass::kFS_Fullscreen,
                                    CColor::White().WithAlphaOf(amount / rate), nullptr, 1.f);
    }
    break;
  case kFS_PortalTransition:
    if (mTransitionPhase >= kTP_Load && mTransitionPhase <= kTP_FadeIn && mPortalTransition.get()) {
      mPortalTransition->Draw();
    }
    if (mTransitionPhase != kTP_Play) {
      DrawTransitionFilter();
    }
    break;
  }
}

void CMFGame::EnterMapScreen() {
  SetFlowState(kFS_Paused);
  CInGameGuiManager& gui = mGuiManager->GetPlayerGuiManager(0);
  gui.GetAutoMapper().SetMapMode(CAutoMapper::kMM_Normal);
  gui.PauseGame(*mStateManager, kIGGS_MapScreen);
  mStateManager->SetInMapScreen(true);
}

void CMFGame::PauseGame() {
  SetFlowState(kFS_Paused);
  CInGameGuiManager& gui = mGuiManager->GetPlayerGuiManager(0);
  gui.GetAutoMapper().SetMapMode(CAutoMapper::kMM_Teleport);
  gui.PauseGame(*mStateManager, kIGGS_MapScreen);
  mStateManager->SetInMapScreen(true);
}

void CMFGame::EnterLogBook() {
  SetFlowState(kFS_Paused);
  // The G2ME01 call passes GUI state 3 for this path.
  mGuiManager->PauseGame(*mStateManager, kIGGS_PauseGame);
}

void CMFGame::SaveGame() {
  SetFlowState(kFS_Paused);
  // The G2ME01 call passes GUI state 4 for this path.
  mGuiManager->PauseGame(*mStateManager, kIGGS_PauseLogBook);
}

void CMFGame::EnterPauseScreenState5() {
  SetFlowState(kFS_Paused);
  mGuiManager->GetPlayerGuiManager(0).PauseGame(*mStateManager, kIGGS_PauseSaveGame);
}

void CMFGame::EnterMessageScreen(float time) {
#ifdef TARGET_PC
  // MP2_SKIP_INTRO: a HUD memo before gameplay is dismissed the way the kFS_Paused case of OnMessage
  // dismisses it once the GUI leaves its paused state, without ever pausing.
  if (PortHarness::IntroDismissMemo(static_cast< unsigned >(mStateManager->GetPauseHUDMessage()))) {
    UnpauseGame();
    if (mStateManager->GetPauseHUDMessage() != kInvalidAssetId) {
      mStateManager->IncrementHUDMessageFrameCounter();
    }
    return;
  }
#endif
  SetFlowState(kFS_Paused);
  mGuiManager->GetPlayerGuiManager(0).ShowPauseGameHudMessage(
      *mStateManager, mStateManager->GetPauseHUDMessage(), time);
}

void CMFGame::UnpauseGame() {
  SetFlowState(kFS_InGame);
  CSfxManager::SetChannel(CSfxManager::kSC_Game);
  mStateManager->DeferStateTransition(kSMT_InGame);
}

void CMFGame::PlayerDied() {
  SetFlowState(kFS_PlayerDied);
  mPlayerAlive = false;
}

bool CMFGame::IsCameraActiveFlow() const {
  return (bool)(mFlowState == kFS_InGame || mFlowState == kFS_PlayerDied ||
                (mFlowState == kFS_PortalTransition && x44_4 != 0));
}

void CMFGame::EndGame(CArchitectureQueue& queue) {
  if (gpGameState->GetGameMode().GetResultIndex() == 0) {
    gpMain->SetRestartMode(CMain::kRM_EndMovie2);
    queue.Push(MakeMsg::CreateQuitGameplay(kAMT_Game));
  } else {
    gpGameState->PreviousGameResults().mShowResults = true;
    CGraphics::SetIsBeginSceneClearFb(false);
    ActivateMultiplayerGui();
  }
}

void CMFGame::FinishMultiplayerGame(CArchitectureQueue& queue) {
  gpGameState->PreviousGameResults().mShowResults = true;
  gpMain->SetRestartMode(CMain::kRM_Default);
  SetFlowState(kFS_MultiplayerResults);
}

CGameState::SPreviousGameResults::SPreviousGameResults(
    uint gameMode, bool showResults, int modeResult, int playerCount,
    const rstl::reserved_vector< SPlayerResult, 4 >& players)
: mGameMode(gameMode)
, mShowResults(showResults)
, x8_(modeResult)
, mPlayerCount(playerCount)
, mPlayers(players) {}

void CMFGame::RecordMultiplayerResults() const {
  const bool showResults = gpGameState->PreviousGameResults().mShowResults;
  CGameMode& mode = gpGameState->GetGameMode();
  const int gameMode = mode.GetGameModeType();
  const int result = mode.GetResultIndex();
  const int playerCount = mode.GetNumPlayers();
  rstl::reserved_vector< CGameState::SPlayerResult, 4 > players;

  if (gameMode == 'DTHM') {
    for (int i = 0; i < playerCount; ++i) {
      const CPlayerState& player = *mStateManager->GetPlayerState(i);
      const CPlayerOptions& options = gpGameState->GameOptions().PlayerOptions(i);
      const uint selection = player.GetPlayerSelection();
      const int score = player.GetItemAmount(CPlayerState::kIT_FragCount);
      const int deaths = player.GetItemAmount(CPlayerState::kIT_DiedCount);
      players.push_back(CGameState::SPlayerResult(
          selection, score, deaths, options.GetInvertYAxis(), options.GetRumbleEnabled()));
    }
  } else if (gameMode == 'COIN') {
    for (int i = 0; i < playerCount; ++i) {
      const CPlayerState& player = *mStateManager->GetPlayerState(i);
      const CPlayerOptions& options = gpGameState->GameOptions().PlayerOptions(i);
      const uint selection = player.GetPlayerSelection();
      const int score = player.GetItemAmount(CPlayerState::kIT_CoinCounter);
      const int deaths = player.GetItemAmount(CPlayerState::kIT_DiedCount);
      players.push_back(CGameState::SPlayerResult(
          selection, score, deaths, options.GetInvertYAxis(), options.GetRumbleEnabled()));
    }
  }
  while (players.size() < 4) {
    players.push_back(CGameState::SPlayerResult());
  }
  gpGameState->PreviousGameResults() =
      CGameState::SPreviousGameResults(gameMode, showResults, result, playerCount, players);
}

void CMFGame::ActivateMultiplayerGui() { mMultiplayerGuiActive = true; }
