# ds38: the CScattershotProjectile arms (PortCScattershotProjectile.cpp) the rooftop fight does not
# take, forced on the coverage build. Run with tools/cov/force_ret.py as the gdb side:
#   COV_GDB_SCRIPT=tools/cov/force_ret.py BUILD=$PWD/build-port-cov MP2_INPUT=LAYER2@400 \
#     tools/cov/force.sh tools/cov/specs/darksamus_scat_arms.py <out> "1baa96c2,10_cliff_rooftop" \
#     --con tools/drivers/darksamus_scat.con --fast --frames 2000
#   then tools/cov/gcov_report.py build-port-cov <out>/gcov [<natural run>/gcov] --module DarkSamus
# FORCE_RET (written at the entry, put back at the return):
#   C98 AddShot          every 7th call: 300 shots (the cap at 0x00000CA8)
#   2C CheckPlayerHit    every 11th: the last shot's target = its position (0x000000A0)
#   2254 UpdateShots     every 13th: the same (the CanBeNormalized arm at 0x00002334)
#   213C / 1D8C          the description's COLR absent (0x00002170 / 0x00001DC4); 1D8C's shot
#                        flown 1.0f (0x00001DB8; the shot is dead by then, not put back)
#   2798 InitRenderParams  call 3k: B1PO/B2PO/TLPO = the description's IVEC (null in the data);
#                          3k+1: every sub-element null; 3k+2: the three textures null
#   2254 UpdateShots call 13k+3: the last shot 0.001 short of its target and still to sweep
# Custom breakpoints below (each logged to <out>/scat.log):
#   GetSoundEffectId from PlayHitSfx: every 2nd returns -1 (0x00002198)
#   GetParticleDescription from SpawnHitExplosion: every 3rd returns an empty optional (0x00001E58;
#     the locked token inside is leaked, so the resource stays loaded for the rest of the run)
#   GetVulnerability from CanHit: every 5th returns effect 2 = pass-through (0x00000BC4)
#   Sweep (fn_10_324): call 9k+1: to = from (0x000003EC), put back at the return;
#     every 3rd call appends a stale uid (the own uid's generation + 1: ObjectById null, 0x0000057C)
#     and the projectile's own uid to the near list; CanHit
#     returns true for the own uid and its touch bounds are replaced, by mode (cycling 1..5):
#     1 a box around `from` (the ray starts inside: no hit, PointInside, 0x00000888)
#     2 a box 1% along the ray (a hit nearer than the world's, 0x00000824 taken)
#     3 a box 99.5% along the ray (a hit past the world's, or none past it)
#     4 a box 50 units off the ray (no hit, PointInside false)
#     5 no touch bounds (0x0000074C)
import os
import gdb

CLASS = 'CScattershotProjectile'
SH = '((CScattershotProjectile::SShot*)$this->mShots.mData)[$this->mShots.mCount - 1]'


def same_target(period, phase):
    return (period, [(phase, [('%s.mTarget.m%s' % (SH, c), '%s.mPos.m%s' % (SH, c)) for c in 'XYZ'])])


DESC = '$this->mDesc'
FORCE_RET = [
    ('C98 AddShot cap (forced)', 'CScattershotProjectile::AddShot(', 7,
     [(3, [('$this->mShots.mCount', '300')])], '300 shots: the early return at 0x00000CA8'),
    ('2C target = pos (forced)', 'CScattershotProjectile::CheckPlayerHit(', *same_target(11, 5),
     'the last shot has no direction (0x000000A0)'),
    ('2254 target = pos / pass with sweep (forced)', 'CScattershotProjectile::UpdateShots(', 13,
     [same_target(13, 7)[1][0],
      (3, [('%s.mTarget.mX' % SH, '%s.mPos.mX + 0.001' % SH), ('%s.mTarget.mY' % SH, '%s.mPos.mY' % SH),
           ('%s.mTarget.mZ' % SH, '%s.mPos.mZ' % SH), ('%s.mFlags' % SH, '%s.mFlags | 4' % SH)])],
     'no direction (0x00002334); 3: passes a target still to sweep (0x00002404 taken)'),
    ('213C no COLR (forced)', 'CScattershotProjectile::PlayHitSfx(', 3,
     [(1, [('%s->mCOLR.m_valid' % DESC, '0')])], 'COLR absent (0x00002170)'),
    ('1D8C no COLR / short (forced)', 'CScattershotProjectile::SpawnHitExplosion(', 4,
     [(1, [('%s->mCOLR.m_valid' % DESC, '0')]),
      (2, [('!((CScattershotProjectile::SShot*)$rdx)->mDistance', '1.0')])],
     'COLR absent (0x00001DC4); the shot flew 1.0f (0x00001DB8)'),
    ('2798 elements null / PO set (forced)', 'CScattershotProjectile::InitRenderParams(', 3,
     [(0, [('%s->m%s' % (DESC, e), '%s->mIVEC ? %s->mIVEC : %s->mIORN' % (DESC, DESC, DESC))
           for e in ('B1PO', 'B2PO', 'TLPO')]),
      (1, [('%s->m%s' % (DESC, e), '0') for e in ('B1SE', 'B1PO', 'B2SE', 'B2PO', 'TSZE', 'TLEN', 'TLPO')]),
      (2, [('%s->m%s' % (DESC, e), '0') for e in ('B1TX', 'B2TX', 'TTEX')])],
     '3k+1: the sub-elements null; 3k+2: the textures null'),
]

