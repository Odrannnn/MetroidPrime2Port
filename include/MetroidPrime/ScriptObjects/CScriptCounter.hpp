#ifndef _CSCRIPTCOUNTER
#define _CSCRIPTCOUNTER

#include "MetroidPrime/CEntity.hpp"

class CScriptCounter : public CEntity {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CScriptCounter(TUniqueId uid, const rstl::string& name, const CEntityInfo& info, int initial,
                 int max, const bool autoReset, const bool wrap);

  // CEntity
  ~CScriptCounter() override;
  CEntity* TypesMatch(int typeId) const override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  int GetCurrent() const { return mCurrent; }
  int GetMax() const { return mMax; }

private:
#ifdef TARGET_PC
  friend struct PortRoomState; // save states' room state (platform/port_savestate_room.cpp)
#endif
  int mInitial;
  int mCurrent;
  int mMax;
  bool mAutoReset : 1;
  bool mWrap : 1;
};
CHECK_SIZEOF(CScriptCounter, 0x34)

#endif // _CSCRIPTCOUNTER
