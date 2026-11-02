# bsw3: native-entry fixtures, ORIGINAL snapshots and FinishBreakpoint rollback
# armed before any mutation. Every restore is read back; any discrepancy stops gdb.
# Use tools/cov/force.sh with its default force.py (FORCE is intentionally empty).
import gdb
import json
import os

CLASS = 'CBacteriaSwarm'
FORCE = []
active = {}
primary = None
think_n = 0
render_n = 0
create_n = 0
waypoint = None
direct_pending = False
direct_done = False

def log(s):
    with open(os.environ['COV_OUT'] + '/bsw3.log', 'a') as f:
        f.write(s + '\n')

def ev(s):
    return gdb.parse_and_eval(s)

def iv(s):
    return int(ev(s))

def fail(s):
    log('FAIL ' + s)
    raise gdb.GdbError(s)

def snapshot_event(label, event, saved):
    rows = []
    for lv, addr, old, new in saved:
        row = {'lvalue': lv, 'address': addr,
               'original': old.hex() if isinstance(old, bytes) else old,
               'fixture': ('bytes:%d' % len(new)) if isinstance(new, bytes) else new}
        if event == 'restore-readback-matched-originals':
            actual = (bytes(gdb.selected_inferior().read_memory(addr, len(old)))
                      if addr is not None else iv(lv))
            row['readback'] = actual.hex() if isinstance(actual, bytes) else actual
            if actual != old:
                fail('snapshot readback mismatch ' + lv)
        rows.append(row)
    with open(os.environ['COV_OUT'] + '/bsw3-snapshots.jsonl', 'a') as f:
        f.write(json.dumps({'label': label, 'event': event, 'fields': rows}) + '\n')

class Tx(gdb.FinishBreakpoint):
    def __init__(self, kind, label, this, mgr, writes):
        if kind in active:
            fail('reentry ' + kind)
        self.kind, self.label, self.this, self.mgr = kind, label, this, mgr
        self.saved = []
        self.oracle = None
        self.answer = None
        # Read EVERY original before arming, and arm before the first write.
        for lv, new in writes:
            value = ev(lv)
            if value.address is not None:
                addr = int(value.address)
                raw = bytes(gdb.selected_inferior().read_memory(addr, value.type.sizeof))
                self.saved.append((lv, addr, raw, new))
            else:
                self.saved.append((lv, None, iv(lv), new))
        super().__init__(gdb.newest_frame(), internal=True)
        active[kind] = self
        snapshot_event(label, 'originals-before-mutation', self.saved)
        log('ARM %s fields=%d this=%#x' % (label, len(self.saved), this))
        try:
            for lv, addr, old, new in self.saved:
                if new is not None:
                    if isinstance(new, bytes):
                        gdb.selected_inferior().write_memory(addr, new)
                    else:
                        gdb.execute('set var %s = %s' % (lv, new))
        except Exception:
            self.restore()
            raise

    def restore(self):
        for lv, addr, old, new in reversed(self.saved):
            if addr is None:
                gdb.execute('set var %s = %d' % (lv, old))
                if iv(lv) != old:
                    fail('bitfield restore mismatch ' + lv)
            else:
                inf = gdb.selected_inferior()
                inf.write_memory(addr, old)
                if bytes(inf.read_memory(addr, len(old))) != old:
                    fail('raw restore mismatch ' + lv)
        log('RESTORE READBACK OK %s fields=%d' % (self.label, len(self.saved)))
        snapshot_event(self.label, 'restore-readback-matched-originals', self.saved)
        active.pop(self.kind, None)

    def stop(self):
        ok = True
        try:
            if self.oracle:
                ok = bool(ev(self.oracle))
                log(('ORACLE PASS ' if ok else 'ORACLE FAIL ') + self.label)
        finally:
            self.restore()
        if not ok:
            fail(self.label + ' native output oracle')
        return False

    def out_of_scope(self):
        self.restore()
        fail('call left without normal return: ' + self.label)

