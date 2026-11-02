#!/usr/bin/env python3
"""Coverage driver for the SpacePirate module (tools/cov/run.sh with tools/cov/specs/spacepirate.py):
wake the pirates, fight them with live aim, then force the states the area's FSM never reaches on
one pirate (`fsm <id> <state>`, labelled "forced" in the coverage) and kill two.

usage: spacepirate_drive.py <port> [layer|-] [rounds]
env:   SP_FORCE      the states to force (default below)
       SP_FORCE_EID  editor id (8 hex digits, as `objs` prints it) of the pirate to force; default the first
       SP_DELETE_TEAM=<n>  before round n, delete every CScriptTeamAiMgr (`send <id> Delete`), so
                     the members' squad ids go stale for the rest of the run
       SP_STEPS      after the rounds and before SP_FORCE: `;`-separated steps, each `wait <s>`,
                     `deleteteams`, or a console command where `{<eid>}` (8 hex digits) becomes that
                     pirate's `u<id>` (`{#<n>}`: the pirate whose uid index, the debug log's id, is n;
                     `{cls:<Class>}`: the first object of that class, e.g. `{cls:CPlayer}`), e.g. `fsm {0036013d} Attack; wait 10; fsm {0036013d} JumpBack`

It talks to the port's debug console (MP2_CONSOLE): it switches the area's layer, reads the
pirates with `objs spacepirate`, activates each of them with `send <id> Activate`, then puts
the player a few units from one of them, aims at it (`aim <yaw> <pitch>`, so the player's aim
*point* - the crosshair's point, which is what the pirate's line-of-sight ray ends at - is the
pirate) and fires. Every step re-reads the live positions, because the ids and the places
move from run to run.
"""
import math
import os
import re
import socket
import sys
import time

port = int(sys.argv[1]) if len(sys.argv) > 1 else 50360
layer = sys.argv[2] if len(sys.argv) > 2 else "3"
rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 4


class Console:
    def __init__(self, port):
        self.port = port
        self.sock = None
        self.file = None

    def connect(self, timeout=150):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                self.sock = socket.create_connection(('127.0.0.1', self.port), timeout=5)
                self.file = self.sock.makefile('rw')
                return True
            except OSError:
                time.sleep(0.5)
        return False

    def cmd(self, line, timeout=60):
        for _ in range(2):
            if self.sock is None and not self.connect():
                return ''
            try:
                self.file.write(line + '\n')
                self.file.flush()
                self.sock.settimeout(timeout)
                out = []
                while True:
                    r = self.file.readline()
                    if not r:
                        raise OSError('eof')
                    r = r.rstrip()
                    out.append(r)
                    if r.startswith('=> '):
                        return '\n'.join(out)
            except OSError:
                try:
                    self.sock.close()
                except OSError:
                    pass
                self.sock = None
                self.file = None
                time.sleep(1.0)
        return ''


OBJ = re.compile(r'u([0-9a-f]+) ([0-9a-f]{8})\s+CSpacePirate\s+a(\d+) \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)')
COV = re.compile(r'u([0-9a-f]+) ([0-9a-f]{8})\s+CScriptCoverPoint\s+a(\d+) \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)')
# the cover points' facing, by editor id (read from the cover probe): the player has to stand in
# front of the cover for it to be usable (CScriptCoverPoint::Blown).
COVER_FWD = {"000a027c": (0.0, 1.0, 0.0), "000a0186": (0.0, 1.0, 0.0),
             "000a0180": (-1.0, -0.08, 0.0)}
# the SpacePirate placements of 02_sand_sandfalls L3, by editor id (the layer-0 pirates of the
# neighbouring areas are not the troopers).
TROOPERS = (0x000A012D, 0x000A010E, 0x000A027A, 0x000A010D,
            0x002A00F3, 0x002A00F4, 0x0036002F, 0x00360030)  # the troopers and the area-08 teleporting pirates


def pirates(out):
    found = []
    for m in OBJ.finditer(out):
        found.append((int(m.group(1), 16), float(m.group(4)), float(m.group(5)),
                      float(m.group(6)), int(m.group(2), 16)))
    return found


def covers(out):
    found = []
    for m in COV.finditer(out):
        found.append((int(m.group(1), 16), float(m.group(4)), float(m.group(5)),
                      float(m.group(6)), m.group(2)))
    return found


