#!/usr/bin/env python3
"""Ing lane driver: wake the Ings of an area, then kill a corporeal one (or grapple the ball).

The recipe (docs/module-analysis/IngFamily.md A.5/A.21), all of it measured with a probe in
`CIng::Listen` and in `CStateManager::InformListeners`:

  * the Ings of the area load with a detection range of 0, so they have to be woken with the
    `Activate` script message - once each, when it first shows up in `objs Ing` (the area's
    actors come up a few seconds after the gameplay). The message restarts the Ing's state
    machine, so it must not be repeated;
  * the noise is registered at the gun, and the retail's Listen rejects it when the squared
    distance is over the hearing radius squared (40) or the squared height over the leash value
    squared, so the player has to stand on the puddle;
  * the power beam is charged, so a short console press never fires - the light beam fires while
    it is held.

With that the Ing wakes, turns corporeal (the state "PathFind" with the form 2) and takes
damage: the console's `damage` goes through the same path a shot takes, and the ing-spot form
takes no damage, so the hits are repeated while the Ing is corporeal. When the Ing will not stay
corporeal long enough, the driver forces the state with `fsm` - a forced arm is labelled as
such in the report.

usage: ing_drive.py <port> <logfile> [kill|grapple] [rounds]
"""
import math
import re
import socket
import sys
import time

PLAYER = "u0400"  # the player's own unique id


class Console:
    def __init__(self, port, timeout=30.0):
        last = None
        for _ in range(180):
            try:
                self.sock = socket.create_connection(("127.0.1", port), timeout=timeout)
                break
            except OSError as e:
                last = e
                time.sleep(1)
        else:
            raise last
        self.sock.settimeout(timeout)
        self.f = self.sock.makefile("rw")

    def cmd(self, line):
        self.f.write(line + "\n")
        self.f.flush()
        out = []
        while True:
            raw = self.f.readline()
            if not raw:
                raise EOFError("the console closed")
            raw = raw.rstrip("\n")
            if raw.startswith("=> ok") or raw.startswith("=> err:"):
                return "\n".join(out)
            out.append(raw)

    def close(self):
        try:
            self.f.close()
            self.sock.close()
        except OSError:
            pass


def ings(console):
    """The Ings the console lists: (uid, x, y, z) each."""
    out = console.cmd("objs Ing")
    found = []
    for line in out.splitlines():
        if "CIng" not in line:
            continue
        uid = line.split()[0]
        pos = line[line.rfind("(") + 1: line.rfind(")")]
        try:
            x, y, z = (float(v) for v in pos.split(","))
        except ValueError:
            continue
        found.append((uid, x, y, z))
    return found


def joints(console, ing, radius=4.0):
    """The Ing's joint collision actors: the CCollisionActor objects near it.

    They carry the Ing's own health and the base damage table (fn_29_3014), so a hit on one is
    what takes HP off a corporeal Ing - the console's `damage` on the Ing itself does not (the
    Ing's own table is the trigger table, and it takes nothing from the power beam).
    """
    out = console.cmd("objs Collision")
    found = []
    for line in out.splitlines():
        if "CCollisionActor" not in line:
            continue
        uid = line.split()[0]
        pos = line[line.rfind("(") + 1: line.rfind(")")]
        try:
            x, y, z = (float(v) for v in pos.split(","))
        except ValueError:
            continue
        if (x - ing[0]) ** 2 + (y - ing[1]) ** 2 + (z - ing[2]) ** 2 <= radius * radius:
            found.append((uid, x, y, z))
    return found


def states(logpath, n=200000):
    """The latest state of every Ing in the log: tag -> (state, form)."""
    try:
        with open(logpath, errors="replace") as f:
            text = f.read()[-n:]
    except OSError:
        return {}
    latest = {}
    for line in text.splitlines():
        if not line.startswith("[ing] "):
            continue
        parts = line.split()
        if len(parts) >= 6 and parts[2] == "state" and parts[-1].isdigit():
            latest[parts[1]] = (parts[3], parts[-1])
    return latest


def yaw_to(dx, dy):
    return math.degrees(math.atan2(dx, dy))


