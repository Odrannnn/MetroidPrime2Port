// Save states, phase 2: room state, the live script objects of the loaded areas (MP1's
// port_savestate_room.cpp, adapted to MP2). See platform/include/port_savestate.h.
//
// An object is keyed by its editor id, whose area bits name its area. Capture records, for each
// loaded area, every scripted object that still exists (a dead enemy counts as gone) with tagged
// blocks of state. Apply deletes the live objects of a saved area that the state does not have -
// a collected pickup or a killed enemy stays gone - and puts the saved state back on the rest.

#include "port_savestate.h"
#include "port_savestate_block.h"

#include "Kyoto/Math/CTransform4f.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "MetroidPrime/CActor.hpp"
#include "MetroidPrime/CWorldLayerState.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/CCameraManager.hpp"
#include "MetroidPrime/CEntity.hpp"
#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CHealthInfo.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/CPhysicsActor.hpp"
#include "MetroidPrime/CScriptObjectLoaderHelper.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Cameras/CCinematicCamera.hpp"
#include "MetroidPrime/Enemies/CDarkTrooper.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCamera.hpp"
#include "MetroidPrime/ScriptObjects/CScriptActorRotate.hpp"
#include "MetroidPrime/ScriptObjects/CScriptCounter.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDebris.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDock.hpp"
#include "MetroidPrime/ScriptObjects/CScriptDoor.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPickup.hpp"
#include "MetroidPrime/ScriptObjects/CScriptPlatform.hpp"
#include "MetroidPrime/ScriptObjects/CScriptStreamedMusic.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTimer.hpp"
#include "MetroidPrime/TCastTo.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

// Friend of CScriptTimer, CScriptCounter and CScriptDoor (their TARGET_PC blocks): their state is
// private.
struct PortRoomState {
  // A flotsam Dark Trooper is a corpse by design (AreaLoaded clears mAlive, HP -1): the reload
  // rebuilds it, so it is not a kill the state should remember.
  static bool IsFlotsam(const CDarkTrooper& t) { return t.mRagDollHolder.mFlotsam; }
  static CScriptDoor::ELockState DoorLock(const CScriptDoor& d) { return d.mLockState; }
  static float TimerTime(const CScriptTimer& t) { return t.mTime; }
  static void SetTimer(CScriptTimer& t, float time, bool timing) {
    t.mTime = time;
    t.mIsTiming = timing;
  }
  static void SetCounter(CScriptCounter& c, int current) { c.mCurrent = current; }

  // Platform: the spline clock and the motion machine's flags (bit 0 active, 1 forward, 2 previous
  // forward, 3 passed end, 4 passed start).
  static float PlatMotionTime(const CScriptPlatform& p) { return p.mMotionTime; }
  static float PlatMoveDelay(const CScriptPlatform& p) { return p.mMoveDelay; }
  static uint32_t PlatFlags(const CScriptPlatform& p) {
    return (p.mMotionActive ? 1u : 0u) | (p.mMotionForward ? 2u : 0u) |
           (p.mPreviousMotionForward ? 4u : 0u) | (p.mPassedMotionEnd ? 8u : 0u) |
           (p.mPassedMotionStart ? 16u : 0u);
  }
  // The clock first (it stops the platform, moves it along its spline and drags its slaves), then
  // the saved pose and flags over that.
  static void SetPlatform(CScriptPlatform& p, CStateManager& mgr, float time, float moveDelay,
                          uint32_t flags, const CTransform4f& xf) {
    p.SetMotionTime(time, mgr);
    p.CActor::SetTransform(xf);
    CTransform4f rot = xf.GetRotation();
    rot.Orthonormalize();
    p.mPreviousRotation = rot;
    p.mCurrentRotation = rot;
    p.mMoveDelay = moveDelay;
    p.mMotionActive = (flags & 1) != 0;
    p.mMotionForward = (flags & 2) != 0;
    p.mPreviousMotionForward = (flags & 4) != 0;
    p.mPassedMotionEnd = (flags & 8) != 0;
    p.mPassedMotionStart = (flags & 16) != 0;
  }

  // Actor rotate: the clock, the play flag and the rotated actors with the pose each started from.
  static float RotTime(const CScriptActorRotate& r) { return r.mCurrentTime; }
  static bool RotPlaying(const CScriptActorRotate& r) { return r.mPlaying; }
  static const rstl::vector< rstl::pair< TUniqueId, CTransform4f > >& RotActors(
      const CScriptActorRotate& r) {
    return r.mActors;
  }
  static void SetRotate(CScriptActorRotate& r, float time, bool playing,
                        const std::vector< std::pair< TUniqueId, CTransform4f > >& actors) {
    r.mCurrentTime = time;
    r.mPlaying = playing;
    r.mActors.clear();
    for (const auto& a : actors) {
      r.mActors.push_back(rstl::pair< TUniqueId, CTransform4f >(a.first, a.second));
    }
  }
};

