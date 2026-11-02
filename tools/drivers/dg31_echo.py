#!/usr/bin/env python3
"""DigitalGuardian area 53, dg31: the echo targets (fn_14_1A1F8 Think, 1A430 XDMG, 1A168 Render,
19A08/19B70/19CE0/19DF8), knee shots (fn_14_D7BC), fn_14_3DA8's invalid and stale knee ids and
fn_14_18B4's flag 0x10, under any spec (gcov is what counts).

usage: dg31_echo.py <port>   (tools/cov/run.sh ... --driver 'python3 tools/drivers/dg31_echo.py {port}')
"""
import re
import socket
import sys
import time

port = int(sys.argv[1])
BODY, HEAD = 'e00350060', 'e0035000f'
t0 = time.time()
s = None
while s is None and time.time() - t0 < 900:
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=5)
    except OSError:
        time.sleep(0.5)
if s is None:
    print('[dg31] no connect', flush=True)
    sys.exit(1)
f = s.makefile('rw')
s.settimeout(300)


def C(c):
    f.write(c + '\n')
    f.flush()
    lines, pending = [], c.count(';') + 1
    while True:
        l = f.readline()
        if not l:
            raise SystemExit('[dg31] eof')
        l = l.rstrip()
        if l.startswith('=> '):
            pending -= 1
            if pending == 0:
                break
        else:
            lines.append(l)
    print(f'> {c}\n   ' + '\n   '.join(lines), flush=True)
    return '\n'.join(lines)


def frame():
    m = re.search(r'frame (\d+)', C('frame'))
    return int(m.group(1)) if m else -1


def at(n, c=None):
    while frame() < n:
        time.sleep(0.05)
    return C(c) if c else None


def var(name, value=None):
    out = C(f'var {BODY} {name}' + ('' if value is None else f' {value}'))
    m = re.search(r'=\s*(-?\d+)', out)
    return int(m.group(1)) if m else None


def head_var(name, value=None):
    out = C(f'var {HEAD} {name}' + ('' if value is None else f' {value}'))
    m = re.search(r'=\s*(-?\d+)', out)
    return int(m.group(1)) if m else None


at(130, 'layer 10; god 1; give EchoVisor')
at(150, f'send {BODY} Activate; send {HEAD} Activate')
at(152, f'place {HEAD} player 20')
at(160)
knees = [var(f'dg.armor{i}') for i in range(4)]
K = ['u%04x' % (k & 0xffff) for k in knees]
# fn_14_D7BC: a beam shot on two knees asks the knee for its vulnerability
C(f'shoot {K[0]}; shoot {K[1]}')
# fn_14_18B4: flag 0x10 of 0x128C set for a few frames
flags = var('dg.flags')
var('dg.flags', flags | 0x10)
# fn_14_3DA8: knee 3 unset, knee 2's id with another version (a stale id: ObjectById is null)
# one console line, so no body frame runs between the writes and the call (as in
# tools/cov/specs/digitalguardian_dg31_knee.con)
C(f'var {BODY} dg.armor3 65535; var {BODY} dg.armor2 {knees[2] ^ 0x800}; '
  f'codecall {BODY} SetVortexAttackMode; var {BODY} dg.armor2 {knees[2]}; '
  f'var {BODY} dg.armor3 {knees[3]}')
# (a fast run renders every 8th update: the flag stays up for a few drawn frames)
at(188)
var('dg.flags', flags)
at(190, f'fsm {BODY} ExitVortexAttack')
at(210, f'fsm {BODY} Idle')
# the echo targets: extending (the transmit sound outside and inside the echo visor), hit alive,
# extended (the sound keeps playing), one killed while it plays, a hit on an ignored survivor,
# retracting to retracted, then extract and retract again inside the extension
at(230, f'codecall {HEAD} ExtractAntenna; codecall {HEAD} ExtractEchoTargets')
T = re.findall(r'^\s*(u[0-9a-f]{4})\b', C('objs EchoTarget'), re.M)
print('[dg31] targets', T, flush=True)
at(260, 'visor echo')
at(300, f'damage {T[0]} 0.01')
at(420, 'visor combat')
at(480, f'damage {T[1]} 0.01; codecall {HEAD} ExtractEchoTargets; objs EchoTarget')
at(500, f'damage {T[0]} 1000')
at(505, f'damage {T[1]} 1')
at(520, f'codecall {HEAD} RetractEchoTargets')
at(620, f'objs EchoTarget; codecall {HEAD} ExtractEchoTargets')
at(640, f'codecall {HEAD} RetractEchoTargets')
# fn_14_18B4's flag 0x10 again, up to the end (the body may not be drawn in the first window)
at(700)
var('dg.flags', var('dg.flags') | 0x10)
at(780, 'status')
