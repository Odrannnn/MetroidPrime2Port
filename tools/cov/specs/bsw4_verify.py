"""bsw4: forced native calls on live actors, never production probes.

Default force.py, not force_ret.py. The batch hook executes outside stop().
Every mutation is inside an already-armed Transaction, with original bytes saved
first; __exit__ restores on normal return, failed oracle and inferior-call unwind.
"""
import gdb
import hashlib
import json
import math
import os
import struct

CLASS = 'CBacteriaSwarm'
FORCE = []
state = {'pending': None, 'done': False, 'scenario': None, 'sound': [],
         'hardware': [], 'expect_sound': None, 'expect_bounds': None}

def log(event, **data):
    with open(os.path.join(os.environ['COV_OUT'], 'bsw4.jsonl'), 'a') as f:
        f.write(json.dumps(dict(event=event, **data), sort_keys=True) + '\n')

def ev(s):
    return gdb.parse_and_eval(s)

def iv(s):
    return int(ev(s))

def check(value, label):
    if not value:
        log('FAIL', label=label)
        raise gdb.GdbError(label)

class Transaction:
    def __init__(self, label, lvalues):
        self.label = label
        self.saved = []
        inf = gdb.selected_inferior()
        for lv in lvalues:
            value = ev(lv)
            check(value.address is not None, 'addressable snapshot: ' + lv)
            address, size = int(value.address), value.type.sizeof
            self.saved.append((lv, address, bytes(inf.read_memory(address, size))))

    def __enter__(self):
        log('ARM', label=self.label, fields=[dict(lvalue=lv, address=a, original=b.hex())
                                           for lv,a,b in self.saved])
        return self

    def __exit__(self, typ, value, tb):
        # A finally-style context is armed before callers perform their first write.
        inf = gdb.selected_inferior()
        errors, rows = [], []
        for lv, address, original in reversed(self.saved):
            try:
                inf.write_memory(address, original)
                actual = bytes(inf.read_memory(address, len(original)))
                rows.append(dict(lvalue=lv, address=address, original=original.hex(),
                                 readback=actual.hex()))
                if actual != original:
                    errors.append(lv)
            except Exception as error:
                errors.append(lv + ': ' + str(error))
        log('RESTORE', label=self.label, fields=rows, errors=errors,
            exception=None if typ is None else typ.__name__)
        check(not errors, 'restore mismatch: ' + str(errors))
        return False

def write(lv, rhs):
    gdb.execute('set var %s = %s' % (lv, rhs), to_string=True)

def replay_graphics():
    for i in range(8):
        gdb.execute('call GXLoadLightObjImm(&CGraphics::mLightObj[%d],(GXLightID)%d)'%(i,1<<i),to_string=True)
    for i in range(2):
        gdb.execute('call GXSetChanAmbColor((GXChannelID)%d,CGX::gpGXState->mChanAmbColors[%d])'%(4+i,i),to_string=True)
    log('GX_REPLAY',lights=8,ambient_channels=2)

class GraphicsTransaction(Transaction):
    def __exit__(self,typ,value,tb):
        try:
            return super().__exit__(typ,value,tb)
        finally:
            replay_graphics()

def release_sounds(before):
    for record in state['sound'][before:]:
        handle=record.get('handle',0)
        if handle:
            soundptr='((CSfxManager::CBaseSfxWrapper**)CSfxManager::mChannels[CSfxManager::mCurrentChannel].mSounds.mData)[%d]'%(handle&0xfff)
            check(iv(soundptr+'->mHandle.mID')==handle,'native emitter handle identity')
            gdb.execute('call CSfxManager::RemoveEmitter('+soundptr+'->mHandle)',to_string=True)
            log('SOUND_RELEASE',handle=handle)

def vector(value):
    return tuple(float(value['m' + axis]) for axis in 'XYZ')

def near(a, b):
    return all(abs(x-y) <= 0.0003 * max(1.,abs(y)) for x,y in zip(a,b))

def symbol(prefix):
    matches = []
    for line in open(os.environ['COV_NM']):
        raw, dem = line.rstrip('\n').split('\t')
        parts = raw.split()
        if len(parts)==3 and parts[1] in 'tTW' and dem.startswith(prefix) and 'clone' not in dem:
            matches.append(parts[2])
    check(len(matches)==1, 'one native symbol: ' + prefix)
    return matches[0]

