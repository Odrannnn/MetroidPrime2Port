# Blogg7: real collision actors/messages and a constructor-backed mega phase list.
# Generic force_ret.py drives calls; only CPatterned::Think is held at its call
# boundary during direct Think scenarios, to keep the explicitly supplied state.
import os
import gdb

here = os.path.dirname(os.environ['COV_SPEC'])
exec(compile(open(os.path.join(here, 'blogg_force4.py')).read(), 'blogg_force4.py', 'exec'))
CLASS = 'CBlogg'
SPEC = []
LOG = os.environ['COV_OUT'] + '/blogg7.log'


class MegaCreate(gdb.Breakpoint):
    def stop(self):
        try:
            frame=gdb.newest_frame()
            while frame is not None:
                if frame.type()==gdb.DUMMY_FRAME:
                    return False
                frame=frame.older()
            obj=int(ev('$rdi'))
            if int(ev('((CScriptMsg*)$rdx)->m_msg'))==int(ev('kSM_Create')) and int(ev('((CBlogg*)%d)->mPhases.mCount'%obj))==3:
                old=int(ev('((CBlogg*)%d)->mIsMegaBlogg'%obj))
                gdb.execute('set var ((CBlogg*)%d)->mIsMegaBlogg=1'%obj)
                Done(lambda: gdb.execute('set var ((CBlogg*)%d)->mIsMegaBlogg=%d'%(obj,old)))
        except Exception as error:
            log('ERROR mega Create '+str(error))
        return False


for where in addrs('CBlogg::AcceptScriptMsg('):
    MegaCreate(where,internal=True)


class Prepare(gdb.Function):
    def __init__(self):
        super().__init__('b7_prepare')

    def invoke(self, actor, mgr):
        table = ev('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem' % int(mgr)).dereference()['mObjects']
        mouth_id = int(actor.dereference()['xac4_']['value'])
        col = table[mouth_id & 2047]['mEntity'].cast(gdb.lookup_type('CCollisionActor').pointer())
        assert mouth_id != 65535 and int(col)
        gdb.set_convenience_variable('b7_col',col)
        for i in range(2048):
            obj = table[i]['mEntity']
            if not int(obj):
                continue
            typ = str(obj.dynamic_type)
            if 'CBlogg' in typ and 'Projectile' not in typ and int(obj)!=int(actor):
                gdb.set_convenience_variable('b7_other',obj.cast(gdb.lookup_type('CBlogg').pointer()))
            if 'CPowerBomb' in typ:
                gdb.set_convenience_variable('b7_weapon',obj.cast(gdb.lookup_type('CWeapon').pointer()))
        log('prepared actor uid=%04x phases=%d mouth=%04x' %
            (int(actor.dereference()['mUniqueId']['value']),int(actor.dereference()['mPhases']['mCount']),mouth_id))
        return 1


class Expect(gdb.Function):
    def __init__(self):
        super().__init__('b7_expect')

    def invoke(self, value, expected):
        if int(value)!=int(expected):
            raise gdb.GdbError('Blogg oracle got%s expected%s'%(value,expected))
        log('oracle PASS')
        return 1


Prepare()
Expect()
scenarios=[([], '$b7_prepare($this,$mgr)')]


def add(writes, call, stubs=None):
    scenarios.append((writes,call) if stubs is None else (writes,call,stubs))


common=[('$this->mActive','1'),('$this->mTeamManagerId.value','65535'),
        ('$this->mPlayerId.value','$P->mUniqueId.value'),('$this->mMouthClosed','1'),
        ('$this->mBallGrabbed','0'),('$this->mCanBite','1'),('$this->xb68_','0.f'),
        ('$this->mIsMegaBlogg','0')]
hold_base=[('CPatterned::Think(',0)]


def think(extra=(), oracle=None):
    expr='$this->Think(.016f,*$mgr)'
    if oracle:
        expr='(%s,$b7_expect(%s,1))'%(expr,oracle)
    add(common+list(extra),expr,hold_base)


