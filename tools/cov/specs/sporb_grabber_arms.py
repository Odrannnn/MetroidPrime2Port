# sporb3: missing UpdateGrabber arms, using force_ret.py's one-call scenarios.
# BUILD=$PWD/build-port-cov COV_GDB_SCRIPT=$PWD/tools/cov/force_ret.py tools/cov/force.sh \
#   tools/cov/specs/sporb_grabber_arms.py <out> 3dfd2249,0e_swamp_hall \
#   --con tools/cov/specs/sporb_hall2.con --fast --frames 3000
import gdb
import os

CLASS = 'CSporbBase'
gdb.execute('set $sp3_proj = (CSporbProjectile*)0')
gdb.set_convenience_variable('sp3_nan', gdb.Value(float('nan')))
gdb.execute('set $sp3_mode = 0')


class Verify(gdb.Function):
    def __init__(self):
        super().__init__('sp3_verify')

    def invoke(self, index, passed):
        with open(os.path.join(os.environ['COV_OUT'], 'forced.log'), 'a') as out:
            out.write('71AC assertion #%d: %s\n' % (int(index), 'PASS' if int(passed) else 'FAIL'))
        if not int(passed):
            raise gdb.GdbError('UpdateGrabber result differs from retail')
        return 1


Verify()

# Capture the projectile through the host object list, with no inferior call in a breakpoint.
proj = ('(CSporbProjectile*)((rstl::auto_ptr<CObjectList>*)$mgr->mObjectLists.mData)'
        '[0].mItem->mObjects[$this->mProjectileId.value & 2047].mEntity')
FORCE_RET = [
    ('71AC capture projectile|once', 'CSporbBase::UpdateGrabber(', 1,
     [(0, [('!$sp3_proj', proj)])], 'capture the first hall Sporb projectile'),
]

# Save fields the scenarios can change, and restore them before the suspended natural update.
# Particle positions/bounds are recomputed by that natural update.
saved = ['$this->mHoldTimer', '$this->mAttachTimer', '$this->mGrabberInSpeed',
         '$this->mGrabberOutSpeed', '$this->mReleaseTimer', '$this->mBallEscaped',
         '$this->mState', '$this->mCanSpit', '$this->mSpitTimer', '$this->mGrabFinished',
         '$this->mTendrilParticleCount', '$this->x8f8_', 'sGrabbingSporb.value',
         '$sp3_proj->mState', '$sp3_proj->mSolidPhase', '$sp3_proj->mCarryingBall',
         '$sp3_proj->mPassable', '$P->mMorphBall.mPtr->mBallState',
         '$P->mMorphBall.mPtr->mBoostState']
saved += ['$sp3_proj->mTransform.m%d%d' % (r, c) for r in range(3) for c in range(4)]
saved += [obj + '->' + member + '.m' + axis
          for obj in ('$sp3_proj', '$P') for member in ('mPosition', 'mVelocity') for axis in 'XYZ']
scenarios = []


def case(label, state, writes=(), dt='0.016666667f', check=None):
    # Each label appears beside its scenario index in the module note.
    common = [(x, x) for x in saved]
    common += [('$this->mGrabberState', str(state))]
    call = '$this->UpdateGrabber(%s, *$mgr)' % dt
    if check:
        call = '(%s, $sp3_verify(%d, %s))' % (call, len(scenarios), check)
    scenarios.append((common + list(writes), call))
    labels.append(label)


labels = []
case('0x722C invalid projectile', 4, [('$this->mProjectileId.value', '65535')])
case('0x724C wrong-type projectile', 4, [('$this->mProjectileId.value', '$this->mTopId.value')])
case('0x7310 Idle', 4)
case('0x72F4 switch default', 7)
case('0x8C48 no tendril, Idle', 4, [('$this->mTendrilGen.mPtr', '0')])

extend = [('sGrabbingSporb.value', '65535'), ('$sp3_proj->mSolidPhase', '0')]
case('0x76D0 another Sporb grabbing', 1, [('$sp3_proj->mSolidPhase', '0'),
     ('sGrabbingSporb.value', '$this->mUniqueId.value')])
case('0x7B78 missing top', 1, extend + [('$this->mTopId.value', '65535')])
case('0x7B84 guardian telegraph skip', 1, extend + [('$this->mIsPowerBombGuardian', '1')])
case('0x7D2C no tendril, Extend', 1, extend + [('$this->mTendrilGen.mPtr', '0')])
case('0x7D44 no active particles, Extend', 1, extend + [('$this->mTendrilGen.mPtr->mActiveParticleCount', '0')])
case('0x7F10 maximum range', 1, extend + [('$this->mMaxGrabRange', '0.15f'),
     ('$sp3_proj->mPosition.mX', '$sp3_proj->mPosition.mX + 10.f')],
     check='$this->mGrabberState == 2')
case('0x7E78 speed below zero', 1, extend + [('$this->mGrabberOutSpeed', '-100.f')])

hold = [('$this->mHoldTimer', '0.f'), ('$this->mAttachTimer', '0.f'),
        ('$this->mHoldDuration', '1.f'), ('$this->mAttachDuration', '10.f'),
        ('$P->mMorphBall.mPtr->mBallState', '0'), ('sGrabbingSporb.value', '65535')]
case('0x8130/0x8250 no tendril, Hold', 2, hold + [('$this->mTendrilGen.mPtr', '0')])
case('0x8148 no active particles, Hold', 2, hold + [('$this->mTendrilGen.mPtr->mActiveParticleCount', '0')])
case('0x8444 solid before hold expiry', 2, hold + [('$sp3_proj->mSolidPhase', '0')])
case('0x847C solid at hold expiry', 2, hold + [('$sp3_proj->mSolidPhase', '0'), ('$this->mHoldTimer', '1.f')])
case('0x8460 nobody grabbing', 2, hold + [('$sp3_proj->mSolidPhase', '1')])
case('0x846C same Sporb grabbing', 2, hold + [('$sp3_proj->mSolidPhase', '1'),
     ('sGrabbingSporb.value', '$this->mUniqueId.value')])
