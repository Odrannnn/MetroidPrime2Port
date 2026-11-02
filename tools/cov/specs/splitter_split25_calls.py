# split25 forced calls, run once at a CSplitterCommandModule::Think entry
# (tools/cov/specs/splitter_split25.py).  Every member written here is written back after the call.
import os
import gdb

_LOG = os.path.join(os.environ['COV_OUT'], 'split25_calls.log')


def log(msg):
    with open(_LOG, 'a') as fh:
        fh.write(msg + '\n')


def run(cmd):
    try:
        out = gdb.execute(cmd, to_string=True)
        log('%s -> %s' % (cmd, out.strip()[:160]))
    except gdb.error as e:
        log('%s FAILED %s' % (cmd, str(e)[:160]))


def ev(expr):
    return gdb.parse_and_eval(expr)


gdb.execute('disable %d' % int(gdb.convenience_variable('s25_think')))
gdb.set_convenience_variable('s25_nf', 1)
gdb.execute('set $s25h = $rdi')
gdb.execute('set $s25m = (CStateManager*)$rsi')
H = '((CSplitterCommandModule*)$s25h)'
C = '((CSplitterMainChassis*)$split23_c521)'
M = '*$s25m'
P = '$s25m->mPlayers[0]'
TR = H + '->mTransform'
CT = C + '->mTransform'
log('split25: head %s uid %s chassis uid %s player uid %s' % (
    ev('$s25h'), ev(H + '->mUniqueId.value'), ev(C + '->mUniqueId.value'), ev(P + '->mUniqueId.value')))


def value(f):
    v = ev(f)
    if v.type.strip_typedefs().code == gdb.TYPE_CODE_FLT:
        return repr(float(v))
    return str(int(v))


def saved(fields, body):
    old = [(f, value(f)) for f in fields]
    try:
        body()
    finally:
        for f, v in old:
            run('set var %s = %s' % (f, v))


def vec(base):
    return [base + '.mX', base + '.mY', base + '.mZ']


def setvec(base, x, y, z):
    run('set var %s.mX = %s' % (base, x))
    run('set var %s.mY = %s' % (base, y))
    run('set var %s.mZ = %s' % (base, z))


XFORM = [CT + '.m%d%d' % (r, c) for r in range(3) for c in range(4)]


def col(xf, c, x, y, z):
    run('set var %s.m0%d = %s' % (xf, c, x))
    run('set var %s.m1%d = %s' % (xf, c, y))
    run('set var %s.m2%d = %s' % (xf, c, z))


# fn_75_B57C FindBestStabLeg: no target (0xB5B0); the target is the player and the Y and X
# columns are each the offset to it or its negation (legs 1-4).
def stab():
    run('set var %s->mSyncId.value = 0xffff' % C)
    run('call %s->FindBestStabLeg(%s, 0.016)' % (C, M))
    log('stab leg (no target): %s' % ev(C + '->mStabLeg'))
    run('set var %s->mSyncId.value = %s->mUniqueId.value' % (C, P))
    d = [float(ev('%s->mTransform.m%d3 - %s.m%d3' % (P, r, CT, r))) for r in range(3)]
    for sy, sx in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        col(CT, 1, sy * d[0], sy * d[1], sy * d[2])
        col(CT, 0, sx * d[0], sx * d[1], sx * d[2])
        run('call %s->FindBestStabLeg(%s, 0.016)' % (C, M))
        log('stab leg (y %d x %d): %s' % (sy, sx, ev(C + '->mStabLeg')))


saved([C + '->mSyncId.value', C + '->mStabLeg'] + XFORM, stab)


# fn_75_B2A0 FindBestDodgeDirection (the split23 gates are off while $s25_nf is 1): no flag bit;
# the timer at 0xD34 at 1.0f; the dodge timer above 0.0f; a missed roll (chance -1.0f); no
# player target; the player orbiting nothing, the chassis and the head.
def dodge():
    fl = C + '->mData.mUnknownCEF5C2FE'
    run('set var %s = %s & ~0x30' % (fl, fl))
    run('call %s->FindBestDodgeDirection(%s, 0.016)' % (C, M))
    run('set var %s = %s | 0x30' % (fl, fl))
    run('set var %s->mTimerD34 = 1.0' % C)
    run('call %s->FindBestDodgeDirection(%s, 0.016)' % (C, M))
    run('set var %s->mTimerD34 = 0' % C)
    run('set var %s->mDodgeTimer = 0.5' % C)
    run('call %s->FindBestDodgeDirection(%s, 0.016)' % (C, M))
    run('set var %s->mDodgeTimer = 0' % C)
    run('set var %s->mData.mDodgeChance = -1.0' % C)
    run('call %s->FindBestDodgeDirection(%s, 0.016)' % (C, M))
    log('dodge timer after miss: %s' % ev(C + '->mDodgeTimer'))
    run('set var %s->mDodgeTimer = 0' % C)
    run('set var %s->mData.mDodgeChance = 100.0' % C)
    run('set var %s->mSyncId.value = 0xffff' % C)
    run('call %s->FindBestDodgeDirection(%s, 0.016)' % (C, M))
    run('set var %s->mSyncId.value = %s->mUniqueId.value' % (C, P))
    for orb in ('0xffff', C + '->mUniqueId.value', H + '->mUniqueId.value'):
        run('set var %s->mCommandModuleId.value = %s->mUniqueId.value' % (C, H))
        run('set var %s->mOrbitTargetId.value = %s' % (P, orb))
        run('call %s->FindBestDodgeDirection(%s, 0.016)' % (C, M))
        log('dodge direction: %s' % ev(C + '->mDodgeDirection'))


