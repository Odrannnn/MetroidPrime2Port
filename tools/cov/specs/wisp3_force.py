# WispTentacle (REL 86), wisp3: forced arms of Think, UpdateGrabbedPirate, AcceptScriptMsg and the ctor.
# Instruction-level breakpoints (`loc:*SYMBOL+off`) write registers/members just before a test; every
# force is one-shot per listed hit and logged to <out>/forced.log. Offsets are for build-port-cov at
# 92bcb4775 (re-check with `gdb -batch -ex 'disassemble /s SYMBOL'` after a rebuild).
#   BUILD=$PWD/build-port-cov tools/cov/force.sh tools/cov/specs/wisp3_force.py <out> \
#     3bfa3eff,0t_temple_hall_dark --con tools/drivers/wisp3_hall.con --fast --frames 7900
import os

import gdb

THIS = '((CWispTentacle*)$rdi)'


def _log(msg):
    try:
        with open(os.environ['COV_OUT'] + '/forced.log', 'a') as fh:
            fh.write(msg + '\n')
    except Exception:
        pass


def _set(g, expr):
    try:
        g.execute('set var ' + expr)
    except Exception as e:  # noqa: BLE001
        _log('set failed (%s): %s' % (expr, str(e)[:70]))


def _read(expr):
    try:
        return gdb.parse_and_eval(expr)
    except Exception as e:  # noqa: BLE001
        _log('read failed (%s): %s' % (expr, str(e)[:60]))
        return None


def _sym(demangled_prefix):
    for ln in open(os.environ['COV_NM']):
        parts = ln.rstrip('\n').split('\t')
        if len(parts) == 2:
            f = parts[0].split()
            if len(f) == 3 and f[1] in 'tTW' and parts[1].startswith(demangled_prefix) \
                    and '.cold' not in parts[1] and 'clone' not in parts[1]:
                return f[2]
    return None


def _save(st, member, value, base=THIS):
    old = _read(base + '->' + member)
    if old is None:
        return
    st.restore.append('%s->%s = %s' % (base, member, str(old)))
    _set(gdb, '%s->%s = %s' % (base, member, value))


class OneShot:
    def __init__(self, hits):
        self.n = 0
        self.hits = hits
        self.restore = []

    def __call__(self, g):
        for expr in self.restore:
            _set(g, expr)
        self.restore = []
        self.n += 1
        fn = self.hits.get(self.n)
        if fn is not None:
            fn(g, self)


# ---- Think (fn_86_13D4): 558 inactive arm, 590 mPirateCaptured with no pirate (UpdateGrabbedPirate's 479)
think = OneShot({4: lambda g, st: _save(st, 'mActive', '0'),
                 9: lambda g, st: _save(st, 'mPirateCaptured', '1'),
                 15: lambda g, st: _save(st, 'mActive', '0'),
                 25: lambda g, st: _save(st, 'mPirateCaptured', '1')})

# 576/585 `GetLocatorSegId(..).val() != 0xff` false arms, first uncached Think only.
_loc = {'n1': 0, 'n2': 0}


def think_lockon(g):
    _loc['n1'] += 1
    if _loc['n1'] == 1:
        _set(g, '$r13 = 0xff')
        _log('13D4 lockOnSeg forced to 0xff (576)')


def think_armend(g):
    _loc['n2'] += 1
    if _loc['n2'] == 1:
        _set(g, '*(unsigned char*)($rsp+0x60) = 0xff')
        _log('13D4 armEndSeg forced to 0xff (585)')


# ---- AcceptScriptMsg (fn_86_2A18)
def _msg():
    m = _read('((CScriptMsg*)$rdx)->m_msg')
    return None if m is None else int(m) & 0xFFFFFFFF


_lights = {'restore': None, 'n': 0}


