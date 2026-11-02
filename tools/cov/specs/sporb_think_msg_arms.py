# sporb5: one-call Think/AcceptScriptMsg scenarios. Use force_ret.py and the coverage build.
# Hall: --con tools/drivers/sporb4_hall.con --fast --frames 1800
# Guardian: SPORB5_GUARDIAN=1, 11_swamp_bloggtank_dark, --con tools/cov/specs/sporb_pbg.con
import gdb
import os

CLASS = 'CSporbBase'
guardian = os.environ.get('SPORB5_GUARDIAN') == '1'
gdb.set_convenience_variable('sp5_nan', gdb.Value(float('nan')))
for name, value in [('top', '(CSporbTop*)0'), ('proj', '(CSporbProjectile*)0'),
                    ('msg', '(CScriptMsg*)0'), ('stage', '(CPowerBombGuardianStageData*)0'),
                    ('trace', '0'), ('flinch_base', '0'), ('base_anim', '(CAnimData*)0')]:
    gdb.execute('set $sp5_%s = %s' % (name, value))


def log(text):
    with open(os.path.join(os.environ['COV_OUT'], 'forced.log'), 'a') as out:
        out.write(text + '\n')


labels = []


class Check(gdb.Function):
    def __init__(self):
        super().__init__('sp5_check')

    def invoke(self, index, passed):
        log('sporb5 assertion #%d %s: %s' % (int(index), labels[int(index)],
                                            'PASS' if int(passed) else 'FAIL'))
        if not int(passed):
            raise gdb.GdbError('Sporb differs from retail')
        return 1


Check()


class VectorEqual(gdb.Function):
    def __init__(self):
        super().__init__('sp5_vector_equal')

    def invoke(self, a, b):
        return int(all(abs(float(a['m' + axis]) - float(b['m' + axis])) < 1.e-4
                       for axis in 'XYZ'))


VectorEqual()
gdb.execute('set $sp5_ray = -1')
gdb.execute('set $sp5_predict_hits = 0')
gdb.execute('set $sp5_count_predict = 0')


class RayReturn(gdb.FinishBreakpoint):
    def __init__(self):
        super().__init__(gdb.newest_frame(), internal=True)
        self.output = int(gdb.parse_and_eval('$rdi'))

    def stop(self):
        out = '((CRayCastResult*)%d)' % self.output
        gdb.execute('set %s->mValid = $sp5_ray' % out)
        for axis, offset in zip('XYZ', (1, 2, 3)):
            gdb.execute('set %s->mPoint.m%s = $sp5_aim.m%s + %d.f' % (out, axis, axis, offset))
        return False


class RayEntry(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval('$sp5_ray')) >= 0:
            RayReturn()
        return False


class PredictEntry(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval('$sp5_count_predict')):
            gdb.execute('set $sp5_predict_hits = $sp5_predict_hits + 1')
        return False


class FlinchTrace(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval('$sp5_trace')) and int(gdb.parse_and_eval('$rcx')) == 1:
            gdb.execute('set $sp5_flinch_base = ((CAnimData*)$rdi == $sp5_base_anim)')
        return False


for line in open(os.environ['COV_NM']):
    raw, _, demangled = line.partition('\t')
    if len(raw.split()) != 3 or raw.split()[1] not in 'tTW':
        continue
    if demangled.startswith('CAnimData::AddAdditiveAnimation(') and '.cold' not in demangled:
        FlinchTrace('*' + raw.split()[2], internal=True)
    if demangled.startswith('CStateManager::RayWorldIntersection(TUniqueId&'):
        RayEntry('*' + raw.split()[2], internal=True)
    if demangled.startswith('CSporbBase::PredictAimOffset(') and '.cold' not in demangled:
        PredictEntry('*' + raw.split()[2], internal=True)

objlist = '((rstl::auto_ptr<CObjectList>*)$mgr->mObjectLists.mData)[0].mItem->mObjects'
children = [('$sp5_top', '(CSporbTop*)' + objlist + '[$this->mTopId.value & 2047].mEntity')]
if not guardian:
    children += [('$sp5_proj', '(CSporbProjectile*)' + objlist +
                  '[$this->mProjectileId.value & 2047].mEntity')]
