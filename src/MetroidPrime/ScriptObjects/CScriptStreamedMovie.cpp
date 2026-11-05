#include "MetroidPrime/ScriptObjects/CScriptStreamedMovie.hpp"

#include "Kyoto/CDvdFile.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/CActorParameters.hpp"
#include "MetroidPrime/CMain.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrStreamedMovie.hpp"
#include "MetroidPrime/ScriptLoaderRel.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSequenceTimer.hpp"
#include "MetroidPrime/TCastTo.hpp"
#ifdef TARGET_PC
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/Input/CFinalInput.hpp"
#include "port_entry.h"
#endif

static CScriptStreamedMovie* sPlayingMovie = nullptr; // Guessed name
#ifdef TARGET_PC
static CScriptStreamedMovie* sPortSkippedMovie = nullptr;
static TUniqueId sPortSkippedMovieId = kInvalidUniqueId;
// The Trilogy front end's title movie (PortTitleHoldsMusic); resolved like sPortSkippedMovie.
static CScriptStreamedMovie* sPortTitleMovie = nullptr;
static TUniqueId sPortTitleMovieId = kInvalidUniqueId;
static const char kPortTitleIntro[] = "Video/Intro_to_Logo.thp";
static const char kPortTitleLoop[] = "Video/Logo_Loop.thp";
static const char kPortTitleOutro[] = "Video/Outro_to_Background.thp";

// Port: the movie a max-speed skip stopped. Retail plays that movie through (drawn over the HUD),
// so the skip's flow state waits for the scene to move on instead of showing the player's view.
bool CScriptStreamedMovie::PortSkippedMovieLive(const CStateManager& mgr) {
  // The object may be gone with its area: only a pointer the manager still resolves is read.
  if (sPortSkippedMovie == nullptr ||
      mgr.GetObjectById(sPortSkippedMovieId) != static_cast< const CEntity* >(sPortSkippedMovie)) {
    return false;
  }
  const CScriptStreamedMovie* movie = sPortSkippedMovie;
  return movie->GetActive() && movie->mSkipping && movie->mMoviePlayer.get() != nullptr;
}
#endif

CScriptStreamedMovie::CScriptStreamedMovie(TUniqueId uid, const rstl::string& name,
                                           const CEntityInfo& info, const CTransform4f& xf,
                                           const rstl::string& movieFile, bool loop,
                                           bool videoFilterEnabled, float cacheLength,
                                           float fadeOutTime, int whenToDraw, int volume,
                                           int volumeType)
: CActor(uid, name, info, 0, xf, CModelData::None(), CMaterialList(), CActorParameters::None(),
         kInvalidUniqueId)
, mMovieFile(movieFile)
, mWhenToDraw(whenToDraw)
, mVolume(volume)
, mVolumeType(volumeType)
, mCacheLength(cacheLength)
, mFadeOutTime(fadeOutTime)
, mLoop(loop)
, mVideoFilterEnabled(videoFilterEnabled)
, mSkipping(false)
, mSkipTime(0.f)
, mMoviePlayer(nullptr)
#ifdef TARGET_PC
, mPortTitlePhase(kPTP_None)
, mPortTitleMusicDelay(0.f)
, mPortTitleIntro(nullptr)
, mPortTitleLoop(nullptr)
, mPortHeldMusic(kInvalidUniqueId)
, mPortTitleArrived(false)
#endif
{
  if (!CDvdFile::FileExists(mMovieFile.c_str())) {
    mMovieFile.assign("", -1);
  }
#ifdef TARGET_PC
  // Trilogy's front end opens on this movie (Outro_to_Background, then the main menu): the
  // GameCube title screen before it is gone from its script, though its movies are on the disc.
  if (port::entry::IsTrilogy(port::entry::CurrentDiscVersion()) &&
      mMovieFile == kPortTitleOutro &&
      CDvdFile::FileExists(kPortTitleIntro) && CDvdFile::FileExists(kPortTitleLoop)) {
    mPortTitlePhase = kPTP_Waiting;
    sPortTitleMovie = this;
    sPortTitleMovieId = uid;
  }
#endif
}