namespace PortSaveState {
namespace {

enum : uint32_t {
  kTagTransform = 1,
  kTagMotion = 2,
  kTagHealth = 3,
  kTagTimer = 5,
  kTagCounter = 6,
  kTagDoor = 7,
  kTagMusic = 8,     // u32 playing: a CScriptStreamedMusic
  kTagCinematic = 9, // f32 seconds into the shot: the CScriptCamera driving the cinematic camera
  kTagPlatform = 10, // f32 time, f32 move delay, u32 flags, 12 f32 pose: a CScriptPlatform
  kTagRotate = 11,   // f32 time, u32 playing, u32 n, n x (u32 editor id, 12 f32 base, 12 f32 now)
  kTagDock = 12,     // u8 reference count, u8 refs, refs x u8 load-other: a CScriptDock's Dock
  kTagGenerated = 13, // empty: a script generator's copy of a template, made again on load
  kTagMessages = 14,  // u8 n, n x (u32 message, u32 sender editor id): what woke a scripted enemy
  kTagFields = 15,    // the schema fields of an enemy, pickup or debris (port_savestate_fields.cpp)
};

// Per-object lines a restore logs, at most; the summary line always follows.
constexpr int kMaxChangeLines = 24;
constexpr uint32_t kMaxCount = 1u << 20; // sanity bound for decoded counts

const RoomBlock* FindBlock(const RoomObject& obj, uint32_t tag) {
  for (const RoomBlock& b : obj.blocks) {
    if (b.tag == tag) {
      return &b;
    }
  }
  return nullptr;
}

void PutTransform(BlockWriter& w, const CTransform4f& xf) {
  const float m[12] = {xf.Get00(), xf.Get01(), xf.Get02(), xf.Get03(), xf.Get10(), xf.Get11(),
                       xf.Get12(), xf.Get13(), xf.Get20(), xf.Get21(), xf.Get22(), xf.Get23()};
  for (float f : m) {
    w.F32(f);
  }
}

bool GetTransform(BlockReader& r, CTransform4f& out) {
  float m[12];
  for (float& f : m) {
    f = r.F32();
  }
  if (!r.ok) {
    return false;
  }
  for (float f : m) {
    if (!(f == f) || f > 1e7f || f < -1e7f) {
      return false;
    }
  }
  out = CTransform4f(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
  return true;
}

template < typename F >
void ForEachObject(CStateManager& mgr, F&& fn) {
  CObjectList& list = mgr.ObjectListById(kOL_All);
  for (int idx = list.GetFirstObjectIndex(); idx != -1; idx = list.GetNextObjectIndex(idx)) {
    if (CEntity* ent = list[idx]) {
      fn(*ent);
    }
  }
}

const CGameArea* ReadyArea(const CWorld& world, int area) {
  if (area < 0 || area >= world.GetNumAreas()) {
    return nullptr;
  }
  const CGameArea* a = world.GetArea(TAreaId(area));
  return a != nullptr && a->IsLoaded() ? a : nullptr;
}

// An editor id's area and layer (its top six bits), the unit an area spawns or skips.
uint32_t LayerKey(uint32_t editorId) {
  return (TEditorId(editorId).AreaNum() << 8) | (editorId >> 26);
}

// What the CStateManager and generator hooks noted per object (raw TUniqueId value), tagged
// with the manager it belongs to so a world being torn down never touches the next one's.
constexpr uint32_t kNoSender = 0xFFFFFFFFu;
constexpr size_t kMaxScriptMsgs = 32; // the first ones: what woke the enemy comes first
struct ScriptMsgEntry {
  uint32_t msg = 0;
  uint32_t sender = kNoSender; // the sender's editor id, or none
};
struct Noted {
  const CStateManager* mgr = nullptr;
  bool generated = false;
  std::vector< ScriptMsgEntry > msgs;
};
std::map< uint16_t, Noted > sNoted;

const Noted* FindNoted(const CStateManager& mgr, TUniqueId uid) {
  const auto it = sNoted.find(uid.value);
  return it != sNoted.end() && it->second.mgr == &mgr ? &it->second : nullptr;
}

Noted& NoteFor(const CStateManager& mgr, TUniqueId uid) {
  Noted& n = sNoted[uid.value];
  if (n.mgr != &mgr) {
    n = Noted();
    n.mgr = &mgr;
  }
  return n;
}

// The commands worth replaying. Activate/Deactivate/ToggleActive are the saved active flag;
// Arrived and Next are waypoint traffic; the rest are engine notifications.
bool JournalsScriptMsg(uint32_t msg) {
  switch (msg) {
  case kSM_Action:
  case kSM_Start:
  case kSM_Stop:
  case kSM_Play:
  case kSM_Alert:
  case kSM_SetToZero:
  case kSM_SetToMax:
  case kSM_Reset:
  case kSM_ResetAndStart:
  case kSM_StopAndReset:
  case kSM_Follow:
  case kSM_Open:
  case kSM_Close:
  case kSM_Increment:
  case kSM_Decrement:
    return true;
  default:
    return false;
  }
}

// A script generator's copy carries its template's editor id. Enemy copies, pickups and debris
// are saved as copies; others (sound, maze walls) are left to their generators.
bool IsGeneratedCopy(const CStateManager& mgr, const CEntity& ent) {
  const Noted* n = FindNoted(mgr, ent.GetUniqueId());
  return n != nullptr && n->generated;
}

bool SavesCopy(CEntity& ent) {
  return TCastToPtr< CPatterned >(&ent) != nullptr || TCastToPtr< CScriptPickup >(&ent) != nullptr ||
         TCastToPtr< CScriptDebris >(&ent) != nullptr;
}

TUniqueId RawId(uint16_t value) { return TUniqueId(value); }

bool IsScripted(const CEntity& ent) { return ent.GetEditorId().value != kInvalidEditorId.value; }

// A dead enemy is gone for the room even while its corpse still runs.
bool IsGone(CEntity& ent) {
  const CPatterned* patterned = TCastToPtr< CPatterned >(&ent);
  if (const CDarkTrooper* trooper = dynamic_cast< const CDarkTrooper* >(&ent)) { // REL class: no TCastTo
    if (PortRoomState::IsFlotsam(*trooper)) {
      return false;
    }
  }
  return patterned != nullptr && !patterned->GetAlive();
}

void CaptureObject(CStateManager& mgr, CEntity& ent, RoomObject& obj) {
  obj.editorId = ent.GetEditorId().value;
  obj.active = ent.GetActive();

  if (CActor* actor = TCastToPtr< CActor >(&ent)) {
    if (const CHealthInfo* health = actor->HealthInfo()) {
      BlockWriter w;
      w.F32(health->GetHP());
      obj.blocks.push_back({kTagHealth, w.bytes});
    }
  }
  CPhysicsActor* physics = TCastToPtr< CPhysicsActor >(&ent);
  if (physics != nullptr && TCastToPtr< CScriptDoor >(&ent) == nullptr) {
    // Inert props keep the card load's pose; enemies and anything moving are saved.
    if (TCastToPtr< CPatterned >(&ent) != nullptr || physics->GetVelocityWR().IsNonZero() ||
        physics->GetMomentumWR().IsNonZero()) {
      BlockWriter xf;
      PutTransform(xf, physics->GetTransform());
      obj.blocks.push_back({kTagTransform, xf.bytes});
      BlockWriter motion;
      motion.Vec(physics->GetVelocityWR());
      motion.Vec(physics->GetMomentumWR());
      obj.blocks.push_back({kTagMotion, motion.bytes});
    }
  }
  if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(&ent)) {
    BlockWriter w;
    w.F32(PortRoomState::PlatMotionTime(*plat));
    w.F32(PortRoomState::PlatMoveDelay(*plat));
    w.U32(PortRoomState::PlatFlags(*plat));
    PutTransform(w, plat->GetTransform());
    obj.blocks.push_back({kTagPlatform, w.bytes});
  }
  if (CScriptActorRotate* rot = TCastToPtr< CScriptActorRotate >(&ent)) {
    BlockWriter w;
    w.F32(PortRoomState::RotTime(*rot));
    w.U32(PortRoomState::RotPlaying(*rot) ? 1 : 0);
    const auto& actors = PortRoomState::RotActors(*rot);
    BlockWriter list;
    uint32_t n = 0;
    for (int i = 0; i < static_cast< int >(actors.size()); ++i) {
      const CEntity* target = mgr.GetObjectById(actors[i].first);
      if (target == nullptr || !IsScripted(*target)) {
        continue;
      }
      list.U32(target->GetEditorId().value);
      PutTransform(list, actors[i].second);
      const CActor* actor = TCastToConstPtr< CActor >(target);
      PutTransform(list, actor != nullptr ? actor->GetTransform() : actors[i].second);
      ++n;
    }
    w.U32(n);
    w.bytes.append(list.bytes);
    obj.blocks.push_back({kTagRotate, w.bytes});
  }
  if (const Noted* n = FindNoted(mgr, ent.GetUniqueId())) {
    if (!n->msgs.empty() && TCastToPtr< CPatterned >(&ent) != nullptr) {
      BlockWriter w;
      const size_t count = std::min(n->msgs.size(), kMaxScriptMsgs);
      w.U8(static_cast< uint8_t >(count));
      for (size_t i = 0; i < count; ++i) {
        w.U32(n->msgs[i].msg);
        w.U32(n->msgs[i].sender);
      }
      obj.blocks.push_back({kTagMessages, w.bytes});
    }
  }
  if (CScriptTimer* timer = TCastToPtr< CScriptTimer >(&ent)) {
    BlockWriter w;
    w.F32(PortRoomState::TimerTime(*timer));
    w.U32(timer->IsTiming() ? 1 : 0);
    obj.blocks.push_back({kTagTimer, w.bytes});
  }
  if (CScriptCounter* counter = TCastToPtr< CScriptCounter >(&ent)) {
    BlockWriter w;
    w.U32(static_cast< uint32_t >(counter->GetCurrent()));
    obj.blocks.push_back({kTagCounter, w.bytes});
  }
  if (CScriptDoor* door = TCastToPtr< CScriptDoor >(&ent)) {
    // Open, then the lock (MP1's door block carries open plus close flags; Echoes' doors have a
    // lock machine instead, and a state written before it was saved has only the first word).
    BlockWriter w;
    w.U32(door->IsOpen() ? 1 : 0);
    w.U32(static_cast< uint32_t >(PortRoomState::DoorLock(*door)));
    obj.blocks.push_back({kTagDoor, w.bytes});
  }
  // Dock: which neighbour the dock points at (its reference count) and which neighbours it
  // keeps loaded, held by the area's Dock record rather than by the script object.
  if (CScriptDock* dock = TCastToPtr< CScriptDock >(&ent)) {
    const CWorld* world = mgr.GetWorld();
    const int area = dock->GetAreaId().Value();
    if (area >= 0 && area < world->GetNumAreas() &&
        dock->GetDockId() < world->GetAreaAlways(TAreaId(area)).GetDockCount()) {
      const IGameArea::Dock& d = world->GetAreaAlways(TAreaId(area)).GetDock(dock->GetDockId());
      const int refs = static_cast< int >(d.GetDockRefs().size());
      BlockWriter w;
      w.U8(static_cast< uint8_t >(d.GetReferenceCount()));
      w.U8(static_cast< uint8_t >(refs));
      for (int i = 0; i < refs; ++i) {
        w.U8(d.GetShouldLoadOther(i) ? 1 : 0);
      }
      obj.blocks.push_back({kTagDock, w.bytes});
    }
  }
  // Music: only whether it plays; a load rebuilds the world, which stops everything.
  // (CScriptMidi has no type id to cast by, so it is not covered.)
  if (const CScriptStreamedMusic* music = TCastToConstPtr< CScriptStreamedMusic >(&ent)) {
    BlockWriter w;
    w.U32(music->PortPlaying() ? 1 : 0);
    obj.blocks.push_back({kTagMusic, w.bytes});
  }
  // A cinematic: how far into the shot the camera is.
  if (TCastToPtr< CScriptCamera >(&ent) != nullptr) {
    const CCameraManager* cameras = mgr.CameraManager(0);
    const CCinematicCamera* cine = cameras != nullptr ? cameras->GetCinematicCamera() : nullptr;
    if (cine != nullptr && cameras->IsInCinematicCamera() &&
        cine->GetScriptCameraId() == ent.GetUniqueId()) {
      BlockWriter w;
      w.F32(cine->PortElapsed());
      obj.blocks.push_back({kTagCinematic, w.bytes});
    }
  }
  // A block over the 64 KiB a block holds would drop the whole room's objects.
  std::string fields;
  if (CaptureEntityFields(mgr, ent, fields) && fields.size() <= 0xffff) {
    obj.blocks.push_back({kTagFields, std::move(fields)});
  }
}

struct ChangeLog {
  int lines = 0;
  void Note(uint32_t editorId, const char* what) {
    if (lines++ < kMaxChangeLines) {
      std::fprintf(stderr, "[savestate] room: object %08x %s\n", editorId, what);
    }
  }
};

// Returns whether a visible part of the object's state (active flag, health, door) differed.
// `holdTimers`: a running timer is restored stopped and listed in `held` (RoomApplyProgress), to be
// started once the last saved area has loaded - a timer firing early would message objects of an
// area that is not there yet.
// Sends an enemy the script messages it had been sent before the save, in order (what woke a
// boss), and keeps them for the next save. Senders are the objects of the rebuilt room with the
// saved editor ids; the messages carry no connection, so they are not noted again.
int ReplayScriptMsgs(CStateManager& mgr, CEntity& ent, const RoomObject& obj) {
  const RoomBlock* b = FindBlock(obj, kTagMessages);
  if (b == nullptr || TCastToPtr< CPatterned >(&ent) == nullptr) {
    return 0;
  }
  BlockReader r(b->data);
  const uint32_t count = r.U8();
  std::vector< ScriptMsgEntry > journal;
  for (uint32_t i = 0; i < count && r.ok && i < kMaxScriptMsgs; ++i) {
    ScriptMsgEntry e;
    e.msg = r.U32();
    e.sender = r.U32();
    if (!r.ok || !JournalsScriptMsg(e.msg)) {
      return 0;
    }
    journal.push_back(e);
  }
  const TUniqueId uid = ent.GetUniqueId();
  int replayed = 0;
  for (const ScriptMsgEntry& e : journal) {
    const TUniqueId sender =
        e.sender == kNoSender ? kInvalidUniqueId : mgr.GetIdForScript(TEditorId(e.sender));
    mgr.SendScriptMsg(&ent, sender, static_cast< EScriptObjectMessage >(e.msg));
    ++replayed;
  }
  NoteFor(mgr, uid).msgs = journal;
  return replayed;
}

bool ApplyObject(CStateManager& mgr, CEntity& ent, const RoomObject& obj, ChangeLog& log,
                 bool holdTimers, std::vector< uint32_t >& held, int* fieldsSet = nullptr) {
  bool changed = false;
  if (ent.GetActive() != obj.active) {
    // Through the message, as a relay does: an object's own kSM_Activate sets up what its Think
    // relies on (a patterned enemy's body controller, helper actors); flipping the flag alone
    // left it thinking without that.
    mgr.SendScriptMsg(&ent, kInvalidUniqueId, obj.active ? kSM_Activate : kSM_Deactivate);
    log.Note(obj.editorId, obj.active ? "activated" : "deactivated");
    changed = true;
  }
  const RoomBlock* fieldsBlock = FindBlock(obj, kTagFields);
  // The fields hold what the messages woke, so a block covering the enemy needs no replay.
  const bool fieldsCover = fieldsBlock != nullptr && EntityFieldsCover(ent, fieldsBlock->data);
  if (const int replayed = fieldsCover ? 0 : ReplayScriptMsgs(mgr, ent, obj)) {
    log.Note(obj.editorId, "script messages replayed");
    (void)replayed;
    changed = true;
  }
  if (CActor* actor = TCastToPtr< CActor >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagHealth)) {
      BlockReader r(b->data);
      const float hp = r.F32();
      CHealthInfo* health = actor->HealthInfo();
      if (r.ok && hp == hp && health != nullptr && health->GetHP() != hp) {
        health->SetHP(hp);
        log.Note(obj.editorId, "health restored");
        changed = true;
      }
    }
  }
  if (CPhysicsActor* physics = TCastToPtr< CPhysicsActor >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagTransform)) {
      BlockReader r(b->data);
      CTransform4f xf = CTransform4f::Identity();
      if (GetTransform(r, xf)) {
        physics->SetTransform(xf);
      }
    }
    if (const RoomBlock* b = FindBlock(obj, kTagMotion)) {
      BlockReader r(b->data);
      CVector3f velocity(0.f, 0.f, 0.f);
      CVector3f momentum(0.f, 0.f, 0.f);
      if (r.Vec(velocity) && r.Vec(momentum)) {
        physics->SetVelocityWR(velocity);
        physics->SetMomentumWR(momentum);
      }
    }
  }
  if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagPlatform)) {
      BlockReader r(b->data);
      const float time = r.F32();
      const float delay = r.F32();
      const uint32_t flags = r.U32();
      CTransform4f xf = CTransform4f::Identity();
      if (GetTransform(r, xf) && time == time && delay == delay && time > -1e5f && time < 1e5f) {
        PortRoomState::SetPlatform(*plat, mgr, time, delay, flags, xf);
        log.Note(obj.editorId, "platform restored");
        changed = true;
      }
    }
  }
  if (CScriptActorRotate* rot = TCastToPtr< CScriptActorRotate >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagRotate)) {
      BlockReader r(b->data);
      const float time = r.F32();
      const bool playing = r.U32() != 0;
      const uint32_t n = r.U32();
      std::vector< std::pair< TUniqueId, CTransform4f > > actors;
      std::vector< std::pair< CActor*, CTransform4f > > poses;
      for (uint32_t i = 0; r.ok && i < n && i < kMaxCount; ++i) {
        const uint32_t editorId = r.U32();
        CTransform4f base = CTransform4f::Identity();
        CTransform4f now = CTransform4f::Identity();
        if (!GetTransform(r, base) || !GetTransform(r, now)) {
          break;
        }
        const TUniqueId uid = mgr.GetIdForScript(TEditorId(editorId));
        CActor* actor = uid != kInvalidUniqueId ? TCastToPtr< CActor >(mgr.ObjectById(uid)) : nullptr;
        if (actor != nullptr) {
          actors.push_back({uid, base});
          poses.push_back({actor, now});
          if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(actor)) {
            plat->SetActorRotateId(rot->GetUniqueId());
          }
        }
      }
      if (r.ok && time == time && time >= 0.f && time < 1e5f) {
        PortRoomState::SetRotate(*rot, time, playing, actors);
        for (auto& p : poses) {
          p.first->SetTransform(p.second);
        }
        log.Note(obj.editorId, "rotation restored");
        changed = true;
      }
    }
  }
  if (CScriptTimer* timer = TCastToPtr< CScriptTimer >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagTimer)) {
      BlockReader r(b->data);
      const float time = r.F32();
      const bool timing = r.U32() != 0;
      if (r.ok && time == time) {
        const bool hold = timing && holdTimers;
        PortRoomState::SetTimer(*timer, time, timing && !hold);
        if (hold) {
          held.push_back(obj.editorId);
        }
      }
    }
  }
  if (CScriptCounter* counter = TCastToPtr< CScriptCounter >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagCounter)) {
      BlockReader r(b->data);
      const int current = static_cast< int >(r.U32());
      if (r.ok && current != counter->GetCurrent()) {
        PortRoomState::SetCounter(*counter, current);
        log.Note(obj.editorId, "counter restored");
      }
    }
  }
  if (CScriptDoor* door = TCastToPtr< CScriptDoor >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagDoor)) {
      BlockReader r(b->data);
      const bool open = r.U32() != 0;
      // The door's own messages run its state machine: an open request waits for the area
      // behind it the way a shot door does, and a close swings it shut.
      if (r.ok && open != door->IsOpen()) {
        mgr.SendScriptMsg(door, kInvalidUniqueId, open ? kSM_Open : kSM_Close);
        log.Note(obj.editorId, open ? "door opened" : "door closed");
        changed = true;
      }
      // The lock, through the same messages a script sends: kSM_Lock locks once Samus is in the
      // door's area and it is shut (closing it first), kSM_Unlock plays the unlock. A lock that
      // was mid-animation counts as where it was heading.
      const CScriptDoor::ELockState saved = static_cast< CScriptDoor::ELockState >(r.U32());
      if (r.ok) {
        const CScriptDoor::ELockState live = PortRoomState::DoorLock(*door);
        const bool wantLocked = saved == CScriptDoor::kLS_Pending ||
                                saved == CScriptDoor::kLS_Locking || saved == CScriptDoor::kLS_Locked;
        const bool isLocked = live == CScriptDoor::kLS_Pending || live == CScriptDoor::kLS_Locking ||
                              live == CScriptDoor::kLS_Locked;
        if (wantLocked != isLocked) {
          mgr.SendScriptMsg(door, kInvalidUniqueId, wantLocked ? kSM_Lock : kSM_Unlock);
          log.Note(obj.editorId, wantLocked ? "door locked" : "door unlocked");
          changed = true;
        }
      }
    }
  }
  if (CScriptDock* dock = TCastToPtr< CScriptDock >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagDock)) {
      const int area = dock->GetAreaId().Value();
      CWorld* world = mgr.World();
      if (area >= 0 && area < world->GetNumAreas() &&
          dock->GetDockId() < world->Area(TAreaId(area))->GetDockCount()) {
        IGameArea::Dock& d = world->Area(TAreaId(area))->DockNC(dock->GetDockId());
        BlockReader r(b->data);
        const int ref = r.U8();
        const int refs = r.U8();
        if (r.ok && refs == static_cast< int >(d.GetDockRefs().size()) && ref <= refs) {
          std::vector< bool > load(refs);
          for (int i = 0; i < refs; ++i) {
            load[i] = r.U8() != 0;
          }
          if (r.ok) {
            if (ref != d.GetReferenceCount()) {
              d.SetReferenceCount(ref);
              log.Note(obj.editorId, "dock reference restored");
              changed = true;
            }
            for (int i = 0; i < refs; ++i) {
              if (load[i] != d.GetShouldLoadOther(i)) {
                d.SetShouldLoadOther(i, load[i]);
                log.Note(obj.editorId, "dock load-other restored");
                changed = true;
              }
            }
          }
        }
      }
    }
  }
  if (CScriptStreamedMusic* streamed = TCastToPtr< CScriptStreamedMusic >(&ent)) {
    if (const RoomBlock* b = FindBlock(obj, kTagMusic)) {
      BlockReader r(b->data);
      const bool wasPlaying = r.U32() != 0;
      // The music that played at the save starts again; it was playing under its own message.
      if (r.ok && wasPlaying && !streamed->PortPlaying() && ent.GetActive()) {
        mgr.SendScriptMsg(&ent, kInvalidUniqueId, kSM_Play);
        log.Note(obj.editorId, "music restarted");
        changed = true;
      }
    }
  }
  if (TCastToPtr< CScriptCamera >(&ent) != nullptr) {
    if (const RoomBlock* b = FindBlock(obj, kTagCinematic)) {
      BlockReader r(b->data);
      const float elapsed = r.F32();
      CCameraManager* cameras = mgr.CameraManager(0);
      if (r.ok && elapsed == elapsed && elapsed >= 0.f && elapsed < 1e5f && cameras != nullptr &&
          cameras->IsInCinematicCamera()) {
        // The activation above restarted the shot at 0; move it to where it was.
        CCinematicCamera* cine = const_cast< CCinematicCamera* >(cameras->GetCinematicCamera());
        if (cine != nullptr && cine->GetScriptCameraId() == ent.GetUniqueId()) {
          cine->PortSetElapsed(elapsed);
          log.Note(obj.editorId, "cinematic resumed");
          changed = true;
        }
      }
    }
  }
  if (fieldsBlock != nullptr) {
    if (const int set = ApplyEntityFields(mgr, ent, fieldsBlock->data)) {
      if (fieldsSet != nullptr) {
        *fieldsSet += set;
      }
      log.Note(obj.editorId, "fields restored");
      changed = true;
    }
  }
  return changed;
}