else:
    children += [('$sp5_stage', '$this->mStages.mItems[0].mPtr')]

saved = ['$this->' + x for x in (
    'mState mShotsInBurst mShotsFired mShooting mGrabberState mAimLocked mTopHit mTopBlend '
    'mAttacksSinceDoubleShot mAttacksPerDoubleShot mTimeSinceDetection x8f8_ x860_ '
    'mForwardLean mBackLean mLeftLean mRightLean mSavedForwardLean mSavedBackLean '
    'mSavedLeftLean mSavedRightLean mShotTimer mTimeBetweenShots mTimeBetweenAttacks '
    'mReadyToFire x88a_ x85c_ x88c_ x88b_ mGrabFinished mHoldTimer mAttachTimer '
    'mGrabberInSpeed mGrabberOutSpeed mMinTimeBetweenShots mMaxTimeBetweenShots '
    'mMinTimeBetweenAttacks mMaxTimeBetweenAttacks mMinShots mMaxShots mStageIndex '
    'mAlerted mActive mTimerScale mTimerScaleMaxed mWaypointIds.mCount').split()]
saved += ['$this->mHealthInfo.mHealth', '$mgr->mRandom.mSeed',
          '$P->mCanStartUnmorphTransition',
          '$sp5_top->mImmune', '$sp5_top->mHitByPlayerProjectile',
          '$sp5_top->mAlive', '$sp5_top->mState', '$sp5_top->mBodyController.mPtr->mFrozen']
saved += [obj + '->' + member + '.m' + axis
          for obj, member in [('$this', 'mAimTarget'), ('$this', 'mTopAttachPosition'),
                              ('$this', 'mProjectilePosition'), ('$this', 'mPowerBombAimPosition'),
                              ('$sp5_top', 'mOrbitPosition'), ('$P', 'mVelocity'),
                              ('$P', 'mMomentum'), ('$P', 'mForce'), ('$P', 'mImpulse')]
          for axis in 'XYZ']
saved += ['$this->mBodyController.mPtr->' + field for field in
          ('mFrozen mHasBeenFrozen mIntoFreezeDur mFrozenDur mBreakoutDur mTimeFrozen mTimeScale').split()]
if not guardian:
    saved += ['$sp5_proj->' + field for field in ('mState mSolidPhase mPassable').split()]
    saved += ['$sp5_proj->mBodyController.mPtr->' + field for field in
              ('mFrozen mHasBeenFrozen mIntoFreezeDur mFrozenDur mBreakoutDur mTimeFrozen mTimeScale').split()]

scenarios = []


def case(label, writes=(), check='1', method='Think', message=None, expression=None):
    inputs = children + [(x, x) for x in saved]
    inputs += [('$this->mState', '2'), ('$this->mShooting', '0'), ('$this->mGrabberState', '4'),
               ('$this->mTimerScaleMaxed', '1'), ('$this->mAimLocked', '1'),
               ('$this->mTopBlend', '1.f'), ('$sp5_top->mAlive', '1'),
               ('$sp5_top->mHitByPlayerProjectile', '0')]
    if method == 'Think':
        call = '$this->Think(0.125f, *$mgr)'
        stubs = [('CPatterned::Think(', '0')]
    elif method == 'vector':
        call, stubs = expression, []
    else:
        inputs += [('$sp5_msg->m_msg', message), ('$sp5_msg->mSenderId.value', '65535'),
                   ('$sp5_msg->m_originator.value', '65535'),
                   ('$sp5_msg->m_id.value', '$this->mUniqueId.value'),
                   ('$sp5_msg->m_state', 'kSS_InvalidState')]
        call = '$this->AcceptScriptMsg(*$mgr, *$sp5_msg)'
        stubs = [('CPatterned::AcceptScriptMsg(', '0')]
    n = len(scenarios)
    scenarios.append((inputs + list(writes),
                      '(%s, $sp5_check(%d, %s))' % (call, n, check), stubs))
    labels.append(label)


