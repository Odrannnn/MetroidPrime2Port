// Seamless pickups: the marks CScriptPickup::Touch leaves on its chain's presentation targets
// (port_pickup_presentation.h).

#include "port_pickup_presentation.h"

#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/ScriptObjects/CScriptSpecialFunction.hpp"
#include "MetroidPrime/ScriptObjects/CScriptTimer.hpp"

#include <map>
#include <utility>

namespace PortPickupPresentation {
namespace {

// (kind, unique id) -> play time the mark expires at.
std::map< std::pair< int, uint16_t >, double > sMarks;

double Now() { return gpGameState != nullptr ? gpGameState->GetTotalPlayTime() : 0.0; }

} // namespace

void Mark(Kind kind, uint16_t uniqueId) { sMarks[std::make_pair(int(kind), uniqueId)] = Now() + kMarkSeconds; }

bool Take(Kind kind, uint16_t uniqueId) {
  if (sMarks.empty()) {
    return false;
  }
  const double now = Now();
  for (std::map< std::pair< int, uint16_t >, double >::iterator it = sMarks.begin(); it != sMarks.end();) {
    // Expired, or from another game (play time went back): drop.
    if (now > it->second || now < it->second - kMarkSeconds) {
      it = sMarks.erase(it);
    } else {
      ++it;
    }
  }
  return sMarks.erase(std::make_pair(int(kind), uniqueId)) != 0;
}

namespace {

// One of ent's state connections sends Action to a map-station special function.
bool ActsOnMapStation(CStateManager& mgr, const CEntity& ent, EScriptObjectState state) {
  for (rstl::vector< SConnection >::const_iterator it = ent.GetConnectionList().begin();
       it != ent.GetConnectionList().end(); ++it) {
    if (it->state != state || it->msg != kSM_Action) {
      continue;
    }
    CStateManager::TIdListResult found = mgr.GetIdListForScript(it->objId);
    for (CStateManager::TIdList::const_iterator c = found.first; c != found.second; ++c) {
      const CScriptSpecialFunction* func =
          dynamic_cast< const CScriptSpecialFunction* >(mgr.GetObjectById(c->second));
      if (func != nullptr && func->GetFunction() == CScriptSpecialFunction::kSF_MapStation) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

bool IsMapStationMemo(CStateManager& mgr, const CScriptMsg& msg) {
  const CEntity* sender = mgr.GetObjectById(msg.GetSenderId());
  if (sender == nullptr || msg.GetState() == kSS_InvalidState) {
    return false;
  }
  if (ActsOnMapStation(mgr, *sender, msg.GetState())) {
    return true;
  }
  // The sender's same-state connections to timers whose expiry triggers the map station.
  for (rstl::vector< SConnection >::const_iterator it = sender->GetConnectionList().begin();
       it != sender->GetConnectionList().end(); ++it) {
    if (it->state != msg.GetState()) {
      continue;
    }
    CStateManager::TIdListResult found = mgr.GetIdListForScript(it->objId);
    for (CStateManager::TIdList::const_iterator c = found.first; c != found.second; ++c) {
      const CEntity* next = mgr.GetObjectById(c->second);
      if (dynamic_cast< const CScriptTimer* >(next) != nullptr &&
          ActsOnMapStation(mgr, *next, kSS_Zero)) {
        return true;
      }
    }
  }
  return false;
}

} // namespace PortPickupPresentation
