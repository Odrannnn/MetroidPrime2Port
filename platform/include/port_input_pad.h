#pragma once

// MP2_INPUT pad prefix: `p2.A@100`, `P1:START@50+4`. Header-only so the parser has a test
// (tests/port_input_pad.cpp). An entry with no prefix is pad 0, as every script was before.
namespace PortInputPad {

constexpr int kPadCount = 4;

// `name` points at the start of one entry (up to its '@' at `at`). Strips a `p<0-3>.` or `p<0-3>:`
// prefix (either case; `prefix` is 'v' for MP2_VPAD_INPUT) and returns the pad; anything else leaves `name` alone and returns 0.
inline int SplitPad(const char*& name, const char* at, char prefix = 'p') {
  if (at - name > 3 && (name[0] == prefix || name[0] == prefix - 32) && name[1] >= '0' &&
      name[1] < '0' + kPadCount && (name[2] == '.' || name[2] == ':')) {
    const int pad = name[1] - '0';
    name += 3;
    return pad;
  }
  return 0;
}

} // namespace PortInputPad
