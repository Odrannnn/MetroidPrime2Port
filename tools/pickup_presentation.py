#!/usr/bin/env python3
"""Check the seamless-pickup rule against every single-player pickup on the disc.

The `seamless_pickups` setting (CScriptPickup::Touch under TARGET_PC) walks a pickup's Arrived
connections, then everything the relays, timers and sequence timers it reaches send on, and marks
three kinds of target for a short while:

  CTLH  a control hint the chain increments: that increment is dropped (no control lock)
  MEMO  a HUD memo the chain zeroes: a message box shows as a non-pausing memo instead
  CAMR  a cinematic camera the chain activates: when it comes on, CMFGame skips the scene the way
        its skip button does, so every door, platform and layer the scene changes still changes

Everything else the chain reaches plays as retail. This is that walk on the disc data: it prints
what the rule would mark and fails unless every pickup reaches exactly one message box and one
control hint, both the item template's ("Pickup Acquired", "Disable Controls"), and the counts
below still hold. With -v it prints every pickup's chain.

  python3 tools/pickup_presentation.py [-v]
"""
import argparse
import collections
import sys

import echoes_disc as ed
import rando_models as rm
import rando_world as rw

# The types the walk follows (CScriptRelay, CScriptMemoryRelay, CScriptTimer, CScriptSequenceTimer).
FLOW = {"SRLY", "MRLY", "TIMR", "SQTR"}
# Measured 2026-11-03 (G2ME01): the walk and the runtime rule agree only while these hold.
EXPECT_PICKUPS = 124       # PCKP in Metroid1-5.pak, refills (no percentage) left out
EXPECT_CINEMATIC = 20      # of them reach a cinematic camera


def name(ent):
    v = rw.editor(ent[4], rw.INAM) if ent else None
    return v.rstrip(b"\0").decode("latin1", "replace") if v else ""


def walk(by, pickup):
    """[(depth, src, state, msg, target, ttype)] the pickup's Arrived chain reaches."""
    out = []
    seen = set()
    q = [(pickup, 0)]
    while q:
        cur, depth = q.pop(0)
        for state, msg, tgt in by[cur][3]:
            if cur == pickup and state != "ARRV":
                continue
            te = by.get(tgt)
            tt = te[1] if te else "????"
            out.append((depth, cur, state, msg, tgt, tt))
            if tt in FLOW and tgt not in seen:
                seen.add(tgt)
                q.append((tgt, depth + 1))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", action="store_true", help="print every pickup's chain")
    args = ap.parse_args()
    iso = ed.Iso(ed.find_iso())
    pickups = cinematic = 0
    marked = collections.Counter()
    bad = []
    for pk in ed.load_paks(iso):
        if not rm.SINGLE_PLAYER_PAKS.search(pk.path):
            continue
        for wname, (kind, mlvl) in sorted(pk.names.items(), key=lambda kv: kv[1][1]):
            if kind != "MLVL":
                continue
            for a in ed.parse_mlvl(pk.get(mlvl))["areas"]:
                ents = []
                for sec in ed.mrea_layers(pk.get(a["mrea"])):
                    for t4, eid, conns, body in rw.scly_entities(sec):
                        ents.append((None, t4, eid, conns, rm.pickup_props(body)))
                by = {e[2]: e for e in ents}
                for e in ents:
                    if e[1] != "PCKP":
                        continue
                    item = rw.i32(e[4], rm.ITEM_PROP, -1)
                    if rw.i32(e[4], rw.PERCENT_PROP, 0) == 0 and item in rw.REFILLS:
                        continue
                    pickups += 1
                    head = f"{wname} {a['iname']} PCKP {e[2]:08X} {name(e)!r} item {item}"
                    chain = walk(by, e[2])
                    memos, hints, cams = set(), set(), set()
                    for d, src, st, msg, tgt, tt in chain:
                        te = by.get(tgt)
                        if args.v:
                            print(f"{'  ' * d}{st}->{msg} {tt}:{tgt:08X} {name(te)}")
                        if tt == "MEMO" and msg == "ZERO":
                            memos.add(name(te))
                        elif tt == "CTLH" and msg == "INCR":
                            hints.add(name(te))
                        elif tt == "CAMR" and msg == "ACTV":
                            cams.add(tgt)
                        elif tt == "????":
                            bad.append(f"{head}: {st}->{msg} to a missing object {tgt:08X}")
                    if args.v:
                        print(f"== {head}: {len(cams)} cinematic camera(s)")
                    if sorted(memos) != ["Pickup Acquired"] or sorted(hints) != ["Disable Controls"]:
                        bad.append(f"{head}: memos {sorted(memos)}, control hints {sorted(hints)}")
                    cinematic += bool(cams)
                    marked["MEMO"] += len(memos)
                    marked["CTLH"] += len(hints)
                    marked["CAMR"] += len(cams)
    print(f"pickups {pickups}, with a cinematic {cinematic}; marked {dict(marked)}")
    if (pickups, cinematic) != (EXPECT_PICKUPS, EXPECT_CINEMATIC):
        bad.append(f"expected {EXPECT_PICKUPS} pickups, {EXPECT_CINEMATIC} with a cinematic")
    for b in bad:
        print("FAIL", b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
