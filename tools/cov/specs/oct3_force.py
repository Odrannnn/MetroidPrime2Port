# OctapedeSegment fourth pass. Run with tools/cov/force.sh, MP2_HEADLESS=1,
# BUILD=<oct3 frozen-build> and tools/drivers/oct3_messages.con (1200 frames).
# The earlier force1 fixture/driver and every executed SHA256 live in oct3-evidence.
# Sites below are PIE-relative, verified against oct3's frozen GCC coverage binary.
# No returns or branch PCs are patched: writes change caller inputs before their loads.
# Each write is restored on the SAME call's return, including pointed-to list/area data.
import os
import gdb

CLASS = 'COctapedeSegment'
FORCE = []
THINK = '_ZN16COctapedeSegment5ThinkEfR13CStateManager'
ACCEPT = '_ZN16COctapedeSegment15AcceptScriptMsgER13CStateManagerRK10CScriptMsg'
COLLIDE = '_ZN16COctapedeSegment12CollidedWithERK9TUniqueIdRK18CCollisionInfoListR13CStateManager'


def log(text):
    with open(os.path.join(os.environ['COV_OUT'], 'forced.log'), 'a') as out:
        out.write(text + '\n')


def ev(expr):
    return gdb.parse_and_eval(expr)


def ptr(typ, expr):
    return '((%s*)%#x)' % (typ, int(ev(expr)))


class Restore(gdb.FinishBreakpoint):
    def __init__(self, label):
        super().__init__(gdb.newest_frame(), internal=True)
        self.writes = []
        self.label = label

    def put(self, expr, value):
        if isinstance(value, bool):
            value = int(value)
        old = ev(expr)
        # Persist scalar values, never a lazy gdb lvalue whose contents can change.
        old = str(float(old)) if old.type.code == gdb.TYPE_CODE_FLT else str(int(old))
        self.writes.append((expr, old))
        gdb.execute('set var %s = %s' % (expr, value))
        log('%s: %s <- %s (was %s)' % (self.label, expr, value, old))

    def stop(self):
        for expr, old in reversed(self.writes):
            gdb.execute('set var %s = %s' % (expr, old))
        log(self.label + ': restored')
        return False


class Cases(gdb.Breakpoint):
    def __init__(self, symbol, offset, label, count, action, skip=0):
        super().__init__('*%s+%d' % (symbol, offset), internal=True)
        self.label, self.count, self.action = label, count, action
        self.n, self.skip = 0, skip

    def stop(self):
        if self.skip:
            self.skip -= 1
            return False
        try:
            if self.action(self.n, self.label):
                self.n += 1
            if self.n == self.count:
                self.enabled = False
        except Exception as error:
            log(self.label + ': ERROR ' + str(error))
            self.enabled = False
        return False


def entry(n, label):
    obj = ptr(CLASS, '$rdi')
    r = Restore(label + '/' + str(n))
    if n == 0:
        r.put(obj + '->mActive', 0)
    else:
        mgr = ptr('CStateManager', '$rsi')
        area = ptr('CGameArea', mgr + '->mWorld.mPtr->mAreas.mItems[' +
                   obj + '->mAreaId.value].mItem')
        r.put(obj + '->mBouncesRemaining', 0)
        if n == 1:
            r.put(area + '->mPostConst.mPtr->mOcclusionState', 0)
        elif n == 2:
            # Only the inline accessor reads this before the early return; restore
            # before any world/collision tick sees the temporarily unloaded phase.
            r.put(area + '->mPhase', 0)
    return True


def obstruction(n, label):
    obj = ptr(CLASS, '$rbx')
    r = Restore(label)
    r.put(obj + '->mPlayerObstructed', 1)
    return True


def post_base(n, label):
    obj = ptr(CLASS, '$rbx')
    r = Restore(label + '/' + str(n))
    if n < 2:
        r.put(obj + '->mDisableMove', 0)
        r.put(obj + '->mBodyController.mPtr->mFrozen', 0)
        r.put(obj + '->mSegmentState', 1 if n == 0 else 0)
        r.put(obj + '->mAlignToFloor', 0)
    elif n < 4:
        r.put(obj + '->mHeadSegmentId.value', 0x7ffe)
        r.put(obj + '->mBodyController.mPtr->mFrozen', 0)
        r.put(obj + '->mSegmentState', 1 if n == 2 else 0)
        r.put(obj + '->mBrokenApart', 1)
    elif n == 4:
        r.put(obj + '->mHeadSegmentId.value', 0xffff)
        r.put(obj + '->mSegmentState', 0)
        r.put(obj + '->mHealthInfo.mHealth', 0)
        r.put(obj + '->mBrokenApart', 1)
    elif n == 5:
        r.put(obj + '->mHeadSegmentId.value', int(ev(obj + '->mUniqueId.value')))
        r.put(obj + '->mHealthInfo.mHealth', 0)
        r.put(obj + '->mSegmentState', 0)
        r.put(obj + '->mBrokenApart', 0)
        r.put(obj + '->mBodyController.mPtr->mFrozen', 1)
    else:
        r.put(obj + '->mHeadSegmentId.value', 0xffff)
        r.put(obj + '->mSegmentState', 1)
        r.put(obj + '->mBodyController.mPtr->mFrozen', 1)
    return True


