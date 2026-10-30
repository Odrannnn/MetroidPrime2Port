#include "port_input_engine.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace PortInput {

namespace {

constexpr ActionInfo kActions[] = {
#define PORT_INPUT_ACTION_INFO(id, kind, key, label) {ActionKind::kind, key, label},
    PORT_INPUT_ACTIONS(PORT_INPUT_ACTION_INFO)
#undef PORT_INPUT_ACTION_INFO
};
static_assert(sizeof(kActions) / sizeof(kActions[0]) == kActionCount);

float Directed(float v, int8_t dir) {
  if (dir > 0) {
    return std::max(v, 0.f);
  }
  if (dir < 0) {
    return std::max(-v, 0.f);
  }
  return v;
}

bool Bit(uint64_t bits, uint16_t code, int count) { return code < count && ((bits >> code) & 1) != 0; }

} // namespace

const ActionInfo& Info(Action a) {
  static constexpr ActionInfo kCustom{ActionKind::Digital, "custom", "Custom action"};
  if (IsCustom(a)) {
    return kCustom;
  }
  const auto i = size_t(a);
  return kActions[i < size_t(kActionCount) ? i : 0];
}

Action ActionFromKey(std::string_view key) {
  for (int i = 1; i < kActionCount; ++i) {
    if (kActions[i].key == key) {
      return Action(i);
    }
  }
  // A custom action: "custom:<decimal id>".
  constexpr std::string_view kCustomPrefix = "custom:";
  if (key.size() > kCustomPrefix.size() && key.substr(0, kCustomPrefix.size()) == kCustomPrefix) {
    std::string_view digits = key.substr(kCustomPrefix.size());
    if (digits.size() > 5) {
      return Action::None;
    }
    long id = 0;
    for (char c : digits) {
      if (c < '0' || c > '9') {
        return Action::None;
      }
      id = id * 10 + (c - '0');
    }
    if (id < 1 || id > kMaxCustomId) {
      return Action::None;
    }
    return CustomAction(uint16_t(id));
  }
  return Action::None;
}

std::string ActionKey(Action a) {
  if (IsCustom(a)) {
    return "custom:" + std::to_string(CustomId(a));
  }
  return std::string(Info(a).key);
}

bool Input::Analog() const {
  switch (device) {
  case Device::MouseWheel:
  case Device::MouseMotion:
  case Device::PadAxis:
  case Device::TouchAxis:
  case Device::Gyro:
    return true;
  default:
    return false;
  }
}

float RawState::Value(const Input& in) const {
  switch (in.device) {
  case Device::Key:
    return in.code < kKeyCount && keys[in.code] ? 1.f : 0.f;
  case Device::MouseButton:
    return Bit(mouseButtons, in.code, kMouseButtonCount) ? 1.f : 0.f;
  case Device::MouseWheel:
    return in.code < 2 ? Directed(wheel[in.code], in.dir) : 0.f;
  case Device::MouseMotion:
    return in.code < 2 ? Directed(mouseDelta[in.code], in.dir) : 0.f;
  case Device::PadButton:
    return Bit(padButtons, in.code, kPadButtonCount) ? 1.f : 0.f;
  case Device::PadAxis:
    return in.code < kPadAxisCount ? Directed(padAxes[in.code], in.dir) : 0.f;
  case Device::Touch:
    return in.code < kTouchCount && touch[in.code] ? 1.f : 0.f;
  case Device::TouchAxis:
    return in.code < kTouchAxisCount ? Directed(touchAxes[in.code], in.dir) : 0.f;
  case Device::Gyro:
    return in.code < 3 ? Directed(gyro[in.code], in.dir) : 0.f;
  case Device::None:
    break;
  }
  return 0.f;
}

bool RawState::Down(const Input& in) const {
  const float v = Value(in);
  if (!in.Analog()) {
    return v > 0.5f;
  }
  return std::fabs(v) >= float(std::max<uint8_t>(in.threshold, 1)) / 100.f;
}

bool Binding::Uses(const Input& in) const {
  for (int i = 0; i < count; ++i) {
    if (inputs[i] == in) {
      return true;
    }
  }
  return false;
}