bool Contains(const std::vector< int32_t >& v, int32_t x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}

} // namespace

void CaptureRoomState(CStateManager& mgr, RoomState& out) {
  out = RoomState();
  const CWorld* world = mgr.World();
  if (world == nullptr) {
    return;
  }
  std::set< int > areas;
  for (int i = 0; i < world->GetNumAreas(); ++i) {
    if (const CGameArea* a = ReadyArea(*world, i)) {
      areas.insert(i);
      RoomArea ra;
      ra.area = i;
      ra.assetId = static_cast< uint32_t >(a->GetAreaAssetId());
      ra.fingerprint = AreaFingerprint(*a);
      ra.known = true;
      ra.spawnLayers = a->PortSpawnLayers();
      ra.hasSpawn = true;
      out.areas.push_back(ra);
    }
  }
  ForEachObject(mgr, [&](CEntity& ent) {
    if (!IsScripted(ent) || areas.count(ent.GetEditorId().AreaNum()) == 0 || IsGone(ent)) {
      return;
    }
    const bool generated = IsGeneratedCopy(mgr, ent);
    if (generated && !SavesCopy(ent)) {
      return;
    }
    RoomObject obj;
    CaptureObject(mgr, ent, obj);
    if (generated) {
      obj.blocks.push_back({kTagGenerated, std::string()});
    }
    out.objects.push_back(std::move(obj));
  });
  size_t copies = 0;
  for (const RoomObject& obj : out.objects) {
    copies += FindBlock(obj, kTagGenerated) != nullptr ? 1 : 0;
  }
  std::fprintf(stderr, "[savestate] room: captured %zu objects (%zu generated copies) in %zu areas\n",
               out.objects.size(), copies, out.areas.size());
}

