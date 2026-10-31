#ifndef _CINGEXITHOSTEFFECT
#define _CINGEXITHOSTEFFECT

#include "types.h"

#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/SObjectTag.hpp"
#include "Kyoto/TToken.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"

class CParticleGen;

// The Ing REL's CIngExitHostEffect (module 29, ctor fn_29_1105C, dtor fn_29_FF88): the swarm effect
// CIng's ExitHost state spawns at the exit host's destination and then rides along
// (docs/module-analysis/IngFamily.md section A.9). Retail's is 0x5e8 bytes (the CActor base is
// 0x158). The REL's own vtable is lbl_29_data_C60; its overrides are Think (retail slot 0x14,
// fn_29_10F58), AcceptScriptMsg (0x18, fn_29_10EB4), AddToRenderer (0x28, fn_29_10DEC), Render
// (0x2C, fn_29_10E64) and the dtor, all ported in PortCIngExitHostEffect.cpp. The offsets in the
// comments are the REL's, from fn_29_1105C. The host members are wider (a CPathFindSearch and a
// CToken carry pointers), so the host class is larger than 0x5e8 - nothing outside the REL reads it
// by offset.
//
// Retail's two "created" bytes at 0x2ac/0x2bc are only `mEffectA/mEffectB != nullptr` (the asm's
// neg/or/srwi-31 idiom) and gate the dtor's delete, so the host has no member for them. Retail's
// 0x304 word is only the trail container's size (60) and is never read; the host's array has no
// separate size word.
class CIngExitHostEffect : public CActor {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CIngExitHostEffect(TUniqueId uid, const CEntityInfo& info, const CTransform4f& xf,
                     const CVector3f& dest, const CVector3f& scale, CAssetId assetA,
                     CAssetId assetB, float x2c4, float x290, TUniqueId target, float x298,
                     float x2a0, ushort sound);
  ~CIngExitHostEffect() override;

  // Retail 0x5e2 bit 7: set once the swarm has arrived, which is what CIng::ExitHost's Update
  // watches to end the state (fn_29_9D68 reads the byte and branches on its sign).
  bool IsDone() const { return (mFlags & 0x80) != 0; }

  // The REL's four overrides (vtable slots 0x14, 0x18, 0x28 and 0x2C). Retail's Think does not
  // chain to CActor's: this actor has no model or animation to advance.
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg&) override;
  void AddToRenderer(const CStateManager&) const override;
  void Render(const CStateManager&) const override;

private:
  void Fly(float dt, CStateManager& mgr);             // fn_29_1077C
  void BuildBezier(CStateManager& mgr);               // fn_29_10154
  CVector3f RandomOffset(CStateManager& mgr) const;   // fn_29_1065C
  float BezierLength() const;                         // fn_29_1046C
  void UpdateParticles(float dt, CStateManager& mgr); // fn_29_10944
  void StartSwarmSfx();                               // fn_29_100B8
  void StopSwarmSfx();                                // fn_29_1006C

  // The host layout (retail offsets in the comments, from fn_29_1105C; the asm offsets are the
  // REL's, so every member can be traced back to the store that filled it).
  // 0x158: retail's own copy of the ctor's transform, not CActor's at 0x54 (asm 0x11164).
  CTransform4f mXf;
  CPathFindSearch mPathFindSearch; // 0x188 (0xec), asm 0x11190: area null, flags 3, index 0
  CVector3f mDest;                 // 0x274: the state's exit destination, CIng+0xdc8 (asm 0x111A4)
  CVector3f mScale;                // 0x280: the model scale the ctor is given (asm 0x111C4)
  float mX28c;                     // 0x28c: 0.0f (asm 0x111DC, rodata_798)
  float mX290;                     // 0x290: the ctor's x290 (CIngData swarm +0x14) (asm 0x111E0)
  TUniqueId mTarget;               // 0x294: the target id the swarm homes on (CIng+0xd6c)
                                   // (asm 0x111E8)
  float mX298;                     // 0x298: the ctor's x298 (CIngData swarm +0x18) (asm 0x111EC)
  float mX29c;                     // 0x29c: the same value as 0x298, stored twice (asm 0x111F0)
  float mX2a0;                     // 0x2a0: the ctor's x2a0 (CIngData swarm +0x1c) (asm 0x111F4)
  CToken mTokenA;                  // 0x2a4: assetA (an SRSC); type-erased, see the ctor
                                   // (asm 0x11238)
  CParticleGen* mEffectA;          // 0x2b0: what fn_29_10D50 built from mTokenA (asm 0x11274)
  CToken mTokenB;                  // 0x2b4: assetB (a PART) (asm 0x11394)
  CParticleGen* mEffectB;          // 0x2c0: what fn_29_10D50 built from mTokenB (asm 0x11300)
  float mX2c4;                     // 0x2c4: the ctor's x2c4 (CIngData swarm +0x0c) (asm 0x11308)
  int mBezierCount;                // 0x2c8: 0, the bezier point count (asm 0x1130C)
  CVector3f mBezier[4];            // 0x2cc..0x2f8: the bezier control points; retail's ctor never
                                   // writes them (they stay uninitialised), the host zeroes them
  float mX2fc;                     // 0x2fc: 0.0f (asm 0x11310, rodata_798)
  float mX300;                     // 0x300: 1.0f (asm 0x11314, rodata_7A8)
  CVector3f mTrail[60];            // 0x308 (retail's container is at 0x304): the swarm's path ring
                                   // (asm 0x11348-0x113F0: 60 copies of the translation)
  int mTrailIndex;                 // 0x5d8: 0, the trail write index (asm 0x1131C)
  CSfxHandle mSwarmMoveHandle;     // 0x5dc: null; fn_29_1006C/100B8 are the emitter's start/stop
                                   // (asm 0x11320)
  ushort mSound;                   // 0x5e0: the ctor's sound, CIngData swarm +0x20 (asm 0x11324)
  uchar mFlags;                    // 0x5e2: bit 7 set once the swarm has arrived; the ctor
                                   // clears it (asm 0x11328-0x11330: rlwimi r0, r6, 7, 24, 24 with
                                   // r6 = 0 clears bit 24 of the word = 1u<<(31-24) = 0x80 of the
                                   // byte)
};
// Retail size; inert off Metrowerks, and the host members are wider.
CHECK_SIZEOF(CIngExitHostEffect, 0x5e8)

#endif // _CINGEXITHOSTEFFECT