def as_lights(g):
    # 242 kSM_Activate's `if (ActorLights())` false arm (Create's 211 is followed by an unguarded
    # ActorLights()-> use, so only Activate is forced)
    if _lights['restore'] is not None:
        for e in _lights['restore']:
            _set(g, e)
        _lights['restore'] = None
    if _msg() == 0x41435456:
        _lights['n'] += 1
        if _lights['n'] in (1, 3):
            item = _read(THIS + '->mActorLights.mPtr')
            if item is None:
                return
            _lights['restore'] = ['%s->mActorLights.mPtr = %s' % (THIS, str(item))]
            _set(g, '%s->mActorLights.mPtr = 0' % THIS)
            _log('2A18 Activate with ActorLights() null (242)')


_coll = {'restore': None, 'addr': None}


def as_null_collision(g):
    m = _msg()
    if m == 0x58415544:
        if _coll['restore'] is None:
            old = _read(THIS + '->mCollisionManager.mPtr')
            if old is not None:
                _coll['restore'] = str(old)
                _coll['addr'] = int(_read('$rdi'))
                _set(g, '%s->mCollisionManager.mPtr = 0' % THIS)
                _log('2A18 AIUpdateDisabled with mCollisionManager null (253)')
    elif _coll['restore'] is not None:
        think_restore_collision(g)


def think_restore_collision(g):
    # Think dereferences the manager unguarded: put it back before any body runs
    if _coll['restore'] is not None:
        _set(g, '((CWispTentacle*)%d)->mCollisionManager.mPtr = %s' % (_coll['addr'], _coll['restore']))
        _coll['restore'] = None


# ---- ctor (fn_86_3628): instruction-level forces
_c = {'portal': 0, 'this': None, 'old': None, 'inf': 0, 'eps': 0}


def ctor_portal(g):
    _c['portal'] += 1
    if _c['portal'] == 1:
        rbx = int(_read('$rbx'))
        _c['this'] = rbx
        _c['old'] = int(_read('*(unsigned char*)($rbx+0x8a9)'))
        _set(g, '*(unsigned char*)($rbx+0x8a9) = 1')
        _log('3628 mSpawnFromPortal forced 1 (110)')


def ctor_join(g):
    if _c['this'] is not None:
        _set(g, '*(unsigned char*)(%d+0x8a9) = %d' % (_c['this'], _c['old']))
        _c['this'] = None


def ctor_inf(g):
    _c['inf'] += 1
    if _c['inf'] in (1, 5):
        _set(g, '$al = 0')
        _log('3628 CCharAnimTime != Infinity forced false (124)')


def ctor_eps(g):
    _c['eps'] += 1
    if _c['eps'] in (1, 4):
        _set(g, '$xmm0.v4_float[0] = 0')
        _log('3628 best.first forced 0 (127)')



# ---- wisp3b: 225/266 (last-touched object) and 176/177/194/211 (Create) at host instructions.
# Offsets are AcceptScriptMsg+off in build-port-cov at 82625c718 (`gdb -batch -ex 'disassemble /s ...'`).
#   +0x169 je (mModelData null)      +0x176 je (animation data null)   +0x19a jne (optional spatial primitive set)
#   +0x213 je (ActorLights null)     +0xd8f jne (CActor cast ok)
#   +0x370 mov %eax,%esi (HitObject touched id)    +0x3b4 cmp $4,%edx (225's mState == Flinch/Withdraw tests)
#   +0x5ba mov %eax,%r13d (Damage touched id)      +0x609 or %r14,%rax (bomb || powerBomb)
# Create entries sent `from u044e` (tools/drivers/wisp3_hall.con) are counted: Create k forces arm k.
_cr = {'k': 0, 'cur': 0}
_h = {'n': 0, 'open': False}
_d = {'n': 0, 'open': False}
BOMB_UID = 0x6a0  # `objs CBomb` at f7940 of the driver (ids repeat run to run)


def as_entry(g):
    m = _msg()
    sender = _read('((CScriptMsg*)$rdx)->mSenderId.value')
    if m == 0x58435254 and sender is not None and int(sender) == 0x44e:
        _cr['k'] += 1
        _cr['cur'] = _cr['k']
        if _cr['k'] == 6:  # opens the Damage window (the live bomb)
            _d['n'] = 0
            _d['open'] = True
        if _cr['k'] == 7:  # opens the HitObject window
            _h['n'] = 0
            _h['open'] = True
        _log('2A18 manual Create #%d' % _cr['k'])
    else:
        _cr['cur'] = 0


