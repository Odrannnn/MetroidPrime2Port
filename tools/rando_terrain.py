#!/usr/bin/env python3
"""Terrain reachability inside each area, from the collision mesh on the disc, checked against the
hand rules (tools/rando_rules/*.rules).

The hand rules guess what movement inside a room needs: how high a ledge is, whether a gap can be
jumped. This tool measures it instead. For each area it reads the static collision (the MREA's
CAreaOctTree section), finds where Samus can stand, links those spots by walking, falling, rolling
and jumping, and works out what a rule edge needs from the mesh alone: the smallest sets of
MorphBall, SpaceJumpBoots, GravityBoost, SpiderBall, LightSuit and BoostBall that make it, or no
path at all (the edge relies on something the static mesh does not have: a moving platform, an
elevator, a grapple point, a spinner, a ball cannon, a bomb slot).

    tools/rando_terrain.py                    report every rule edge whose terms the terrain refutes
    tools/rando_terrain.py --area NAME        check one area only
    tools/rando_terrain.py --all              print every checked edge, not only the refuted ones
    tools/rando_terrain.py --no-cache         recheck every area (verdicts are cached in CACHE)

The model, and its limits (docs/RANDOMIZER.md, "Terrain check"):
- Physics from Standard.ntwk: gravity 35, jump force 50 for 0.25 s (apex 3.78), double jump impulse
  8 then force 60 for 0.1 s (apex 7.68), air speed 16.5, step up 1.0, height 2.7, radius 0.5, ball
  radius 0.7. Analytical, not measured in game; the margins below are deliberately wide.
- Floors are what CGameCollision::IsFloor says: the Floor material, or normal z > 0.85, facing up (a
  ceiling's top is no floor); collision blocks from both sides, as the game's does.
- Water: WATR boxes on the default layers that are never deactivated. Underwater jumps follow
  CPlayerDynamics (fluid gravity 10, jump factor 0.37 while submerged, depth from the box top), with
  Gravity Boost (force 45 for 1.5 s, from an apex or an early trigger, both counted) and dark water
  (low gravity everywhere in it unless the Light Suit, no jump factor), simulated per depth bucket.
  A fluid never lowers a jump below the dry one: the vanilla route climbs underwater stairs past
  the simulated walk jump (jump_sim), so water only adds height.
- Spider Ball: tracks (material bit 61) sampled into cells, a track is its connected cells; the ball
  gets on from a floor at most SPIDER_TOUCH below a cell and gets off onto any floor beside or below
  the track.
- Boost Ball: half-pipes (material bit 13) the same way, on from a floor at most PIPE_TOUCH below a
  cell, off onto any floor beside a cell up to PIPE_LIP (ballHalfPipeBoostHeight 3.2) above it or
  below: boosting builds up to 95 a second there, enough for any half-pipe on the disc. Boosted gap
  crossings, ramps and spider-track boosts are not modelled.
- Swimming, Screw Attack, bomb jumps, dash and slope jumps are not modelled: the result only ever
  needs more than the game does, never less.
- Static mesh plus script platforms and solid actors (PLAT, ACTR with isSolid: DCLN, collision box
  or model bounds) at their initial place on the default layers, in two states: as the area starts
  (breakable crates included), and once its switches are thrown (activated, never deactivated or
  destroyed); an edge is also tried on the static mesh alone, so these only ever add paths. Moving
  platforms, story layer switches, cannons, doors, gates and elevators are not modelled.
- A jump may end in a ball-only spot (a tunnel mouth above the floor): Samus rises standing, morphs
  near the apex and crosses at ball height; both legs are checked against the mesh. A ball rolls
  off a tunnel lip onto a lower floor up to DROP_REACH (1.5) ahead.
- Terms with an UNMODELLED item (grapple, Screw Attack, bombs, ...) are not refuted.
- Enforced by tools/rando_rules.py: a refuted edge fails unless tools/rando_rules/terrain_accepted.txt
  lists it as a gap of this model.
"""
import functools
import struct
import sys
import pathlib

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import echoes_disc as ed  # noqa: E402
import rando_models as rm  # noqa: E402
import savw_diff  # noqa: E402

CACHE = pathlib.Path.home() / ".cache" / "mp2-rando-terrain"    # verdicts per area, keyed by inputs
GRID = 0.5            # floor sample spacing
FLOOR_NZ = 0.85       # normal z of a floor without the Floor material (CGameCollision::IsFloor)
FLOOR_FLAGGED_NZ = 0.3  # a Floor-material triangle steeper than this is a wall for standing
HEIGHT = 2.7          # player height
BALL = 1.4            # ball diameter
STEP_UP = 1.0
CLEAR = 1.1           # body test height above the feet (above a step)
GRAVITY = 35.0
AIR_SPEED = 16.5
SPEED_FACTOR = 0.6    # of the air speed counted as horizontal reach
RISE_MARGIN = 0.4     # the feet must clear a ledge by this much
NO_PLAYER = 1 << 58
FLOOR = 1 << 31
FLIPPED = 1 << 24

SPIDER_TRACK = 1 << 61   # kMT_Unknown61: the Spider Ball sticks to it
HALF_PIPE = 1 << 13      # kMT_HalfPipe: CMorphBall::UpdateHalfPipeStatus, boosting up to 95 a second

NONE, MORPH, SJ, GRAV, SPIDER, LIGHT, BOOST = 0, 1, 2, 4, 8, 16, 32     # ability bits
ALL = MORPH | SJ | GRAV | SPIDER | LIGHT | BOOST
BIT_ITEMS = {"MorphBall": MORPH, "SpaceJumpBoots": SJ, "GravityBoost": GRAV, "SpiderBall": SPIDER,
             "LightSuit": LIGHT, "BoostBall": BOOST}
# movement the mesh check does not model (Screw Attack, grapple, bomb jumps): a rule term with one
# of these is not refuted
UNMODELLED = ["ScrewAttack", "GrappleBeam", "MorphBallBombs"]


def set_text(s):
    return " & ".join(n for n, b in BIT_ITEMS.items() if s & b) or "walk"


# ---- the collision section -----------------------------------------------------------------------

def mrea_section(b, want):
    h = struct.unpack_from(">32I", b, 0)
    nsec, nblk = h[16], h[28]
    pos = 0x80
    sizes = struct.unpack_from(">%dI" % nsec, b, pos)
    pos += (nsec * 4 + 31) & ~31
    blocks = [struct.unpack_from(">4I", b, pos + 16 * i) for i in range(nblk)]
    pos += (nblk * 16 + 31) & ~31
    si = 0
    for buf, dl, cl, sc in blocks:
        rd = ((cl or buf) + 31) & ~31
        if si <= want < si + sc:
            raw = b[pos:pos + rd]
            data = ed.inflate_chunks(raw[rd - cl:], dl) if cl else raw[:dl]
            off = sum(sizes[si:want])
            return data[off:off + sizes[want]]
        pos += rd
        si += sc
    raise ValueError(f"no MREA section {want}")


def area_mesh(mrea):
    """(triangles (n,3,3) float, material flags (n,) uint64) of the MREA's static collision."""
    sec = mrea_section(mrea, struct.unpack_from(">32I", mrea, 0)[20])
    assert sec[0] == 1
    (size,) = struct.unpack_from(">I", sec, 4)
    c = sec[8:8 + size]
    magic, _ver = struct.unpack_from(">II", c, 0)
    assert magic == 0xDEAFBABE
    p = 8 + 24
    _ttype, tsize = struct.unpack_from(">II", c, p)
    tris, flags, p = index_tris(c, p + 8 + tsize)
    assert p == len(c), (p, len(c))
    return tris, flags


