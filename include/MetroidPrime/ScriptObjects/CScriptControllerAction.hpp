#ifndef _CSCRIPTCONTROLLERACTION
#define _CSCRIPTCONTROLLERACTION

#include "MetroidPrime/CControlMapper.hpp"
#include "MetroidPrime/CEntity.hpp"

class CScriptControllerAction : public CEntity {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CScriptControllerAction(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                          CControlMapper::ECommands command, bool mapScreenResponse,
                          uint mapScreenSubaction, bool deactivateOnClose);

  // CEntity
  ~CScriptControllerAction() override;
  void Think(float dt, CStateManager& mgr) override;

#ifdef TARGET_PC
  // Port: an active controller action (not a map screen one) watched for a command `input` holds
  // in the last update or the one before: the scene is waiting on that button, as a cinematic's
  // dialog waits on fire to turn the page. The cinematic skip leaves such an A press to the scene
  // (CMFGame kAM_UserInput).
  static bool PortSceneWaitsOn(const CFinalInput& input, const CStateManager& mgr);
#endif

private:
  CControlMapper::ECommands mCommand;
  uint mMapScreenSubaction;
  uchar mMapScreenResponse : 1;
  bool mDeactivateOnClose : 1;
  bool mPressed : 1;
};
CHECK_SIZEOF(CScriptControllerAction, 0x30)

#endif // _CSCRIPTCONTROLLERACTION
