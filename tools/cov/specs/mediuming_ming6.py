# MediumIng ming6. force_ret.py; a real initialized actor, tentative state inputs
# are restored after each call. Retain module statics with ming6_keep_static.cmake.
import gdb
import os
import struct

CLASS = 'CMediumIng'
SPEC = []


def ev(expr):
    return gdb.parse_and_eval(expr)


def log(text):
    with open(os.environ['COV_OUT'] + '/ming6.log', 'a') as out:
        out.write(text + '\n')


symbols = []
clocks = []
saved_clocks = []
for line in open(os.environ['COV_NM']):
    raw, _, name = line.rstrip().partition('\t')
    parts = raw.split()
    if len(parts) == 3 and parts[1] in 'tTW' and '.cold' not in name:
        symbols.append((parts[2], name))


def symbol(prefix):
    return next(m for m, d in symbols if d.startswith(prefix))


class Constructed(gdb.FinishBreakpoint):
    def __init__(self, ptr, tentacle, attack_time=None):
        super().__init__(gdb.newest_frame(),internal=True)
        self.ptr,self.tentacle,self.attack_time=ptr,tentacle,attack_time

    def stop(self):
        try:
            cls='CMediumIngTentacle' if self.tentacle else 'CMediumIng'
            obj=ev('((%s*)%d)'%(cls,self.ptr)).dereference()
            if self.tentacle:
                for field in ('mCanDamage','mExtending','mRetracting'):
                    assert int(obj[field]) == 0
                for field in ('mElapsedTime','mRetractTime'):
                    assert float(obj[field]) == 0
                assert float(obj['mLengthScale']) == 1
                assert float(obj['mAttackTime']) == self.attack_time
            else:
                assert int(obj['mMistState']) == 0 and float(obj['mMistAmount']) == 0
                assert bool(obj['mC1']) and bool(obj['mC3']) and not bool(obj['mC0'])
                for field in ('mJumpPointId','mTeamAiMgrId','mSafeZoneId','mLightId','mMistEffectId'):
                    assert int(obj[field]['value']) == 65535
                assert int(obj['mPathFindSearch']['mFlags']) == 0x30b
                assert float(obj['mPathFindSearch']['mChHeight']) == 5
                assert float(obj['mPathFindSearch']['mChRadius']) == 2
            log(cls+' constructor field oracle PASS')
        except Exception as error:
            log('ERROR constructor oracle '+str(error))
        return False


class Constructor(gdb.Breakpoint):
    def __init__(self,sym,tentacle):
        super().__init__('*'+sym,internal=True)
        self.tentacle=tentacle

    def stop(self):
        try:
            attack_time=float(ev('((CMediumIngData*)$r9)->mArmAttackTime')) if self.tentacle else None
            Constructed(int(ev('$rdi')),self.tentacle,attack_time)
        except Exception as error:
            log('ERROR constructor capture '+str(error))
        return False


for sym,name in symbols:
    if 'C2' in sym and name.startswith('CMediumIng::CMediumIng('):
        Constructor(sym,False)
    elif 'C2' in sym and name.startswith('CMediumIngTentacle::CMediumIngTentacle('):
        Constructor(sym,True)


class Loaded(gdb.FinishBreakpoint):
    def __init__(self, stream, end, unknown):
        super().__init__(gdb.newest_frame(),internal=True)
        self.stream,self.end,self.unknown=stream,end,unknown

    def stop(self):
        try:
            assert int(ev('((CInputStream*)%d)->mPtr'%self.stream)) == self.end
            log('loader consumed-length oracle PASS unknown=%s nonnull=%s' %
                (self.unknown,bool(int(self.return_value))))
        except Exception as error:
            log('ERROR loader return '+str(error))
        return False


class Loader(gdb.Breakpoint):
    def stop(self):
        try:
            stream=int(ev('$rsi'))
            obj=ev('((CInputStream*)%d)'%stream).dereference()
            start=int(obj['mPtr'])
            data=bytes(gdb.selected_inferior().read_memory(start,int(obj['mBuffer'])+int(obj['mBufferSize'])-start))
            offset,unknown=2,False
            for _ in range(int.from_bytes(data[:2],'big')):
                prop,size=struct.unpack_from('>IH',data,offset)
                unknown |= prop == 0xfeedbeef
                offset += 6+size
            Loaded(stream,start+offset,unknown)
        except Exception as error:
            log('ERROR loader capture '+str(error))
        return False


