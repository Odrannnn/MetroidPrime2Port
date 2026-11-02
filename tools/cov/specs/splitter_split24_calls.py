# split24 forced calls, run once at a CSplitterCommandModule::Think entry
# (tools/cov/specs/splitter_split24.py).  Every member written here is written back after the call.
import os
import gdb

_LOG = os.path.join(os.environ['COV_OUT'], 'split24_calls.log')


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


H = '((CSplitterCommandModule*)$s24h)'
C = '((CSplitterMainChassis*)$split23_c521)'
TD = '*(CTriggerData*)$s24h'
# The Think breakpoint that sourced this file must not stop the forced Think calls below.
gdb.execute('disable %d' % int(gdb.convenience_variable('s24_think')))
gdb.execute('set $s24h = $rdi')
gdb.execute('set $s24m = (CStateManager*)$rsi')
M = '*$s24m'
log('split24: head %s uid %s' % (ev('$s24h'), ev(H + '->mUniqueId.value')))


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


# fn_75_5864 HasDockingPath: no CONN/FOLW link (false); GetConnectedObject answering the head's
# own id (true).
run('call %s->HasDockingPath(%s, %s)' % (H, M, TD))
gdb.execute('set $split24_gcoid = %s->mUniqueId.value' % H)
gdb.execute('set $split24_gco = 1')
run('call %s->HasDockingPath(%s, %s)' % (H, M, TD))
gdb.execute('set $split24_gco = 0')

# fn_75_3FE0 FollowDockingPath: activate, update with a safe and with a zero move vector,
# deactivate and an out-of-range message.
BC = H + '->mBodyController.mPtr'
MV = BC + '->mCmdMgr.mMove'
TR = H + '->mTransform'


def follow():
    run('call %s->FollowDockingPath(%s, (EStateMsg)0, 0.016)' % (H, M))
    run('set var %s.mX = 1' % MV)
    run('set var %s.mY = 0' % MV)
    run('set var %s.mZ = 0' % MV)
    run('call %s->FollowDockingPath(%s, (EStateMsg)1, 0.016)' % (H, M))
    run('set var %s.mX = 0' % MV)
    run('call %s->FollowDockingPath(%s, (EStateMsg)1, 0.016)' % (H, M))
    run('call %s->FollowDockingPath(%s, (EStateMsg)2, 0.016)' % (H, M))
    run('call %s->FollowDockingPath(%s, (EStateMsg)3, 0.016)' % (H, M))


saved([MV + '.mX', MV + '.mY', MV + '.mZ', BC + '->mLocomotionType', TR + '.m03', TR + '.m13',
       TR + '.m23', H + '->mWaypointNavigation.mDestination.value'], follow)

# fn_75_4E90 PathFind: both 0xF6A 0x80 senses, a zero move vector, deactivate and an
# out-of-range message.


def pathfind():
    run('set var %s.mX = 0' % MV)
    run('set var %s.mY = 0' % MV)
    run('set var %s.mZ = 0' % MV)
    run('set var %s->mAutoDestructFlags = %s->mAutoDestructFlags & 0x7f' % (H, H))
    run('call %s->PathFind(%s, (EStateMsg)1, 0.016)' % (H, M))
    run('call %s->PathFind(%s, (EStateMsg)3, 0.016)' % (H, M))
    run('call %s->PathFind(%s, (EStateMsg)2, 0.016)' % (H, M))


saved([MV + '.mX', MV + '.mY', MV + '.mZ', H + '->mAutoDestructFlags', TR + '.m03', TR + '.m13',
       TR + '.m23'], pathfind)

# fn_75_523C Scanning (update): no chassis; a chassis and the head's own hit bit; the Y column
# flattened to zero; the X column flattened to zero; an out-of-range message.
SC = [H + '->mChassisId.value', H + '->mHitByPlayerProjectile', H + '->mEditorProperties.mX',
      H + '->mEditorProperties.mY', H + '->mEditorProperties.mZ', TR + '.m00', TR + '.m10',
      TR + '.m01', TR + '.m11', C + '->mHitByPlayerProjectile']


