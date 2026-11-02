# GunTurret gt2: constructor, targeting and projectile fixtures. The constructor
# PAS no-match result and mixed near-list inputs are forced and logged explicitly.
# Run with force_ret.py on build-port-cov (gb6 inline-retention config retained);
# tools/drivers/gt2_gunturret.con. No native probes or production-code overrides.
import gdb
import os
import struct

CLASS = 'CGunTurretBase'
SPEC = []


def log(text):
    with open(os.environ['COV_OUT'] + '/gt2.log', 'a') as out:
        out.write(text + '\n')


def ev(text):
    return gdb.parse_and_eval(text)


symbols = []
for line in open(os.environ['COV_NM']):
    raw, _, dem = line.rstrip().partition('\t')
    parts = raw.split()
    if len(parts) == 3 and parts[1] in 'tTW' and '.cold' not in dem:
        symbols.append((parts[2], dem))


class ConstructorDone(gdb.FinishBreakpoint):
    def __init__(self, ptr, which, raise_input):
        super().__init__(gdb.newest_frame(), internal=True)
        self.ptr, self.which, self.raise_input = ptr, which, raise_input

    def stop(self):
        try:
            obj = ev('((CGunTurretBase*)%d)' % self.ptr).dereference()
            assert int(obj['mPirateProjectileEffect']['m_valid']) == (self.which == 1)
            assert int(obj['mAlert']) == 1 and int(obj['mOccluded']) == 0
            assert int(obj['mTopId']['value']) == 65535
            assert abs(float(obj['mMaxRaise']) - min(1., self.raise_input / 10.)) < 1.e-5
            if self.which == 2:
                assert int(obj['mAdditiveAnim']) == 0
            log('6518 ctor%d field oracle PASS (additive=%d)' % (self.which, int(obj['mAdditiveAnim'])))
        except Exception as error:
            log('ERROR ctor return %s' % error)
        return False


class Constructor(gdb.Breakpoint):
    def __init__(self, symbol):
        super().__init__('*' + symbol, internal=True)
        self.n = 0

    def stop(self):
        self.n += 1
        try:
            effect = int(ev('pirateProjectileEffect'))
            log('6518 ctor%d effect=%08x maxRaiseInput=%s' % (self.n, effect, ev('unknown80ce')))
            # SysV: this/uid/name/info/xf/model occupy six integer registers;
            # patterned, damage, actor params, four byte/bool args, crsc,
            # projectile, effect are the first ten stack words.
            assert int(ev('*(unsigned int*)($rsp+80)')) == effect
            if self.n in (1, 2):
                value = 0x11592cde if self.n == 1 else 0xffffffff
                gdb.execute('set var *(unsigned int*)($rsp+80)=%d' % value)
                log('6518 ctor%d: optional particle input %08x' % (self.n, value))
                if self.n == 2:
                    assert abs(float(ev('*(float*)($rsp+296)')) - float(ev('unknown80ce'))) < 1.e-5
                    gdb.execute('set var *(float*)($rsp+296)=20.f')
                    log('6518 ctor2: max-raise input20 tests min(1,20/10) cap')
                ConstructorDone(int(ev('$rdi')), self.n, float(ev('unknown80ce')))
        except Exception as error:
            log('ERROR ctor %s' % error)
        return False


for sym, dem in symbols:
    if dem.startswith('CGunTurretBase::CGunTurretBase(') and 'C2' in sym:
        Constructor(sym)


class LoaderDone(gdb.FinishBreakpoint):
    def __init__(self, stream, start, end, unknown, label):
        super().__init__(gdb.newest_frame(), internal=True)
        self.stream, self.start, self.end, self.unknown, self.label = stream, start, end, unknown, label

    def stop(self):
        try:
            actual = int(ev('((CInputStream*)%d)->mPtr' % self.stream))
            assert actual == self.end, (actual - self.start, self.end - self.start)
            log('%s loader consumed-length oracle PASS (%d bytes, unknown=%s, nonnull=%s)' %
                (self.label, actual - self.start, self.unknown, bool(int(self.return_value))))
        except Exception as error:
            log('ERROR loader return %s' % error)
        return False