OUT = os.environ['COV_OUT']
LOGF = os.path.join(OUT, 'scat.log')
logged = {}


def slog(key, msg):
    logged[key] = logged.get(key, 0) + 1
    if logged[key] <= 4:
        with open(LOGF, 'a') as fh:
            fh.write('%s #%d: %s\n' % (key, logged[key], msg))


def dump_counts():
    with open(os.path.join(OUT, 'scat_counts.txt'), 'w') as fh:
        for k, v in sorted(logged.items()):
            fh.write('%-30s %d\n' % (k, v))


gdb.events.exited.connect(lambda ev: dump_counts())

_syms = []
for ln in open(os.environ['COV_NM']):
    parts = ln.rstrip('\n').split('\t')
    if len(parts) == 2:
        f = parts[0].split()
        if len(f) == 3 and f[1] in 'tTW':
            _syms.append((f[2], parts[1]))


def entries(prefix):
    hits = ['*' + m for m, d in _syms if d.startswith(prefix) and '.cold' not in d and 'clone' not in d]
    if not hits:
        slog('missing', prefix)
    return hits


def caller():
    try:
        return gdb.newest_frame().older().name() or ''
    except (gdb.error, AttributeError):
        return ''


def reg(name):
    return int(gdb.parse_and_eval('$' + name))


def rdf(addr, i):
    return float(gdb.parse_and_eval('*(float*)%d' % (addr + 4 * i)))


def wrf(addr, i, v):
    gdb.execute('set var *(float*)%d = %r' % (addr + 4 * i, float(v)))


def valid_offset(frame):
    rt = frame.function().type.target().strip_typedefs()
    return [f for f in rt.fields() if f.name == 'm_valid'][0].bitpos // 8


state = {'sweep': None, 'sfx': 0, 'gen': 0, 'vuln': 0, 'nsweep': 0, 'mode': 0}


class SafeBp(gdb.Breakpoint):
    # A Python error inside stop() would halt the game: log it and run on.
    def stop(self):
        try:
            self.go()
        except Exception as e:
            slog('error', '%s: %s' % (type(self).__name__, str(e)[:120]))
        return False


class SetRet(gdb.FinishBreakpoint):
    def __init__(self, frame, fn):
        super().__init__(frame, internal=True)
        self.fn = fn

    def stop(self):
        try:
            self.fn()
        except Exception as e:
            slog('error', 'finish: %s' % str(e)[:120])
        return False

    def out_of_scope(self):
        slog('out_of_scope', 'a finish breakpoint left its frame')


class SfxBp(SafeBp):
    def go(self):
        if 'PlayHitSfx' in caller():
            state['sfx'] += 1
            if state['sfx'] % 2 == 0:
                def ret():
                    slog('sfx -1', 'GetSoundEffectId %d -> -1' % (reg('rax') & 0xffffffff))
                    gdb.execute('set var $rax = 0xffffffff')
                SetRet(gdb.newest_frame(), ret)
        return False


class GenBp(SafeBp):
    def go(self):
        if 'SpawnHitExplosion' in caller():
            state['gen'] += 1
            if state['gen'] % 3 == 0:
                fr = gdb.newest_frame()
                addr, off = reg('rdi'), valid_offset(fr)

                def ret():
                    slog('gen empty', 'optional at 0x%x valid %d -> 0' %
                         (addr, int(gdb.parse_and_eval('*(unsigned char*)%d' % (addr + off)))))
                    gdb.execute('set var *(unsigned char*)%d = 0' % (addr + off))
                SetRet(fr, ret)
        return False


