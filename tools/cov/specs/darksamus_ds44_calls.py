# ds45: the arms of the bodies ds44/ds45 read against the asm (B86C 12F60 B164 C470 1905C 1D9A4
# CA0C 1707C 1C99C 19B64 CEB4) that the rooftop fight (tools/drivers/darksamus_ds43.con) does not reach, forced by gdb calls
# the ds43 way: gdb boots the game, and at the DS44_AT-th (default 4600) CDarkSamus::Think it runs
# tools/cov/specs/darksamus_ds44_calls.gdb, which writes the members each arm needs, calls the
# body and writes every member back. The breakpoints below force return values while their gate
# (a gdb convenience variable) is set; a count gate forces only the n-th call.
#   COV_GDB_SCRIPT=$PWD/tools/cov/specs/darksamus_ds44_calls.py BUILD=$PWD/build-port-cov \
#     MP2_INPUT=LAYER2@400 tools/cov/force.sh tools/cov/specs/darksamus_ds44_calls.py <out> \
#     "1baa96c2,10_cliff_rooftop" --con tools/drivers/darksamus_ds43.con --fast --frames 5900
import os
import gdb

CLASS = 'CDarkSamus'
FORCE = []
HERE = os.path.dirname(os.path.abspath(os.environ['COV_SPEC']))
OUT = os.environ['COV_OUT']

syms = []
for ln in open(os.environ['COV_NM']):
    parts = ln.rstrip('\n').split('\t')
    if len(parts) == 2:
        f = parts[0].split()
        if len(f) == 3 and f[1] in 'tTW':
            syms.append((f[2], parts[1]))


def addr(prefix):
    hits = [m for m, d in syms if d.startswith(prefix) and '.cold' not in d and 'clone' not in d]
    if len(hits) != 1:
        raise gdb.GdbError('%s: %d symbols' % (prefix, len(hits)))
    return '*' + hits[0]


def var(name):
    v = gdb.convenience_variable(name)
    try:
        return 0 if v is None else int(v)
    except gdb.error:  # a void value (an unset variable gdb has already seen)
        return 0


class OnFinish(gdb.FinishBreakpoint):
    def __init__(self, frame, cmd):
        super().__init__(frame, internal=True)
        self.cmd = cmd

    def stop(self):
        if callable(self.cmd):
            self.cmd()
        else:
            gdb.execute(self.cmd)
        return False

    def out_of_scope(self):
        pass


class ForceRet(gdb.Breakpoint):
    """While `gate` is 1, run `cmd` when the function returns. A gate of n > 1 counts down and
    forces only the n-th call (then reads 0); a gate of -1 forces every call."""

    def __init__(self, spec, gate_name, cmd, on_entry=None):
        super().__init__(spec, internal=True)
        self.gate_name, self.cmd, self.on_entry = gate_name, cmd, on_entry

    def stop(self):
        g = var(self.gate_name)
        if g == 0:
            return False
        if g > 1:
            gdb.set_convenience_variable(self.gate_name, g - 1)
            return False
        if g == 1:
            gdb.set_convenience_variable(self.gate_name, 0)
        if self.on_entry is not None:
            self.on_entry()
        OnFinish(gdb.newest_frame(), self.cmd)
        return False


# Recorded return values of CStateManager::ObjectById while $rec is 1: $recn(), $recat(i).
recorded = []


recorded_active = {}


def record_rax():
    ptr = int(gdb.parse_and_eval('$rax'))
    recorded.append(ptr)
    if ptr != 0 and ptr not in recorded_active:
        recorded_active[ptr] = int(gdb.parse_and_eval('((CEntity*)%d)->mActive' % ptr))


class Rec(gdb.Breakpoint):
    def stop(self):
        if var('rec') == 1:
            OnFinish(gdb.newest_frame(), record_rax)
        return False


Rec(addr('CStateManager::ObjectById('), internal=True)


class RecN(gdb.Function):
    def __init__(self):
        super().__init__('recn')

    def invoke(self):
        return len(recorded)


class RecI(gdb.Function):
    def __init__(self):
        super().__init__('recat')

    def invoke(self, i):
        i = int(i)
        return recorded[i] if 0 <= i < len(recorded) else 0


