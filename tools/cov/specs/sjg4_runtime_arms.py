# Last module34 runtime arms. force_ret.py; actual bodies/callees, no return stubs.
import gdb
import os
import re
import struct

CLASS = 'CIngSpaceJumpGuardian'
SPEC = []
MODE = os.environ.get('SJG4_MODE', 'core')
symbols = []
for line in open(os.environ['COV_NM']):
    raw, _, name = line.rstrip().partition('\t')
    parts = raw.split()
    if len(parts) == 3 and parts[1] in 'tTW' and '.cold' not in name:
        symbols.append((parts[2], name))


def symbol(prefix):
    return next(m for m, d in symbols if d.startswith(prefix))


def log(text):
    with open(os.path.join(os.environ['COV_OUT'], 'forced.log'), 'a') as out:
        out.write(text + '\n')


def val(expr):
    return gdb.parse_and_eval(expr)


def assign(expr, value):
    gdb.execute('set var %s=%s' % (expr, value))


class Check(gdb.Function):
    def __init__(self):
        super().__init__('sj4_check')

    def invoke(self, good, which=0):
        label = '3DA8 real emitter handle' if not int(which) else '5760 jump-velocity callee height arm'
        log(label + ' ' + ('PASS' if bool(good) else 'FAIL'))
        if not bool(good):
            raise gdb.GdbError('runtime fixture oracle failed: ' + label)
        return 1


Check()


class Prepare(gdb.Function):
    def __init__(self):
        super().__init__('sj4_prepare')

    def invoke(self, actor, mgr):
        listing = val('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem' % int(mgr))
        found = {}
        for index in range(2048):
            entry = listing.dereference()['mObjects'][index]
            entity = entry['mEntity']
            if int(entity):
                typename = str(entity.dynamic_type).replace(' *', '')
                if typename in ('CScriptWaypoint', 'CScriptAIWaypoint', 'CScriptAiJumpPoint',
                                'CScriptCameraShaker', 'CScriptSafeZone', 'CGameProjectile', 'CEnergyProjectile', 'CPlasmaProjectile'):
                    found.setdefault(typename, []).append(int(entity))
        for name, items in found.items():
            log('inventory %s: %s' % (name, ','.join('%x' % p for p in items)))
        for short, name in (('jp', 'CScriptAiJumpPoint'), ('ai', 'CScriptAIWaypoint'),
                            ('wp', 'CScriptWaypoint'), ('shaker', 'CScriptCameraShaker'),
                            ('zone', 'CScriptSafeZone')):
            address = found.get(name, [0])[0]
            assign('$sj4_' + short, '(%s*)%d' % (name, address))
        assign('$sj4_all', '(CObjectList*)%d' % int(listing))
        weapons = found.get('CEnergyProjectile', []) + found.get('CGameProjectile', []) + found.get('CPlasmaProjectile', [])
        assign('$sj4_weapon', '(CWeapon*)%d' % (weapons[0] if weapons else 0))
        cid = int(actor.dereference()['mCollisionManager']['mPtr'].dereference()['mJointDescriptions']['mItems'][0]['mActorId']['value'])
        gdb.set_convenience_variable('sj4_col', listing.dereference()['mObjects'][cid & 2047]['mEntity'].cast(gdb.lookup_type('CCollisionActor').pointer()))
        assign('$sj4_list', '((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[5].mItem' % int(mgr))
        assign('$sj4_conn', '(SConnection*)malloc(sizeof(SConnection))')
        assign('$sj4_ids', '(TUniqueId*)malloc(64)')
        vec_type = gdb.lookup_type('rstl::vector<TUniqueId, rstl::rmemory_allocator>')
        gdb.set_convenience_variable('sj4_vec', val('(void*)malloc(%d)' % vec_type.sizeof).cast(vec_type.pointer()))
        assign('$sj4_vec->mCount', '0')
        assign('$sj4_vec->mCapacity', '32')
        assign('$sj4_vec->mItems', '$sj4_ids')
        assign('$sj4_msg', '(CScriptMsg*)malloc(sizeof(CScriptMsg))')
        assign('$sj4_msg->mSenderId.value', str(int(actor.dereference()['mUniqueId']['value'])))
        assign('$sj4_msg->m_id.value', str(int(actor.dereference()['mUniqueId']['value'])))
        assign('$sj4_msg->m_originator.value', '65535')
        assign('$sj4_msg->m_state', 'kSS_InvalidState')
        # Version3 name/hash, user-event type, zero time/index, nonunique, weight1,
        # character-1,flags0,value0,root locator. Real stream/node constructors.
        data = struct.pack('>H', 3) + b'fixture\0' + struct.pack('>IHfi', 0, 6, 0., 0)
        data += struct.pack('>iBfiii', 0, 0, 1., -1, 0, 0) + b'head\0'
        assign('$sj4_buf', '(char*)malloc(1024)')
        gdb.selected_inferior().write_memory(int(val('$sj4_buf')), data)
        assign('$sj4_stream', '(CInputStream*)malloc(sizeof(CInputStream))')
        val('((void(*)(void*,void*,unsigned long))%s)($sj4_stream,$sj4_buf,%d)' %
            (symbol('CInputStream::CInputStream(void const*, unsigned long)'), len(data)))
        assign('$sj4_node', '(CInt32POINode*)malloc(sizeof(CInt32POINode))')
        val('((void(*)(void*,void*))%s)($sj4_node,$sj4_stream)' % symbol('CInt32POINode::CInt32POINode(CInputStream&)'))
        log('fixture locator ' + str(val('$sj4_node->mLctrName')))
        if MODE == 'core':
            assembly = gdb.execute('disassemble %s' % symbol('CIngSpaceJumpGuardian::SelectJumpTarget('), to_string=True)
            line = next(line for line in assembly.splitlines() if 'call' in line and 'GetObjectById' in line)
            LookupInput('*' + re.search(r'0x[0-9a-f]+', line).group())
        return 1


