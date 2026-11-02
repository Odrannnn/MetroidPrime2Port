# GunTurret data defaults, scan bounds and gun orientation. force_ret.py.
# Coverage-only gb6_gunturret_keep_inline.cmake retains real inline SLdr bodies.
import gdb
import os

CLASS = 'CGunTurretBase'
SPEC = []
symbols = []
for line in open(os.environ['COV_NM']):
    raw, _, name = line.rstrip().partition('\t')
    parts = raw.split()
    if len(parts) == 3 and parts[1] in 'tTW' and '.cold' not in name:
        symbols.append((parts[2], name))


def symbol(prefix):
    return next(m for m, d in symbols if d.startswith(prefix))


def log(text):
    with open(os.path.join(os.environ['COV_OUT'], 'forced.log'), 'a') as out:
        out.write(text + '\n')


def val(expr):
    return gdb.parse_and_eval(expr)


def put(expr, value):
    gdb.execute('set var %s=%s' % (expr, value))


def assert_equal(actual, expected, label):
    good = abs(float(actual) - expected) < 1.e-5 if isinstance(expected, float) else int(actual) == expected
    if not good:
        raise gdb.GdbError('%s: got%s expected%s' % (label, actual, expected))


def color(data, expected, label):
    for channel, value in zip('RGBA', expected):
        assert_equal(data['m' + channel], value, label + channel)


class Defaults(gdb.Function):
    def __init__(self):
        super().__init__('gb6_defaults')

    def invoke(self, ptr, top):
        data = ptr.dereference()
        if int(top):
            expected = {'powerUpTime': .5, 'powerDownTime': .5, 'pART': 0xffffffff,
                        'pART_0xaf6e671a': 0xffffffff, 'alwaysFF': -1,
                        'alwaysFF_0x68d8b844': -1, 'sound': -1, 'sound_0x5d9ed447': -1}
            color(data['lightColor'], (255, 255, 255, 0), 'top light')
        else:
            expected = {'hurtSleepDelay': 2., 'gunAimTurnSpeed': 180., 'gunLockOnTurnSpeed': 180.,
                'minTimeBetweenAttacks': 2., 'maxTimeBetweenAttacks': 2.,
                'minTimeBetweenShots': 1., 'maxTimeBetweenShots': 1.,
                'minShotsInABurst': 1, 'maxShotsInABurst': 1, 'maxPitchAngleUp': 85.,
                'maxPitchAngleDown': -45., 'unknown_0xfc036e93': 0., 'shotAngleVariance': 0.,
                'patrolDelay': 0., 'withdrawDelay': 0., 'unknown_0x8a35b1ea': 1.,
                'unknown_0xd49bec5a': 1., 'maxPoleExtensionLength': 1., 'attackDelay': 0.,
                'detectionHeightUp': 0., 'detectionHeightDown': 0., 'attackLeashTime': 0.,
                'gunRespawns': 0, 'unknown_0x5cf12e9a': 0, 'unknown_0x479d8dc4': 0, 'isPirateTurret': 0,
                'cRSC': 0xffffffff, 'pirateProjectileEffect': 0xffffffff, 'pirateProjectileVisorEffect': 0xffffffff,
                'maxAudibleDistance': 100., 'soundFallOff': 0.}
            sounds = ('gFFireShotSound', 'pirateFireShotSound', 'lockOnSound', 'gunPanSound',
                'gFGunChargeSound', 'pirateGunChargeSound', 'gunLowerLoopedSound', 'gunLowerOffSound',
                'gunRaiseLoopedSound', 'gunRaiseOffSound', 'pirateGunDeathLowerLoopedSound',
                'gFGunDeathLowerLoopedSound', 'poleSparksSound')
            expected.update((field, -1) for field in sounds)
            assert_equal(data['attackDamage']['dI_WeaponType'], 11, 'base weapon')
            assert_equal(data['attackDamage']['dI_Damage'], 5., 'base damage')
            assert_equal(data['patterned']['echoParameters']['isEchoEmitter'], 1, 'base echo emitter')
        for field, expected_value in expected.items():
            assert_equal(data[field], expected_value, field)
        assert_equal(data['patterned']['health']['hI_KnockBackResistance'], 2., 'resistance')
        color(data['actorInformation']['lighting']['ambientColor'], (255, 255, 255, 255), 'ambient')
        assert_equal(data['actorInformation']['visor']['visorFlags'], 15, 'visor')
        log(('7FB8' if int(top) else '13C0') + ' standalone constructor default oracle PASS')
        return 1


