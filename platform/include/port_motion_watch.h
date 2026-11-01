#pragma once

#include "MetroidPrime/CStateManager.hpp"

class CPlayer;
class CVector3f;

// Permanent, near-free motion diagnostic. A ring of the player's last ticks (position and
// velocity before and after the mover, the impulse it was fed, which mover ran, knockbacks) that
// is written to stderr / the port log as `[motion]` lines when the player moves impossibly far or
// fast in one tick, or jumps between ticks outside CGameCollision::MovePlayer. See
// docs/GOTCHAS.md.
namespace PortMotionWatch {

enum class Branch : unsigned char {
  None,
  GroundNew, // MoveGroundCollider_New
  Ball,      // standard collider (morph ball) MoveAndCollide
  Air,       // airborne MoveAndCollide
};

using ColliderList = rstl::reserved_vector< TUniqueId, 1024 >;

// One player MovePlayer call. Construct at the top, Set() the branch where it is chosen; the
// destructor records the tick and decides whether to dump.
class TickScope {
public:
  TickScope(CStateManager& mgr, CPlayer* player, const ColliderList* colliderList);
  ~TickScope();
  TickScope(const TickScope&) = delete;
  TickScope& operator=(const TickScope&) = delete;
  void Set(Branch branch) { mBranch = branch; }

private:
  CPlayer* mPlayer;
  Branch mBranch = Branch::None;
};

// CStateManager::ApplyKnockBack hit the player: `source` is the damaging object.
void NoteKnockback(const CStateManager& mgr, TUniqueId source, const CVector3f& dir, float power);
// A legitimate warp (CPlayer::Teleport): the next tick's jump is tagged `warp` and not dumped.
void NoteWarp();

} // namespace PortMotionWatch