class Loader(gdb.Breakpoint):
    def __init__(self, sym, label):
        super().__init__('*' + sym, internal=True)
        self.label = label

    def stop(self):
        try:
            stream = int(ev('$rsi'))
            obj = ev('((CInputStream*)%d)' % stream).dereference()
            start = int(obj['mPtr'])
            remaining = int(obj['mBuffer']) + int(obj['mBufferSize']) - start
            data = bytes(gdb.selected_inferior().read_memory(start, remaining))
            count, offset, unknown = int.from_bytes(data[:2], 'big'), 2, False
            for _ in range(count):
                prop, size = struct.unpack_from('>IH', data, offset)
                unknown |= prop == 0xfeedbeef
                offset += 6 + size
            LoaderDone(stream, start, start + offset, unknown, self.label)
        except Exception as error:
            log('ERROR loader %s' % error)
        return False


for sym, dem in symbols:
    if dem.startswith('LoadGunTurretBase('):
        Loader(sym, '4B0')
    elif dem.startswith('LoadGunTurretTop('):
        Loader(sym, '7B98')

class Prepare(gdb.Function):
    def __init__(self):
        super().__init__('gt2_prepare')

    def invoke(self, actor, mgr):
        table = ev('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem' % int(mgr)).dereference()['mObjects']
        for index in range(2048):
            item = table[index]
            obj = item['mEntity']
            if not int(obj):
                continue
            typ = str(obj.dynamic_type)
            if 'CSpankWeed' in typ:
                gdb.set_convenience_variable('gt2_other', obj.cast(gdb.lookup_type('CPatterned').pointer()))
            elif 'CGunTurretTop' in typ:
                if int(obj.dereference()['mUniqueId']['value']) == int(actor.dereference()['mTopId']['value']):
                    gdb.set_convenience_variable('gt2_top', obj.cast(gdb.lookup_type('CGunTurretTop').pointer()))
            elif 'CGunTurretBase' in typ and int(obj) != int(actor):
                gdb.set_convenience_variable('gt2_base2', obj.cast(gdb.lookup_type('CGunTurretBase').pointer()))
        other = ev('$gt2_other')
        log('target fixtures: real CSpankWeed uid=%04x, top PART=%08x' %
            (int(other.dereference()['mUniqueId']['value']), int(ev('$gt2_top->mGFChargeEffect'))))
        gdb.set_convenience_variable('gt2_near_mode', gdb.Value(0))
        return 1


class Expect(gdb.Function):
    def __init__(self):
        super().__init__('gt2_expect')

    def invoke(self, actual, expected):
        if int(actual) != int(expected):
            raise gdb.GdbError('gt2 oracle: got %s expected %s' % (actual, expected))
        log('runtime return oracle PASS')
        return 1


class NeighborsDone(gdb.FinishBreakpoint):
    def __init__(self, ptr, mode):
        super().__init__(gdb.newest_frame(), internal=True)
        self.ptr, self.mode = ptr, mode

    def stop(self):
        try:
            ids = [] if self.mode == 1 else [65535, int(ev('$gt2_player_uid')),
                  int(ev('$gt2_top->mUniqueId.value')), int(ev('$gt2_base2->mUniqueId.value')),
                  int(ev('$gt2_base2->mTopId.value')), int(ev('$gt2_other->mUniqueId.value')),
                  int(ev('$gt2_other->mUniqueId.value'))]
            gdb.selected_inferior().write_memory(self.ptr, struct.pack('<i', len(ids)) +
                                                 b''.join(struct.pack('<H', i) for i in ids))
            log('forced neighbor input list: %s' % ids)
        except Exception as error:
            log('ERROR neighbor %s' % error)
        return False


