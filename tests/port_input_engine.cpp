#include "port_input_engine.h"
#include "port_test.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace PortInput;

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// SDL_GamepadButton / SDL_GamepadAxis numbering.
constexpr uint16_t kA = 0, kB = 1, kX = 2, kY = 3, kLB = 9, kRB = 10, kDUp = 11;
constexpr uint16_t kLeftX = 0, kRightX = 2;

Input Btn(uint16_t code) { return {Device::PadButton, 0, 50, code}; }
Input Axis(uint16_t code, int8_t dir) { return {Device::PadAxis, dir, 50, code}; }

Binding Bind(Action a, std::initializer_list<Input> inputs, Trigger trigger = Trigger::Press) {
  Binding b;
  b.action = a;
  for (const Input& in : inputs) {
    b.inputs[b.count++] = in;
  }
  b.trigger = trigger;
  return b;
}

struct Rig {
  Profile profile;
  Runtime runtime;
  RawState raw;
  uint16_t contexts = kCtxGameplay;
  uint16_t quiet = 0;
  double now = 1000;

  void Start() { runtime.SetProfile(&profile); }
  void Set(uint16_t button, bool down) {
    if (down) {
      raw.padButtons |= 1u << button;
    } else {
      raw.padButtons &= ~(1u << button);
    }
  }
  const Output& Step(double ms = 16) {
    now += ms;
    return runtime.Poll(raw, contexts, now, quiet);
  }
};
} // namespace