#ifdef TARGET_PC
CScriptStreamedMovie::~CScriptStreamedMovie() {
  PortTitleStopMusic();
  if (sPortTitleMovie == this) {
    sPortTitleMovie = nullptr;
    sPortTitleMovieId = kInvalidUniqueId;
  }
  if (sPlayingMovie == this) {
    sPlayingMovie = nullptr;
    CMoviePlayer::SetSfxVolume(127);
  }
}

bool CScriptStreamedMovie::PortTitleHoldsMusic(const CStateManager& mgr, TUniqueId music) {
  CScriptStreamedMovie* movie = sPortTitleMovie;
  if (movie == nullptr ||
      mgr.GetObjectById(sPortTitleMovieId) != static_cast< const CEntity* >(movie) ||
      movie->mPortTitlePhase == kPTP_None ||
      movie->mPortTitlePhase == kPTP_Done) {
    return false;
  }
  movie->mPortHeldMusic = music;
  return true;
}

static bool PortTitlePressed(const CStateManager& mgr) {
  for (int i = 0; i < 4; ++i) {
    if (mgr.mFinalInputs[i].PA() || mgr.mFinalInputs[i].PStart()) {
      return true;
    }
  }
  return false;
}

// Advances one title movie the way Think does a loaded one; true once it plays.
static bool PortTitleAdvance(CMoviePlayer& player, float dt) {
  if (!player.CanDrawVideo()) {
    player.ContinueLoading();
    return false;
  }
  if (player.GetPlayMode() != CMoviePlayer::kPM_Playing) {
    player.SetPlayMode(CMoviePlayer::kPM_Playing);
    player.Update(0.f);
    return true;
  }
  player.Update(dt);
  return true;
}

// Port: Echoes' GameCube title screen on the Trilogy disc, rebuilt from FrontEnd04's script
// (MOVI 0x11c/0x11e, GWIG 0x123/0x42, SOND 0xf6/0xf7, SQTR 0x23e): the intro movie (A or Start
// skips it) into the looping logo until A or Start, then this movie and the menu as before. The
// title music plays from the intro on and stops 2.75 s after the press, when the menu music starts.
// True while a title movie owns the screen.
bool CScriptStreamedMovie::PortTitleThink(float dt, CStateManager& mgr) {
  switch (mPortTitlePhase) {
  case kPTP_Waiting:
    // This movie's Arrived picks the title (SWTC 0x6 closed: SRLY 0x137 plays it, all within one
    // message dispatch) or the multiplayer game-type screen (open: never played). Still waiting
    // a Think later means the latter: no title, and the menu music it held starts.
    if (mPortTitleArrived) {
      mPortTitlePhase = kPTP_Done;
      if (mPortHeldMusic != kInvalidUniqueId) {
        mgr.SendScriptMsg(mPortHeldMusic, GetUniqueId(), kSM_Play);
        mPortHeldMusic = kInvalidUniqueId;
      }
    }
    return false;
  case kPTP_Intro: {
    if (!mPortTitleLoop->CanDrawVideo()) {
      mPortTitleLoop->ContinueLoading();
    }
    const bool playing = PortTitleAdvance(*mPortTitleIntro, dt);
    UpdateVolume(mgr);
    if (gpMain->IsMaxSpeed() || PortTitlePressed(mgr) ||
        (playing && mPortTitleIntro->GetIsMovieFinishedPlaying())) {
      mPortTitleIntro = nullptr;
      mPortTitlePhase = kPTP_Loop;
    }
    return true;
  }
  case kPTP_Loop:
    PortTitleAdvance(*mPortTitleLoop, dt);
    UpdateVolume(mgr);
    if (gpMain->IsMaxSpeed() || PortTitlePressed(mgr)) {
      PortTitleEnd(mgr);
      return false;
    }
    return true;
  case kPTP_Outro:
    mPortTitleMusicDelay -= dt;
    if (mPortTitleMusicDelay <= 0.f) {
      PortTitleStopMusic();
      mPortTitlePhase = kPTP_Done;
      if (mPortHeldMusic != kInvalidUniqueId) {
        mgr.SendScriptMsg(mPortHeldMusic, GetUniqueId(), kSM_Play);
        mPortHeldMusic = kInvalidUniqueId;
      }
    }
    return false;
  default:
    return false;
  }
}