def index_tris(c, p):
    """(triangles, material flags, end offset) of the CCollisionIndexData at c[p:], players' only."""
    def u32():
        nonlocal p
        p += 4
        return struct.unpack_from(">I", c, p - 4)[0]

    def arr(dt, n):
        nonlocal p
        a = np.frombuffer(c, dt, n, p)
        p += a.nbytes
        return a

    mats = arr(">u8", u32()).astype(np.uint64)
    for _ in range(2):              # vertex and edge materials
        arr("u1", u32())
    smat = arr("u1", u32())
    edges = arr(">u2", u32() * 2).reshape(-1, 2).astype(np.int64)
    nt = u32() // 3
    sidx = arr(">u2", nt * 3).reshape(-1, 3).astype(np.int64)
    arr(">u2", u32())               # extra indices
    verts = arr(">f4", u32() * 3).reshape(-1, 3).astype(np.float64)
    flags = mats[smat[:nt]]
    e0, e1 = edges[sidx[:, 0]], edges[sidx[:, 1]]
    flip = ((flags & np.uint64(FLIPPED)) != 0)
    tri = np.where(flip[:, None], np.stack([e0[:, 1], e0[:, 0], e1[:, 0]], 1),
                   np.stack([e0[:, 0], e0[:, 1], e1[:, 1]], 1))
    keep = (flags & np.uint64(NO_PLAYER)) == 0
    return verts[tri[keep]], flags[keep], p


# ---- script platforms ----------------------------------------------------------------------------

PLAT_BOX, PLAT_OFFSET = 0xF344C0B0, 0x2E686C2A    # SLdrPlatform.inc collisionBox, collisionOffset
PLAT_MODEL, PLAT_DCLN = 0xC27FFA8F, 0x0FC966DC    # model, collisionModel
EDITOR_ACTIVE = 0x41435456                         # EditorProperties active
ACTR_SOLID = 0x1D8DD846                             # SLdrActor.inc isSolid (same box/model ids)


def euler(deg):
    """ConvertEditorEulerToTransform4f's rotation: Z * Y * X, degrees."""
    x, y, z = np.radians(deg)
    rx = np.array([[1, 0, 0], [0, np.cos(x), -np.sin(x)], [0, np.sin(x), np.cos(x)]])
    ry = np.array([[np.cos(y), 0, np.sin(y)], [0, 1, 0], [-np.sin(y), 0, np.cos(y)]])
    rz = np.array([[np.cos(z), -np.sin(z), 0], [np.sin(z), np.cos(z), 0], [0, 0, 1]])
    return rz @ ry @ rx


def box_tris(lo, hi):
    c = np.array([[(lo, hi)[i >> k & 1][k] for k in range(3)] for i in range(8)], float)
    quads = [(0, 2, 6, 4), (1, 5, 7, 3), (0, 4, 5, 1), (2, 3, 7, 6), (0, 1, 3, 2), (4, 6, 7, 5)]
    return np.array([c[[q[0], q[i + 1], q[i]]] for q in quads for i in (1, 2)])   # wound outward


def dcln_tris(d):
    """Triangles and flags of a DCLN (COBBTreeGroup): u32 count, then per tree DEAFBABE, version,
    size, the collision index data and an OBB tree (skipped by finding the next magic)."""
    (count,) = struct.unpack_from(">I", d, 0)
    p, tris, flags = 4, [], []
    for i in range(count):
        magic, _ver, _size = struct.unpack_from(">III", d, p)
        assert magic == 0xDEAFBABE, hex(magic)
        t, f, p = index_tris(d, p + 12)
        tris.append(t)
        flags.append(f)
        if i + 1 < count:
            p = d.index(b"\xde\xaf\xba\xbe", p)
    return np.concatenate(tris), np.concatenate(flags)


def platforms(ents, layer_on, get):
    """[(triangles, flags, names)] of the PLATs and solid ACTRs that stand in the area as it starts,
    then once its switches are thrown, as CScriptPlatform and CScriptActor collide: the DCLN under
    the object's rotation and position, else the collision box (a PLAT's unrotated, an ACTR's scaled
    and its bounds under the rotation), else the scaled model's bounds under the rotation. An object
    counts when its layer is on by default: later if it starts active or is sent ACTV and is never
    sent DCTV (a raised drawbridge, a statue before it falls, a blocker during a crate drop are
    not); at the start if it starts active or counts later (what an entry relay brings, such as the
    landed ship; both positions of a moved statue may count, an optimistic union). Motion is not
    followed. One that a DEAD removes (DECR, DCTV) is an obstacle for the hand rules and counts in
    neither state. A later state equal to the start is left out."""
    import rando_world as rw
    sent = {(m, t) for *_x, conns, _p in ents for _s, m, t in conns}
    killed = {t for *_x, conns, _p in ents for s, m, t in conns if s == "DEAD" and m in rw.KILL_MSGS}
    shapes, start, later = {}, set(), set()
    for layer, t4, eid, _conns, props in ents:
        if t4 not in ("PLAT", "ACTR") or not layer_on >> layer & 1:
            continue
        if t4 == "ACTR" and props.get(ACTR_SOLID, b"\0")[-1:] == b"\0":
            continue
        active = rw.editor(props, EDITOR_ACTIVE)[-1:] != b"\0"
        if (active or ("ACTV", eid) in sent) and ("DCTV", eid) not in sent and eid not in killed:
            later.add(eid)
        if active or eid in later:
            start.add(eid)
        if eid not in start and eid not in later:
            continue
        x = struct.unpack(">9f", rw.editor(props, rw.XFRM)[:36])
        pos, rot, scale = np.array(x[:3]), euler(x[3:6]), np.array(x[6:])
        dcln = get(struct.unpack(">I", props[PLAT_DCLN][-4:])[0]) if PLAT_DCLN in props else None
        box = np.array(struct.unpack(">3f", props[PLAT_BOX])) if PLAT_BOX in props else np.zeros(3)
        if dcln is not None:
            t, f = dcln_tris(dcln)
            t = t @ rot.T + pos
        elif box.any():
            off = np.array(struct.unpack(">3f", props[PLAT_OFFSET])) if PLAT_OFFSET in props else 0
            lo, hi = off - box / 2, off + box / 2
            if t4 == "ACTR":
                corners = box_tris(lo * scale, hi * scale).reshape(-1, 3) @ rot.T
                lo, hi = corners.min(0), corners.max(0)
            t, f = box_tris(pos + lo, pos + hi), None
        else:
            cmdl = get(struct.unpack(">I", props[PLAT_MODEL][-4:])[0]) if PLAT_MODEL in props else None
            if cmdl is None:
                start.discard(eid)
                later.discard(eid)
                continue
            b = np.array(struct.unpack_from(">6f", cmdl, 12)).reshape(2, 3) * scale
            corners = box_tris(b[0], b[1]).reshape(-1, 3) @ rot.T
            t, f = box_tris(pos + corners.min(0), pos + corners.max(0)), None
        shapes[eid] = (t, np.zeros(len(t), np.uint64) if f is None else f,
                       rw.editor(props, rw.INAM).rstrip(b"\0").decode("latin1"))
    out = []
    for ids in [start] + ([later] if later != start else []):
        if ids:
            got = [shapes[e] for e in sorted(ids)]
            out.append((np.concatenate([g[0] for g in got]), np.concatenate([g[1] for g in got]), [g[2] for g in got]))
    return out