float Output::Axis(Action neg, Action pos) const {
  return std::clamp(Value(pos) - Value(neg), -1.f, 1.f);
}

void Runtime::SetProfile(const Profile* profile) {
  mProfile = profile;
  Rebuild();
}

void Runtime::Reset() { mResetPending = true; }

void Runtime::Rebuild() {
  mInputs.clear();
  mBindings.clear();
  mSupersets.clear();
  mPasses[0].clear();
  mPasses[1].clear();
  // What was held stays held for the next poll's edges: a rebuild with a key
  // down must not read as a fresh press (MP1 issue #235, the F1 overlay flipping
  // every frame while F1 was held).
  const auto held = mOut.held;
  const auto customHeld = mOut.customHeld;
  mOut = {};
  mOut.held = held;
  mOut.customHeld = customHeld;
  mHavePolled = false;
  if (mProfile == nullptr) {
    return;
  }
  const auto& bindings = mProfile->bindings;
  // The output's custom vectors cover the ids the profile binds.
  uint16_t maxCustom = 0;
  for (const Binding& b : bindings) {
    if (IsCustom(b.action)) {
      maxCustom = std::max(maxCustom, CustomId(b.action));
    }
  }
  if (maxCustom != 0) {
    mOut.customHeld.resize(std::max(mOut.customHeld.size(), size_t(maxCustom) + 1), 0);
    mOut.customPressed.assign(size_t(maxCustom) + 1, 0);
    mOut.customReleased.assign(size_t(maxCustom) + 1, 0);
  }
  mBindings.resize(bindings.size());
  mSupersets.resize(bindings.size());
  for (size_t bi = 0; bi < bindings.size(); ++bi) {
    const Binding& b = bindings[bi];
    for (int k = 0; k < b.count && k < kMaxChord; ++k) {
      const Input& in = b.inputs[k];
      auto it = std::find_if(mInputs.begin(), mInputs.end(), [&](const InputState& s) { return s.input == in; });
      if (it == mInputs.end()) {
        mInputs.push_back({in});
        it = mInputs.end() - 1;
      }
      mBindings[bi].inputs[k] = int(it - mInputs.begin());
    }
  }
  // Chords this binding could still grow into: its inputs are all modifiers of
  // an ordered chord, or all members of an any-order one.
  for (size_t bi = 0; bi < bindings.size(); ++bi) {
    const Binding& b = bindings[bi];
    if (b.count == 0 || Passthrough(b)) {
      continue;
    }
    for (size_t ci = 0; ci < bindings.size(); ++ci) {
      const Binding& c = bindings[ci];
      if (ci == bi || c.count <= b.count || Passthrough(c)) {
        continue;
      }
      const int pool = c.anyOrder ? c.count : c.count - 1;
      bool subset = true;
      for (int k = 0; k < b.count && subset; ++k) {
        subset = std::find(c.inputs.begin(), c.inputs.begin() + pool, b.inputs[k]) != c.inputs.begin() + pool;
      }
      if (subset) {
        mSupersets[bi].push_back(int(ci));
      }
    }
  }
  // Layer bindings run first so the layers they hold apply this poll; within a
  // pass, larger chords resolve before their members.
  for (size_t bi = 0; bi < bindings.size(); ++bi) {
    const Action a = bindings[bi].action;
    const bool layer = a >= Action::Layer1 && a <= Action::Layer4;
    mPasses[layer ? 0 : 1].push_back(int(bi));
  }
  // A layer binding outranks one for a context, which outranks one that
  // applies everywhere: the higher rank claims its inputs for the poll.
  for (size_t bi = 0; bi < bindings.size(); ++bi) {
    const uint16_t ctx = bindings[bi].contexts;
    mBindings[bi].rank = (ctx & kCtxLayers) != 0 && (ctx & ~kCtxLayers) == 0 ? 2 : ctx != kCtxAll ? 1 : 0;
  }
  for (auto& pass : mPasses) {
    std::stable_sort(pass.begin(), pass.end(), [&](int a, int b) {
      if (mBindings[a].rank != mBindings[b].rank) {
        return mBindings[a].rank > mBindings[b].rank;
      }
      return bindings[a].count > bindings[b].count;
    });
  }
}