saved([C + '->mData.mUnknownCEF5C2FE', C + '->mTimerD34', C + '->mDodgeTimer',
       C + '->mData.mDodgeChance', C + '->mSyncId.value', C + '->mCommandModuleId.value',
       C + '->mDodgeDirection', P + '->mOrbitTargetId.value'], dodge)

# fn_75_B678 SetupLaserSweep: no head (0xB6EC); then the head is this one: the player target;
# no target with the Y column steep up (45-degree clamp), steep down (15-degree clamp), gently up
# and down (no clamp), straight up (the flat form unsafe) and zero (the offset unsafe).
HL = [H + '->mLaserSweepStage'] + vec(H + '->mLaserSweepStart') + vec(H + '->mLaserSweepEnd')


def sweep():
    run('set var %s->mCommandModuleId.value = 0xffff' % C)
    run('call %s->SetupLaserSweep(%s, 0.016)' % (C, M))
    run('set var %s->mCommandModuleId.value = %s->mUniqueId.value' % (C, H))
    run('set var %s->mChassisId.value = %s->mUniqueId.value' % (H, C))
    run('set var %s->mSyncId.value = %s->mUniqueId.value' % (C, P))
    run('call %s->SetupLaserSweep(%s, 0.016)' % (C, M))
    run('set var %s->mSyncId.value = 0xffff' % C)
    for y in ((0, 1, 2), (0, 1, -1), (0, 1, 0.1), (0, 1, -0.1), (0, 0, 1), (0, 0, 0)):
        col(CT, 1, *y)
        run('set var %s->mLaserSweepStage = -1' % H)
        run('call %s->SetupLaserSweep(%s, 0.016)' % (C, M))
        log('sweep y %s: stage %s start %s end %s' % (y, ev(H + '->mLaserSweepStage'),
                                                       ev(H + '->mLaserSweepStart'), ev(H + '->mLaserSweepEnd')))


saved([C + '->mCommandModuleId.value', C + '->mSyncId.value', H + '->mChassisId.value'] + XFORM + HL,
      sweep)

# fn_75_44DC LaserSweep: activate with the target (the ends swapped, then not) and without;
# update with a zero frame time, the end straight over the locator (flat unsafe), ahead at stage 0
# (fires, stage 1) and 1 (ends), to the side (both Slerps); deactivate.
HX = [H + '->mSyncedId.value', H + '->mLaserSweepStage'] + vec(H + '->mLaserSweepStart') + \
    vec(H + '->mLaserSweepEnd') + vec(H + '->mLookAtOffset') + vec(H + '->mEditorProperties')


