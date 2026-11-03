#!/usr/bin/env python3
"""Terrain reachability inside each area, from the collision mesh on the disc, checked against the
hand rules (tools/rando_rules/*.rules).

The hand rules guess what movement inside a room needs: how high a ledge is, whether a gap can be
jumped. This tool measures it instead. For each area it reads the static collision (the MREA's
CAreaOctTree section), finds where Samus can stand, links those spots by walking, falling, rolling
and jumping, and works out what a rule edge needs from the mesh alone: nothing, MorphBall,
SpaceJumpBoots, both, or no path at all (the edge relies on something the static mesh does not
have: a moving platform, an elevator, a grapple point, a spinner, a bomb slot).

    tools/rando_terrain.py                    report every rule edge whose terms the terrain refutes
    tools/rando_terrain.py --area NAME        check one area only
    tools/rando_terrain.py --all              print every checked edge, not only the refuted ones

The model, and its limits (docs/RANDOMIZER.md, "Terrain check"):
- Physics from Standard.ntwk: gravity 35, jump force 50 for 0.25 s (apex 3.78), double jump impulse
  8 then force 60 for 0.1 s (apex 7.68), air speed 16.5, step up 1.0, height 2.7, radius 0.5, ball
  radius 0.7. Analytical, not measured in game; the margins below are deliberately wide.
- Floors are what CGameCollision::IsFloor says: the Floor material, or normal z > 0.85; collision is two-sided, as the game's is.
- Water (no Gravity Boost), Screw Attack, bomb jumps, dash and slope jumps are not modelled: the
  result only ever needs more than the game does, never less.
- Static mesh plus script platforms (PLAT: DCLN, collision box or model bounds) that start
  present or are activated, at their initial place; an edge is also tried on the static mesh
  alone, so a platform only ever adds paths. Destructible platforms, doors, gates, elevators and
  other script obstacles are not modelled.
- A jump may end in a ball-only spot (a tunnel mouth above the floor): Samus rises standing, morphs
  near the apex and crosses at ball height; both legs are checked against the mesh.
- Advisory: not yet enforced by tools/rando_rules.py. Known false refutations are listed in
  docs/RANDOMIZER.md; calibrate before trusting a result.
"""
import struct
import sys
import pathlib

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import echoes_disc as ed  # noqa: E402
import rando_models as rm  # noqa: E402
import savw_diff  # noqa: E402

GRID = 0.5            # floor sample spacing
FLOOR_NZ = 0.85       # |normal z| of a floor without the Floor material (CGameCollision::IsFloor)
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

NONE, MORPH, SJ = 0, 1, 2     # ability bits
LEVELS = ["walk", "MorphBall", "SpaceJumpBoots", "MorphBall & SpaceJumpBoots"]


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
    return np.array([c[[q[0], q[i], q[i + 1]]] for q in quads for i in (1, 2)])


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
    """(triangles, flags, names) of the PLATs that may stand in the area, as CScriptPlatform collides:
    its DCLN under the object's rotation and position, else the collision box (unrotated), else the
    scaled model's bounds under the rotation. A PLAT counts when its layer is on by default and it
    starts active or something sends it ACTV, wherever it starts (motion is not followed); one that
    a DEAD removes (DECR, DCTV) is an obstacle for the hand rules and does not count."""
    import rando_world as rw
    sent = {(m, t) for *_x, conns, _p in ents for _s, m, t in conns}
    killed = {t for *_x, conns, _p in ents for s, m, t in conns if s == "DEAD" and m in rw.KILL_MSGS}
    tris, flags, names = [], [], []
    for layer, t4, eid, _conns, props in ents:
        if t4 != "PLAT" or not layer_on >> layer & 1 or eid in killed:
            continue
        if rw.editor(props, EDITOR_ACTIVE)[-1:] == b"\0" and ("ACTV", eid) not in sent:
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
            t, f = box_tris(pos + off - box / 2, pos + off + box / 2), None
        else:
            cmdl = get(struct.unpack(">I", props[PLAT_MODEL][-4:])[0]) if PLAT_MODEL in props else None
            if cmdl is None:
                continue
            b = np.array(struct.unpack_from(">6f", cmdl, 12)).reshape(2, 3) * scale
            corners = box_tris(b[0], b[1]).reshape(-1, 3) @ rot.T
            t, f = box_tris(pos + corners.min(0), pos + corners.max(0)), None
        tris.append(t)
        flags.append(np.zeros(len(t), np.uint64) if f is None else f)
        names.append(rw.editor(props, rw.INAM).rstrip(b"\0").decode("latin1"))
    if not tris:
        return None
    return np.concatenate(tris), np.concatenate(flags), names


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