WATR_FLUID = 0x7D606B78    # SLdrWater.inc fluidType: 0 water, 1 Fluid (Echoes' water), 2 Dark Water


def waters(ents, layer_on):
    """(lo, hi, dark) boxes of the WATRs in the area for good: layer on by default, active, never
    sent DCTV or killed (water a switch drains or raises later is left out, the generous way)."""
    import rando_world as rw
    sent = {(m, t) for *_x, conns, _p in ents for _s, m, t in conns}
    killed = {t for *_x, conns, _p in ents for s, m, t in conns if s == "DEAD" and m in rw.KILL_MSGS}
    lo, hi, dark = [], [], []
    for layer, t4, eid, _conns, props in ents:
        if (t4 != "WATR" or not layer_on >> layer & 1 or rw.editor(props, EDITOR_ACTIVE)[-1:] == b"\0"
                or ("DCTV", eid) in sent or eid in killed):
            continue
        x = struct.unpack(">9f", rw.editor(props, rw.XFRM)[:36])    # centre and full size
        lo.append(np.array(x[:3]) - np.abs(x[6:]) / 2)
        hi.append(np.array(x[:3]) + np.abs(x[6:]) / 2)
        dark.append(WATR_FLUID in props and struct.unpack(">i", props[WATR_FLUID][-4:])[0] == 2)
    return np.array(lo).reshape(-1, 3), np.array(hi).reshape(-1, 3), np.array(dark, bool)


# ---- geometry ------------------------------------------------------------------------------------

def column_hits(tris):
    """Where each vertical line of the GRID crosses a triangle: (column ix, iy, z, triangle index)."""
    xy = tris[:, :, :2] / GRID
    lo = np.ceil(xy.min(1)).astype(np.int64)
    n = np.maximum(np.floor(xy.max(1)).astype(np.int64) - lo + 1, 0)
    cnt = n[:, 0] * n[:, 1]
    t = np.repeat(np.arange(len(tris)), cnt)
    k = np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)
    ix, iy = lo[t, 0] + k % n[t, 0], lo[t, 1] + k // n[t, 0]
    a, b, c = tris[t, 0], tris[t, 1], tris[t, 2]
    px, py = ix * GRID, iy * GRID
    e1x, e1y, e2x, e2y = b[:, 0] - a[:, 0], b[:, 1] - a[:, 1], c[:, 0] - a[:, 0], c[:, 1] - a[:, 1]
    d = e1x * e2y - e1y * e2x
    ok = np.abs(d) > 1e-9
    d = np.where(ok, d, 1.0)
    sx, sy = px - a[:, 0], py - a[:, 1]
    u = (sx * e2y - sy * e2x) / d
    v = (e1x * sy - e1y * sx) / d
    ok &= (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6)
    z = a[:, 2] + u * (b[:, 2] - a[:, 2]) + v * (c[:, 2] - a[:, 2])
    return ix[ok], iy[ok], z[ok], t[ok]


def cross(a, b):
    return np.stack([a[:, 1] * b[:, 2] - a[:, 2] * b[:, 1], a[:, 2] * b[:, 0] - a[:, 0] * b[:, 2],
                     a[:, 0] * b[:, 1] - a[:, 1] * b[:, 0]], 1)


class Segments:
    """Segment-against-mesh tests, with the triangles binned on a coarse XY grid."""
    CELL = 2.0

    def __init__(self, tris):
        self.a = tris[:, 0]
        self.e1 = tris[:, 1] - tris[:, 0]
        self.e2 = tris[:, 2] - tris[:, 0]
        self.lo, self.hi = tris.min(1), tris.max(1)
        lo = np.floor(tris[:, :, :2].min(1) / self.CELL).astype(np.int64)
        hi = np.floor(tris[:, :, :2].max(1) / self.CELL).astype(np.int64)
        keys, ids = self._cells(lo, hi)
        order = np.argsort(keys, kind="stable")
        self.keys, self.ids = keys[order], ids[order]

    @staticmethod
    def _cells(lo, hi):
        n = hi - lo + 1
        cnt = n[:, 0] * n[:, 1]
        idx = np.repeat(np.arange(len(lo)), cnt)
        k = np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)
        cx, cy = lo[idx, 0] + k % n[idx, 0], lo[idx, 1] + k // n[idx, 0]
        return (cx + (1 << 20)) * (1 << 21) + (cy + (1 << 20)), idx

    def blocked(self, p0, p1):
        """Whether each segment p0[i] -> p1[i] crosses a triangle (ends excluded)."""
        out = np.zeros(len(p0), bool)
        if not len(p0):
            return out
        lo = np.floor(np.minimum(p0, p1)[:, :2] / self.CELL).astype(np.int64)
        hi = np.floor(np.maximum(p0, p1)[:, :2] / self.CELL).astype(np.int64)
        keys, seg = self._cells(lo, hi)
        s = np.searchsorted(self.keys, keys, "left")
        e = np.searchsorted(self.keys, keys, "right")
        cnt = e - s
        seg = np.repeat(seg, cnt)
        k = np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)
        tri = self.ids[np.repeat(s, cnt) + k]
        slo, shi = np.minimum(p0, p1), np.maximum(p0, p1)
        near = np.all((self.lo[tri] <= shi[seg]) & (self.hi[tri] >= slo[seg]), axis=1)
        seg, tri = seg[near], tri[near]
        for i in range(0, len(seg), 1 << 21):
            sg, tr = seg[i:i + (1 << 21)], tri[i:i + (1 << 21)]
            o, dr = p0[sg], p1[sg] - p0[sg]
            e1, e2 = self.e1[tr], self.e2[tr]
            h = cross(dr, e2)
            det = np.einsum("ij,ij->i", e1, h)
            good = np.abs(det) > 1e-12
            f = 1.0 / np.where(good, det, 1.0)
            sv = o - self.a[tr]
            u = f * np.einsum("ij,ij->i", sv, h)
            q = cross(sv, e1)
            v = f * np.einsum("ij,ij->i", dr, q)
            t = f * np.einsum("ij,ij->i", e2, q)
            hit = good & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-4) & (t < 1 - 1e-4)
            out[sg[hit]] = True
        return out


def jump_curve(double):
    """Times and heights of a held jump (with a double jump at the apex when `double`)."""
    dt = 1 / 600
    t, z, vz, out = 0.0, 0.0, 0.0, []
    apex = 0.2495 + 50 * 0.2495 / GRAVITY
    while True:
        if t < 0.2495:
            a = 50.0
        elif double and apex <= t < apex + 0.1:
            if vz < 0.5 * dt * GRAVITY and t - apex < dt:
                vz = 8.0
            a = 60.0
        else:
            a = -GRAVITY
        vz += a * dt
        z += vz * dt
        t += dt
        out.append((t, z))
        if z < -12:
            return np.array(out)


CURVES = {NONE: jump_curve(False), SJ: jump_curve(True)}

# Fluids (CPlayer::GetGravity, JumpInput, ApplyGravityBoost; Tweaks.inc motion defaults)
JUMP_TIME = 0.2495     # allowedJumpTime
EYE = 2.5              # eye height: CheckSubmerged is half of it, the camera's fluid test all of it
WATER_GRAVITY = 10.0   # fluidGravitationalAccel
WATER_JUMP = 0.37      # water/lavaJumpFactor (Dark Water sets the Lava restraint)
GB_ACCEL, GB_TIME, GB_CANCEL = 45.0, 1.5, 0.3   # gravityBoostForce / mass 200, time, cancel dampening
DEPTH_STEP, DEPTH_MAX = 0.25, 24.0              # take-off depth buckets
JUMP_SETS = [s for s in range(LIGHT * 2) if s & ~(SJ | GRAV | LIGHT) == 0]