void CScriptStreamedMovie::PortTitleStart(CStateManager& mgr) {
  if (sPlayingMovie != nullptr) {
    // Another movie owns the screen: no title screen, as if it had been passed.
    PortTitleEnd(mgr);
    return;
  }
  sPlayingMovie = this;
  mPortTitleIntro = rs_new CMoviePlayer(kPortTitleIntro, mCacheLength, false, mVideoFilterEnabled);
  mPortTitleLoop = rs_new CMoviePlayer(kPortTitleLoop, mCacheLength, true, mVideoFilterEnabled);
  mPortTitleIntro->SetPlayMode(CMoviePlayer::kPM_Stopped);
  mPortTitleLoop->SetPlayMode(CMoviePlayer::kPM_Stopped);
  mPortTitlePhase = kPTP_Intro;
  // SOND 0xf6/0xf7: looped, volume 105 by the music volume, panned hard right and left.
  const short volume =
      static_cast< short >(105 * int(gpGameState->GameOptions().GetMusicVolume()) / 127);
  mPortTitleMusic[0] =
      CSfxManager::SfxStart(0x2687, volume, 126, CSfxManager::kAllAreas, false, true, 127);
  mPortTitleMusic[1] =
      CSfxManager::SfxStart(0x2787, volume, 0, CSfxManager::kAllAreas, false, true, 127);
}

// Leaves the title screen: this movie plays as the script asked, the menu music follows.
void CScriptStreamedMovie::PortTitleEnd(CStateManager& mgr) {
  mPortTitleIntro = nullptr;
  mPortTitleLoop = nullptr;
  if (sPlayingMovie == this) {
    sPlayingMovie = nullptr;
    CMoviePlayer::SetSfxVolume(127);
  }
  mPortTitlePhase = kPTP_Outro;
  mPortTitleMusicDelay = 2.75f;
  PlayMovie(mgr);
}

void CScriptStreamedMovie::PortTitleStopMusic() {
  for (int i = 0; i < 2; ++i) {
    if (mPortTitleMusic[i]) {
      CSfxManager::SfxStop(mPortTitleMusic[i]);
      mPortTitleMusic[i].Clear();
    }
  }
}
#endif

void CScriptStreamedMovie::Think(float dt, CStateManager& mgr) {
  CActor::Think(dt, mgr);
#ifdef TARGET_PC
  // The outro's countdown runs while inactive too, so the held menu music always starts.
  if (mPortTitlePhase != kPTP_None && (GetActive() || mPortTitlePhase == kPTP_Outro) &&
      PortTitleThink(dt, mgr)) {
    return;
  }
#endif
  if (!GetActive() || mMoviePlayer.get() == nullptr) {
    return;
  }

  if (!mMoviePlayer->CanDrawVideo()) {
    if (!mMoviePlayer->ContinueLoading()) {
      SendScriptMsgs(kSS_Arrived, mgr, kInvalidUniqueId, kSM_None);
#ifdef TARGET_PC
      mPortTitleArrived = true;
#endif
    }
  }

  if (!gpMain->IsMaxSpeed()) {
    const float previousTime = mMoviePlayer->GetPlayedSeconds();
    const bool wasFinished = mMoviePlayer->GetIsMovieFinishedPlaying();
    mMoviePlayer->Update(dt);
    const float fadeStart = mMoviePlayer->GetTotalSeconds() - mFadeOutTime;
    if (previousTime < fadeStart && mMoviePlayer->GetPlayedSeconds() >= fadeStart) {
      SendScriptMsgs(kSS_Approach, mgr, kInvalidUniqueId, kSM_None);
    }
    if (mMoviePlayer->GetIsMovieFinishedPlaying() && !wasFinished) {
      SendScriptMsgs(kSS_MaxReached, mgr, kInvalidUniqueId, kSM_None);
    }
    UpdateVolume(mgr);
    SendExternalTime(mMoviePlayer->GetPlayedSeconds(), mgr);
  } else {
    if (!mSkipping) {
      mSkipTime = mMoviePlayer->GetPlayedSeconds();
      mSkipping = true;
#ifdef TARGET_PC
      sPortSkippedMovie = this;
      sPortSkippedMovieId = GetUniqueId();
#endif
      StopMovie(mgr);
    }
    const float fadeStart = mMoviePlayer->GetTotalSeconds() - mFadeOutTime;
    if (mSkipTime < fadeStart) {
      mSkipTime = fadeStart;
      SendScriptMsgs(kSS_Approach, mgr, kInvalidUniqueId, kSM_None);
    } else if (mSkipTime < mMoviePlayer->GetTotalSeconds()) {
      mSkipTime = mMoviePlayer->GetTotalSeconds();
      SendScriptMsgs(kSS_MaxReached, mgr, kInvalidUniqueId, kSM_None);
    }
    SendExternalTime(mSkipTime, mgr);
  }
}

