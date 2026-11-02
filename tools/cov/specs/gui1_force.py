# Direct native GUI calls, with real FrontEnd objects and restored input/state.
# COV_GDB_SCRIPT=tools/cov/force_ret.py; FrontEnd, gui1.con, coverage build.
import gdb
import os

gdb.execute('set environment MP2_SKIP_FRONTEND 0')

CLASS='CScriptGuiFrontEndScreen'
SPEC=[]

def ev(s):
    return gdb.parse_and_eval(s)

def log(s):
    with open(os.environ['COV_OUT']+'/gui1.log','a') as f:
        f.write(s+'\n')

class Prepare(gdb.Function):
    def __init__(self):
        super().__init__('gui1_prepare')
    def invoke(self,mgr):
        table=ev('((rstl::auto_ptr<CObjectList>*)((CStateManager*)%d)->mObjectLists.mData)[0].mItem'%int(mgr)).dereference()['mObjects']
        for i in range(2048):
            p=table[i]['mEntity']
            if int(p) and 'CScriptGuiPlayerJoinManager' in str(p.dynamic_type):
                gdb.set_convenience_variable('g1_join',p.cast(gdb.lookup_type('CScriptGuiPlayerJoinManager').pointer()))
            if int(p) and 'CScriptSound' in str(p.dynamic_type):
                sound=p.cast(gdb.lookup_type('CScriptSound').pointer())
                if int(sound.dereference()['mSoundId'])!=65535:
                    gdb.set_convenience_variable('g1_sound',sound)
        assert int(ev('$g1_join'))
        assert int(ev('$g1_sound'))
        log('real join manager and sound captured')
        log('sound id '+str(ev('$g1_sound->mSoundId')))
        return 1

class Expect(gdb.Function):
    def __init__(self):
        super().__init__('gui1_expect')
    def invoke(self,value):
        assert bool(value), 'GUI field oracle failed'
        log('field oracle PASS')
        return 1

Prepare();Expect()
queue=[([], '$gui1_prepare($mgr)')]
def add(writes,call,stubs=None):
    queue.append((writes,call,stubs or []))

# Join transitions: every button ordering/connection state, all four slots.
js='((CScriptGuiPlayerJoinManager::EJoinState*)$g1_join->mJoinStates.mData)'
inputs=['$mgr->mFinalInputs[%d].'%i for i in range(4)]
base=[('$g1_join->mActive','1')]
for i,p in enumerate(inputs):
    base += [(js+'[%d]'%i,'0')]+[(p+n,'0') for n in ('btns1','btns2','btns3','btns4')]
add([('$g1_join->mActive','0')], '$g1_join->Think(0.016f,*$mgr)')
for state in range(5):
    for button in ('','mB24_PA','mB27_PY','mB25_PB','mB27_PStart'):
        writes=base+[(js+'[0]',str(state))]
        if button:
            writes += [(inputs[0]+button,'1')]
        expected=None
        if state==3: expected=3
        elif button=='mB24_PA': expected=2
        elif button=='mB27_PY': expected=3 if state in (2,4) else state
        elif button=='mB25_PB': expected=1 if state in (2,4) else state
        call='$g1_join->Think(0.016f,*$mgr)'
        if expected is not None:
            call='('+call+',$gui1_expect('+js+'[0]==%d))'%expected
        add(writes,call)
add(base+[(js+'[0]','2'),(js+'[1]','2'),(inputs[0]+'mB27_PStart','1')], '$g1_join->Think(0.016f,*$mgr)')
add(base+[(js+'[0]','0'),(inputs[0]+'mB25_PB','1'),(js+'[1]','3')], '$g1_join->Think(0.016f,*$mgr)')
for enough in ('0','1'):
    add(base+[(js+'[0]','2'),(inputs[0]+'mB27_PStart','1'),('$g1_join->mPlayerCountState',enough)], '$g1_join->Think(0.016f,*$mgr)')

