# Rezbit (REL 53) rez5: direct-call forcing of the leaf bodies rez4 left (UpdateShield, LowerShields,
# BreakShield, UpdateSeparation, CheckVerticalClearance, JoinTeam, UpdatePatrolMovement,
# SetupLaserTargets, UpdateLaser, ...). gdb is the whole driver: at the REZ5_AT-th live
# CRezbit::Think entry it writes the members each arm needs, `call`s the body, and writes them back
# (the spl5 / dtr9 way); every section ends with PortCoverageFlush() (= __gcov_dump + reset), so a
# stalled or killed run keeps what ran. Each section's results go to <out>/rez5.log.
#   COV_GDB_SCRIPT=$PWD/tools/cov/specs/rezbit_force3.py BUILD=$PWD/build-port-cov MP2_CONSOLE_WAIT=120 \
#     SECS=600 tools/cov/force.sh tools/cov/specs/rezbit_force3.py <out> 1baa96c2,02_cliff_reactorcore \
#     --con tools/drivers/rezbit_forced4.con --fast --frames 1800
import os
import gdb

CLASS = 'CRezbit'
FORCE = []
OUT = os.environ['COV_OUT']
LOG = OUT + '/rez5.log'

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


def log(msg):
    with open(LOG, 'a') as fh:
        fh.write(msg + '\n')


def run(cmd):
    try:
        out = gdb.execute(cmd, to_string=True)
        log('> %s\n%s' % (cmd, out.rstrip()))
        return out
    except gdb.error as e:
        log('> %s\n  ERROR %s' % (cmd, e))
        return ''


def ev(expr):
    return gdb.parse_and_eval(expr)


def flush():
    run('call (void)PortCoverageFlush()')


gdb.execute('set $busy = 0')
gdb.execute('set $done = 0')
gdb.execute('set $tvalid = 0')
gdb.execute('set $fbsd = -99')   # CPatterned::FindBestStepDirection answer, -99 = real

# members (lvalues; `$t` is the Rezbit under test, `$m` the CStateManager)
BRK = '$t->mShieldBroken'
ALERT = '$t->mAlert'
RZ = '$t->mRezState'
AK = '$t->mAttackKind'
TMR = '$t->mShieldTimer'
SID = '$t->mShieldActorId.value'
SFX = '*(unsigned*)&$t->mShieldSfx'
AD = '$t->mBodyController.mPtr->mBodyStateInfo.mAdditiveState'
TID = '$t->mTargetId.value'
EFF = '$t->mShieldExplodeEffect.m_valid'
PLID = '$m->mPlayers[0]->mUniqueId.value'
FLOATS = (TMR,)


def snap(lvs):
    d = {}
    for lv in lvs:
        v = ev(lv)
        d[lv] = float(v) if lv in FLOATS else int(v)
    return d


def put(d):
    for lv, v in d.items():
        run('set var %s = %r' % (lv, v))


def w(lv, v):
    run('set var %s = %s' % (lv, v))


class FbsdRet(gdb.FinishBreakpoint):
    def stop(self):
        gdb.execute('set var $rax = $fbsd')
        return False

    def out_of_scope(self):
        pass


class FbsdBp(gdb.Breakpoint):
    def stop(self):
        if int(gdb.convenience_variable('fbsd')) != -99:
            FbsdRet(gdb.newest_frame(), internal=True)
        return False


FbsdBp(addr('CPatterned::FindBestStepDirection('), internal=True)

class GatRet(gdb.FinishBreakpoint):
    def stop(self):
        gdb.execute('set var $rax = $gat')
        return False

    def out_of_scope(self):
        pass


class GatBp(gdb.Breakpoint):
    def stop(self):
        if int(gdb.convenience_variable('gat')) != -99:
            GatRet(gdb.newest_frame(), internal=True)
        return False


gdb.execute('set $gat = -99')
GatBp(addr('CScriptTeamAiMgr::GetAssociatedTeamId('), internal=True)