class Neighbors(gdb.Breakpoint):
    def stop(self):
        mode = gdb.convenience_variable('gt2_near_mode')
        if mode is not None and int(mode):
            NeighborsDone(int(ev('$rsi')), int(mode))
        return False


for sym, dem in symbols:
    if dem.startswith('CStateManager::BuildNearList(') and 'CAABox' in dem:
        Neighbors('*' + sym, internal=True)


class AnimationResult(gdb.FinishBreakpoint):
    def stop(self):
        # SysV returns pair<float,int> in RAX (one INTEGER-class eightbyte).
        # Supply a no-match result. The other constructor retains its shipped match.
        log('6518 PAS actual result=%s; forced no-match score0 animation0' % self.return_value)
        gdb.execute('set $rax=0')
        return False


class AnimationQuery(gdb.Breakpoint):
    def __init__(self, symbol):
        super().__init__('*' + symbol, internal=True)
        self.n = 0

    def stop(self):
        parent = gdb.newest_frame().older()
        if parent is not None and 'CGunTurretBase' in (parent.name() or '') and '__ct' not in (parent.name() or ''):
            self.n += 1
            if self.n == 2:
                AnimationResult(gdb.newest_frame(), internal=True)
        return False


for sym, dem in symbols:
    if dem.startswith('CPASDatabase::FindBestAnimation(') and 'int, bool' not in dem:
        AnimationQuery(sym)

Prepare()
Expect()
gdb.set_convenience_variable('gt2_near_mode', gdb.Value(0))
scenarios = [([], '$gt2_prepare($this,$mgr)'),
             ([], '($gt2_player_uid=$P->mUniqueId.value,1)')]


def add(writes, expr, expected=None):
    if expected is not None:
        expr = '$gt2_expect((long)(%s),(long)(%s))' % (expr, expected)
    scenarios.append((writes, expr))


base_inputs = [('$this->mAlert', '1'), ('$this->mOccluded', '0'),
               ('$this->mTeamIndex', '-1'), ('$this->mGunHit', '0'),
               ('$this->x8be_', '0'), ('$this->x8bf_', '0'),
               ('$this->mMinAttackRange', '0.f'), ('$this->mMaxAttackRange', '1000.f'),
               ('$this->mDetectionHeightUp', '0.f'), ('$this->mDetectionHeightDown', '0.f')]
for changes in ([('$this->mAlert', '0')], [('$this->mOccluded', '1')],
                [('$this->mTopId.value', '65535')],
                [('$mgr->mNumPlayers', '0')],
                [('$this->mTeamIndex', '$mgr->mPlayerStates[0]->mPersistentState.mTeamIndex')],
                [('$this->mMaxAttackRange', '0.f')],
                [('$this->mMinAttackRange', '1000.f')]):
    add(base_inputs + changes, '$this->FindTarget(*$mgr)', '0')
add(base_inputs, '$this->FindTarget(*$mgr)', '$P')
add(base_inputs + [('$this->mTeamIndex', '$mgr->mPlayerStates[0]->mPersistentState.mTeamIndex+1')],
    '$this->FindTarget(*$mgr)', '$P')
add(base_inputs + [('$this->mGunHit', '1'), ('$this->mMaxAttackRange', '0.f')],
    '$this->FindTarget(*$mgr)', '$P')
add(base_inputs + [('$this->mGunHit', '1'), ('$this->x8be_', '1')],
    '$this->FindTarget(*$mgr)', '$P')
add(base_inputs + [('$mgr->mNumPlayers', '2'), ('$mgr->mPlayers[1]', '$P'),
                  ('$mgr->mPlayerStates[1]', '$mgr->mPlayerStates[0]')],
    '$this->FindTarget(*$mgr)', '$P')
