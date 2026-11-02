# sporb5 runtime continuation. force_ret.py, sporb4_hall.con (1800 frames) or
# SPORB5_GUARDIAN=1 with sporb_pbg.con (3200 frames). No production probes.
import gdb
import math
import os

CLASS = 'CSporbBase'
guardian = os.environ.get('SPORB5_GUARDIAN') == '1'
gdb.set_convenience_variable('sp5r_nan', gdb.Value(float('nan')))
for name, value in [('top', '(CSporbTop*)0'), ('proj', '(CSporbProjectile*)0'),
                    ('area', '(CGameArea*)0'), ('stage', '(CPowerBombGuardianStageData*)0'),
                    ('scratch', '(void*)0'), ('shoots', '0'), ('trace', '0')]:
    gdb.execute('set $sp5r_%s = %s' % (name, value))


def log(text):
    with open(os.path.join(os.environ['COV_OUT'], 'forced.log'), 'a') as out:
        out.write(text + '\n')


labels = []
scenarios = []


class Check(gdb.Function):
    def __init__(self):
        super().__init__('sp5r_check')

    def invoke(self, index, passed):
        log('sporb5 runtime #%d %s: %s' % (int(index), labels[int(index)],
                                          'PASS' if int(passed) else 'FAIL'))
        if not int(passed):
            raise gdb.GdbError('Sporb runtime differs from retail')
        return 1


class VectorCheck(gdb.Function):
    def __init__(self):
        super().__init__('sp5r_vector')

    def invoke(self, v, x, y, z):
        return int(all(abs(float(v['m' + a]) - float(w)) < 2.e-5
                       for a, w in zip('XYZ', (x, y, z))))


Check()
VectorCheck()


class ShootTrace(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval('$sp5r_trace')):
            gdb.execute('set $sp5r_shoots = $sp5r_shoots + 1')
        return False


for line in open(os.environ['COV_NM']):
    raw, _, demangled = line.partition('\t')
    if len(raw.split()) == 3 and raw.split()[1] in 'tTW' and \
            demangled.startswith('CSporbBase::ShootPowerBomb(') and '.cold' not in demangled:
        ShootTrace('*' + raw.split()[2], internal=True)

objlist = '((rstl::auto_ptr<CObjectList>*)$mgr->mObjectLists.mData)[0].mItem->mObjects'
children = [('$sp5r_top', '(CSporbTop*)' + objlist + '[$this->mTopId.value & 2047].mEntity'),
            ('$sp5r_area', '$mgr->mWorld.mPtr->mAreas.mItems[$this->mAreaId.value].mItem')]
if guardian:
    children += [('$sp5r_stage', '$this->mStages.mItems[0].mPtr')]
else:
    children += [('$sp5r_proj', '(CSporbProjectile*)' + objlist +
                  '[$this->mProjectileId.value & 2047].mEntity')]
saved = ['$this->' + field for field in (
    'mState mAnimationState.mState mReadyToFire x860_ mAimLocked mAttackType mGrabberState '
    'mAttacksSinceDoubleShot mPowerBombsFired mTendrilParticleCount mBodyController.mPtr->mLocomotionType '
    'mColor.mRgba mBodyController.mPtr->mFrozen mBodyController.mPtr->mTimeScale '
    'mBodyController.mPtr->mTimeFrozen mBodyController.mPtr->mHasBeenFrozen').split()]
saved += ['sGrabbingSporb.value', '$sp5r_top->mState', '$P->mCanStartUnmorphTransition',
          '$P->mMorphBall.mPtr->mBallState', '$P->mMorphBall.mPtr->mBoostState', '$mgr->mRandom.mSeed']
saved += [p + '.m' + a for p in ('$this->mAimTarget', '$P->mPosition') for a in 'XYZ']
if not guardian:
    saved += ['$sp5r_proj->mState', '$sp5r_proj->mPassable', '$sp5r_proj->mSolidPhase']


def case(label, call, writes=(), check='1', stubs=()):
    n = len(scenarios)
    inputs = children + [(x, x) for x in saved] + list(writes)
    scenarios.append((inputs, '(%s, $sp5r_check(%d,%s))' % (call, n, check), list(stubs)))
    labels.append(label)


scenarios.append(([], '$sp5r_scratch=calloc(1,4096)'))
labels.append('scratch allocation')

attack_inputs = [('$P->mSpawnedMorphBallState', '0'), ('$P->mMorphBallState', '1'),
                 ('$this->mMaxGrabRange', '100000.f'), ('$this->mMinGrabRange', '0.f'),
                 ('sGrabbingSporb.value', '65535')]
