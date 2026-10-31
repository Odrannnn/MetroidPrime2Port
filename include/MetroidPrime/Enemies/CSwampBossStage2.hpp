#ifndef _CSWAMPBOSSSTAGE2
#define _CSWAMPBOSSSTAGE2

#include "types.h"

#include "Kyoto/Audio/CSfxHandle.hpp"
#include "Kyoto/Animation/CSegId.hpp"
#include "MetroidPrime/CDamageInfo.hpp"
#include "MetroidPrime/CDamageVulnerability.hpp"
#include "MetroidPrime/CMissileRepeller.hpp"
#include "MetroidPrime/CModelData.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/ScriptLoader/SLdrSwampBossStage2.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"

#include "rstl/list.hpp"
#include "rstl/optional_object.hpp"
#include "rstl/reserved_vector.hpp"
#include "rstl/single_ptr.hpp"
#include "rstl/vector.hpp"

class CActor;
class CCollisionActor;
class CCollisionActorManager;
class CElementGen;
class CGenDescription;
class CScannableObjectInfo;

// Wii SEL / retail REL class name (SwampBossStage2, module 79): Chykka's adult form. The light
// flyer (head, wing growths) and the dark flyer (ovipositor) alternate through four phases.
// docs/module-analysis/SwampBoss-stage2.md has the function map and the retail member offsets.
class CSwampBossStage2 : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CSwampBossStage2(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                   const CTransform4f& xf, const CModelData& modelData,
                   const CActorParameters& actParms, const CPatternedInfo& pInfo,
                   const SLdrSwampBossStage2Data& data);

  // CEntity
  ~CSwampBossStage2() override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CActor
  void PreRender(CStateManager& mgr) override;
  void AddToRenderer(const CStateManager& mgr) const override;
  void Render(const CStateManager& mgr) const override;
  CVector3f GetAimPosition(const CStateManager& mgr, float dt) const override;
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  CScannableObjectInfo* GetScannableObjectInfo() const override;

  // CAi
  void TakeDamage(const CVector3f& dir, float amount) override;

  // CPatterned
  void SetupStateMachine(CStateManager& mgr) override;
  // fn_79_A4AC
  CProjectileInfo* ProjectileInfo() override { return &mSpitProjectile; }
  CVector3f GetIngSnatchingNormal(float t) const override;
  CVector3f GetIngSnatchingPoint(float t) const override;

  // Triggers
  bool StateOver(CStateManager& mgr, const CTriggerData& data) const;
  bool AllGrowthsDead(CStateManager& mgr, const CTriggerData& data) const;
  bool OvipositorDead(CStateManager& mgr, const CTriggerData& data) const;
  bool ZeroHealth(CStateManager& mgr, const CTriggerData& data) const;
  bool PlayerInWater(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldSpit(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldBarrage(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldTaunt(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldSwoop(CStateManager& mgr, const CTriggerData& data) const;
  bool SwoopOver(CStateManager& mgr, const CTriggerData& data) const;
  bool CircleDamage(CStateManager& mgr, const CTriggerData& data) const;
  bool CircleOver(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldBlow(CStateManager& mgr, const CTriggerData& data) const;
  bool IsLevel(CStateManager& mgr, const CTriggerData& data) const;
  bool SwitchedPlatform(CStateManager& mgr, const CTriggerData& data) const;
  bool Stunned(CStateManager& mgr, const CTriggerData& data) const;
  bool StunTimeOut(CStateManager& mgr, const CTriggerData& data) const;
  bool OneGrowthDead(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldDropFlyer(CStateManager& mgr, const CTriggerData& data) const;

  // States
  void Start(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeLightFlyer(CStateManager& mgr, EStateMsg msg, float dt);
  void BecomeDarkFlyer(CStateManager& mgr, EStateMsg msg, float dt);
  void DeathSequence(CStateManager& mgr, EStateMsg msg, float dt);
  void Lurk(CStateManager& mgr, EStateMsg msg, float dt);
  void DarkLurk(CStateManager& mgr, EStateMsg msg, float dt);
  void SpitWater(CStateManager& mgr, EStateMsg msg, float dt);
  void SpitBarrage(CStateManager& mgr, EStateMsg msg, float dt);
  void Taunt(CStateManager& mgr, EStateMsg msg, float dt);
  void Level(CStateManager& mgr, EStateMsg msg, float dt);
  void Swoop(CStateManager& mgr, EStateMsg msg, float dt);
  void Blow(CStateManager& mgr, EStateMsg msg, float dt);
  void Circle(CStateManager& mgr, EStateMsg msg, float dt);
  void Reel(CStateManager& mgr, EStateMsg msg, float dt);
  void Recover(CStateManager& mgr, EStateMsg msg, float dt);
  void Flinch(CStateManager& mgr, EStateMsg msg, float dt);
  void DropFlyer(CStateManager& mgr, EStateMsg msg, float dt);

  // Code
  void ResetAttackPhase(CStateManager& mgr, float dt);

protected:
  struct SDamageMemory {
    float mTime;
    float mDamage;
  };

  const SLdrSwampBossStage2Phase& CurrentPhase() const;
  int AttackForIndex(int idx) const;
  void NextAttack();
  CCollisionActor* CollisionActor(CStateManager& mgr, int idx) const;
  CVector3f CollisionActorPosition(const CStateManager& mgr, int idx) const;
  CVector3f CenterPosition(const CStateManager& mgr) const;
  CVector3f TargetPoint(const CStateManager& mgr) const;
  void RequestMove(const CVector3f& point);
  CActor* NextChainNode(CStateManager& mgr) const;
  void SetBodyVulnerable(CStateManager& mgr, bool vulnerable);
  void UpdatePlayerSideMaterials(CStateManager& mgr);
  CVector3f DirectionFromRandomNode(CStateManager& mgr) const;
  CVector3f DirectionToPlayerAim(const CStateManager& mgr) const;
  CVector3f PlayerAimPoint(const CStateManager& mgr) const;
  TUniqueId NearestNodeToPlayer(CStateManager& mgr) const;
  void InitGrowths();
  void InitAdditiveAnim();
  void SetupCollisionManager(CStateManager& mgr);
  void SetupCollisionJoints(CStateManager& mgr);
  void Activate(CStateManager& mgr);
  void SetGrowthTargetEffects(CStateManager& mgr, bool on);
  void SetGrowthsActive(CStateManager& mgr, bool active);
  void SetGrowthActive(CStateManager& mgr, int idx, bool active);
  void UpdateHover(float dt);
  void UpdateAdditive(CStateManager& mgr, float dt);
  void UpdateWater(CStateManager& mgr, const CVector3f& pos, CVector3f& lastSplash);
  void UpdateHealthFraction(CStateManager& mgr);
  void AddAdditiveFlinch(float amount);
  void StartBubbleTelegraph(CStateManager& mgr, const CVector3f& pos);
  void SpawnShockWave(CStateManager& mgr, const CVector3f& pos);
  void SpawnWindEffect(CStateManager& mgr);
  void SpawnWingDamageEffect(CStateManager& mgr, int idx);
  void ReadWaterAndNodes(CStateManager& mgr);
  void PickSwoopTarget(CStateManager& mgr);
  void PickSpitNode(CStateManager& mgr);
  void FireSpit(CStateManager& mgr, const rstl::string& locator);
  CVector3f PlayerOverheadPosition(const CStateManager& mgr) const;
  CVector3f SpitOrigin(const CStateManager& mgr) const;
  void AdvanceSpitNode(CStateManager& mgr);
  void LaunchPlatformChain(CStateManager& mgr, TUniqueId chain,
                           rstl::vector< TUniqueId >& nodes, rstl::vector< CVector3f >& positions,
                           const CVector3f& from);
  void AimSpitPlatforms(CStateManager& mgr, const rstl::vector< TUniqueId >& platforms,
                        const CVector3f& from);
  void PlaceChainNodes(CStateManager& mgr, const rstl::vector< TUniqueId >& nodes,
                       const rstl::vector< CVector3f >& positions) const;
  void CollectChainNodes(CStateManager& mgr, TUniqueId chain, rstl::vector< TUniqueId >& nodes,
                         rstl::vector< CVector3f >& positions);
  void WalkChainNodes(CStateManager& mgr, CActor* node, rstl::vector< TUniqueId >& nodes,
                      rstl::vector< CVector3f >& positions);
  void SwingChainNodes(CStateManager& mgr, const rstl::vector< TUniqueId >& nodes,
                       const CVector3f& origin, const CTransform4f& rotation) const;
  void PlaceChainSegment(CStateManager& mgr, CActor& chain, const CVector3f& origin,
                         const CTransform4f& rotation);
  void SendDecrement(CStateManager& mgr);
  void ProcessDamage(CStateManager& mgr, TUniqueId colActorId);
  float RecentDamage() const;
  void DecayDamageMemory(float dt);
  void AddDamageMemory(float damage);
  bool PlayerOnGrapplePoint(const CStateManager& mgr) const;

  SLdrSwampBossStage2Data mData;                             // 0x7c0
  rstl::single_ptr< CCollisionActorManager > mCollisionMgr;  // 0xa98
  CVector3f mHeadPos;                                        // 0xa9c, collision actor 5
  CVector3f mTailPos;                                        // 0xaa8, collision actor 4
  CVector3f mSplashPos;                                      // 0xab4, collision actor 6
  rstl::reserved_vector< CSegId, 4 > mGrowthLocators;        // 0xac0
  rstl::reserved_vector< rstl::optional_object< CModelData >, 4 > mGrowthModels; // 0xac8
  rstl::reserved_vector< float, 4 > mGrowthHealth;           // 0xc0c
  rstl::reserved_vector< float, 4 > mGrowthFlash;            // 0xc20
  rstl::vector< TUniqueId > mSwoopStarts;                    // 0xc38, IS00
  TUniqueId mSwoopNode;                                      // 0xc48
  float mHeadHealth;                                         // 0xc4c
  float mOvipositorHealth;                                   // 0xc50
  rstl::vector< TUniqueId > mPathNodes;                      // 0xc54, IS05
  TUniqueId mCenter;                                         // 0xc64, IS01
  TUniqueId mEdgeMarker;                                     // 0xc66, IS06
  CVector3f mReelDir;                                        // 0xc68
  // 0xc74: the retail's flags, with the byte's bit positions (the word at +0xc74 holds its
  // bits 0..7).
  bool mDashing : 1;            // 0xc74 bit 0 (the word's bit 0)
  bool mSwoopDone : 1;           // 0xc74 bit 1 (the word's bit 1)
  bool mSwoopDamage : 1;        // 0xc74 bit 2 (the word's bit 2)
  bool mSwoopWindow : 1;        // 0xc74 bit 3 (the word's bit 3)
  bool mSurfaced : 1;           // 0xc74 bit 4 (the word's bit 4)
  bool mGrowthDestroyed : 1;    // 0xc74 bit 5 (the word's bit 5)
  bool mGrowthsVulnerable : 1;  // 0xc74 bit 6 (the word's bit 6)
  bool mHeadMode : 1;           // 0xc74 bit 7 (the word's bit 7)
  // 0xc75: the retail's flags, with the byte's bit positions.
  bool mCircleDamaged : 1;      // 0xc75 bit 0 (the word's bit 8)
  bool mSwoopRepeat : 1;        // 0xc75 bit 1 (the word's bit 9)
  bool mFacePlayer : 1;         // 0xc75 bit 2 (the word's bit 10)
  bool mRotateSign : 1;         // 0xc75 bit 3 (the word's bit 11)
  bool mClampHealth : 1;        // 0xc75 bit 4 (the word's bit 12)
  bool mUnusedC75Bit5 : 1;      // 0xc75 bit 5 (the word's bit 13): never read or written
  bool mPlayerInWater : 1;      // 0xc75 bit 6 (the word's bit 14)
  bool mAttackReady : 1;        // 0xc75 bit 7 (the word's bit 15)
  // 0xc76: the retail's flags, with the byte's bit positions (the word at +0xc74 holds its
  // bits 16..23). The names say what the retail's bits do; see
  // docs/module-analysis/SwampBoss-stage2.md 7.11 and 7.19.
  bool mLightFlyer : 1;        // 0xc76 bit 0 (the word's bit 16): the chain flag - set while the
                               // boss is in the BecomeLightFlyer state (0x95CC, cleared 0x9734)
                               // and read by the anim event 0x1b's chain pick (0xA9D8) and by
                               // the egg lay (0xAA5C); the ctor clears it (0xC0E8)
  bool mMoveRequested : 1;     // 0xc76 bit 1 (the word's bit 17): a move was asked for (0x6128)
  bool mSwoopSide : 1;         // 0xc76 bit 2 (the word's bit 18): the swoop's side (0x83C8)
  bool mCircleDone : 1;        // 0xc76 bit 3 (the word's bit 19): the circle is done (0x7C90)
  bool mSwooping : 1;          // 0xc76 bit 4 (the word's bit 20): the boss is swooping (0x835C)
  bool mCircleCounting : 1;    // 0xc76 bit 5 (the word's bit 21): the circle is counting (0xAB9C)
  bool mUnusedC76Bit6 : 1;     // 0xc76 bit 6 (the word's bit 22): only the ctor clears it (0xC0AC)
  bool mSwoopSelected : 1;     // 0xc76 bit 7 (the word's bit 23): the Lurk picked the swoop
                               // (set 0x9B14, cleared 0x9BCC, read 0x9ADC)
  // 0xc77: the retail's eight flags, with the bit numbers of the word at +0xc74 (the byte's
  // bit 0 is the word's bit 24). The names say what the retail's bits do; see
  // docs/module-analysis/SwampBoss-stage2.md 7.11.
  bool mUnusedC77Bit0 : 1;       // 0xc77 bit 0 (the word's bit 24): never read or written
  bool mReelEffect : 1;          // 0xc77 bit 1 (the word's bit 25): a growth was hit (ProcessDamage)
  bool mTargetEffectsOn : 1;     // 0xc77 bit 2 (the word's bit 26): the growth target effects are on (Reel)
  bool mDarkFlyerForm : 1;       // 0xc77 bit 3 (the word's bit 27): the boss is a dark flyer (BecomeDarkFlyer)
  bool mBlowing : 1;             // 0xc77 bit 4 (the word's bit 28): the player is being blown (Blow)
  bool mBlowWindowOpen : 1;      // 0xc77 bit 5 (the word's bit 29): the blow window is open (anim event 0x2a)
  bool mWindEffectPending : 1;   // 0xc77 bit 6 (the word's bit 30): the wind effect is waiting (anim event -> Blow)
  bool mBubbleTelegraphed : 1;   // 0xc77 bit 7 (the word's bit 31): one-shot for anim event 7 (fn_79_A838)
  int mAdditiveAnim;                                          // 0xc78
  float mAdditiveWeight;                                      // 0xc7c
  float mAdditiveRate;                                        // 0xc80
  // Random per-frame jitter; written by BecomeDarkFlyer (fn_79_91A4) and read by the additive
  // update and GetIngSnatchingNormal (fn_79_5E3C).
  float mAdditiveJitterX;                                     // 0xc84
  float mAdditiveJitterY;                                     // 0xc88
  int mChainDepth;                                            // 0xc8c
  float mGrowthVulnerableTime;                                // 0xc90
  int mPhase;                                                 // 0xc94
  int mAttackIndex;                                           // 0xc98
  int mAttackType;                                            // 0xc9c
  float mAttackDelay;                                         // 0xca0
  TUniqueId mLightChain;                                      // 0xca4, IS02
  CProjectileInfo mSpitProjectile;                            // 0xca8
  rstl::vector< TUniqueId > mPlatformsA;                      // 0xcd0, IS03
  rstl::vector< TUniqueId > mPlatformsB;                      // 0xce0, IS04
  TUniqueId mSpitNode;                                        // 0xcf0
  int mBarrageCount;                                          // 0xcf4
  float mNodeSpacing;                                         // 0xcf8
  TLockedToken< CGenDescription > mSplashGen;                 // 0xcfc
  TLockedToken< CGenDescription > mWingDamageGen;             // 0xd08
  TLockedToken< CGenDescription > mBlowGen;                   // 0xd14
  CVector3f mLastHeadSplash;                                  // 0xd20
  CVector3f mLastTailSplash;                                  // 0xd2c
  float mWaterZ;                                              // 0xd38
  TUniqueId mWater;                                           // 0xd3c, IS12
  CDamageInfo mSwoopDamageInfo;                               // 0xd40
  float mSwoopTimer;                                          // 0xd5c
  CVector3f mLevelTarget;                                     // 0xd60
  float mCollisionActorHP;                                    // 0xd6c
  float mEdgeDistance;                                        // 0xd74
  int mFlyersLeft;                                            // 0xd78
  float mHeadBlend;                                           // 0xd7c
  float mGrowthDeadTime;                                      // 0xd80
  int mSwoopCount;                                            // 0xd84
  float mCircleDamage;                                        // 0xd88
  float mGrappleTime;                                         // 0xd8c
  float mDashTimer;                                           // 0xd90
  rstl::list< SDamageMemory > mDamageMemory;                  // 0xd94
  rstl::vector< TUniqueId > mChainANodes;                     // 0xdac
  rstl::vector< CVector3f > mChainAPositions;                 // 0xdbc
  float mInterceptTime;                                       // 0xdcc
  TUniqueId mDarkChain;                                       // 0xdd0, IS07
  rstl::vector< TUniqueId > mChainBNodes;                     // 0xdd4
  rstl::vector< CVector3f > mChainBPositions;                 // 0xde4
  CVector3f mSwoopTarget;                                     // 0xdf4
  int mCircleCount;                                           // 0xe00
  rstl::optional_object< TLockedToken< CScannableObjectInfo > > mScanLight; // 0xe04
  rstl::optional_object< TLockedToken< CScannableObjectInfo > > mScanDark;  // 0xe08
  CVector3f mMoveDir;                                         // 0xe0c
  CVector3f mMoveTarget;                                      // 0xe18
  float mMoveSpeed;                                           // 0xe24
  float mFlashTime;                                           // 0xe28
  CDamageVulnerability mLightVulnerability;                   // 0xe2c
  CDamageVulnerability mDarkVulnerability;                    // 0xe5c
  rstl::single_ptr< CElementGen > mBubbleGen;                 // 0xe90
  rstl::optional_object< TLockedToken< CGenDescription > > mSpitVisorGen; // 0xe94
  CMissileRepeller mRepeller;                                 // 0xea4
  CDamageInfo mBlowDamageInfo;                                // 0xee4
  TUniqueId mWindNode;                                        // 0xf00
  CSfxHandle mWaterSfx;                                       // 0xf08
  float mDropFlyerCooldown;                                   // 0xf0c
  float mPendingFlinch;                                       // 0xf10
  rstl::reserved_vector< bool, 4 > mGrowthBroken;             // 0xf14
  TUniqueId mLastTouched;                                     // 0xf1c
};

#endif // _CSWAMPBOSSSTAGE2
