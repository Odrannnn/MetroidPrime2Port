#include "MetroidPrime/ScriptObjects/CScriptStreamedMusic.hpp"

#include "Kyoto/Audio/CStreamAudioManager.hpp"
#include "Kyoto/Basics/CCast.hpp"
#include "Kyoto/CDvdFile.hpp"
#include "MetroidPrime/CInGameTweakManager.hpp"
#include "MetroidPrime/CMain.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrStreamedAudio.hpp"
#include "rstl/StringExtras.hpp"

#include <string.h>

#ifdef TARGET_PC
#include "MetroidPrime/ScriptObjects/CScriptStreamedMovie.hpp"
#include "port_overlay.h"
#endif

extern "C" void fn_8015DCF0(CScriptStreamedMusic*);

CScriptStreamedMusic::CScriptStreamedMusic(TUniqueId id, const CEntityInfo& info,
                                           const rstl::string& name, const rstl::string& fileName,
                                           bool noStopOnDeactivate, float fadeIn, float fadeOut,
                                           uint volume, bool loop, bool music)
: CEntity(id, info, name, 0)
, mFileName(fileName)
, mNoStopOnDeactivate(noStopOnDeactivate)
, mFileIsDsp(IsDSPFile(fileName))
, mLoop(loop)
, mMusic(music)
, mPreloadPending(false)
, mFadeIn(fadeIn)
, mFadeOut(fadeOut)
, mVolume(volume)
, mPreload() {
  fn_8015DCF0(this);
}

extern "C" void fn_8015DCF0(CScriptStreamedMusic*) {}

CScriptStreamedMusic::~CScriptStreamedMusic() {}

bool CScriptStreamedMusic::IsDSPFile(const rstl::string& fileName) {
  return CStringExtras::CompareCaseInsensitive(fileName, rstl::string_l("sw")) == 0 ||
         CStringExtras::IndexOfSubstring(fileName, rstl::string_l(".dsp")) != -1;
}

CStreamAudioManager::ESoftwareChannel CScriptStreamedMusic::IsOneShot(bool loop) {
  return loop ? CStreamAudioManager::kSC_Default : CStreamAudioManager::kSC_OneShot;
}

void CScriptStreamedMusic::Think(float dt, CStateManager& mgr) {
  if (mPreloadPending && mPreload->IsReady()) {
    SendScriptMsgs(static_cast< EScriptObjectState >(0x41525256), mgr);
    mPreloadPending = false;
  }
}

void CScriptStreamedMusic::StartStream() {
  if (!mPreload || mPreload->IsReady()) {
    CStreamAudioManager::PlaySoftwareAudio(IsOneShot(mLoop), mFileName, mFadeIn, mFadeOut,
                                           static_cast< uchar >(mVolume), mMusic);
  }
}

void CScriptStreamedMusic::StopStream() {
  CStreamAudioManager::StopSoftwareAudio(IsOneShot(mLoop), mFileName);
}

namespace rstl {
// Keep the iterator-range constructor's distance calculation inline in this TU.
template <>
inline long distance< string::const_iterator >(string::const_iterator first,
                                               string::const_iterator last) {
  return last - first;
}
} // namespace rstl

template <>
template <>
rstl::basic_string< char >::basic_string(rstl::basic_string< char >::const_iterator first,
                                         rstl::basic_string< char >::const_iterator last,
                                         const rstl::rmemory_allocator&) {
  const int len = rstl::distance(first, last);
  internal_allocate(len + 1);
  int i = 0;
  for (const_iterator it = first; it != last; it = it + 1, ++i) {
    const_cast< char& >(mPtr[i]) = *it;
  }
  const_cast< char& >(mPtr[i]) = char_traits< char >::eos();
  mSize = len;
}

#if defined(TARGET_PC) && defined(_MSC_VER)
// Port: other units implicitly instantiate string.hpp's substr; lld-link rejects this explicit
// specialization next to those COMDATs (ELF linkers take the strong one), and both are the same.
#else
template <>
rstl::string rstl::basic_string< char >::substr(int pos, int count) const {
  const pair< const_iterator, const_iterator > range = range_iterator(pos, count);
  return basic_string(range.first, range.second);
}
#endif

void CScriptStreamedMusic::PreloadMemoryAudio() {
  if (mPreload) {
    mPreloadPending = true;
    return;
  }
  const char* fileName = mFileName.data();
  if (strncmp(fileName, "mem:", strlen("mem:")) == 0) {
    const rstl::string path = mFileName.substr(4);
    if (CDvdFile::FileExists(path.data())) {
      mPreload = CStreamPreloadedToken(path);
      mPreloadPending = true;
    }
  }
}

void CScriptStreamedMusic::TweakOverride(CStateManager& mgr) {
  const rstl::string key = CInGameTweakManager::GetIdentifierForMusicEvent(
      mgr.GetWorld()->GetAreaAlways(GetCurrentAreaId()).GetAreaAssetId(), rstl::string_l(""));
  if (gpTweakManager->HasTweakValue(key)) {
    const CTweakValue::Audio& audio = gpTweakManager->GetTweakValue(key)->GetAudio();
    const rstl::string fileName(audio.GetFileName());
    const float fadeIn = audio.GetFadeIn();
    const char volume = CCast::ToInt8(audio.GetVolume() * 127.f);
    const float fadeOut = audio.GetFadeOut();

    mFileName = fileName;
    mFileIsDsp = IsDSPFile(mFileName);
    mFadeIn = fadeIn;
    mVolume = volume;
    mFadeOut = fadeOut;
    fn_8015DCF0(this);
    SetStereoPair();
  }
}