Prepare()
scenarios = [([], '$sj4_prepare($this,$mgr)')]


def call(name, args='', writes=()):
    scenarios.append((list(writes), '$this->%s(*$mgr%s)' % (name, ', ' + args if args else '')))


for target in ('65535', '$sj4_ai->mUniqueId.value', '$P->mUniqueId.value'):
    call('FollowIntroPattern', '(EStateMsg)0,0.016f', [('$this->mIntroWaypoint.value', target)])
for sign in (-1, 1):
    writes = [('$this->mIntroWaypoint.value', '$sj4_ai->mUniqueId.value')]
    # Native CActor has a separate mPosition cache; GetTranslation does not read m03/m13/m23.
    for coord, forward in (('X', '01'), ('Y', '11'), ('Z', '21')):
        writes.append(('$this->mPosition.m' + coord,
                       '$sj4_ai->mPosition.m%s + %d*$this->mTransform.m%s' % (coord, sign, forward)))
    call('FollowIntroPattern', '(EStateMsg)0,0.016f', writes)
for msg in (1, 2, 3):
    call('FollowIntroPattern', '(EStateMsg)%d,0.016f' % msg)

# Launching's ComputeJumpVelocity callee: correct the old transform-only height fixture.
for height, expected in ((-5, 42.4264069), (5, 34.6410162)):
    call('ComputeJumpVelocity', writes=[('$this->mJumping', '0'),
         ('$this->mJumpTarget.value', '$P->mUniqueId.value'), ('$this->mJumpApexHeight', '10.f'),
         ('$this->mPosition.mZ', '$P->mPosition.mZ + %d' % height)])
    writes, expr = scenarios[-1]
    scenarios[-1] = (writes, '(' + expr + ',$sj4_check($this->mVelocity.mZ>%f && $this->mVelocity.mZ<%f,1))' %
                       (expected - .001, expected + .001))

for mode in (0, 1):
    call('CameraShake', '$sj4_node->mLctrName', [('$this->mJumpMode', str(mode)),
         ('$sj4_conn->state', 'kSS_Retreat' if mode else 'kSS_Play'),
         ('$sj4_conn->msg', 'kSM_Attach'), ('$sj4_conn->objId.value', '$sj4_shaker->mEditorId.value'),
         ('$this->mConnections.mItems', '$sj4_conn'), ('$this->mConnections.mCount', '1')])
call('CameraShake', '$sj4_node->mLctrName', [('$this->mConnections.mCount', '0')])

