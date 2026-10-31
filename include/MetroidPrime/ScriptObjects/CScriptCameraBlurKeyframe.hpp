#ifndef _CSCRIPTCAMERABLURKEYFRAME
#define _CSCRIPTCAMERABLURKEYFRAME

#include "MetroidPrime/CEntity.hpp"

#include "MetroidPrime/Cameras/CCameraBlurPass.hpp"

class CScriptCameraBlurKeyframe : public CEntity {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CScriptCameraBlurKeyframe(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                            CCameraBlurPass::EBlurType type, float amount, int filterGroup,
                            float timeIn, float timeOut);

  // CEntity
  ~CScriptCameraBlurKeyframe() override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

private:
  CCameraBlurPass::EBlurType mType;
  float mAmount;
  int mFilterGroup;
  float mTimeIn;
  float mTimeOut;
};
CHECK_SIZEOF(CScriptCameraBlurKeyframe, 0x38)

#endif // _CSCRIPTCAMERABLURKEYFRAME