def _sim(abil, depth, dark, early):
    """Times and heights of a held jump taken `depth` under a fluid surface. Dark Water without the
    Light Suit: low gravity and the jump factor while in it at all (Gravity Boost lifts the factor,
    not the gravity). Water (or Dark Water with the Light Suit): both only when submerged (feet 1.25
    under) without Gravity Boost, the factor not in Dark Water. Then the double jump (SJ), then the
    Gravity Boost while the eye is in the fluid: at the apex, or with `early` as soon as allowed."""
    sj, grav, light = abil & SJ, abil & GRAV, abil & LIGHT
    dt = 1 / 600
    t = z = vz = 0.0
    dj = gb = None
    out = []
    while z >= -12 and t <= 10:
        d = depth - z
        if dark and not light:
            g = WATER_GRAVITY if d > 0 else GRAVITY
            scale = WATER_JUMP if d > 0 and not grav else 1.0
        else:
            sub = d >= EYE / 2 and not grav
            g = WATER_GRAVITY if sub else GRAVITY
            scale = WATER_JUMP if sub and not dark else 1.0
        if t < JUMP_TIME:
            a = scale * 50.0
        elif dj is not None and t < dj + 0.1:
            a = scale * 60.0
        elif gb is not None and gb >= 0:
            if t - gb >= GB_TIME or d <= EYE:
                vz, gb, a = vz * GB_CANCEL, -1.0, -g
            else:
                a = GB_ACCEL - g
        else:
            a = -g
            if sj and dj is None and vz <= 0 and t < 2:
                dj, vz, a = t, 8.0 * (scale if d >= 0.8 * EYE else 1.0), scale * 60.0
            elif grav and gb is None and d > EYE and (early or vz <= 0) and (not sj or dj is not None):
                gb, vz, a = t, vz * 0.1, GB_ACCEL - g
        vz += a * dt
        z += vz * dt
        t += dt
        out.append((t, z))
    return np.array(out)


@functools.lru_cache(maxsize=None)
def jump_sim(abil, depth=None, dark=False):
    """Height envelope of the held jumps with abilities abil from `depth` under a fluid (None: dry).
    Never below the dry jump: the vanilla route climbs Torvus's underwater stairs (0r_swamp_hall,
    steps 1.5 to 2.5) before Gravity Boost, past the simulated 1.65 walk jump (step-up and slopes
    are not modelled), so a fluid only adds height (Gravity Boost, low gravity)."""
    if depth is None:
        return CURVES[abil & SJ]
    abil &= SJ | GRAV | (LIGHT if dark else 0)
    out = _envelope(CURVES[abil & SJ], _sim(abil, depth, dark, False))
    return _envelope(out, _sim(abil, depth, dark, True)) if abil & GRAV else out


def _envelope(a, b):
    """Pointwise higher of two curves on the same time step (the longer one's tail kept)."""
    if len(a) < len(b):
        a, b = b, a
    a = a.copy()
    a[:len(b), 1] = np.maximum(a[:len(b), 1], b[:, 1])
    return a