for target in ('65535', '$P->mUniqueId.value'):
    call('FindJumpPoints', writes=[('$this->mJumpTarget.value', target)])
connected = [('$this->mJumpTarget.value', '$sj4_wp->mUniqueId.value'),
             ('$sj4_wp->mConnections.mItems', '$sj4_conn'), ('$sj4_wp->mConnections.mCount', '1'),
             ('$sj4_conn->state', 'kSS_Arrived'), ('$sj4_conn->msg', 'kSM_Next'),
             ('$sj4_conn->objId.value', '$sj4_jp->mEditorId.value')]
call('FindJumpPoints', writes=connected)
baseline = [('$this->mJumpTarget.value', '65535'), ('$this->mLastJumpPoint.value', '65535'),
            ('$sj4_jp->mActive', '1'), ('$sj4_jp->mType', '0'), ('$sj4_jp->mInUse', '0'),
            ('$sj4_jp->mOccupant.value', '65535'), ('$sj4_jp->mTimeRemaining', '0'),
            ('$sj4_jp->mAreaId.value', '$this->mAreaId.value')]
for writes in ([], [('$sj4_jp->mActive', '0')], [('$sj4_jp->mInUse', '1')],
               [('$sj4_jp->mType', '1')], [('$this->mLastJumpPoint.value', '$sj4_jp->mUniqueId.value')],
               [('$sj4_jp->mAreaId.value', '-1')], [('$this->mPathFindSearch.mArea', '0')],
               [('$sj4_list->mFirstId', '-1')]):
    call('FindJumpPoints', writes=baseline + writes)
call('FindJumpPoints', writes=baseline + [('$sj4_list->mObjects[$sj4_list->mFirstId].mEntity', '(CEntity*)$P')])

stages = ('ingSpaceJumpGuardianStruct', 'ingSpaceJumpGuardianStruct_0x6b08e2e5',
          'ingSpaceJumpGuardianStruct_0xf223aa76', 'ingSpaceJumpGuardianStruct_0xd0db5f7a')
for count, point, target, chance in ((0, '65535', '65535', 101), (1, '65535', '65535', 101),
    (1, '$sj4_jp->mUniqueId.value', '65535', 101),
    (1, '$sj4_jp->mUniqueId.value', '$sj4_wp->mUniqueId.value', 101),
    (1, '$sj4_jp->mUniqueId.value', '$sj4_ai->mUniqueId.value', 101),
    (1, '$sj4_jp->mUniqueId.value', '$sj4_ai->mUniqueId.value', -1)):
    writes = [('$sj4_vec->mCount', str(count)), ('$sj4_ids[0].value', point),
              ('$sj4_jp->mCurrentWaypoint.value', target), ('$mgr->mSafeZoneManager->mZones.mCount', '0')]
    writes += [('$this->mData.' + stage + '.unknown_0x03698c10', str(chance)) for stage in stages]
    call('FilterJumpPoints', '*$sj4_vec', writes)
safe = [('$sj4_vec->mCount', '1'), ('$sj4_ids[0].value', '$sj4_jp->mUniqueId.value'),
        ('$sj4_jp->mCurrentWaypoint.value', '$sj4_wp->mUniqueId.value'),
        ('$mgr->mSafeZoneManager->mZones.mCount', '1'),
        ('((CSafeZoneManager::SZone*)$mgr->mSafeZoneManager->mZones.mData)[0].mId.value', '$sj4_zone->mUniqueId.value')]
safe += [('$sj4_wp->mPosition.m' + coord, '$sj4_zone->mPosition.m' + coord) for coord in 'XYZ']
call('FilterJumpPoints', '*$sj4_vec', safe)

call('SelectJumpTarget', '0.016f', [('$this->mJumpTarget.value', '65535'), ('$sj4_list->mFirstId', '-1')])
call('SelectJumpTarget', '0.016f', connected)
call('SelectJumpTarget', '0.016f', connected)

for target in ('$sj4_ai->mUniqueId.value', '$P->mUniqueId.value'):
    call('Jump', '(EStateMsg)0,0.016f', [('$this->mJumpTarget.value', target)])