int main() {
  // A plain press: held while down, pressed/released on the edges.
  {
    Rig r;
    r.profile.bindings = {Bind(Action::PadA, {Btn(kA)})};
    r.Start();
    REQUIRE(!r.Step().Held(Action::PadA));
    r.Set(kA, true);
    const Output& o = r.Step();
    REQUIRE(o.Held(Action::PadA) && o.Pressed(Action::PadA) && Near(o.Value(Action::PadA), 1.f));
    REQUIRE(r.Step().Held(Action::PadA) && !r.runtime.Last().Pressed(Action::PadA));
    r.Set(kA, false);
    REQUIRE(!r.Step().Held(Action::PadA) && r.runtime.Last().Released(Action::PadA));
  }

  // Sticks pass through below the press threshold, one half per action.
  {
    Rig r;
    r.profile.bindings = {Bind(Action::MainRight, {Axis(kLeftX, 1)}),
                          Bind(Action::MainLeft, {Axis(kLeftX, -1)})};
    r.Start();
    r.raw.padAxes[kLeftX] = 0.2f;
    const Output& o = r.Step();
    REQUIRE(Near(o.Value(Action::MainRight), 0.2f) && Near(o.Value(Action::MainLeft), 0.f));
    REQUIRE(Near(o.Axis(Action::MainLeft, Action::MainRight), 0.2f));
    r.raw.padAxes[kLeftX] = -0.7f;
    REQUIRE(Near(r.Step().Axis(Action::MainLeft, Action::MainRight), -0.7f));
  }

  // Ordered chord LB + A: wins over A, takes LB from its own binding, and keeps
  // LB consumed until it is released. LB + B shares the held modifier.
  {
    Rig r;
    r.profile.bindings = {Bind(Action::SpringBall, {Btn(kLB)}), Bind(Action::PadA, {Btn(kA)}),
                          Bind(Action::VisorScan, {Btn(kLB), Btn(kA)}),
                          Bind(Action::VisorThermal, {Btn(kLB), Btn(kB)}),
                          Bind(Action::PadB, {Btn(kB)})};
    r.Start();
    r.Set(kLB, true);
    REQUIRE(!r.Step().Held(Action::SpringBall)); // waits the chord window
    REQUIRE(!r.Step(16).Held(Action::SpringBall));
    REQUIRE(r.Step(24).Held(Action::SpringBall)); // 40 ms after the press: fires
    r.Set(kA, true);
    const Output& o = r.Step();
    REQUIRE(o.Held(Action::VisorScan) && !o.Held(Action::PadA) && !o.Held(Action::SpringBall));
    r.Set(kA, false);
    REQUIRE(!r.Step().Held(Action::VisorScan) && !r.runtime.Last().Held(Action::SpringBall));
    r.Set(kB, true);
    REQUIRE(r.Step().Held(Action::VisorThermal) && !r.runtime.Last().Held(Action::PadB));
    r.Set(kB, false);
    r.Set(kLB, false);
    r.Step();
    // A alone still works; LB pressed and A inside the window: LB never fires.
    r.Set(kA, true);
    REQUIRE(r.Step().Held(Action::PadA));
    r.Set(kA, false);
    r.Step();
    r.Set(kLB, true);
    REQUIRE(!r.Step().Held(Action::SpringBall));
    r.Set(kA, true);
    REQUIRE(r.Step().Held(Action::VisorScan));
    for (int i = 0; i < 5; ++i) {
      REQUIRE(!r.Step().Held(Action::SpringBall) && r.runtime.Last().Held(Action::VisorScan));
    }
    r.Set(kA, false);
    r.Set(kLB, false);
    r.Step();
    // LB tapped inside the window fires for one poll on release.
    r.Set(kLB, true);
    REQUIRE(!r.Step().Held(Action::SpringBall));
    r.Set(kLB, false);
    REQUIRE(r.Step().Pressed(Action::SpringBall));
    REQUIRE(r.Step().Released(Action::SpringBall));
  }

  // The default beam chords: shift + D-pad up picks the beam and leaves the visor
  // alone; the D-pad alone still picks the visor.
  {
    Rig r;
    r.profile.bindings = {Bind(Action::VisorCombat, {Btn(kDUp)}), Bind(Action::BeamPower, {Btn(kLB), Btn(kDUp)})};
    r.Start();
    r.Set(kDUp, true);
    REQUIRE(r.Step().Pressed(Action::VisorCombat) && !r.runtime.Last().Held(Action::BeamPower)); // no lag
    r.Set(kDUp, false);
    r.Step();
    r.Set(kLB, true);
    r.Step();
    r.Set(kDUp, true);
    const Output& o = r.Step();
    REQUIRE(o.Pressed(Action::BeamPower) && !o.Held(Action::VisorCombat));
    REQUIRE(!r.Step(60).Held(Action::VisorCombat));
  }

  // Any-order chord X + Y: within the window it wins over both; outside it the
  // singles fire.
  {
    Rig r;
    Binding chord = Bind(Action::PadZ, {Btn(kX), Btn(kY)});
    chord.anyOrder = true;
    r.profile.bindings = {Bind(Action::PadX, {Btn(kX)}), Bind(Action::PadY, {Btn(kY)}), chord};
    r.Start();
    r.Set(kY, true); // the "trigger" first: order doesn't matter
    r.Step();
    r.Set(kX, true);
    const Output& o = r.Step();
    REQUIRE(o.Held(Action::PadZ) && !o.Held(Action::PadX) && !o.Held(Action::PadY));
    for (int i = 0; i < 4; ++i) {
      REQUIRE(r.Step().Held(Action::PadZ) && !r.runtime.Last().Held(Action::PadY));
    }
    r.Set(kX, false);
    r.Set(kY, false);
    r.Step();
    r.Set(kX, true);
    r.Step();
    REQUIRE(r.Step(100).Held(Action::PadX));
    r.Set(kY, true);
    REQUIRE(!r.Step().Held(Action::PadZ));
    REQUIRE(r.Step(48).Held(Action::PadY) && !r.runtime.Last().Held(Action::PadZ));
  }

  // Triggers: tap, hold, double tap, toggle.
  {
    Rig r;
    r.profile.bindings = {Bind(Action::PadB, {Btn(kB)}, Trigger::Tap),
                          Bind(Action::PadX, {Btn(kB)}, Trigger::Hold),
                          Bind(Action::PadY, {Btn(kB)}, Trigger::DoubleTap),
                          Bind(Action::SpringBall, {Btn(kA)}, Trigger::Toggle)};
    r.Start();
    r.Set(kB, true);
    REQUIRE(!r.Step().Held(Action::PadB));
    r.Set(kB, false);
    REQUIRE(r.Step(100).Pressed(Action::PadB)); // short press: tap
    REQUIRE(!r.Step().Held(Action::PadB));
    r.Set(kB, true);
    REQUIRE(r.Step(50).Held(Action::PadY)); // second press soon after a tap
    REQUIRE(!r.runtime.Last().Held(Action::PadX));
    REQUIRE(r.Step(300).Held(Action::PadX)); // held 300 ms
    r.Set(kB, false);
    REQUIRE(!r.Step().Held(Action::PadB)); // long press: no tap
    r.Set(kB, true);
    REQUIRE(!r.Step(50).Held(Action::PadY)); // the last press wasn't a tap
    r.Set(kB, false);
    r.Step();

    r.Set(kA, true);
    REQUIRE(r.Step().Pressed(Action::SpringBall));
    r.Set(kA, false);
    REQUIRE(r.Step().Held(Action::SpringBall));
    r.Set(kA, true);
    REQUIRE(r.Step().Released(Action::SpringBall));
    r.Set(kA, false);
    REQUIRE(!r.Step().Held(Action::SpringBall));
  }

  // Untoggle: a toggle turned off from outside reads as let go, and the next
  // press turns it on again.
  {
    Rig r;
    r.profile.bindings = {Bind(Action::PadL, {Btn(kA)}, Trigger::Toggle)};
    r.Start();
    r.Set(kA, true);
    REQUIRE(r.Step().Pressed(Action::PadL));
    r.Set(kA, false);
    REQUIRE(r.Step().Held(Action::PadL) && r.runtime.Toggled(Action::PadL));
    REQUIRE(!r.runtime.Toggled(Action::PadR));
    r.runtime.Untoggle(Action::PadL);
    REQUIRE(!r.runtime.Toggled(Action::PadL));
    REQUIRE(r.Step().Released(Action::PadL));
    REQUIRE(!r.Step().Held(Action::PadL));
    r.Set(kA, true);
    REQUIRE(r.Step().Pressed(Action::PadL));
  }

  // A chord taking a toggle's press undoes the flip.
  {
    Rig r;
    r.profile.chordWindowMs = 0; // no deferral: LB toggles at once
    r.profile.bindings = {Bind(Action::SpringBall, {Btn(kLB)}, Trigger::Toggle),
                          Bind(Action::VisorScan, {Btn(kLB), Btn(kA)})};
    r.Start();
    r.Set(kLB, true);
    REQUIRE(r.Step().Held(Action::SpringBall));
    r.Set(kA, true);
    REQUIRE(r.Step().Held(Action::VisorScan) && !r.runtime.Last().Held(Action::SpringBall));
    r.Set(kA, false);
    r.Set(kLB, false);
    REQUIRE(!r.Step().Held(Action::SpringBall));
  }

  // Turbo: 10 Hz = 50 ms on, 50 ms off from the first press.
  {
    Rig r;
    Binding b = Bind(Action::PadA, {Btn(kA)});
    b.turboHz = 10;
    r.profile.bindings = {b};
    r.Start();
    r.Set(kA, true);
    REQUIRE(r.Step().Held(Action::PadA));    // 0 ms
    REQUIRE(r.Step(48).Held(Action::PadA));  // 48
    REQUIRE(!r.Step(16).Held(Action::PadA)); // 64
    REQUIRE(r.Step(48).Pressed(Action::PadA)); // 112
  }

  // Context switch: the A that changed context doesn't act in the new one, a
  // held stick keeps moving, and a context binding outranks a general one.
  {
    Rig r;
    Binding map = Bind(Action::PadB, {Btn(kA)});
    map.contexts = kCtxMap;
    r.profile.bindings = {Bind(Action::PadA, {Btn(kA)}), map, Bind(Action::MainRight, {Axis(kLeftX, 1)})};
    r.Start();
    r.Set(kA, true);
    r.raw.padAxes[kLeftX] = 0.8f;
    REQUIRE(r.Step().Held(Action::PadA) && !r.runtime.Last().Held(Action::PadB));
    r.contexts = kCtxMap;
    const Output& o = r.Step();
    REQUIRE(!o.Held(Action::PadA) && !o.Held(Action::PadB) && Near(o.Value(Action::MainRight), 0.8f));
    r.Set(kA, false);
    r.Step();
    r.Set(kA, true);
    REQUIRE(r.Step().Held(Action::PadB) && !r.runtime.Last().Held(Action::PadA));
  }

  // A quiet context (the scan text's Menu, issue #103) comes and goes without
  // consuming the held input: the scan lock stays, and its Menu row joins in.
  {
    Rig r;
    Binding lock = Bind(Action::PadL, {Btn(kA)});
    lock.contexts = kCtxGameplay | kCtxScan;
    Binding menu = Bind(Action::MenuL, {Btn(kA)});
    menu.contexts = kCtxMenu | kCtxMap;
    r.profile.bindings = {lock, menu};
    r.contexts = kCtxGameplay | kCtxScan;
    r.Start();
    r.Set(kA, true);
    REQUIRE(r.Step().Held(Action::PadL));
    r.contexts |= kCtxMenu;
    r.quiet = kCtxMenu;
    REQUIRE(r.Step().Held(Action::PadL) && r.runtime.Last().Held(Action::MenuL));
    r.contexts &= ~kCtxMenu;
    REQUIRE(r.Step().Held(Action::PadL) && !r.runtime.Last().Held(Action::MenuL));
    // Not quiet: the switch consumes the held input.
    r.quiet = 0;
    r.contexts |= kCtxMenu;
    REQUIRE(!r.Step().Held(Action::PadL) && !r.runtime.Last().Held(Action::MenuL));
  }

  // Layers: holding RB switches A to its layer binding in the same poll;
  // letting RB go with A held leaves A dead until released.
  {
    Rig r;
    Binding xray = Bind(Action::VisorXray, {Btn(kA)});
    xray.contexts = kCtxLayer1;
    r.profile.bindings = {Bind(Action::PadA, {Btn(kA)}), xray, Bind(Action::Layer1, {Btn(kRB)})};
    r.Start();
    r.Set(kRB, true);
    r.Set(kA, true);
    const Output& o = r.Step();
    REQUIRE(o.Held(Action::VisorXray) && !o.Held(Action::PadA) && (r.runtime.ActiveContexts() & kCtxLayer1));
    r.Set(kRB, false);
    REQUIRE(!r.Step().Held(Action::PadA) && !r.runtime.Last().Held(Action::VisorXray));
    r.Set(kA, false);
    r.Step();
    r.Set(kA, true);
    REQUIRE(r.Step().Held(Action::PadA));
  }

  // Delta actions sum over bindings.
  {
    Rig r;
    Binding stick = Bind(Action::LookX, {Axis(kRightX, 0)});
    stick.scale = 10.f;
    r.profile.bindings = {Bind(Action::LookX, {{Device::MouseMotion, 0, 50, 0}}), stick};
    r.Start();
    r.raw.mouseDelta[0] = 3.f;
    r.raw.padAxes[kRightX] = -0.5f;
    REQUIRE(Near(r.Step().Value(Action::LookX), -2.f));
  }

  // Reset: inputs held through it stay dead until released.
  {
    Rig r;
    r.profile.bindings = {Bind(Action::PadA, {Btn(kA)}), Bind(Action::SpringBall, {Btn(kB)}, Trigger::Toggle)};
    r.Start();
    r.Set(kA, true);
    r.Set(kB, true);
    REQUIRE(r.Step().Held(Action::PadA) && r.runtime.Last().Held(Action::SpringBall));
    r.runtime.Reset();
    REQUIRE(!r.Step().Held(Action::PadA) && !r.runtime.Last().Held(Action::SpringBall));
    r.Set(kA, false);
    r.Step();
    r.Set(kA, true);
    REQUIRE(r.Step().Held(Action::PadA));
  }

  // Custom actions: values past the fixed enum, held, pressed and released like a
  // Digital action, beside a fixed one, one id each.
  {
    Rig r;
    r.profile.bindings = {Bind(CustomAction(3), {Btn(kA)}), Bind(CustomAction(7), {Btn(kB)}),
                          Bind(Action::PadB, {Btn(kX)})};
    r.Start();
    REQUIRE(!r.Step().Held(CustomAction(3)));
    r.Set(kA, true);
    const Output& a = r.Step();
    REQUIRE(a.Held(CustomAction(3)) && a.Pressed(CustomAction(3)) && Near(a.Value(CustomAction(3)), 1.f));
    REQUIRE(!a.Held(CustomAction(7)) && !a.Pressed(CustomAction(7)) && !a.Held(Action::PadB));
    int pressedCustom = 0;
    a.ForEachCustomPressed([&](Action act) {
      ++pressedCustom;
      REQUIRE(act == CustomAction(3));
    });
    REQUIRE(pressedCustom == 1);
    REQUIRE(r.Step().Held(CustomAction(3)) && !r.runtime.Last().Pressed(CustomAction(3)));
    r.Set(kB, true);
    const Output& b = r.Step();
    REQUIRE(b.Pressed(CustomAction(7)) && b.Held(CustomAction(3)) && !b.Pressed(CustomAction(3)));
    r.Set(kA, false);
    const Output& rel = r.Step();
    REQUIRE(rel.Released(CustomAction(3)) && !rel.Held(CustomAction(3)) && rel.Held(CustomAction(7)));
    REQUIRE(!rel.Released(CustomAction(7)));
    // The fixed action next to them is unaffected.
    r.Set(kX, true);
    const Output& fixed = r.Step();
    REQUIRE(fixed.Pressed(Action::PadB) && fixed.Held(CustomAction(7)) && !fixed.Pressed(CustomAction(7)));
    r.Set(kX, false);
    r.Set(kB, false);
    const Output& done = r.Step();
    REQUIRE(done.Released(CustomAction(7)) && done.Released(Action::PadB) && !done.Held(CustomAction(3)));
    REQUIRE(!done.Released(CustomAction(3)) && !done.Pressed(CustomAction(7)));
  }

  // A null profile reports nothing held or pressed, the customs as the fixed ones.
  {
    Rig r;
    r.profile.bindings = {Bind(CustomAction(3), {Btn(kA)}), Bind(Action::PadA, {Btn(kA)})};
    r.Start();
    r.Set(kA, true);
    REQUIRE(r.Step().Held(CustomAction(3)) && r.runtime.Last().Held(Action::PadA));
    r.runtime.SetProfile(nullptr);
    const Output& none = r.Step();
    REQUIRE(!none.Held(CustomAction(3)) && !none.Pressed(CustomAction(3)) && !none.Held(Action::PadA));
    // The customs report exactly what the fixed actions do here.
    REQUIRE(none.Released(CustomAction(3)) == none.Released(Action::PadA));
  }

  // Action keys round-trip.
  REQUIRE(ActionFromKey("visor_scan") == Action::VisorScan);
  REQUIRE(ActionFromKey("nope") == Action::None);
  REQUIRE(Info(Action::LookX).kind == ActionKind::Delta);
  REQUIRE(IsCustom(CustomAction(1)) && !IsCustom(Action::PadA) && CustomId(Action::PadA) == 0);

  std::puts("port_input_engine: all checks passed");
  return 0;
}