think([('$this->mActive','0')])
for state in ('Patrol','MoveToAttackPosition','MoveToValidPosition','FacePlayer',
              'ProjectileAttack','ChargeAttack','Dying','GrabBall','Thrash'):
    think([('$this->mState','CBlogg::kBS_'+state)],
          '$this->mSpeed==0.f' if state=='Dying' else None)
think([('$this->mTeamManagerId.value','$P->mUniqueId.value')])
think([('$this->mPlayerId.value','65535')])
for inrange,closed,flag in ((0,1,1),(1,0,1),(1,1,0),(1,1,1)):
    think([('$this->mState','CBlogg::kBS_ChargeAttack'),('$this->mCurrentAttackRange',str(10000*inrange)),
           ('$this->mUnknown_0x689a803f','1.f'),('$this->mUnknown_0x479ccc37','1.f'),
           ('$this->mMouthClosed',str(closed)),('$this->xbc5_28_',str(flag))],
          '$this->mMouthClosed==0' if inrange and closed and flag else None)
for spawned,morph in ((0,0),(0,1),(1,1)):
    think([('$this->mState','CBlogg::kBS_MoveToPlayer'),('$P->mSpawnedMorphBallState',str(spawned)),
           ('$P->mMorphBallState',str(morph))])
think([('$this->mState','CBlogg::kBS_Patrol'),('$this->mMouthClosed','0')])
pos=[('$P->mPosition.m'+c,'$P->mPosition.m'+c) for c in 'XYZ']
pos += [('$P->mTransform.m'+r,'$P->mTransform.m'+r) for r in ('03','13','23')]
think([('$this->mBallGrabbed','1')]+pos)
for timer in (-1,100):
    think([('$this->mCanBite','0'),('$this->mMeleeDelayTimer',str(timer))])
think([('$this->xb68_','.2f')])
for phase,threshold,count,last in ((0,0,0,0),(0,2,0,0),(0,2,3,0),(1,0,0,0),
                                   (1,0,3,0),(0,2,0,1)):
    think([('$this->mIsMegaBlogg','1'),('$this->mHealthInfo.mHealth','$this->mHealthInfo.mInitialHealth'),
           ('$this->xbb2_',str(last)),('$this->xbc5_30_',str(phase)),
           ('$this->mPhaseValue',str(threshold)),('$this->xbb1_',str(count))])

# Create a genuine player power bomb while the game is stopped, then rediscover
# its registered object (no vtable or type-tag fabrication).
add([], '($b7_pb=$P->mGun.mPtr->CreatePowerBomb(*$mgr),1)')
add([], '$b7_prepare($this,$mgr)')
add([], '($b7_msg=(CScriptMsg*)malloc(sizeof(CScriptMsg)),$b7_msg->mSenderId.value=65535,'
        '$b7_msg->m_originator.value=65535,$b7_msg->m_id=$this->mUniqueId,'
        '$b7_msg->m_msg=kSM_None,$b7_msg->m_state=kSS_InvalidState,1)')


def msg(kind,writes=(),sender='65535',oracle=None,stubs=None,actor='$this'):
    expr='($b7_msg->m_msg=%s,$b7_msg->mSenderId.value=%s,$b7_msg->m_id=%s->mUniqueId,%s->AcceptScriptMsg(*$mgr,*$b7_msg)'%(kind,sender,actor,actor)
    if oracle:
        expr+=',$b7_expect(%s,1)'%oracle
    expr+=',1)'
    add(list(writes),expr,stubs)


for kind in ('kSM_AreaLoaded','kSM_AIUpdateDisabled','kSM_Alert','kSM_HitObject',
             'kSM_Damage','kSM_ResistedDamage','kSM_InternalMessage1','kSM_Increment','kSM_Decrement','kSM_None'):
    msg(kind)
msg('kSM_Create',[('$this->mModelData.mPtr','0')],
    stubs=[('CPatterned::AcceptScriptMsg(',0)])
msg('kSM_Create',[('$this->mModelData.mPtr->mAnimData.mItem','0')],
    stubs=[('CPatterned::AcceptScriptMsg(',0)])
msg('kSM_Create',[('$this->mModelData.mPtr->mAnimData.mItem->mSpatialPrimitive.m_valid','0')],
    stubs=[('CPatterned::AcceptScriptMsg(',0)])
