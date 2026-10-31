#ifndef _CDARKSAMUSBATTLESTAGE
#define _CDARKSAMUSBATTLESTAGE

#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/ScriptLoader/SLdrDarkSamusBattleStage.hpp"

// Guessed name; the class follows the DSBS loader and the DarkSamusBattleStage REL.
// It carries the stage's action tuning for the Dark Samus boss.
class CDarkSamusBattleStage : public CEntity {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CDarkSamusBattleStage(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                        const SLdrDSStageInfo& stageInfo);

  // CEntity
  ~CDarkSamusBattleStage() override;
  CEntity* TypesMatch(int typeId) const override;

  const SLdrDSStageInfo& GetStageInfo() const; // Guessed name

#ifdef TARGET_PC
  // The guest's object holds the stage info at its own 0x24 - module 11's accessor fn_11_B70
  // returns this+0x24 - in the loader's SLdrDSStageInfo layout, which the host keeps as it is
  // (the guest's ctor fn_11_90C stores the nine floats at 0x00..0x20, the missiles at 0x24, the
  // six bools at 0x30..0x35, the floats at 0x38/0x3C, the byte at 0x40, the floats at 0x44..0x54,
  // the bytes at 0x58/0x59 and the floats from 0x5C - the host's struct's own layout).
  // The Dark Samus's ShouldFadeOut trigger reads the second bool (the guest's +0x31) and its
  // CanMissileDeflect trigger the first missile float (the guest's +0x24).
  bool GetFadeOutFlag() const { return mStageInfo.unknown_0x49b9936d; }
  float GetMissileDeflectTime() const { return mStageInfo.missiles.unknown_0x7ee77018; }
  // The fourth bool (the guest's +0x33): the Dark Samus's AddToRenderer (fn_10_17F04 at
  // 0x00017F64) draws no effects through the dark visor with it set.
  bool GetUnknown53FDCB5B() const { return mStageInfo.unknown_0x53fdcb5b; }
  // The bool after the boostBall float (the guest's +0x78): the Dark Samus's DiveAfterBoost
  // trigger (fn_10_95A8, `lbz r0, 0x78(r3)` at 0x000095F4) answers with it.
  bool GetDiveAfterBoost() const { return mStageInfo.unknown_0x2d7551e6; }
#endif
private:
  SLdrDSStageInfo mStageInfo; // Guessed name
};
CHECK_SIZEOF(CDarkSamusBattleStage, 0xac)

#endif // _CDARKSAMUSBATTLESTAGE