def scanning():
    run('set var %s->mChassisId.value = 0xffff' % H)
    run('call %s->Scanning(%s, (EStateMsg)1, 0.016)' % (H, M))
    run('set var %s->mChassisId.value = %s->mUniqueId.value' % (H, C))
    run('set var %s->mHitByPlayerProjectile = 1' % H)
    run('call %s->Scanning(%s, (EStateMsg)1, 0.016)' % (H, M))
    log('chassis hit bit after: %s' % ev(C + '->mHitByPlayerProjectile'))
    run('set var %s.m01 = 0' % TR)
    run('set var %s.m11 = 0' % TR)
    run('call %s->Scanning(%s, (EStateMsg)1, 0.016)' % (H, M))
    run('set var %s.m01 = 1' % TR)
    run('set var %s.m00 = 0' % TR)
    run('set var %s.m10 = 0' % TR)
    run('call %s->Scanning(%s, (EStateMsg)1, 0.016)' % (H, M))
    run('call %s->Scanning(%s, (EStateMsg)3, 0.016)' % (H, M))


saved(SC, scanning)

# fn_75_23F0 / fn_75_8EC8: every listening AI in another area (the head's and the chassis's own
# area id moved for one call).
saved([H + '->mAreaId.value'], lambda: (run('set var %s->mAreaId.value = 77' % H),
                                       run('call %s->PortSeparationVector(%s)' % (H, M))))
saved([C + '->mAreaId.value'], lambda: (run('set var %s->mAreaId.value = 77' % C),
                                       run('call %s->PortSeparationVector(%s)' % (C, M))))

# Chassis triggers: fn_75_E58C HasHead and fn_75_E62C IsIntact (head id valid / invalid),
# fn_75_E530 CanReachTarget (0xD3C 0.0f / 5.0f), fn_75_E494 PathOver (0xD40 0x20 clear / set).


def triggers():
    run('set var %s->mCommandModuleId.value = %s->mUniqueId.value' % (C, H))
    run('call %s->HasHead(%s, %s)' % (C, M, TD))
    run('call %s->IsIntact(%s, %s)' % (C, M, TD))
    run('set var %s->mCommandModuleId.value = 0xffff' % C)
    run('call %s->HasHead(%s, %s)' % (C, M, TD))
    run('call %s->IsIntact(%s, %s)' % (C, M, TD))
    run('set var %s->mTimerD3C = 0' % C)
    run('call %s->CanReachTarget(%s, %s)' % (C, M, TD))
    run('set var %s->mTimerD3C = 5' % C)
    run('call %s->CanReachTarget(%s, %s)' % (C, M, TD))
    run('set var %s->mFlagsD40 = %s->mFlagsD40 & 0xdf' % (C, C))
    run('call %s->PathOver(%s, %s)' % (C, M, TD))
    run('set var %s->mFlagsD40 = %s->mFlagsD40 | 0x20' % (C, C))
    run('call %s->PathOver(%s, %s)' % (C, M, TD))


saved([C + '->mCommandModuleId.value', C + '->mTimerD3C', C + '->mFlagsD40'], triggers)

# fn_75_BBC0 SetRetreatDest: no follow object; a waypoint (the chassis's or the head's waypoint
# navigation destination, when one is set).  fn_75_BC70 SetTargetDest: no target.
# fn_75_BE28 SelectTarget: no head (0xC6C invalid: no AddEmitter).
PF = C + '->mPathFindNavigation'


def dests():
    run('set var %s->mFollowObjectId.value = 0xffff' % C)
    run('call %s->SetRetreatDest(%s, 0.016)' % (C, M))
    wp = 0xffff
    for f in (C + '->mWaypointNavigation.mDestination.value',
              C + '->mWaypointNavigation.mLastDestination.value',
              H + '->mWaypointNavigation.mDestination.value',
              H + '->mWaypointNavigation.mLastDestination.value'):
        v = int(ev(f))
        if v != 0xffff:
            wp = v
            break
    log('waypoint id %#x' % wp)
    run('set var %s->mFollowObjectId.value = %d' % (C, wp))
    run('call %s->SetRetreatDest(%s, 0.016)' % (C, M))
    log('D40 after waypoint: %s' % ev(C + '->mFlagsD40'))
    run('set var %s->mSyncId.value = 0xffff' % C)
    run('call %s->SetTargetDest(%s, 0.016)' % (C, M))
    run('set var %s->mCommandModuleId.value = 0xffff' % C)
    run('call %s->SelectTarget(%s, 0.016)' % (C, M))


