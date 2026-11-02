# sporb4: UpdateShooting (6ABC) and UpdateAim (6390), including NaNs and finite smoothing clamps.
# BUILD=$PWD/build-port-cov COV_GDB_SCRIPT=$PWD/tools/cov/force_ret.py tools/cov/force.sh \
#   tools/cov/specs/sporb_shoot_aim_arms.py <out> 3dfd2249,0e_swamp_hall \
#   --con tools/drivers/sporb4_hall.con --fast --frames 1800
import gdb
import math
import os

CLASS = 'CSporbBase'
gdb.set_convenience_variable('sp4_nan', gdb.Value(float('nan')))
gdb.execute('set $sp4_force_angle = 0')
gdb.execute('set $sp4_angle = 0.f')
gdb.execute('set $sp4_top = (CSporbTop*)0')
gdb.execute('set $sp4_projectile = (CSporbProjectile*)0')


def log(text):
    with open(os.path.join(os.environ['COV_OUT'], 'forced.log'), 'a') as out:
        out.write(text + '\n')


class Check(gdb.Function):
    def __init__(self):
        super().__init__('sp4_check')

    def invoke(self, index, passed):
        log('6ABC assertion #%d: %s' % (int(index), 'PASS' if int(passed) else 'FAIL'))
        if not int(passed):
            raise gdb.GdbError('UpdateShooting differs from retail')
        return 1


Check()
scenarios, labels = [], []
objlist = '((rstl::auto_ptr<CObjectList>*)$mgr->mObjectLists.mData)[0].mItem->mObjects'
children = [('$sp4_top', '(CSporbTop*)' + objlist + '[$this->mTopId.value & 2047].mEntity'),
            ('$sp4_projectile', '(CSporbProjectile*)' + objlist +
             '[$this->mProjectileId.value & 2047].mEntity')]

shot_inputs = [('mShooting', '1'), ('mShotTimer', '0.5f'), ('mTimeBetweenShots', '0.25f'),
               ('mShotsFired', '0'), ('mShotsInBurst', '3'), ('mTimerScale', '2.f'),
               ('mMinTimeBetweenShots', '0.5f'), ('mMaxTimeBetweenShots', '0.5f'),
               ('mMinTimeBetweenAttacks', '2.f'), ('mMaxTimeBetweenAttacks', '2.f'),
               ('mMinShots', '2'), ('mMaxShots', '2'), ('mIsPowerBombGuardian', '0')]
shot_saved = ['$this->mTimeBetweenAttacks', '$this->x88c_', '$this->x85c_',
              '$sp4_top->mState', '$sp4_projectile->mState', '$mgr->mRandom.mSeed']


def shot(label, writes, check):
    # Bind children to this scenario's actor, rather than the first Sporb Think of the run.
    # Pointer variables restore last, after the pointed-to state fields.
    inputs = children + [(x, x) for x in shot_saved] + [('$this->' + x, v) for x, v in shot_inputs]
    n = len(scenarios)
    scenarios.append((inputs + writes, '($this->UpdateShooting(0.125f, *$mgr), '
                      '$sp4_check(%d, %s))' % (n, check)))
    labels.append(label)


no_shot = '$this->mShotsFired == 0 && $this->mShotTimer == 0.625f'
fired = '$this->mShotsFired == 1 && $this->mShotTimer == 0.125f && $this->mTimeBetweenShots == 1.f'
shot('6AF8 not shooting', [('$this->mShooting', '0')], no_shot)
shot('6B0C timer below interval', [('$this->mShotTimer', '0.f')],
     '$this->mShotsFired == 0 && $this->mShotTimer == 0.125f')
shot('6B0C NaN timer does not fire', [('$this->mShotTimer', '$sp4_nan')],
     '$this->mShotsFired == 0 && $this->mShotTimer != $this->mShotTimer')
shot('6B0C NaN interval does not fire', [('$this->mTimeBetweenShots', '$sp4_nan')], no_shot)
shot('6B48 missing top', [('$this->mTopId.value', '65535')], no_shot)
shot('6B5C missing projectile, ordinary Sporb', [('$this->mProjectileId.value', '65535')], no_shot)
shot('6B5C/6B74 guardian without projectile', [('$this->mProjectileId.value', '65535'),
     ('$this->mIsPowerBombGuardian', '1')], fired)
shot('6CCC invalid spawner (masked editor id)', [('$this->mNeedleSpawnerId.value', '0x03ffffff')], fired)
shot('6CF4 absent spawner returns null', [('$this->mNeedleSpawnerId.value', '0')], fired)
shot('6BC8 burst ends', [('$this->mShotsFired', '2')],
     '$this->mShotsFired == 0 && !$this->mShooting && $this->mShotsInBurst == 2 '
     '&& $this->mTimeBetweenAttacks == 4.f && $this->x88c_ && $this->x85c_ == 0.f '
     '&& $this->mShotTimer == 0.125f')
shot('6BB8 shot counter wraps to byte', [('$this->mShotsFired', '255')],
     '$this->mShotsFired == 0 && $this->mShooting && $this->mShotTimer == 0.125f')
shot('6C30 negative spread saturates (reversed bounds)', [('$this->mShotsFired', '2'),
     ('$this->mMinShots', '4'), ('$this->mMaxShots', '2'), ('$mgr->mRandom.mSeed', '1')],
     '$this->mShotsInBurst == 4 && !$this->mShooting')
