#ifndef _CDIGITALGUARDIANECHOTARGET
#define _CDIGITALGUARDIANECHOTARGET

// Port-only: the DigitalGuardian REL's echo-target actor (module 14). Upstream has no source for
// it; the retail class is 0x790 bytes, vtable lbl_14_data_10D0 (31 words), constructor
// fn_14_1A698 and destructor fn_14_1A5F4. The head's fn_14_12314 spawns three of them ("EchoTarget")
// round its locator; the head's ExtractEchoTargets/RetractEchoTargets move them between states.
// The guest offsets are in the member names; the host class has its own layout.

#include "types.h"

#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/ScriptLoader/SLdrDigitalGuardianHead.hpp"

#include "rstl/optional_object.hpp"

class CElementGen;

class CDigitalGuardianEchoTarget : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  // The states at 0x708: 0 retracted, 1 extended, 2 retracting, 3 extending, 4 destroyed.
  enum EState {
    kS_Retracted = 0,
    kS_Extended = 1,
    kS_Retracting = 2,
    kS_Extending = 3,
    kS_Destroyed = 4,
  };

  // fn_14_1A698. The retail's argument order: the vulnerability, the owner (the head) and the
  // head's parameter block come on the stack after the material list.
  CDigitalGuardianEchoTarget(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                             uint inGrave, const CTransform4f& xf, const CModelData& mData,
                             const CMaterialList& list, const CDamageVulnerability& vuln,
                             TUniqueId owner, const SLdrDigitalGuardianHeadData& data);
  ~CDigitalGuardianEchoTarget() override; // fn_14_1A5F4

  // CEntity
  void Think(float dt, CStateManager& mgr) override;                        // fn_14_1A1F8
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override; // fn_14_1A430

  // CActor
  void Render(const CStateManager& mgr) const override;                 // fn_14_1A168
  // @ fn_14_1ABD8
  CHealthInfo* HealthInfo() override { return &x764_; } // addi r3, r3, 0x764
  const CDamageVulnerability* GetDamageVulnerability() const override;  // fn_14_1A12C
  rstl::optional_object< CAABox > GetTouchBounds() const override;      // fn_14_1AB50
  CVector3f GetOrbitPosition(const CStateManager& mgr) const override;  // fn_14_1AB98
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override; // fn_14_19FD0

  int GetState() const { return x708_; }
  // fn_14_19DF8 (0x120 bytes): a new state (not once destroyed) and its sound.
  void SetState(CStateManager& mgr, int state);
  // The flag at 0x78C bit 7: set, XDMG is ignored (the head's RetractEchoTargets and
  // fn_14_11FEC set it, ExtractEchoTargets clears it).
  void SetDamageIgnored(bool ignored) { x78C_24_ = ignored; }
  // fn_14_19F18 (0xB8 bytes): xf pushed out along its up column by 6 * the extension (0x710).
  void SetExtendedTransform(const CTransform4f& xf);

private:
  // fn_14_19A08 (0x168 bytes): the transmit sound while extending.
  void x19A08_(CStateManager& mgr);
  // fn_14_19B70 (0x170 bytes): the emitter effect while extending or extended.
  void x19B70_(CStateManager& mgr, float dt);
  // fn_14_19CE0 (0x118 bytes): the extension and the target materials per state.
  void x19CE0_(CStateManager& mgr, float dt);

  SLdrDigitalGuardianHeadData x158_;     // 0x158: the head's parameter block (fn_14_1912C)
  int x708_;                             // 0x708: the state
  float x70C_;                           // 0x70C: the hit flash timer
  float x710_;                           // 0x710: the extension (0..1)
  TUniqueId x714_;                       // 0x714: the owner (the head)
  rstl::optional_object< CAABox > x718_; // 0x718 (flag 0x730): the touch bounds
  CDamageVulnerability x734_;            // 0x734: the vulnerability while extended (fn_14_3B4C)
  CHealthInfo x764_;                     // 0x764
  CElementGen* x784_;                    // 0x784: the emitter effect (data +0xA0)
  CSfxHandle x788_;                      // 0x788: the transmit sound's emitter
  bool x78C_24_ : 1;                     // 0x78C bit 7: XDMG ignored
};

#endif
