#ifndef _CMYSTERYFLYER
#define _CMYSTERYFLYER

#include "types.h"

#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/PathFinding/CPathFindSearch.hpp"
#include "MetroidPrime/ScriptLoader/SLdrMysteryFlyer.hpp"
#include "MetroidPrime/Weapons/CProjectileInfo.hpp"

// Guessed class: a flying enemy that hovers around its patrol path and shoots projectiles.
class CMysteryFlyer : public CPatterned {
#ifdef TARGET_PC
  friend struct PortEntitySchema; // save-state field schema (platform/include/port_entity_schema.h)
#endif
public:
  CMysteryFlyer(TUniqueId uid, const rstl::string& name, const CEntityInfo& info,
                const CTransform4f& xf, const CModelData& modelData,
                const CActorParameters& actorParams, const CPatternedInfo& patternedInfo,
                const SLdrMysteryFlyerData& data);

  // CEntity
  ~CMysteryFlyer() override;
  void Think(float dt, CStateManager& mgr) override;
  void AcceptScriptMsg(CStateManager& mgr, const CScriptMsg& msg) override;

  // CAi
  // @ fn_45_0
  bool IsListening() const override { return true; }

  // CPatterned
  // @ fn_45_8
  CPathFindSearch* GetSearchPath() override { return &mPathFindSearch; }
  void DoUserAnimEvent(CStateManager& mgr, const CInt32POINode& node, EUserEventType type,
                       float dt) override;
  CProjectileInfo* ProjectileInfo() override;
  void SetupStateMachine(CStateManager& mgr) override;

  // CMysteryFlyer
  void Start(CStateManager& mgr, EStateMsg msg, float dt);
  void Generate(CStateManager& mgr, EStateMsg msg, float dt);
  void GoToPatrol(CStateManager& mgr, EStateMsg msg, float dt);
  void Patrol(CStateManager& mgr, EStateMsg msg, float dt);
  void FollowAttackPath(CStateManager& mgr, EStateMsg msg, float dt);
  void Hover(CStateManager& mgr, EStateMsg msg, float dt);
  void Approach(CStateManager& mgr, EStateMsg msg, float dt);
  void Attack(CStateManager& mgr, EStateMsg msg, float dt);
  void Dodge(CStateManager& mgr, EStateMsg msg, float dt);
  void Dead(CStateManager& mgr, EStateMsg msg, float dt);
  void ResetAttack(CStateManager& mgr, float dt);
  void CalcLineOfSight(CStateManager& mgr, float dt);
  bool StateOver(CStateManager& mgr, const CTriggerData& data) const;
  bool NearPatrol(CStateManager& mgr, const CTriggerData& data) const;
  bool InDetectionRange(CStateManager& mgr, const CTriggerData& data) const;
  bool AttackPathOver(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldAttack(CStateManager& mgr, const CTriggerData& data) const;
  bool Leash(CStateManager& mgr, const CTriggerData& data) const;
  bool ShouldApproach(CStateManager& mgr, const CTriggerData& data) const;
  bool PathOver(CStateManager& mgr, const CTriggerData& data) const;
  bool Stuck(CStateManager& mgr, const CTriggerData& data) const;
  bool LineOfSight(CStateManager& mgr, const CTriggerData& data) const;

private:
  void JoinTeam(CStateManager& mgr);                 // Guessed name
  void QuitTeam(CStateManager& mgr);                 // Guessed name
  void FaceTarget(CStateManager& mgr, float dt);     // Guessed name
  bool UpdateLineOfSight(CStateManager& mgr);        // Guessed name
  CVector3f CalculateSeparation(CStateManager& mgr); // Guessed name
  void UpdateMovement(CStateManager& mgr, float dt); // Guessed name
  void UpdateSteering(CStateManager& mgr, float dt); // Guessed name

  SLdrMysteryFlyerData mData;          // Guessed name
  uint mGenerateAnimId;                // Guessed name
  TUniqueId mPatrolWaypointId;         // Guessed name
  CProjectileInfo mShotProjectileInfo; // Guessed name
  bool mMoving : 1;                    // Guessed name; 0x814 0x80 (`rlwimi r0, r3, 7, 24, 24` at 0x00001B70)
  bool mHasLineOfSight : 1;            // Guessed name; 0x814 0x40 (`rlwimi` at 0x00000B40)
  bool mAlerted : 1;                   // Guessed name; 0x814 0x20
  bool mHasAttackPath : 1;             // Guessed name; 0x814 0x10
  CPathFindSearch mPathFindSearch;     // 0x818 (0x8 addi r3,r3,0x818)

  CSteeringBehaviors mSteering;        // Guessed name
  float mAttackInterval;               // Guessed name
  float mAttackTimer;                  // Guessed name
  CVector3f mMoveDirection;            // Guessed name
  float mCurrentSpeed;                 // Guessed name
  TUniqueId mTeamAiMgrId;              // Guessed name
  float mHoverHeight;                  // Guessed name
  CVector3f mStuckReferencePos;        // Guessed name
  float mStuckTimer;                   // Guessed name
  float mLineOfSightTimer;             // Guessed name
};
CHECK_SIZEOF(CMysteryFlyer, 0x940)

#endif // _CMYSTERYFLYER