def find_anim(mgr):
    table = ev('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem' % mgr).dereference()['mObjects']
    for index in range(2048):
        entity = table[index]['mEntity']
        if not int(entity):
            continue
        try:
            obj = entity.cast(entity.dynamic_type).dereference()
            model = obj['mModelData']['mPtr']
            if int(model):
                anim = model.dereference()['mAnimData']['mItem']
                if int(anim):
                    log('REAL_ANIM', actor=int(entity), dynamic_type=str(entity.dynamic_type),
                        anim=int(anim), char_index=int(anim.dereference()['mCharIdx']))
                    return int(anim)
        except (gdb.error, KeyError, RuntimeError):
            continue
    raise gdb.GdbError('no live animated actor; do not fabricate one')

def centers(S):
    # Independent affine-box calculation, not calls to BoxForPosition/FindClosestCell.
    t = ev(S + 'mTransform')
    e = vector(ev(S + 'mBoundingBoxExtent'))
    lo, hi = [], []
    for row in range(3):
        extent = sum(abs(float(t['m%d%d'%(row,col)])) * e[col] * .5 for col in range(3))
        origin = float(t['m%d3'%row])
        lo.append(origin-extent)
        hi.append(origin+extent)
    return [tuple(lo[j]+(hi[j]-lo[j])*(idx[j]+.5)/5. for j in range(3))
            for idx in ((0,0,0),(4,0,0))]

def nearest(points, pos):
    # Strict < keeps the first equal candidate, exactly as retail's bge skip.
    best, distance = (0.,0.,0.), 3.4028234663852886e38
    for point in points:
        new_distance = sum((a-b)**2 for a,b in zip(point,pos))
        if new_distance < distance:
            best, distance = point, new_distance
    return best

class Return(gdb.FinishBreakpoint):
    def __init__(self, callback):
        super().__init__(gdb.newest_frame(), internal=True)
        self.callback = callback
    def stop(self):
        self.callback()
        return False
    def out_of_scope(self):
        log('FAIL', label='observed native callee unwound')

class Sound(gdb.Breakpoint):
    def stop(self):
        expected = state['expect_sound']
        if expected is None:
            return False
        params = ev('((CAudioSys::C3DEmitterParmData*)%d)'%iv('$rdi')).dereference()
        actual = dict(pos=vector(params['mPos']), direction=vector(params['mDir']),
                      maxdist=float(params['mMaxDist']), falloff=float(params['mDistComp']),
                      flags=int(params['mFlags']), sfx=int(params['mSfxId']),
                      maxvol=int(params['mMaxVol']), minvol=int(params['mMinVol']),
                      area=iv('$esi'), acoustics=iv('$edx')&255, looped=iv('$ecx')&255)
        check(near(actual['pos'],expected['pos']), 'sound nearest-cell output')
        for key in ('direction','maxdist','falloff','flags','sfx','maxvol','minvol','area','acoustics','looped'):
            check(actual[key]==expected[key], 'sound parameter ' + key)
        record = dict(scenario=state['scenario'], params=actual)
        state['sound'].append(record)
        def returned():
            record['handle'] = iv('$eax') & 0xffffffff
            log('SOUND_RESULT', **record)
        Return(returned)
        return False

class LightStage(gdb.Breakpoint):
    def __init__(self, prefix, stage):
        super().__init__('*'+symbol(prefix), internal=True)
        self.stage=stage
    def stop(self):
        if state['scenario'] != 'hardware':
            return False
        L = '((CActorLights*)%d)->'%iv('$rdi')
        stage = self.stage
        state['hardware'].append(stage)
        if stage=='area':
            check(iv(L+'mAreaUpdateFramePeriod')==8 and iv(L+'mMaxAreaLights')==4 and
                  iv(L+'mMaxDynamicLights')==4, 'light constructor 8/4/4')
            check(iv(L+'mDirty')==1 and iv(L+'mCastShadows')==0, 'relight/shadow flags')
            check(vector(ev(L+'mLightingPositionOffset'))==(0.,0.,0.), 'zero light offset')
            check(iv('$rcx')==state['expect_bounds'], 'area bounds identity')
        elif stage=='dynamic':
            check(iv('$rdx')==state['expect_bounds'], 'dynamic bounds identity')
        else:
            na,nd=iv(L+'mAreaLights.mCount'),iv(L+'mDynamicLights.mCount')
            white=iv(L+'mInArea') and (not iv(L+'mHasAreaLights') or iv(L+'mAmbientOnly'))
            mask=0 if white else (1<<(na+nd))-1
            check(0<=na<=4 and 0<=nd<=4, 'bounded light counts')
            # This is the real ActivateLights output, not a stubbed answer.
            def returned():
                actual=iv('CGraphics::mLightActive')
                check(actual==mask and iv('CGraphics::mNumLightsActive')==mask.bit_count(),
                      'native light-mask/count output')
                colors=[]
                for i in range(2):
                    c=ev('CGX::gpGXState->mChanAmbColors[%d]'%i)
                    colors.append([int(c[k]) for k in ('r','g','b','a')])
                check(colors[0]==colors[1], 'ambient output applied to both channels')
                log('LIGHT_RESULT', area_count=na,dynamic_count=nd,expected_mask=mask,
                    actual_mask=actual,ambient=colors)
            Return(returned)
        log('LIGHT_STAGE', stage=stage)
        return False

