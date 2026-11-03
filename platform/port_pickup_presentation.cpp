// Seamless pickups: the marks CScriptPickup::Touch leaves on its chain's presentation targets
// (port_pickup_presentation.h).

#include "port_pickup_presentation.h"

#include "MetroidPrime/Player/CGameState.hpp"

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

} // namespace PortPickupPresentation