for seed in range(8):
    shot('6D58/6D7C real needle, seed %d' % seed, [('$mgr->mRandom.mSeed', str(seed))], fired)

# Only force GetAimAngle's return value in aim scenarios; the transforms and normalizations run.
class AngleReturn(gdb.FinishBreakpoint):
    def stop(self):
        gdb.execute('set $xmm0.v4_float[0] = $sp4_angle')
        return False


class AngleEntry(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval('$sp4_force_angle')):
            AngleReturn(gdb.newest_frame(), internal=True)
        return False


for line in open(os.environ['COV_NM']):
    raw, _, demangled = line.partition('\t')
    if demangled.startswith('CSporbBase::GetAimAngle(') and '.cold' not in demangled:
        AngleEntry('*' + raw.split()[2], internal=True)

lean_fields = ['mForwardLean', 'mBackLean', 'mLeftLean', 'mRightLean']
expected = {}


class AimCheck(gdb.Function):
    def __init__(self):
        super().__init__('sp4_aim_check')

    def invoke(self, actor, index):
        got = [float(actor.dereference()[field]) for field in lean_fields]
        want = expected[int(index)]
        good = all(math.isfinite(a) and abs(a - b) < 2.e-6 for a, b in zip(got, want))
        log('6390 assertion #%d: %s got=%s expected=%s' %
            (int(index), 'PASS' if good else 'FAIL', got, want))
        if not good:
            raise gdb.GdbError('UpdateAim differs from retail smoothing/clamping')
        return 1


AimCheck()


def aim(x, y, degrees, old):
    n = len(scenarios)
    # Independent oracle: advance toward the requested lean by at most 0.025 each call.
    # Unordered targets request decay; an unordered previous positive-target lean snaps to target.
    scale = min(abs(degrees), 120.) / 120. if math.isfinite(degrees) else float('nan')
    length = math.hypot(x, y)
    axes = (y / length, -x / length)
    goals = [0., 0., 0., 0.]
    for component, positive, negative in ((axes[0], 0, 1), (axes[1], 2, 3)):
        goal = scale * abs(component)
        if goal > 0:
            goals[positive if component >= 0 else negative] = goal
    expected[n] = [max(0., old - .025) if goal == 0 and math.isfinite(old) else
                   0. if goal == 0 else goal if not math.isfinite(old) else
                   old + max(-.025, min(.025, goal - old)) for goal in goals]
    writes = [('$this->mTransform.m%d%d' % (r, c), str(float(r == c)))
              for r in range(3) for c in range(4)]
    value = lambda v: '$sp4_nan' if math.isnan(v) else repr(v)
    writes += [('$this->mAimTarget.mX', value(x)), ('$this->mAimTarget.mY', value(y)),
               ('$this->mAimTarget.mZ', '0.f'), ('$sp4_force_angle', '1'),
               ('$sp4_angle', value(degrees / 57.295776))]
    writes += [('$this->' + field, value(old)) for field in lean_fields]
    scenarios.append((writes, '($this->UpdateAim(*$mgr), $sp4_aim_check($this, %d))' % n))
    labels.append('6390 aim=(%s,%s), degrees=%s, old=%s' % (x, y, degrees, old))


for x, y in ((-1., 1.), (1., 1.), (-1., -1.), (1., -1.)):
    for old in (0., .7, .71, 1., float('nan')):
        aim(x, y, 120., old)
    aim(x, y, 0., .5)
    aim(x, y, float('nan'), .5)
for x, y in ((1., 0.), (-1., 0.), (0., 1.), (0., -1.)):
    aim(x, y, 60., .5)
    aim(x, y, 240., 0.)
aim(-1., 1., -60., .5)
aim(float('nan'), 1., 60., .5)

shot('6B0C timer equals interval', [('$this->mShotTimer', '0.25f')], fired)
shot('6C30 positive spread truncates', [('$this->mShotsFired', '2'),
     ('$this->mMinShots', '2'), ('$this->mMaxShots', '10'), ('$mgr->mRandom.mSeed', '1')],
     '$this->mShotsInBurst == 3 && !$this->mShooting')  # third RNG = 10113; int(8*10113/65535)=1
for x, y in ((-1., 1.), (1., 1.), (-1., -1.), (1., -1.)):
    aim(x, y, 0., .01)  # each zero-target decay clamps below zero
    aim(x, y, 0., float('nan'))  # each zero-target decay selects zero on unordered

CALLS = [('sporb4 shoot and aim', 'CSporbBase::UpdateShooting(',
          '((CSporbBase*)$rdi)->mShooting && !((CSporbBase*)$rdi)->mIsPowerBombGuardian '
          '&& ((CSporbBase*)$rdi)->mTopId.value != 65535 '
          '&& ((CSporbBase*)$rdi)->mProjectileId.value != 65535',
          scenarios, '; '.join('%d=%s' % item for item in enumerate(labels)))]
SPEC = [('6ABC shooting', 'CSporbBase::UpdateShooting(',
         ['$this->mShooting', '$this->mIsPowerBombGuardian'], 'max=4000'),
        ('6390 aim', 'CSporbBase::UpdateAim(', [], 'max=4000')]