class VulnBp(SafeBp):
    def go(self):
        c = caller()
        if 'CScattershotProjectile' in c:
            state['vuln'] += 1
            if state['vuln'] % 5 == 0:
                def ret():
                    rax = reg('rax') & 0xffffffffffffffff
                    slog('vuln passthrough', 'rax 0x%x (%s)' % (rax, c))
                    gdb.execute('set var $rax = %d' % ((rax & 0xffffffff) | (2 << 32)))
                SetRet(gdb.newest_frame(), ret)
        return False


class SweepBp(SafeBp):
    def go(self):
        state['nsweep'] += 1
        this, frm, to, near = reg('rsi'), reg('rdx'), reg('rcx'), reg('r8')
        if state['nsweep'] % 9 == 1:
            old = [rdf(to, i) for i in range(3)]
            for i in range(3):
                wrf(to, i, rdf(frm, i))
            slog('sweep to=from', 'to %r' % old)

            def back():
                for i in range(3):
                    wrf(to, i, old[i])
            SetRet(gdb.newest_frame(), back)
            return False
        if state['nsweep'] % 3 != 0:
            return False
        nl = '((rstl::reserved_vector<TUniqueId, 2048>*)%d)' % near
        cnt = int(gdb.parse_and_eval('%s->mCount' % nl))
        if cnt >= 2047:
            return False
        uid = int(gdb.parse_and_eval('*(unsigned short*)&((CEntity*)%d)->mUniqueId' % this))
        stale = (uid + 0x800) & 0xffff
        gdb.execute('set var *(unsigned short*)((char*)%s->mData + 2 * %d) = %d' % (nl, cnt, stale))
        gdb.execute('set var *(unsigned short*)((char*)%s->mData + 2 * %d) = %d' % (nl, cnt + 1, uid))
        gdb.execute('set var %s->mCount = %d' % (nl, cnt + 2))
        state['mode'] = state['mode'] % 5 + 1
        state['sweep'] = (this, [rdf(frm, i) for i in range(3)], [rdf(to, i) for i in range(3)])
        slog('sweep mode %d' % state['mode'], 'uid 0x%x appended at %d' % (uid, cnt))

        def done():
            state['sweep'] = None
        SetRet(gdb.newest_frame(), done)
        return False


class CanHitBp(SafeBp):
    def go(self):
        sw = state['sweep']
        if sw is not None and reg('rsi') == sw[0]:
            def ret():
                gdb.execute('set var $rax = 1')
            SetRet(gdb.newest_frame(), ret)
        return False


class TouchBp(SafeBp):
    def go(self):
        sw = state['sweep']
        if sw is None or reg('rsi') != sw[0] or 'Sweep' not in caller():
            return False
        fr = gdb.newest_frame()
        addr, off, mode = reg('rdi'), valid_offset(fr), state['mode']
        _, frm, to = sw
        d = [to[i] - frm[i] for i in range(3)]

        def box(t, side):
            c = [frm[i] + t * d[i] for i in range(3)]
            c[2] += side
            return [c[i] - 0.25 for i in range(3)] + [c[i] + 0.25 for i in range(3)]

        def ret():
            if mode == 5:
                gdb.execute('set var *(unsigned char*)%d = 0' % (addr + off))
                return
            b = {1: box(0.0, 0.0), 2: box(0.01, 0.0), 3: box(0.995, 0.0), 4: box(0.5, 50.0)}[mode]
            for i in range(6):
                wrf(addr, i, b[i])
            gdb.execute('set var *(unsigned char*)%d = 1' % (addr + off))
        SetRet(fr, ret)
        return False


class EyeBp(SafeBp):
    # UpdateShots' `toEye.CanBeNormalized()` (the eye at the box centre): every 40th pass at the
    # test, toEye is zeroed in place.
    def go(self):
        state['eye'] = state.get('eye', 0) + 1
        if state['eye'] % 40 == 0:
            for c in 'XYZ':
                gdb.execute('set var toEye.m%s = 0' % c)
            slog('toEye zero', str(gdb.parse_and_eval('toEye')))


EyeBp('PortCScattershotProjectile.cpp:442', internal=True)
for s in entries('CCollisionResponseData::GetSoundEffectId('):
    SfxBp(s, internal=True)
for s in entries('CCollisionResponseData::GetParticleDescription('):
    GenBp(s, internal=True)
for s in entries('CDamageVulnerability::GetVulnerability('):
    VulnBp(s, internal=True)
for s in entries('CScattershotProjectile::Sweep('):
    SweepBp(s, internal=True)
for s in entries('CScattershotProjectile::CanHit('):
    CanHitBp(s, internal=True)
for s in entries('CScattershotProjectile::GetTouchBounds('):
    TouchBp(s, internal=True)