def ptrs():
    return iv('$rdi'), iv('$rsi')

def bindings(this, mgr):
    S = '((CBacteriaSwarm*)%d)->' % this
    M = '((CStateManager*)%d)->' % mgr
    B = S + 'mBoids.mItems[0].'
    P = '((CPlayer*)%s mPlayers[0])->mPlayerState->' % M
    A = M + 'mWorld.mPtr->mAreas.mItems[%s mAreaId.value].mItem->mPostConst.mPtr->' % S
    return S, M, B, P, A

def list_snapshot(S):
    return [(S + 'mPartitionedBoidLists', None), (S + 'mOutlierBoidList', None)]

def outside_writes(S, B):
    # Actual boid storage and a real geometric outlier; UpdatePartition runs itself.
    return [(S + 'mBoids.mCount', '1'), (S + 'mBoids.mItems[0]', None)] + list_snapshot(S) + [
        (B + 'mTransform.m03', S + 'mTransform.m03 + 2.f * ' + S + 'mBoundingBoxExtent.mX'),
        (B + 'mTransform.m13', S + 'mTransform.m13'),
        (B + 'mTransform.m23', S + 'mTransform.m23')]

class Think(gdb.Breakpoint):
    def stop(self):
        global primary, think_n
        this, mgr = ptrs()
        S, M, B, P, A = bindings(this, mgr)
        frame = iv(M + 'mUpdateFrameIdx')
        if frame < 350 or iv(S + 'mEditorId.value') != 0x003d0023:
            return False
        if primary is None:
            primary = this
            log('PRIMARY this=%#x boids=%d area=%d' %
                (this, iv(S + 'mBoids.mCount'), iv(S + 'mAreaId.value')))
        if this != primary or not iv(S + 'mActive') or not iv(S + 'mBoids.mCount'):
            return False
        think_n += 1
        if think_n % 8 or think_n > 8 * 15:
            return False
        phase = think_n // 8 - 1
        writes = []
        label = 'Think phase %d' % phase
        if phase == 0:
            writes = [(S + 'mActive', '0')]
        elif phase in (1, 2, 3, 4):
            timer = ('0.f', '0.001f', '5.f', '5.f')[phase - 1]
            counter = ('0', '0', '1', '3')[phase - 1]
            writes = [(A + 'mOcclusionState', '0'), (S + 'mOccludedTimer', timer),
                      (S + 'mThinkCounter', counter)]
        elif phase == 5:
            writes = [(S + 'mMaxCreatedBoids', '0'), (S + 'mBoidGenCooldownTimer', '-1.f'),
                      (B + 'mActive', '0')]
        elif phase == 6:
            writes = [(S + 'mBoids.mCount', '0'), (S + 'mMaxCreatedBoids', '0'),
                      (S + 'mBoidGenCooldownTimer', '-1.f')] + list_snapshot(S)
        elif phase == 7:
            writes = [(S + 'mBoidParticleGenerator.mPtr', '0'),
                      (S + 'mMaxCreatedBoids', '0'), (S + 'mBoidGenCooldownTimer', '-1.f'),
                      (B + 'mActive', '0')]
        elif phase in (8, 9):
            if not iv(B + 'mActive'):
                return False
            writes = outside_writes(S, B) + [(S + 'mBoidGenCooldownTimer', '10.f'),
                     (S + 'mThinkCounter', str(phase & 1)), (S + 'mAabox', None)]
        elif phase in (10, 11, 12):
            writes = [(P + 'mCurrentVisor', '2'),
                      (P + 'mTransitioningVisor', '0' if phase == 10 else '2')]
        elif phase == 13:
            area = A.rsplit('mPostConst.mPtr->', 1)[0]
            # Finished script objects, not yet Loaded: native query returns Occluded.
            # Timeout is already expired, so no other area API runs before rollback.
            writes = [(area + 'mPhase', '13'), (S + 'mOccludedTimer', '0.f')]
        elif phase == 14:
            writes = [(S + 'mMaxCreatedBoids', '1'), (S + 'mCreatedBoids', '1')]
        tx = Tx('Think', label, this, mgr, writes)
        return False