SHIELD_LVS = [BRK, ALERT, RZ, AK, TMR, SID, SFX, AD]


def shield_group():
    base = snap(SHIELD_LVS)
    sid0 = base[SID]

    def us(label, brk, rz, alert, ak, tmr, ad, sid=None, sfx=0):
        log('# UpdateShield %s' % label)
        put({BRK: brk, RZ: rz, ALERT: alert, AK: ak, TMR: tmr, AD: ad, SID: sid0 if sid is None else sid, SFX: sfx})
        run('call $t->UpdateShield(*$m, 0.016f)')
        put(base)

    for ad in (28, 0):
        us('broken ad=%d' % ad, 1, 0, 1, 0, 1e9, ad)
        us('rez1 ad=%d' % ad, 0, 1, 1, 0, 1e9, ad)
        us('no alert ad=%d' % ad, 0, 0, 0, 0, 1e9, ad)
        us('attack kind 2 ad=%d' % ad, 0, 0, 1, 2, 1e9, ad)
        us('timer running ad=%d' % ad, 0, 0, 1, 0, 1e9, ad)
        us('timer elapsed ad=%d' % ad, 0, 0, 1, 0, -1.0, ad)
        us('ak 5 timer elapsed ad=%d' % ad, 0, 0, 1, 5, -1.0, ad)
        us('ak -1 timer elapsed ad=%d' % ad, 0, 0, 1, -1, -1.0, ad)
        us('no shield actor ad=%d' % ad, 0, 0, 1, 0, 1e9, ad, sid=0xffff)
        us('sfx live ad=%d' % ad, 0, 0, 1, 0, 1e9, ad, sfx=0x1234)
        us('sfx live no shield ad=%d' % ad, 0, 0, 1, 0, 1e9, ad, sid=0xffff, sfx=0x1234)
    flush()

    def ls(label, brk, ad, sid=None, sfx=0):
        log('# LowerShields %s' % label)
        put({BRK: brk, AD: ad, SID: sid0 if sid is None else sid, SFX: sfx})
        run('call $t->LowerShields(*$m)')
        put(base)

    ls('broken', 1, 28)
    for ad in (28, 0):
        for sid in (None, 0xffff):
            for sfx in (0, 0x1234):
                ls('ad=%d sid=%s sfx=%#x' % (ad, sid, sfx), 0, ad, sid, sfx)
    flush()

    def bs(label, brk, ad, sid=None, sfx=0, eff=None):
        log('# BreakShield %s' % label)
        e0 = int(ev(EFF))
        put({BRK: brk, AD: ad, SID: sid0 if sid is None else sid, SFX: sfx})
        if eff is not None:
            w(EFF, eff)
        run('call $t->BreakShield(*$m)')
        w(EFF, e0)
        put(base)

    bs('already broken', 1, 28)
    bs('loop reaction, sfx live, effect valid', 0, 28, sfx=0x1234)
    bs('idle, no sfx, effect valid', 0, 0)
    bs('effect invalid, sfx live', 0, 0, sfx=0x1234, eff=0)
    bs('effect invalid, no sfx', 0, 0, eff=0)
    bs('no shield actor', 0, 28, sid=0xffff)
    flush()