def curve(abil, key):
    """jump_sim for a take-off depth key (Terrain.wkey: -2 dry, else depth bucket * 2 + dark)."""
    return jump_sim(abil) if key < 0 else jump_sim(abil, (key // 2) * DEPTH_STEP, bool(key & 1))


def reach(dz, curve):
    """Horizontal distance a jump along curve can cover to land dz higher (NaN when it cannot)."""
    t, z = curve.T
    # last time the feet are still RISE_MARGIN above the landing: search the running maximum from
    # the end (non-increasing), reversed
    late = np.maximum.accumulate(z[::-1])
    i = np.searchsorted(late, dz + RISE_MARGIN)
    ok = i < len(late)
    return np.where(ok, SPEED_FACTOR * AIR_SPEED * t[::-1][np.minimum(i, len(t) - 1)], np.nan)


# ---- one area ------------------------------------------------------------------------------------

BALL_STEP = 0.5
JUMP_MAX = 10.0       # horizontal search radius for jumps
JUMP_DROP = 12.0      # deepest jump down considered
DROP_REACH = 1.5      # horizontal reach of a ball rolling off a ledge
NEIGHBOURS = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
SETS = sorted(range(ALL + 1), key=lambda s: bin(s).count("1"))   # every ability set, subsets first
SPIDER_CELL = 0.5     # spider track sample spacing
SPIDER_GAP = 2        # track cells this far apart (in cells) are one track
SPIDER_TOUCH = 1.9    # a ball on a floor this far below a track cell gets on
SPIDER_SIDE = 3       # floor columns (GRID) this far beside a track cell to get on or off at
PIPE_TOUCH = 1.0      # a ball on a floor this far below a half-pipe cell rolls onto it
PIPE_LIP = 3.2        # ballHalfPipeBoostHeight (disc TweakBall): a boost carries the ball this far
                      # over a half-pipe cell onto a floor beside it


def components(n, u, v):
    """Labels of the undirected components of n nodes joined by edges u-v."""
    lab = np.arange(n)
    while True:
        m = np.minimum(lab[u], lab[v])
        new = lab.copy()
        np.minimum.at(new, u, m)
        np.minimum.at(new, v, m)
        new = new[new]
        while True:
            nn = new[new]
            if np.array_equal(nn, new):
                break
            new = nn
        if np.array_equal(new, lab):
            return lab
        lab = new


class Terrain:
    def __init__(self, tris, flags, water=(np.zeros((0, 3)), np.zeros((0, 3)), np.zeros(0, bool))):
        nrm = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        nz = nrm[:, 2] / (np.linalg.norm(nrm, axis=1) + 1e-12)   # signed: a ceiling is no floor
        floor_tri = (nz > FLOOR_NZ) | (((flags & np.uint64(FLOOR)) != 0) & (nz >= FLOOR_FLAGGED_NZ))
        self.seg = Segments(tris)
        ix, iy, z, t = column_hits(tris)
        o = np.lexsort((z, iy, ix))
        ix, iy, z, fl = ix[o], iy[o], z[o], floor_tri[t[o]]
        same = (ix[1:] == ix[:-1]) & (iy[1:] == iy[:-1]) & (z[1:] - z[:-1] < 0.02)
        first = np.concatenate([[True], ~same])
        fl = np.maximum.reduceat(fl, np.nonzero(first)[0])
        ix, iy, z = ix[first], iy[first], z[first]
        samecol = np.append((ix[1:] == ix[:-1]) & (iy[1:] == iy[:-1]), False)
        above = np.where(samecol, np.append(z[1:], np.inf), np.inf)
        assert np.abs(z).max() < 9000
        # hits addressed by column id then z, for "highest hit at or below z in column c"
        key = (ix + (1 << 20)) * (1 << 21) + (iy + (1 << 20))
        self.cols, col = np.unique(key, return_inverse=True)
        self.comb = col * 20000.0 + (z + 10000.0)
        self.hit_col, self.hit_z, self.hit_above = col, z, above
        pt = fl & (above - z >= BALL)
        self.hit_pt = np.where(pt, np.cumsum(pt) - 1, -1)
        h = np.nonzero(pt)[0]
        self.pos = np.stack([ix[h] * GRID, iy[h] * GRID, z[h]], 1)
        self.above = above[h]
        self.stand = self.above - self.pos[:, 2] >= HEIGHT
        self.ixy = np.stack([ix[h], iy[h]], 1)
        self.links = {}   # kind -> (p, q)
        for kind in ("stand", "ball"):
            self.links[kind] = self._links(kind)
        self.n = len(self.pos)
        sp, sq = self.links["stand"]
        both = np.isin(sp * self.n + sq, sq * self.n + sp)
        deg = np.bincount(sp[both], minlength=self.n)
        self.walk = components(self.n, sp[both], sq[both])
        bp, bq = self.links["ball"]
        bboth = np.isin(bp * self.n + bq, bq * self.n + bp)
        self.roll = components(self.n, bp[bboth], bq[bboth])
        # fluid over each point: depth of the surface above the feet (a point just under a box's
        # bottom counts: the box is the volume, its floor the mesh's), Dark Water or not
        depth, dark = np.full(self.n, np.nan), np.zeros(self.n, bool)
        z = self.pos[:, 2]
        for lo, hi, dk in zip(*water):
            dd = hi[2] - z
            inside = (np.all((self.pos[:, :2] >= lo[:2]) & (self.pos[:, :2] <= hi[:2]), axis=1)
                      & (z >= lo[2] - 0.5) & (z < hi[2]) & ~(depth >= dd))
            depth[inside], dark[inside] = dd[inside], dk
        self.wkey = np.where(np.isnan(depth), -2, np.minimum(np.round(np.nan_to_num(depth) / DEPTH_STEP),
                                                               DEPTH_MAX / DEPTH_STEP).astype(int) * 2 + dark)
        self.rmax = {k: max(curve(s, k)[:, 1].max() for s in JUMP_SETS) for k in np.unique(self.wkey).tolist()}
        self.jumps = tuple(np.concatenate(x) for x in zip(self._jumps(self.stand & (deg < 8), ~self.stand),
                                                           self._drops(), self._ride(tris, flags, SPIDER),
                                                           self._ride(tris, flags, BOOST)))
        # items that change anything here (the rest are masked off before building a graph)
        jl = self.jumps[2]
        self.relevant = (MORPH | SJ | (GRAV if (self.wkey >= 0).any() else 0)
                         | (LIGHT if (self.wkey % 2 == 1).any() else 0)
                         | (SPIDER if (jl & SPIDER).any() else 0) | (BOOST if (jl & BOOST).any() else 0))

    def rise(self, pts, abil):
        """Highest a jump with abil takes the feet from each of pts."""
        k = self.wkey[pts]
        out = np.zeros(len(pts))
        for key in np.unique(k).tolist():
            out[k == key] = curve(abil, key)[:, 1].max()
        return out

    def _below(self, ixy, zmax):
        """Hit index of the highest hit at or below zmax in column ixy, or -1."""
        key = (ixy[:, 0] + (1 << 20)) * (1 << 21) + (ixy[:, 1] + (1 << 20))
        c = np.searchsorted(self.cols, key)
        c = np.minimum(c, len(self.cols) - 1)
        have = self.cols[c] == key
        i = np.searchsorted(self.comb, c * 20000.0 + (zmax + 10000.0), "right") - 1
        ok = have & (i >= 0)
        ok[ok] &= self.hit_col[i[ok]] == c[ok]
        return np.where(ok, i, -1)

    def _links(self, kind):
        stand = kind == "stand"
        src = np.nonzero(self.stand)[0] if stand else np.arange(len(self.pos))
        step, body = (STEP_UP, HEIGHT) if stand else (BALL_STEP, BALL)
        ps, qs = [], []
        for dx, dy in NEIGHBOURS:
            p = src
            pz = self.pos[p, 2]
            h = self._below(self.ixy[p] + (dx, dy), pz + step)
            ok = h >= 0
            p, h, pz = p[ok], h[ok], pz[ok]
            q = self.hit_pt[h]
            ok = q >= 0
            p, q, h, pz = p[ok], q[ok], h[ok], pz[ok]
            if stand:
                ok = self.stand[q]
                p, q, h, pz = p[ok], q[ok], h[ok], pz[ok]
            top = np.maximum(pz, self.pos[q, 2])
            ok = self.hit_above[h] >= top + body
            p, q, top = p[ok], q[ok], top[ok]
            a, b = self.pos[p].copy(), self.pos[q].copy()
            heights = (CLEAR, HEIGHT - 0.2) if stand else (BALL / 2,)
            blocked = np.zeros(len(p), bool)
            for hh in heights:
                a[:, 2] = b[:, 2] = top + hh
                blocked |= self.seg.blocked(a, b)
            ps.append(p[~blocked])
            qs.append(q[~blocked])
        return np.concatenate(ps), np.concatenate(qs)

    def _jumps(self, edge, ball):
        """(p, q, level) jumps from edge stand points to edge stand points of other walk components,
        or into ball-only spots (a raised tunnel mouth: jump, then morph in the air)."""
        e, f = np.nonzero(edge)[0], np.nonzero(edge | ball)[0]
        if not len(e) or not len(f):
            return np.zeros(0, int), np.zeros(0, int), np.zeros(0, int)
        cell = np.floor(self.pos[f, :2] / JUMP_MAX).astype(np.int64)
        key = (cell[:, 0] + (1 << 20)) * (1 << 21) + (cell[:, 1] + (1 << 20))
        o = np.argsort(key, kind="stable")
        f, key = f[o], key[o]
        ecell = np.floor(self.pos[e, :2] / JUMP_MAX).astype(np.int64)
        P, Q = [], []
        for dx, dy in NEIGHBOURS + [(0, 0)]:
            k2 = (ecell[:, 0] + dx + (1 << 20)) * (1 << 21) + (ecell[:, 1] + dy + (1 << 20))
            s, t = np.searchsorted(key, k2, "left"), np.searchsorted(key, k2, "right")
            cnt = t - s
            src = np.repeat(np.arange(len(e)), cnt)
            k = np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)
            dst = np.repeat(s, cnt) + k
            p, q = e[src], f[dst]
            d = np.hypot(*(self.pos[q, :2] - self.pos[p, :2]).T)
            dz = self.pos[q, 2] - self.pos[p, 2]
            apart = np.where(self.stand[q], self.walk[p] != self.walk[q], self.roll[p] != self.roll[q])
            top = np.vectorize(self.rmax.get, otypes=[float])(self.wkey[p]) if len(p) else np.zeros(0)
            ok = apart & (d <= JUMP_MAX) & (dz >= -JUMP_DROP) & (dz < top + 0.02)
            P.append(p[ok])
            Q.append(q[ok])
        p, q = np.concatenate(P), np.concatenate(Q)
        if not len(p):
            return np.zeros(0, int), np.zeros(0, int), np.zeros(0, int)
        d = np.hypot(*(self.pos[q, :2] - self.pos[p, :2]).T)
        dz = self.pos[q, 2] - self.pos[p, 2]
        # one candidate per minimal jump set that makes it, per take-off depth
        wk = self.wkey[p]
        rows, levels = [], []
        for key in np.unique(wk).tolist():
            m = np.nonzero(wk == key)[0]
            sets = [s for s in JUMP_SETS if s & ~(SJ | (GRAV if key >= 0 else 0) | (LIGHT if key % 2 else 0)) == 0]
            works = {s: d[m] <= reach(dz[m], curve(s, key)) for s in sets}
            for s in sets:
                minimal = works[s].copy()
                for x in sets:
                    if x != s and x & s == x:
                        minimal &= ~works[x]
                rows.append(m[minimal])
                levels.append(np.full(minimal.sum(), s))
        idx, level = np.concatenate(rows), np.concatenate(levels)
        p, q, d = p[idx], q[idx], d[idx]
        level = level | np.where(self.stand[q], NONE, MORPH)
        # test the nearest few candidates of each (component, component, level) at a time
        dcomp = np.where(self.stand[q], self.walk[q], self.n + self.roll[q])
        group = (self.walk[p] * 2 * self.n + dcomp) * 64 + level
        o = np.lexsort((d, group))
        p, q, level, group = p[o], q[o], level[o], group[o]
        rank = np.arange(len(group)) - np.searchsorted(group, group, "left")
        done = np.zeros(0, np.int64)
        out = []
        for r0 in range(0, 64, 8):
            pick = (rank >= r0) & (rank < r0 + 8) & ~np.isin(group, done)
            if not pick.any():
                break
            pp, qq = p[pick], q[pick]
            good = self._jump_clear(pp, qq)
            out.append((pp[good], qq[good], level[pick][good]))
            done = np.union1d(done, group[pick][good])
        if not out:
            return np.zeros(0, int), np.zeros(0, int), np.zeros(0, int)
        return tuple(np.concatenate(x) for x in zip(*out))

    def _drops(self):
        """(p, q, MORPH) rolls off a ledge from a ball-only spot (a raised tunnel mouth) to a lower
        spot nearby, which the neighbour links miss when a steep lip lies between the two."""
        b = np.nonzero(~self.stand)[0]
        if not len(b):
            return np.zeros(0, int), np.zeros(0, int), np.zeros(0, int)
        o = np.lexsort((self.ixy[:, 1], self.ixy[:, 0]))
        key = (self.ixy[o, 0] + (1 << 20)) * (1 << 21) + (self.ixy[o, 1] + (1 << 20))
        r = int(np.ceil(DROP_REACH / GRID))
        P, Q = [], []
        for dx in range(-r, r + 1):
            for dy in range(-r, r + 1):
                if dx * dx + dy * dy > r * r:
                    continue
                k = (self.ixy[b, 0] + dx + (1 << 20)) * (1 << 21) + (self.ixy[b, 1] + dy + (1 << 20))
                s, t = np.searchsorted(key, k, "left"), np.searchsorted(key, k, "right")
                cnt = t - s
                src = np.repeat(b, cnt)
                dst = o[np.repeat(s, cnt) + np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)]
                dz = self.pos[dst, 2] - self.pos[src, 2]
                ok = (dz < -BALL_STEP) & (dz >= -JUMP_DROP) & (self.roll[src] != self.roll[dst])
                P.append(src[ok])
                Q.append(dst[ok])
        p, q = np.concatenate(P), np.concatenate(Q)
        a, c = self.pos[p].copy(), self.pos[q].copy()
        a[:, 2] += BALL / 2
        c[:, 2] = a[:, 2]
        blocked = self.seg.blocked(a, c)
        a[:, :2] = c[:, :2]
        c[:, 2] = self.pos[q, 2] + 0.3
        blocked |= self.seg.blocked(a, c)
        p, q = p[~blocked], q[~blocked]
        return p, q, np.full(len(p), MORPH)

    def _ride(self, tris, flags, item):
        """(p, q, MORPH | item) rides along a surface the ball sticks to or boosts up.
        SPIDER, a spider track: from a spot where the ball touches the track (a cell within
        SPIDER_TOUCH above the floor) to any spot beside or below the same track, where it lets go.
        BOOST, a half-pipe: from a spot at its foot (a cell within PIPE_TOUCH above the floor) to any
        spot beside it at most PIPE_LIP above one of its cells (boosted over the lip) or below."""
        none = np.zeros(0, int), np.zeros(0, int), np.zeros(0, int)
        touch_max, lip = (SPIDER_TOUCH, 0.5) if item == SPIDER else (PIPE_TOUCH, PIPE_LIP)
        t = tris[(flags & np.uint64(SPIDER_TRACK if item == SPIDER else HALF_PIPE)) != 0]
        if not len(t) or not self.n:
            return none
        # sample every track triangle at SPIDER_CELL, as 3D cells
        span = np.linalg.norm(t[:, [1, 2, 0]] - t, axis=2).max(1)
        steps = np.clip(np.ceil(span / SPIDER_CELL), 1, 400).astype(int)
        cells = []
        for n in np.unique(steps).tolist():
            i, j = np.meshgrid(np.arange(n + 1), np.arange(n + 1), indexing="ij")
            keep = i + j <= n
            w = np.stack([i[keep], j[keep]], 1) / n
            tt = t[steps == n]
            s = tt[:, None, 0] + w[None, :, :1] * (tt[:, None, 1] - tt[:, None, 0]) + \
                w[None, :, 1:] * (tt[:, None, 2] - tt[:, None, 0])
            cells.append(np.floor(s.reshape(-1, 3) / SPIDER_CELL).astype(np.int64))
        cells = np.unique(np.concatenate(cells), axis=0)

        def key(c):
            return ((c[:, 0] + (1 << 16)) << 34) | ((c[:, 1] + (1 << 16)) << 17) | (c[:, 2] + (1 << 16))
        k = key(cells)    # sorted: np.unique sorts rows lexicographically
        u, v = [], []
        r = range(-SPIDER_GAP, SPIDER_GAP + 1)
        for off in [(a, b, c) for a in r for b in r for c in r if (a, b, c) > (0, 0, 0)]:
            k2 = key(cells + off)
            i = np.minimum(np.searchsorted(k, k2), len(k) - 1)
            hit = k[i] == k2
            u.append(np.nonzero(hit)[0])
            v.append(i[hit])
        track = components(len(cells), np.concatenate(u), np.concatenate(v))
        # floors beside or below each cell: on when the ball touches the cell, off anywhere
        ctr = (cells + 0.5) * SPIDER_CELL
        col = np.round(ctr[:, :2] / GRID).astype(np.int64)
        on, off = [], []
        for dx in range(-SPIDER_SIDE, SPIDER_SIDE + 1):
            for dy in range(-SPIDER_SIDE, SPIDER_SIDE + 1):
                if dx * dx + dy * dy > SPIDER_SIDE * SPIDER_SIDE:
                    continue
                for zmax, out in ((ctr[:, 2] + 0.5, on), (ctr[:, 2] + lip, off)):
                    h = self._below(col + (dx, dy), zmax)
                    q = np.where(h >= 0, self.hit_pt[np.maximum(h, 0)], -1)
                    c = np.nonzero(q >= 0)[0]
                    q = q[c]
                    if out is on:
                        near = ctr[c, 2] - self.pos[q, 2] <= touch_max
                        c, q = c[near], q[near]
                    out.append(np.stack([track[c], q], 1))
        # one point per (track, roll component) on each side
        P, Q = [], []
        for pairs, out in ((np.concatenate(on), P), (np.concatenate(off), Q)):
            lab = pairs[:, 0] * self.n + self.roll[pairs[:, 1]]
            _l, first = np.unique(lab, return_index=True)
            out.append(pairs[first])
        a, b = P[0], Q[0]
        p, q = [], []
        for tr in np.intersect1d(a[:, 0], b[:, 0]).tolist():
            src, dst = a[a[:, 0] == tr, 1], b[b[:, 0] == tr, 1]
            sp, dp = np.repeat(src, len(dst)), np.tile(dst, len(src))
            ok = self.roll[sp] != self.roll[dp]
            p.append(sp[ok])
            q.append(dp[ok])
        if not p:
            return none
        p, q = np.concatenate(p), np.concatenate(q)
        return p, q, np.full(len(p), MORPH | item)

    def _jump_clear(self, p, q):
        a, b = self.pos[p], self.pos[q]
        top = np.maximum(a[:, 2], b[:, 2])
        blocked = np.zeros(len(p), bool)
        ball = ~self.stand[q]
        if ball.any():
            # into a tunnel mouth: rise standing at p (Samus morphs near the apex), then cross at
            # ball height
            r = np.nonzero(ball)[0]
            mid = top[r] + BALL / 2
            head = top[r] + RISE_MARGIN + HEIGHT - 0.2
            for s0, s1 in (((a[r], a[r, 2] + 0.3), (a[r], head)), ((a[r], mid), (b[r], mid))):
                x0, x1 = s0[0].copy(), s1[0].copy()
                x0[:, 2], x1[:, 2] = s0[1], s1[1]
                blocked[r] |= self.seg.blocked(x0, x1)
            stand = np.nonzero(~ball)[0]
            out = ~blocked
            out[stand] = self._jump_clear(p[stand], q[stand]) if len(stand) else out[stand]
            return out
        for s0, s1 in (((a, a[:, 2] + 0.3), (a, top + HEIGHT - 0.2)),
                       ((a, top + CLEAR), (b, top + CLEAR)),
                       ((a, top + HEIGHT - 0.2), (b, top + HEIGHT - 0.2)),
                       ((b, top + HEIGHT - 0.2), (b, b[:, 2] + 0.3))):
            x0, x1 = s0[0].copy(), s1[0].copy()
            x0[:, 2], x1[:, 2] = s0[1], s1[1]
            blocked |= self.seg.blocked(x0, x1)
        return ~blocked

    # ---- queries ---------------------------------------------------------------------------------

    def graph(self, abil):
        """(component label per point, set of directed component edges) with abilities abil."""
        abil &= self.relevant
        if not hasattr(self, "_graphs"):
            self._graphs = {}
        if abil in self._graphs:
            return self._graphs[abil]
        kinds = ["stand", "ball"] if abil & MORPH else ["stand"]
        p = np.concatenate([self.links[k][0] for k in kinds])
        q = np.concatenate([self.links[k][1] for k in kinds])
        both = np.isin(p * self.n + q, q * self.n + p)
        lab = components(self.n, p[both], q[both])
        jp, jq, jl = self.jumps
        jok = (jl & ~abil) == 0
        u = np.concatenate([lab[p[~both]], lab[jp[jok]]])
        v = np.concatenate([lab[q[~both]], lab[jq[jok]]])
        adj = {}
        for a, b in set(zip(u.tolist(), v.tolist())):
            if a != b:
                adj.setdefault(a, []).append(b)
        self._graphs[abil] = (lab, adj)
        return lab, adj

    def reachable(self, src, dst, abil):
        """Whether any point of dst is reachable from any point of src with abilities abil."""
        active = np.ones(self.n, bool) if abil & MORPH else self.stand
        src, dst = src[active[src]], dst[active[dst]]
        if not len(src) or not len(dst):
            return False
        lab, adj = self.graph(abil)
        goal = set(lab[dst].tolist())
        seen = set(lab[src].tolist())
        todo = list(seen)
        while todo:
            c = todo.pop()
            if c in goal:
                return True
            for d in adj.get(c, ()):
                if d not in seen:
                    seen.add(d)
                    todo.append(d)
        return False

    def anchor(self, pos, radius, below, above):
        """Points within radius horizontally and z in [pos.z - below, pos.z + above]."""
        d = np.hypot(*(self.pos[:, :2] - pos[:2]).T)
        dz = self.pos[:, 2] - pos[2]
        return np.nonzero((d <= radius) & (dz >= -below) & (dz <= above))[0]