saved([C + '->mFollowObjectId.value', C + '->mFlagsD40', C + '->mSyncId.value',
       C + '->mCommandModuleId.value', C + '->mTeamId.value'], dests)


def gate(var, value):
    gdb.set_convenience_variable(var, value)


# split24 r3 ------------------------------------------------------------------------------------
# fn_75_4D04 Hover (update): a zero frame time (0x4D64), no target (0x4D80), the head 200 above
# and 200 below its target (the chassis, 0x4E18 both senses).
HV = [H + '->mSyncedId.value', TR + '.m23', MV + '.mX', MV + '.mY', MV + '.mZ']


def hover():
    run('call %s->Hover(%s, (EStateMsg)1, 0)' % (H, M))
    run('set var %s->mSyncedId.value = 0xffff' % H)
    run('call %s->Hover(%s, (EStateMsg)1, 0.016)' % (H, M))
    run('set var %s->mSyncedId.value = %s->mUniqueId.value' % (H, C))
    run('set var %s.m23 = %s->mTransform.m23 + 200' % (TR, C))
    run('call %s->Hover(%s, (EStateMsg)1, 0.016)' % (H, M))
    run('set var %s.m23 = %s->mTransform.m23 - 200' % (TR, C))
    run('call %s->Hover(%s, (EStateMsg)1, 0.016)' % (H, M))


saved(HV, hover)

# fn_75_3FE0 FollowDockingPath (update): IsMagnitudeSafe answering false and true (Patrol
# rewrites the move vector, so the answer is forced).
for v in (0, 1):
    gate('s24_ims', v)
    saved([MV + '.mX', MV + '.mY', MV + '.mZ', TR + '.m03', TR + '.m13', TR + '.m23',
           BC + '->mLocomotionType'],
          lambda: run('call %s->FollowDockingPath(%s, (EStateMsg)1, 0.016)' % (H, M)))
gate('s24_ims', None)


# fn_75_523C Scanning (update): a chassis, the hit bit clear, InDetectionRange false and true.
def scanning2():
    run('set var %s->mChassisId.value = %s->mUniqueId.value' % (H, C))
    run('set var %s->mHitByPlayerProjectile = 0' % H)
    for v in (0, 1):
        gate('s24_idr', v)
        run('call %s->Scanning(%s, (EStateMsg)1, 0.016)' % (H, M))
    gate('s24_idr', None)


saved(SC, scanning2)

# fn_75_5A54 PortAutoDestruct: alive with the 0x04 bit clear (sets it), alive with it set, dead.
AD = [H + '->mAutoDestructFlags', H + '->mDeathTimer', H + '->mAlive']


def autodestruct():
    run('set var %s->mAutoDestructFlags = %s->mAutoDestructFlags & 0xfb' % (H, H))
    run('call %s->PortAutoDestruct(30)' % H)
    log('after: flags %s timer %s' % (ev(H + '->mAutoDestructFlags'), ev(H + '->mDeathTimer')))
    run('call %s->PortAutoDestruct(40)' % H)
    log('after (bit set): timer %s' % ev(H + '->mDeathTimer'))
    run('set var %s->mAutoDestructFlags = %s->mAutoDestructFlags & 0xfb' % (H, H))
    run('set var %s->mAlive = 0' % H)
    run('call %s->PortAutoDestruct(50)' % H)
    log('after (dead): flags %s timer %s' % (ev(H + '->mAutoDestructFlags'), ev(H + '->mDeathTimer')))


saved(AD, autodestruct)