for mode, alive, score, expected in ((1,1,0,'0'), (2,0,0,'0'), (2,1,0,'$gt2_other'),
                                     (2,1,1,'$P')):
    changes = [('$gt2_near_mode', str(mode)), ('$gt2_other->mAlive', str(alive)),
               ('$this->x8be_', '1'), ('$this->mDetectionRange', '0.f')]
    if score == 0:
        changes.append(('$this->mTeamIndex', '$mgr->mPlayerStates[0]->mPersistentState.mTeamIndex'))
    else:
        for coord in 'XYZ':
            changes.append(('$gt2_other->mPosition.m' + coord, '$P->mPosition.m%s+100.f' % coord))
    add(base_inputs + changes, '$this->FindTarget(*$mgr)', expected)

for changes, expected in (([('$this->mTopId.value','65535')],0),
                         ([('$mgr->mNumPlayers','0')],0),
                         ([],1),
                         ([('$this->mTeamIndex','$mgr->mPlayerStates[0]->mPersistentState.mTeamIndex')],0),
                         ([('$this->mDetectionHeightUp','.01f'), ('$this->mDetectionHeightDown','.01f')],0)):
    add(base_inputs + changes, '$this->PlayerInRange(*$mgr,1000.f)', str(expected))
add(base_inputs, '$this->PlayerInRange(*$mgr,0.f)', '0')
add(base_inputs + [('$this->mTeamIndex', '$mgr->mPlayerStates[0]->mPersistentState.mTeamIndex+1')],
    '$this->PlayerInRange(*$mgr,1000.f)', '1')
for mode, alive, dist, height, expected in ((1,1,1,0,0), (2,0,1,0,0),
                                           (2,1,1,0,1), (2,1,100,0,0), (2,1,1,3,0)):
    changes = [('$gt2_near_mode', str(mode)), ('$gt2_other->mAlive', str(alive)),
               ('$this->x8bf_', '1'), ('$this->mDetectionRange', '200.f'),
               ('$this->mTeamIndex', '$mgr->mPlayerStates[0]->mPersistentState.mTeamIndex'),
               ('$this->mDetectionHeightUp', '2.f'), ('$this->mDetectionHeightDown', '2.f')]
    for coord, delta in zip('XYZ', (dist,0,height)):
        changes.append(('$gt2_other->mPosition.m' + coord, '$this->mPosition.m%s+%d.f' % (coord, delta)))
    add(base_inputs + changes, '$this->PlayerInRange(*$mgr,10.f)', str(expected))

add([('$this->mTopId.value', '65535')], '$this->LaunchProjectile(*$mgr)')
add([('$this->mProjectileInfo.mWeaponDescription.mItem', '0'),
     ('$this->mProjectileInfo.mWeaponDescription.mLockHeld', '0')], '$this->LaunchProjectile(*$mgr)')
add([('$this->mProjectileInfo.mWeaponDescription.mItem', '0'),
     ('$this->mProjectileInfo.mWeaponDescription.mLockHeld', '1'),
     ('$this->mProjectileInfo.mWeaponDescription.mObjRef->mObject', '0')], '$this->LaunchProjectile(*$mgr)')
add([('$this->mProjectileInfo.mWeaponDescription.mItem', '0'),
     ('$this->mProjectileInfo.mWeaponDescription.mLockHeld', '1')],
    '($this->LaunchProjectile(*$mgr),$gt2_expect($this->mProjectileInfo.mWeaponDescription.mItem!=0,1))')
inc = '$mgr->mWeaponMgr->IncrCount($this->mUniqueId,kWT_AI)'
dec = '$mgr->mWeaponMgr->DecrCount($this->mUniqueId,kWT_AI)'
add([], '(' + ','.join([inc]*8 + ['$this->LaunchProjectile(*$mgr)'] + [dec]*8 + ['1']) + ')')
add([], '$this->LaunchProjectile(*$mgr)')
CALLS = [('gt2 runtime suite', 'CGunTurretBase::UpdateGunOrientation(',
          '((CStateManager*)$rsi)->mUpdateFrameIdx>600', scenarios,
          'input-only targeting, neighbor fixtures and projectile guards')]