def laser():
    px, py, pz = (float(ev(TR + '.m%d3' % r)) for r in range(3))
    yx, yy, yz = (float(ev(TR + '.m%d1' % r)) for r in range(3))
    xx, xy, xz = (float(ev(TR + '.m%d0' % r)) for r in range(3))
    run('set var %s->mSyncedId.value = %s->mUniqueId.value' % (H, P))
    setvec(H + '->mLaserSweepStart', px - 10 * yx, py - 10 * yy, pz - 10 * yz)
    setvec(H + '->mLaserSweepEnd', px + 10 * yx, py + 10 * yy, pz + 10 * yz)
    run('call %s->LaserSweep(%s, (EStateMsg)0, 0.016)' % (H, M))
    log('after swap: start %s' % ev(H + '->mLaserSweepStart'))
    run('call %s->LaserSweep(%s, (EStateMsg)0, 0.016)' % (H, M))
    run('set var %s->mSyncedId.value = 0xffff' % H)
    run('call %s->LaserSweep(%s, (EStateMsg)0, 0.016)' % (H, M))
    run('set var %s->mLaserSweepStage = 0' % H)
    run('call %s->LaserSweep(%s, (EStateMsg)1, 0)' % (H, M))
    try:
        lx = float(ev('%s->GetLctrTransform(%s->mLookAtSegment).m03' % (H, H)))
        ly = float(ev('%s->GetLctrTransform(%s->mLookAtSegment).m13' % (H, H)))
        setvec(H + '->mLaserSweepStart', lx, ly, pz + 20)
        run('call %s->LaserSweep(%s, (EStateMsg)1, 0.016)' % (H, M))
        log('stage after flat-unsafe: %s' % ev(H + '->mLaserSweepStage'))
    except gdb.error as e:
        log('locator FAILED %s' % str(e)[:160])
    run('set var %s->mLaserSweepStage = 0' % H)
    setvec(H + '->mLaserSweepStart', px + 1000 * yx, py + 1000 * yy, pz)
    run('call %s->LaserSweep(%s, (EStateMsg)1, 100)' % (H, M))
    log('stage after ahead (0): %s' % ev(H + '->mLaserSweepStage'))
    setvec(H + '->mLaserSweepEnd', px + 1000 * yx, py + 1000 * yy, pz)
    run('set var %s->mLaserSweepStage = 1' % H)
    run('call %s->LaserSweep(%s, (EStateMsg)1, 100)' % (H, M))
    log('stage after ahead (1): %s' % ev(H + '->mLaserSweepStage'))
    run('set var %s->mLaserSweepStage = 1' % H)
    setvec(H + '->mLaserSweepEnd', px + 1000 * xx, py + 1000 * xy, pz)
    run('call %s->LaserSweep(%s, (EStateMsg)1, 0.016)' % (H, M))
    log('stage after side: %s' % ev(H + '->mLaserSweepStage'))
    run('call %s->LaserSweep(%s, (EStateMsg)2, 0.016)' % (H, M))
    run('call %s->LaserSweep(%s, (EStateMsg)3, 0.016)' % (H, M))


saved(HX, laser)


# fn_75_270C PortDodgeDirection: the chassis in another area; the chassis moved (its position) 3
# units right, left, ahead-right and ahead-left of the head with a dodge distance of 10 (each side
# closed, then left open past 60 degrees); a second AI beside it on each side; a short dodge
# distance, eight times (both sides open, the random bit both ways).
def dodgedir():
    run('set var %s->mAreaId.value = 99' % C)
    run('call %s->PortDodgeDirection(%s)' % (H, M))
    run('set var %s->mAreaId.value = %s->mAreaId.value' % (C, H))
    run('set var %s->mDodgeDistance = 10' % H)
    p = [float(ev(TR + '.m%d3' % r)) for r in range(3)]
    x = [float(ev(TR + '.m%d0' % r)) for r in range(3)]
    y = [float(ev(TR + '.m%d1' % r)) for r in range(3)]
    for sx, sy in ((3, 0), (-3, 0), (1, 6), (-1, 6)):
        for r in range(3):
            run('set var %s.m%s = %r' % (OT, 'XYZ'[r], p[r] + sx * x[r] + sy * y[r]))
        run('call %s->PortDodgeDirection(%s)' % (H, M))
    for r in range(3):
        run('set var %s.m%s = %r' % (OT, 'XYZ'[r], op[r]))
    # Two AIs on one side: the second one finds the side already closed (0x2820-0x2824 / 0x284C-0x2850).
    if O2 is not None:
        run('set var %s->mAreaId.value = %s->mAreaId.value' % (O2, H))
        for sx in (3, -3):
            for o, k in ((OT, 1.0), (O2 + '->mPosition', 1.5)):
                for r in range(3):
                    run('set var %s.m%s = %r' % (o, 'XYZ'[r], p[r] + k * sx * x[r]))
            run('call %s->PortDodgeDirection(%s)' % (H, M))
    for r in range(3):
        run('set var %s.m%s = %r' % (OT, 'XYZ'[r], op[r]))
    run('set var %s->mDodgeDistance = 0.5' % H)
    for _ in range(8):
        run('call %s->PortDodgeDirection(%s)' % (H, M))


# The chassis's CActor position (0x54-0x5C, what the walk reads; not the transform column).
OT = C + '->mPosition'
op = [float(ev(OT + '.m' + c)) for c in 'XYZ']


# The listening-AI list's first entry that is neither the head nor the chassis.
LA = '(*((rstl::auto_ptr<CObjectList>*)$s25m->mObjectLists.mData)[kOL_ListeningAi].mItem)'
O2 = None
idx = int(ev(LA + '.mFirstId'))
while idx != -1:
    e = '((CActor*)%s.mObjects[%d].mEntity)' % (LA, idx)
    if O2 is None and int(ev('(long)' + e)) not in (int(ev('(long)$s25h')), int(ev('(long)$split23_c521'))):
        O2 = e
    idx = int(ev('%s.mObjects[%d].mNext' % (LA, idx)))
log('second ai: %s' % (ev(O2 + '->mUniqueId.value') if O2 else None))
O2F = [O2 + '->mAreaId.value'] + vec(O2 + '->mPosition') if O2 else []
saved([C + '->mAreaId.value', H + '->mDodgeDistance'] + vec(OT) + O2F, dodgedir)

