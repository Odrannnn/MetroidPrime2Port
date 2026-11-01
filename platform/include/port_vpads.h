#pragma once

#include <string>

// SDL virtual gamepads for headless controller-to-port tests (issue #45): `MP2_VPADS=<n>` (1-4)
// attaches n virtual gamepads at start, the console's `vpad` drives them. Off by default; the
// controllers go through Aurora's normal add/assign path, unlike MP2_INPUT which writes pad ports.
namespace PortVpads {

// Called once per loop iteration on the main thread; attaches the MP2_VPADS pads on the first call.
void Poll();

// `vpad` console command: `attach [n]`, `detach <i>`, `<i> <button|axis> <value>`, `list`.
// Returns false and sets `err` on bad input.
bool Command(const std::string& args, std::string& out, std::string& err);

// One button or axis of virtual pad `slot` (the `vpad <i> <name> <value>` names).
bool Set(int slot, const std::string& what, double value, std::string& err);
// The gyro of virtual pad `slot`, rad/s (positive yaw turns right, positive pitch looks up); held
// until changed.
bool SetGyro(int slot, double yaw, double pitch, std::string& err);
bool Present(int slot);
bool AttachSlot(int slot, std::string& err);
bool DetachSlot(int slot, std::string& err);

} // namespace PortVpads