# ---- the rules check -----------------------------------------------------------------------------

SHAPES = {"wall": "door", "floor": "floor or ceiling dock", "loc": "pickup"}


def node_points(T, node, source):
    """{ability set: anchor points} of a rule node, or None for a node the mesh cannot place.
    node: ("wall" | "floor" | "virtual" | "loc", position, editor scale)."""
    shape, pos, scale = node
    if shape == "virtual":      # a boundary in open space shared with the next area, placed anywhere
        return None
    if shape == "wall":         # a door: Samus stands on the floor at its foot
        pts = T.anchor(pos, 2.5, max(4.0, scale[2] / 2 + 1.0), 0.5)   # tall doors sit higher
        return {s: pts for s in SETS}
    if shape == "floor":        # a hole in a floor or ceiling: falls through it reach whatever is below
        r = max(scale[0], scale[1]) / 2
        below = T.anchor(pos, r, 1e4, 0.5)
        if source:
            return {s: below for s in SETS}
        rim = T.anchor(pos, r + 1.5, 4.0, 1e4)  # or a rim at any height above, to drop in from
        pts = np.union1d(below[T.pos[below, 2] >= pos[2] - 4.0], rim)
        return {s: pts for s in SETS}
    # a pickup Samus can touch from a spot, standing or at the top of the best jump from there
    near = T.anchor(pos, 1.5, HEIGHT + max(T.rmax.values()) - 0.3, 1.0)
    out = {}
    for s in SETS:
        k = s & T.relevant & (SJ | GRAV | LIGHT)
        if k not in out:
            out[k] = near[T.pos[near, 2] + HEIGHT + T.rise(near, k) - 0.3 >= pos[2]]
        out[s] = out[k]
    return {s: out[s] for s in SETS}