def aim_at(px, py, pz, tx, ty, tz):
    """The yaw/pitch (the port's `aim` convention: 0 = +y, 90 = +x, the pitch up is positive)."""
    dx, dy, dz = tx - px, ty - py, tz - pz
    yaw = math.degrees(math.atan2(dx, dy))
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return yaw, pitch



TEAM = re.compile(r'u([0-9a-f]+) [0-9a-f]{8}\s+CScriptTeamAiMgr')
FORCED = os.environ.get('SP_FORCE', 'JumpBack Halt Approach TurnAround Shuffle DoubleSnap Crouch '
                        'CoverAttack Cover GetUp WallHang LaunchGrenade WarpIn Generate Captured '
                        'Deactivate').split()


def main():
    c = Console(port)
    if not c.connect(timeout=600):
        print('driver: no console')
        return 1
    if layer != '-':
        c.cmd('layer %s' % layer)
    ps = []
    for _ in range(300):  # the console is up before the world has loaded
        ps = pirates(c.cmd('objs spacepirate'))
        if ps:
            break
        time.sleep(1)
    time.sleep(3)
    print('driver: %d pirates: %s' % (len(ps), [(hex(p[0]), hex(p[4])) for p in ps]), flush=True)
    for p in ps:
        c.cmd('send u%04x Activate' % p[0])
    delete_at = int(os.environ.get('SP_DELETE_TEAM', '-1'))
    for r in range(rounds):
        if r == delete_at:
            for m in TEAM.finditer(c.cmd('objs teamai')):
                print('driver: delete team u%s %s' % (m.group(1), c.cmd('send u%s Delete' % m.group(1)).splitlines()[-1:]),
                      flush=True)
            time.sleep(2)
        ps = pirates(c.cmd('objs spacepirate'))
        if not ps:
            break
        uid, x, y, z, _e = ps[r % len(ps)]
        px, py, pz = x, y - 6.0, z
        c.cmd('tp %.2f %.2f %.2f' % (px, py, pz))
        yaw, pitch = aim_at(px, py, pz, x, y, z + 1.5)
        c.cmd('aim %.1f %.1f' % (yaw, pitch))
        c.cmd('press a 12')
        print('driver: round %d at u%04x' % (r, uid), flush=True)
        time.sleep(3)
    for step in [x.strip() for x in os.environ.get('SP_STEPS', '').split(';') if x.strip()]:
        if step.startswith('wait '):
            time.sleep(float(step.split()[1]))
            continue
        if step == 'deleteteams':
            for m in TEAM.finditer(c.cmd('objs teamai')):
                print('driver: delete team u%s %s' % (m.group(1), c.cmd('send u%s Delete' % m.group(1)).splitlines()[-1:]),
                      flush=True)
            continue
        ids = {}
        for p in pirates(c.cmd('objs spacepirate')):
            ids['%08x' % p[4]] = ids['#%d' % (p[0] & 0x7ff)] = 'u%04x' % p[0]
        for cls in set(re.findall(r'\{cls:(\w+)\}', step)):
            m = re.search(r'u([0-9a-f]+) [0-9a-f]{8}\s+%s\b' % cls, c.cmd('objs %s' % cls.lower()))
            ids['cls:' + cls] = 'u' + m.group(1) if m else 'u_missing_' + cls
        line = re.sub(r'\{(#\d+|[0-9a-f]{8}|cls:\w+)\}', lambda m: ids.get(m.group(1), 'u_missing_' + m.group(1)), step)
        print('driver: step %s -> %s' % (line, c.cmd(line).splitlines()[-2:]), flush=True)
    ps = pirates(c.cmd('objs spacepirate'))
    want = os.environ.get('SP_FORCE_EID', '').lower()
    ps = [p for p in ps if '%08x' % p[4] == want] or ps
    if ps:
        uid = ps[0][0]
        print('driver: forcing u%04x %08x' % (uid, ps[0][4]), flush=True)
        for st in FORCED:
            print('driver: forced %s -> %s' % (st, c.cmd('fsm u%04x %s' % (uid, st)).splitlines()[-1:]),
                  flush=True)
            time.sleep(4)
    for p in pirates(c.cmd('objs spacepirate'))[1:3]:
        print('driver: kill u%04x %s' % (p[0], c.cmd('damage u%04x 1000' % p[0]).splitlines()[-1:]), flush=True)
        time.sleep(4)
    print('driver: done', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