# Slider: actual brightness widget and its callback, input/clamp/sound paths.
slider='$this->mBrightnessSlider'
sb=[(slider+'->mActive','1'),(slider+'->mLocked','0'),(slider+'->mControllerNumber','0'),
    (slider+'->mValue','4.f'),(slider+'->mTargetValue','4.f'),(slider+'->mMinValue','0.f'),
    (slider+'->mMaxValue','8.f'),(slider+'->mSlideSpeed','1.f'),
    ('$mgr->mFinalInputs[0].mAnaLeftX','0.f')]
sb += [(inputs[0]+n,'0') for n in ('btns1','btns2','btns3','btns4')]
add([(slider+'->mActive','0')],slider+'->Think(0.016f,*$mgr)')
for direction in ('left','right','dleft','dright','idle'):
    for edge in (False,True):
        writes=sb+[(slider+'->mSlideRequested','0'),(slider+'->mSlideState','CScriptGuiSlider::kSS_Idle')]
        if direction in ('left','right'):
            writes += [('$mgr->mFinalInputs[0].mAnaLeftX','-1.f' if direction=='left' else '1.f')]
        elif direction in ('dleft','dright'):
            writes += [(inputs[0]+('mB26_PDPLeft' if direction=='dleft' else 'mB24_PDPRight'),'1')]
        if edge:
            value='0.f' if direction in ('left','dleft') else '8.f'
            writes += [(slider+'->mValue',value),(slider+'->mTargetValue',value)]
        add(writes,slider+'->Think(0.25f,*$mgr)')
for state in ('kSS_Decreasing','kSS_Increasing'):
    add(sb+[(slider+'->mSlideState','CScriptGuiSlider::'+state),(slider+'->mSlideRequested','0')],slider+'->Think(0.016f,*$mgr)')
add(sb+[(slider+'->mTargetValue','3.f')],slider+'->Think(0.016f,*$mgr)')
add(sb+[(slider+'->mTargetValue','3.f')]+[(slider+'->mCallback.mMethod.mFuncStorage[%d]'%i,'0') for i in range(16)],slider+'->Think(0.016f,*$mgr)')
add(sb+[('$mgr->mSaveGameScreen.mPtr','0')],slider+'->Think(0.016f,*$mgr)')
add(sb+[('$mgr->mSaveGameScreen.mPtr->mUiType','CSaveGameScreen::kUIT_BusyReading')],slider+'->Think(0.016f,*$mgr)')
add(sb+[(slider+'->mSlideSfx','$g1_sound->mSoundId'),(inputs[0]+'mAnaLeftX','-1.f')],
    '('+slider+'->Think(0.125f,*$mgr),$gui1_expect('+slider+'->mSlideSfxHandle.mID!=0),'+slider+'->Think(0.125f,*$mgr),'+inputs[0]+'mAnaLeftX=0.f,'+slider+'->Think(0.016f,*$mgr),1)')

# FE Think: controlled real save screen completion, busy and failure transitions.
for ret in ('kMR_Normal','kMR_RemoveIOWin','kMR_RemoveIOWinAndExit'):
    for busy in (False,True):
        writes=[('$this->mCardDriverReset','1'),('$this->mSaveScreenFailed','0'),
            ('$this->mSaveScreenBusy','0' if busy else '1'),
            ('$mgr->mSaveGameScreen.mPtr->mIowRet','CIOWin::'+ret),
            ('$mgr->mSaveGameScreen.mPtr->mUiType','CSaveGameScreen::kUIT_BusyReading' if busy else 'CSaveGameScreen::kUIT_SaveReady')]
        stubs=[('CSaveGameScreen::PumpLoad(',1)]
        # Keep the real save object alive for subsequent scenarios. The actual
        # CloseSaveGameScreen call has already been covered by the natural driver.
        if ret!='kMR_Normal': stubs += [('CScriptGuiFrontEndScreen::CloseSaveGameScreen(',0)]
        add(writes,'$this->Think(0.016f,*$mgr)',stubs)
add([('$mgr->mSaveGameScreen.mPtr','0')],'$this->Think(0.016f,*$mgr)')