def reach(dz, level):
    """Horizontal distance a jump can cover to land dz higher (NaN when it cannot)."""
    t, z = CURVES[level].T
    # last time the feet are still RISE_MARGIN above the landing: the curve falls monotonically after
    # its apex, so search the falling half reversed
    top = z.argmax()
    fall_t, fall_z = t[top:][::-1], z[top:][::-1]
    i = np.searchsorted(fall_z, dz + RISE_MARGIN)
    ok = i < len(fall_z)
    return np.where(ok, SPEED_FACTOR * AIR_SPEED * fall_t[np.minimum(i, len(fall_t) - 1)], np.nan)


# ---- one area ------------------------------------------------------------------------------------

BALL_STEP = 0.5
JUMP_MAX = 10.0       # horizontal search radius for jumps
JUMP_DROP = 12.0      # deepest jump down considered
NEIGHBOURS = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
SETS = [NONE, MORPH, SJ, MORPH | SJ]


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
    def __init__(self, tris, flags):
        nrm = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        nz = np.abs(nrm[:, 2]) / (np.linalg.norm(nrm, axis=1) + 1e-12)
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
        self.jumps = self._jumps(self.stand & (deg < 8), ~self.stand)

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
            ok = apart & (d <= JUMP_MAX) & (dz >= -JUMP_DROP) & (dz < 7.7)
            P.append(p[ok])
            Q.append(q[ok])
        p, q = np.concatenate(P), np.concatenate(Q)
        d = np.hypot(*(self.pos[q, :2] - self.pos[p, :2]).T)
        dz = self.pos[q, 2] - self.pos[p, 2]
        level = np.where(d <= reach(dz, NONE), NONE, np.where(d <= reach(dz, SJ), SJ, -1))
        ok = level >= 0
        p, q, d, level = p[ok], q[ok], d[ok], level[ok]
        level = level | np.where(self.stand[q], NONE, MORPH)
        # test the nearest few candidates of each (component, component, level) at a time
        dcomp = np.where(self.stand[q], self.walk[q], self.n + self.roll[q])
        group = (self.walk[p] * 2 * self.n + dcomp) * 4 + level
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

def node_points(T, kind, pos):
    if kind == "dock":
        return T.anchor(pos, 2.5, 4.0, 0.5)
    return T.anchor(pos, 1.5, HEIGHT + 0.3, 1.0)       # a pickup Samus can touch


def check_area(job):
    """Verdicts of one area's rule edges: (line, text of src, text of dst, reach per set) or an
    unanchored node."""
    name, mrea, edges, plats = job
    tris, flags = area_mesh(mrea)
    T = Terrain(tris, flags)
    P = Terrain(np.concatenate([tris, plats[0]]), np.concatenate([flags, plats[1]])) if plats else None
    out = []
    for line, a, c, apos, cpos, terms in edges:
        r, miss, helped = {s: False for s in SETS}, None, False
        for t in (T, P) if P else (T,):
            src, dst = node_points(t, a[0], apos), node_points(t, c[0], cpos)
            if not len(src) or not len(dst):
                miss = miss or (len(src), len(dst))
                continue
            for s in SETS:
                if not r[s] and t.reachable(src, dst, s):
                    r[s], helped = True, helped or t is P
        if not any(r.values()) and miss:
            out.append((line, a, c, None, terms, miss, False))
            continue
        out.append((line, a, c, r, terms, None, helped))
    return name, T.n, out


