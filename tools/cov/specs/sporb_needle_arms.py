# sporb6: real UpdateShooting spawn, then every Explode/Render arm.
# BUILD=$PWD/build-port-cov COV_GDB_SCRIPT=$PWD/tools/cov/force_ret.py SECS=300 \
# tools/cov/force.sh tools/cov/specs/sporb_needle_arms.py <out> \
# 3dfd2249,0e_swamp_hall --con tools/drivers/sporb4_hall.con --fast --frames 1800
import gdb
import math
import os

CLASS = 'CSporbBase'
gdb.set_convenience_variable('sp6_nan', gdb.Value(float('nan')))
for name, value in [('needle', '(CSporbNeedle*)0'), ('collision', '(CCollisionActor*)0'),
                    ('trace', '0'), ('near', '0'), ('top', '(CSporbTop*)0'),
                    ('proj', '(CSporbProjectile*)0'), ('base', '(CSporbBase*)0'), ('locator', '-1')]:
    gdb.execute('set $sp6_%s = %s' % (name, value))


def log(text):
    with open(os.path.join(os.environ['COV_OUT'], 'forced.log'), 'a') as out:
        out.write(text + '\n')


symbols = []
for line in open(os.environ['COV_NM']):
    raw, _, demangled = line.rstrip().partition('\t')
    fields = raw.split()
    if len(fields) == 3 and fields[1] in 'tTW' and '.cold' not in demangled:
        symbols.append((fields[2], demangled))


def symbol(prefix):
    return next(m for m, d in symbols if d.startswith(prefix))


class CaptureNeedle(gdb.Breakpoint):
    def stop(self):
        gdb.execute('set $sp6_needle = (CSporbNeedle*)$rdi')
        return False


CaptureNeedle('*' + symbol('CSporbNeedle::CSporbNeedle('), internal=True)


class CaptureBase(gdb.Breakpoint):
    def stop(self):
        actor = gdb.parse_and_eval('(CSporbBase*)$rdi')
        if bool(actor['mShooting']) and not bool(actor['mIsPowerBombGuardian']) and \
                int(actor['mTopId']['value']) != 65535 and int(actor['mProjectileId']['value']) != 65535:
            gdb.set_convenience_variable('sp6_base',actor)
            self.enabled = False
        return False


CaptureBase('*' + symbol('CSporbBase::UpdateShooting('), internal=True)
damage = []


class Damage(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval('$sp6_trace')) and \
                int(gdb.parse_and_eval('$rsi & 65535')) == \
                int(gdb.parse_and_eval('$sp6_needle->mUniqueId.value')):
            info = gdb.parse_and_eval('*(CDamageInfo*)$r8')
            damage.append((int(gdb.parse_and_eval('$rdx & 65535')),
                           float(info['mDamage']), float(info['mKnockbackPower'])))
        return False


Damage('*' + symbol('CStateManager::ApplyDamage(TUniqueId'), internal=True)


class NearReturn(gdb.FinishBreakpoint):
    def __init__(self, frame, target, mode):
        super().__init__(frame, internal=True)
        self.target, self.mode = target, mode

    def stop(self):
        # Execute the actual broadphase, then supply a reproducible candidate list.
        # Includes a stale uid to exercise ObjectById's null arm after the owner test.
        ids = [] if self.mode == 2 else [
            '$sp6_needle->mOwnerId.value', '$sp6_collision->mUniqueId.value',
            '$sp6_player->mUniqueId.value', '65535']
        gdb.execute('set %s->mCount = %d' % (self.target, len(ids)))
        for i, uid in enumerate(ids):
            gdb.execute('set ((TUniqueId*)%s->mData)[%d].value = %s' % (self.target, i, uid))
        return False


class Near(gdb.Breakpoint):
    def stop(self):
        mode = int(gdb.parse_and_eval('$sp6_near'))
        if mode:
            target = '((rstl::reserved_vector<TUniqueId, 2048>*)%d)' % int(gdb.parse_and_eval('$rsi'))
            NearReturn(gdb.newest_frame(), target, mode)
        return False


Near('*' + symbol('CStateManager::BuildNearList(rstl::reserved_vector<TUniqueId, 2048>&, CAABox'),
     internal=True)


class LocatorReturn(gdb.FinishBreakpoint):
    def stop(self):
        gdb.execute('set $rax = $sp6_locator')
        return False


class Locator(gdb.Breakpoint):
    def stop(self):
        if int(gdb.parse_and_eval('$sp6_locator')) >= 0:
            LocatorReturn(gdb.newest_frame(), internal=True)
        return False


Locator('*' + symbol('CAnimData::GetLocatorSegId('), internal=True)
labels, scenarios = [], []


class Begin(gdb.Function):
    def __init__(self):
        super().__init__('sp6_begin')

    def invoke(self):
        damage.clear()
        return 1