Loader('*'+symbol('LoadMediumIng('),internal=True)


class Prepare(gdb.Function):
    def __init__(self):
        super().__init__('m6_prepare')

    def invoke(self, actor, mgr):
        table = ev('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem' % int(mgr)).dereference()['mObjects']
        for i in range(2048):
            obj = table[i]['mEntity']
            if not int(obj):
                continue
            typ = str(obj.dynamic_type)
            if 'CScriptAiJumpPoint' in typ:
                gdb.set_convenience_variable('m6_jp', obj.cast(gdb.lookup_type('CScriptAiJumpPoint').pointer()))
            elif 'CScriptWaypoint' in typ:
                gdb.set_convenience_variable('m6_wp', obj.cast(gdb.lookup_type('CScriptWaypoint').pointer()))
        ids = actor.dereference()['mTentacleIds']
        machine = actor.dereference()['mStateMachine']['mPtr']
        pending, seen = [machine.cast(machine.dynamic_type)], set()
        while pending:
            ptr = pending.pop()
            if int(ptr) in seen:
                continue
            seen.add(int(ptr))
            item = ptr.dereference()
            clocks.append(int(item['mTime'].address))
            if any(f.name == 'mSubMachines' for f in item.type.fields()):
                children = item['mSubMachines']
                for index in range(int(children['mCount'])):
                    pending.append(children['mItems'][index]['mMachine']['mPtr'])
        assert int(ids['mCount']) == 4
        for i in range(4):
            uid = ev('((TUniqueId*)%d)[%d].value' % (int(ids['mData'].address), i))
            ent = table[int(uid) & 2047]['mEntity'].cast(gdb.lookup_type('CMediumIngTentacle').pointer())
            gdb.set_convenience_variable('m6_t%d' % i, ent)
        log('real actor/tentacles/jump/waypoint fixture objects captured')
        return 1


class Expect(gdb.Function):
    def __init__(self):
        super().__init__('m6_expect')

    def invoke(self, value, expected):
        if int(value) != int(expected):
            raise gdb.GdbError('ming6 oracle got%s expected%s' % (value, expected))
        log('oracle PASS')
        return 1


class Time(gdb.Function):
    def __init__(self):
        super().__init__('m6_time')

    def invoke(self, value):
        if float(value) < 0:
            for address, old in saved_clocks:
                gdb.execute('set var *(float*)%d=%r' % (address,old))
            saved_clocks.clear()
        else:
            assert not saved_clocks
            for address in clocks:
                saved_clocks.append((address,float(ev('*(float*)%d'%address))))
                gdb.execute('set var *(float*)%d=%r' % (address,float(value)))
        return 1


Prepare()
Expect()
Time()
scenarios = [([], '$m6_prepare($this,$mgr)'),
             ([], '$m6_expect((void*)$this->GetSearchPath()==(void*)&$this->mPathFindSearch,1)')]


def add(writes, call, expected=None, stubs=None):
    timed = [value for lv,value in writes if lv == '$m6_fixture_time']
    writes = [(lv,value) for lv,value in writes if lv != '$m6_fixture_time']
    if timed:
        call = '($m6_time(%sf),%s,$m6_time(-1.f))' % (repr(float(timed[0])),call)
    if expected is not None:
        call = '$m6_expect((long)(%s),(long)(%s))' % (call, expected)
    scenarios.append((writes, call) if not stubs else (writes, call, stubs))


tid = '((TUniqueId*)$this->mTentacleIds.mData)'
for index in (-1, 0, 1, 4):
    add([], '$this->ExtendTentacle(*$mgr,%d)' % index)
add([(tid + '[0].value', '65535')], '$this->ExtendTentacle(*$mgr,0)')
for fn in ('RetractTentacles', 'PositionTentacles'):
    add([], '$this->%s(*$mgr)' % fn)
    add([(tid + '[0].value', '65535')], '$this->%s(*$mgr)' % fn)
    add([('$this->mTentacleIds.mCount','0')], '$this->%s(*$mgr)' % fn)
for active in (0, 1):
    add([(tid + '[0].value', '65535')], '$this->SetTentaclesActive(*$mgr,%d)' % active)
    add([('$this->mTentacleIds.mCount','0')], '$this->SetTentaclesActive(*$mgr,%d)' % active)