// The running timers ApplyObject restored stopped, started once the last saved area is in.
static void StartHeldTimers(CStateManager& mgr, RoomApplyProgress& progress) {
  for (uint32_t editorId : progress.held) {
    if (CScriptTimer* timer = TCastToPtr< CScriptTimer >(
            mgr.ObjectById(mgr.GetIdForScript(TEditorId(editorId))))) {
      PortRoomState::SetTimer(*timer, PortRoomState::TimerTime(*timer), true);
    }
  }
  progress.held.clear();
}

int ApplyRoomState(CStateManager& mgr, const RoomState& room, RoomApplyProgress& progress,
                   bool force) {
  const CWorld* world = mgr.World();
  if (world == nullptr) {
    return static_cast< int >(room.areas.size());
  }
  RoomApplyStats& stats = progress.stats;
  std::set< int > now;
  int waiting = 0;
  for (const RoomArea& ra : room.areas) {
    if (Contains(progress.done, ra.area)) {
      continue;
    }
    const CGameArea* a = ReadyArea(*world, ra.area);
    if (a == nullptr) {
      ++waiting;
      continue;
    }
    if (static_cast< uint32_t >(a->GetAreaAssetId()) != ra.assetId) {
      std::fprintf(stderr, "[savestate] room: area %d is another area now; left as loaded\n",
                   ra.area);
      ++stats.skipped;
      progress.done.push_back(ra.area);
      continue;
    }
    if (const char* why = RoomAreaSkipReason(ra, AreaFingerprint(*a))) {
      // The loaded scripts are not the ones the state was made against (another disc build or a
      // mod): its editor ids would land on other objects, so the area is left as loaded.
      std::fprintf(stderr, "[savestate] room: area %d %s; left as loaded\n", ra.area, why);
      ++stats.skipped;
      progress.done.push_back(ra.area);
      continue;
    }
    if (!Contains(progress.seen, ra.area)) {
      progress.seen.push_back(ra.area);
      ++waiting;
      continue;
    }
    now.insert(ra.area);
  }
  if (waiting == 0) {
    StartHeldTimers(mgr, progress);
  }
  // One pass for every area, once all are back: an area applied while another still loads would run
  // on from its saved clocks (platforms, rotations, AI timers) for the whole wait. `force` is the
  // give-up pass, which restores the areas that did come back.
  if (now.empty() || (waiting != 0 && !force)) {
    return waiting;
  }

  ChangeLog log;
  std::set< uint32_t > present;
  std::set< uint32_t > savedLayers;
  for (const RoomObject& obj : room.objects) {
    if (FindBlock(obj, kTagGenerated) != nullptr) {
      continue; // a copy's id is its template's, which the rebuilt room does not have
    }
    present.insert(TEditorId(obj.editorId).Value());
    savedLayers.insert(LayerKey(obj.editorId));
  }
  std::map< uint32_t, int > otherLayers;
  std::map< uint32_t, int > unmatchedLayers;
  std::vector< std::pair< TUniqueId, uint32_t > > doomed;
  ForEachObject(mgr, [&](CEntity& ent) {
    if (!IsScripted(ent) || now.count(ent.GetEditorId().AreaNum()) == 0) {
      return;
    }
    // A layer the state has no object of was switched on after its area spawned (the reload
    // follows the saved layer flags, the live room did not yet): leave it as the reload made it.
    if (savedLayers.count(LayerKey(ent.GetEditorId().value)) == 0) {
      ++stats.otherLayer;
      ++otherLayers[LayerKey(ent.GetEditorId().value)];
      return;
    }
    // A generator's copy made since the load stays unless the state has it as a copy too.
    if (IsGeneratedCopy(mgr, ent) && !SavesCopy(ent)) {
      return;
    }
    if (present.count(ent.GetEditorId().Value()) == 0 || IsGone(ent)) {
      doomed.push_back({ent.GetUniqueId(), ent.GetEditorId().value});
    }
  });
  for (const std::pair< TUniqueId, uint32_t >& d : doomed) {
    log.Note(d.second, "removed (gone in the state)");
    mgr.DeleteObjectRequest(d.first);
    ++stats.removed;
  }
  std::vector< const RoomObject* > copies;
  for (const RoomObject& obj : room.objects) {
    if (now.count(TEditorId(obj.editorId).AreaNum()) == 0) {
      continue;
    }
    if (FindBlock(obj, kTagGenerated) != nullptr) {
      copies.push_back(&obj);
      continue;
    }
    CEntity* ent = mgr.ObjectById(mgr.GetIdForScript(TEditorId(obj.editorId)));
    if (ent == nullptr) {
      ++stats.unmatched;
      ++unmatchedLayers[LayerKey(obj.editorId)];
      continue;
    }
    if (ApplyObject(mgr, *ent, obj, log, waiting > 0, progress.held, &stats.fields)) {
      ++stats.changed;
    }
    ++stats.applied;
  }
  // The enemies, pickups and debris a script generator made: the rebuilt room has only their
  // templates in the generated-object buffer, so each is made again the way the generator does
  // and given the copy's saved state (ApplyObject sends the Activate the generator sent).
  int generatedCopies = 0;
  for (const RoomObject* obj : copies) {
    const CScriptObjectLoaderHelper::SGeneratedObject made =
        mgr.ScriptObjectLoaderHelper().GenerateScriptObject(TEditorId(obj->editorId), mgr);
    CEntity* ent = made.mUniqueId != kInvalidUniqueId ? mgr.ObjectById(made.mUniqueId) : nullptr;
    if (ent == nullptr) {
      ++stats.unmatched;
      continue;
    }
    NoteFor(mgr, made.mUniqueId).generated = true;
    if (ApplyObject(mgr, *ent, *obj, log, waiting > 0, progress.held, &stats.fields)) {
      ++stats.changed;
    }
    ++stats.applied;
    ++generatedCopies;
  }
  if (generatedCopies != 0) {
    std::fprintf(stderr, "[savestate] room: %d generated copies made again\n", generatedCopies);
  }
  if (log.lines > kMaxChangeLines) {
    std::fprintf(stderr, "[savestate] room: ... %d more object lines\n",
                 log.lines - kMaxChangeLines);
  }
  for (const std::pair< const uint32_t, int >& l : unmatchedLayers) {
    std::fprintf(stderr,
                 "[savestate] room: area %u layer %u: %d saved objects not in the reloaded room\n",
                 l.first >> 8, l.first & 0xff, l.second);
  }
  for (const std::pair< const uint32_t, int >& l : otherLayers) {
    std::fprintf(stderr,
                 "[savestate] room: area %u layer %u: %d live objects left as loaded (no saved "
                 "object)\n",
                 l.first >> 8, l.first & 0xff, l.second);
  }
  for (int area : now) {
    progress.done.push_back(area);
    std::fprintf(stderr, "[savestate] room: area %d restored\n", area);
  }
  if (waiting == 0) {
    StartHeldTimers(mgr, progress);
  }
  return waiting;
}

