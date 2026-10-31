# Save-state entity schema (generated)

Written by `tools/gen_entity_schema.py`; do not edit. The table itself is
`platform/generated/port_entity_schema.inc`, its types `platform/include/port_entity_schema.h`.

243 classes (CEntity and every subclass a header defines), 6282 own fields; 4818 of them plain data (`statedump` writes those under `fields` when the object's `kTagFields` block saves them, else under `fields_other`).

| kind | fields |
|---|---|
| bool | 151 |
| int | 1037 |
| float | 1787 |
| bitfield | 1001 |
| math | 484 |
| uid | 358 |
| pod | 373 |
| pointer | 61 |
| smart_ptr | 399 |
| container | 200 |
| state_machine | 7 |
| body_controller | 1 |
| anim_data | 35 |
| path_search | 26 |
| other | 362 |

## What a save keeps (`kTagFields`)

An enemy gets one `kTagFields` block in the room record, and so do the pickups and debris a
generator made: `platform/port_savestate_fields.cpp` writes it for the objects
`EntityFieldsSaved` names, and applies it in passes - the plain fields, the AI and body
codecs, the animation, the path search, then the plain fields again over what entering the
saved states changed. A class whose fingerprint changed since the save is skipped, not
guessed. Plain kinds are written as values; `pointer`, `smart_ptr`, `container` and
`other` fields are never written; four kinds have a codec:

- `state_machine`: the active state's name, time and delay, and what re-enters it - for an
  FSM1 state its index, drawn randoms and code-trigger flag; for a `CGenericFSM2State` the
  whole active path: each level's own clock, the sub-machine names down the path, and the
  active node's index in its machine (two states can share a name). A restore walks that
  path back down, and a state saved before the path was written still loads, its extra
  bytes simply absent. Only the patterned shapes are taken (`CodecTypeKnown`): the
  player's body and gun machines and the grapple arm's are not.
- `body_controller`: state, additives, locomotion, the timers and the queued commands, and
  (policy 3) the `TUniqueId` a command carries, by identity.
- `anim_data`: the current animation, loop/animating, speed and phase.
- `path_search`: the waypoints the search found, the current one and its result.

`SavedField` drops fields on purpose too: `const` ones, the port's own `mPort*`/`xPort*`
members, anything named `...Loaded`/`...Loading` (asset streaming is this run's), every
field `CEntity` itself declares, all of `CActor`'s except a named few (its model, draw and
target flags, `mMuted`, `mCallTouch`), all of `CPhysicsActor`'s except its motion and force
members, and `CAi::mHealthInfo` (the health block's). `MP_SAVESTATE_FIELDS=0` turns the
whole block off - no capture, no apply - and `statedump` then reports every field under
`fields_other`; any other value, or unset, leaves it on.

## `other`: types with no codec (by field count)

These fields the save state leaves alone. `CSfxHandle` is the common case and will never
get one: it names a runtime sound, so copying it restores nothing (`HANDLES` in
`tools/gen_entity_schema.py`). The rest would each need a codec of its own; the `pointer`,
`smart_ptr` and `container` rows above are skipped the same way.

- 76 `CSfxHandle`
- 67 `CDamageVulnerability`
- 35 `CProjectileInfo`
- 28 `CMayaSpline`
- 9 `CMotionSpline`
- 9 `CBoneTracking`
- 9 `CRELFileToken`
- 7 `CSurfaceAlignmentHelper`
- 6 `CCollidableSphere`
- 4 `CSfxHandle[2]`
- 4 `CCameraShakerData`
- 3 `CCameraColliderGroup`
- 3 `CBurstFire`
- 3 `CCollidableAABox`
- 3 `CPathFindPointSearch`
- 3 `CDarkWorldInfo`
- 2 `CBoostBallGuardianData`
- 2 `CActorParameters`
- 2 `CGameHint::SCallback`
- 2 `PortTickPair<CVector3f>`
- 2 `CLumite::SShotAttack`
- 2 `rstl::optional_object<CModelData>[8]`
- 2 `CScriptCameraSpline`
- 2 `TReservedAverage<float, 8>`
- 2 `CSpline`
- 2 `CGuiTextSupport`
- 1 `CFoldySurface`
- 1 `CCommandoPirateData`
- 1 `CDarkSamusDive`
- 1 `SReflectChance[10]`
- 1 `CDarkTrooper::CRagDollHolder`
- 1 `CDarkTrooper::CSmallShotAttack`
- 1 `CDarkTrooper::CMissileAttack`
- 1 `CSfxHandle[4]`
- 1 `CElitePirateData`
- 1 `SPositionHistory`
- 1 `CEmperorIngStage1JointVector`
- 1 `CEmperorIngStage1Joint`
- 1 `CBasicSwarmData`
- 1 `CEnergyProjectile::CCollisionCooldowns`
- 1 `CImpactVisorEffect`
- 1 `CProjectileWeapon`
- 1 `CGrenchler::SReflectInfo`
- 1 `CGrenchler::SBeamAttack`
- 1 `CGrenchler::SBurstAttack`
- 1 `CGrenchler::SGrappleEffect`
- 1 `CGrenchler::SEffectA`
- 1 `CGrenchler::SEffectB`
- 1 `CGrenchler::SEffectC`
- 1 `CGrenchler::SDamageEffect`
- 1 `CGrenchler::SEffectD`
- 1 `CMarkerGrid`
- 1 `CIngData`
- 1 `CIngBoostBallGuardianData`
- 1 `CIngMiniPortalInfo`
- 1 `SIngSnatchingSwarmData`
- 1 `CLumite::SHopPlan`
- 1 `CLumite::SSunlightEffect`
- 1 `CMediumIngData`
- 1 `CMetroidData`
- 1 `CMinorIngData`
- 1 `CAiKnockBackMgr`
- 1 `PortTickPair<CPlasmaProjectile::SPortBeam>`
- 1 `CPlayerEnergyDrain`
- 1 `TReservedAverage<CVector3f, 10>`
- 1 `CPlayerKnockBackMgr`
- 1 `TReservedAverage<float, 20>`
- 1 `CControlMapper`
- 1 `CPlayerBodyStateCmdMgr`
- 1 `CPlayerBodyController::SLocomotionState`
- 1 `void (CPlayerGun::*)(const CStateManager &, const CVector3f &, bool, const CTransform4f &, const CTransform4f &, const CModelFlags &, const CModelFlags &)`
- 1 `CDependencyGroupToken`
- 1 `CActorLights`
- 1 `rstl::optional_object<SLdrSandwormStruct>[5]`
- 1 `SSandwormAttackHistory`
- 1 `SSandwormSpit`
- 1 `SSandworm1670`
- 1 `SSandwormPincers`
- 1 `SSandwormSfx`
- 1 `SSandwormBombs`
- 1 `SSandwormGlow`
- 1 `CCameraOverrideInfo`
- 1 `CBarrierChunkGrid`
- 1 `CScriptDynamicLight::SDescription`
- 1 `TFunctor3<CStateManager &, CScriptGuiWidget *, int>`
- 1 `CPickupGeneratorRuleEvaluator`
- 1 `CScriptPlayerProxy::CListener`
- 1 `CScriptRubiksPuzzle::CPressQueue`
- 1 `SLdrSequenceConnections`
- 1 `TAverage<float>`
- 1 `CSpindleCameraParameters`
- 1 `CVisorFlare`
- 1 `CShriekerData`
- 1 `CSplinter::SSurfaceAlignment`
- 1 `CSplinter::SStateMachine2`
- 1 `CSplinter::SParticleData`
- 1 `CSplitterCommandModuleData`
- 1 `CSplitterMainChassisData`
- 1 `CMissileRepeller`

## `pod`: pointer-free structs and arrays, copied as bytes

Scalar bytes are copied; embedded `TUniqueId` members have generated accessors and are
saved by stable identity, never restored as raw runtime IDs. This includes line-of-sight
and visual trackers, waypoint/path navigation, health damage owners, sac/zone targets,
camera interpolation, melee contacts, script messages and fixed ID arrays.

Field policy 2 stores self/player/editor identities after the POD bytes (whose IDs are
scrubbed). Unnamed dynamic references retain only a valid rebuilt reference. Policy 1
states remain readable, but their embedded raw IDs are replaced with rebuilt references.
Field policy 3 stores a body-state command's `TUniqueId` member (the wall-hang waypoint)
the same way, in the command payload's own four bytes; older policies stay readable.

`CDamageInfo` 63, `CSegId` 63, `CColor` 43, `CHealthInfo` 24, `CLineOfSightTracker` 11, `SLdrAudioPlaybackParms` 8, `CCameraSpring` 7, `CSteeringBehaviors` 5, `CModelFlags` 4, `CMaterialList` 4, `int[4]` 4, `SLdrPlasmaBeamInfo` 4, `CBouncyGrenadeData` 3, `CChozoGhost::CBehaveChance` 3, `CShockWaveInfo` 3, `CIngSpotPathFindNavigation` 3, `CVector3f[3]` 3, `CAnimRes` 3, `TAreaId` 2, `TEditorId` 2, `CMaterialFilter` 2, `CRandom16` 2, `CVisualKnowledge` 2, `TUniqueId[2]` 2, `u16[4]` 2, `float[4]` 2, `SLdrDigitalGuardianHeadData` 2, `CAnimationParameters` 2, `CVector3f[4]` 2, `CGunDrawBlockSet` 2, `uchar[3]` 2, `CGrappleParameters` 2, `CRepeatState` 2, `CSafeZoneFog` 2, `CScriptMsg` 2, `bool[4]` 2, `SLdrAIMannedTurretData` 1, `SLdrDarkCommandoData` 1, `SLdrDarkCommandoEMPData` 1, `CDarkSamusData` 1, `uint[3]` 1, `CDarkSamusDash` 1, `CDarkSamus::SAttackWeights` 1, `CDarkSamusMeleeContact` 1, `CWeaponMode` 1, `ushort[18]` 1, `SLdrDSStageInfo` 1, `CDarkTrooper::CMeleeAttack` 1, `CSegId[4]` 1, `CDigitalGuardianData` 1, `CSegId[2]` 1, `CTransform4f[3]` 1, `SLdrAnimationSet` 1, `SLdrEmperorIngStage3Stage[4]` 1, `CFlyingPirate::CFlyingPirateData` 1, `CGameCamera::SFovInterpolation` 1, `CLight` 1, `CGrenchler::SBiteAttack` 1, `CGrenchler::SChargeAttack` 1, `CGrenchler::SVectorTriple` 1, `CVector3f[60]` 1, `SLdrIngPuddleData` 1, `SLdrIngSpaceJumpGuardianData` 1, `SLdrIngSpiderballGuardianData` 1, `CBeamInfo` 1, `CLumite::SSurfaceTarget` 1, `int[2]` 1, `SLdrMysteryFlyerData` 1, `CAdvancementDeltas` 1, `SLdrIngPossessionData` 1, `CAnimationState` 1, `CWaypointNavigation` 1, `CPathFindNavigation` 1, `CMotionState` 1, `CPlayer::CVisorSteam` 1, `short[5]` 1, `CVector2i` 1, `CPlayerBodyController::SJumpState` 1, `CPlayerBodyController::SGrappleState` 1, `CPlayerBodyController::SDashState` 1, `CPlayerBodyController::SKnockBackState` 1, `CPlayerBodyController::SAdditiveAimState` 1, `CPlayerBodyController::SAdditiveFlinchState` 1, `CPlayerBodyController::SAdditiveReactionState` 1, `CPlayerGun::CGunMorph` 1, `CPlayerGun::CMotionState` 1, `CFidget` 1, `SPuddleSporeData` 1, `SLdrRezbitData` 1, `CRezbitEffectOptions` 1, `SLdrSandBossData` 1, `CSegId[8]` 1, `int[8]` 1, `u8[15]` 1, `float[13]` 1, `float[12]` 1, `SSandwormCharge` 1, `SSandwormToss` 1, `SSandwormMelee` 1, `SSandwormBombPhase` 1, `CVector3i` 1, `CHUDMemoParms` 1, `TLayerId` 1, `SLdrRubiksPuzzleData` 1, `uchar[8]` 1, `CScriptTeamAiMgr::CTeamAiData` 1, `CSpacePirateData` 1, `CIkChain` 1, `uchar[48]` 1, `CSplinter::SJumpData` 1, `CSplinter::SSacData` 1, `SLdrSwampBossStage1Data` 1, `SLdrShockWaveInfo` 1, `SLdrSwampBossStage2Data` 1, `CCollisionSurface` 1

## `smart_ptr` / `container` templates

`rstl::single_ptr` 195, `rstl::optional_object` 136, `rstl::reserved_vector` 98, `rstl::vector` 84, `rstl::auto_ptr` 20, `TLockedToken` 20, `TToken` 12, `rstl::list` 8, `rstl::basic_string` 8, `TCachedToken` 6, `rstl::ncrc_ptr` 5, `CToken` 5, `rstl::pair` 2

## Not in the table

- `CSpankWeedCollisionActor`: defined in `src/MetroidPrime/Enemies/CSpankWeed.cpp` only
- `CCommandoPirateEGrenade`: defined in `src/MetroidPrime/Enemies/PortCCommandoPirateCore.cpp` only
- `CGuardianProjectile`: defined in `src/MetroidPrime/Enemies/PortCDigitalGuardianHead.cpp` only
- `CSplitterBeamEffect`: defined in `src/MetroidPrime/Enemies/PortCSplitterCommandModule.cpp` only
