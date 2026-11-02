# SpankWeed: real shipped Wisp/Pirate donor models, constructor and message fixtures.
# force_ret.py; coverage config spankweed_spw1_keep_inline.cmake.
import gdb
import os
import math

CLASS='CSpankWeed'
SPEC=[]


def ev(expr):
    return gdb.parse_and_eval(expr)


def log(text):
    with open(os.environ['COV_OUT']+'/spw1.log','a') as out:
        out.write(text+'\n')


syms=[]
for line in open(os.environ['COV_NM']):
    raw,_,name=line.rstrip().partition('\t')
    parts=raw.split()
    if len(parts)==3 and parts[1] in 'tTW' and '.cold' not in name:
        syms.append((parts[2],name))


def symbol(prefix,part=None):
    return next(m for m,d in syms if d.startswith(prefix) and (part is None or part in m))


class Constructed(gdb.FinishBreakpoint):
    def __init__(self,ptr,model,old,desired,which):
        super().__init__(gdb.newest_frame(),internal=True)
        self.ptr,self.model,self.old,self.desired,self.which=ptr,model,old,desired,which

    def stop(self):
        try:
            obj=ev('((CSpankWeed*)%d)'%self.ptr).dereference()
            scale=obj['mModelData']['mPtr'].dereference()['mScale']
            expected=math.sqrt(sum(v*v for v in self.desired)/3.)
            for c in 'XYZ':
                assert abs(float(scale['m'+c])-expected)<1.e-5
            assert int(obj['mState'])==-1 and int(obj['mPreviousState'])==-1
            assert bool(obj['mIsHiding']) and not bool(obj['mCanKnockBack'])
            if self.which in (2,3):
                for c in 'XYZ':
                    delta=float(obj['mLockonTarget']['m'+c])-float(obj['mPosition']['m'+c])-float(obj['mLockonOffset']['m'+c])
                    assert abs(delta)<1.e-4
            elif self.which==4:
                assert all(float(obj['mLockonTarget']['m'+c])==0. and
                           float(obj['mLockonOffset']['m'+c])==0. for c in 'XYZ')
            if self.which==2:
                gdb.set_convenience_variable('s1_actor',ev('((CSpankWeed*)%d)'%self.ptr))
            elif self.which in (1,4):
                gdb.set_convenience_variable('s1_fallback',ev('((CSpankWeed*)%d)'%self.ptr))
                if self.which==4:
                    gdb.set_convenience_variable('s1_fallback_ready',gdb.Value(1))
            log('actor ctor%d scale/lockon/default oracle PASS'%self.which)
        except Exception as error:
            log('ERROR actor ctor '+str(error))
        for c,value in zip('XYZ',self.old):
            gdb.execute('set var ((CModelData*)%d)->mScale.m%s=%r'%(self.model,c,value))
        return False


class Constructor(gdb.Breakpoint):
    def __init__(self,where):
        super().__init__(where,internal=True)
        self.n=0

    def stop(self):
        self.n+=1
        try:
            model=int(ev('$r9'))
            old=[float(ev('((CModelData*)%d)->mScale.m%s'%(model,c))) for c in 'XYZ']
            desired=(1.,2.,3.) if self.n==2 else (1.,1.,2.) if self.n==3 else tuple(old)
            for c,v in zip('XYZ',desired):
                gdb.execute('set var ((CModelData*)%d)->mScale.m%s=%r'%(model,c,v))
            Constructed(int(ev('$rdi')),model,old,desired,self.n)
        except Exception as error:
            log('ERROR ctor capture '+str(error))
        return False


Constructor('*'+symbol('CSpankWeed::CSpankWeed(','C2'))
gdb.set_convenience_variable('s1_actor',gdb.Value(0))
gdb.set_convenience_variable('s1_fallback_ready',gdb.Value(0))


class Prepare(gdb.Function):
    def __init__(self):
        super().__init__('s1_prepare')

    def invoke(self,actor,mgr):
        assert int(ev('$s1_fallback_ready'))==1
        primitive=actor.dereference()['mModelData']['mPtr'].dereference()['mAnimData']['mItem'].dereference()['mSpatialPrimitive']
        assert bool(primitive['m_valid'])
        token=primitive['m_data'].address.cast(gdb.lookup_type('TLockedToken<CSpatialPrimitive>').pointer()).dereference()
        assert int(token['mItem'].dereference()['mBoxes']['mCount'])==0
        log('native Wisp CSPP box count zero, matching disc scan')
        table=ev('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem'%int(mgr)).dereference()['mObjects']
        found=False
        for i in range(2048):
            obj=table[i]['mEntity']
            if not int(obj):
                continue
            typ=str(obj.dynamic_type)
            if 'CCollisionActor' in typ:
                gdb.set_convenience_variable('s1_col',obj.cast(gdb.lookup_type('CCollisionActor').pointer()))
                found=True
            elif 'CSpankWeed' in typ and int(obj)!=int(actor):
                gdb.set_convenience_variable('s1_other',obj.cast(gdb.lookup_type('CSpankWeed').pointer()))
        assert found
        gdb.set_convenience_variable('s1_active',actor.dereference()['mActive'])
        log('real donor actor and collision sender captured')
        return 1