class Check(gdb.Function):
    def __init__(self):
        super().__init__('sp6_check')

    def invoke(self, index, passed, count, amount):
        good = bool(passed) and len(damage) == int(count)
        if int(count):
            good &= all(abs(d - float(amount)) < 2.e-5 and abs(k - float(amount)/2) < 2.e-5
                        for _, d, k in damage)
        log('sporb6 #%d %s: %s damage=%s' %
            (int(index), labels[int(index)], 'PASS' if good else 'FAIL', damage))
        if not good:
            raise gdb.GdbError('Sporb needle differs from retail')
        return 1


Begin()
Check()
objlist = '((rstl::auto_ptr<CObjectList>*)$mgr->mObjectLists.mData)[0].mItem->mObjects'
scenarios.append(([], '($sp6_player=$P,'
                  '$sp6_top=(CSporbTop*)' + objlist + '[$this->mTopId.value & 2047].mEntity,'
                  '$sp6_proj=(CSporbProjectile*)' + objlist +
                  '[$this->mProjectileId.value & 2047].mEntity,1)'))
labels.append('bind player/base/children')
shot_writes = [('$this->mShooting', '1'), ('$this->mShotTimer', '0.5f'),
               ('$this->mTimeBetweenShots', '0.25f'), ('$this->mShotsFired', '0'),
               ('$this->mShotsInBurst', '3')]
scenarios.append((shot_writes, '($this->UpdateShooting(.125f,*$mgr),'
                  '$sp6_check(1,$sp6_needle != 0,0,0))'))
labels.append('6ABC real needle spawn')
ctor = symbol('CCollisionActor::CCollisionActor(TUniqueId, TAreaId, TUniqueId, bool, float, float)')
scenarios.append(([], '$sp6_collision=(CCollisionActor*)calloc(1,sizeof(CCollisionActor))'))
labels.append('allocate collision actor')
scenarios.append(([], "('%s'($sp6_collision,$mgr->AllocateUniqueId(),$this->mAreaId,"
                  "$this->mUniqueId,true,.5f,1.f),$mgr->AddObject(*$sp6_collision),1)" % ctor))
labels.append('construct/register collision actor')
inputs = [('$sp6_needle->mExploded', '0'), ('$sp6_needle->mOwnerId.value', '$this->mUniqueId.value'),
          ('$sp6_needle->mAttackDamage.mDamage', '8.f'),
          ('$sp6_needle->mAttackDamage.mKnockbackPower', '4.f'),
          ('$sp6_needle->mAttackDamage.mDamageRadius', '2.f'),
          ('$sp6_collision->mOwner.value', '$this->mUniqueId.value'),
          ('$sp6_trace', '1'), ('$sp6_near', '0')]
saved = ['$sp6_needle->mMaterial.mValue']


def explode(label, hit, writes=(), count=0, amount=0):
    n = len(scenarios)
    scenarios.append(([(s, s) for s in saved] + inputs + list(writes),
                      '($sp6_begin(),$sp6_needle->Explode(*$mgr,%s),'
                      '$sp6_check(%d,$sp6_needle->mExploded,%d,%sf))' % (hit,n,count,float(amount))))
    labels.append(label)


explode('106A4 already exploded', '$P->mUniqueId', [('$sp6_needle->mExploded', '1')])
explode('10778 direct owner', '$this->mUniqueId')
explode('10760 collision owner', '$sp6_collision->mUniqueId')
explode('107F4 direct player, trail present', '$P->mUniqueId', count=1, amount=8)
explode('107F4 collision other owner, trail null', '$sp6_collision->mUniqueId',
        [('$sp6_collision->mOwner.value', '$P->mUniqueId.value'),
         ('$sp6_needle->mTrailGen.mPtr', '0')], 1, 8)
for radius in ('0.f', '-1.f', '$sp6_nan'):
    explode('10808 radius=' + radius, 'kInvalidUniqueId',
            [('$sp6_needle->mAttackDamage.mDamageRadius', radius)])
explode('108E0 empty near list', 'kInvalidUniqueId', [('$sp6_near', '2')])
for distance, count, amount in ((0.,1,8.),(1.,1,4.),(2.,0,0.),(3.,0,0.),(float('nan'),0,0.)):
    position = [('$P->mPosition.m' + axis, '$sp6_needle->mPosition.m' + axis) for axis in 'XYZ']
    position += [('$P->mPosition.mX', '$sp6_needle->mPosition.mX + ' +
                  ('$sp6_nan' if math.isnan(distance) else repr(distance)))]
    explode('10998 distance=%s radius=2' % distance, 'kInvalidUniqueId',
            [('$sp6_near','1')] + position, count, amount)
explode('10934 collision other owner outside radius', 'kInvalidUniqueId',
        [('$sp6_near','1'), ('$sp6_collision->mOwner.value','$P->mUniqueId.value')] +
        [('$sp6_collision->mPosition.m'+a,'$sp6_needle->mPosition.m'+a+' + 100.f') for a in 'XYZ'] +
        [('$P->mPosition.m'+a,'$sp6_needle->mPosition.m'+a+' + 100.f') for a in 'XYZ'])