# fn_75_23F0 head PortSeparationVector: a second head in the same area, docked and undocked.
if gdb.convenience_variable('s24h2') is not None:
    h1 = int(gdb.convenience_variable('s24h1'))
    h2 = int(gdb.convenience_variable('s24h2'))
    other = h2 if h1 == int(ev('(long)$s24h')) else h1
    O = '((CSplitterCommandModule*)%d)' % other
    log('second head %#x uid %s' % (other, ev(O + '->mUniqueId.value')))

    def seps():
        run('set var %s->mAreaId.value = %s->mAreaId.value' % (O, H))
        run('set var %s->mChassisId.value = %s->mUniqueId.value' % (O, C))
        run('call %s->PortSeparationVector(%s)' % (H, M))
        run('set var %s->mChassisId.value = 0xffff' % O)
        run('call %s->PortSeparationVector(%s)' % (H, M))

    saved([O + '->mAreaId.value', O + '->mChassisId.value'], seps)
else:
    log('no second head recorded')

# fn_75_91E4 PortFindDodgeDirection / fn_75_909C PortDodgeIsClear: every step direction answer
# (0x9258-0x9304) at several step distances (0 skips every actor; 1000 takes all of them in), the
# chassis alone in its area once.  The offset is the head's editor-properties vector.
AW = H + '->mEditorProperties'


def dodge():
    run('set var %s.mX = 1' % AW)
    run('set var %s.mY = 2' % AW)
    run('set var %s.mZ = 0' % AW)
    for step in (0, 0.5, 3, 15, 1000):
        run('set var %s->mStepDistance = %s' % (C, step))
        for sd in (0, 1, 2, 3, 4, -1):
            gate('s24_sd', sd)
            run('call %s->PortFindDodgeDirection(%s, %s)' % (C, M, AW))
    run('set var %s->mAreaId.value = 77' % C)
    run('call %s->PortFindDodgeDirection(%s, %s)' % (C, M, AW))
    gate('s24_sd', None)
    run('set var %s->mStepDistance = 0.5' % C)
    for i in range(8):
        run('call %s->PortFindDodgeDirection(%s, %s)' % (C, M, AW))


saved([AW + '.mX', AW + '.mY', AW + '.mZ', C + '->mStepDistance', C + '->mAreaId.value'], dodge)


# The same side twice (0x93AC / 0x93D8: a side already closed skips the angle): both heads one
# unit along the axis (step direction 4: the chassis's X column), then one unit against it.
def dodge_same_side():
    if gdb.convenience_variable('s24h2') is None:
        log('no second head: same-side dodge skipped')
        return
    O2 = '((CSplitterCommandModule*)$s24h2)'
    O1 = '((CSplitterCommandModule*)$s24h1)'
    run('set var %s->mStepDistance = 10' % C)
    gate('s24_sd', 4)
    for sign in (1, -1):
        for O in (O1, O2):
            run('set var %s->mAreaId.value = %s->mAreaId.value' % (O, C))
            for k, a in (('m03', 'm00'), ('m13', 'm10'), ('m23', 'm20')):
                run('set var %s->mTransform.%s = %s->mTransform.%s + (%d) * %s->mTransform.%s'
                    % (O, k, C, k, sign, C, a))
        run('call %s->PortFindDodgeDirection(%s, %s)' % (C, M, AW))
    gate('s24_sd', None)


_same = [C + '->mStepDistance']
for _o in ('$s24h1', '$s24h2'):
    if gdb.convenience_variable(_o[1:]) is not None:
        _O = '((CSplitterCommandModule*)%s)' % _o
        _same += [_O + '->mAreaId.value', _O + '->mTransform.m03', _O + '->mTransform.m13',
                  _O + '->mTransform.m23']
saved(_same, dodge_same_side)
# PortDodgeIsClear straight: up and down 5 units (a clear ray off the mesh, a floor hit).
saved([AW + '.mX', AW + '.mY', AW + '.mZ'], lambda: (
    run('set var %s.mX = 0' % AW), run('set var %s.mY = 0' % AW), run('set var %s.mZ = 1' % AW),
    run('call %s->PortDodgeIsClear(%s, %s, 5)' % (C, M, AW)),
    run('call %s->PortDodgeIsClear(%s, %s, -5)' % (C, M, AW)),
    run('call %s->PortDodgeIsClear(%s, %s, 0.1)' % (C, M, AW))))


