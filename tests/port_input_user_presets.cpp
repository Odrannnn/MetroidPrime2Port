#include "port_input_user_presets.h"
#include "port_test.h"

#include <cstdio>
#include <string>

using namespace PortInput;
using namespace PortInputUserPresets;

namespace {

std::string FakeKeyName(int code) { return code == 44 ? "space" : code == 30 ? "1" : ""; }
int FakeKeyParse(std::string_view name) { return name == "space" ? 44 : name == "1" ? 30 : -1; }

Binding Bind(Action a, Input in) {
  Binding b;
  b.action = a;
  b.inputs[b.count++] = in;
  return b;
}

Preset Sample(Family family) {
  Preset p;
  p.layout.family = family;
  p.layout.name = "My \"Layout\" #1";
  if (family == Family::Touch) {
    // bindings only: the layout stays default
  } else if (family == Family::Pad) {
    p.layout.twinStick = true;
    p.layout.padButtons = {{1, 2}, {3, 4}};
    p.layout.padAxes = {{0, -1, 5, 6}, {2, 1, -1, 3}};
    p.layout.swapScanXray = true;
    p.layout.shift[2] = 7;
    p.layout.padAlt = {-1, 3, 4};
  } else {
    p.layout.twinStick = true;
    p.layout.keyButtons = {{0, 44, 2}, {1, 30, 4}};
    p.layout.keyAxes = {{0, 44, 1, 100}};
    p.layout.mouseAim = true;
    p.layout.shift[0] = 5;
    p.layout.shift[1] = 9;
    p.layout.mouseActions = {1, 2, 3, 4, 5};
  }
  CustomActionDef def;
  def.id = 3;
  def.name = "Warp home";
  def.preset = "warp";
  def.arg = "MLVL:MREA";
  p.bindings.customActions.push_back(def);
  UserProfile prof;
  prof.name = "Default";
  prof.unbind = {{Action::PadZ, family}};
  const Input in = family == Family::Pad     ? Input{Device::PadButton, 0, 50, 1}
                   : family == Family::Touch ? Input{Device::Touch, 0, 50, 1}
                                             : Input{Device::Key, 0, 50, 44};
  prof.bindings.push_back(Bind(Action::BeamWave, in));
  const Input in2 = family == Family::Pad     ? Input{Device::PadButton, 0, 50, 2}
                    : family == Family::Touch ? Input{Device::Touch, 0, 50, 2}
                                              : Input{Device::Key, 0, 50, 30};
  prof.bindings.push_back(Bind(CustomAction(3), in2));
  p.bindings.profiles.push_back(prof);
  return p;
}

void TestSlugAndName() {
  REQUIRE(Slug("My Layout #1") == "my-layout-1");
  REQUIRE(Slug("  ---  ") == "preset");
  REQUIRE(Slug("../../x") == "x");
  REQUIRE(CleanName("  hi\tthere \n") == "hithere");
  REQUIRE(CleanName(std::string(100, 'a')).size() == kMaxName);
  REQUIRE(CleanName("").empty());
}

void TestRoundTrip() {
  SetKeyNames(FakeKeyName, FakeKeyParse);
  for (Family f : {Family::Keyboard, Family::Pad, Family::Touch}) {
    const Preset p = Sample(f);
    const std::string text = Serialize(p);
    Preset back;
    std::string error;
    REQUIRE(Parse(text, back, &error));
    REQUIRE(back.layout == p.layout);
    REQUIRE(back.bindings == p.bindings);
    REQUIRE(Serialize(back) == text);
    // The file is also plain controls.toml text.
    UserBindings plain;
    REQUIRE(ParseUserBindings(text, plain, &error));
    REQUIRE(plain == p.bindings);
  }
  Preset bad;
  REQUIRE(!Parse("", bad));
  REQUIRE(!Parse("[[profile]]\nname = \"Default\"\n", bad)); // no header
  REQUIRE(!Parse("#! controls-preset = 1\n#! family = tablet\n#! name = x\n", bad));
  REQUIRE(!Parse("#! controls-preset = 1\n#! family = pad\n", bad)); // no name
  std::string broken = Serialize(Sample(Family::Pad)) + "\n[[bogus\n";
  REQUIRE(!Parse(broken, bad));
  // Touch is bindings only: no settings lines in the file.
  const std::string touch = Serialize(Sample(Family::Touch));
  REQUIRE(touch.find("family = touch") != std::string::npos);
  REQUIRE(touch.find("twin_stick") == std::string::npos);
  REQUIRE(touch.find("key_button") == std::string::npos && touch.find("pad_button") == std::string::npos);
  SetKeyNames(nullptr, nullptr);
}

void TestExtractMerge() {
  SetKeyNames(FakeKeyName, FakeKeyParse);
  const Preset pad = Sample(Family::Pad);
  const Preset key = Sample(Family::Keyboard);
  // The user's file holds both families; extract takes one.
  UserBindings all = key.bindings;
  UserProfile& prof = all.profiles[0];
  for (const Binding& b : pad.bindings.profiles[0].bindings) prof.bindings.push_back(b);
  prof.unbind.insert(prof.unbind.end(), pad.bindings.profiles[0].unbind.begin(), pad.bindings.profiles[0].unbind.end());
  const UserBindings got = ExtractFamily(all, Family::Pad);
  REQUIRE(got.profiles.size() == 1);
  REQUIRE(got.profiles[0].bindings.size() == 2);
  REQUIRE(got.customActions.size() == 1);

  // Merge into a cleared file: the custom id is kept when free, reused when equal,
  // renumbered when taken by something else.
  UserBindings target;
  MergeFamily(target, got);
  REQUIRE(target.customActions.size() == 1 && target.customActions[0].id == 3);
  MergeFamily(target, got);
  REQUIRE(target.customActions.size() == 1);
  REQUIRE(target.profiles[0].unbind.size() == 1);

  UserBindings clash;
  CustomActionDef other;
  other.id = 3;
  other.name = "Other";
  other.preset = "command";
  other.command = "status";
  clash.customActions.push_back(other);
  MergeFamily(clash, got);
  REQUIRE(clash.customActions.size() == 2);
  const uint16_t added = clash.customActions[1].id;
  REQUIRE(added != 3);
  bool named = false;
  for (const Binding& b : clash.profiles[0].bindings)
    if (b.action == CustomAction(added)) named = true;
  REQUIRE(named);
  SetKeyNames(nullptr, nullptr);
}

} // namespace

int main() {
  TestSlugAndName();
  TestRoundTrip();
  TestExtractMerge();
  std::puts("input user presets ok");
  return 0;
}