def find_waypoint(mgr):
    table = ev('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem' % mgr).dereference()['mObjects']
    for i in range(2048):
        ent = table[i]['mEntity']
        if int(ent) and 'CScriptWaypoint' in str(ent.dynamic_type):
            w = ent.cast(gdb.lookup_type('CScriptWaypoint').pointer())
            log('REAL WAYPOINT ptr=%#x uid=%#x' % (int(w), int(w.dereference()['mUniqueId']['value'])))
            return int(w)
    return None

class Create(gdb.Breakpoint):
    def stop(self):
        global create_n, waypoint
        this, mgr = ptrs()
        if this != primary or create_n >= 12:
            return False
        if waypoint is None:
            waypoint = find_waypoint(mgr)
            if waypoint is None:
                log('NO REAL WAYPOINT: no fixture and no invented actor')
                return False
        S, M, B, P, A = bindings(this, mgr)
        index, force = iv('$edx'), iv('$ecx') & 255
        count = iv(S + 'mBoids.mCount')
        if not (0 <= index < count):
            fail('CreateBoid native index outside real vector')
        phase = create_n % 3
        create_n += 1
        W = '((CScriptWaypoint*)%d)->' % waypoint
        tx = Tx('Create', 'Create phase %d force=%d index=%d' % (phase, force, index),
                this, mgr, [(W + 'mActive', str(int(phase == 2)))])
        tx.answer = 65535 if phase == 0 else iv(W + 'mUniqueId.value')
        if phase == 2:
            C = S + 'mBoids.mItems[%d].' % index
            tx.oracle = ' && '.join('%smTransform.m%d%d==%smTransform.m%d%d' % (C,r,c,W,r,c)
                                    for r in range(3) for c in range(4))
        return False

class Render(gdb.Breakpoint):
    def stop(self):
        global render_n
        this, mgr = ptrs()
        if this != primary or render_n >= 32:
            return False
        S, M, B, P, A = bindings(this, mgr)
        if not iv(S + 'mBoids.mCount') or not iv(B + 'mActive'):
            return False
        phase = render_n % 4
        render_n += 1
        writes = [('CGraphics::mLightActive', '0' if phase & 1 else '1'),
                  (B + 'mInFrustum', str(phase & 1))]
        if phase in (2, 3):
            # A consistent list containing one real, geometrically outlying active
            # boid. All original heads, link, transform, count and colour are saved.
            writes += outside_writes(S, B)
            writes += [(S + 'mOutlierBoidList', '&' + S + 'mBoids.mItems[0]'),
                       (B + 'mNext', '0'), (S + 'mThinkCounter', '1' if phase == 3 else '2')]
            writes += [(S + 'mPartitionedBoidLists.mData',
                        bytes(125 * gdb.lookup_type('CBacteriaSwarm::CBoid').pointer().sizeof))]
        if phase == 1:
            writes += [(S + 'mBoidParticleGenerator.mPtr', '0')]
        tx = Tx('Render', 'Render phase %d' % phase, this, mgr, writes)
        tx.answer = 64 if phase == 0 else -1
        return False

class AnswerExit(gdb.FinishBreakpoint):
    def __init__(self, tx, label):
        super().__init__(gdb.newest_frame(), internal=True)
        self.tx, self.label = tx, label

    def stop(self):
        original = iv('$eax')
        gdb.execute('set $rax = %d' % (self.tx.answer & 0xffffffff))
        log('CALLEE ANSWER %s original=%#x fixture=%#x' %
            (self.label, original, self.tx.answer & 0xffffffff))
        return False

