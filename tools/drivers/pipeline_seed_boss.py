#!/usr/bin/env python3
"""Fights a boss through the console for the pipeline seed (tools/pipeline_seed.py), no probes.

usage: pipeline_seed_boss.py <port> <frames> <outfile> <layers> <filter> [tp x y z]

Waits for the warp (frame 300), activates <layers> (comma-separated) of the boot area, gives god
mode, every beam with charge, missiles and the ball upgrades with infinite ammo, then sends
Activate to every object `objs <filter>` lists (the intro cinema's job in retail). Until <frames>
it cycles the beams and fires plain, charged and missile shots at each boss, and steps its
health down so the later stages and the death run: what matters for the seed is that each
attack, hit and stage effect is drawn once, not how the fight goes. New matches (stages that
spawn later) are picked up on the way. `tp` puts the player at x y z once the boss is found.
"""
import re
import socket
import sys
import time

if len(sys.argv) < 6:
    sys.exit(__doc__)
port, frames, outfile = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
layers, pattern = sys.argv[4].split(','), sys.argv[5]
tp = sys.argv[7:10] if len(sys.argv) >= 10 and sys.argv[6] == 'tp' else None


def emit(s):
    print(s, flush=True)
    with open(outfile, 'a') as o:
        o.write(s + '\n')


t0 = time.time()
s = None
while s is None and time.time() - t0 < 600:
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=5)
    except OSError:
        time.sleep(0.5)
if s is None:
    emit('[boss] no connect')
    sys.exit(1)
f = s.makefile('rw')
s.settimeout(60)


def req(c):
    try:
        f.write(c + '\n')
        f.flush()
    except OSError:  # the game exited
        emit('[boss] console closed')
        sys.exit(0)
    lines, pending = [], c.count(';') + 1
    while True:
        l = f.readline()
        if not l:
            emit('[boss] console closed')
            sys.exit(0)
        l = l.rstrip()
        if l.startswith('=> '):
            if l != '=> ok':
                emit('[boss] %s %s' % (c, l))
            pending -= 1
            if pending == 0:
                return lines
            continue
        lines.append(l)


def frame():
    for l in req('frame'):
        m = re.search(r'frame (\d+)', l)
        if m:
            return int(m.group(1))
    return -1


def wait(n):
    while frame() < n:
        time.sleep(0.2)


def bosses():
    return [l.split()[0] for l in req('objs ' + pattern) if re.match(r'u[0-9a-f]{4} ', l)]


def hp(uid):
    for l in req('obj ' + uid):
        m = re.match(r'\s*hp ([-\d.]+)', l)
        if m:
            return float(m.group(1))
    return None


wait(300)
for layer in layers:
    req('layer ' + layer)
wait(400)
req('god 1; infammo all 1; give chargeBeam 1; give darkBeam 1; give lightBeam 1; give annihilatorBeam 1; '
    'give missile 250; give morphBall 1; give morphBallBombs 1; give boostBall 1; give powerbomb 10')
found, start = {}, frame()
while not found and frame() < start + 900:
    for uid in bosses():
        found[uid] = hp(uid)
    time.sleep(0.5)
if not found:
    emit('[boss] nothing matches %r on layers %s' % (pattern, sys.argv[4]))
    sys.exit(0)
for uid in found:
    req('send %s Activate' % uid)
emit('[boss] %s' % ' '.join('%s hp %s' % kv for kv in found.items()))
if tp:
    req('tp ' + ' '.join(tp))

# Health steps down in eight hits spread over the first 80% of the run, so every stage and the
# death are reached before the run ends.
first = frame()
span = max(1, int((frames - first) * 0.8))
cycle = 0
while frame() < frames:
    beam = cycle % 4
    req('beam %d' % beam)
    for uid in bosses():
        if uid not in found:
            found[uid] = hp(uid)
            req('send %s Activate' % uid)
            emit('[boss] new %s hp %s' % (uid, found[uid]))
        req('shoot %s' % uid)
        req('shoot %s charged' % uid)
        if cycle % 3 == 0:
            req('shoot %s missile' % uid)
        full = found[uid]
        step = int((frame() - first) * 8 / span)
        now = hp(uid)
        if full and now is not None and now > 0 and step >= 1 and now > full * (1 - step / 8.0):
            req('damage %s %d 0 %s' % (uid, max(1, int(full / 8)), ('power', 'dark', 'light', 'annihilator')[beam]))
    cycle += 1
    time.sleep(0.3)
emit('[boss] done after %d cycles' % cycle)