#ifdef TARGET_PC
// Enhancement (#36): each region's Dark Aether theme becomes its light world theme. Only these
// area themes change (read off the disc's StreamedAudio objects); boss, event and cutscene music
// stays. A portal hop asks for the track already playing, so it keeps going instead of restarting.
// The name is swapped before playing, so Stop and save states see the track that plays.
void CScriptStreamedMusic::PortLightWorldMusic() {
  static const char* const kSwaps[][2] = {
      {"/audio/Dark-Temple32.dsp", "/audio/Moth-Temple-open32-2.dsp"}, // Sky Temple Grounds
      {"/audio/Darkworld_sand.dsp", "/audio/sand_soto.dsp"},           // Dark Agon Wastes
      {"/audio/Dark-Swamp-32.dsp", "/audio/swamp3-32.dsp"},            // Dark Torvus Bog
      {"/audio/swanp-chika-dark32.dsp", "/audio/swanp-chika32.dsp"},   // Dark Torvus, underground
      {"/audio/Clif-dark32.dsp", "/audio/cliff2-1-32.dsp"},            // Ing Hive
      {"/audio/sunG-heya-dark32.dsp", "/audio/sunG-heya32.dsp"},       // dark energy controllers
  };
  if (!mFileIsDsp || !PortOverlay::DarkWorldLightMusic()) {
    return;
  }
  for (int i = 0; i < static_cast< int >(sizeof(kSwaps) / sizeof(kSwaps[0])); ++i) {
    if (CStringExtras::CompareCaseInsensitive(mFileName, rstl::string_l(kSwaps[i][0])) == 0) {
      if (CDvdFile::FileExists(kSwaps[i][1])) {
        mFileName = rstl::string_l(kSwaps[i][1]);
      }
      return;
    }
  }
}
#endif

void CScriptStreamedMusic::SetStereoPair() {
  if (mFileIsDsp && mFileName.find('|', 0) == -1 && mFileName.size() >= 5) {
    if (CStringExtras::CompareCaseInsensitive(
            rstl::string_l(mFileName.data() + mFileName.size() - 5), rstl::string_l("L.dsp")) ==
        0) {
      rstl::string right = rstl::string(mFileName.begin(), mFileName.end() - 5) + "R.dsp";
      if (CDvdFile::FileExists(right.data())) {
        mFileName = mFileName + '|' + right;
      }
    }
  }
}

void CScriptStreamedMusic::StopNonDsp() { CStreamAudioManager::FadeBackIn(mFadeOut); }

void CScriptStreamedMusic::PlayNonDsp() {
  const char volume = mVolume;
  if (mNoStopOnDeactivate) {
    CStreamAudioManager::SetDefaultAudio(mFileName, mFadeOut, mFadeIn, volume);
  } else {
    CStreamAudioManager::SetCurrentAudio(mFileName, mFadeOut, mFadeIn, volume);
  }
}

void CScriptStreamedMusic::Stop() {
  if (mFileIsDsp) {
    StopStream();
  } else {
    StopNonDsp();
  }
}

void CScriptStreamedMusic::PlayAudio() {
  if (mFileIsDsp) {
    StartStream();
  } else {
    PlayNonDsp();
  }
}

void CScriptStreamedMusic::Play(CStateManager& mgr) {
  if (!GetUpdateDuringCinematicSkip() && gpMain->IsMaxSpeed()) {
    return;
  }
  TweakOverride(mgr);
#ifdef TARGET_PC
  PortLightWorldMusic();
#endif
  if (mFileIsDsp) {
    StartStream();
  } else {
    PlayNonDsp();
  }
}

void CScriptStreamedMusic::AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) {
  const EScriptObjectMessage message = msg.GetMessage();
  CEntity::AcceptScriptMsg(mgr, msg);
  switch (message) {
  case kSM_Play:
#ifdef TARGET_PC
    // Trilogy's menu music waits for the GameCube title screen the port puts back.
    if (GetActive() && CScriptStreamedMovie::PortTitleHoldsMusic(mgr, GetUniqueId())) {
      break;
    }
#endif
    if (GetActive()) {
      Play(mgr);
    }
    break;
  case kSM_Load:
    if (GetActive() && mFileIsDsp) {
      PreloadMemoryAudio();
    }
    break;
  case kSM_Stop:
    if (GetActive()) {
      Stop();
    }
    break;
  case kSM_Deactivate:
    if ((!mFileIsDsp && !mNoStopOnDeactivate) || mFileIsDsp) {
      Stop();
    }
    break;
  case kSM_Increment:
    if (mFileIsDsp) {
      CStreamAudioManager::FadeInSoftwareAudio(IsOneShot(mLoop), mFadeIn);
    } else {
      CStreamAudioManager::fn_803653F8(mFadeIn);
    }
    break;
  case kSM_Decrement:
    if (mFileIsDsp) {
      CStreamAudioManager::FadeOutSoftwareAudio(IsOneShot(mLoop), mFadeOut);
    } else {
      CStreamAudioManager::fn_80365424(mFadeOut);
    }
    break;
  default:
    break;
  }
}

CEntity* LoadStreamedAudio(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrStreamedAudio sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrStreamedAudio.inc"

  return new CScriptStreamedMusic(
      mgr.AllocateUniqueId(), LdrToEntityInfo(info, sldrThis.editorProperties),
      sldrThis.editorProperties.name, sldrThis.songFile, sldrThis.defaultAudio, sldrThis.fadeInTime,
      sldrThis.fadeOutTime, sldrThis.volume, sldrThis.softwareChannel == 0,
      sldrThis.softwareIsMusic);
}