for target, mode, last in (('65535', 0, 0), ('$P->mUniqueId.value', 0, 0),
                          ('$sj4_ai->mUniqueId.value', 1, 0), ('$sj4_ai->mUniqueId.value', 1, 1)):
    call('Jump', '(EStateMsg)1,0.016f', [('$this->mAnimationState.mState', '1'),
         ('$this->mBodyController.mPtr->mBodyStateInfo.mState', 'pas::kAS_Locomotion'),
         ('$this->mJumpTarget.value', target), ('$this->mJumpMode', str(mode)),
         ('$this->mLastJumpToWaypoint', str(last))])
for jumping, timer, duration, shield, handle in ((0, 0, 2, 1, 0), (1, 0, 2, 1, 0),
                                               (1, 2, 2, 0, 0), (1, 2, 2, 0, 1)):
    call('Jump', '(EStateMsg)1,0.016f', [('$this->mAnimationState.mState', '0'),
         ('$this->mJumping', str(jumping)), ('$this->mJumpTimer', str(timer)),
         ('$this->mJumpDuration', str(duration)), ('$this->mShieldTimer', str(shield)),
         ('$this->mJumpSfx.mID', '$sj4_handle' if handle else '0')])
    if jumping and timer == 2 and handle == 0:
        writes, expr = scenarios[-1]
        scenarios[-1] = (writes, '(' + expr + ',$sj4_handle=$this->mJumpSfx.mID,$sj4_check($sj4_handle!=0))')
for mode in (0, 1):
    call('Jump', '(EStateMsg)2,0.016f', [('$this->mJumpMode', str(mode))])
call('Jump', '(EStateMsg)3,0.016f')

for event in ('kUE_BreakLockOn', 'kUE_DamageOff', 'kUE_DamageOn', 'kUE_ScreenShake',
              'kUE_EventStart', 'kUE_EffectOn', 'kUE_EffectOff', 'kUE_Activate'):
    call('DoUserAnimEvent', '*$sj4_node,%s,0.016f' % event)
call('DoUserAnimEvent', '*$sj4_node,kUE_BreakLockOn,0.016f', [('$this->mJumpMode', '-1')])
call('DoUserAnimEvent', '*$sj4_node,(EUserEventType)100,0.016f')
for present, index, count in ((0, 0, 1), (1, -1, 1), (1, 1, 1), (1, 0, 1)):
    call('DoUserAnimEvent', '*$sj4_node,kUE_Projectile,0.016f',
         [('$this->mMiniPortalEffect.m_valid', str(present)),
          ('$this->mPortalIndex', str(index)), ('$this->mPortalCount', str(count)),
          ('$this->mPlayerId.value', '$P->mUniqueId.value')])

for message in ('kSM_Activate', 'kSM_Deactivate', 'kSM_AreaLoaded', 'kSM_OffGround',
                'kSM_Launching', 'kSM_Landed', 'kSM_Damage', 'kSM_ResistedDamage',
                'kSM_HitObject', 'kSM_None'):
    call('AcceptScriptMsg', '*$sj4_msg', [('$sj4_msg->m_msg', message)])
for message in ('kSM_Activate', 'kSM_Deactivate'):
    call('AcceptScriptMsg', '*$sj4_msg', [('$sj4_msg->m_msg', message),
         ('$this->mLightId.value', '65535')])
call('AcceptScriptMsg', '*$sj4_msg', [('$sj4_msg->m_msg', 'kSM_Activate')])

# Simulate stale candidate lookup input once, after real Find/Filter/RNG ran.
# The real manager lookup returns null; no return value or callee is stubbed.
class LookupInput(gdb.Breakpoint):
    def __init__(self, where):
        super().__init__(where, internal=True)
        self.done = False

    def stop(self):
        if not self.done:
            gdb.execute('set $esi=65535')
            self.done = True
            log('34CC forced stale lookup input after real Find/Filter/RNG')
        return False

