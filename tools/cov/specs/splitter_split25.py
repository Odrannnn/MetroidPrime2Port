# Splitter (module 75) split25 lane: split24's run (split23's con, member writes and return
# overrides, split24's gdb calls) plus a second batch of gdb calls: at the SPLIT25_AT-th (default
# 6100) CSplitterCommandModule::Think entry gdb sources tools/cov/specs/splitter_split25_calls.py,
# which writes the members each arm needs, calls the body and writes every member back.
# fn_75_B2A0's forced gates (split23) cycle through both flag bits, 0x20 alone, 0x10 alone (the
# missile arm's two halves) and both with the chassis's area 99 (the missile in another area);
# the player's orbit target is the chassis for the call (0xB370-0xB388) and the near list gets the
# player's id in front (not a CWeapon: the cast's null arm, 0xB464), then the live energy
# projectile split23 records ($split23_wid: the player's beam shots and missiles).  While $s25_nf is 1 (the
# forced calls) none of this happens.
#
#   COV_GDB_SCRIPT=$PWD/tools/cov/force_ret.py BUILD=$PWD/build-port-cov SECS=3600 \
#     tools/cov/force.sh tools/cov/specs/splitter_split25.py <out> 1baa96c2,914f1381 \
#     --con tools/cov/specs/splitter_split23.con --fast --frames 12000
import os
import gdb

_S24 = os.path.join(os.path.dirname(os.path.abspath(os.environ['COV_SPEC'])), 'splitter_split24.py')
exec(compile(open(_S24).read(), _S24, 'exec'))


def _nf(lv, val):
    return (lv, '($s25_nf ? %s : %s)' % (lv, val))


_FL = '$this->mData.mUnknownCEF5C2FE'
FORCE_RET = [f for f in FORCE_RET if f[0] != 'fn_75_B2A0']
_GATES = [_nf('$this->mTimerD34', '0.f'), _nf('$this->mDodgeTimer', '0.f')]
FORCE_RET += [
    ('fn_75_B2A0', 'CSplitterMainChassis::FindBestDodgeDirection(', 4,
     [(0, [_nf(_FL, _FL + ' | 0x30')] + _GATES),
      (1, [_nf(_FL, '(%s & ~0x30) | 0x20' % _FL)] + _GATES),
      (2, [_nf(_FL, '(%s & ~0x30) | 0x10' % _FL)] + _GATES),
      (3, [_nf(_FL, _FL + ' | 0x30'), _nf('$this->mAreaId.value', '99')] + _GATES)],
     'dodge gates, one flag bit at a time, another area'),
]

if gdb.convenience_variable('split25_on') is None:
    gdb.set_convenience_variable('split25_on', 1)
    gdb.set_convenience_variable('s25_nf', 0)
    _H25 = os.path.dirname(os.path.abspath(os.environ['COV_SPEC']))
    _t25 = gdb.Breakpoint('*' + _addrs('CSplitterCommandModule::Think(')[0])
    _t25.ignore_count = int(os.environ.get('SPLIT25_AT', '6100'))
    gdb.set_convenience_variable('s25_think', _t25.number)

    def _off():
        v = gdb.convenience_variable('s25_nf')
        return v is not None and int(v) == 1

    class _OrbitRestore(gdb.FinishBreakpoint):
        def __init__(self, frame, lv, old):
            super().__init__(frame, internal=True)
            self.lv, self.old = lv, old

        def stop(self):
            gdb.execute('set var %s = %s' % (self.lv, self.old))
            for b in _NEAR:
                b.enabled = False
            return False

        def out_of_scope(self):
            for b in _NEAR:
                b.enabled = False

    # The near-list breakpoints are armed only inside FindBestDodgeDirection (every actor builds
    # near lists; a Python stop on each would stall the run).
    _NEAR = []

    class _Orbit(gdb.Breakpoint):
        def stop(self):
            if _off():
                return False
            lv = '((CStateManager*)%d)->mPlayers[0]->mOrbitTargetId.value' % int(gdb.parse_and_eval('$rsi'))
            old = int(gdb.parse_and_eval(lv))
            gdb.execute('set var %s = ((CEntity*)%d)->mUniqueId.value' % (lv, int(gdb.parse_and_eval('$rdi'))))
            _OrbitRestore(gdb.newest_frame(), lv, old)
            for b in _NEAR:
                b.enabled = True
            return False

    for _m in _addrs('CSplitterMainChassis::FindBestDodgeDirection('):
        _Orbit('*' + _m, internal=True)

    class _NearFinish(gdb.FinishBreakpoint):
        def __init__(self, frame, lst, pid):
            super().__init__(frame, internal=True)
            self.lst, self.pid = lst, pid

        def stop(self):
            inf = gdb.selected_inferior()
            n = int(gdb.parse_and_eval('*(int*)%d' % self.lst))
            if n < 2048:
                data = bytes(inf.read_memory(self.lst + 4, 2 * n)) if n else b''
                ids = [self.pid]
                w = gdb.convenience_variable('split23_wid')
                if w is not None and int(w) & 0xffff != 0xffff and n < 2047:
                    ids.append(int(w) & 0xffff)
                inf.write_memory(self.lst + 4, b''.join(i.to_bytes(2, 'little') for i in ids) + data)
                gdb.execute('set var *(int*)%d = %d' % (self.lst, n + len(ids)))
            return False

        def out_of_scope(self):
            pass

    class _Near(gdb.Breakpoint):
        def stop(self):
            if _off():
                return False
            up = gdb.newest_frame().older()
            if up is None or not up.name() or \
                    not up.name().startswith('CSplitterMainChassis::FindBestDodgeDirection'):
                return False
            pid = int(gdb.parse_and_eval('((CStateManager*)$rdi)->mPlayers[0]->mUniqueId.value'))
            _NearFinish(gdb.newest_frame(), int(gdb.parse_and_eval('$rsi')), pid)
            return False

    for _m in _addrs('CStateManager::BuildNearList('):
        _b = _Near('*' + _m, internal=True)
        _b.enabled = False
        _NEAR.append(_b)
    _t25.commands = 'source %s/splitter_split25_calls.py\ndelete %d\ncontinue' % (_H25, _t25.number)