# fn_75_E494 PathOver with the base's PathOver answering true; the head's fn_75_55B0 too.
def pathover():
    run('set var %s->mFlagsD40 = %s->mFlagsD40 | 0x20' % (C, C))
    run('set var %s->mAutoDestructFlags = %s->mAutoDestructFlags | 0x80' % (H, H))
    gate('s24_po', 1)
    run('call %s->PathOver(%s, %s)' % (C, M, TD))
    run('call %s->PathOver(%s, %s)' % (H, M, TD))
    gate('s24_po', None)


saved([C + '->mFlagsD40', H + '->mAutoDestructFlags'], pathover)

# fn_75_BC70 SetTargetDest with line of sight and a role: projectile far (range 0), projectile
# near (range 1e4), melee.  The role is a malloc'd CTeamAiRole.
role = int(ev('(long)malloc(64)'))
R = '((CTeamAiRole*)%d)' % role
SD = [C + '->mSyncId.value', C + '->mVisualKnowledge.mHasLineOfSight', C + '->mFlagsD40',
      C + '->mData.mSpinAttackMinAttackRange', C + '->mData.mSpinAttackMaxAttackRange',
      C + '->mTeamId.value']


def targetdest():
    run('set var %s->mSyncId.value = %s->mUniqueId.value' % (C, H))
    run('set var %s->mVisualKnowledge.mHasLineOfSight = 1' % C)
    gate('s24_role', role)
    gate('s24_role', 0)
    run('call %s->SetTargetDest(%s, 0.016)' % (C, M))
    gate('s24_role', role)
    for cur, rng in ((2, 0), (2, 10000), (1, 0)):
        run('set var %s->mCurRole = %d' % (R, cur))
        run('set var %s->mData.mSpinAttackMinAttackRange = %d' % (C, rng))
        run('set var %s->mData.mSpinAttackMaxAttackRange = %d' % (C, rng))
        run('call %s->SetTargetDest(%s, 0.016)' % (C, M))
        log('D40 after role %d range %d: %s' % (cur, rng, ev(C + '->mFlagsD40')))
    gate('s24_role', None)


saved(SD, targetdest)

# fn_75_C8F4 WaitForSnatch: Activate, Update (no swarm: the id cleared), Deactivate with the head;
# Activate and Deactivate without it; an out-of-range message.
WS = [C + '->mSnatcherId.value', C + '->mSpinPenaltyTimer', C + '->mCommandModuleId.value']


def snatch():
    run('set var %s->mCommandModuleId.value = %s->mUniqueId.value' % (C, H))
    run('set var %s->mSnatcherId.value = %s->mUniqueId.value' % (C, H))
    run('call %s->WaitForSnatch(%s, (EStateMsg)0, 0.016)' % (C, M))
    run('call %s->WaitForSnatch(%s, (EStateMsg)1, 0.016)' % (C, M))
    log('snatcher after update: %s' % ev(C + '->mSnatcherId.value'))
    run('call %s->WaitForSnatch(%s, (EStateMsg)2, 0.016)' % (C, M))
    run('set var %s->mCommandModuleId.value = 0xffff' % C)
    run('call %s->WaitForSnatch(%s, (EStateMsg)0, 0.016)' % (C, M))
    run('call %s->WaitForSnatch(%s, (EStateMsg)2, 0.016)' % (C, M))
    run('call %s->WaitForSnatch(%s, (EStateMsg)3, 0.016)' % (C, M))


saved(WS, snatch)

# Inactive Thinks: the head (fn_75_6490), the chassis (fn_75_F0E8) and the beam (fn_75_10DC4).
for obj in (H, C):
    saved(['((CEntity*)%s)->mActive' % obj], lambda: (
        run('set var ((CEntity*)%s)->mActive = 0' % obj),
        run('call %s->Think(0.016, %s)' % (obj, M))))


# fn_75_6178 CollidedWith: the 0x01 bit set; a valid id; an invalid id with no infos (the
# material list built once), one non-matching info (kMT_Solid) and a non-matching then a
# matching one (kMT_Solid, kMT_Wall: the bit set, the walk ends).  The list is calloc'd.
lst = int(ev('(long)calloc(1, sizeof(CCollisionInfoList))'))
L = '((CCollisionInfoList*)%d)' % lst
I = '((CCollisionInfo*)%s->mList.mData)' % L
idp = int(ev('(long)calloc(1, 8)'))
ID = '*(TUniqueId*)%d' % idp