// @ fn_67_A7C
void CScriptStreamedMovie::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  CActor::AcceptScriptMsg(mgr, msg);
  if (!GetActive()) {
    return;
  }
  switch (msg.GetMessage()) {
  case kSM_Load:
    LoadMovie(mgr);
    break;
  case kSM_Alert:
    if (mMoviePlayer.get() != nullptr && mMoviePlayer->CanDrawVideo()) {
      SendScriptMsgs(kSS_Arrived, mgr, kInvalidUniqueId, kSM_None);
#ifdef TARGET_PC
      mPortTitleArrived = true;
#endif
    }
    break;
  case kSM_Play:
#ifdef TARGET_PC
    if (mPortTitlePhase == kPTP_Waiting && mMoviePlayer.get() != nullptr &&
        mMoviePlayer->CanDrawVideo()) {
      PortTitleStart(mgr);
      break;
    }
#endif
    PlayMovie(mgr);
    break;
  case kSM_Stop:
    StopMovie(mgr);
    break;
  case kSM_Reset:
    RewindMovie(mgr);
    break;
  case kSM_Unload:
  case kSM_Delete:
    UnloadMovie(mgr);
    break;
  default:
    break;
  }
}

// @ fn_67_A28
void CScriptStreamedMovie::PreRender(CStateManager& mgr) {
  if (mWhenToDraw == 0) {
    mgr.RenderLastOverlay(GetUniqueId());
  } else {
    mgr.RenderLastHUD(GetUniqueId());
  }
}

void CScriptStreamedMovie::Render(const CStateManager& mgr) const {
  CMoviePlayer* player = mMoviePlayer.get();
#ifdef TARGET_PC
  if (mPortTitlePhase == kPTP_Intro) {
    player = mPortTitleIntro.get();
  } else if (mPortTitlePhase == kPTP_Loop) {
    player = mPortTitleLoop.get();
  }
#endif
  if (player != nullptr && player->GetPlayMode() == CMoviePlayer::kPM_Playing) {
    const int viewportLeft = CGraphics::GetViewport().mLeft;
    const int viewportTop = CGraphics::GetViewport().mTop;
    const int viewportWidth = CGraphics::GetViewport().mWidth;
    const int viewportHeight = CGraphics::GetViewport().mHeight;
    const CTransform4f viewMatrix = CGraphics::GetViewMatrix();
    const CGraphics::CProjectionState projection = CGraphics::GetProjectionState();
    const float depthNear = CGraphics::GetDepthNear();
    const float depthFar = CGraphics::GetDepthFar();

    int left, bottom, width, height;
    mgr.CalculatePlayerViewport(
        mgr.mPlayerStates[mgr.mCurrentRenderPlayerIndex]->GetPlayerSelection(), &left, &bottom,
        &width, &height);
    gpRender->SetViewport(left, bottom, width, height);
    player->DrawVideo();
    gpRender->SetViewport(viewportLeft, viewportTop, viewportWidth, viewportHeight);
    CGraphics::SetViewPointMatrix(viewMatrix);
    CGraphics::SetProjectionState(projection);
    CGraphics::SetDepthRange(depthNear, depthFar);
  }
}