# Rendering executes at a normal game render call, where the GPU frame is open.
render_cases = []
for exploded, missing in ((1,False),(0,True),(0,False)):
    render_cases.append(([('$sp6_needle->mExploded',str(exploded))] +
                         ([('$sp6_needle->mModelData.mPtr','0')] if missing else []),
                         '($sp6_needle->Render(*$mgr),1)'))
render_cases.append(([('$sp6_needle->mExploded','0'),
                      ('$sp6_needle->mModelData.mPtr->mNormalModel.m_valid','0')],
                     '($sp6_needle->Render(*$mgr),1)'))
scenarios.append(([], '($sp6_begin(),$sp6_check(%d,$sp6_needle->GetCollisionPrimitive() == '
                  '&$sp6_needle->mSphere,0,0))' % len(scenarios)))
labels.append('10468 collision primitive')
for call, check, label in [
    ('$sp6_proj->GetCollisionPrimitive()', '$sp6_result == &$sp6_proj->mSphere', 'DE50 primitive'),
    ('$sp6_proj->IsScanVisorSelfRender()', '$sp6_result == 1', 'DE58 self render'),
    ('$sp6_top->IsScanVisorSelfRender()', '$sp6_result == 1', '2E8 self render')]:
    n = len(scenarios)
    scenarios.append(([], '($sp6_begin(),$sp6_result=%s,$sp6_check(%d,%s,0,0))' % (call,n,check)))
    labels.append(label)
for method, off in [('PlayBallSpitEffect', 'E4D4'), ('PlayBallEscapeEffect','E358')]:
    for locator in (-1,255):
        n = len(scenarios)
        scenarios.append(([('$sp6_locator',str(locator)), ('$sp6_proj->mEffectIndex','255')],
                          '($sp6_begin(),$sp6_proj->%s(*$mgr),'
                          '$sp6_check(%d,$sp6_proj->mEffectIndex == 0,0,0))' % (method,n)))
        labels.append('%s effect locator=%d, index wraps' % (off,locator))
scenarios.append(([], '($mgr->DeleteObjectRequest($sp6_collision->mUniqueId),1)'))
labels.append('delete collision fixture')

render_snapshot = None


class RenderSnapshot(gdb.Function):
    def __init__(self):
        super().__init__('sp6_render_snapshot')

    def invoke(self, verify):
        global render_snapshot
        values = []
        for obj in ('$sp6_base','$sp6_top'):
            for field in ('mTransform','mDrawFlags'):
                value = gdb.parse_and_eval(obj + '->' + field)
                values.append(bytes(gdb.selected_inferior().read_memory(value.address,value.type.sizeof)))
        if int(verify):
            good = values == render_snapshot
            log('ScanVisorRender restored transforms/flags: ' + ('PASS' if good else 'FAIL'))
            if not good:
                raise gdb.GdbError('scan render failed to restore actor state')
        else:
            render_snapshot = values
        return 1


RenderSnapshot()
for state, scanning, missing in ((2,False,False),(7,True,False),(0,True,False),
                                (2,True,True),(2,True,False)):
    writes = [('$sp6_base->mState',str(state)),
              ('$P->mScanningObject.value','$sp6_base->mUniqueId.value' if scanning else '65535')]
    if missing:
        writes.append(('$sp6_base->mTopId.value','65535'))
    render_cases.append((writes, '($sp6_render_snapshot(0),$sp6_base->ScanVisorRender('
                         '*$mgr,$sp6_proj->mTransform,$sp6_proj->mDrawFlags),$sp6_render_snapshot(1))'))
for scanning, missing, projectile in (('none',False,False),('top',False,True),
                                     ('proj',False,True),('top',True,True)):
    writes = [('$P->mScanningObject.value', {'none':'65535','top':'$sp6_top->mUniqueId.value',
                                           'proj':'$sp6_proj->mUniqueId.value'}[scanning])]
    if missing:
        writes.append(('$sp6_top->mBaseId.value','65535'))
    if not projectile:
        writes.append(('$sp6_top->mProjectileId.value','65535'))
    render_cases.append((writes, '($sp6_render_snapshot(0),$sp6_top->ScanVisorRender('
                         '*$mgr,$sp6_proj->mTransform,$sp6_proj->mDrawFlags),$sp6_render_snapshot(1))'))
# force_ret's batch hook services one stopping CALLS breakpoint. Capture the base
# without stopping, then execute the whole suite at a real render entry.
scenarios = [([(k.replace('$this','$sp6_base'),v.replace('$this','$sp6_base')) for k,v in writes],
              call.replace('$this','$sp6_base')) for writes,call in scenarios]
CALLS = [('sporb6 needle and render', 'CActor::Render(', '$sp6_base != 0', scenarios+render_cases,
          '; '.join('%d=%s' % item for item in enumerate(labels)))]
SPEC = []
