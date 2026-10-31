#ifndef _CSCRIPTRELAY
#define _CSCRIPTRELAY

#include "MetroidPrime/CEntity.hpp"

class CScriptRelay : public CEntity {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CScriptRelay(TUniqueId uid, const rstl::string& name, const CEntityInfo& info, bool oneShot);

  // CEntity
  ~CScriptRelay() override;
  CEntity* TypesMatch(int typeId) const override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

private:
  TUniqueId mOriginator;
  bool mOneShot : 1;
};
CHECK_SIZEOF(CScriptRelay, 0x28)

#endif // _CSCRIPTRELAY