class Expect(gdb.Function):
    def __init__(self):
        super().__init__('s1_expect')

    def invoke(self,value):
        if not bool(value):
            raise gdb.GdbError('SpankWeed oracle failed')
        log('oracle PASS')
        return 1


class Defaults(gdb.Function):
    def __init__(self):
        super().__init__('s1_defaults')

    def invoke(self,ptr):
        data=ptr.dereference()
        for name,value in (('wakeUpRadius',60.),('searchRadius',30.),('attackRadius',15.),('hurtSleepDelay',5.)):
            assert float(data[name])==value
        assert bool(data['unknown_0x5cdc877d'])
        pat=data['patterned']
        assert int(pat['contactDamage']['dI_WeaponType'])==9
        assert float(pat['contactDamage']['dI_Damage'])==20.
        assert float(pat['contactDamage']['dI_KnockBackPower'])==1.
        assert float(pat['damageWaitTime'])==1.
        assert float(pat['health']['health'])==1000000.
        assert float(pat['health']['hI_KnockBackResistance'])==2.
        light=data['actorInformation']['lighting']['ambientColor']
        assert all(int(light['m'+c])==255 for c in 'RGBA')
        assert int(data['actorInformation']['visor']['visorFlags'])==15
        log('B90 standalone SLdr default oracle PASS')
        return 1


Prepare();Expect();Defaults()
ctor=symbol('SLdrSpankWeed::SLdrSpankWeed(')
dtor=symbol('SLdrSpankWeed::~SLdrSpankWeed(').replace('D1','D2')
scenarios=[([], '$s1_prepare($this,$mgr)'),
    ([], '($s1_data=(SLdrSpankWeed*)malloc(sizeof(SLdrSpankWeed)),((void(*)(void*))%s)($s1_data),$s1_defaults($s1_data),((void(*)(void*))%s)($s1_data),(void)free($s1_data),1)'%(ctor,dtor)),
    ([], '($s1_msg=(CScriptMsg*)malloc(sizeof(CScriptMsg)),$s1_msg->m_originator.value=65535,$s1_msg->m_state=kSS_InvalidState,1)')]


def add(writes,call):
    scenarios.append((writes,call))


def msg(kind,writes=(),sender='65535',oracle=None,actor='$this'):
    call='($s1_msg->m_msg=%s,$s1_msg->mSenderId.value=%s,$s1_msg->m_id=%s->mUniqueId,%s->AcceptScriptMsg(*$mgr,*$s1_msg)'%(kind,sender,actor,actor)
    if oracle:
        call+=',$s1_expect(%s)'%oracle
    add(list(writes),call+',1)')


for dt in (0.,.1):
    add([], '$this->GetAimPosition(*$mgr,%sf)'%repr(dt))
    add([], '$s1_fallback->GetAimPosition(*$mgr,%sf)'%repr(dt))
msg('kSM_Create')
msg('kSM_Create',[('$this->mModelData.mPtr->mAnimData.mItem','0')])
msg('kSM_Create',[('$this->mModelData.mPtr->mAnimData.mItem->mSpatialPrimitive.m_valid','0')])
for kind in ('kSM_HitObject','kSM_None','kSM_AIUpdateDisabled','kSM_Decrement'):
    msg(kind)
msg('kSM_AIUpdateDisabled',[('$this->mCollisionMgr.mPtr','0')])
for touched,timer,state in (('65535',0,-1),('$P->mUniqueId.value',1,-1),
    ('$P->mUniqueId.value',0,4),('$P->mUniqueId.value',0,6),('$P->mUniqueId.value',0,-1)):
    msg('kSM_HitObject',[('$s1_col->mLastTouched.value',touched),('$this->mCurDamageRemTime',str(timer)),
                       ('$this->mState',str(state))], '$s1_col->mUniqueId.value',
        '$this->mCurDamageRemTime==$this->mDamageWaitTime' if touched!='65535' and not timer and state==-1 else None)
for state in (-1,0,5,6,4):
    msg('kSM_Decrement',[('$this->mState',str(state)),('$this->mHitByPlayerProjectile','0')],
        oracle='$this->mHitByPlayerProjectile==%d'%(state==-1))
msg('kSM_Activate',[('$this->mActive','0'),('$this->mActorLights.mPtr','0'),('$this->mCollisionMgr.mPtr','0')])
msg('kSM_Activate',[('$this->mActive','0')])
msg('kSM_Deactivate')
msg('kSM_Activate')
msg('kSM_Delete',actor='$s1_other')
add([], '($this->mActive=$s1_active,$this->mCollisionMgr.mPtr->SetActive(*$mgr,$s1_active),(void)free($s1_msg),1)')

CALLS=[('spw1 actor queue','CSpankWeed::Think(',
    '((CStateManager*)$rsi)->mUpdateFrameIdx>700 && $rdi==(unsigned long)$s1_actor',scenarios,
    'real donor models, stored scale variants and crafted messages')]