for msg in (0, 1, 2, 3):
    for missing in (False, True):
        writes = [('$this->mTopId.value', '65535'), ('$this->mProjectileId.value', '65535')] if missing else []
        case('AA88 Patrol msg=%d missing=%d' % (msg, missing),
             '$this->Patrol(*$mgr,(EStateMsg)%d,.125f)' % msg, writes,
             '$this->mState == 1' if msg == 0 else '1')
        case('A898 Attack msg=%d missing=%d' % (msg, missing),
             '$this->Attack(*$mgr,(EStateMsg)%d,.125f)' % msg, attack_inputs + writes,
             '$this->mState == 2 && $this->mReadyToFire && !$this->mAimLocked && '
             '$this->mPowerBombsFired == 0 && $this->mAttackType == %d' % (not guardian)
             if msg == 0 else '1')
for label, writes in [
    ('unmorphed', [('$P->mMorphBallState', '0')]),
    ('spawned', [('$P->mSpawnedMorphBallState', '1')]),
    ('outside', [('$this->mMaxGrabRange', '0.f')]),
    ('inside minimum', [('$this->mMinGrabRange', '100000.f')]),
    ('grab owned', [('sGrabbingSporb.value', '$this->mUniqueId.value')])]:
    case('A964-A9D4 ' + label, '$this->Attack(*$mgr,(EStateMsg)0,.125f)', attack_inputs + writes,
         '$this->mAttackType == 0')
if guardian:
    stage_inputs = [('$this->mStageIndex', '0'), ('$this->mShotsInBurst', '1'),
                    ('$sp5r_stage->mDoubleShotChance', '1.f')]
    for since, every, result in ((0, 3, 1), (3, 3, 3), (255, 3, 3), (255, 255, 255)):
        case('AA50 clamp before byte store %d/%d' % (since, every),
             '$this->Attack(*$mgr,(EStateMsg)0,.125f)', stage_inputs +
             [('$this->mAttacksSinceDoubleShot', str(since)), ('$this->mAttacksPerDoubleShot', str(every))],
             '$this->mAttacksSinceDoubleShot == %d' % result)
    for label, writes in [('empty', [('$this->mStages.mCount', '0')]),
                          ('bad stage', [('$this->mStageIndex', '4')]),
                          ('double burst', [('$this->mShotsInBurst', '2')]),
                          ('zero chance', [('$sp5r_stage->mDoubleShotChance', '0.f')]),
                          ('NaN chance', [('$sp5r_stage->mDoubleShotChance', '$sp5r_nan')])]:
        case('A9F4-AA44 ' + label, '$this->Attack(*$mgr,(EStateMsg)0,.125f)',
             stage_inputs + [('$this->mAttacksSinceDoubleShot', '0')] + writes,
             '$this->mAttacksSinceDoubleShot == 0')

ready = [('$this->mAlerted', '1'), ('sGrabbingSporb.value', '65535'),
         ('$this->mMaxAttackDistance', '100000.f'), ('$this->mMinAttackDistance', '0.f'),
         ('$sp5r_area->mPostConst.mPtr->mOcclusionState', '1'), ('$this->mTopHit', '0')]
for label, writes, result in [('visible/in range', [], 1),
    ('not loaded', [('$sp5r_area->mPhase', '0')], 0),
    ('occluded', [('$sp5r_area->mPostConst.mPtr->mOcclusionState', '0')], 0),
    ('unalerted', [('$this->mAlerted', '0')], 0),
    ('grab held', [('sGrabbingSporb.value', '$this->mUniqueId.value')], 0),
    ('out of range', [('$this->mMaxAttackDistance', '0.f')], 0),
    ('too close', [('$this->mMinAttackDistance', '100000.f')], 0),
    ('hit outside', [('$this->mMaxAttackDistance', '0.f'), ('$this->mTopHit', '1')], 1),
    ('no top in range', [('$this->mTopId.value', '65535')], 1),
    ('no top outside', [('$this->mTopId.value', '65535'), ('$this->mMaxAttackDistance', '0.f')], 0)]:
    case('915C ShouldAttack ' + label,
         '$sp5r_result=$this->ShouldAttack(*$mgr,*(CTriggerData*)$sp5r_scratch)', ready + writes,
         '$sp5r_result == %d' % result)

for attack, state in ((-1, 2), (1, 2), (0, 2), (0, 11)):
    case('55F4 sound attack=%d state=%d' % (attack, state),
         '$this->DoUserAnimEvent(*$mgr,*(CInt32POINode*)$sp5r_scratch,(EUserEventType)31,.125f)',
         [('$this->mAttackType', str(attack)), ('$this->mState', str(state))],
         stubs=[('CPatterned::DoUserAnimEvent(', '0')])
for event, state in ((8, 8), (8, 2), (99, 2)):
    case('55A4 event=%d state=%d' % (event, state),
         '$this->DoUserAnimEvent(*$mgr,*(CInt32POINode*)$sp5r_scratch,(EUserEventType)%d,.125f)' % event,
         [('$this->mState', str(state))], stubs=[('CPatterned::DoUserAnimEvent(', '0')])
