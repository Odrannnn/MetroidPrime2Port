# fp4b: FlyingPirate (module 22) Think 15DC arms that no shipped fight
# reaches, forced on the coverage build by direct calls (CALLS: members written, the body called,
# members written back; a stub makes a callee return at once). The helpers ($fp3_obj, $fp3_uid,
# $fp4_slot) are those of flyingpirate_fp3.py / flyingpirate_fp4.py, loaded from this directory.
#   COV_GDB_SCRIPT=tools/cov/force_ret.py BUILD=$PWD/build-port-cov tools/cov/force.sh \
#     tools/cov/specs/flyingpirate_fp4b.py <out> 1baa96c2,01_cliff_entrance \
#     --con tools/cov/specs/flyingpirate_fp3_cliff.con --fast --frames 4200
#   (the water scenarios need 08_sand_casualpirates: flyingpirate_fp4b_casual.con)
import os
_here = os.path.dirname(os.environ['COV_SPEC'])
for _f in ('flyingpirate_fp3.py', 'flyingpirate_fp4.py'):
    exec(compile(open(os.path.join(_here, _f)).read(), _f, 'exec'))

CLASS = 'CFlyingPirate'
TH = '$this->Think(0.016f, *$mgr)'
BC = '$this->mBodyController.mPtr'
BURST = [('$this->mAlive', '1'), ('$this->mBurstTimer', '-1.0'), ('$this->mBurstFire.mBurstType', '0'),
         ('$this->mAimAtTarget', '1')]
LAI = '$fp3_obj($mgr, 4, %d, "CFlyingPirate")'
ANIM = '$this->DoUserAnimEvent(*$mgr, *(CInt32POINode*)$rdx, %d, 0.016f)'
NOTLOADED = [('CObjectReference::IsLoaded(', 0)]
NOCREATE = [('CStateManager::CanCreateProjectile(', 0)]

CALLS = [
    ('Think arms fp4', 'CFlyingPirate::Think(', '((CEntity*)$rdi)->mActive != 0', [
        # body controller inactive on a non-flying pirate: the Activate without SetLocomotionType
        ([('%s->mActive' % BC, '0'), ('$this->mIsFlyingPirate', '0')], TH),
        ([('%s->mActive' % BC, '0'), ('$this->mIsFlyingPirate', '1')], TH),
        # InFluidId() names an id no object has: the water cast of null
        ([('$this->mAlive', '1'), ('$this->mIsAquaPirate', '0')], TH, [('CActor::InFluidId(', 2047)]),
        # dead: the burst branch's !mAlive arm, the !mAlive else arms (burst timer 22050, 1997 else)
        ([('$this->mAlive', '0'), ('$this->mBurstTimer', '-1.0'), ('$this->mBurstFire.mBurstType', '0')], TH),
        ([('$this->mAlive', '0'), ('$this->mBurstTimer', '5.0'), ('$this->mBurstFire.mBurstType', '0')], TH),
        # alive, may shoot: each term of the && false in turn
        (BURST + [('$this->mStopped', '1')], TH),
        (BURST + [('%s->mElectrocutionDur' % BC, '4.0')], TH),
        (BURST + [('%s->mBodyStateInfo.mState' % BC, '18')], TH),
        (BURST + [('$this->mAimAtTarget', '0')], TH),
        # the listening-AI walk: another area, an inactive actor, a non-physics-actor entry
        (BURST + [('$this->mAreaId.value', '$this->mAreaId.value + 1')], TH),
        (BURST + [('((CEntity*)%s)->mActive' % (LAI % 0), '0')], TH),
        (BURST + [('((CEntity*)%s)->mActive' % (LAI % 1), '0')], TH),
        (BURST + [(SLOT.replace('5)', '4)'), '*(unsigned long*)$fp4_slot($mgr, 5)')], TH),
        # electrocuted / frozen, alive (1979 chain), dead states of 1997, pitch bend clamps
        ([('$this->mAlive', '1'), ('%s->mElectrocutionDur' % BC, '4.0')], TH),
        ([('$this->mAlive', '1'), ('$this->mAimAtTarget', '1'), ('%s->mFrozen' % BC, '1')], TH),
        ([('$this->mAlive', '1'), ('$this->mAimAtTarget', '1'), ('$this->mIsAquaPirate', '1')], TH),
        ([('$this->mAlive', '1'), ('%s->mBodyStateInfo.mState' % BC, '10')], TH),
        ([('$this->mAlive', '1'), ('%s->mBodyStateInfo.mState' % BC, '14')], TH),
        ([('$this->mAlive', '1'), ('%s->mBodyStateInfo.mState' % BC, '2')], TH),
        ([('$this->mAlive', '1'), ('%s->mBodyStateInfo.mState' % BC, '1')], TH),
        ([('$this->mTargetPitchBend', '0.2')], TH),
        ([('$this->mTargetPitchBend', '5.0')], TH),
        ([('$this->mFlightVelocity.mX', '3.0'), ('$this->mFlightAcceleration.mX', '0.0')], TH),
    ], 'Think with dead / stopped / electrocuted / state / actor-list / pitch arms forced'),
]
SPEC = []