case('AFD0 inactive', [('((CEntity*)$this)->mActive', '0'), ('$this->mTopBlend', '.5f')],
     '$this->mTopBlend == .5f')
case('B084 inside detection', [('$this->mDetectionDistance', '100000.f')],
     '$this->mTimeSinceDetection == 0.f')
case('B0BC outside attack, timer below leash', [('$this->mDetectionDistance', '0.f'),
     ('$this->mMaxAttackDistance', '0.f'), ('$this->mTimeSinceDetection', '0.f'),
     ('$this->mPlayerLeashTime', '10.f')], '$this->mTimeSinceDetection == .125f')
case('B0BC NaN timer stays unordered', [('$this->mDetectionDistance', '0.f'),
     ('$this->mMaxAttackDistance', '0.f'), ('$this->mTimeSinceDetection', '$sp5_nan')],
     '$this->mTimeSinceDetection != $this->mTimeSinceDetection')
case('B154 missing top', [('$this->mTopId.value', '65535')])
case('B174 wrong-type top', [('$this->mTopId.value', '$P->mUniqueId.value')])
for blend in ('.5f', '1.5f', '$sp5_nan'):
    expected = '$this->mTopBlend != $this->mTopBlend' if blend == '$sp5_nan' else \
               '$this->mTopBlend == ' + ('.625f' if blend == '.5f' else '1.f')
    case('B3A4 blend ' + blend, [('$this->mTopBlend', blend)], expected)
for state in (6, 7):
    case('B198/B1A0 dead top state %d' % state,
         [('$sp5_top->mAlive', '0'), ('$this->mState', str(state))])
for state in range(12):
    case('B5F0 state %d' % state, [('$this->mState', str(state)), ('$this->x860_', '0.f')])
case('B65C NaN windup waits', [('$this->mState', '3'), ('$this->x860_', '$sp5_nan')],
     '$this->x860_ != $this->x860_')
if not guardian:
    for blend in ('1.f', '.5f', '$sp5_nan'):
        case('B2B0 base flinch, blend ' + blend,
             [('$sp5_top->mHitByPlayerProjectile', '1'), ('$sp5_top->mImmune', '0'),
              ('$this->mTopBlend', blend), ('$sp5_trace', '1'), ('$sp5_flinch_base', '0'),
              ('$sp5_base_anim', '$this->mModelData.mPtr->mAnimData.mItem')],
             '$sp5_flinch_base && $this->mTopBlend == 0.f && $this->mTopHit')
    for frozen in (0, 1):
        for projectile in ('$this->mProjectileId.value', '65535'):
            case('B41C freeze relay base=%d projectile=%s' % (frozen, projectile),
                 [('$sp5_top->mBodyController.mPtr->mFrozen', '1'),
                  ('$this->mBodyController.mPtr->mFrozen', str(frozen)),
                  ('$this->mProjectileId.value', projectile)])
    for caught in (0, 1):
        for boost in (0, 1):
            case('B564/B57C passable previous=%d boost=%d' % (caught, boost),
                 [('$sp5_proj->mPassable', '1'), ('$this->x8f8_', str(caught)),
                  ('$P->mMorphBall.mPtr->mBallState', str(boost)),
                  ('$P->mMorphBall.mPtr->mBoostState', str(boost))], '$this->x8f8_')
    for state in (0, 1):
        case('B5C0/B5C8 grabber state %d' % state, [('$this->mGrabberState', str(state))])
    case('B514 missing projectile', [('$this->mProjectileId.value', '65535')])
    case('B534 wrong-type projectile', [('$this->mProjectileId.value', '$this->mTopId.value')])