namespace {
// The override ArmSpawnLayers installed for a state load, and the live bits Begin replaced.
struct SpawnOverride {
  uint32_t world = 0;
  int area = 0;
  uint64_t spawn = 0;
  uint64_t live = 0;
  uint64_t applied = 0;
  bool swapped = false;
  CWorldLayerState* layers = nullptr; // the state Begin swapped, valid until the restore ends
};
std::vector< SpawnOverride > sOverrides;

uint64_t LayerMask(const CWorldLayerState& layers, int area) {
  uint64_t mask = 0;
  const int count = std::min(layers.GetLayerCount(TAreaId(area)), 64);
  for (int l = 0; l < count; ++l) {
    if (layers.IsLayerActive(TAreaId(area), TLayerId(l))) {
      mask |= uint64_t(1) << l;
    }
  }
  return mask;
}

void SetLayerMask(CWorldLayerState& layers, int area, uint64_t mask) {
  const int count = std::min(layers.GetLayerCount(TAreaId(area)), 64);
  for (int l = 0; l < count; ++l) {
    layers.SetLayerActive(TAreaId(area), TLayerId(l), ((mask >> l) & 1) != 0);
  }
}
} // namespace

void NoteAreaSpawnBegin(CWorldLayerState& layers, int area) {
  if (gpGameState == nullptr) {
    return;
  }
  for (SpawnOverride& o : sOverrides) {
    if (o.area == area && o.world == static_cast< uint32_t >(gpGameState->CurrentWorldAssetId()) &&
        !o.swapped) {
      o.live = LayerMask(layers, area);
      // Only layers the memory card stream also has on: one it lacks (the opening cinematic's, for a
      // state from a fresh game) would spawn its objects again, and the restore activates those the
      // saved room had that the stream lacked (TickRoom) anyway. What goes is the layers the live
      // stream switched on after the saved room was built.
      o.applied = o.spawn & o.live;
      SetLayerMask(layers, area, o.applied);
      o.swapped = true;
      o.layers = &layers;
      return;
    }
  }
}