def node_text(n):
    kind, val = n
    return f"@{val}" if kind == "@" else f"{kind}{val}" if kind == "dock" else f"{kind}0x{val:08X}"


def main():
    import multiprocessing
    import rando_rules as rr
    import rando_world as rw
    args = sys.argv[1:]
    items = rr.item_table()
    items = {**items, **rr.scan_events(rr.RULES_DIR, items)}
    morph, sj = items["MorphBall"], items["SpaceJumpBoots"]
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
    only = args[args.index("--area") + 1] if "--area" in args else None
    for path in sorted(rr.RULES_DIR.glob("*.rules")):
        wi = by_world[path.stem]
        for b in rr.parse_file(path, items):
            if only and b.name != only:
                continue
            gi = next(g for g, a in enumerate(areas) if a[0] == wi and a[3] == b.name)

            def pos(n):
                if n[0] == "dock":
                    return dock_pos.get((gi, n[1]))
                if n[0] == "loc":
                    return loc_pos.get((gi, n[1]))
                return None
            edges = []
            for line, a, c, dnf, both in b.edges:
                terms = [(t, (MORPH if dict(t).get(morph) else 0) | (SJ if dict(t).get(sj) else 0)) for t in dnf]
                for x, y in [(a, c), (c, a)] if both else [(a, c)]:
                    px, py = pos(x), pos(y)
                    if px is None or py is None:
                        skipped += 1
                        continue
                    edges.append((line, x, y, np.array(px), np.array(py), terms))
            pk = mrea_pak[areas[gi][1]]
            mrea = pk.get(areas[gi][1])
            ents = [(layer, *e[:3], rm.pickup_props(e[3]))
                    for layer, sec in enumerate(ed.mrea_layers(mrea)) for e in rw.scly_entities(sec)]
            plats = platforms(ents, layer_on[wi][gi - first[wi]], lambda r: pk.get(r) if r in pk.res else None)
            jobs.append((f"{path.name}:{b.line} {b.name}", mrea, edges, plats))
    with multiprocessing.Pool() as pool:
        results = pool.map(check_area, jobs, chunksize=1)
    names = {v: k for k, v in items.items()}

    def term_text(t):
        return " & ".join(names[i] + (f">={n}" if n > 1 else "") for i, n in t) or "any"
    counts = {"ok": 0, "refuted": 0, "nopath": 0, "unanchored": 0}
    for area, _n, out in results:
        lines = []
        for line, a, c, r, terms, miss, helped in out:
            head = f"  line {line}: {node_text(a)} > {node_text(c)}" + (" (with platforms)" if helped else "")
            if r is None:
                counts["unanchored"] += 1
                lines.append(f"{head}: no floor at {'source' if not miss[0] else 'target'}")
                continue
            if not r[MORPH | SJ]:
                counts["nopath"] += 1
                lines.append(f"{head}: no path in the static mesh; terms " +
                             " | ".join(f"({term_text(t)})" for t, _b in terms))
                continue
            need = [LEVELS[s] for s in SETS if r[s] and not any(r[x] for x in SETS if x != s and x & s == x)]
            bad = [t for t, bits in terms if not r[bits]]
            if bad:
                counts["refuted"] += 1
                lines.append(f"{head}: needs {' or '.join(need)}; refuted terms " +
                             " | ".join(f"({term_text(t)})" for t in bad))
            else:
                counts["ok"] += 1
                if "--all" in args:
                    lines.append(f"{head}: ok, needs {' or '.join(need)}")
        if lines:
            print(area)
            print("\n".join(lines))
    print(f"edges checked: {sum(counts.values())} ({skipped} skipped: teleporter or named node); " +
          ", ".join(f"{k} {v}" for k, v in counts.items()))


if __name__ == "__main__":
    main()