for maxed, count in ((0, 0), (1, 0), (1, 1)):
    case('57CC projectile maxed=%d fired=%d' % (maxed, count),
         '$this->DoUserAnimEvent(*$mgr,*(CInt32POINode*)$sp5r_scratch,(EUserEventType)0,.125f)',
         [('$this->mTimerScaleMaxed', str(maxed)), ('$this->mPowerBombsFired', str(count)),
          ('$sp5r_trace', '1'), ('$sp5r_shoots', '0')],
         '$sp5r_shoots == %d && $this->mPowerBombsFired == %d' % (guardian, count + guardian),
         [('CPatterned::DoUserAnimEvent(', '0')])

case('5558 distinct lean axes', '$sp5r_v=$this->GetLeanDirection(1.f,2.f,3.f,7.f)',
     check='$sp5r_vector($sp5r_v,%.12f,%.12f,0.f)' % (4/math.sqrt(17), -1/math.sqrt(17)))
for label, writes in [('invalid', [('$this->mTopId.value', '65535')]),
                      ('wrong type', [('$this->mTopId.value', '$P->mUniqueId.value')]),
                      ('dead', [('$sp5r_top->mAlive', '0')])]:
    case('5388 ' + label, '$sp5r_v=$this->GetTopAttachPosition(*$mgr)', writes,
         '$sp5r_vector($sp5r_v,0.f,0.f,0.f)')
case('5388 aligned top attachment', '$sp5r_v=$this->GetTopAttachPosition(*$mgr)',
     [('$sp5r_top->mAlive', '1'), ('$this->mForwardLean', '2.f'), ('$this->mBackLean', '1.f'),
      ('$this->mLeftLean', '3.f'), ('$this->mRightLean', '3.f'),
      ('$this->mTopAttachPosition.mX', '0.f'), ('$this->mTopAttachPosition.mY', '1.f')],
     '$sp5r_vector($sp5r_v,$sp5r_top->mOrbitPosition.mX,$sp5r_top->mOrbitPosition.mY,'
     '$sp5r_top->mOrbitPosition.mZ)')
for id in ('65535', '$P->mUniqueId.value'):
    case('7000 missing projectile ' + id, '$this->AttachPlayer(*$mgr)',
         [('$this->mProjectileId.value', id)])
if not guardian:
    case('7000 attach to live projectile', '$this->AttachPlayer(*$mgr)')
    for spawned, morph, state, phase, radius, want in (
        (0, 0, 0, 0, '10000.f', 0), (1, 1, 0, 0, '10000.f', 0),
        (0, 1, 1, 0, '10000.f', 0), (0, 1, 0, 1, '10000.f', 1),
        (0, 1, 0, 0, '10000.f', 2), (0, 1, 0, 0, '0.f', 0),
        (0, 1, 0, 0, '$sp5r_nan', 0), (0, 1, 0, 2, '10000.f', 2),
        (0, 1, 0, 2, '0.f', 0), (0, 1, 0, 2, '$sp5r_nan', 2)):
        case('F084 morph=%d/%d state=%d phase=%d radius=%s' % (spawned,morph,state,phase,radius),
             '$sp5r_proj->Think(.125f,*$mgr)',
             [('$P->mSpawnedMorphBallState', str(spawned)), ('$P->mMorphBallState', str(morph)),
              ('$sp5r_proj->mState', str(state)), ('$sp5r_proj->mSolidPhase', str(phase)),
              ('$sp5r_proj->mBallEscapeRadius', radius)], '$sp5r_proj->mSolidPhase == %d' % want,
             [('CPatterned::Think(', '0')])

for msg, anim in ((1, 0), (1, 1), (2, 0), (3, 0)):
    case('9304 FakeDeath msg=%d animation=%d' % (msg, anim),
         '$this->FakeDeath(*$mgr,(EStateMsg)%d,.125f)' % msg,
         [('$this->mAnimationState.mState', str(anim)),
          ('$this->mBodyController.mPtr->mBodyStateInfo.mState', '0')])
# Delete requests are real and deferred: these are last, after all projectile users.
for mine, missing in ((False, True), (True, False)):
    case('9304 FakeDeath grabbed=%d missing=%d' % (mine, missing),
         '$this->FakeDeath(*$mgr,(EStateMsg)0,.125f)',
         [('sGrabbingSporb.value', '$this->mUniqueId.value' if mine else '65535'),
          ('$this->mColor.mA', '123')] + ([('$this->mProjectileId.value', '65535')] if missing else []),
         '$this->mState == 6 && sGrabbingSporb.value == 65535 && '
         '$this->mColor.mR == 0 && $this->mColor.mG == 0 && $this->mColor.mB == 0 && '
         '$this->mColor.mA == 123')
scenarios.append(([], '(free($sp5r_scratch),1)'))
labels.append('scratch free')
CALLS = [('sporb5 runtime', 'CSporbBase::Think(',
          '((CSporbBase*)$rdi)->mTopId.value != 65535 && ((CSporbBase*)$rdi)->mState == 2 '
          '&& ((CSporbBase*)$rdi)->mIsPowerBombGuardian == %d' % guardian,
          scenarios, '; '.join('%d=%s' % item for item in enumerate(labels)))]
SPEC = []
