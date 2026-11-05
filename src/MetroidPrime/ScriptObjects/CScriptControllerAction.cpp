#include "MetroidPrime/ScriptObjects/CScriptControllerAction.hpp"

#include "MetroidPrime/CMain.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/ScriptLoader.hpp"
#include "MetroidPrime/ScriptLoader/SLdrControllerAction.hpp"

CScriptControllerAction::CScriptControllerAction(TUniqueId uid, const rstl::string& name,
                                                 const CEntityInfo& info,
                                                 const CControlMapper::ECommands command,
                                                 const bool mapScreenResponse,
                                                 const uint mapScreenSubaction,
                                                 const bool deactivateOnClose)
: CEntity(uid, info, name, 0)
, mCommand(command)
, mMapScreenSubaction(mapScreenSubaction)
, mMapScreenResponse(mapScreenResponse)
, mDeactivateOnClose(deactivateOnClose)
, mPressed(false) {}

#ifdef TARGET_PC
namespace {
// The commands active controller actions watched in the latest updates, with the update they
// were last seen in (CScriptControllerAction::PortSceneWaitsOn).
struct SPortWatchedCommand {
  CControlMapper::ECommands command;
  int frame;
};
const int kPortWatchedMax = 8;
SPortWatchedCommand sPortWatched[kPortWatchedMax];
int sPortWatchedCount = 0;

void PortNoteWatched(CControlMapper::ECommands command, int frame) {
  int oldest = 0;
  for (int i = 0; i < sPortWatchedCount; ++i) {
    if (sPortWatched[i].command == command) {
      sPortWatched[i].frame = frame;
      return;
    }
    if (sPortWatched[i].frame < sPortWatched[oldest].frame) {
      oldest = i;
    }
  }
  const int slot = sPortWatchedCount < kPortWatchedMax ? sPortWatchedCount++ : oldest;
  sPortWatched[slot] = {command, frame};
}
} // namespace

bool CScriptControllerAction::PortSceneWaitsOn(const CFinalInput& input, const CStateManager& mgr) {
  const int frame = mgr.GetUpdateFrameIdx();
  for (int i = 0; i < sPortWatchedCount; ++i) {
    const int age = frame - sPortWatched[i].frame;
    if (age >= 0 && age <= 1 &&
        gpGameState->ControlMapper().GetDigitalInput(sPortWatched[i].command, input,
                                                     CControlMapper::kFT_Unfiltered)) {
      return true;
    }
  }
  return false;
}
#endif

void CScriptControllerAction::Think(float dt, CStateManager& mgr) {
  const bool oldPressed = mPressed;
#ifdef TARGET_PC
  if (GetActive() && !mMapScreenResponse) {
    PortNoteWatched(mCommand, mgr.GetUpdateFrameIdx());
  }
#endif

  if (mMapScreenResponse) {
    switch (mMapScreenSubaction) {
    case 0:
      if (mgr.GetInMapScreen()) {
        mPressed = true;
      } else {
        mPressed = false;
      }
      break;
    default:
      break;
    }
  } else if (gpMain->IsMaxSpeed()) {
    mPressed = !mPressed;
  } else {
    if (gpGameState->ControlMapper().GetDigitalInput(mCommand, mgr.mFinalInputs[0],
                                                     CControlMapper::kFT_Unfiltered)) {
      mPressed = true;
    } else {
      mPressed = false;
    }
  }

  if (GetActive() && mPressed != oldPressed) {
    if (mPressed) {
      SendScriptMsgs(kSS_Open, mgr);
      return;
    }

    SendScriptMsgs(kSS_Closed, mgr);
    if (mDeactivateOnClose) {
      SetActive(false);
      SendScriptMsgs(kSS_Inactive, mgr);
    }
  }
}

CEntity* LoadControllerAction(CStateManager& mgr, CInputStream& input, CEntityInfo& info) {
  SLdrControllerAction sldrThis;
#include "MetroidPrime/ScriptLoader/SLdrControllerAction.inc"

  const int command = sldrThis.cmd.command;
  if (command > 0 && command <= 0x4c) {
    return rs_new CScriptControllerAction(mgr.AllocateUniqueId(), sldrThis.editorProperties.name,
                                          LdrToEntityInfo(info, sldrThis.editorProperties),
                                          static_cast< CControlMapper::ECommands >(command), false,
                                          0, sldrThis.oneShot);
  }
  return nullptr;
}

CScriptControllerAction::~CScriptControllerAction() {}
