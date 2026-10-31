#!/usr/bin/env python3
"""Regression: a load executed by a held step must restart without a later `hold 0`.

Start a gameplay run with MP2_CONSOLE and enough MP2_FRAMES, then:
    python3 tools/drivers/savestate_restart_hold.py PORT GAME_LOG [--loads 60]
Uses scratch save slot 8 and leaves the restored game held after the final load.
"""
import argparse
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from statecheck import Console


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port", type=int)
    ap.add_argument("log", type=Path)
    ap.add_argument("--loads", type=int, default=60)
    ap.add_argument("--timeout", type=float, default=60)
    a = ap.parse_args()
    con = Console(a.port, a.timeout)

    def count(text):
        return a.log.read_text(errors="replace").count(text) if a.log.exists() else 0

    def wait(text, before):
        deadline = time.monotonic() + a.timeout
        while count(text) <= before:
            if time.monotonic() >= deadline:
                raise RuntimeError(f"no '{text}' within {a.timeout}s; see {a.log}")
            time.sleep(0.05)

    try:
        wait("[harness] gameplay reached", 0)
        for i in range(a.loads):
            con.cmd("hold 1")
            saved = count("[savestate] saved slot 8")
            con.cmd("state save 8")
            con.cmd("step 1")
            wait("[savestate] saved slot 8", saved)
            restored = count("[savestate] room state restored")
            con.cmd("state load 8 hold")
            # Force the ordering that normally depends on a pending step winning the
            # race against the client's next `hold 0`: load while still held, then
            # let FrameEnd remove CMFGame. Never send hold 0 to rescue the restart.
            con.cmd("step 1")
            wait("[savestate] room state restored", restored)
            _, held = con.cmd("hold")
            if "hold 1" not in held:
                raise RuntimeError("load did not re-hold after restoring the room")
            print(f"load {i + 1}/{a.loads}: restored and held", flush=True)
    finally:
        con.sock.close()


if __name__ == "__main__":
    main()