damage = []
for alive, sender, touched, hp, damageable, last in (
        (0, '$sj4_col->mUniqueId', '$sj4_weapon->mUniqueId.value', 100, 0, 0),
        (1, '$P->mUniqueId', '$sj4_weapon->mUniqueId.value', 100, 0, 0),
        (1, '$sj4_col->mUniqueId', '$P->mUniqueId.value', 100, 0, 0),
        (1, '$sj4_col->mUniqueId', '$sj4_weapon->mUniqueId.value', 100, 0, 0),
        (1, '$sj4_col->mUniqueId', '$sj4_weapon->mUniqueId.value', 1, 0, 0),
        (1, '$sj4_col->mUniqueId', '$sj4_weapon->mUniqueId.value', 1, 1, 0),
        (1, '$sj4_col->mUniqueId', '$sj4_weapon->mUniqueId.value', 1, 1, 1),
        (1, '$sj4_col->mUniqueId', '$sj4_weapon->mUniqueId.value', 0, 0, 0)):
    writes = [('$this->mAlive', str(alive)), ('$this->mHealthInfo.mHealth', str(hp)),
              ('$this->mDamageable', str(damageable)), ('$this->mLastJumpToWaypoint', str(last)),
              ('$sj4_col->mHealthInfo.mHealth', '$this->mHealthInfo.mInitialHealth-10'),
              ('$sj4_col->mLastTouched.value', touched)]
    damage.append((writes, '$this->CollisionDamage(*$mgr,%s)' % sender))
for boss in ('65535', '$this->mUniqueId.value'):
    damage.append(([('$mgr->mBossId.value', boss), ('$sj4_msg->m_msg', 'kSM_Delete')],
                   '$this->AcceptScriptMsg(*$mgr,*$sj4_msg)'))
core_scenarios = scenarios
damage.append(([], '($this->mAlive=0,$this->mActive=0,$mgr->DeleteObjectRequest($this->mUniqueId),1)'))
gdb.execute('set $sj4_ready=0')


class Ready(gdb.Breakpoint):
    def __init__(self):
        super().__init__('*' + symbol('CIngSpaceJumpGuardian::Think('), internal=True)
        self.n = 0

    def stop(self):
        if int(val('$sj4_ready')) or not int(val('((CIngSpaceJumpGuardian*)$rdi)->mActive')):
            return False
        self.n += 1
        if self.n % 8:
            return False
        table = val('((rstl::auto_ptr<CObjectList>*)((CStateManager*)$rsi)->mObjectLists.mData)[0].mItem').dereference()['mObjects']
        for index in range(2048):
            entity = table[index]['mEntity']
            if int(entity) and str(entity.dynamic_type).replace(' *', '') in ('CGameProjectile', 'CEnergyProjectile', 'CPlasmaProjectile'):
                assign('$sj4_ready', '1')
                log('suite ready with live ' + str(entity.dynamic_type))
                break
        return False


if MODE == 'damage':
    Ready()
    scenarios = [([], '$sj4_prepare($this,$mgr)')] + damage
    when = '$sj4_ready'
else:
    scenarios = core_scenarios
    when = '((CIngSpaceJumpGuardian*)$rdi)->mActive'
CALLS = [('sjg4 ' + MODE, 'CIngSpaceJumpGuardian::Think(', when, scenarios,
          'actual runtime fixtures; damage mode owns and queues actor cleanup')]

expected_launch = None


class LaunchInput(gdb.Breakpoint):
    def stop(self):
        global expected_launch
        msg = val('((CScriptMsg*)$rdx)').dereference()
        if int(msg['m_msg']) == int(val('kSM_Launching')):
            expected_launch = (int(val('$rdi')), int(msg['mSenderId']['value']))
        return False


class LaunchBase(gdb.Breakpoint):
    def stop(self):
        global expected_launch
        msg = val('((CScriptMsg*)$rdx)').dereference()
        if expected_launch and int(val('$rdi')) == expected_launch[0] and int(msg['m_msg']) == int(val('kSM_OffGround')):
            good = int(msg['mSenderId']['value']) == expected_launch[1]
            uid = int(val('((CIngSpaceJumpGuardian*)$rdi)->mUniqueId.value'))
            log('5760 Launching sender oracle %s sender=%04x actor=%04x' % ('PASS' if good else 'FAIL', expected_launch[1], uid))
            expected_launch = None
            if not good:
                raise gdb.GdbError('synthetic OffGround changed sender')
        return False


LaunchInput('*' + symbol('CIngSpaceJumpGuardian::AcceptScriptMsg('), internal=True)
LaunchBase('*' + symbol('CPatterned::AcceptScriptMsg('), internal=True)
