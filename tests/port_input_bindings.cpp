#include "port_input_bindings.h"
#include "port_test.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

using namespace PortInput;

namespace {

Input In(Device d, uint16_t code, int8_t dir = 0, uint8_t threshold = 50) { return {d, dir, threshold, code}; }

Binding Bind(Action a, std::initializer_list<Input> inputs) {
  Binding b;
  b.action = a;
  for (const Input& in : inputs)
    b.inputs[b.count++] = in;
  return b;
}

bool RoundTrips(const Input& in) {
  Input back;
  return InputFromText(InputToText(in), back) && back == in;
}

std::string FakeKeyName(int code) { return code == 44 ? "space" : code == 30 ? "1" : code == 31 ? "@" : ""; }
int FakeKeyParse(std::string_view name) {
  return name == "space" ? 44 : name == "1" ? 30 : name == "@" ? 31 : -1;
}

int ErrorLine(const char* text) {
  UserBindings out;
  std::string error;
  if (ParseUserBindings(text, out, &error))
    return 0;
  return std::atoi(error.c_str() + 5); // "line N: ..."
}

void TestInputText() {
  for (int d = int(Device::Key); d <= int(Device::Gyro); ++d)
    for (uint16_t code : {0, 1, 2})
      for (int8_t dir : {0, 1, -1}) {
        Device device = Device(d);
        if ((device == Device::MouseWheel || device == Device::MouseMotion) && code > 1)
          continue;
        bool directed = device == Device::MouseWheel || device == Device::MouseMotion ||
                        device == Device::PadAxis || device == Device::TouchAxis || device == Device::Gyro;
        REQUIRE(RoundTrips(In(device, code, directed ? dir : 0)));
        REQUIRE(RoundTrips(In(device, code, directed ? dir : 0, 75)));
      }
  for (uint16_t code = 0; code < kMouseButtonCount; ++code)
    REQUIRE(RoundTrips(In(Device::MouseButton, code)));
  for (uint16_t code = 0; code < kPadButtonCount; ++code)
    REQUIRE(RoundTrips(In(Device::PadButton, code)));
  for (uint16_t code = 0; code < kPadAxisCount; ++code)
    REQUIRE(RoundTrips(In(Device::PadAxis, code, -1)));
  REQUIRE(RoundTrips(In(Device::Key, kKeyCount - 1)));
  REQUIRE(RoundTrips(In(Device::Touch, kTouchCount - 1)));

  REQUIRE(InputToText(In(Device::PadButton, 9)) == "pad:leftshoulder");
  REQUIRE(InputToText(In(Device::PadButton, 16)) == "pad:paddle1");
  REQUIRE(InputToText(In(Device::PadButton, 30)) == "pad:30");
  REQUIRE(InputToText(In(Device::PadAxis, 5, 1)) == "axis:righttrigger+");
  REQUIRE(InputToText(In(Device::PadAxis, 2)) == "axis:rightx");
  REQUIRE(InputToText(In(Device::MouseButton, 8)) == "mouse:left");
  REQUIRE(InputToText(In(Device::MouseButton, 2)) == "mouse:aim:right");
  REQUIRE(InputToText(In(Device::MouseButton, 17)) == "mouse:menu:middle");
  REQUIRE(InputToText(In(Device::MouseButton, 28)) == "mouse:held:x2");
  REQUIRE(InputToText(In(Device::MouseWheel, 0, 1)) == "wheel:up");
  REQUIRE(InputToText(In(Device::MouseWheel, 1, -1)) == "wheel:left");
  REQUIRE(InputToText(In(Device::MouseMotion, 1)) == "motion:y");
  REQUIRE(InputToText(In(Device::Gyro, 1, -1)) == "gyro:yaw-");
  REQUIRE(InputToText(In(Device::TouchAxis, 3, 1, 20)) == "touchaxis:3+@20");
  REQUIRE(InputToText(In(Device::Key, 44)) == "key:44");

  Input in;
  REQUIRE(InputFromText("axis:lefttrigger+@30", in) && in == In(Device::PadAxis, 4, 1, 30));
  REQUIRE(InputFromText(" pad:a ", in) && in == In(Device::PadButton, 0));
  REQUIRE(!InputFromText("pad:a@101", in));
  REQUIRE(!InputFromText("pad:nope", in));
  REQUIRE(!InputFromText("pad:32", in));
  REQUIRE(!InputFromText("touch:128", in));
  REQUIRE(InputFromText("touch:127", in) && in == In(Device::Touch, 127));
  REQUIRE(!InputFromText("key:space", in)); // no key names installed
  REQUIRE(!InputFromText("bogus:1", in));
  REQUIRE(!InputFromText("pad:", in));
  REQUIRE(!InputFromText("gyro:3", in));

  SetKeyNames(FakeKeyName, FakeKeyParse);
  REQUIRE(InputToText(In(Device::Key, 44)) == "key:space");
  REQUIRE(InputToText(In(Device::Key, 45)) == "key:45");
  REQUIRE(InputFromText("key:space", in) && in == In(Device::Key, 44));
  REQUIRE(InputFromText("key:45", in) && in == In(Device::Key, 45));
  REQUIRE(!InputFromText("key:enter", in));
  REQUIRE(InputFromText("key:1", in) && in == In(Device::Key, 30)); // the name wins
  REQUIRE(InputFromText("key:@", in) && in == In(Device::Key, 31));
  REQUIRE(InputFromText("key:@@20", in) && in == In(Device::Key, 31, 0, 20));
  REQUIRE(RoundTrips(In(Device::Key, 31)) && RoundTrips(In(Device::Key, 30, 0, 75)));
  SetKeyNames(nullptr, nullptr);

  REQUIRE(InputToText(In(Device::Touch, 3)) == "touch:a");
  REQUIRE(InputToText(In(Device::Touch, 28)) == "touch:tturbo");
  REQUIRE(InputToText(In(Device::Touch, 32)) == "touch:beam_shift");
  REQUIRE(InputToText(In(Device::Touch, 35)) == "touch:35");
  REQUIRE(InputToText(In(Device::Touch, 40)) == "touch:bbomb");
  REQUIRE(InputToText(In(Device::Touch, 44)) == "touch:bunmorph");
  REQUIRE(InputToText(In(Device::Touch, 45)) == "touch:45");
  REQUIRE(InputToText(In(Device::Touch, 63)) == "touch:63");
  REQUIRE(InputFromText("touch:missile", in) && in == In(Device::Touch, 20));
  REQUIRE(InputFromText("touch:map_tap", in) && in == In(Device::Touch, 34));
  REQUIRE(InputFromText("touch:5", in) && in == In(Device::Touch, 5));
  REQUIRE(!InputFromText("touch:nope", in));
  for (uint16_t code = 0; code < kTouchCount; ++code)
    REQUIRE(RoundTrips(In(Device::Touch, code)));
}

void TestContexts() {
  REQUIRE(ContextsToText(kCtxAll) == "all");
  REQUIRE(ContextsToText(kCtxGameplay | kCtxMap) == "gameplay,map");
  REQUIRE(ContextsToText(kCtxLayer2) == "layer2");
  uint16_t c = 0;
  REQUIRE(ContextFromText("morphball", c) && c == kCtxMorphBall);
  REQUIRE(ContextFromText("all", c) && c == kCtxAll);
  REQUIRE(!ContextFromText("nowhere", c));
}

void TestRoundTrip() {
  UserBindings in;
  UserProfile base;
  base.name = "Default \"mine\" \\ here";
  base.unbind = {{Action::PadZ, Family::Pad}, {Action::Screenshot, Family::Keyboard}};
  Binding chord = Bind(Action::BeamWave, {In(Device::PadButton, 9), In(Device::PadButton, 14)});
  base.bindings.push_back(chord);
  Binding any = Bind(Action::SaveState, {In(Device::Key, 224), In(Device::Key, 22), In(Device::Key, 4)});
  any.anyOrder = true;
  any.contexts = kCtxGameplay | kCtxMorphBall;
  base.bindings.push_back(any);
  for (Trigger t : {Trigger::Tap, Trigger::Hold, Trigger::DoubleTap, Trigger::Toggle}) {
    Binding b = Bind(Action::PadA, {In(Device::MouseButton, 10)});
    b.trigger = t;
    b.tapMs = 150;
    b.holdMs = 400;
    b.doubleMs = 333;
    base.bindings.push_back(b);
  }
  Binding turbo = Bind(Action::PadA, {In(Device::Touch, 33)});
  turbo.turboHz = 15;
  turbo.contexts = kCtxLayer1;
  base.bindings.push_back(turbo);
  Binding look = Bind(Action::LookX, {In(Device::Gyro, 1)});
  look.scale = 2.37f;
  look.invert = true;
  base.bindings.push_back(look);
  Binding whole = Bind(Action::LookY, {In(Device::MouseMotion, 1)});
  whole.scale = 3.f;
  base.bindings.push_back(whole);
  in.profiles.push_back(base);

  UserProfile pad;
  pad.name = "DualSense";
  pad.match = "type:ps5";
  pad.replace = true;
  pad.chordWindowMs = 60;
  pad.bindings.push_back(Bind(Action::PadB, {In(Device::PadAxis, 4, 1, 25)}));
  in.profiles.push_back(pad);
  UserProfile empty;
  empty.name = "";
  empty.match = "guid:0300ab";
  in.profiles.push_back(empty);

  std::string text = SerializeUserBindings(in);
  UserBindings back;
  std::string error;
  REQUIRE(ParseUserBindings(text, back, &error));
  REQUIRE(back == in);
  REQUIRE(SerializeUserBindings(back) == text);
  // Defaults are left out.
  REQUIRE(text.find("tap_ms = 200") == std::string::npos);
  REQUIRE(text.find("contexts = [\"all\"]") == std::string::npos);
  REQUIRE(text.find("contexts = [\"gameplay\", \"morphball\"]") != std::string::npos);
  REQUIRE(text.find("scale = 3.0") != std::string::npos);

  // Hand-written forms: comments, trailing comma, integer scale, "all".
  const char* hand = "# mine\n"
                     "[[profile]]  # the default\n"
                     "name = \"x\"\n"
                     "unbind = []\n"
                     "\n"
                     "[[profile.bind]]\n"
                     "action = \"pad_a\"\n"
                     "inputs = [\"pad:a\", ]\n"
                     "scale = 2\n"
                     "contexts = [\"all\"]\n";
  REQUIRE(ParseUserBindings(hand, back, &error));
  REQUIRE(back.profiles.size() == 1 && back.profiles[0].bindings.size() == 1);
  REQUIRE(back.profiles[0].match.empty());
  REQUIRE(back.profiles[0].bindings[0].scale == 2.f);
  REQUIRE(back.profiles[0].bindings[0].contexts == kCtxAll);
  REQUIRE(ParseUserBindings("", back) && back.profiles.empty());
  REQUIRE(ParseUserBindings("\xEF\xBB\xBF[[profile]]\nname = \"bom\"\n", back, &error));
  REQUIRE(back.profiles.size() == 1 && back.profiles[0].name == "bom");
}

void TestErrors() {
  REQUIRE(ErrorLine("[[profile]]\nname = \"a\"\n[[profile.bind]]\naction = \"jump\"\ninputs = [\"pad:a\"]\n") == 4);
  REQUIRE(ErrorLine("[[profile]]\ncolour = \"red\"\n") == 2);
  REQUIRE(ErrorLine("[[profile]]\n[[profile.bind]]\naction = \"pad_a\"\ninputs = [\"pad:zz\"]\n") == 4);
  REQUIRE(ErrorLine("\n[[profile.bind]]\naction = \"pad_a\"\n") == 2);
  REQUIRE(ErrorLine("[[profile]]\n[[profile.bind]]\naction = \"pad_a\"\n"
                    "inputs = [\"pad:a\", \"pad:b\", \"pad:x\", \"pad:y\", \"pad:back\"]\n") == 4);
  REQUIRE(ErrorLine("[[profile]]\n[[profile.bind]]\naction = \"pad_a\"\ninputs = []\n") == 4);
  REQUIRE(ErrorLine("[[profile]]\n\n[[profile.bind]]\naction = \"pad_a\"\n") == 3);  // no inputs
  REQUIRE(ErrorLine("[[profile]]\nchord_window_ms = 70000\n") == 2);
  REQUIRE(ErrorLine("[[profile]]\nreplace = 1\n") == 2);
  REQUIRE(ErrorLine("[[profile]]\nname = \"open\n") == 2);
  REQUIRE(ErrorLine("[[profile]]\nunbind = [\"pad_a@mouse\"]\n") == 2);
  REQUIRE(ErrorLine("name = \"x\"\n") == 1);
  REQUIRE(ErrorLine("[controls]\n") == 1);
  const char* bind = "[[profile]]\n[[profile.bind]]\naction = \"pad_a\"\ninputs = [\"pad:a\"]\n";
  REQUIRE(ErrorLine((std::string(bind) + "scale = inf\n").c_str()) == 5);
  REQUIRE(ErrorLine((std::string(bind) + "scale = nan\n").c_str()) == 5);
  REQUIRE(ErrorLine((std::string(bind) + "scale = 1e300\n").c_str()) == 5);
  REQUIRE(ErrorLine((std::string(bind) + "scale = 101.0\n").c_str()) == 5);

  UserBindings out;
  out.profiles.emplace_back();
  REQUIRE(!ParseUserBindings("[[profile]]\nbad\n", out));
  REQUIRE(out.profiles.size() == 1); // left unchanged
}

void TestSelect() {
  UserBindings b;
  auto add = [&](const char* name, const char* match) {
    UserProfile p;
    p.name = name;
    p.match = match;
    b.profiles.push_back(p);
  };
  add("type", "type:PS5");
  add("base", "");
  add("guid", "guid:0300ABCD");
  add("base2", "");
  add("type2", "type:ps5");

  Selection s = Select(b, "0300abcd", "ps5");
  REQUIRE(s.base && s.base->name == "base");
  REQUIRE(s.pad && s.pad->name == "guid");
  s = Select(b, "ffff", "Ps5");
  REQUIRE(s.pad && s.pad->name == "type");
  s = Select(b, "", "");
  REQUIRE(s.base && s.base->name == "base" && !s.pad);
  s = Select(b, "", "xbox360");
  REQUIRE(!s.pad);
  REQUIRE(!Select({}, "a", "b").base);
}

void TestOverlay() {
  Profile base;
  base.bindings.push_back(Bind(Action::PadA, {In(Device::Key, 44)}));
  base.bindings.push_back(Bind(Action::PadA, {In(Device::PadButton, 0)}));
  base.bindings.push_back(Bind(Action::PadZ, {In(Device::PadButton, 10)}));
  base.bindings.push_back(Bind(Action::PadZ, {In(Device::Touch, 5)}));
  base.bindings.push_back(Bind(Action::PadB, {In(Device::PadButton, 1)}));
  // A chord counts by its trigger (last input): this one is a keyboard binding.
  base.bindings.push_back(Bind(Action::PadB, {In(Device::PadButton, 9), In(Device::Key, 5)}));

  UserProfile user;
  user.bindings.push_back(Bind(Action::PadA, {In(Device::PadButton, 9), In(Device::PadButton, 2)}));
  user.unbind.push_back({Action::PadZ, Family::Pad});
  user.bindings.push_back(Bind(Action::PadB, {In(Device::Key, 6)}));

  Profile p = base;
  Overlay(p, user);
  REQUIRE(p.chordWindowMs == 40);
  REQUIRE(p.bindings.size() == 5);
  REQUIRE(p.bindings[0] == base.bindings[0]); // keyboard Fire kept
  REQUIRE(p.bindings[1] == base.bindings[3]); // touch Z kept
  REQUIRE(p.bindings[2] == base.bindings[4]); // pad B kept
  REQUIRE(p.bindings[3] == user.bindings[0]);
  REQUIRE(p.bindings[4] == user.bindings[1]);

  user.replace = true;
  user.chordWindowMs = 90;
  p = base;
  Overlay(p, user);
  REQUIRE(p.chordWindowMs == 90);
  REQUIRE(p.bindings == user.bindings);
}

void TestFamilyEditing() {
  Profile inherited;
  inherited.bindings.push_back(Bind(Action::PadA, {In(Device::Key, 44)}));
  inherited.bindings.push_back(Bind(Action::PadA, {In(Device::Key, 45)}));
  inherited.bindings.push_back(Bind(Action::PadA, {In(Device::PadButton, 0)}));
  inherited.bindings.push_back(Bind(Action::PadB, {In(Device::PadButton, 1)}));

  UserProfile user;
  REQUIRE(BindingFamily(inherited.bindings[2]) == Family::Pad);
  REQUIRE(!Overridden(user, Action::PadA, Family::Keyboard));

  // Editing one of two inherited keys keeps the other, and the pad binding alone.
  Materialize(user, Action::PadA, Family::Keyboard, inherited);
  REQUIRE(user.bindings.size() == 2);
  REQUIRE(Overridden(user, Action::PadA, Family::Keyboard));
  REQUIRE(!Overridden(user, Action::PadA, Family::Pad));
  Materialize(user, Action::PadA, Family::Keyboard, inherited); // already overridden: no copy
  REQUIRE(user.bindings.size() == 2);
  Profile p = inherited;
  Overlay(p, user);
  REQUIRE(p.bindings.size() == 4);
  REQUIRE(InFamily(p.bindings, Action::PadA, Family::Pad).size() == 1);

  // Removing the last one unbinds the family.
  RemoveUserBinding(user, 0);
  REQUIRE(user.unbind.empty());
  RemoveUserBinding(user, 0);
  REQUIRE(user.bindings.empty());
  REQUIRE(user.unbind.size() == 1 && user.unbind[0] == (Unbind{Action::PadA, Family::Keyboard}));
  p = inherited;
  Overlay(p, user);
  REQUIRE(InFamily(p.bindings, Action::PadA, Family::Keyboard).empty());
  REQUIRE(InFamily(p.bindings, Action::PadA, Family::Pad).size() == 1);

  // A new binding supersedes the unbind.
  AddUserBinding(user, Bind(Action::PadA, {In(Device::Key, 9)}));
  REQUIRE(user.unbind.empty() && user.bindings.size() == 1);

  // Revert and ClearFamily.
  AddUserBinding(user, Bind(Action::PadB, {In(Device::PadButton, 2)}));
  Revert(user, Action::PadA, Family::Keyboard);
  REQUIRE(!Overridden(user, Action::PadA, Family::Keyboard));
  REQUIRE(Overridden(user, Action::PadB, Family::Pad));
  user.unbind.push_back({Action::PadZ, Family::Keyboard});
  ClearFamily(user, Family::Pad);
  REQUIRE(user.bindings.empty());
  REQUIRE(user.unbind.size() == 1);
  ClearFamily(user, Family::Keyboard);
  REQUIRE(user.unbind.empty());
}
void TestDuplicateBindings() {
  const Binding key = Bind(Action::PadA, {In(Device::Key, 44)});
  Profile inherited;
  inherited.bindings.push_back(key);
  UserProfile user;
  Materialize(user, key.action, Family::Keyboard, inherited);
  for (int i = 0; i < 5; ++i) AddUserBinding(user, key);
  REQUIRE(user.bindings.size() == 1);

  Binding other = key;
  other.action = Action::PadB;
  AddUserBinding(user, other);
  other = key;
  other.trigger = Trigger::Tap;
  AddUserBinding(user, other);
  REQUIRE(user.bindings.size() == 3);

  // Editing another key to the existing key coalesces the duplicate.
  AddUserBinding(user, Bind(Action::PadA, {In(Device::Key, 45)}));
  RemoveUserBinding(user, 3);
  AddUserBinding(user, key);
  REQUIRE(user.bindings.size() == 3);
  REQUIRE(user.unbind.empty());

  other = key;
  other.contexts = kCtxMorphBall;
  AddUserBinding(user, other);
  AddUserBinding(user, other);
  REQUIRE(user.bindings.size() == 4);

  UserProfile controllerProfile;
  AddUserBinding(controllerProfile, key);
  REQUIRE(controllerProfile.bindings.size() == 1);
}
// Issue #92: the layout editor's buttons, from the touch layout string.
void TestTouchButtons() {
  REQUIRE(ParseTouchButtons("").empty());
  // The stock entries, old (3 and 5 fields) and new, are not buttons.
  REQUIRE(ParseTouchButtons("a:0.1000,0.0000,1.200;menu:0,0,1,25;eye:0,0,1,100,1").empty());
  const std::vector<TouchButton> got = ParseTouchButtons(
      "a:0.1,0,1;k3:0,0,1,100,0,visor_scan; k0 :-0.5,0.25,1.5,50,1, beam_wave ;k63:0,0,1,100,0,save_state");
  REQUIRE(got.size() == 3);
  REQUIRE((got[0] == TouchButton{0, Action::BeamWave}));
  REQUIRE((got[1] == TouchButton{3, Action::VisorScan}));
  REQUIRE((got[2] == TouchButton{63, Action::SaveState}));
  // Bad slots, field counts and actions are skipped, the rest still load.
  for (const char* bad : {"k64:0,0,1,100,0,pad_a", "k-1:0,0,1,100,0,pad_a", "k:0,0,1,100,0,pad_a",
                          "k01:0,0,1,100,0,pad_a", "kx:0,0,1,100,0,pad_a", "k100:0,0,1,100,0,pad_a",
                          "k1:0,0,1,100,pad_a", "k1:0,0,1,100,0,pad_a,1,0", "k1:0,0,1,100,0,", "k1:0,0,1,100,0,warp",
                          "k1:0,0,1,100,0,none", "k1:0,0,1,100,0,stick_up", "k1:0,0,1,100,0,look_x",
                          "k1:0,0,1,100,0,layer_1", "k1:0,0,1,100,0,menu_a", "k1", "K1:0,0,1,100,0,pad_a"}) {
    const std::string text = std::string(bad) + ";k2:0,0,1,100,0,pad_y";
    const std::vector<TouchButton> one = ParseTouchButtons(text);
    REQUIRE(one.size() == 1);
    REQUIRE((one[0] == TouchButton{2, Action::PadY}));
  }
  // A repeated slot keeps its last valid entry.
  const std::vector<TouchButton> twice = ParseTouchButtons("k4:0,0,1,100,0,pad_a;k4:0,0,1,100,0,pad_b;k4:0,0,1,100,0,warp");
  REQUIRE(twice.size() == 1 && twice[0].action == Action::PadB);
  // A 7th field is the overlay's page; the action stays the 6th.
  const std::vector<TouchButton> paged = ParseTouchButtons("k5:0,0,1,100,0,ball_bomb,1;k6:0,0,1,100,0, pad_x ,2");
  REQUIRE(paged.size() == 2);
  REQUIRE((paged[0] == TouchButton{5, Action::BallBomb}));
  REQUIRE((paged[1] == TouchButton{6, Action::PadX}));
  // All 64 at once.
  std::string full;
  for (int slot = 0; slot < kTouchButtonSlots; ++slot)
    full += "k" + std::to_string(slot) + ":0.1234,-0.5678,1.250,75,0,spring_ball;";
  REQUIRE(ParseTouchButtons(full).size() == size_t(kTouchButtonSlots));

  REQUIRE(TouchButtonAction(Action::PadA));
  REQUIRE(TouchButtonAction(Action::SpringBall));
  REQUIRE(TouchButtonAction(Action::PortMenu));
  REQUIRE(!TouchButtonAction(Action::None));
  REQUIRE(!TouchButtonAction(Action::AimUp));
  REQUIRE(!TouchButtonAction(Action::LookY));
  REQUIRE(!TouchButtonAction(Action::Layer4));
  REQUIRE(!TouchButtonAction(Action::MenuRight));

  // The codes reach the profile as single-input presses, after what was there,
  // and a user's touch rebinding of the same action (Overlay first) keeps them.
  Profile p;
  p.bindings.push_back(Bind(Action::VisorScan, {In(Device::Touch, 5)}));
  UserProfile user;
  user.bindings.push_back(Bind(Action::VisorScan, {In(Device::Touch, 6)}));
  Overlay(p, user);
  BindTouchButtons(p, "k1:0,0,1,100,0,visor_scan;k7:0,0,1,100,1,spring_ball;k63:0,0,1,0,0,beam_ice");
  REQUIRE(p.bindings.size() == 4);
  REQUIRE((p.bindings[0] == Bind(Action::VisorScan, {In(Device::Touch, 6)})));
  REQUIRE((p.bindings[1] == Bind(Action::VisorScan, {In(Device::Touch, 65)})));
  REQUIRE((p.bindings[2] == Bind(Action::SpringBall, {In(Device::Touch, 71)})));
  REQUIRE((p.bindings[3] == Bind(Action::BeamIce, {In(Device::Touch, 127)})));
  REQUIRE(p.bindings[1].trigger == Trigger::Press && p.bindings[1].contexts == kCtxAll);

  // The top slot's code is read through the 128-bit touch state and fires its action.
  RawState raw;
  REQUIRE(raw.Value(In(Device::Touch, 127)) == 0.f);
  raw.touch.set(127);
  REQUIRE(raw.Value(In(Device::Touch, 127)) == 1.f);
  REQUIRE(raw.Value(In(Device::Touch, 63)) == 0.f);
  REQUIRE(raw.Value(In(Device::Touch, 128)) == 0.f);
  Runtime runtime;
  runtime.SetProfile(&p);
  raw.touch.reset();
  runtime.Poll(raw, kCtxGameplay, 1000);
  REQUIRE(!runtime.Last().Held(Action::BeamIce));
  raw.touch.set(127);
  REQUIRE(runtime.Poll(raw, kCtxGameplay, 1016).Held(Action::BeamIce));
  REQUIRE(!runtime.Last().Held(Action::SpringBall));
  raw.touch.reset(127);
  REQUIRE(!runtime.Poll(raw, kCtxGameplay, 1032).Held(Action::BeamIce));

  // Named in controls.toml by their layout ids.
  REQUIRE(InputToText(In(Device::Touch, 64)) == "touch:k0");
  REQUIRE(InputToText(In(Device::Touch, 127)) == "touch:k63");
  Input back;
  REQUIRE(InputFromText("touch:k9", back) && back == In(Device::Touch, 73));
}
// Custom actions: Action values past the fixed enum, named custom:<id> in the
// file and defined by [[custom_action]] tables before the profiles.
void TestCustomActions() {
  REQUIRE(ActionFromKey("custom:1") == CustomAction(1));
  REQUIRE(ActionKey(CustomAction(12)) == "custom:12");
  REQUIRE(ActionKey(Action::PadA) == "pad_a");
  REQUIRE(ActionFromKey("custom:0") == Action::None);
  REQUIRE(ActionFromKey("custom:x") == Action::None);
  REQUIRE(ActionFromKey("custom:70000") == Action::None);
  REQUIRE(ActionFromKey("custom:") == Action::None);
  REQUIRE(ActionFromKey("custom:61439") == CustomAction(uint16_t(kMaxCustomId)));
  REQUIRE(IsCustom(CustomAction(1)) && !IsCustom(Action::PadA) && CustomId(Action::PadA) == 0);
  REQUIRE(CustomId(CustomAction(7)) == 7);
  REQUIRE(Info(CustomAction(2)).kind == ActionKind::Digital);
  REQUIRE(Info(CustomAction(2)).key == "custom");
  REQUIRE(TouchButtonAction(CustomAction(2)));

  // Two definitions and binds on them, with inputs and triggers: a round trip.
  UserBindings in;
  CustomActionDef warp;
  warp.id = 2;
  warp.name = "Warp \"home\"";
  warp.preset = "warp";
  warp.arg = "MLVL:MREA";
  in.customActions.push_back(warp);
  CustomActionDef spawn;
  spawn.id = 5;
  spawn.name = "Spawn";
  spawn.renamed = true;
  spawn.preset = "command";
  spawn.mode = "on";
  spawn.command = "spawn 12";
  in.customActions.push_back(spawn);
  UserProfile base;
  base.name = "Default";
  Binding hold = Bind(CustomAction(2), {In(Device::Key, 44)});
  hold.trigger = Trigger::Hold;
  hold.holdMs = 400;
  base.bindings.push_back(hold);
  Binding chord = Bind(CustomAction(5), {In(Device::PadButton, 9), In(Device::PadButton, 0)});
  chord.trigger = Trigger::Toggle;
  chord.turboHz = 12;
  base.bindings.push_back(chord);
  base.unbind.push_back({CustomAction(5), Family::Touch});
  in.profiles.push_back(base);

  std::string text = SerializeUserBindings(in);
  UserBindings back;
  std::string error;
  REQUIRE(ParseUserBindings(text, back, &error));
  REQUIRE(back == in);
  REQUIRE(SerializeUserBindings(back) == text);
  REQUIRE(text.find("[[custom_action]]") != std::string::npos);
  REQUIRE(text.find("action = \"custom:5\"") != std::string::npos);
  REQUIRE(text.find("custom:5@touch") != std::string::npos);
  REQUIRE(text.find("mode = \"on\"") != std::string::npos);
  REQUIRE(text.find("command = \"spawn 12\"") != std::string::npos);

  // A [[custom_action]] after a [[profile]] starts a new top-level table.
  const char* hand = "[[profile]]\n"
                     "name = \"x\"\n"
                     "\n"
                     "[[custom_action]]\n"
                     "id = 4\n"
                     "name = \"Look back\"\n"
                     "preset = \"setting\"\n"
                     "arg = \"invert_y\"\n"
                     "mode = \"toggle\"\n"
                     "\n"
                     "[[profile.bind]]\n"
                     "action = \"custom:4\"\n"
                     "inputs = [\"pad:dpright\"]\n";
  REQUIRE(ParseUserBindings(hand, back, &error));
  REQUIRE(back.profiles.size() == 1 && back.profiles[0].bindings.size() == 1);
  REQUIRE(back.customActions.size() == 1);
  REQUIRE(back.customActions[0].id == 4 && back.customActions[0].name == "Look back");
  REQUIRE(back.customActions[0].mode == "toggle" && !back.customActions[0].renamed);
  REQUIRE(back.profiles[0].bindings[0].action == CustomAction(4));

  // Bad ids and a repeated one are errors with their line.
  REQUIRE(ErrorLine("[[custom_action]]\nid = 0\nname = \"x\"\n") == 2);
  REQUIRE(ErrorLine("[[custom_action]]\nid = 61440\n") == 2);
  REQUIRE(ErrorLine("[[custom_action]]\nid = \"3\"\n") == 2);
  REQUIRE(ErrorLine("[[custom_action]]\nid = 2\nname = \"a\"\n[[custom_action]]\nid = 2\n") == 5);
  REQUIRE(ErrorLine("[[custom_action]]\nname = \"a\"\n") == 1);
  REQUIRE(ErrorLine("[[custom_action]]\nid = 2\nnope = 1\n") == 3);

  // The next id is the highest plus one, at least 1.
  REQUIRE(NextCustomId({}) == 1);
  UserBindings ids;
  ids.customActions.emplace_back();
  ids.customActions.back().id = 3;
  REQUIRE(NextCustomId(ids) == 4);
  ids.customActions.emplace_back();
  ids.customActions.back().id = 9;
  REQUIRE(NextCustomId(ids) == 10);

  // Deleting a custom action takes its definition, its bindings and its unbinds
  // out of every profile, and leaves the others alone.
  UserBindings del;
  CustomActionDef keep;
  keep.id = 2;
  keep.preset = "refill_energy";
  del.customActions.push_back(keep);
  CustomActionDef gone;
  gone.id = 7;
  gone.preset = "refill_ammo";
  del.customActions.push_back(gone);
  UserProfile a;
  a.bindings.push_back(Bind(CustomAction(7), {In(Device::Key, 44)}));
  a.bindings.push_back(Bind(CustomAction(2), {In(Device::Key, 45)}));
  a.unbind.push_back({CustomAction(7), Family::Pad});
  del.profiles.push_back(a);
  UserProfile b;
  b.bindings.push_back(Bind(CustomAction(7), {In(Device::Touch, 3)}));
  b.bindings.push_back(Bind(Action::PadA, {In(Device::Key, 46)}));
  del.profiles.push_back(b);
  RemoveCustomAction(del, 7);
  REQUIRE(del.customActions.size() == 1 && del.customActions[0].id == 2);
  REQUIRE(del.profiles[0].bindings.size() == 1 && del.profiles[0].bindings[0].action == CustomAction(2));
  REQUIRE(del.profiles[0].unbind.empty());
  REQUIRE(del.profiles[1].bindings.size() == 1 && del.profiles[1].bindings[0].action == Action::PadA);
  RemoveCustomAction(del, 9); // an id nothing has
  REQUIRE(del.customActions.size() == 1 && del.profiles[0].bindings.size() == 1);

  // The overlay rule is per (action, family): a custom action's other families
  // are kept, and two custom ids stay apart.
  Profile p;
  p.bindings.push_back(Bind(CustomAction(5), {In(Device::PadButton, 0)}));
  p.bindings.push_back(Bind(CustomAction(5), {In(Device::Key, 44)}));
  p.bindings.push_back(Bind(CustomAction(6), {In(Device::PadButton, 0)}));
  UserProfile user;
  user.bindings.push_back(Bind(CustomAction(5), {In(Device::Key, 6)}));
  Overlay(p, user);
  REQUIRE(p.bindings.size() == 3);
  REQUIRE((p.bindings[0] == Bind(CustomAction(5), {In(Device::PadButton, 0)})));
  REQUIRE((p.bindings[1] == Bind(CustomAction(6), {In(Device::PadButton, 0)})));
  REQUIRE((p.bindings[2] == Bind(CustomAction(5), {In(Device::Key, 6)})));
  // An unbind of one custom id leaves the other's bindings alone.
  UserProfile unbindUser;
  unbindUser.unbind.push_back({CustomAction(6), Family::Pad});
  Profile q;
  q.bindings = p.bindings;
  Overlay(q, unbindUser);
  REQUIRE(q.bindings.size() == 2);
  REQUIRE(q.bindings[0] == p.bindings[0]);
}

// Polls `p` with nothing down, then with `buttons` (pad bits) and `mouse` down.
const Output& PollWith(Runtime& runtime, const Profile& p, uint16_t contexts, uint32_t buttons,
                       uint32_t mouse = 0, float rightTrigger = 0.f) {
  runtime.SetProfile(&p);
  RawState raw;
  runtime.Poll(raw, contexts, 1000);
  raw.padButtons = buttons;
  raw.mouseButtons = mouse;
  raw.padAxes[5] = rightTrigger;
  return runtime.Poll(raw, contexts, 1100);
}

// Issue #167: a Morph Ball row's input does only that row in the ball and keeps
// everything it does elsewhere; an unbound row changes nothing.
void TestBallButtons() {
  REQUIRE(FindBallRow(Action::BallBomb)->game == Action::PadA);
  REQUIRE(BallRowForGame(Action::PadR)->row == Action::BallSpider);
  REQUIRE(FindBallRow(Action::PadA) == nullptr && BallRowForGame(Action::PadZ) == nullptr);
  REQUIRE(ActionFromKey("ball_power_bomb") == Action::BallPowerBomb);
  REQUIRE(TouchButtonAction(Action::BallBomb));

  const Input lb = In(Device::PadButton, 9), rb = In(Device::PadButton, 10);
  const uint32_t kLB = 1u << 9, kRB = 1u << 10;
  Binding springOnRb = Bind(Action::SpringBall, {rb});
  springOnRb.contexts = kCtxMorphBall;
  Profile base;
  base.chordWindowMs = 0;
  base.bindings = {Bind(Action::PadR, {rb}), Bind(Action::VisorNext, {rb}), springOnRb,
                   Bind(Action::Layer1, {lb})};
  Profile p = base;
  ApplyBallButtons(p);
  REQUIRE(p == base);

  // Outside the ball nothing changes, ranks included: RB still does R and Next visor.
  p.bindings.push_back(Bind(Action::BallBomb, {rb}));
  ApplyBallButtons(p);
  REQUIRE(p.bindings[0].contexts == kCtxAll && p.bindings[0].blocked == kCtxMorphBall);
  REQUIRE(p.bindings[2].contexts == kCtxMorphBall && p.bindings[2].blocked == kCtxMorphBall);
  REQUIRE(p.bindings[3].blocked == 0);
  Runtime runtime;
  const Output& play = PollWith(runtime, p, kCtxGameplay, kRB);
  REQUIRE(play.Held(Action::PadR) && play.Held(Action::VisorNext) && !play.Held(Action::BallBomb));
  const Output& ball = PollWith(runtime, p, kCtxMorphBall, kRB);
  REQUIRE(ball.Held(Action::BallBomb) && !ball.Held(Action::PadR) && !ball.Held(Action::VisorNext) &&
          !ball.Held(Action::SpringBall));
  // A held layer doesn't bring them back in the ball.
  const Output& layered = PollWith(runtime, p, kCtxMorphBall, kLB | kRB);
  REQUIRE(layered.Held(Action::Layer1) && layered.Held(Action::BallBomb));
  REQUIRE(!layered.Held(Action::PadR) && !layered.Held(Action::SpringBall));

  // The same mouse button under another code, and an axis at another threshold.
  Profile m;
  m.bindings = {Bind(Action::PadA, {In(Device::MouseButton, 0)}), Bind(Action::PadA, {In(Device::MouseButton, 16)}),
                Bind(Action::BallPowerBomb, {In(Device::MouseButton, 8)}),
                Bind(Action::PadR, {In(Device::PadAxis, 5, 1)}),
                Bind(Action::BallSpider, {In(Device::PadAxis, 5, 1, 75)})};
  ApplyBallButtons(m);
  const Output& mouse = PollWith(runtime, m, kCtxMorphBall, 0, (1u << 8) | (1u << 16), 1.f);
  REQUIRE(mouse.Held(Action::BallPowerBomb) && mouse.Held(Action::BallSpider));
  REQUIRE(!mouse.Held(Action::PadA) && !mouse.Held(Action::PadR));
  REQUIRE(SamePhysicalInput(In(Device::MouseButton, 2), In(Device::MouseButton, 26)));
  REQUIRE(!SamePhysicalInput(In(Device::MouseButton, 2), In(Device::MouseButton, 1)));
  REQUIRE(!SamePhysicalInput(In(Device::PadAxis, 5, 1), In(Device::PadAxis, 5, -1)));

  // A chord row blocks the same chord; its members alone keep theirs, as with any
  // chord (they are its modifiers).
  Profile c;
  c.chordWindowMs = 0;
  Binding spring = Bind(Action::SpringBall, {lb});
  spring.contexts = kCtxMorphBall;
  c.bindings = {Bind(Action::BeamWave, {lb, rb}), spring, Bind(Action::BallBomb, {lb, rb})};
  ApplyBallButtons(c);
  REQUIRE(c.bindings[0].blocked == kCtxMorphBall && c.bindings[1].blocked == 0);
  REQUIRE(PollWith(runtime, c, kCtxGameplay, kLB | kRB).Held(Action::BeamWave));

  // A row with a layer: in the ball while the layer is held, nowhere else.
  Profile l;
  l.chordWindowMs = 0;
  Binding layerBomb = Bind(Action::BallBomb, {rb});
  layerBomb.contexts = kCtxMorphBall | kCtxLayer1;
  Binding ballOrLayer = Bind(Action::SpringBall, {rb});
  ballOrLayer.contexts = kCtxMorphBall | kCtxLayer1;
  l.bindings = {Bind(Action::Layer1, {lb}), ballOrLayer, layerBomb};
  ApplyBallButtons(l);
  REQUIRE(l.bindings[1].contexts == (kCtxMorphBall | kCtxLayer1)); // its rank stays a context's
  REQUIRE(l.bindings[2].contexts == kCtxLayer1);
  const Output& inBall = PollWith(runtime, l, kCtxMorphBall, kLB | kRB);
  REQUIRE(inBall.Held(Action::BallBomb) && !inBall.Held(Action::SpringBall));
  REQUIRE(!PollWith(runtime, l, kCtxGameplay, kLB | kRB).Held(Action::BallBomb));
  REQUIRE(!PollWith(runtime, l, kCtxMorphBall, kRB).Held(Action::BallBomb));
  // Without the layer the input keeps its other bindings in the ball.
  REQUIRE(l.bindings[1].blocked == 0);
  REQUIRE(PollWith(runtime, l, kCtxMorphBall, kRB).Held(Action::SpringBall));

  // A bound row replaces its game button in the ball for that family only: the
  // pad's A stops bombing, the key and the touch button still press A.
  Profile r;
  r.chordWindowMs = 0;
  const Input padA = In(Device::PadButton, 0), keyA = In(Device::Key, 4), turbo = In(Device::Touch, 33);
  r.bindings = {Bind(Action::PadA, {padA}), Bind(Action::PadA, {keyA}), Bind(Action::PadA, {turbo}),
                Bind(Action::BallBomb, {rb}), Bind(Action::BallBomb, {In(Device::Touch, 3)})};
  ApplyBallButtons(r);
  REQUIRE(r.bindings[0].blocked == kCtxMorphBall && r.bindings[1].blocked == 0 && r.bindings[2].blocked == 0);
  REQUIRE(PollWith(runtime, r, kCtxGameplay, 1u).Held(Action::PadA));
  REQUIRE(!PollWith(runtime, r, kCtxMorphBall, 1u).Held(Action::PadA));
  REQUIRE(PollWith(runtime, r, kCtxMorphBall, kRB).Held(Action::BallBomb));
  // A row only under a layer adds to the button.
  Profile rl = r;
  rl.bindings[3].contexts = kCtxLayer1;
  rl.bindings[0].blocked = 0;
  ApplyBallButtons(rl);
  REQUIRE(rl.bindings[0].blocked == 0);
  // The overlay's ball page is one button per row, on touch codes 40..44.
  REQUIRE(std::size(kTouchBallControls) == std::size(kBallRows));
  for (size_t i = 0; i < std::size(kTouchBallControls); ++i) {
    const TouchBallControl& c = kTouchBallControls[i];
    REQUIRE(FindBallRow(c.row) != nullptr && c.control == 40 + int(i));
  }
  // An added button's row is scoped to the ball like any other.
  Profile tb;
  BindTouchButtons(tb, "k2:0,0,1,100,0,ball_boost,1");
  ApplyBallButtons(tb);
  REQUIRE(tb.bindings.size() == 1 && tb.bindings[0].contexts == kCtxMorphBall);

  // everywhereOnly (the Menu rows) leaves bindings scoped under Advanced alone.
  Profile e;
  Binding mapOnly = Bind(Action::PadZ, {In(Device::Key, 4)});
  mapOnly.contexts = kCtxMap;
  e.bindings = {Bind(Action::PadA, {In(Device::Key, 4)}), mapOnly, Bind(Action::MenuA, {In(Device::Key, 4)})};
  BlockRowInputs(e, [](const Binding& b) { return b.action == Action::MenuA; }, kCtxMap, true);
  REQUIRE(e.bindings[0].blocked == kCtxMap && e.bindings[1].blocked == 0 && e.bindings[2].blocked == 0);
}

// The stock touch buttons: fixed actions a user binding can't take or clear.
void TestStockTouch() {
  REQUIRE(StockTouchInput(In(Device::Touch, 4)) && StockTouchInput(In(Device::Touch, 22)));
  REQUIRE(StockTouchInput(In(Device::Touch, 33)));
  REQUIRE(!StockTouchInput(In(Device::Touch, 10)) && !StockTouchInput(In(Device::Touch, 20)));
  REQUIRE(!StockTouchInput(In(Device::Touch, 40)) && !StockTouchInput(In(Device::Touch, 64)));
  REQUIRE(!StockTouchInput(In(Device::PadButton, 4)));

  // A binding with a stock code anywhere in it goes; Visor, the Morph Ball rows
  // and added buttons stay.
  UserProfile user;
  user.replace = true;
  user.bindings.push_back(Bind(Action::PadA, {In(Device::Touch, 4)}));
  user.bindings.push_back(Bind(Action::PadY, {In(Device::Touch, 32), In(Device::Touch, 5)}));
  user.bindings.push_back(Bind(Action::PadX, {In(Device::Touch, 10)}));
  user.bindings.push_back(Bind(Action::BallBomb, {In(Device::Touch, 40)}));
  user.bindings.push_back(Bind(Action::PadY, {In(Device::Touch, 64)}));
  const UserProfile kept = WithoutStockTouch(user);
  REQUIRE(kept.replace && kept.bindings.size() == 3);
  REQUIRE((kept.bindings[0] == user.bindings[2]) && (kept.bindings[2] == user.bindings[4]));

  // After even a replacing overlay, touch B and LB jump in every context, L locks
  // with its analog, and Turbo's pulse presses A.
  Profile p;
  Overlay(p, kept);
  BindStockTouch(p);
  Runtime runtime;
  runtime.SetProfile(&p);
  RawState raw;
  raw.touch.set(4);
  REQUIRE(runtime.Poll(raw, kCtxGameplay, 1000).Held(Action::PadB));
  REQUIRE(!runtime.Last().Held(Action::PadA));
  raw.touch.reset();
  raw.touch.set(22);
  REQUIRE(runtime.Poll(raw, kCtxMenu, 1016).Held(Action::PadB));
  raw.touch.reset();
  raw.touch.set(7);
  REQUIRE(runtime.Poll(raw, kCtxGameplay, 1032).Held(Action::PadL));
  REQUIRE(runtime.Last().Value(Action::LAnalog) == 1.f);
  raw.touch.reset();
  raw.touch.set(kTouchTurboCode);
  REQUIRE(runtime.Poll(raw, kCtxGameplay, 1048).Held(Action::PadA));
}

// Issue #31: a key on a map row does only that on the map, scoped bindings
// included, and everything it did before elsewhere; an unbound row changes nothing.
void TestMapRows() {
  REQUIRE(FindMapRow(Action::MapPanUp)->game == Action::CUp);
  REQUIRE(FindMapRow(Action::MapRotateLeft)->game == Action::MainLeft);
  REQUIRE(FindMapRow(Action::CUp) == nullptr);
  REQUIRE(ActionFromKey("map_pan_right") == Action::MapPanRight);

  const Input w = In(Device::Key, 26), i = In(Device::Key, 12);
  Binding scoped = Bind(Action::PadZ, {w});
  scoped.contexts = kCtxMap | kCtxMenu;
  Profile base;
  base.chordWindowMs = 0;
  base.bindings = {Bind(Action::MainUp, {w}), Bind(Action::CUp, {i}), scoped};
  Profile p = base;
  ApplyMapRows(p);
  REQUIRE(p == base);

  Binding pan = Bind(Action::MapPanUp, {w});
  pan.contexts = kCtxGameplay | kCtxLayer1;
  p.bindings.push_back(pan);
  ApplyMapRows(p);
  REQUIRE(p.bindings[3].contexts == kCtxMap && p.bindings[3].blocked == 0);
  REQUIRE(p.bindings[0].blocked == kCtxMap && p.bindings[1].blocked == 0 && p.bindings[2].blocked == kCtxMap);
  Runtime runtime;
  runtime.SetProfile(&p);
  RawState raw;
  raw.keys.set(26);
  const Output& play = runtime.Poll(raw, kCtxGameplay, 1000);
  REQUIRE(play.Value(Action::MainUp) == 1.f && play.Value(Action::MapPanUp) == 0.f);
  raw.keys.reset();
  runtime.Poll(raw, kCtxMap, 1008);
  raw.keys.set(26);
  const Output& map = runtime.Poll(raw, kCtxMap, 1016);
  REQUIRE(map.Value(Action::MapPanUp) == 1.f);
  REQUIRE(map.Value(Action::MainUp) == 0.f);
  REQUIRE(!map.Held(Action::PadZ));
  raw.keys.reset();
  runtime.Poll(raw, kCtxMenu, 1024);
  raw.keys.set(26);
  REQUIRE(runtime.Poll(raw, kCtxMenu, 1032).Held(Action::PadZ));
  // The C-stick's own key still pans the map.
  raw.keys.reset();
  runtime.Poll(raw, kCtxMap, 1040);
  raw.keys.set(12);
  REQUIRE(runtime.Poll(raw, kCtxMap, 1048).Value(Action::CUp) == 1.f);
}
} // namespace

int main() {
  TestCustomActions();
  TestBallButtons();
  TestMapRows();
  TestTouchButtons();
  TestStockTouch();
  TestDuplicateBindings();
  TestFamilyEditing();
  TestInputText();
  TestContexts();
  TestRoundTrip();
  TestErrors();
  TestSelect();
  TestOverlay();
  std::puts("input bindings ok");
  return 0;
}