bool Runtime::Passthrough(const Binding& b) {
  if (b.count == 0) {
    return false;
  }
  const ActionKind kind = Info(b.action).kind;
  return kind == ActionKind::Delta ||
         (kind == ActionKind::HalfAxis && b.trigger == Trigger::Press && b.turboHz == 0 &&
          b.inputs[b.count - 1].Analog());
}

bool Runtime::MembersFree(int bi) const {
  const Binding& b = mProfile->bindings[bi];
  const BindingState& s = mBindings[bi];
  for (int k = 0; k < b.count; ++k) {
    if (mInputs[s.inputs[k]].claimRank > s.rank) {
      return false;
    }
    const int c = mInputs[s.inputs[k]].consumedBy;
    if (c == -1 || c == bi) {
      continue;
    }
    // Ordered chords share their modifiers: LB + A and LB + B both work while
    // LB stays held.
    if (c >= 0 && b.IsChord() && !b.anyOrder && k != b.count - 1) {
      continue;
    }
    return false;
  }
  return true;
}

void Runtime::Claim(int bi) {
  const Binding& b = mProfile->bindings[bi];
  for (int k = 0; k < b.count; ++k) {
    int8_t& r = mInputs[mBindings[bi].inputs[k]].claimRank;
    r = std::max(r, mBindings[bi].rank);
  }
}

void Runtime::Consume(int bi) {
  const Binding& b = mProfile->bindings[bi];
  for (int k = 0; k < b.count; ++k) {
    int& c = mInputs[mBindings[bi].inputs[k]].consumedBy;
    if (c == -1) {
      c = bi;
    }
  }
}

bool Runtime::Deferrable(int bi, uint16_t contexts) const {
  if (mProfile->chordWindowMs == 0) {
    return false;
  }
  for (int ci : mSupersets[bi]) {
    if (InContext(mProfile->bindings[ci], contexts)) {
      return true;
    }
  }
  return false;
}

// Whether the binding's input expression is down this poll, after the chord rules.
bool Runtime::Evaluate(int bi, double nowMs, uint16_t contexts) {
  const Binding& b = mProfile->bindings[bi];
  BindingState& s = mBindings[bi];
  if (b.count == 0 || !InContext(b, contexts)) {
    s.active = s.pending = false;
    return false;
  }
  bool allDown = true;
  for (int k = 0; k < b.count; ++k) {
    allDown = allDown && mInputs[s.inputs[k]].down;
  }
  const bool free = MembersFree(bi);
  if (s.active) {
    if (allDown && free) {
      if (b.IsChord()) {
        Consume(bi);
      }
      return true;
    }
    s.active = false;
    s.cancelled = !free;
    return false;
  }
  if (s.pending) {
    if (!free) {
      s.pending = false;
      s.cancelled = true;
      return false;
    }
    if (allDown) {
      if (nowMs - s.pendingSince >= mProfile->chordWindowMs) {
        s.pending = false;
        s.active = true;
        return true;
      }
      return false;
    }
    s.pending = false; // let go inside the window: fire once
    return true;
  }
  if (!allDown || !free) {
    return false;
  }
  bool edge = false;
  if (b.IsChord()) {
    if (b.anyOrder) {
      double first = nowMs;
      double last = -1e300;
      for (int k = 0; k < b.count; ++k) {
        const InputState& in = mInputs[s.inputs[k]];
        edge = edge || in.edge;
        first = std::min(first, in.pressedAt);
        last = std::max(last, in.pressedAt);
      }
      if (!edge || last - first > mProfile->chordWindowMs) {
        return false;
      }
    } else if (!mInputs[s.inputs[b.count - 1]].edge) {
      return false;
    }
    Consume(bi);
    s.active = true;
    return true;
  }
  edge = mInputs[s.inputs[0]].edge;
  if (edge && Deferrable(bi, contexts)) {
    s.pending = true;
    s.pendingSince = nowMs;
    return false;
  }
  s.active = true;
  return true;
}