def collided():
    run('set var %s->mAutoDestructFlags = %s->mAutoDestructFlags | 1' % (H, H))
    run('set var ((TUniqueId*)%d)->value = 0xffff' % idp)
    run('call %s->CollidedWith(%s, *%s, %s)' % (H, ID, L, M))
    run('set var %s->mAutoDestructFlags = %s->mAutoDestructFlags & 0xfe' % (H, H))
    run('set var ((TUniqueId*)%d)->value = %s->mUniqueId.value' % (idp, C))
    run('call %s->CollidedWith(%s, *%s, %s)' % (H, ID, L, M))
    run('set var ((TUniqueId*)%d)->value = 0xffff' % idp)
    run('call %s->CollidedWith(%s, *%s, %s)' % (H, ID, L, M))
    run('set var %s->mList.mCount = 1' % L)
    run('set var %s[0].mMaterialLeft.mValue = 1ULL << 59' % I)
    run('call %s->CollidedWith(%s, *%s, %s)' % (H, ID, L, M))
    log('flags after no match: %s' % ev(H + '->mAutoDestructFlags'))
    run('set var %s->mList.mCount = 2' % L)
    run('set var %s[1].mMaterialLeft.mValue = 1ULL << 30' % I)
    run('call %s->CollidedWith(%s, *%s, %s)' % (H, ID, L, M))
    log('flags after match: %s' % ev(H + '->mAutoDestructFlags'))


saved([H + '->mAutoDestructFlags'], collided)


# fn_75_4D04 Hover diagnostics: the target's aim position and the head's z around each call.
def hover2():
    for tgt in (C + '->mUniqueId.value', '$s24h2u'):
        if tgt == '$s24h2u':
            if gdb.convenience_variable('s24h2') is None:
                continue
            gdb.execute('set $s24h2u = ((CSplitterCommandModule*)$s24h2)->mUniqueId.value')
        run('set var %s->mSyncedId.value = %s' % (H, tgt))
        run('print %s->mSyncedId.value' % H)
        for dz in (200, -200):
            run('set var %s.m23 = %s->mTransform.m23 + (%d)' % (TR, C, dz))
            log('head z %s chassis z %s' % (ev(TR + '.m23'), ev(C + '->mTransform.m23')))
            run('call %s->Hover(%s, (EStateMsg)1, 0.016)' % (H, M))


saved(HV, hover2)

# fn_75_3EEC PortDockToChassis: no chassis at 0xEFC (0x3F24); a chassis that already has a head
# (fn_75_E6E4 false, 0x3F40); accepted with 0xF6A's 0x02 bit set and the data's 0x1 bit set (no
# sound), set and clear (sound), and the 0x02 bit clear (sound).
DK = [H + '->mChassisId.value', H + '->mCheckedChassisId.value', H + '->mNextDrawNode.value',
      H + '->mAutoDestructFlags', H + '->mData.mUnknownBD80FD94', BC + '->mLocomotionType',
      C + '->mCommandModuleId.value', C + '->mSecondLinkId.value', C + '->mFlagsD40']


def dock():
    run('set var %s->mCheckedChassisId.value = 0xffff' % H)
    run('call %s->PortDockToChassis(%s)' % (H, M))
    run('set var %s->mCheckedChassisId.value = %s->mUniqueId.value' % (H, C))
    run('set var %s->mCommandModuleId.value = %s->mUniqueId.value' % (C, H))
    run('call %s->PortDockToChassis(%s)' % (H, M))
    for flag, data in ((1, 1), (1, 0), (0, 1)):
        run('set var %s->mCheckedChassisId.value = %s->mUniqueId.value' % (H, C))
        run('set var %s->mCommandModuleId.value = 0xffff' % C)
        run('set var %s->mAutoDestructFlags = (%s->mAutoDestructFlags & 0xfd) | %d' % (H, H, flag * 2))
        run('set var %s->mData.mUnknownBD80FD94 = %d' % (H, data))
        run('call %s->PortDockToChassis(%s)' % (H, M))
        log('docked: chassis %s flags %s' % (ev(H + '->mChassisId.value'), ev(H + '->mAutoDestructFlags')))


saved(DK, dock)
log('split24: done')