msg('kSM_AIUpdateDisabled',[('$this->mCollisionActorManager.mPtr','0')])
msg('kSM_Activate',[('$this->mActive','0'),('$this->mCollisionActorManager.mPtr','0')])
for touched in ('65535','$P->mUniqueId.value'):
    msg('kSM_HitObject',[('$b7_col->mLastTouched.value',touched)],'$b7_col->mUniqueId.value')
for state in ('ChargeAttack','MeleeAttack','Patrol'):
    msg('kSM_HitObject',[('$this->mState','CBlogg::kBS_'+state),('$this->mChargeOver','0'),
                        ('$b7_col->mLastTouched.value','$P->mUniqueId.value')],
        '$this->xac4_.value','$this->mChargeOver' if state=='ChargeAttack' else None)
msg('kSM_HitObject',[('$this->xac4_.value','65535'),('$b7_col->mLastTouched.value','$P->mUniqueId.value')],
    '$b7_col->mUniqueId.value')
for closed in (0,1):
    for touched in ('65535','$P->mUniqueId.value'):
        msg('kSM_Damage',[('$this->mMouthClosed',str(closed)),('$b7_col->mLastTouched.value',touched)],
            '$b7_col->mUniqueId.value')
for mouth,closed,gate,direction in ((1,0,1,1),(1,1,1,1),(1,0,0,1),
                                   (0,0,0,1),(0,0,0,-1),(1,0,0,-1)):
    writes=[('$b7_col->mLastTouched.value','$b7_weapon->mUniqueId.value'),
            ('$this->mMouthClosed',str(closed)),('$this->mHitByPlayerProjectile','0'),
            ('$b7_col->mHealthInfo.mHealth','$this->mHealthInfo.mInitialHealth-2.f'),
            ('$this->mHealthInfo.mHealth','$this->mHealthInfo.mInitialHealth'),
            ('$this->xac4_.value','$b7_col->mUniqueId.value' if mouth else '65535')]
    for row,col in zip(('01','11','21'),('02','12','22')):
        writes.append(('$b7_weapon->mTransform.m'+row,'%d*$this->mTransform.m%s'%(direction,col)))
    msg('kSM_Damage',writes,'$b7_col->mUniqueId.value',
        '$this->mHitByPlayerProjectile==%d'%bool((mouth and not closed and gate) or direction>0),
        [('CBlogg::IsHitInMouthDirection(',gate)])
for touched in ('65535','$b7_weapon->mUniqueId.value'):
    msg('kSM_ResistedDamage',[('$b7_col->mLastTouched.value',touched)],'$b7_col->mUniqueId.value')
normal = next(m for m,d in syms if d.startswith('CDamageVulnerability::NormalVulnerabilty('))
add([], '($b7_saved_vuln=$b7_col->mDamageVulnerability,$b7_col->mDamageVulnerability=%s(),1)'%normal)
for damage in (0,10):
    msg('kSM_ResistedDamage',[('$b7_col->mLastTouched.value','$b7_weapon->mUniqueId.value'),
         ('$b7_col->mNonUniformVulnerability.mPtr','0'),('$b7_weapon->mCurDamageInfo.mDamage',str(damage)),
         ('$this->xb68_','0.f'),('$this->xbc5_26_','0')], '$b7_col->mUniqueId.value',
        '$this->xbc5_26_' if damage else '!$this->xbc5_26_')
add([], '($b7_col->mDamageVulnerability=$b7_saved_vuln,1)')
for kind in ('kSM_Deactivate','kSM_Activate'):
    msg(kind)
# A non-current object can safely receive deletion while this actor's Think is
# suspended; never resume a deleted current frame.
msg('kSM_InternalMessage0',actor='$b7_other')
msg('kSM_Delete',actor='$b7_other')
add([], '(void)free($b7_msg)')
CALLS=[('blogg7 queue','CBlogg::Think(',
        '((CStateManager*)$rsi)->mUpdateFrameIdx>700 && ((CBlogg*)$rdi)->mPhases.mCount==3',
        scenarios,'Think state matrix and crafted-message queue with real senders')]