class Answer(gdb.Breakpoint):
    def __init__(self, symbol, kind):
        super().__init__('*' + symbol, internal=True)
        self.kind = kind

    def stop(self):
        tx = active.get(self.kind)
        if tx and tx.this == iv('$rdi') and tx.answer is not None:
            AnswerExit(tx, self.kind)
        return False

def symbol(prefix):
    names = []
    for line in open(os.environ['COV_NM']):
        raw, dem = line.rstrip('\n').split('\t')
        parts = raw.split()
        if len(parts) == 3 and parts[1] in 'tTW' and dem.startswith(prefix) and '.cold' not in dem and 'clone' not in dem:
            names.append(parts[2])
    assert len(names) == 1, (prefix, names)
    return names[0]

Think('*' + symbol('CBacteriaSwarm::Think('), internal=True)
Create('*' + symbol('CBacteriaSwarm::CreateBoid('), internal=True)
Render('*' + symbol('CBacteriaSwarm::Render('), internal=True)
Answer(symbol('CBacteriaSwarm::GetWaypointForState('), 'Create')
Answer(symbol('CActor::GetRenderAlphaBufferAlpha('), 'Render')

# GDB cannot make an inferior call inside Breakpoint.stop(). force.sh's batch
# executes `bt 20` after a stop, so its hook runs the guarded native call.
class DirectCreate(gdb.Breakpoint):
    def stop(self):
        global direct_pending
        this, mgr = ptrs()
        S, M, B, P, A = bindings(this, mgr)
        if this != primary or iv(M + 'mUpdateFrameIdx') < 750 or active:
            return False
        if not iv(S + 'mBoidParticleGenerator.mPtr') or not iv(S + 'mBoids.mCount'):
            return False
        self.enabled = False
        direct_pending = True
        return True

class DirectCommand(gdb.Command):
    def __init__(self):
        super().__init__('bsw3-direct-create', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        global direct_pending, direct_done
        if not direct_pending:
            fail('unexpected debugger stop: no direct call pending')
        direct_pending = False
        this, mgr = ptrs()
        S, M, B, P, A = bindings(this, mgr)
        label = 'Create native forceParticle=false'
        saved = []
        for lv in (S + 'mBoids.mItems[0]', M + 'mRandom'):
            value = ev(lv)
            addr = int(value.address)
            raw = bytes(gdb.selected_inferior().read_memory(addr, value.type.sizeof))
            saved.append((lv, addr, raw, None))
        snapshot_event(label, 'originals-before-mutation', saved)
        log('ARM ' + label)
        try:
            gdb.execute('call ((CBacteriaSwarm*)%d)->CreateBoid(*((CStateManager*)%d),0,false)' % (this, mgr))
            assert iv(B + 'mActive') == 1
            assert bool(ev(B + 'mSpeed == ' + S + 'mPatrolSpeed'))
            for axis in ('X', 'Y', 'Z'):
                assert bool(ev(B + 'mVelocity.m' + axis + ' == 0.f'))
            log('ORACLE PASS ' + label)
            direct_done = True
        finally:
            for lv, addr, raw, new in reversed(saved):
                gdb.selected_inferior().write_memory(addr, raw)
                assert bytes(gdb.selected_inferior().read_memory(addr, len(raw))) == raw
            snapshot_event(label, 'restore-readback-matched-originals', saved)
            log('RESTORE READBACK OK ' + label)

DirectCommand()
direct = DirectCreate('*' + symbol('CBacteriaSwarm::Think('), internal=True)
gdb.execute('define hook-backtrace\nbsw3-direct-create\ncontinue\nend')

def finished(event):
    log('EXIT pending=%s primary=%s Think=%d Render=%d Create=%d' %
        (list(active), primary, think_n, render_n, create_n))
    if active or primary is None or render_n != 32 or create_n != 12 or not direct_done:
        log('FAIL incomplete fixture run')
gdb.events.exited.connect(finished)