def check_area(job):
    """Verdicts of one area's rule edges: (line, text of src, text of dst, reach per set) or an
    unanchored node."""
    name, mrea, edges, plats, water = job
    tris, flags = area_mesh(mrea)
    T = Terrain(tris, flags, water)
    Ps = [Terrain(np.concatenate([tris, p[0]]), np.concatenate([flags, p[1]]), water) for p in plats]
    out = []
    for line, a, c, apos, cpos, terms in edges:
        r, miss, helped = {s: False for s in SETS}, None, False
        if not terms:               # never: too strict at worst
            out.append((line, a, c, "never", terms, None, False))
            continue
        if apos[0] == "virtual" or cpos[0] == "virtual":
            out.append((line, a, c, "virtual", terms, None, False))
            continue
        for t in [T] + Ps:
            src, dst = node_points(t, apos, True), node_points(t, cpos, False)
            n_src, n_dst = len(src[ALL]), len(dst[ALL])
            if not n_src or not n_dst:
                miss = miss or (n_src, n_dst, apos[0], cpos[0])
                continue
            seen = {}               # items irrelevant here change nothing
            for s in SETS:          # subsets first: an ability set does what any of its subsets does
                if r[s]:
                    continue
                if any(r[x] for x in SETS if x != s and x & s == x):
                    r[s] = True
                    continue
                k = s & t.relevant
                if k not in seen:
                    seen[k] = t.reachable(src[s], dst[s], s)
                if seen[k]:
                    r[s], helped = True, helped or t is not T
        if not any(r.values()) and miss:
            out.append((line, a, c, None, terms, miss, False))
            continue
        out.append((line, a, c, r, terms, None, helped))
    return name, T.n, out


def node_text(n):
    kind, val = n
    return f"@{val}" if kind == "@" else f"{kind}{val}" if kind == "dock" else f"{kind}0x{val:08X}"