void CScriptStreamedMovie::SendExternalTime(float time, CStateManager& mgr) {
  const rstl::vector< SConnection >& connections = GetConnectionList();
  for (rstl::vector< SConnection >::const_iterator it = connections.begin();
       it != connections.end(); ++it) {
    if (it->state != kSS_InternalState0) {
      continue;
    }
    CEntity* entity = mgr.ObjectById(mgr.GetIdForScript(it->objId));
    if (entity != nullptr && entity->GetActive()) {
      TCastToPtr< CScriptSequenceTimer >(entity)->ReceiveExternalTime(mgr, time);
    }
  }
}

void CScriptStreamedMovie::LoadMovie(CStateManager& mgr) {
  if (mMoviePlayer.get() == nullptr && mMovieFile.size() != 0) {
    mMoviePlayer =
        rs_new CMoviePlayer(mMovieFile.c_str(), mCacheLength, mLoop, mVideoFilterEnabled);
    mMoviePlayer->SetPlayMode(CMoviePlayer::kPM_Stopped);
  }
}

// @ fn_67_6B8
void CScriptStreamedMovie::PlayMovie(CStateManager& mgr) {
  if (mMoviePlayer.get() != nullptr && mMoviePlayer->CanDrawVideo() && sPlayingMovie == nullptr) {
    sPlayingMovie = this;
    mMoviePlayer->SetPlayMode(CMoviePlayer::kPM_Playing);
    mMoviePlayer->Update(0.f);
  }
}

// @ fn_67_648
void CScriptStreamedMovie::StopMovie(CStateManager& mgr) {
  if (mMoviePlayer.get() != nullptr && mMoviePlayer->CanDrawVideo()) {
    if (this == sPlayingMovie) {
      sPlayingMovie = nullptr;
      CMoviePlayer::SetSfxVolume(127);
    }
    mMoviePlayer->SetPlayMode(CMoviePlayer::kPM_Stopped);
  }
}

// @ fn_67_610
void CScriptStreamedMovie::RewindMovie(CStateManager& mgr) {
  if (mMoviePlayer.get() != nullptr && mMoviePlayer->CanDrawVideo()) {
    mMoviePlayer->Rewind();
  }
}

// @ fn_67_5B4
void CScriptStreamedMovie::UnloadMovie(CStateManager& mgr) {
  if (this == sPlayingMovie) {
    sPlayingMovie = nullptr;
    CMoviePlayer::SetSfxVolume(127);
  }
  mMoviePlayer = nullptr;
}

void CScriptStreamedMovie::UpdateVolume(CStateManager& mgr) {
  if (this != sPlayingMovie) {
    return;
  }
  int volume = mVolume;
  switch (mVolumeType) {
  case 1:
    volume = volume * int(gpGameState->GameOptions().GetSfxVolume()) / 127;
    break;
  case 2:
    volume = volume * int(gpGameState->GameOptions().GetMusicVolume()) / 127;
    break;
  default:
    break;
  }
  CMoviePlayer::SetSfxVolume(volume);
}

CEntity* LoadStreamedMovie(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrStreamedMovie sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrStreamedMovie.inc"

  return rs_new CScriptStreamedMovie(
      mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
      LdrToEntityInfo(info, sldrThis.editorProperties), LdrToTransform4f(sldrThis.editorProperties),
      sldrThis.movieFile, sldrThis.loop, sldrThis.videoFilterEnabled, sldrThis.cacheLength,
      sldrThis.fadeOutTime, sldrThis.whenToDraw, sldrThis.volume, sldrThis.volumeType);
}

#ifndef MONOLITHIC
// @ fn_67_F8
static void SetFuncPtrs() {
  static SStreamedMovie_FuncPtrs funcPtrs;
  funcPtrs.mLoader = &LoadStreamedMovie;
  SetSStreamedMovie_FuncPtrs(&funcPtrs);
}

extern "C" void RELMain() { SetFuncPtrs(); }

extern "C" void RELExit() { SetSStreamedMovie_FuncPtrs(nullptr); }
#endif