# POD file metadata is a valid fixture independent of serialized card writes.
add([], '($g1_file=(CGameState::GameFileStateInfo*)calloc(1,sizeof(CGameState::GameFileStateInfo)),$g1_file->mMlvlId=0x42b935e4,$g1_file->mPlayTime=3661.,$g1_file->mItemPercent=57,$g1_normal=gpGameState->mSystemOptions.mEnvVars.FindEnvironmentVariable("NormalModeCompleted"),1)')
for count in (0,1,2,3):
    for hard in (False,True):
        writes=[('$g1_file->mHardMode',str(int(hard))),('$g1_file->x21_',str(int(hard))),('$g1_normal->mValue',str(int(hard)))]
        results=['$g1_file' if i<count else '0' for i in range(3)]
        stubs=[('CSaveGameScreen::GetGameData(',results)]
        if count==2:
            stubs += [('CSaveWorldMemory::GetDarkFrontEndName(',0),('CSaveWorldMemory::GetFrontEndName(',0)]
        add(writes,'$this->UpdateSaveSlots(*$mgr)',stubs)
add([], '(free($g1_file),1)')

# Re-collect the real shipped connection graph with multiplayer results.
# All vectors here contain non-owning pointers or trivially destructible records.
collections=('mPlayerSetups','mTeleporters','mSaveSlots','mSlotCountRelays','mOptionsPages','mResetPages','mSlotNamePanes','mUnlockSwitches','mSounds','mSoundVolumes','mValuePanes')
pr='gpGameState->mPreviousGameResults'
player='((CGameState::SPlayerResult*)'+pr+'.mPlayers.mData)[0]'
for mode in ('CFrontEndGameMode::kSGM_Coin','CFrontEndGameMode::kSGM_DeathMatch'):
    writes=[('$this->'+n+'.mCount','0') for n in collections]
    writes += [(pr+'.mGameMode',mode),(pr+'.mPlayerCount','1'),(player+'.mPlayerSelection','0'),(player+'.xc_','1'),(player+'.mRumbleEnabled','0')]
    setup='((CScriptGuiFrontEndScreen::SPlayerSetup*)$this->mPlayerSetups.mData)[0]'
    add(writes,'($this->CollectWidgets(*$mgr),$gui1_expect('+setup+'.mInvertMenu->mSelection==1 && '+setup+'.mRumbleMenu->mSelection==0))')

# Add a typed connection to a real scene sound; don't fabricate an entity/tag.
add([], '($g1_n=$this->mConnections.mCount,$g1_connections=(SConnection*)malloc(($g1_n+1)*sizeof(SConnection)),(void)memcpy($g1_connections,$this->mConnections.mItems,$g1_n*sizeof(SConnection)),$g1_connections[$g1_n].state=kSS_PowerBombDamage,$g1_connections[$g1_n].msg=kSM_None,$g1_connections[$g1_n].objId=$g1_sound->mEditorId,1)')
add([('$this->'+n+'.mCount','0') for n in collections]+[('$this->mConnections.mItems','$g1_connections'),('$this->mConnections.mCount','$g1_n+1')],
    '($this->CollectWidgets(*$mgr),$gui1_expect($this->mSounds.mCount==1 && $this->mSoundVolumes.mCount==1))')
add([], '(free($g1_connections),1)')

# Exit is last: exercise the actual delete and PC early return, not a fake pointer.
add([('$this->mConnections.mCount','0')], '($this->mCardDriverReset=true,$mgr->mSaveGameScreen.mPtr->mIowRet=CIOWin::kMR_Exit,$this->Think(0.016f,*$mgr),$gui1_expect($mgr->mSaveGameScreen.mPtr==0 && $this->mGameStarted),$this->mGameStarted=false,1)',
    [('CSaveGameScreen::PumpLoad(',1)])

CALLS=[('gui1 native queue','CScriptGuiFrontEndScreen::Think(',
    '((CStateManager*)$rsi)->mUpdateFrameIdx>400 && ((CStateManager*)$rsi)->mSaveGameScreen.mPtr!=0',queue,
    'real GUI objects, crafted inputs and save completion states')]