def misc_group():
    # --- UpdateSeparation --------------------------------------------------------------------------
    log('# UpdateSeparation')
    run('call $t->UpdateSeparation(*$m)')
    a0 = int(ev('$t->mAreaId.value'))
    w('$t->mAreaId.value', a0 + 1)
    run('call $t->UpdateSeparation(*$m)')
    w('$t->mAreaId.value', a0)
    run('set $ol = *(CObjectList**)($m->mObjectLists.mData + 16 * (int)kOL_ListeningAi + 8)')
    i = int(ev('$ol->mFirstId'))
    others = []
    while i != -1:
        e = ev('$ol->mObjects[%d].mEntity' % i)
        if int(e) != int(ev('$t')):
            others.append((i, int(e)))
        i = int(ev('$ol->mObjects[%d].mNext' % i))
    log('# listening AI: %d others' % len(others))
    px = [float(ev('$t->mPosition.m' + c)) for c in 'XYZ']
    for n, (idx, ptr) in enumerate(others[:3]):
        log('# UpdateSeparation next to listener %d (%#x)' % (n, ptr))
        for j, c in enumerate('XYZ'):
            ov = float(ev('((CActor*)%d)->mPosition.m%s' % (ptr, c)))
            w('$t->mPosition.m' + c, ov + (0.5 if j == 0 else 0.0))
        run('call $t->UpdateSeparation(*$m)')
    for j, c in enumerate('XYZ'):
        w('$t->mPosition.m' + c, px[j])
    flush()
    # --- JoinTeam ------------------------------------------------------------------------------------
    t0 = int(ev('$t->mTeamAiMgrId.value'))
    for label, val in (('invalid', 0xffff), ('stale id', 0xfffe), ('self (not a team)', int(ev('$t->mUniqueId.value'))),
                       ('as found', t0)):
        log('# JoinTeam %s' % label)
        w('$t->mTeamAiMgrId.value', val)
        run('call $t->JoinTeam(*$m)')
    log('# JoinTeam lookup finds no team')
    w('$t->mTeamAiMgrId.value', 0xffff)
    run('set $gat = 65535')
    run('call $t->JoinTeam(*$m)')
    run('set $gat = -99')
    w('$t->mTeamAiMgrId.value', t0)
    flush()
    # --- CheckVerticalClearance -----------------------------------------------------------------------
    run('set $av = (unsigned char*)malloc(16)')
    tid0 = int(ev(TID))
    sd0 = float(ev('$t->mStrafeDistance'))
    posz = float(ev('$t->mPosition.mZ'))

    def setz(z):
        w('$t->mPosition.mZ', z)

    def cvc(label):
        for i in range(4):
            w('$av[%d]' % (4 + i), 1)
        w('*(int*)$av', 4)
        log('# CheckVerticalClearance %s' % label)
        run('call $t->CheckVerticalClearance(*$m, *(rstl::reserved_vector<bool,4>*)$av)')

    w(TID, 0xffff)
    cvc('no target')
    w(TID, PLID)
    pz = float(ev('$m->mPlayers[0]->mPosition.mZ'))
    for dz in (30.0, 10.0, 5.0, 0.0, -5.0, -100.0, 1000.0):
        setz(pz + dz)
        for sd in (-100.0, 0.0, 5.0, 10.0, 100.0):
            w('$t->mStrafeDistance', sd)
            cvc('dz=%s sd=%s' % (dz, sd))
    w('$t->mPosition.mZ', posz)
    w('$t->mStrafeDistance', sd0)
    w(TID, tid0)
    flush()
    # --- UpdatePatrolMovement --------------------------------------------------------------------------
    mv = '$t->mBodyController.mPtr->mCmdMgr.mMove'
    old = [float(ev(mv + '.m' + c)) for c in 'XYZ']
    for label, vec in (('safe', (1.0, 2.0, 0.5)), ('unsafe', (0.0, 0.0, 0.0))):
        for fb in (-99, 0, 1, 2, 3, 4, -1):
            for i, c in enumerate('XYZ'):
                w(mv + '.m' + c, vec[i])
            run('set $fbsd = %d' % fb)
            log('# UpdatePatrolMovement %s dir=%d' % (label, fb))
            run('call $t->UpdatePatrolMovement()')
            run('set $fbsd = -99')
    for i, c in enumerate('XYZ'):
        w(mv + '.m' + c, old[i])
    flush()
    # --- SetupLaserTargets / SpawnLaser / UpdateLaser / DeleteLaser -----------------------------------
    ld0 = float(ev('$t->mLaserDuration'))
    lid0 = int(ev('$t->mLaserId.value'))
    w('$t->mLaserDuration', 2.0)
    w(TID, 0xffff)
    log('# SetupLaserTargets no target')
    run('call $t->SetupLaserTargets(*$m)')
    w(TID, int(ev('$t->mUniqueId.value')))
    log('# SetupLaserTargets target is itself')
    run('call $t->SetupLaserTargets(*$m)')
    # unsafe perpendicular: this stands where the laser centre is (xy), the target is the player, duration 0
    w(TID, PLID)
    w('$t->mLaserDuration', 0.0)
    ax = float(ev('$m->mPlayers[0]->GetAimPosition(*$m, 0.0f).mX'))
    ay = float(ev('$m->mPlayers[0]->GetAimPosition(*$m, 0.0f).mY'))
    tx = float(ev('$m->mPlayers[0]->mPosition.mX'))
    ty = float(ev('$m->mPlayers[0]->mPosition.mY'))
    px0, py0 = float(ev('$t->mPosition.mX')), float(ev('$t->mPosition.mY'))
    w('$t->mPosition.mX', 0.5 * (tx + ax))
    w('$t->mPosition.mY', 0.5 * (ty + ay))
    log('# SetupLaserTargets perpendicular unsafe')
    run('call $t->SetupLaserTargets(*$m)')
    w('$t->mPosition.mX', px0)
    w('$t->mPosition.mY', py0)
    w('$t->mLaserDuration', 2.0)
    log('# SetupLaserTargets normal')
    run('call $t->SetupLaserTargets(*$m)')
    w('$t->mLaserId.value', 0xffff)
    log('# UpdateLaser no laser')
    run('call $t->UpdateLaser(*$m, 0.016f)')
    log('# SpawnLaser + UpdateLaser found')
    run('call $t->SpawnLaser(*$m, 0.016f)')
    run('call $t->UpdateLaser(*$m, 0.016f)')
    run('call $t->UpdateLaser(*$m, 0.016f)')
    run('call $t->DeleteLaser(*$m)')
    w('$t->mLaserDuration', ld0)
    w('$t->mLaserId.value', lid0)
    w(TID, tid0)
    flush()



