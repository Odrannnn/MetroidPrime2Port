#ifndef _CSCRIPTPLAYERHINT
#define _CSCRIPTPLAYERHINT

#include "MetroidPrime/CGameHint.hpp"

class CScriptPlayerHint : public CGameHint {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CScriptPlayerHint(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                    const CTransform4f& xf, int priority, float timer, uint overrideFlags,
                    int acrossAreas, float controlInterpDur);

  // CEntity
  ~CScriptPlayerHint() override;
  CEntity* TypesMatch(int typeId) const override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  uint GetOverrideFlags() const { return mOverrideFlags; }
  TUniqueId GetActorId() const { return mActorId; }
#ifdef TARGET_PC
  // Host: REL code clears the hint's actor id (the retail's field at 0x1AC): the Splitter's
  // fn_75_AAC4 when the chassis releases the hint, DigitalGuardian's fn_14_13F70.
  void SetActorId(TUniqueId id) { mActorId = id; }
#endif
  float GetControlInterpDur() const { return mControlInterpDur; }

private:
  uint mOverrideFlags;
  TUniqueId mActorId;
  float mControlInterpDur;
};
CHECK_SIZEOF(CScriptPlayerHint, 0x1b8)

#endif // _CSCRIPTPLAYERHINT