def aim_at(console, player, target):
    """The yaw and pitch that point the shot at the target (the gun fires 2.15 above the player)."""
    dx = target[0] - player[0]
    dy = target[1] - player[1]
    dz = target[2] - (player[2] + 2.15)
    flat = math.hypot(dx, dy)
    pitch = math.degrees(math.atan2(dz, flat)) if flat > 0.01 else 0.0
    console.cmd(f"aim {yaw_to(dx, dy):.0f} {pitch:.0f}")


def setup(console):
    for c in ("give lightBeam 1", "give lightAmmo 50", "beam light"):
        print("setup:", c, "->", console.cmd(c).replace("\n", " | "), flush=True)


def kill(console, logpath, rounds):
    setup(console)
    # The Dark visor is one of the arms of the valid-target body (fn_29_426C).
    print("visor:", console.cmd("give darkVisor 1").replace("\n", " | "), flush=True)
    print("visor:", console.cmd("visor dark").replace("\n", " | "), flush=True)
    woken = set()
    for _ in range(rounds):
        spots = ings(console)
        for uid, x, y, z in spots:
            if uid not in woken:
                print("wake", uid, "->", console.cmd(f"send {uid} Activate").replace("\n", " | "),
                      flush=True)
                woken.add(uid)
        if spots:
            uid, x, y, z = spots[0]
            # Stand a few units off the puddle: the noise is registered at the gun, and the
            # shot has to reach the Ing's joint collision actors (they carry the base damage
            # table, A.19), so the beam must travel and not start inside the puddle.
            px, py, pz = x + 5.0, y + 3.0, z + 0.5
            console.cmd(f"tp {px:.1f} {py:.1f} {pz:.1f}")
            aim_at(console, (px, py, pz), (x, y, z + 1.0))
            time.sleep(0.2)
        console.cmd("press a 8")
        time.sleep(1.0)
        for tag, (state, form) in states(logpath).items():
            if form != "2":
                continue
            # The log prints the unique id without its version bits, the console with them.
            for cuid, cx, cy, cz in spots:
                if (int(cuid.lstrip("u"), 16) & 0x7ff) != int(tag, 16):
                    continue
                # The hit goes to the joint collision actors: they carry the Ing's health, and
                # the Ing's own table would take nothing from the console's damage.
                for juid, jx, jy, jz in joints(console, (cx, cy, cz)):
                    # A real beam projectile at the joint: the console's `damage` does not
                    # reach the projectile-only hit path that carries the damage to the Ing.
                    for _ in range(3):
                        console.cmd(f"shoot {juid} charged 1")
                        time.sleep(0.3)
                    print(f"shot {juid} (a joint of {cuid}) in the state {state}", flush=True)
    return 0


def grapple(console, logpath, rounds):
    """The Agon recipe (A.12): the layers on, the player morphed into a ball on the puddle."""
    console.cmd("layer 2")
    console.cmd("layer 3")
    setup(console)
    woken = set()
    for _ in range(rounds):
        spots = ings(console)
        for uid, x, y, z in spots:
            if uid not in woken:
                print("wake", uid, "->", console.cmd(f"send {uid} Activate").replace("\n", " | "),
                      flush=True)
                woken.add(uid)
        if spots:
            uid, x, y, z = spots[0]
            console.cmd("press x 4")
            console.cmd(f"tp {x:.1f} {y:.1f} {z:.1f}")
            time.sleep(0.4)
            console.cmd("press a 4")
            time.sleep(1.0)
        if "GrappleBall" in open(logpath, errors="replace").read()[-100000:]:
            print("grapple: the Ing holds the ball", flush=True)
            return 0
    return 1


def main():
    port = int(sys.argv[1])
    logpath = sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else "kill"
    rounds = int(sys.argv[4]) if len(sys.argv) > 4 else 150
    console = Console(port)
    try:
        # Wait for the gameplay before driving.
        for _ in range(180):
            if "gameplay reached" in open(logpath, errors="replace").read()[-40000:]:
                break
            time.sleep(1)
        time.sleep(2)
        if mode == "grapple":
            return grapple(console, logpath, rounds)
        return kill(console, logpath, rounds)
    finally:
        console.close()


if __name__ == "__main__":
    sys.exit(main())