class GateRet(gdb.FinishBreakpoint):
    def __init__(self, frame, val):
        super().__init__(frame, internal=True)
        self.val = val

    def stop(self):
        gdb.execute('set var $rax = %d' % self.val)
        return False

    def out_of_scope(self):
        pass


class Gate(gdb.Breakpoint):
    """A callee's return value is overridden: `seq` answers are used in order, then `sticky` (None: real).
    The breakpoint stays disabled except inside a phase, so a hot callee costs nothing otherwise."""

    def __init__(self, sym):
        super().__init__(addr(sym), internal=True)
        self.seq = []
        self.sticky = None
        self.enabled = False

    def stop(self):
        val = self.seq.pop(0) if self.seq else self.sticky
        if val is not None:
            GateRet(gdb.newest_frame(), val)
        return False


GATES = {
    'ipf': Gate('CSfxManager::IsPlaying('),
    'cam': Gate('CCameraManager::IsInCinematicCamera('),
    'ray': Gate('CStateManager::RayCollideWorld(CVector3f const&, CVector3f const&, CMaterialFilter const&, CActor const*)'),
    'rez': Gate('CPlayer::GetRezbitState('),
    'catne': Gate('CCharAnimTime::operator!='),
    'team': Gate('CScriptTeamAiMgr::CanStartAttack('),
}


class AfterRet(gdb.FinishBreakpoint):
    def __init__(self, frame, writes):
        super().__init__(frame, internal=True)
        self.writes = writes

    def stop(self):
        for lv, v in self.writes:
            gdb.execute('set var %s = %s' % (lv, v))
        return False

    def out_of_scope(self):
        pass


