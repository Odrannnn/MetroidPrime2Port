#ifndef _CSCRIPTSTREAMEDMOVIE
#define _CSCRIPTSTREAMEDMOVIE

#include "MetroidPrime/CActor.hpp"

#include "Kyoto/Graphics/CMoviePlayer.hpp"
#include "rstl/single_ptr.hpp"
#include "rstl/string.hpp"
#ifdef TARGET_PC
#include "Kyoto/Audio/CSfxHandle.hpp"
#endif

// Guessed name: plays a THP movie from the disc while the script object is active.
class CScriptStreamedMovie : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CScriptStreamedMovie(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                       const CTransform4f& xf, const rstl::string& movieFile, bool loop,
                       bool videoFilterEnabled, float cacheLength, float fadeOutTime,
                       int whenToDraw, int volume, int volumeType);

  // CEntity
#ifdef TARGET_PC
  ~CScriptStreamedMovie() override;
#else
  ~CScriptStreamedMovie() override {}
#endif
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CActor
  void PreRender(CStateManager& mgr) override;
  void Render(const CStateManager& mgr) const override;

#ifdef TARGET_PC
  // Port: true while a movie that a cinematic skip stopped is still in the area (see CMFGame).
  static bool PortSkippedMovieLive(const CStateManager& mgr);
  // Port: true (and the music remembered) while the Trilogy title screen holds back the menu
  // music; it starts when the title screen ends.
  static bool PortTitleHoldsMusic(const CStateManager& mgr, TUniqueId music);
#endif

private:
#ifdef TARGET_PC
  enum EPortTitlePhase {
    kPTP_None,    // not the Trilogy front end's first movie
    kPTP_Waiting, // title music on, waiting for the script to play this movie
    kPTP_Intro,
    kPTP_Loop,
    kPTP_Outro, // this movie plays; the title music stops after the GameCube's delay
    kPTP_Done,
  };
  bool PortTitleThink(float dt, CStateManager& mgr);
  void PortTitleStart(CStateManager& mgr);
  void PortTitleEnd(CStateManager& mgr);
  void PortTitleStopMusic();
#endif

  void LoadMovie(CStateManager& mgr);                    // Guessed name
  void PlayMovie(CStateManager& mgr);                    // Guessed name
  void StopMovie(CStateManager& mgr);                    // Guessed name
  void RewindMovie(CStateManager& mgr);                  // Guessed name
  void UnloadMovie(CStateManager& mgr);                  // Guessed name
  void UpdateVolume(CStateManager& mgr);                 // Guessed name
  void SendExternalTime(float time, CStateManager& mgr); // Guessed name

  rstl::string mMovieFile;                       // Guessed name
  int mWhenToDraw;                               // Guessed name
  int mVolume;                                   // Guessed name
  int mVolumeType;                               // Guessed name
  float mCacheLength;                            // Guessed name
  float mFadeOutTime;                            // Guessed name
  bool mLoop : 1;                                // Guessed name
  bool mVideoFilterEnabled : 1;                  // Guessed name
  bool mSkipping : 1;                            // Guessed name
  float mSkipTime;                               // Guessed name
  rstl::single_ptr< CMoviePlayer > mMoviePlayer; // Guessed name
#ifdef TARGET_PC
  EPortTitlePhase mPortTitlePhase;
  float mPortTitleMusicDelay;
  rstl::single_ptr< CMoviePlayer > mPortTitleIntro;
  rstl::single_ptr< CMoviePlayer > mPortTitleLoop;
  CSfxHandle mPortTitleMusic[2];
  TUniqueId mPortHeldMusic;
#endif
};
CHECK_SIZEOF(CScriptStreamedMovie, 0x188)

#endif // _CSCRIPTSTREAMEDMOVIE