class Getters(gdb.Function):
    def __init__(self):
        super().__init__('gb6_getters')

    def invoke(self, good):
        if not bool(good):
            raise gdb.GdbError('GunTurret getter oracle failed')
        log('360/1C38/7B54 getter oracle PASS')
        return 1


class Prepare(gdb.Function):
    def __init__(self):
        super().__init__('gb6_prepare')

    def invoke(self, actor, mgr, segment):
        put('$gb6_seg', '(CSegId*)malloc(sizeof(CSegId))')
        put('$gb6_seg->mId', str(int(segment) & 255))
        table = val('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem' % int(mgr)).dereference()['mObjects']
        uid = int(actor.dereference()['mTopId']['value'])
        top = table[uid & 2047]['mEntity'].cast(gdb.lookup_type('CGunTurretTop').pointer())
        gdb.set_convenience_variable('gb6_top', top)
        log('3520 real segment%d top%04x' % (int(segment) & 255, uid))
        return 1


Defaults()
Getters()
Prepare()
scenarios = [([], '$gb6_prepare($this,$mgr,$rdx)')]
for is_top, cls in ((0, 'SLdrGunTurretBase'), (1, 'SLdrGunTurretTop')):
    ctor = symbol(cls + '::' + cls + '()')
    dtor = symbol(cls + '::~' + cls + '()').replace('D1', 'D2').replace('D0', 'D2')
    expr = ('($gb6_data=(%s*)malloc(sizeof(%s)),((void(*)(void*))%s)($gb6_data),'
            '$gb6_defaults($gb6_data,%d),((void(*)(void*))%s)($gb6_data),(void)free($gb6_data),1)') % (
                cls, cls, ctor, is_top, dtor)
    scenarios.append(([], expr))
scenarios.append(([], '$gb6_getters($this->IsScanVisorSelfRender() && $gb6_top->IsScanVisorSelfRender() && '
                     '(void*)$this->GetCollisionPrimitive()==(void*)&$this->mCollisionPrimitive)'))


def orient(aim, dt, writes):
    scenarios.append((list(writes), '$this->UpdateGunOrientation(*$mgr,*$gb6_seg,%d,%sf)' % (aim, dt)))


orient(0, .016, [('$this->mTopId.value', '65535')])
orient(0, .016, [('$gb6_top->mBodyController.mPtr->mFrozen', '1')])
for aim in (0, 1):
    for first in (0, 1):
        for speed in (.01, 100.):
            for side, height in ((-30, -100), (0, 0), (30, 100)):
                writes = [('$gb6_top->mBodyController.mPtr->mFrozen', '0'),
                          ('$this->mFirstShot', str(first)), ('$this->mGunAimTurnSpeed', str(speed)),
                          ('$this->mGunLockOnTurnSpeed', str(speed)), ('$this->mMaxPitchAngleUp', '.25f'),
                          ('$this->mMaxPitchAngleDown', '-.25f'), ('$this->x8ac_', '0.f'), ('$this->x8b0_', '0.f'),
                          ('$this->xfc03_', '100.f' if speed == 100. else '0.f')]
                for coord, trans, right, forward, up in (('X','03','00','01','02'), ('Y','13','10','11','12'), ('Z','23','20','21','22')):
                    writes.append(('$this->mTargetPos.m' + coord,
                                   '$this->mTransform.m%s+%d*$this->mTransform.m%s+20*$this->mTransform.m%s+%d*$this->mTransform.m%s' %
                                   (trans, side, right, forward, height, up)))
                orient(aim, .016, writes)

for scan, destroyed, state, raised, missing_top in (
        (0,0,2,1,0), (1,1,2,1,0), (1,0,2,1,1), (1,0,2,1,0),
        (1,0,3,1,0), (1,0,1,1,0), (1,0,8,1,0), (1,0,9,1,0), (1,0,10,1,0),
        (1,0,4,0,0), (1,0,4,1,0), (1,0,5,0,0), (1,0,5,1,0), (1,0,0,0,0)):
    writes = [('$P->mScanningObject.value', '$this->mUniqueId.value' if scan else '65535'),
              ('$this->mGunDestroyed', str(destroyed)), ('$this->mState', str(state)),
              ('$this->mRaise', str(raised))]
    if missing_top:
        writes.append(('$this->mTopId.value', '65535'))
    scenarios.append((writes, '$this->GetScanVisorRenderBounds(*$mgr)'))
scenarios.append(([], '(void)free($gb6_seg)'))
CALLS = [('gb6 turret suite', 'CGunTurretBase::UpdateGunOrientation(',
          '((CGunTurretBase*)$rdi)->mTopId.value!=65535', scenarios,
          'real data ctor lifetimes and input-only scan/orientation fixtures')]
