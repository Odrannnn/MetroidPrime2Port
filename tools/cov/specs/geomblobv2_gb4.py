# Private SPE/geometry fixtures. See GeomBlobV2.md, Coverage (gb4).
# Requires geomblobv2_gb4_keep_static.cmake in the coverage build.
# x86-64 SysV / GCC. All fixtures are debugger-only; no production probes.
# Cast results for Platform/Water are predicate-only inputs, not live assets.
# Success requires the final completion line, no Python/FAILED messages, and
# a normal inferior exit; run.sh's own rc alone does not detect debugger stops.
import os
import runpy
import struct
import gdb

CLASS = 'CCollisionTracker'
SPEC = []
FORCE = []

if not hasattr(gdb, '_gb4_installed'):
    gdb._gb4_installed = True
    lib = runpy.run_path(os.path.join(os.path.dirname(os.environ['COV_SPEC']), 'geomblobv2_gb3_surface.py'),
                         init_globals={'GB3_SURFACE_LIBRARY': True})
    cmd, ev, invoke, symbol, address, log = [lib[k] for k in
                                           ('cmd', 'ev', 'invoke', 'symbol', 'address', 'log')]

    def write(ptr, data):
        gdb.selected_inferior().write_memory(ev('(unsigned long)(%s)' % ptr), data)

    def put(expr, value):
        cmd('set %s = %s' % (expr, value))

    class Finished(gdb.FinishBreakpoint):
        def __init__(self, callback):
            super().__init__(gdb.newest_frame(), internal=True)
            self.callback = callback

        def stop(self):
            self.enabled = False
            self.callback()
            return False

    class Gather(gdb.Breakpoint):
        def stop(self):
            if suite.case is not None:
                surfaces, ids = ev('$rcx'), ev('$r8')
                def fill():
                    put('$gb4_think_surfaces', '(rstl::reserved_vector<CCollisionSurface, 256>*)%d' % surfaces)
                    put('$gb4_think_ids', '(rstl::reserved_vector<unsigned int, 256>*)%d' % ids)
                    count = 0 if suite.case in (5,12) else 1
                    put('$gb4_think_surfaces->mCount', count)
                    put('$gb4_think_ids->mCount', count)
                    write('$gb4_think_surfaces->mData', struct.pack('<9f4xQ',
                          -10, -10, 0, 10, -10, 0, 0, 10, 0, 0))
                    write('$gb4_think_ids->mData', struct.pack('<I', 123))
                Finished(fill)
            return False

    class Update(gdb.Breakpoint):
        def stop(self):
            if suite.case is None or suite.case == 12 or ev('$rdi') != ev('$gb3_spe->mParticleGen.mPtr'):
                return False
            def fill():
                n = suite.case
                gen = '$gb3_spe->mParticleGen.mPtr'
                count = ev(gen + '->mParticles.mCount')
                assert count > 0, 'need live particles for Think case %d' % n
                frame = ev(gen + '->mCurFrame')
                put('$gb3_spe->mFrameCounter', ((frame-1) + (1 if n in (9, 10) else 0)) & 7)
                for i in range(count):
                    p = gen + '->mParticles.mItems[%d]' % i
                    put(p + '.mStartFrame', frame-1)
                    put(p + '.mEndFrame', -1 if n == 7 else frame+3 if n == 8 else frame+100)
                    write('&' + p + '.mPos', struct.pack('<3f', 0, 0, 0))
                    write('&' + p + '.mVel', struct.pack('<3f', *((0,0,1) if n == 1 else (1,0,0))))
                    sid = 0xFFFFFFFF if n in (6,7,8,9,10) else 123 if n == 4 else 456
                    current = (0.70710678, 0.70710678, 0, 0)
                    target = (1,0,0,0) if n in (9,11) else current
                    write(gen + '->mAdvValues.mItems + %d' % i, struct.pack('<I8f', sid, *current, *target))
                    matrix = (1,0,0, 0,0,-1, 0,1,0)
                    if n == 2:
                        matrix = (1,0,0, 0,1,0, 0,0,1)
                    elif n == 3:
                        matrix = (1,0,0, 0,0.6,-0.8, 0,0.8,0.6)
                    write(gen + '->mParentMatrices.mItems + %d' % i, struct.pack('<9f', *matrix))
                log('Think case %d: configured %d particles after real Update' % (n, count))
            Finished(fill)
            return False

    class Near(gdb.Breakpoint):
        def stop(self):
            if suite.dynamic is not None:
                ptr = ev('$rsi')
                def fill():
                    n = suite.dynamic
                    uid = ev('$gb4_actor->mUniqueId.value')
                    if n in (7, 8, 10):
                        uid = ev('$gb3_ct->mUniqueId.value')
                    elif n == 9:
                        uid = 65534
                    put('$gb4_near', '(rstl::reserved_vector<TUniqueId, 2048>*)%d' % ptr)
                    put('$gb4_near->mCount', 0 if n in (0,11) else 1)
                    write('$gb4_near->mData', struct.pack('<H', uid))
                    if n not in (0,11):
                        put('$gb4_surfaces->mCount', 0)
                        put('$gb4_ids->mCount', 0)
                    log('Gather case %d: near uid %d' % (n, uid))
                Finished(fill)
            return False

    class Primitive(gdb.Breakpoint):
        def stop(self):
            if suite.dynamic in (3,4,5,6):
                actor = ev('$rdi')
                value = ev('$gb4_primitive') if suite.dynamic in (3,4) else ev(
                    '&((CPhysicsActor*)%d)->mCollisionPrimitive' % actor)
                Finished(lambda: put('$rax', value))
            return False

    class Cast(gdb.Breakpoint):
        def __init__(self, kind, case):
            super().__init__('*' + symbol('%s* TCastToPtr<%s>(CEntity*)' % (kind,kind)), internal=True)
            self.case = case

        def stop(self):
            if suite.dynamic == self.case:
                actor = ev('$rdi')
                Finished(lambda: put('$rax', actor))
                log('Gather case %d: forced non-null predicate-only cast result' % self.case)
            return False

    def fixtures():
        # Caller-owned scratch is above the separate scheduled-call stack.
        for name, typ, offset in (
                ('surfaces', 'rstl::reserved_vector<CCollisionSurface, 256>', 60000),
                ('ids', 'rstl::reserved_vector<unsigned int, 256>', 45000),
                ('box', 'CAABox', 43000), ('center', 'CVector3f', 42900),
                ('extent', 'CVector3f', 42800), ('tree', 'COBBTree', 42000),
                ('verts', 'CVector3f', 40000), ('edges', 'CCollisionEdge', 39000),
                ('indices', 'unsigned short', 38000), ('mats', 'unsigned long long', 37000),
                ('smats', 'unsigned char', 36000)):
            put('$gb4_' + name, '(%s*)($rsp - %d)' % (typ, offset))
        write('$gb4_box', struct.pack('<6f', -1, -1, -1, 1, 1, 1))
        write('$gb4_center', struct.pack('<3f', 0, 0, 0))
        write('$gb4_extent', struct.pack('<3f', 2, 2, 2))
        write('$gb4_verts', struct.pack('<9f', -1, -1, 0, 1, -1, 0, 0, 1, 0))
        write('$gb4_edges', struct.pack('<6H', 0, 1, 1, 2, 2, 0))
        write('$gb4_indices', struct.pack('<6H', 0, 1, 2, 0, 1, 2))
        write('$gb4_mats', struct.pack('<Q', 0))
        write('$gb4_smats', bytes(2))
        for field, value in (('mMaterials', '$gb4_mats'), ('mVertices', '$gb4_verts'),
                             ('mEdges', '$gb4_edges'), ('mSurfaceIndices', '$gb4_indices'),
                             ('mSurfaceMaterials', '$gb4_smats'), ('mTriangleCount', 2),
                             ('mMaterialCount', 1), ('mVertexCount', 3), ('mEdgeCount', 3)):
            put('$gb4_tree->' + field, value)
        put('$gb4_surfaces->mCount', 0)
        put('$gb4_ids->mCount', 0)

    class Suite(lib['Surface']):
        stage = 0
        case = None
        dynamic = None

        def stop(self):
            try:
                tracker, mgr = ev('$rdi'), ev('$rsi')
                put('$gb3_ct', '(CCollisionTracker*)%d' % tracker)
                put('$gb4_mgr', '(CStateManager*)%d' % mgr)
                if self.stage == 0:
                    put('$gb3_name', '(rstl::string*)($rsp - 2048)')
                    put('$gb3_name->mPtr', '(char*)%d' % address(
                        'rstl::basic_string<char, rstl::char_traits<char>, rstl::rmemory_allocator>::mNull'))
                    put('$gb3_name->mCow', 0)
                    put('$gb3_name->mSize', 0)
                    invoke('CreateSurfaceParticleEffect(', [
                        '&$gb3_ct->mParticleSystem.mPtr->mGenDesc', 65535,
                        '$gb3_ct->mAreaId.value', 1, '$gb3_name',
                        '&$gb3_ct->mTransform', 65535, 0], self.created)
                elif self.stage <= 9:
                    fixtures()
                    n = self.stage
                    count = 255 if n in (3, 8) else 256 if n in (4, 9) else 0
                    put('$gb4_surfaces->mCount', count)
                    put('$gb4_ids->mCount', count)
                    if n in (2, 6):
                        write('$gb4_center', struct.pack('<3f', 100, 100, 100))
                    if n == 5:
                        put('$gb4_tree->mTriangleCount', 0)
                    if n == 7:
                        put('*$gb4_mats', '1ULL << kMT_ProjectilePassthrough')
                    expected = {1:12, 2:0, 3:256, 4:256, 5:0, 6:0, 7:0, 8:256, 9:256}[n]
                    def verify(n=n, count=count, expected=expected):
                        actual = ev('$gb4_surfaces->mCount')
                        assert actual == expected and ev('$gb4_ids->mCount') == expected
                        ids = gdb.parse_and_eval('(unsigned int*)$gb4_ids->mData')
                        assert all(int(ids[i]) == (0xA4000000 | (i-count)) for i in range(count, actual))
                        log('geometry case %d: %d -> %d, IDs verified' % (n, count, actual))
                    if n <= 4:
                        invoke('GatherFromBox(', ['$gb4_surfaces', '$gb4_ids', '$gb4_box',
                               '$gb4_center', '$gb4_extent', 0xA4000000], verify)
                    else:
                        invoke('GatherFromObbTree(', ['$gb4_surfaces', '$gb4_ids', '$gb4_tree',
                               '&CTransform4f::sIdentity', '$gb4_center', '$gb4_extent', 0xA4000000], verify)
                elif self.stage == 10:
                    # Record available real actor types for the dynamic-gather cases.
                    objects = gdb.parse_and_eval('((rstl::auto_ptr<CObjectList>*)$gb4_mgr->mObjectLists.mData)[0].mItem->mObjects')
                    seen = set()
                    for index in range(2048):
                        entry = objects[index]
                        actor = entry['mEntity']
                        if int(actor) and str(actor.dynamic_type) not in seen:
                            seen.add(str(actor.dynamic_type))
                            log('object %d %s %s' % (int(actor['mUniqueId']['value']),
                                actor.dynamic_type, actor))
                            if str(actor.dynamic_type) == 'CScriptActor *':
                                put('$gb4_actor', '(CScriptActor*)%d' % int(actor))
                elif 11 <= self.stage < 311:
                    put('$gb3_spe->mGeneratorRate', '0.0' if self.stage == 11 else '1.0')
                    put('$xmm0.v4_float[0]', '0.016666667')
                    invoke('CSurfaceParticleEffect::Think(', [self.effect, mgr])
                elif self.stage in (311, 329):
                    put('$gb3_msg', '(CScriptMsg*)($rsp - 2048)')
                    for member in ('mSenderId', 'm_originator', 'm_id'):
                        put('$gb3_msg->%s.value' % member, 65535)
                    put('$gb3_msg->m_state', 'kSS_InvalidState')
                    put('$gb3_msg->m_msg', 'kSM_Create' if self.stage == 311 else 'kSM_Delete')
                    put('$gb3_spe->mParticleGen.mPtr->mLightType', 'CElementGen::kLT_Custom')
                    invoke('CSurfaceParticleEffect::AcceptScriptMsg(', [self.effect, mgr, '$gb3_msg'])
                elif 312 <= self.stage <= 328:
                    self.case = (self.stage - 312) % 12
                    # Keep a single real, allocated particle; all three arrays
                    # hold trivial records. Bound debugger work below watchdog.
                    for field in ('mParticles.mCount', 'mParentMatrices.mCount',
                                  'mAdvValues.mCount', 'mActiveParticleCount',
                                  'mRecursiveParticleCount', 'mMAXP'):
                        put('$gb3_spe->mParticleGen.mPtr->' + field, 1)
                    put('$gb3_spe->mGeneratorRate', '1.0')
                    put('$gb3_spe->mSpawnRemainder', '1000.0' if self.stage == 312 else '1.0')
                    put('$gb3_spe->mActive', 0 if self.stage == 327 else 1)
                    if self.stage == 328:
                        self.light = ev('$gb3_spe->mLightId.value')
                        put('$gb3_spe->mLightId.value', '$gb3_ct->mUniqueId.value')
                    put('$xmm0.v4_float[0]', '0.0')
                    def done(n=self.case):
                        gen = '$gb3_spe->mParticleGen.mPtr'
                        sid = ev('*(unsigned int*)' + gen + '->mAdvValues.mItems')
                        expected = 0xFFFFFFFF if n in (1,2,5,6,7,8,9,10,11) else 123
                        assert sid == expected, (n,sid,expected)
                        end = ev(gen + '->mParticles.mItems[0].mEndFrame')
                        if n in (7,8):
                            assert end == -1
                        if n in (0,3,4):
                            assert end == ev(gen + '->mCurFrame') + 100
                        if n in (1,2,5,6,9,10,11):
                            assert end == ev(gen + '->mCurFrame') + 89
                        vel = gdb.parse_and_eval(gen + '->mParticles.mItems[0].mVel')
                        if n in (1,2,11):
                            assert all(float(vel[c]) == 0 for c in ('mX','mY','mZ'))
                        log('Think case %d: surface/velocity/fade result verified' % n)
                        if self.stage == 329:
                            put('$gb3_spe->mLightId.value', self.light)
                        self.case = None
                    invoke('CSurfaceParticleEffect::Think(', [self.effect, mgr], done)
                elif 330 <= self.stage <= 341:
                    self.dynamic = n = self.stage - 330
                    fixtures()
                    write('$gb4_box', struct.pack('<6f', -10000,-10000,-10000,10000,10000,10000))
                    if n == 11:
                        put('$gb4_surfaces->mCount', 256)
                        put('$gb4_ids->mCount', 256)
                    put('$gb3_spe->mFlag', int(n in (2,3)))
                    put('$gb3_spe->mIgnoredCollisionId.value', '$gb4_actor->mUniqueId.value' if n == 1 else 65535)
                    saved = [(expr,ev(expr)) for expr in ('$gb4_actor->mActive',
                        '$gb4_actor->mMaterial.mValue', '$gb3_ct->mMaterial.mValue')]
                    put('$gb4_actor->mActive', int(n != 6))
                    put('$gb4_actor->mMaterial.mValue', '1ULL << kMT_Solid')
                    put('$gb3_ct->mMaterial.mValue', '1ULL << kMT_Solid' if n == 10 else 0)
                    put('$gb4_primitive', '(CCollidableOBBTreeGroup*)($rsp - 35000)')
                    put('$gb4_group', '(COBBTreeGroup*)($rsp - 34000)')
                    put('$gb4_treeptrs', '(rstl::auto_ptr<COBBTree>*)($rsp - 33000)')
                    put('*(unsigned long*)$gb4_primitive', address('vtable for CCollidableOBBTreeGroup') + 16)
                    put('$gb4_primitive->mContainer', '$gb4_group')
                    put('$gb4_group->mTrees.mCount', 0 if n == 4 else 1)
                    put('$gb4_group->mTrees.mCapacity', 1)
                    put('$gb4_group->mTrees.mItems', '$gb4_treeptrs')
                    put('$gb4_treeptrs->mItem', '$gb4_tree')
                    def done(n=n, saved=saved):
                        try:
                            count = ev('$gb4_surfaces->mCount')
                            assert count == ev('$gb4_ids->mCount')
                            assert count == {0:256,3:2,5:12,11:256}.get(n,0)
                            if n in (3,5):
                                bits = (ev('$gb4_actor->mUniqueId.value') << 22) & 0xFFFFFFFF
                                ids = gdb.parse_and_eval('(unsigned int*)$gb4_ids->mData')
                                assert all(int(ids[i]) == bits | i for i in range(count))
                            log('Gather case %d: returned %d surfaces; IDs verified' % (n,count))
                        finally:
                            for expr, value in saved:
                                put(expr, value)
                            self.dynamic = None
                    invoke('CSurfaceParticleEffect::GatherCollisionSurfaces(', [self.effect, mgr,
                           '$gb4_box', '$gb4_surfaces', '$gb4_ids'], done)
                elif self.stage == 342:
                    self.case = 12
                    for field in ('mParticles.mCount', 'mParentMatrices.mCount', 'mAdvValues.mCount',
                                  'mActiveParticleCount', 'mRecursiveParticleCount'):
                        put('$gb3_spe->mParticleGen.mPtr->' + field, 0)
                    put('$gb3_spe->mParticleGen.mPtr->mMAXP', 128)
                    put('$gb3_spe->mSpawnRemainder', '1.0')
                    put('$gb3_spe->mGeneratorRate', '1.0')
                    put('$gb3_spe->mFlag', 0)
                    put('$xmm0.v4_float[0]', '0.016666667')
                    def done():
                        assert ev('$gb3_spe->mParticleGen.mPtr->mParticles.mCount') == 0
                        self.case = None
                        log('Think case 12: no spawn surface; Update removed expired particles')
                    invoke('CSurfaceParticleEffect::Think(', [self.effect,mgr], done)
                elif self.stage == 343:
                    m = next(m for m, d in lib['symbols'] if d == 'CSurfaceParticleEffect::~CSurfaceParticleEffect()' and 'D0' in m)
                    invoke('CSurfaceParticleEffect::~CSurfaceParticleEffect()', [self.effect],
                           lambda: log('gb4 complete; private SPE destroyed'))
                    put('$rip', "(unsigned long)&'%s'" % m)
                    self.enabled = False
                self.stage += 1
            except Exception as exc:
                log('FAILED gb4 stage %d: %s' % (self.stage, exc))
                self.enabled = False
            return False

    suite = Suite('*' + symbol('CCollisionTracker::Think('), internal=True)
    Gather('*' + symbol('CSurfaceParticleEffect::GatherCollisionSurfaces('), internal=True)
    Update('*' + symbol('CElementGen::Update('), internal=True)
    Near('*' + symbol('CStateManager::BuildNearList(rstl::reserved_vector<TUniqueId, 2048>&, CAABox'), internal=True)
    Primitive('*' + symbol('CScriptActor::GetCollisionPrimitive('), internal=True)
    Cast('CScriptPlatform', 3)
    Cast('CScriptWater', 8)