class RecClear(gdb.Command):
    def __init__(self):
        super().__init__('ds45-recclear', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        del recorded[:]


class RecRestore(gdb.Command):
    """Write back the mActive each recorded entity had when first looked up."""

    def __init__(self):
        super().__init__('ds45-recrestore', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        for ptr, act in recorded_active.items():
            gdb.execute('set var ((CEntity*)%d)->mActive = %d' % (ptr, act))


class IsA(gdb.Function):
    """$isa(ptr, "Name"): 1 when the entity at ptr has a dynamic type naming Name."""

    def __init__(self):
        super().__init__('isa')

    def invoke(self, ptr, name):
        if int(ptr) == 0:
            return 0
        v = gdb.parse_and_eval('(CEntity*)%d' % int(ptr)).dereference()
        return 1 if name.string() in str(v.dynamic_type) else 0


RecRestore()
IsA()
RecN()
RecI()
RecClear()

# fn_10_B86C: a null ObjectById for the n-th lookup ($onull = n).
ForceRet(addr('CStateManager::ObjectById('), 'onull', 'set var $rax = 0')
# fn_10_B164: no boost hint; no arrived hint; the arrived hint kept in $ahint.
ForceRet(addr('CDarkSamus::FindBoostBallHint('), 'fbh0', 'set var $rax = 0')
ForceRet(addr('CDarkSamus::FindArrivedHint('), 'fah0', 'set var $rax = 0')
ForceRet(addr('CDarkSamus::FindArrivedHint('), 'capah',
         lambda: gdb.set_convenience_variable('ahint', int(gdb.parse_and_eval('$rax'))))
# fn_10_1D9A4: GetIdForScript answering $gifsuid; a region with an obstruction.
ForceRet(addr('CStateManager::GetIdForScript('), 'gifs',
         lambda: gdb.execute('set var $rax = %d' % var('gifsuid')))
ForceRet(addr('CPFRegion::GetObstructionCount('), 'obs1', 'set var $rax = 1')
# fn_10_1C99C: WeaponHits answering $whv.
ForceRet(addr('CDamageVulnerability::WeaponHits('), 'wh', 'set var $rax = $whv')
# fn_10_C470: the CCollisionResponseData the decal is asked of, kept in $crd.
ForceRet(addr('CCollisionResponseData::GetDecalDescription('), 'capcrd', 'print 0',
         on_entry=lambda: gdb.set_convenience_variable('crd', int(gdb.parse_and_eval('$rsi'))))


# A forced ray hit (CRayCastResult by sret: rax at the return). The hit point lies s along the
# ray from its start: s = length - $rwN_back (N = the call's number from 1), or 1 behind the start
# for a $rwN_back of -1; a $rwN_back of 0 leaves that call alone. RayWorldIntersection: rcx start,
# r8 dir, xmm0 length (sret rdi, this rsi, hit id rdx). RayStaticIntersection: rdx start, rcx dir.
class RayForce(gdb.Breakpoint):
    def __init__(self, spec, gate_name, start_reg, dir_reg):
        super().__init__(spec, internal=True)
        self.gate_name, self.start_reg, self.dir_reg = gate_name, start_reg, dir_reg

    def stop(self):
        if var(self.gate_name) == 0:
            return False
        n = var(self.gate_name + 'n') + 1
        gdb.set_convenience_variable(self.gate_name + 'n', n)
        back = var('%s%d_back' % (self.gate_name, n))
        if back == 0:
            return False
        s0 = gdb.parse_and_eval('*(CVector3f*)$%s' % self.start_reg)
        d0 = gdb.parse_and_eval('*(CVector3f*)$%s' % self.dir_reg)
        start = [float(s0['mX']), float(s0['mY']), float(s0['mZ'])]
        d = [float(d0['mX']), float(d0['mY']), float(d0['mZ'])]
        length = float(gdb.parse_and_eval('$xmm0.v4_float[0]'))
        s = -1.0 if back == -1 else length - back
        p = [start[i] + s * d[i] for i in range(3)]

        def write():
            r = '((CRayCastResult*)$rax)'
            gdb.execute('set var %s->mValid = 1' % r)
            gdb.execute('set var %s->mTime = %r' % (r, s))
            for i, c in enumerate('XYZ'):
                gdb.execute('set var %s->mPoint.m%s = %r' % (r, c, p[i]))
                gdb.execute('set var %s->mPlane.mNormal.m%s = %r' % (r, c, -d[i]))
            gdb.execute('set var %s->mPlane.mConstant = %r' % (r, -sum(p[i] * d[i] for i in range(3))))
            gdb.execute('printf "ray %d forced: s %%f len %%f\\n", %r, %r' % (n, s, length))

        OnFinish(gdb.newest_frame(), write)
        return False


RayForce(addr('CStateManager::RayWorldIntersection('), 'rw', 'rcx', 'r8')
RayForce(addr('CStateManager::RayStaticIntersection('), 'rs', 'rdx', 'rcx')


# The repulsors: $crep = the first repulsor among the actor's connections (else 0), $anyrep /
# $anyrepuid = the first repulsor in the object list (else 0).
class FindRep(gdb.Command):
    def __init__(self):
        super().__init__('ds45-findrep', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        gdb.set_convenience_variable('crep', 0)
        gdb.set_convenience_variable('anyrep', 0)
        gdb.set_convenience_variable('anyrepuid', 0xFFFF)
        n = int(gdb.parse_and_eval('$t->mConnections.mCount'))
        for i in range(n):
            uid = int(gdb.parse_and_eval(
                '$m->GetIdForScript($t->mConnections.mItems[%d].objId).value' % i))
            name = '-'
            if uid != 0xFFFF:
                e = gdb.parse_and_eval(
                    '((rstl::auto_ptr<CObjectList>*)$m->mObjectLists.mData)[0].mItem'
                    '->mObjects[%d].mEntity' % (uid & 0x7ff))
                if int(e) != 0:
                    name = str(e.dereference().dynamic_type)
                    if 'CScriptRepulsor' in name and var('crep') == 0:
                        gdb.set_convenience_variable('crep', int(e))
            st = int(gdb.parse_and_eval('(int)$t->mConnections.mItems[%d].state' % i))
            ms = int(gdb.parse_and_eval('(int)$t->mConnections.mItems[%d].msg' % i))
            print('conn %d: state %08x msg %08x uid %04x %s' % (i, st & 0xFFFFFFFF,
                                                                ms & 0xFFFFFFFF, uid, name))
        lst = gdb.parse_and_eval('((rstl::auto_ptr<CObjectList>*)$m->mObjectLists.mData)[0].mItem')
        for i in range(2048):
            e = lst['mObjects'][i]['mEntity']
            if int(e) == 0:
                continue
            name = str(e.dereference().dynamic_type)
            if 'CScriptRepulsor' in name:
                uid = int(e['mUniqueId']['value'])
                print('repulsor %04x at %x' % (uid, int(e)))
                if var('anyrep') == 0:
                    gdb.set_convenience_variable('anyrep', int(e))
                    gdb.set_convenience_variable('anyrepuid', uid)


FindRep()


# fn_10_C470: swap a valid decal entry into entry 1 of the response data at $crd ($dswap = its
# index, -1 when there is none); run again to swap back. The shipped DarkSamus sweep-beam CRSC has
# no valid decal, so the entry is taken from the CRSC of one of the actor's projectile weapons
# (a byte swap of the two optional tokens: every lock count stays as it was).
class DecalSwap(gdb.Command):
    def __init__(self):
        super().__init__('ds45-decalswap', gdb.COMMAND_USER)
        self.swap = None

    @staticmethod
    def valid_entries(crsc):
        v = crsc['mDecalTokens']
        items = v['mItems']
        n = int(v['mCount'])
        return int(items), items.dereference().type.sizeof, \
            [i for i in range(n) if int(items[i]['m_valid'])]

    def sources(self, crd):
        yield 'sweep', crd
        lt = gdb.parse_and_eval('$t->mSweepBeamResponse').type

        def colr(wd):
            if int(wd) == 0:
                return None
            c = wd.dereference()['mCOLR']
            if not int(c['m_valid']):
                return None
            item = c['m_data'].address.cast(lt.pointer()).dereference()['mItem']
            return item.dereference() if int(item) != 0 else None

        pit = gdb.lookup_type('CProjectileInfo').pointer()
        for name in ('mScatterShotInfo', 'mScatterShotInfo2', 'mMissileInfo', 'mSuperMissileInfo',
                     'mPhazonShotInfo', 'mPhazonSuperMissileInfo'):
            o = gdb.parse_and_eval('$t->' + name)
            if not int(o['m_valid']):
                continue
            pi = o['m_data'].address.cast(pit).dereference()
            c = colr(pi['mWeaponDescription']['mItem'])
            if c is not None:
                yield name, c
        # the player's beams (the power beam and the selectable ones): their weapon descriptions.
        gun = gdb.parse_and_eval('((CPlayer*)$m->mPlayers[0])->mGun.mPtr')
        if int(gun) == 0:
            return
        beams = [gun['mPowerBeam']['mPtr']]
        sel = gun['mSelectableBeams']
        bt = gdb.lookup_type('CGunWeapon').pointer()
        for i in range(int(sel['mCount'])):
            beams.append(sel['mData'].address.cast(bt.pointer())[i])
        for bi, b in enumerate(beams):
            if int(b) == 0:
                continue
            w = b.dereference()['mWeapons']
            tt = w.type.template_argument(0)
            for k in range(int(w['mCount'])):
                c = colr(w['mData'].address.cast(tt.pointer())[k]['mItem'])
                if c is not None:
                    yield 'beam %d weapon %d' % (bi, k), c

    def invoke(self, arg, from_tty):
        inf = gdb.selected_inferior()
        if self.swap is None:
            crd = gdb.parse_and_eval('*(CCollisionResponseData*)$crd')
            base, size, _ = self.valid_entries(crd)
            gdb.set_convenience_variable('dswap', -1)
            for name, crsc in self.sources(crd):
                sbase, ssize, valid = self.valid_entries(crsc)
                print('decals valid in %s: %s' % (name, valid))
                valid = [i for i in valid if not (sbase == base and i == 1)]
                if valid:
                    self.swap = (base + size, sbase + valid[0] * ssize, size)
                    gdb.set_convenience_variable('dswap', valid[0])
                    break
            if self.swap is None:
                return
            done = False
        else:
            done = True
        a, b, size = self.swap
        ma = bytes(inf.read_memory(a, size))
        mb = bytes(inf.read_memory(b, size))
        inf.write_memory(a, mb)
        inf.write_memory(b, ma)
        if done:
            self.swap = None


DecalSwap()

# fn_10_1707C: IsGunLineClear (fn_10_164C0) answers the next digit of the sequence set with
# `ds45-glc <digits>` (one per call, in call order); an empty sequence leaves the calls alone.
glc_seq = []


class GunLineSeq(gdb.Command):
    def __init__(self):
        super().__init__('ds45-glc', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        glc_seq[:] = [int(c) for c in arg.strip()]


class GunLineForce(gdb.Breakpoint):
    def stop(self):
        if glc_seq:
            v = glc_seq.pop(0)
            OnFinish(gdb.newest_frame(), 'set var $rax = %d' % v)
        return False


GunLineSeq()
GunLineForce(addr('CDarkSamus::IsGunLineClear('), internal=True)

# fn_10_19B64 (and others): `ds45-seq <key> <digits>` makes the keyed bool function answer the
# digits in call order; an empty digit list leaves its calls alone.
seq_funcs = {
    'aim': 'CDarkSamus::IsPlayerAimLineClear(',
    'walk': 'CDarkSamus::CanWalkToPlayer(',
    'ring': 'CDarkSamus::IsRingPathClear(',
    'reach': 'CDarkSamus::IsPlayerInAnimReach(',
    'onpath': 'CDarkSamus::IsOnPath(',
    'walkpt': 'CDarkSamus::CanWalkToPoint(',
}
seqs = {k: [] for k in seq_funcs}
# $seqcaller picks the caller whose direct calls are forced.
seq_callers = {1: 'CDarkSamus::CollectReachAttacks', 2: 'CDarkSamus::PickDash'}


class SeqSet(gdb.Command):
    def __init__(self):
        super().__init__('ds45-seq', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        parts = arg.split()
        seqs[parts[0]][:] = [int(c) for c in (parts[1] if len(parts) > 1 else '')]


class SeqForce(gdb.Breakpoint):
    def __init__(self, key):
        super().__init__(addr(seq_funcs[key]), internal=True)
        self.key = key

    def stop(self):
        q = seqs[self.key]
        if q:
            # only the direct calls of the body under test (a forced call's own callees run free)
            older = gdb.newest_frame().older()
            if older is None or var('seqcaller') == 0 or \
                    seq_callers.get(var('seqcaller'), '') not in (older.name() or ''):
                return False
            v = q.pop(0)
            OnFinish(gdb.newest_frame(), 'set var $rax = %d' % v)
        return False


SeqSet()
for _k in seq_funcs:
    SeqForce(_k)


class Scratch(gdb.Command):
    """ds45-scratch: $scratch = zeroed 0x3000 bytes 2 MB under the stopped thread's stack pointer
    (gdb-called allocators handed back pointers cut to 32 bits); the forced calls' frames stay
    far above it."""

    def __init__(self):
        super().__init__('ds45-scratch', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        sp = int(gdb.parse_and_eval('(unsigned long)$sp'))
        s = (sp - 0x200000) & ~0xfff
        gdb.selected_inferior().write_memory(s, b'\0' * 0x3000)
        gdb.set_convenience_variable(
            'scratch', gdb.Value(s).cast(gdb.lookup_type('char').pointer()))
        gdb.write('ds45 scratch sp %#x -> %#x\n' % (sp, s))


Scratch()

# DS44_GDB names another script of this directory to run instead (ds46: darksamus_ds46_pickdash.gdb).
GDB_SCRIPT = os.environ.get('DS44_GDB', 'darksamus_ds44_calls.gdb')
think = gdb.Breakpoint(addr('CDarkSamus::Think('))
think.ignore_count = int(os.environ.get('DS44_AT', '4600'))
think.commands = ('set logging file %s/ds44_calls.log\nset logging overwrite on\n'
                  'set logging redirect off\nset logging enabled on\n'
                  'source %s/%s\nset logging enabled off\n'
                  'delete %d\ncontinue' % (OUT, HERE, GDB_SCRIPT, think.number))