class After(gdb.Breakpoint):
    """At the return of `sym` the pending `writes` are made on the object of the call (`$t` = rdi)."""

    def __init__(self, sym):
        super().__init__(addr(sym), internal=True)
        self.writes = []
        self.enabled = False

    def stop(self):
        if self.writes:
            obj = '((CRezbit*)%d)' % int(ev('$rdi'))
            AfterRet(gdb.newest_frame(), [(lv.replace('$t', obj), v) for lv, v in self.writes])
        return False


AFTER = {'base_asm': After('CPatterned::AcceptScriptMsg(')}


def gates_on(spec):
    """spec: {name: (sticky, [seq])}"""
    for n, (sticky, seq) in spec.items():
        g = GATES[n]
        g.sticky = sticky
        g.seq = list(seq)
        g.enabled = True


def gates_off(names=None):
    for n, g in GATES.items():
        if names is None or n in names:
            g.enabled = False
            g.sticky = None
            g.seq = []


class NearRet(gdb.FinishBreakpoint):
    def __init__(self, frame, lst, uid):
        super().__init__(frame, internal=True)
        self.lst = lst
        self.uid = uid

    def stop(self):
        n = int(ev('*(int*)%d' % self.lst))
        gdb.execute('set var ((unsigned short*)(%d + 4))[%d] = %d' % (self.lst, n, self.uid))
        gdb.execute('set var *(int*)%d = %d' % (self.lst, n + 1))
        return False

    def out_of_scope(self):
        pass


class NearAppend(gdb.Breakpoint):
    """CStateManager::BuildNearList(list&, aabox, filter, actor) returns with `uid` appended (a stray id)."""

    def __init__(self):
        super().__init__(addr('CStateManager::BuildNearList(rstl::reserved_vector<TUniqueId, 2048>&, CAABox const&'),
                         internal=True)
        self.uid = None
        self.enabled = False

    def stop(self):
        if self.uid is not None:
            NearRet(gdb.newest_frame(), int(ev('$rsi')), self.uid)
        return False


NEAR = NearAppend()


class QueueRet(gdb.FinishBreakpoint):
    def __init__(self, frame, saved, gates, afters=()):
        super().__init__(frame, internal=True)
        self.saved = saved
        self.gates = gates
        self.afters = afters

    def stop(self):
        for lv, v in self.saved:
            gdb.execute('set var %s = %s' % (lv, v))
        gates_off(self.gates)
        return False

    def out_of_scope(self):
        pass


class Queue(gdb.Breakpoint):
    """At each call of `sym` on the Rezbit `$t` the next phase's writes are applied (restored at the return);
    phase = {'w': [(lvalue, value)], 'g': {gate: (sticky, [seq])}, 'when': gdb expression}.
    cyclic: phases repeat, at most `cap` calls are used; otherwise the breakpoint disables after the last phase."""

    def __init__(self, sym, phases, tag, cyclic=False, cap=0, anyobj=False):
        super().__init__(addr(sym))
        self.anyobj = anyobj
        self.phases = phases
        self.tag = tag
        self.i = 0
        self.cyclic = cyclic
        self.cap = cap

    def stop(self):
        try:
            if int(ev('$rdi')) != int(ev('(long)$t')) and not self.anyobj:   # a gdb condition is not honoured next to stop()
                return False
            obj = '((CRezbit*)%d)' % int(ev('$rdi')) if self.anyobj else '$t'
            sub = lambda x: x.replace('$t', obj) if isinstance(x, str) else x
            ph = self.phases[self.i % len(self.phases)]
            if 'when' in ph and not int(ev(sub(ph['when']))):
                return False
            self.i += 1
            log('# queue %s phase %d' % (self.tag, (self.i - 1) % len(self.phases)))
            saved = []
            for lv, val in ph.get('w', []):
                lv, val = sub(lv), sub(val)
                if lv.startswith('!'):
                    gdb.execute('set var %s = %s' % (lv[1:], val))
                else:
                    old = ev(lv)   # lazy: convert before the write
                    saved.append((lv, repr(float(old)) if old.type.code == gdb.TYPE_CODE_FLT else str(int(old))))
                    gdb.execute('set var %s = %s' % (lv, val))
            gates_on(ph.get('g', {}))
            for name, wl in ph.get('after', {}).items():
                AFTER[name].writes = [(sub(lv), sub(v)) for lv, v in wl]
                AFTER[name].enabled = True
            if self.tag == 'AcceptScriptMsg':
                log('#   active=%s shield=%s' % (ev(sub('$t->mActive')), ev(sub('$t->mShieldActorId.value'))))
            QueueRet(gdb.newest_frame(), saved, list(ph.get('g', {}).keys()), list(ph.get('after', {}).keys()))
            if (not self.cyclic and self.i >= len(self.phases)) or (self.cyclic and self.i >= self.cap):
                self.enabled = False
        except Exception as e:
            log('QUEUE EXC %s %r' % (self.tag, e))
        return False