void NoteAreaSpawnEnd(CWorldLayerState& layers, int area) {
  // The swapped bits stay until the restore ends: the area's dependency tokens and layer phases were
  // built from them, and the live bits would disagree with the objects the area spawned.
  for (const SpawnOverride& o : sOverrides) {
    if (o.area == area && o.swapped && o.layers == &layers) {
      std::fprintf(stderr,
                   "[savestate] room: area %d rebuilt with layers %llx (saved spawn %llx, stream %llx)\n",
                   area, static_cast< unsigned long long >(o.applied),
                   static_cast< unsigned long long >(o.spawn),
                   static_cast< unsigned long long >(o.live));
      return;
    }
  }
}

void ArmSpawnLayers(const RoomState& room, uint32_t worldId) {
  sOverrides.clear();
  for (const RoomArea& ra : room.areas) {
    if (ra.hasSpawn) {
      sOverrides.push_back({worldId, ra.area, ra.spawnLayers, 0, 0, false, nullptr});
    }
  }
}

void DisarmSpawnLayers() { sOverrides.clear(); }

void NoteGenerated(CStateManager& mgr, uint16_t uid) { NoteFor(mgr, RawId(uid)).generated = true; }

void NoteRemoved(CStateManager& mgr, uint16_t uid) {
  const auto it = sNoted.find(uid);
  if (it != sNoted.end() && it->second.mgr == &mgr) {
    sNoted.erase(it);
  }
}