else:
    for stage_count, stage, since, every in ((0, 0, 0, 1), (4, 4, 1, 1), (4, 0, 0, 1),
                                            (4, 0, 1, 1)):
        case('B0DC/B67C/B68C stages=%d index=%d counter=%d/%d' %
             (stage_count, stage, since, every),
             [('$this->mState', '3'), ('$this->x860_', '10.f'),
              ('$this->mStages.mCount', str(stage_count)), ('$this->mStageIndex', str(stage)),
              ('$this->mAttacksSinceDoubleShot', str(since)),
              ('$this->mAttacksPerDoubleShot', str(every))])
    for minimum, maximum, answer in ((2, 2, 2), (2, 5, 4), (4, 3, 202)):
        # Seed=1 gives Next()=16838. Reversed bounds wrap to 255, then modulo 256.
        case('B6C8 double-shot range %d/%d' % (minimum, maximum),
             [('$this->mState', '3'), ('$this->x860_', '10.f'), ('$this->mStageIndex', '0'),
              ('$this->mAttacksSinceDoubleShot', '1'), ('$this->mAttacksPerDoubleShot', '1'),
              ('$sp5_stage->mMinAttacksPerDoubleShot', str(minimum)),
              ('$sp5_stage->mMaxAttacksPerDoubleShot', str(maximum)), ('$mgr->mRandom.mSeed', '1')],
             '$this->mAttacksSinceDoubleShot == 0 && $this->mAttacksPerDoubleShot == %d' % answer)

# Allocate a plain message in the inferior; no production test hook or constructor changes.
scenarios.append(([], '($sp5_msg=(CScriptMsg*)calloc(1,sizeof(CScriptMsg)), 1)'))
labels.append('message scratch allocation')
for message in ('kSM_Create', 'kSM_Increment', 'kSM_Decrement', 'kSM_Alert', 'kSM_Reset',
                'kSM_Start', 'kSM_Stop', 'kSM_SetToMax', 'kSM_SetToZero', 'kSM_None'):
    case(message, method='AcceptScriptMsg', message=message)
case('BBF0 AreaLoaded unknown Approach target / absent children',
     [('$this->mConnections.mCount', '1'),
      ('$this->mConnections.mItems[0].state', 'kSS_Approach'),
      ('$this->mConnections.mItems[0].objId.value', '0'),
      ('$this->mTopId.value', '65535'), ('$this->mProjectileId.value', '65535')],
     method='AcceptScriptMsg', message='kSM_AreaLoaded')
case('BC40 empty connections', [('$this->mConnections.mCount', '0')],
     method='AcceptScriptMsg', message='kSM_AreaLoaded')
if guardian:
    for minimum, maximum, answer in ((2, 2, 2), (2, 5, 4), (4, 3, 202)):
        case('BDB4 Create double-shot range %d/%d' % (minimum, maximum),
             [('$this->mStageIndex', '0'), ('$this->mTendrilGen.mPtr', '0'),
              ('$sp5_stage->mMinAttacksPerDoubleShot', str(minimum)),
              ('$sp5_stage->mMaxAttacksPerDoubleShot', str(maximum)), ('$mgr->mRandom.mSeed', '1')],
             '$this->mAttacksPerDoubleShot == %d' % answer,
             method='AcceptScriptMsg', message='kSM_Create')
    for count, index in ((0, 0), (4, 4)):
        case('BD84/BD90 Create no valid stage', [('$this->mStages.mCount', str(count)),
             ('$this->mStageIndex', str(index)), ('$this->mTendrilGen.mPtr', '0')],
             method='AcceptScriptMsg', message='kSM_Create')
    for index, answer in ((0, '.75f'), (3, '0.f'), (4, '1073741824.f'), (255, '1.f')):
        case('BF0C Decrement stage %d unsigned health' % index,
             [('$this->mStageIndex', str(index)), ('$this->mHealthInfo.mInitialHealth', '1.f')],
             '$this->mHealthInfo.mHealth == ' + answer,
             method='AcceptScriptMsg', message='kSM_Decrement')