def deflect_seq(tag):
    """DeflectMissiles under every list/sound arm; returns the id of the first deflected missile (or None)."""
    R = '$t->mData.missileDeflectRadius'
    CNT = '$t->mDeflectedMissiles.mCount'
    SFXT = '$t->mDeflectSfxTime'
    r0 = float(ev(R))
    log('# DeflectMissiles %s: real' % tag)
    run('call $t->DeflectMissiles(*$m)')
    cnt = int(ev(CNT))
    pid = int(ev('((unsigned short*)$t->mDeflectedMissiles.mData)[0]')) if cnt > 0 else None
    log('# deflected %d, first %s' % (cnt, pid))
    run('call $t->DeflectMissiles(*$m)')              # the same missiles again: all in the previous list
    for lab, ipf, tm in (('not playing', 0, 0.0), ('playing, fresh', 1, 0.0), ('playing, old', 1, 1.0)):
        log('# DeflectMissiles %s: %s' % (tag, lab))
        w(CNT, 0)
        w(SFXT, tm)
        gates_on({'ipf': (ipf, [])})
        run('call $t->DeflectMissiles(*$m)')
        gates_off(['ipf'])
    log('# DeflectMissiles %s: whole area' % tag)
    w(R, 1.0e9)
    w(CNT, 0)
    run('call $t->DeflectMissiles(*$m)')
    for lab, uid in (('not a projectile', int(ev('$t->mUniqueId.value'))), ('no such object', 0xfffe), ('invalid id', 0xffff)):
        log('# DeflectMissiles %s: stray id in the near list (%s)' % (tag, lab))
        NEAR.uid = uid
        NEAR.enabled = True
        run('call $t->DeflectMissiles(*$m)')
        NEAR.enabled = False
        NEAR.uid = None
    # shots in flight re-typed: power shot without / with kPA_ComboShot (0x80), and a foreign type
    run('set $actl = *(CObjectList**)($m->mObjectLists.mData + 16 * (int)kOL_Actor + 8)')
    shots = []
    i = int(ev('$actl->mFirstId'))
    while i != -1:
        e = ev('$actl->mObjects[%d].mEntity' % i)
        try:
            tn = str(e.dereference().dynamic_type)
        except gdb.error:
            tn = ''
        if 'CEnergyProjectile' in tn:
            q = int(e)
            shots.append((q, int(ev('((CWeapon*)%d)->mWeaponType' % q)), int(ev('((CWeapon*)%d)->mProjectileAttribs' % q))))
        i = int(ev('$actl->mObjects[%d].mNext' % i))
    log('# DeflectMissiles %s: %d energy projectiles in flight' % (tag, len(shots)))
    for lab, ty, at in (('power shot', 0, 0x0), ('power combo shot', 0, 0x80), ('foreign type', 99, 0x0)):
        for q, t0, a0 in shots:
            w('((CWeapon*)%d)->mWeaponType' % q, ty)
            w('((CWeapon*)%d)->mProjectileAttribs' % q, (a0 & ~0x80) | at)
        log('# DeflectMissiles %s: %s' % (tag, lab))
        w(CNT, 0)
        run('call $t->DeflectMissiles(*$m)')
        log('#   deflected %s' % ev(CNT))
    for q, t0, a0 in shots:
        w('((CWeapon*)%d)->mWeaponType' % q, t0)
        w('((CWeapon*)%d)->mProjectileAttribs' % q, a0)
    w(R, r0)
    w(SFXT, 0.0)
    flush()
    return pid


