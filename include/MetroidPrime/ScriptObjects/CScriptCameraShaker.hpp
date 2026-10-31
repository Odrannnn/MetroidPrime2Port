#ifndef _CSCRIPTCAMERASHAKER
#define _CSCRIPTCAMERASHAKER

#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/Cameras/CCameraShakerData.hpp"

class CScriptCameraShaker : public CEntity {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CScriptCameraShaker(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                      const CCameraShakerData& shakeData);
  const CCameraShakerData& GetShakeData() const { return mShakeData; }

  // CEntity
  ~CScriptCameraShaker() override;
  CEntity* TypesMatch(int typeId) const override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

private:
  CCameraShakerData mShakeData;
  int mPlayerShakeIds[4];
};
CHECK_SIZEOF(CScriptCameraShaker, 0x128)

#endif // _CSCRIPTCAMERASHAKER