time = '$m6_fixture_time'
body = '$this->mBodyController.mPtr->mBodyStateInfo.mState'
anim = '$this->mAnimationState.mState'
for fn in ('Jump', 'MistIn', 'MistOut'):
    add([], '$this->%s(*$mgr,kStateMsg_Activate,.016f)' % fn)
    add([], '$this->%s(*$mgr,kStateMsg_Deactivate,.016f)' % fn)
for jump in ('65535', '$m6_jp->mUniqueId.value'):
    add([('$this->mJumpPointId.value', jump)], '$this->Jump(*$mgr,kStateMsg_Activate,.016f)')
for inpos in (0, 1):
    add([('$this->mInPosition',str(inpos))], '$this->Jump(*$mgr,kStateMsg_Update,.016f)')
for mode in (0, 1, 2):
    for d0 in (0, 1):
        add([('$this->mData.mSpawnMode',str(mode)), ('$this->mD0',str(d0)),
             (anim,'1'), (body,'0'), ('$this->mFCC','0.f')],
            '$this->MistIn(*$mgr,kStateMsg_Update,.016f)')
for state in (0, 12):
    for t in (0., 3.):
        add([(anim,'0'), (body,str(state)), (time,str(t)), ('$this->mFCC','2.f')],
            '$this->MistIn(*$mgr,kStateMsg_Update,.016f)')
for ready in (0, 1):
    for t in (0., 3.):
        for alpha in (0., 1.):
            add([(anim,str(ready)), (body,'0'), (time,str(t)), ('$this->mFCC',str(alpha))],
                '$this->MistOut(*$mgr,kStateMsg_Update,.016f)')
audio = symbol('(anonymous namespace)::AddLoop(')
add([], '($this->mSfx1=%s((ushort)0x20d6,*$this),$this->mSfx2=%s((ushort)0x20d5,*$this),'
        '$m6_expect($this->mSfx1.mID!=0 && $this->mSfx2.mID!=0,1))' % (audio,audio))
add([], '$this->MistOut(*$mgr,kStateMsg_Deactivate,.016f)')
add([], '$this->MistOut(*$mgr,kStateMsg_Deactivate,.016f)')
add([], '($m6_nan=(float*)malloc(sizeof(float)),*(unsigned int*)$m6_nan=0x7fc00000,1)')
for fn,value in (('MistIn',1),('MistOut',0)):
    add([('$this->mFCC','*$m6_nan'), (anim,'0'), (body,'0')],
        '($this->%s(*$mgr,kStateMsg_Update,.016f),$m6_expect($this->mFCC==%d.f,1))' % (fn,value))
add([], '(void)free($m6_nan)')

# A temporary one-entry real script connection; restore vector metadata per call.
add([], '($m6_conn=(SConnection*)malloc(sizeof(SConnection)),1)')
for fn, state in (('FollowApproachPattern','kSS_Approach'), ('FollowAttackPattern','kSS_GeneratorConnection')):
    add([], '$this->%s(*$mgr,kStateMsg_Update,.016f)' % fn)
    add([('$this->mConnections.mCount','0')], '$this->%s(*$mgr,kStateMsg_Activate,.016f)' % fn)
    add([], '($m6_conn->state=%s,$m6_conn->msg=kSM_Follow,$m6_conn->objId=$m6_wp->mEditorId,1)' % state)
    for side in (-1, 1):
        writes=[('$this->mConnections.mItems','$m6_conn'), ('$this->mConnections.mCount','1'),
                ('$this->mConnections.mCapacity','1'), ('$this->mWaypointNavigation.mInPosition','0')]
        for coord,row in zip('XYZ', ('01','11','21')):
            writes.append(('$m6_wp->mPosition.m'+coord,'$this->mPosition.m%s+%d*$this->mTransform.m%s' % (coord,side,row)))
        add(writes, '($this->%s(*$mgr,kStateMsg_Activate,.016f),$m6_expect($this->mWaypointNavigation.mInPosition,%d))' % (fn,side<0))
add([], '(void)free($m6_conn)')