STATE = {'obj': 0, 'render': None}


def hook_obj():
    gdb.execute('set $busy = 1')
    try:
        STATE['obj'] += 1
        log('# obj trigger %d' % STATE['obj'])
        pid = deflect_seq('trigger %d' % STATE['obj'])
        if pid is not None and STATE['render'] is None:
            CNT = '$t->mDeflectedMissiles.mCount'
            D0 = '((unsigned short*)$t->mDeflectedMissiles.mData)[0]'
            R = '$t->mData.missileDeflectRadius'
            ph = [{'w': [(CNT, 1), (D0, pid), (R, 0.01)]}, {'w': [(CNT, 1), (D0, pid), (R, 1.0e9)]},
                  {'w': [(CNT, 1), (D0, 0xfffe)]}, {'w': [(CNT, 1), (D0, pid), (R, 0.0)]}, {'w': [(CNT, 0)]}]
            STATE['render'] = Queue('CRezbit::Render(', ph, 'Render', anyobj=True)
    except Exception as e:
        log('EXC obj %r' % (e,))
    finally:
        flush()
        gdb.execute('set $busy = 0')


def think_group():
    ACT = '$t->mActive'
    FRZ = '$t->mBodyController.mPtr->mFrozen'
    old = (int(ev(ACT)), int(ev(RZ)), int(ev(FRZ)))
    for label, act, rz, fz in (('inactive', 0, 0, 0), ('rezzed', 1, 0, 0), ('derezzed', 1, 1, 0), ('derezzed, frozen', 1, 1, 1)):
        log('# Think %s' % label)
        w(ACT, act)
        w(RZ, rz)
        w(FRZ, fz)
        run('call $t->Think(0.016f, *$m)')
    w(ACT, old[0])
    w(RZ, old[1])
    w(FRZ, old[2])
    flush()