def tail(n, label):
    obj = ptr(CLASS, '$rbx')
    if not int(ev(obj + '->mElectric.mItem')):
        return False
    r = Restore(label + '/' + str(n))
    r.put(obj + '->mFollowedSegmentId.value', int(ev(obj + '->mUniqueId.value')))
    r.put(obj + ('->mElectric.mItem' if n == 0 else '->mAlive'), 0)
    return True


bad_connections = set()


def connection(n, label):
    conn = ptr('SConnection', '$rbx')
    if int(ev(conn + '->state')) != 0x41505243:
        return False
    action = int(ev(conn + '->msg')) == 0x4143544e
    if action in bad_connections:
        return False
    bad_connections.add(action)
    r = Restore(label + ('/Action' if action else '/Activate'))
    r.put(conn + '->objId.value', 0xffffffff)
    return True


def head_children(n, label):
    obj = ptr(CLASS, '$r14')
    r = Restore(label + '/' + str(n))
    if n == 0:
        r.put(obj + '->mHeadSegmentId.value', int(ev(obj + '->mUniqueId.value')))
    else:
        assert int(ev(obj + '->mChildSegmentIds.mCapacity')) >= 16
        r.put(obj + '->mChildSegmentIds.mItems[0].value', 0xffff)
        r.put(obj + '->mChildSegmentIds.mCount', 1)
    return True


def landed(n, label):
    obj = ptr(CLASS, '$rdi')
    r = Restore(label + '/' + str(n))
    r.put(obj + '->mSegmentState', 0 if n == 2 else 1)
    r.put(obj + '->mBouncesRemaining', 2 if n == 0 else 0)
    return True


def collision(n, label):
    obj = ptr(CLASS, '$rdi')
    uid = ptr('TUniqueId', '$rsi')
    info = ptr('CCollisionInfoList', '$rdx')
    count = int(ev(info + '->mList.mCount'))
    if n in (2, 3) and count == 0:
        return False
    r = Restore(label + '/' + str(n))
    r.put(obj + '->mBrokenApart', n != 0)
    if n == 0:
        return True
    r.put(uid + '->value', 0xffff)
    if n == 1:
        r.put(info + '->mList.mCount', 0)
        r.put(obj + '->mBouncesRemaining', 2)
    elif n in (2, 3):
        data = ptr('CCollisionInfo', '&' + info + '->mList.mData[0]')
        for i in range(count):
            r.put(data + '[%d].mMaterialLeft.mValue' % i, 0 if n == 2 else 1 << 31)
        r.put(obj + '->mBouncesRemaining', 0 if n == 3 else 2)
    elif n == 4:
        mgr = ptr('CStateManager', '$rcx')
        r.put(uid + '->value', int(ev(mgr + '->mPlayers[0]->mUniqueId.value')))
    elif n == 5:
        r.put(uid + '->value', int(ev(obj + '->mUniqueId.value')))
    else:
        mgr = ptr('CStateManager', '$rcx')
        # kOL_Actor is index 1. Pick a real actor from its qualified list; exclude
        # players/segments so the actor-only collision arm receives an actual actor.
        actors = ev('((rstl::auto_ptr<CObjectList>*)&' + mgr +
                    '->mObjectLists.mData[0])[1].mItem')
        actor_list = '((CObjectList*)%#x)' % int(actors)
        for i in range(2048):
            actor = ev(actor_list + '->mObjects[%d].mEntity' % i)
            if not int(actor):
                continue
            a = '((CActor*)%#x)' % int(actor)
            vtable = gdb.execute('info symbol *(void**)%#x' % int(actor), to_string=True)
            if 'CPlayer' in vtable or 'COctapedeSegment' in vtable:
                continue
            log(label + '/actor: ' + vtable.strip())
            r.put(uid + '->value', int(ev(a + '->mUniqueId.value')))
            r.put(obj + '->mBouncesRemaining', 2)
            r.put(obj + '->mBounceLatch', 0)
            return True
        raise RuntimeError('No non-player, non-segment actor')
    return True


# 208: first instruction after CWallCrawler::Think; 635: before followed-id load;
# 1687: visible-area zero-bounce path, after line 307 has cleared obstruction.
Cases(THINK, 0, 'Think entry', 4, entry, skip=32)
Cases(THINK, 1687, 'Think late obstruction', 1, obstruction)
Cases(THINK, 208, 'Think after base', 7, post_base, skip=64)
Cases(THINK, 635, 'Think electric tail', 2, tail, skip=96)
# 525: before loading the connection; 764: before loading the head-id for line 248.
Cases(ACCEPT, 525, 'AreaLoaded invalid target', 2, connection)
Cases(ACCEPT, 764, 'AreaLoaded head/children', 2, head_children)
Cases(ACCEPT, 832, 'Landed states/bounces', 3, landed)
Cases(COLLIDE, 0, 'Collision inputs', 7, collision)