def _zf(g, k, label):
    if _cr['cur'] == k:
        _set(g, '$eflags = ((int)$eflags | 0x40)')
        _log('2A18 forced (%s)' % label)


def cr1(g): _zf(g, 1, 'send Create from u044e: mModelData null, 176')
def cr2(g): _zf(g, 2, 'send Create from u044e: animation data null, 177')
def cr3(g): _zf(g, 3, 'send Create from u044e: spatial primitive optional empty, 177')
def cr4(g): _zf(g, 4, 'send Create from u044e: collision actor not a CActor, 194')
def cr5(g): _zf(g, 5, 'send Create from u044e: ActorLights null, 211')


def hit_touched(g):
    if not _h['open']:
        return
    _h['n'] += 1
    if _h['n'] in (2, 3):
        pid = _read('((CStateManager*)$r12)->mPlayers[0]->mUniqueId.value')
        if pid is not None:
            _set(g, '$eax = %d' % int(pid))
            _log('2A18 forced (HitObject last-touched id = player %d, hit %d)' % (int(pid), _h['n']))


def hit_state(g):
    if not _h['open']:
        return
    if _h['n'] == 2:
        _set(g, '$edx = 4')
        _log('2A18 forced (mState = kS_Flinch at the 225 test)')
    elif _h['n'] == 3:
        _set(g, '$edx = 6')
        _log('2A18 forced (mState = kS_Withdraw at the 225 test)')


_dm = {'bomb': 0, 'none': 0, 'dbg': 0, 'tries': 0}


def _find_bomb(g):
    # the live CBomb's id: walk the manager's all-objects list (no inferior calls), match the vtable address
    try:
        vt = int(gdb.parse_and_eval("(long)&'vtable for CBomb'"))
        lst = gdb.parse_and_eval('**(CObjectList**)(((CStateManager*)$r12)->mObjectLists.mData + 8)')
        for idx in range(2048):
            ent = int(lst['mObjects'][idx]['mEntity'])
            if ent == 0:
                continue
            v = int(gdb.parse_and_eval('*(long*)%d' % ent))
            if vt <= v <= vt + 0x40:
                uid = int(gdb.parse_and_eval('((CEntity*)%d)->mUniqueId.value' % ent))
                _log('findbomb: slot %d ent 0x%x uid 0x%x (vtable CBomb 0x%x, object vptr 0x%x)' % (idx, ent, uid, vt, v))
                return uid
        if _dm['dbg'] < 12:
            _dm['dbg'] += 1
            nz = [i for i in range(2048) if int(lst['mObjects'][i]['mEntity'])]
            hi = [(i, hex(int(gdb.parse_and_eval('*(long*)%d' % int(lst['mObjects'][i]['mEntity']))))) for i in nz if i >= 0x290]
            _log('findbomb: no CBomb vtable 0x%x; list0 count %d, slots>=0x290: %s' % (vt, len(nz), hi[:12]))
    except Exception as e:  # noqa: BLE001
        _log('findbomb failed: %s' % str(e)[:120])
    return None


def dmg_touched(g):
    # Damage/ResistedDamage past the state test: first hit with a live CBomb gets its id as the last-touched
    # object (the casts then run for real: CBomb non-null); the next hit gets 0xffff (neither cast succeeds).
    if not _d['open'] or _dm['tries'] > 8:
        return
    _dm['tries'] += 1
    if not _dm['bomb']:
        uid = _find_bomb(g)
        if uid is not None:
            _dm['bomb'] = 1
            _set(g, '$eax = %d' % uid)
            _log('2A18 forced (Damage last-touched id = live CBomb %d)' % uid)
    elif not _dm['none']:
        _dm['none'] = 1
        _set(g, '$eax = 0xffff')
        _log('2A18 forced (Damage last-touched id = none 0xffff: neither CBomb nor CPowerBomb, 266)')