# fn_75_ECA4 CollidedWith: 0xD41's 0x80 bit clear; the timer at 0xD30 at 0.2f; then spinning
# past it: the world (no infos; a floor; a wall material; a steep normal), a missing object, the
# player (hurt; then the spin damage 0.0f), the beam effect (no health info) and the chassis
# itself (a CAi; a zero offset).  The list is calloc'd.
lst = int(ev('(long)calloc(1, sizeof(CCollisionInfoList))'))
L = '((CCollisionInfoList*)%d)' % lst
I = '((CCollisionInfo*)%s->mList.mData)' % L
idp = int(ev('(long)calloc(1, 8)'))
ID = '*(TUniqueId*)%d' % idp
SET_ID = 'set var ((TUniqueId*)%d)->value = %%s' % idp
CALL = 'call %s->CollidedWith(%s, *%s, %s)' % (C, ID, L, M)


def collided():
    run(SET_ID % '0xffff')
    run('set var %s->mFlagsD41 = %s->mFlagsD41 & 0x7f' % (C, C))
    run(CALL)
    run('set var %s->mFlagsD41 = %s->mFlagsD41 | 0x80' % (C, C))
    run('set var %s->mTimerD30 = 0.2' % C)
    run(CALL)
    run('set var %s->mTimerD30 = 1.0' % C)
    run(CALL)
    run('set var %s->mList.mCount = 1' % L)
    run('set var %s[0].mMaterialLeft.mValue = 1ULL << 59' % I)
    setvec(I + '[0].mNormalLeft', CT + '.m02', CT + '.m12', CT + '.m22')
    run(CALL)
    log('d40 after floor: %s' % ev(C + '->mFlagsD40'))
    run('set var %s[0].mMaterialLeft.mValue = 1ULL << 30' % I)
    run(CALL)
    log('d40 after wall: %s normal %s' % (ev(C + '->mFlagsD40'), ev(C + '->mCollisionNormal')))
    run('set var %s->mFlagsD40 = %s->mFlagsD40 & 0xfb' % (C, C))
    run('set var %s[0].mMaterialLeft.mValue = 1ULL << 59' % I)
    setvec(I + '[0].mNormalLeft', CT + '.m00', CT + '.m10', CT + '.m20')
    run(CALL)
    log('d40 after steep: %s' % ev(C + '->mFlagsD40'))
    run('set var %s->mFlagsD40 = %s->mFlagsD40 & 0xfb' % (C, C))
    run('set var %s->mList.mCount = 0' % L)
    run(SET_ID % '0x7ffe')
    run(CALL)
    run(SET_ID % (P + '->mUniqueId.value'))
    run(CALL)
    log('d40 after player hurt: %s' % ev(C + '->mFlagsD40'))
    run('set var %s->mData.mSpinAttackDamage.mDamage = 0' % C)
    run(CALL)
    log('d40 after player unhurt: %s normal %s' % (ev(C + '->mFlagsD40'), ev(C + '->mCollisionNormal')))
    run('set var %s->mFlagsD40 = %s->mFlagsD40 & 0xfb' % (C, C))
    # The first non-AI actor of the actor list with no health info (the walk is capped).
    al = '(*((rstl::auto_ptr<CObjectList>*)$s25m->mObjectLists.mData)[kOL_Actor].mItem)'
    try:
        idx = int(ev(al + '.mFirstId'))
        for _ in range(2048):
            if idx == -1:
                break
            a = '((CActor*)%s.mObjects[%d].mEntity)' % (al, idx)
            if int(ev('(long)%s->TypesMatch(kET_Ai)' % a)) == 0 and \
                    int(ev('(long)%s->GetHealthInfo()' % a)) == 0:
                log('no-health actor %s idx %d' % (ev(a + '->mUniqueId.value'), idx))
                run(SET_ID % (a + '->mUniqueId.value'))
                run(CALL)
                log('d40 after no-health actor: %s' % ev(C + '->mFlagsD40'))
                run('set var %s->mFlagsD40 = %s->mFlagsD40 & 0xfb' % (C, C))
                break
            idx = int(ev('%s.mObjects[%d].mNext' % (al, idx)))
    except gdb.error as e:
        log('actor walk FAILED %s' % str(e)[:160])
    run(SET_ID % (C + '->mUniqueId.value'))
    run(CALL)
    log('d40 after self: %s' % ev(C + '->mFlagsD40'))


saved([C + '->mFlagsD41', C + '->mFlagsD40', C + '->mTimerD30', C + '->mData.mSpinAttackDamage.mDamage']
      + vec(C + '->mCollisionNormal'), collided)
gdb.set_convenience_variable('s25_nf', 0)
log('split25: done')