class Trigger(gdb.Breakpoint):
    def stop(self):
        if state['done'] or state['pending'] is not None:
            return False
        this,mgr=iv('$rdi'),iv('$rsi')
        S='((CBacteriaSwarm*)%d)->'%this
        if iv('((CStateManager*)%d)->mUpdateFrameIdx'%mgr)<520 or iv(S+'mEditorId.value')!=0x003d0023:
            return False
        if iv(S+'mBoids.mCount')<2 or not iv(S+'mBoids.mItems[0].mActive') or not iv(S+'mBoids.mItems[1].mActive'):
            return False
        state['pending']=(this,mgr)
        self.enabled=False
        return True

class Run(gdb.Command):
    def __init__(self):
        super().__init__('bsw4-run',gdb.COMMAND_USER)
    def invoke(self,arg,from_tty):
        check(state['pending'] is not None,'unexpected debugger stop')
        this,mgr=state['pending'];state['pending']=None
        S='((CBacteriaSwarm*)%d)->'%this
        M='((CStateManager*)%d)->'%mgr
        A='((CAnimData*)%d)->'%find_anim(mgr)
        anim=iv(A[:-2])
        check(iv('CAnimData::mSoundPOINodes.mCount')==48,'48 constructed real POI slots')
        N='((CSoundPOINode*)CAnimData::mSoundPOINodes.mData)[0].'
        node=N[:-1]
        points=centers(S)
        boid_size=gdb.lookup_type('CBacteriaSwarm::CBoid').pointer().sizeof
        list_lv=S+'mPartitionedBoidLists.mData'
        with Transaction('geometry', [list_lv,S+'mOutlierBoidList',
                         S+'mBoids.mItems[0]',S+'mBoids.mItems[1]',
                         S+'mTransform',S+'mBoundingBoxExtent']):
            inf=gdb.selected_inferior()
            # Exact integral centers make the tie oracle independent of rounding.
            original=ev(S+'mTransform')
            origins=[round(float(original['m%d3'%i])) for i in range(3)]
            for row in range(3):
                for col in range(3):write(S+'mTransform.m%d%d'%(row,col),str(int(row==col)))
                write(S+'mTransform.m%d3'%row,str(origins[row]))
                write(S+'mBoundingBoxExtent.m'+'XYZ'[row],'10.f')
            points=centers(S)
            inf.write_memory(int(ev(list_lv).address),bytes(125*boid_size))
            write(S+'mOutlierBoidList','0')
            for index,point in enumerate(points):
                B=S+'mBoids.mItems[%d].'%index
                write(B+'mNext','0')
                for axis,v in enumerate(point):write(B+'mTransform.m%d3'%axis,repr(v))
            def heads(count):
                for slot in (0,4):write('((CBacteriaSwarm::CBoid**)&'+list_lv+')[%d]'%slot,'0')
                for i,slot in enumerate((0,4)[:count]):
                    write('((CBacteriaSwarm::CBoid**)&'+list_lv+')[%d]'%slot,
                          '&'+S+'mBoids.mItems[%d]'%i)
            Q=S+'mBoids.mItems[0].mVelocity'
            for label,count,pos in (
                    ('empty',0,points[0]),('second wins',2,points[1]),
                    ('first stays',2,points[0]),('tie first',2,tuple((a+b)*.5 for a,b in zip(*points))),
                    ('unordered',2,(float('nan'),0.,0.))):
                state['scenario']='closest '+label
                heads(count)
                for axis,value in zip('XYZ',pos):
                    if math.isnan(value):
                        inf.write_memory(int(ev(Q+'.m'+axis).address),struct.pack('<I',0x7fc00000))
                    else:
                        write(Q+'.m'+axis,repr(value))
                result=vector(ev('((CBacteriaSwarm*)%d)->FindClosestCell(%s)'%(this,Q)))
                expected=nearest(points[:count],pos)
                check(near(result,expected),'closest output '+label)
                log('CLOSEST_RESULT',scenario=label,actual=result,expected=expected)
            heads(2)
            T=ev('((CPlayer*)'+M+'mPlayers[0])->mTransform')
            player=tuple(float(T['m%d3'%i]) for i in range(3))
            area=iv(S+'mAreaId.value')
            sfx=iv(S+'mPatrolSound')
            check(0<=sfx<=65535,'real swarm sound id (including invalid sentinel)')
            log('SOUND_ID',value=sfx,source='live swarm mPatrolSound')
            # Guard/static symbols are resolved by the native symbol table, no offsets.
            statics=[]
            for suffix in ('maxDistance','falloff'):
                for guard in (False,True):
                    prefix=('guard variable for ' if guard else '')+'CBacteriaSwarm::UpdateEffects(CStateManager&, CAnimData&, int)::'+suffix
                    candidates=[]
                    for line in open(os.environ['COV_NM']):
                        raw,dem=line.rstrip('\n').split('\t')
                        if dem==prefix:candidates.append(raw.split()[-1])
                    check(len(candidates)==1,'static identity '+prefix)
                    statics.append(("*(%s*)&'%s'"%('unsigned long long' if guard else 'float',candidates[0]),guard))
            with Transaction('sound statics',[lv for lv,guard in statics]):
                for lv,guard in statics:write(lv,'0')
                cases=(('zero',0,8,-1,sfx,64,0),('wrong type',1,6,-1,sfx,64,0),
                       ('wrong character',1,8,iv(A+'mCharIdx')+1,sfx,64,0),
                       ('high bit',1,8,-1,sfx|0x80000000,64,0),
                       ('negative volume',1,8,-1,sfx|0x12340000,-1,1),
                       ('character match',1,8,iv(A+'mCharIdx'),sfx,64,1),
                       ('high volume',1,8,-1,sfx,200,1),('boundary volume',1,8,-1,sfx,127,1))
                for label,count,kind,char,sound,volume,expected_calls in cases:
                    state['scenario']='effects '+label
                    with Transaction(state['scenario'],[A+'mPassedSoundCount',node]):
                        write(A+'mPassedSoundCount',str(count))
                        for field,value in (('mType',kind),('mCharIdx',char),('mSfxId',sound),
                                            ('mMaxDist',17 if label=='negative volume' else 31),
                                            ('mFalloff',.25 if label=='negative volume' else .75)):
                            write(N+field,str(value))
                        state['expect_sound']=dict(pos=nearest(points,player),direction=(0.,0.,0.),
                            maxdist=17.,falloff=.25,flags=1,sfx=sound&65535,
                            maxvol=max(0,min(volume,127)),minvol=20,area=area,acoustics=1,looped=0)
                        before=len(state['sound'])
                        try:
                            gdb.execute('call ((CBacteriaSwarm*)%d)->UpdateEffects(*((CStateManager*)%d),*((CAnimData*)%d),%d)'%(this,mgr,anim,volume),to_string=True)
                            check(len(state['sound'])-before==expected_calls,'emitter count '+label)
                            log('EFFECTS_RESULT',scenario=label,emitter_calls=expected_calls)
                        finally:
                            state['expect_sound']=None
                            release_sounds(before)
            # Native output mutates only graphics state and constructor scheduling.
            # Restore CPU bytes then replay immediate GX light/ambient uploads.
            hw=['CGraphics::mLightActive','CGraphics::mNumLightsActive','CGraphics::mLightObj',
                'CGraphics::mLightTypes','*CGX::gpGXState','CActorLights::sFrameSchedulerCount']
            with GraphicsTransaction('hardware graphics',hw):
                state['scenario']='hardware'
                state['expect_bounds']=iv('&'+S+'mAabox')
                gdb.execute('call ((CBacteriaSwarm*)%d)->HardwareLight(*((CStateManager*)%d),%smAabox)'%(this,mgr,S),to_string=True)
                check(state['hardware']==['area','dynamic','activate'],'ordered light pipeline')
        state['scenario']=None
        state['done']=True
        log('DONE',sound_calls=len(state['sound']),hardware=state['hardware'])

def finished(event):
    log('EXIT',done=state['done'])
    if not state['done']:log('FAIL',label='fixture did not complete')

def install():
    binary=gdb.current_progspace().filename
    check(bool(binary),'GDB executable identity')
    with open(binary,'rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
    log('BINARY',path=binary,sha256=digest)
    Sound('*'+symbol('CSfxManager::AddEmitter(CAudioSys::C3DEmitterParmData&'),internal=True)
    for prefix,stage in (('CActorLights::BuildAreaLightList(','area'),
                         ('CActorLights::BuildDynamicLightList(','dynamic'),
                         ('CActorLights::ActivateLights(','activate')):
        LightStage(prefix,stage)
    Trigger('*'+symbol('CBacteriaSwarm::Think('),internal=True)
    Run()
    gdb.execute('define hook-backtrace\nbsw4-run\ncontinue\nend')
    gdb.events.exited.connect(finished)

if os.environ.get('BSW4_SELFTEST')!='1':
    install()
