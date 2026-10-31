#pragma once

// Debugging aid behind the `statedump <file>` console command: every live object's state as JSON
// lines, for the save-state round-trip check (tools/statecheck.py). Nothing here runs unless the
// command is called.

#include <string>

class CStateManager;

namespace PortStateDump {
// Writes one JSON object per line to `path`: a "player" line, then one "obj" line per entity of
// the all-objects list. Returns the number of objects, or -1 (with `error` set) when the file
// can't be written.
int Write(CStateManager& mgr, const std::string& path, std::string& error);
} // namespace PortStateDump