// The trigger (tap, hold, ...) and turbo on top of the expression; returns the
// binding's value this poll.
float Runtime::RunTrigger(int bi, bool expr, const RawState& raw, double nowMs) {
  const Binding& b = mProfile->bindings[bi];
  BindingState& s = mBindings[bi];
  const bool on = expr && !s.exprWas;
  const bool off = !expr && s.exprWas;
  if (on) {
    s.exprSince = nowMs;
    s.doubleActive = nowMs - s.lastRelease <= b.doubleMs && s.lastPressDur < b.tapMs;
    s.toggledThisPress = false;
    if (b.trigger == Trigger::Toggle) {
      s.toggled = !s.toggled;
      s.toggledThisPress = true;
    }
  }
  bool tapped = false;
  if (off) {
    const double dur = nowMs - s.exprSince;
    tapped = !s.cancelled && dur < b.tapMs;
    s.lastPressDur = s.cancelled ? 1e9 : dur;
    s.lastRelease = nowMs;
    s.doubleActive = false;
  }
  if (s.cancelled && s.toggledThisPress) {
    s.toggled = !s.toggled; // a chord took the press: undo its flip
    s.toggledThisPress = false;
  }
  if (!expr) {
    s.cancelled = false;
  }
  s.exprWas = expr;

  bool out = false;
  switch (b.trigger) {
  case Trigger::Press:
    out = expr;
    break;
  case Trigger::Tap:
    out = tapped;
    break;
  case Trigger::Hold:
    out = expr && nowMs - s.exprSince >= b.holdMs;
    break;
  case Trigger::DoubleTap:
    out = expr && s.doubleActive;
    break;
  case Trigger::Toggle:
    out = s.toggled;
    break;
  }
  if (out && !s.outWas) {
    s.outSince = nowMs;
  }
  s.outWas = out;
  if (!out) {
    return 0.f;
  }
  if (b.turboHz != 0) {
    // The epsilon keeps a phase that lands exactly on a poll (the clock sums
    // tick periods) from rounding down a poll late.
    const auto phase = int64_t(std::floor((nowMs - s.outSince) * b.turboHz * 2.0 / 1000.0 + 1e-6));
    if (phase % 2 != 0) {
      return 0.f;
    }
  }
  const Input& trig = b.inputs[b.count - 1];
  float v = b.trigger == Trigger::Press && trig.Analog() ? std::fabs(raw.Value(trig)) : 1.f;
  v *= b.scale;
  return b.invert ? -v : v;
}