case('0x846C different Sporb grabbing', 2, hold + [('$sp3_proj->mSolidPhase', '1'),
     ('sGrabbingSporb.value', '$this->mTopId.value')])
case('0x84A8 boost but not passable', 2, hold + [('$sp3_proj->mPassable', '0'),
     ('$P->mMorphBall.mPtr->mBallState', '1')])
case('0x84A8 boost escape', 2, hold + [('$sp3_proj->mPassable', '1'),
     ('$P->mMorphBall.mPtr->mBallState', '1')])
case('0x83F0 attach timeout without ball', 2, hold + [('$this->mAttachTimer', '10.f'),
     ('$sp3_proj->mSolidPhase', '0')])
case('0x83F0 attach timeout with ball', 2, hold + [('$this->mAttachTimer', '10.f'),
     ('$sp3_proj->mSolidPhase', '2')])

retract = [('$this->mGrabberInSpeed', '0.f'), ('$this->mGrabberInAcceleration', '0.f'),
           ('$sp3_proj->mCarryingBall', '0')]
case('0x87BC/0x88DC no tendril, Retract', 3, retract + [('$this->mTendrilGen.mPtr', '0')])
case('0x87D4 no active particles, Retract', 3, retract + [('$this->mTendrilGen.mPtr->mActiveParticleCount', '0')])
case('0x8A50 positive retract speed', 3, retract + [('$this->mGrabberInSpeed', '1.f')])
case('0x8B30 missing top at arrival', 3, [('$this->mGrabberInSpeed', '-100000.f'),
     ('$sp3_proj->mCarryingBall', '1'), ('$this->mTopId.value', '65535')])

# Force just the returned look-at's forward column, while a BallInside scenario runs.
# The zero / tiny-positive vectors reach the two short-circuited tests at 0x795C/0x797C;
# an unordered dot must keep extending (the existing sporb1 correction).
# force_ret.py's outer FinishBreakpoint cannot bind to CALLS' dummy frame. Bind directly to
# the real BuildLookAtTransform callee instead; only scenarios 27-29 set sp3_mode nonzero.
class LookReturn(gdb.FinishBreakpoint):
    def __init__(self, frame, address, mode):
        super().__init__(frame, internal=True)
        self.address, self.mode = address, mode

    def stop(self):
        for row in range(3):
            lv = '((CTransform4f*)%d)->m%d1' % (self.address, row)
            value = '0.f' if self.mode == 1 else lv + ' * 1.e-8f' if self.mode == 2 else '$sp3_nan'
            gdb.execute('set var %s = %s' % (lv, value))
        return False


class LookEntry(gdb.Breakpoint):
    def stop(self):
        mode = int(gdb.parse_and_eval('$sp3_mode'))
        if mode:
            LookReturn(gdb.newest_frame(), int(gdb.parse_and_eval('$rdi')), mode)
        return False


for line in open(os.environ['COV_NM']):
    raw, _, demangled = line.partition('\t')
    if demangled.startswith('CSporbBase::BuildLookAtTransform(') and '.cold' not in demangled:
        LookEntry('*' + raw.split()[2], internal=True)
ball = [('sGrabbingSporb.value', '65535'), ('$sp3_proj->mSolidPhase', '2')]
case('0x795C dot zero', 1, ball + [('$sp3_mode', '1')], check='$this->mGrabberState == 2')
case('0x797C dot tiny positive', 1, ball + [('$sp3_mode', '2')], check='$this->mGrabberState == 2')
case('0x795C/0x797C/0x799C unordered extends', 1, ball + [('$sp3_mode', '3')],
     check='$this->mGrabberState == 1')
case('0x7E78 NaN out speed propagates', 1, extend + [('$this->mGrabberOutSpeed', '$sp3_nan')],
     check='$this->mGrabberOutSpeed != $this->mGrabberOutSpeed')
case('0x7F94 NaN hold timer propagates', 2, hold + [('$this->mHoldTimer', '$sp3_nan')],
     check='$this->mHoldTimer != $this->mHoldTimer')
case('0x83BC NaN attach timer propagates', 2, hold + [('$this->mAttachTimer', '$sp3_nan')],
     check='$this->mAttachTimer != $this->mAttachTimer && $this->mGrabberState == 2')
case('0x8A50 NaN in speed propagates', 3, retract + [('$this->mGrabberInSpeed', '$sp3_nan')],
     check='$this->mGrabberInSpeed != $this->mGrabberInSpeed')
for state, setup, addr in ((1, extend, '0x7C30'), (2, hold, '0x800C'), (3, retract, '0x86FC')):
    case(addr + ' unordered length selects finite range', state,
         setup + [('$sp3_proj->mPosition.mX', '$sp3_nan'), ('$this->mMaxGrabRange', '3.f'),
                  ('$this->mTendrilGen.mPtr', '0')], check='$this->mTendrilParticleCount == 22')

CALLS = [('71AC missing arms', 'CSporbBase::UpdateGrabber(',
          '$sp3_proj != 0 && ((CSporbBase*)$rdi)->mProjectileId.value == $sp3_proj->mUniqueId.value'
          ' && ((CSporbBase*)$rdi)->mGrabberState == 2', scenarios,
          '; '.join('%d=%s' % x for x in enumerate(labels)))]
SPEC = [('71AC natural state', 'CSporbBase::UpdateGrabber(', ['$this->mGrabberState'], 'max=6000')]