def check(rules_dir=None, only=None, cached=True):
    """Verdicts of every rule edge: ([(rules file, area, line, edge text, kind, detail, helped)], skipped).
    kind is ok, refuted, nopath, unanchored or virtual; a never edge is ok. With cached, an area
    whose mesh, platforms, edges and this file are unchanged reuses its last verdicts (CACHE)."""
    import hashlib
    import multiprocessing
    import pickle
    import rando_rules as rr
    import rando_world as rw
    rules_dir = rules_dir or rr.RULES_DIR
    items = rr.item_table()
    items = {**items, **rr.scan_events(rules_dir, items)}
    bit_of = {items[n]: b for n, b in BIT_ITEMS.items()}
    unmodelled = {items[n] for n in UNMODELLED}
    worlds, areas, _d, _t, _l, _dl, _o, dock_pos, loc_pos, _r = rw.collect()
    by_world = {name: i for i, (_m, name) in enumerate(worlds)}
    iso = ed.Iso(ed.find_iso())
    mrea_pak = {}
    for pk in ed.load_paks(iso):
        if rw.SINGLE_PLAYER_PAKS.search(pk.path):
            for rid, (kind, *_x) in pk.res.items():
                if kind in ("MREA", "MLVL"):
                    mrea_pak[rid] = pk
    first = {}
    for g, a in enumerate(areas):
        first.setdefault(a[0], g)
    layer_on = {wi: savw_diff.parse_mlvl(mrea_pak[m].get(m))["layerDefaults"] for wi, (m, _n) in enumerate(worlds)
                if m in mrea_pak}
    jobs, skipped = [], 0
    dock_number, dock_virtual = rw.sldr_prop("Dock", "dockNumber"), rw.sldr_prop("Dock", "isVirtual")
    for path in sorted(rules_dir.glob("*.rules")):
        wi = by_world[path.stem]
        for b in rr.parse_file(path, items):
            if only and b.name != only:
                continue
            gi = next(g for g, a in enumerate(areas) if a[0] == wi and a[3] == b.name)

            pk = mrea_pak[areas[gi][1]]
            mrea = pk.get(areas[gi][1])
            ents = [(layer, *e[:3], rm.pickup_props(e[3]))
                    for layer, sec in enumerate(ed.mrea_layers(mrea)) for e in rw.scly_entities(sec)]
            shapes = {}
            for _l, t4, _e, _c, props in ents:
                if t4 == "DOCK":
                    x = struct.unpack(">9f", rw.editor(props, rw.XFRM)[:36])
                    flat = x[8] < min(x[6], x[7]) and not any(x[3:6])
                    virtual = props.get(dock_virtual, b"\0")[-1:] == b"\1"
                    shapes[rw.i32(props, dock_number, -1)] = ("virtual" if virtual else "floor" if flat else "wall",
                                                             np.array(x[6:]))

            def pos(n):
                if n[0] == "dock" and (gi, n[1]) in dock_pos:
                    shape, scale = shapes.get(n[1], ("wall", np.ones(3)))
                    return shape, np.array(dock_pos[gi, n[1]]), scale
                if n[0] == "loc" and (gi, n[1]) in loc_pos:
                    return "loc", np.array(loc_pos[gi, n[1]]), np.ones(3)
                return None
            edges = []
            for line, a, c, dnf, both in b.edges:
                # MorphBall is in the start inventory (port_rando_logic StartInventory) and never placed;
                # a term with an item the mesh cannot model gets no bits and is never refuted
                terms = [(t, None if any(i in unmodelled for i, _n in t) else
                          functools.reduce(lambda x, y: x | y, (bit_of.get(i, 0) for i, _n in t), MORPH))
                         for t in dnf]
                for x, y in [(a, c), (c, a)] if both else [(a, c)]:
                    px, py = pos(x), pos(y)
                    if px is None or py is None:
                        skipped += 1
                        continue
                    edges.append((line, x, y, px, py, terms))
            plats = platforms(ents, layer_on[wi][gi - first[wi]], lambda r: pk.get(r) if r in pk.res else None)
            water = waters(ents, layer_on[wi][gi - first[wi]])
            key = hashlib.sha1(pathlib.Path(__file__).read_bytes() + mrea + pickle.dumps(
                ([(e[0], e[1], e[2], e[3][0], e[3][1].tolist(), e[3][2].tolist(), e[4][0], e[4][1].tolist(),
                   e[4][2].tolist(), e[5]) for e in edges], [(p[0].tobytes(), p[1].tobytes()) for p in plats],
                 [w.tobytes() for w in water]))).hexdigest()
            jobs.append((path.name, b.name, key, (b.name, mrea, edges, plats, water)))
    hit = {}
    if cached:
        for _f, _a, key, _j in jobs:
            if (CACHE / key).exists():
                hit[key] = pickle.loads((CACHE / key).read_bytes())
    todo = [j for j in jobs if j[2] not in hit]
    if todo:
        with multiprocessing.Pool() as pool:
            for (_f, _a, key, _j), (_n, _t, out) in zip(todo, pool.map(check_area, [j[3] for j in todo], chunksize=1)):
                hit[key] = out
                if cached:
                    CACHE.mkdir(parents=True, exist_ok=True)
                    (CACHE / key).write_bytes(pickle.dumps(out))
    names = {v: k for k, v in items.items()}

    def term_text(t):
        return " & ".join(names[i] + (f">={n}" if n > 1 else "") for i, n in t) or "any"
    rows = []
    for fname, area, key, _j in jobs:
        for line, a, c, r, terms, miss, helped in hit[key]:
            edge = f"{node_text(a)} > {node_text(c)}"
            if r == "never":
                rows.append((fname, area, line, edge, "ok", "never, not checked", False))
            elif r == "virtual":
                rows.append((fname, area, line, edge, "virtual", "virtual dock, not checked", False))
            elif r is None:
                rows.append((fname, area, line, edge, "unanchored", f"no floor at {'source' if not miss[0] else 'target'}"
                             f" ({SHAPES[miss[2] if not miss[0] else miss[3]]})", False))
            elif not r[ALL]:
                rows.append((fname, area, line, edge, "nopath", "no path in the static mesh; terms " +
                             " | ".join(f"({term_text(t)})" for t, _b in terms), helped))
            else:
                need = " or ".join(set_text(s) for s in SETS
                                   if r[s] and not any(r[x] for x in SETS if x != s and x & s == x))
                bad = [t for t, bits in terms if bits is not None and not r[bits]]
                if bad:
                    rows.append((fname, area, line, edge, "refuted", f"needs {need}; refuted terms " +
                                 " | ".join(f"({term_text(t)})" for t in bad), helped))
                else:
                    rows.append((fname, area, line, edge, "ok", f"ok, needs {need}", helped))
    return rows, skipped


def main():
    args = sys.argv[1:]
    rows, skipped = check(only=args[args.index("--area") + 1] if "--area" in args else None,
                          cached="--no-cache" not in args)
    counts = {"ok": 0, "refuted": 0, "nopath": 0, "unanchored": 0, "virtual": 0}
    last = None
    for fname, area, line, edge, kind, detail, helped in rows:
        counts[kind] += 1
        if kind in ("ok", "virtual") and "--all" not in args:
            continue
        if (fname, area) != last:
            print(f"{fname} {area}")
            last = fname, area
        print(f"  line {line}: {edge}" + (" (with platforms)" if helped else "") + f": {detail}")
    print(f"edges checked: {sum(counts.values())} ({skipped} skipped: teleporter or named node); " +
          ", ".join(f"{k} {v}" for k, v in counts.items()))


if __name__ == "__main__":
    main()