void NoteScriptMsg(CStateManager& mgr, uint16_t dest, uint16_t sender, uint32_t msg) {
  if (!JournalsScriptMsg(msg)) {
    return;
  }
  CEntity* ent = mgr.ObjectById(RawId(dest));
  if (ent == nullptr || TCastToPtr< CPatterned >(ent) == nullptr || !IsScripted(*ent)) {
    return;
  }
  ScriptMsgEntry e;
  e.msg = msg;
  const TUniqueId from = RawId(sender);
  if (from != kInvalidUniqueId && mgr.ObjectById(from) != nullptr) {
    const TEditorId eid = mgr.GetEditorIdForUniqueId(from);
    if (eid != kInvalidEditorId) {
      e.sender = eid.value;
    }
  }
  std::vector< ScriptMsgEntry >& journal = NoteFor(mgr, ent->GetUniqueId()).msgs;
  if (journal.size() < kMaxScriptMsgs &&
      (journal.empty() || journal.back().msg != e.msg || journal.back().sender != e.sender)) {
    journal.push_back(e);
  }
}

// Read-only views of what the room save keeps, for the statedump console command
// (platform/port_statedump.cpp, tools/statecheck.py): the same members CaptureObject writes, as
// JSON members (each starting with a comma) rather than as a block.
bool IsScriptGeneratedCopy(const CStateManager& mgr, CEntity& ent) { return IsGeneratedCopy(mgr, ent); }