# Direct target/prediction calls exercise the same live objects, including each morph selector.
zero = '$this->mAttackAimOffset'
zero_inputs = [('$this->mAttackAimOffset.m' + a, '0.f') for a in 'XYZ']
for spawned, morph in ((0, 0), (0, 1), (1, 1)):
    for track in (0, 1):
        writes = [('$this->mIsPowerBombGuardian', '0'),
                  ('$P->mSpawnedMorphBallState', str(spawned)), ('$P->mMorphBallState', str(morph)),
                  ('$sp5_count_predict', '1'), ('$sp5_predict_hits', '0')]
        case('5C3C ordinary target spawn=%d morph=%d track=%d' % (spawned, morph, track),
             writes + zero_inputs, '$sp5_predict_hits == 0 && $sp5_vector_equal($sp5_result,$sp5_aim)',
             method='vector', expression='($sp5_aim=$P->GetAimPosition(*$mgr,0.f), '
             '$sp5_result=$this->PickTargetPosition(*$mgr,%d))' % track)
for spawned, morph, boost, ray in ((0, 0, 0, -1), (1, 1, 0, -1), (0, 1, 1, -1),
                                    (0, 1, 0, 0), (0, 1, 0, 1)):
    writes = zero_inputs + [('$P->mSpawnedMorphBallState', str(spawned)),
              ('$P->mMorphBallState', str(morph)), ('$P->mMorphBall.mPtr->mBallState', str(boost)),
              ('$P->mMorphBall.mPtr->mBoostState', str(boost)),
              ('$this->mAimSpreadRadius', '0.f'), ('$this->mAimPredictionTimeScale', '0.f'),
              ('$sp5_ray', str(ray))]
    check = '$sp5_vector_equal($sp5_result,' + zero + ')' if ray != 1 else \
            '$sp5_result.mX == 1.f && $sp5_result.mY == 2.f && $sp5_result.mZ == 3.f'
    case('481C prediction spawn=%d morph=%d boost=%d ray=%d' % (spawned, morph, boost, ray),
         writes, check, method='vector', expression='($sp5_aim=$P->GetAimPosition(*$mgr,0.f), '
         '$sp5_result=$this->PredictAimOffset(20.f,*$mgr,$this->mPosition))')
if guardian:
    for count in (0, 1, 3):
        writes = [('$this->mWaypointIds.mCount', str(count))]
        for i in range(count):
            waypoint = '(CScriptWaypoint*)' + objlist + '[$this->mWaypointIds.mItems[%d].value & 2047].mEntity' % i
            writes += [('((CEntity*)(' + waypoint + '))->mActive', '1')]
        case('5B90/5B98 target waypoints=%d' % count, writes,
             '$sp5_vector_equal($sp5_result,$P->mPosition)' if count == 0 else '1',
             method='vector', expression='$sp5_result=$this->PickTargetPosition(*$mgr,0)')
    for stage_count, index in ((0, 0), (4, 4)):
        waypoint = '(CScriptWaypoint*)' + objlist + '[$this->mWaypointIds.mItems[0].value & 2047].mEntity'
        case('5AB0/5ABC no valid target stage',
             [('$this->mStages.mCount', str(stage_count)), ('$this->mStageIndex', str(index)),
              ('$this->mWaypointIds.mCount', '1'), ('((CEntity*)(' + waypoint + '))->mActive', '1')],
             '$sp5_vector_equal($sp5_result,(' + waypoint + ')->mPosition)', method='vector',
             expression='$sp5_result=$this->PickTargetPosition(*$mgr,0)')
    case('5A84 waypoint cast missing', [('$this->mWaypointIds.mCount', '1'),
         ('$this->mWaypointIds.mItems[0].value', '65535')],
         '$sp5_vector_equal($sp5_result,$P->mPosition)', method='vector',
         expression='$sp5_result=$this->PickTargetPosition(*$mgr,0)')

case('B198 dead top enters FakeDeath', [('$sp5_top->mAlive', '0')],
     '$this->mState == 6')
scenarios.append(([], '(free($sp5_msg), 1)'))
labels.append('message scratch free')

CALLS = [('sporb5 think/msg', 'CSporbBase::Think(',
          '((CSporbBase*)$rdi)->mTopId.value != 65535 && ((CSporbBase*)$rdi)->mState == 2 '
          '&& ((CSporbBase*)$rdi)->mIsPowerBombGuardian == %d' % guardian,
          scenarios, '; '.join('%d=%s' % item for item in enumerate(labels)))]
SPEC = []
