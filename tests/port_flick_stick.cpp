#include "port_flick_stick.h"
#include "port_test.h"

#include <cmath>

namespace {
using PortFlickStick::kPi;
constexpr float kDt = 1.f / 60.f;

bool Near(float a, float b, float eps = 0.0005f) { return std::fabs(a - b) < eps; }

// Holds the stick at angle (0 up, right positive) and tilt for n ticks; returns the turn.
float Hold(PortFlickStick::State& s, float angle, float tilt, int n, float dt = kDt) {
  float sum = 0.f;
  for (int i = 0; i < n; ++i) sum += s.Update(std::sin(angle) * tilt, std::cos(angle) * tilt, dt);
  return sum;
}
} // namespace

int main() {
  {
    // A flick right turns a quarter turn, eased over kFlickTime rather than at once.
    PortFlickStick::State s;
    const float first = s.Update(1.f, 0.f, kDt);
    CHECK(first > 0.f && first < kPi / 2.f);
    const float total = first + Hold(s, kPi / 2.f, 1.f, 30);
    CHECK(Near(total, kPi / 2.f));
    // Held still, nothing more; released, nothing more.
    CHECK(Near(Hold(s, kPi / 2.f, 1.f, 30), 0.f));
    CHECK(Near(Hold(s, 0.f, 0.f, 10), 0.f));
  }
  {
    // Left and down; the same at 120 Hz ticks.
    PortFlickStick::State s;
    CHECK(Near(Hold(s, -kPi / 2.f, 1.f, 20), -kPi / 2.f));
    Hold(s, 0.f, 0.f, 2);
    CHECK(Near(std::fabs(Hold(s, kPi * 0.999f, 1.f, 20)), kPi * 0.999f, 0.002f));
    Hold(s, 0.f, 0.f, 2);
    CHECK(Near(Hold(s, kPi / 4.f, 1.f, 40, 1.f / 120.f), kPi / 4.f));
  }
  {
    // Short of the threshold no flick; past it one, until let go under the release threshold.
    PortFlickStick::State s;
    CHECK(Near(Hold(s, kPi / 2.f, 0.85f, 20), 0.f));
    CHECK(Near(Hold(s, kPi / 2.f, 1.f, 20), kPi / 2.f));
    CHECK(Near(Hold(s, kPi / 2.f, 0.8f, 5) + Hold(s, kPi / 2.f, 1.f, 20), 0.f));
    Hold(s, kPi / 2.f, 0.5f, 1);
    CHECK(Near(Hold(s, kPi / 2.f, 1.f, 20), kPi / 2.f));
  }
  {
    // Rotating the held stick turns with it, slow turns too (smoothing delays, never drops).
    PortFlickStick::State s;
    float total = Hold(s, kPi / 2.f, 1.f, 10);
    for (int i = 1; i <= 120; ++i) total += Hold(s, kPi / 2.f + kPi / 2.f * i / 120.f, 1.f, 1);
    total += Hold(s, 0.f, 0.f, 1);
    CHECK(Near(total, kPi));
    // A quick turn the other way, across the stick's bottom.
    total = Hold(s, kPi * 0.9f, 1.f, 10);
    for (int i = 1; i <= 6; ++i) total += Hold(s, kPi * 0.9f + kPi * 0.2f * i / 6.f, 1.f, 1);
    total += Hold(s, 0.f, 0.f, 1);
    CHECK(Near(total, kPi * 0.9f + kPi * 0.2f, 0.002f));
  }
  {
    // A stick jittering by a count of the C-stick's gate (about a degree) barely moves the view
    // and comes back to where the stick is.
    PortFlickStick::State s;
    const float base = 0.6f;
    const float jitter = 1.f / 59.f;
    float total = Hold(s, base, 1.f, 10);
    float most = 0.f;
    for (int i = 0; i < 60; ++i) {
      const float turn = Hold(s, base + (i % 2 ? jitter : 0.f), 1.f, 1);
      most = std::fmax(most, std::fabs(turn));
      total += turn;
    }
    CHECK(most <= jitter * 0.3f);
    total += Hold(s, base, 1.f, 1) + Hold(s, 0.f, 0.f, 1);
    CHECK(Near(total, base, 0.002f));
  }
  {
    // A new flick right after one turns the rest of the first at once.
    PortFlickStick::State s;
    float total = Hold(s, kPi / 2.f, 1.f, 2);
    total += Hold(s, 0.f, 0.f, 1);
    total += Hold(s, kPi / 2.f, 1.f, 20);
    CHECK(Near(total, kPi));
  }
  {
    // Reset drops the flick under way and waits for the stick to be let go.
    PortFlickStick::State s;
    Hold(s, kPi / 2.f, 1.f, 1);
    s.Reset();
    CHECK(Near(Hold(s, kPi / 2.f, 1.f, 20) + Hold(s, kPi, 1.f, 20), 0.f));
    Hold(s, 0.f, 0.f, 1);
    CHECK(Near(Hold(s, -kPi / 2.f, 1.f, 20), -kPi / 2.f));
    // Bad input turns nothing.
    CHECK(s.Update(NAN, 0.f, kDt) == 0.f && s.Update(1.f, 0.f, 0.f) == 0.f);
  }
  return PORT_TEST_RESULT();
}