estimate = symbol('JumpRouteEstimate(')
route = symbol('SearchJumpRoute(')
add([], '($m6_uid=(TUniqueId*)malloc(sizeof(TUniqueId)),$m6_uid->value=65535,1)')
add([], '%s(*$mgr,$P->mPosition,*$m6_uid,$P->mPosition)>3.e38f' % estimate, '1')
add([], '($m6_route=%s($this->mPathFindSearch,*$mgr,$P->mPosition,*$m6_uid,$P->mPosition),$m6_route.mValid)' % route, '0')
add([], '($m6_uid->value=$m6_jp->mUniqueId.value,1)')
for land in ('65535', '$m6_wp->mUniqueId.value'):
    writes=[('$m6_jp->mCurrentWaypoint.value',land)]
    for coord in 'XYZ':
        writes += [('$m6_jp->mPosition.m'+coord,'$P->mPosition.m'+coord),
                   ('$m6_wp->mPosition.m'+coord,'$P->mPosition.m'+coord)]
    add(writes, '%s(*$mgr,$P->mPosition,*$m6_uid,$P->mPosition)' % estimate, '0')
    add(writes, '($m6_route=%s($this->mPathFindSearch,*$mgr,$P->mPosition,*$m6_uid,$P->mPosition),$m6_route.mValid)' % route,
        '1' if land != '65535' else '0')
    if land != '65535':
        add(writes+[('$this->mPathFindSearch.mArea','0')],
            '($m6_route=%s($this->mPathFindSearch,*$mgr,$P->mPosition,*$m6_uid,$P->mPosition),$m6_route.mValid)' % route, '0')
        add([], '($m6_far=(CVector3f*)malloc(sizeof(CVector3f)),$m6_far->mX=1.e6f,$m6_far->mY=1.e6f,$m6_far->mZ=1.e6f,1)')
        add(writes, '($m6_route=%s($this->mPathFindSearch,*$mgr,$P->mPosition,*$m6_uid,*$m6_far),$m6_route.mValid)' % route, '0')
        add([], '(void)free($m6_far)')
add([], '(void)free($m6_uid)')

for fn in ('ArmAttack','MeleeAttack'):
    for team,result in (('65535',None), ('1',0), ('1',1)):
        add([('$this->mTeamAiMgrId.value',team), (anim,'0')],
            '($this->%s(*$mgr,kStateMsg_Activate,.016f),$m6_expect($this->mAnimationState.mState,%d))' %
            (fn, 3 if result == 0 else 1),
            stubs=None if result is None else [('CScriptTeamAiMgr::StartAttack(',result)])
    add([('$this->mTeamAiMgrId.value','65535')], '$this->%s(*$mgr,kStateMsg_Deactivate,.016f)' % fn)
    for mist,ready,state in ((1,0,0), (0,1,0), (0,0,0)):
        add([('$this->mMistAmount',str(mist)), (anim,str(ready)), (body,str(state))],
            '$this->%s(*$mgr,kStateMsg_Update,.016f)' % fn)

for t,counter,timer,radius in ((.2,0,0,1000), (.2,0,1,0), (.2,0,1,1000),
                              (1.,0,1,1000), (1.,4,1,1000), (3.,0,1,1000)):
    add([('$this->mMistAmount','0.f'), (anim,'0'), (body,'9'), (time,str(t)),
         ('$this->mI6C',str(counter)), ('$this->mF70',str(timer)),
         ('$this->mData.mMeleeDamage.mDamageRadius',str(radius))],
        '$this->MeleeAttack(*$mgr,kStateMsg_Update,.016f)')
for t,e0,timer,index,d6,length,missing in ((0,0,0,0,0,1,0), (2,0,1,0,0,1,0),
        (2,1,-1,0,0,1,0), (2,1,-1,5,0,1,0), (2,1,-1,5,1,-1,0), (2,1,-1,5,0,-1,1)):
    writes=[('$this->mMistAmount','0.f'), (anim,'0'), (body,'9'), (time,str(t)),
            ('$this->mE0',str(e0)), ('$this->mFA4',str(timer)),
            ('$this->mTentacleIndex',str(index)), ('$this->mD6',str(d6))]
    for i in range(4):
        writes.append(('$m6_t%d->mLengthScale'%i,str(length)))
    if missing:
        writes.append((tid+'[0].value','65535'))
    add(writes,'$this->ArmAttack(*$mgr,kStateMsg_Update,.016f)')

CALLS = [('ming6 states and helpers', 'CMediumIng::Think(',
          '((CStateManager*)$rsi)->mUpdateFrameIdx>600', scenarios,
          'forced state fields/real tentacles and script connections; no native probes')]