void Runtime::ConsumeChangedOnContextSwitch(uint16_t before, uint16_t after) {
  const auto& bindings = mProfile->bindings;
  // The action values that apply in each context: sorted, unique, so a custom
  // action's value compares like a fixed one's.
  std::vector<uint16_t> was, now;
  for (size_t i = 0; i < mInputs.size(); ++i) {
    InputState& in = mInputs[i];
    if (!in.down || in.edge || in.consumedBy != -1) {
      continue;
    }
    was.clear();
    now.clear();
    for (const Binding& b : bindings) {
      if (!b.Uses(in.input)) {
        continue;
      }
      if (InContext(b, before)) {
        was.push_back(uint16_t(b.action));
      }
      if (InContext(b, after)) {
        now.push_back(uint16_t(b.action));
      }
    }
    const auto sortUnique = [](std::vector<uint16_t>& v) {
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    sortUnique(was);
    sortUnique(now);
    if (was != now) {
      in.consumedBy = kConsumedAll;
    }
  }
}

const Output& Runtime::Poll(const RawState& raw, uint16_t contexts, double nowMs, uint16_t quiet) {
  const auto prevHeld = mOut.held;
  const auto prevCustom = mOut.customHeld; // this poll's edges compare against it
  mOut.value.fill(0.f);
  mOut.held.reset();
  std::fill(mOut.customHeld.begin(), mOut.customHeld.end(), uint8_t(0));
  if (mProfile == nullptr) {
    mOut.pressed.reset();
    mOut.released = prevHeld;
    mOut.customPressed.assign(prevCustom.size(), 0);
    mOut.customReleased = prevCustom;
    return mOut;
  }
  const auto& bindings = mProfile->bindings;
  for (InputState& in : mInputs) {
    const bool down = raw.Down(in.input);
    in.edge = down && !in.down;
    if (in.edge) {
      in.pressedAt = nowMs;
    }
    if (!down) {
      in.consumedBy = -1;
    }
    in.down = down;
    in.claimRank = -1;
  }
  if (mResetPending) {
    mResetPending = false;
    for (InputState& in : mInputs) {
      in.consumedBy = in.down ? kConsumedAll : -1;
      in.edge = false;
    }
    for (BindingState& s : mBindings) {
      BindingState fresh;
      std::copy(std::begin(s.inputs), std::end(s.inputs), fresh.inputs);
      s = fresh;
    }
  }

  auto apply = [&](int bi, float v) {
    const Binding& b = bindings[bi];
    if (IsCustom(b.action)) {
      // Custom actions are Digital: held while the trigger's output is on.
      if (v != 0.f) {
        const uint16_t id = CustomId(b.action);
        if (id >= mOut.customHeld.size()) {
          mOut.customHeld.resize(size_t(id) + 1, 0);
        }
        mOut.customHeld[id] = 1;
      }
      return;
    }
    const auto a = size_t(b.action);
    if (Info(b.action).kind == ActionKind::Delta) {
      mOut.value[a] += v;
      if (v != 0.f) {
        mOut.held.set(a);
      }
      return;
    }
    if (std::fabs(v) > std::fabs(mOut.value[a])) {
      mOut.value[a] = v;
    }
    if (v != 0.f) {
      mOut.held.set(a);
    }
  };
  auto runPass = [&](int pass, uint16_t ctx) {
    for (int bi : mPasses[pass]) {
      const Binding& b = bindings[bi];
      if (Passthrough(b)) {
        // Analog moves skip the chord rules and the threshold: modifiers held,
        // the last input's value as it is.
        bool mods = InContext(b, ctx);
        for (int k = 0; k + 1 < b.count && mods; ++k) {
          mods = mInputs[mBindings[bi].inputs[k]].down;
        }
        const int c = mInputs[mBindings[bi].inputs[b.count - 1]].consumedBy;
        if (mods && (c == -1 || c == bi) && MembersFree(bi)) {
          float v = raw.Value(b.inputs[b.count - 1]);
          if (Info(b.action).kind == ActionKind::HalfAxis) {
            v = std::fabs(v);
          }
          v *= b.scale;
          if (v != 0.f) {
            Claim(bi);
          }
          apply(bi, b.invert ? -v : v);
        }
        continue;
      }
      const bool expr = Evaluate(bi, nowMs, ctx);
      if (expr || mBindings[bi].pending) {
        Claim(bi);
      }
      apply(bi, RunTrigger(bi, expr, raw, nowMs));
    }
  };

  runPass(0, contexts);
  uint16_t active = contexts;
  for (int l = 0; l < 4; ++l) {
    if (mOut.held[size_t(Action::Layer1) + l]) {
      active |= uint16_t(kCtxLayer1 << l);
    }
  }
  if (mHavePolled && (active & ~quiet) != (mContexts & ~quiet)) {
    ConsumeChangedOnContextSwitch(mContexts & ~quiet, active & ~quiet);
  }
  mContexts = active;
  mHavePolled = true;
  runPass(1, active);

  mOut.pressed = mOut.held & ~prevHeld;
  mOut.released = prevHeld & ~mOut.held;
  // Custom actions: the same edges, from the custom ids held last poll.
  const size_t customCount = std::max(prevCustom.size(), mOut.customHeld.size());
  mOut.customHeld.resize(customCount, 0);
  mOut.customPressed.assign(customCount, 0);
  mOut.customReleased.assign(customCount, 0);
  for (size_t id = 0; id < customCount; ++id) {
    const bool was = id < prevCustom.size() && prevCustom[id] != 0;
    const bool now = mOut.customHeld[id] != 0;
    mOut.customPressed[id] = uint8_t(!was && now);
    mOut.customReleased[id] = uint8_t(was && !now);
  }
  return mOut;
}

} // namespace PortInput