def dmg_state(g):
    # Damage arrives while the wisp is in Sleep/Spawn/Withdraw/Flinch: present a state the 260 test lets through
    if _d['open'] and _dm['dbg'] < 3:
        _dm['dbg'] += 1
        try:
            sid = int(gdb.parse_and_eval('((CScriptMsg*)$r15)->mSenderId.value'))
            lst = gdb.parse_and_eval('**(CObjectList**)(((CStateManager*)$r12)->mObjectLists.mData + 8)')
            ent = int(lst['mObjects'][sid & 0x7ff]['mEntity'])
            sym = g.execute('info symbol *(long*)%d' % ent, to_string=True) if ent else 'null'
            _log('dstate dbg: sender 0x%x slot ent 0x%x %s' % (sid, ent, sym.strip()[:60]))
        except Exception as e:  # noqa: BLE001
            _log('dstate dbg failed: %s' % str(e)[:100])
    if _d['open'] and not _dm['none'] and _dm['tries'] <= 8:
        _set(g, '$eax = 1')
        _log('2A18 forced (Damage: mState = kS_Patrol-like non-excluded state at the 260 test)')


def dmg_or(g):
    pass


CLASS = 'CWispTentacle'
_ctor = _sym('CWispTentacle::CWispTentacle(')
_think = _sym('CWispTentacle::Think(')
_as = _sym('CWispTentacle::AcceptScriptMsg(')
FORCE = [
    ('13D4 Think inactive/captured', 'CWispTentacle::Think(', [], [think], 0,
     'mActive clear (558), mPirateCaptured set with no pirate (590 -> 479)'),
    ('13D4 Think restore', 'CWispTentacle::Think(', [], [think_restore_collision], 0, 'restore'),
    ('13D4 Think lockon', 'loc:*' + _think + '+0x2a1', [], [think_lockon], 0, 'lockOnSeg 0xff'),
    ('13D4 Think armend', 'loc:*' + _think + '+0x2f4', [], [think_armend], 0, 'armEndSeg 0xff'),
    ('2A18 lights', 'CWispTentacle::AcceptScriptMsg(', [], [as_entry, as_lights, as_null_collision], 0,
     'Activate without lights, AIUpdateDisabled without manager'),
    ('2A18 cr1', 'loc:*' + _as + '+0x169', [], [cr1], 0, 'Create mModelData null'),
    ('2A18 cr2', 'loc:*' + _as + '+0x176', [], [cr2], 0, 'Create anim data null'),
    ('2A18 cr3', 'loc:*' + _as + '+0x19a', [], [cr3], 0, 'Create no spatial primitive'),
    ('2A18 cr4', 'loc:*' + _as + '+0xd8f', [], [cr4], 0, 'Create collision actor not CActor'),
    ('2A18 cr5', 'loc:*' + _as + '+0x213', [], [cr5], 0, 'Create ActorLights null'),
    ('2A18 touched', 'loc:*' + _as + '+0x370', [], [hit_touched], 0, 'HitObject touched = player'),
    ('2A18 state', 'loc:*' + _as + '+0x3b4', [], [hit_state], 0, '225 state Flinch/Withdraw'),
    ('2A18 dtouched', 'loc:*' + _as + '+0x5ba', [], [dmg_touched], 0, 'Damage touched = bomb'),
    ('2A18 dstate', 'loc:*' + _as + '+0x552', [], [dmg_state], 0, 'Damage state past 260 test'),
    ('2A18 dor', 'loc:*' + _as + '+0x609', [], [dmg_or], 0, 'powerBomb non-null'),
    ('3628 portal', 'loc:*' + _ctor + '+0x243', [], [ctor_portal], 0, 'portal arm'),
    ('3628 join', 'loc:*' + _ctor + '+0x392', [], [ctor_join], 0, 'restore'),
    ('3628 inf', 'loc:*' + _ctor + '+0x4e3', [], [ctor_inf], 0, 'time == Infinity'),
    ('3628 eps', 'loc:*' + _ctor + '+0x544', [], [ctor_eps], 0, 'best <= eps'),
]
