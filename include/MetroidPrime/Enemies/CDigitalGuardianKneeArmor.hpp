#ifndef _CDIGITALGUARDIANKNEEARMOR
#define _CDIGITALGUARDIANKNEEARMOR

// Port-only: the DigitalGuardian REL's knee-armour actor (module 14). Upstream has no source for
// it; the retail class is 0x1A0 bytes, vtable lbl_14_data_694 (31 words), constructor
// fn_14_D4D0 and destructor fn_14_D460. fn_14_4080 spawns four of them, one at each knee
// locator. The guest offsets are in the member names; the host class has its own layout.

#include "types.h"

#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/ScriptLoader/Structs/SLdrAudioPlaybackParms.hpp"

#include "rstl/optional_object.hpp"

struct SEchoParameters;

class CDigitalGuardianKneeArmor : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // fn_14_D4D0. The retail's argument order: the owner id, the vulnerable flag, the actor
  // parameters, the echo parameters and the sound come on the stack after the material list.
  CDigitalGuardianKneeArmor(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                            uint inGrave, const CTransform4f& xf, const CModelData& mData,
                            const CMaterialList& list, TUniqueId owner, bool vulnerable,
                            const CActorParameters& params, const SEchoParameters& echo,
                            const SLdrAudioPlaybackParms& sound);
  ~CDigitalGuardianKneeArmor() override; // fn_14_D460

  // CEntity
  void Think(float dt, CStateManager& mgr) override;                      // fn_14_CF94
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override; // fn_14_D320

  // CActor
  const CDamageVulnerability* GetDamageVulnerability() const override; // fn_14_D7BC
  rstl::optional_object< CAABox > GetTouchBounds() const override;       // fn_14_D774

  // fn_14_CE94 (0x100 bytes): the armoured flag (guest 0x19C bit 7). Clearing it starts the
  // vulnerable sound and makes the armour a target; setting it stops the sound and removes the
  // target materials.
  void SetArmored(CStateManager& mgr, bool armored);
  // fn_14_CE1C (0x78 bytes): the orbit material on, or off with the player's orbit on it broken.
  void SetOrbitable(CStateManager& mgr, bool orbitable);
  bool IsArmored() const { return x19C_24_armored_; }

private:
  float x158_;     // 0x158: the hit flash timer ("XDMG" sets it to 1.5)
  float x15C_;     // 0x15C: the vulnerable pulse's phase
  TUniqueId x160_; // 0x160: the owner (the guardian)
  rstl::optional_object< CAABox > x164_; // 0x164 (flag 0x17C): the touch bounds
  SLdrAudioPlaybackParms x180_;          // 0x180: the vulnerable sound (sound_KneeVulnerable)
  CSfxHandle x198_;                      // 0x198: the vulnerable sound's emitter
  bool x19C_24_armored_ : 1;             // 0x19C bit 7
};

#endif