std::string RoomStateJson(CStateManager& mgr, CEntity& ent) {
  std::string out;
  char buf[160];
  auto add = [&](const char* fmt, auto... args) {
    std::snprintf(buf, sizeof(buf), fmt, args...);
    out += buf;
  };
  if (CScriptTimer* timer = TCastToPtr< CScriptTimer >(&ent)) {
    add(",\"timer\":%.4f,\"timing\":%d", PortRoomState::TimerTime(*timer), timer->IsTiming() ? 1 : 0);
  }
  if (CScriptCounter* counter = TCastToPtr< CScriptCounter >(&ent)) {
    add(",\"counter\":%d", counter->GetCurrent());
  }
  if (CScriptDoor* door = TCastToPtr< CScriptDoor >(&ent)) {
    add(",\"door_open\":%d,\"door_lock\":%d", door->IsOpen() ? 1 : 0,
        static_cast< int >(PortRoomState::DoorLock(*door)));
  }
  if (CScriptPlatform* plat = TCastToPtr< CScriptPlatform >(&ent)) {
    add(",\"plat_time\":%.4f,\"plat_delay\":%.4f,\"plat_flags\":%u", PortRoomState::PlatMotionTime(*plat),
        PortRoomState::PlatMoveDelay(*plat), PortRoomState::PlatFlags(*plat));
  }
  if (CScriptActorRotate* rot = TCastToPtr< CScriptActorRotate >(&ent)) {
    add(",\"rot_time\":%.4f,\"rot_playing\":%d,\"rot_actors\":%d", PortRoomState::RotTime(*rot),
        PortRoomState::RotPlaying(*rot) ? 1 : 0, static_cast< int >(PortRoomState::RotActors(*rot).size()));
  }
  if (CScriptDock* dock = TCastToPtr< CScriptDock >(&ent)) {
    const CWorld* world = mgr.GetWorld();
    const int area = dock->GetAreaId().Value();
    if (area >= 0 && area < world->GetNumAreas() &&
        dock->GetDockId() < world->GetAreaAlways(TAreaId(area)).GetDockCount()) {
      const IGameArea::Dock& d = world->GetAreaAlways(TAreaId(area)).GetDock(dock->GetDockId());
      add(",\"dock_refs\":%d", static_cast< int >(d.GetReferenceCount()));
      out += ",\"dock_load\":\"";
      for (int i = 0; i < static_cast< int >(d.GetDockRefs().size()); ++i) {
        out += d.GetShouldLoadOther(i) ? '1' : '0';
      }
      out += '"';
    }
  }
  if (const CScriptStreamedMusic* music = TCastToConstPtr< CScriptStreamedMusic >(&ent)) {
    add(",\"music\":%d", music->PortPlaying() ? 1 : 0);
  }
  if (TCastToPtr< CScriptCamera >(&ent) != nullptr) {
    const CCameraManager* cameras = mgr.CameraManager(0);
    const CCinematicCamera* cine = cameras != nullptr ? cameras->GetCinematicCamera() : nullptr;
    if (cine != nullptr && cameras->IsInCinematicCamera() && cine->GetScriptCameraId() == ent.GetUniqueId()) {
      add(",\"cine\":%.4f", cine->PortElapsed());
    }
  }
  return out;
}

} // namespace PortSaveState