def arm_queues():
    ACT = '$t->mActive'
    SID_ = '$t->mShieldActorId.value'
    msg = '((CScriptMsg*)$rdx)->m_msg'
    def m(name, w=()):
        return {'w': list(w), 'when': msg + ' == kSM_' + name}
    SFXL = '*(unsigned*)&$t->mLaserSfx'
    act_ph = [m('Activate', [(ACT, 0)]), m('Activate', [(ACT, 0), (SID_, 0xffff)]), {'w': [(ACT, 0)], 'after': {'base_asm': [(AD, 28)]}, 'when': msg + ' == kSM_Activate'},
              m('Activate', [(ACT, 0), (AD, 0)]), m('Activate'),
              m('Deactivate', [(ACT, 0)]), m('Deactivate', [(SID_, 0xffff)]), m('Deactivate'),
              m('Alert'), m('Damage'), m('ResistedDamage'), m('AreaLoaded'),
              m('Delete', [(SFX, 0x1234), (SFXL, 0x1234)]), m('Delete', [(SFX, 0), (SFXL, 0)])]
    STATE['asm'] = Queue('CRezbit::AcceptScriptMsg(', act_ph, 'AcceptScriptMsg', anyobj=True)
    ev_ph = [{'w': [('!$ecx', 33)], 'g': {'catne': (None, [0, 0])}},
             {'w': [('!$ecx', 33)], 'g': {'catne': (None, [1, 1])}},
             {'w': [('!$ecx', 33)], 'g': {'catne': (None, [1, 0])}},
             {'w': [('!$ecx', 33), ('$t->mTargetId.value', 0xffff)], 'g': {'catne': (None, [1, 1])}},
             {'w': [('!$ecx', 34)]},
             {'w': [('!$ecx', 99)]}]
    STATE['uae'] = Queue('CRezbit::DoUserAnimEvent(', ev_ph, 'DoUserAnimEvent')
    MB = PLID.replace('mUniqueId.value', 'mSpawnedMorphBallState')
    sv = [{'w': [(TID, PLID), (MB, 0)], 'g': {'cam': (0, []), 'ray': (1, [])}},
          {'w': [(TID, PLID), (MB, 0)], 'g': {'cam': (0, []), 'ray': (0, [])}},
          {'w': [(TID, PLID), (MB, 0)], 'g': {'cam': (1, [])}},
          {'w': [(TID, PLID), (MB, 1)], 'g': {'cam': (0, [])}},
          {'w': [(TID, 0xffff)]},
          {'w': [(TID, '$t->mUniqueId.value')]}]
    STATE['sva'] = Queue('CRezbit::StartVirusAttack(', sv, 'StartVirusAttack')
    VIRUS = [('$t->mVirusTimer', 0.0), (SID, 0xffff), (TID, PLID), ('$t->mTeamAiMgrId.value', 0xffff),
             ('$t->mData.virusMinAttackDist', 0.0), ('$t->mData.virusMaxAttackDist', 1.0e9)]
    sh = lambda extra, g=None: {'w': VIRUS + extra, 'g': g or {}}
    vph = [sh([(MB, 0)], {'rez': (0, []), 'cam': (0, [])}),
           sh([(MB, 1)], {'rez': (0, []), 'cam': (0, [])}),
           sh([(MB, 0)], {'rez': (1, []), 'cam': (0, [])}),
           sh([('$t->mData.virusMinAttackDist', 1.0e9), (MB, 0)], {'rez': (0, []), 'cam': (0, [])}),
           sh([('$t->mData.virusMaxAttackDist', 0.0), (MB, 0)], {'rez': (0, []), 'cam': (0, [])}),
           sh([(TID, 0xffff)], {'cam': (0, [])}),
           sh([('$t->mVirusTimer', 1.0e9)], {'cam': (0, [])}),
           sh([], {'cam': (1, [])}),
           sh([('$t->mTeamAiMgrId.value', '$t->mUniqueId.value')], {'cam': (0, []), 'team': (0, [])}),
           sh([('$t->mTeamAiMgrId.value', '$t->mUniqueId.value')], {'cam': (0, []), 'team': (1, [])})]
    STATE['vir'] = Queue('CRezbit::ShouldVirusAttack(', vph, 'ShouldVirusAttack', cyclic=True, cap=60)
    log('# queues armed')


def hook():
    gdb.execute('set $busy = 1')
    try:
        run('set $t = (CRezbit*)$rdi')
        run('set $m = (CStateManager*)$rsi')
        log('# rezbit %s' % ev('$t'))
        which = os.environ.get('REZ5_GROUPS', 'shield,misc,think,queues').split(',')
        if 'shield' in which:
            shield_group()
        if 'misc' in which:
            misc_group()
        if 'think' in which:
            think_group()
        if 'queues' in which:
            arm_queues()
        gdb.execute('set $tvalid = 1')
        gdb.execute('set $done = 1')
        log('# done')
    except Exception as e:
        log('EXC %r' % (e,))
        gdb.execute('set $done = 1')
    finally:
        gdb.execute('set $busy = 0')


bp = gdb.Breakpoint(addr('CRezbit::Think('))
bp.condition = '$busy == 0 && $done == 0 && ((CRezbit*)$rdi)->mAlive && ((CRezbit*)$rdi)->mActive'
bp.ignore_count = int(os.environ.get('REZ5_AT', '300'))
bp.commands = 'python hook()\ncontinue'

ob = gdb.Breakpoint(addr('PortConsole::(anonymous namespace)::CmdObj('))
ob.condition = '$tvalid == 1 && $busy == 0'
ob.commands = 'python hook_obj()\ncontinue'
