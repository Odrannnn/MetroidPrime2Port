#pragma once

#include <cmath>

namespace PortFlickStick {

constexpr float kPi = 3.14159265358979323846f;
// The stick flicks once it is tilted past kFlickThreshold and re-arms once it
// is back under kReleaseThreshold (the gap keeps a stick resting on its gate
// from flicking twice).
constexpr float kFlickThreshold = 0.9f;
constexpr float kReleaseThreshold = 0.75f;
// A flick turns over this long, easing out, rather than snapping in one tick.
constexpr float kFlickTime = 0.1f;
// Turning the held stick: changes slower than kSmoothRate (rad/s) are averaged
// over kSmoothWindow seconds, faster than kDirectRate pass straight through, and
// a blend in between (GyroWiki's tiered smoothing). The C-stick has about one
// degree of resolution at full tilt; averaging cancels that jitter but loses no
// turn (what is held back comes out on release), it only delays slow turns.
constexpr float kSmoothRate = kPi;        // 180 deg/s
constexpr float kDirectRate = 2.f * kPi;  // 360 deg/s
constexpr float kSmoothWindow = 1.f / 15.f;

// Flick Stick (Jibb Smart's scheme): tilting the stick turns the view to face
// the stick's direction relative to the current view (right turns 90 degrees,
// down turns around), and rotating the held stick turns the view by the same
// angle. Only yaw: pitch is left to gyro aim.
class State {
public:
  // (x, y): the stick, x right and y up, on the unit circle. Returns the yaw to
  // turn this tick in radians, right positive.
  float Update(float x, float y, float dt) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dt) || dt <= 0.f) return 0.f;
    float turn = 0.f;
    // The flick under way goes on whatever the stick does.
    if (mFlickLeft > 0.f) {
      const float t0 = 1.f - mFlickLeft / kFlickTime;
      mFlickLeft = std::fmax(mFlickLeft - dt, 0.f);
      const float t1 = 1.f - mFlickLeft / kFlickTime;
      turn += mFlickAngle * (Ease(t1) - Ease(t0));
    }
    const float tilt = std::hypot(x, y);
    if (mHeld || mBlocked) {
      if (tilt < kReleaseThreshold) {
        // What smoothing still held back is turned on release.
        turn += mPending;
        mHeld = mBlocked = false;
        ClearSmoothing();
        return turn;
      }
      if (mBlocked) return turn;
      const float angle = std::atan2(x, y);
      const float delta = std::remainder(angle - mLastAngle, 2.f * kPi);
      mLastAngle = angle;
      return turn + Smoothed(delta, dt);
    }
    if (tilt < kFlickThreshold) return turn;
    // A new flick: whatever the last one had left to turn is turned now.
    if (mFlickLeft > 0.f) turn += mFlickAngle * (1.f - Ease(1.f - mFlickLeft / kFlickTime));
    mFlickAngle = std::atan2(x, y);
    mLastAngle = mFlickAngle;
    mHeld = true;
    ClearSmoothing();
    const float t1 = std::fmin(dt / kFlickTime, 1.f);
    mFlickLeft = kFlickTime * (1.f - t1);
    return turn + mFlickAngle * Ease(t1);
  }

  // Drops the flick under way; a stick still tilted past the threshold must be
  // let go before it flicks again.
  void Reset() {
    mFlickLeft = 0.f;
    mHeld = false;
    mBlocked = true;
    ClearSmoothing();
  }

private:
  static float Ease(float t) {
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    return 1.f - (1.f - t) * (1.f - t);
  }

  float Smoothed(float delta, float dt) {
    const float rate = std::fabs(delta) / dt;
    float direct = (rate - kSmoothRate) / (kDirectRate - kSmoothRate);
    direct = direct < 0.f ? 0.f : direct > 1.f ? 1.f : direct;
    int window = static_cast< int >(std::lround(kSmoothWindow / dt));
    window = window < 1 ? 1 : window > kHistory ? kHistory : window;
    mHistory[mHistoryAt] = delta * (1.f - direct);
    mPending += mHistory[mHistoryAt];
    mHistoryAt = (mHistoryAt + 1) % kHistory;
    float sum = 0.f;
    for (int i = 1; i <= window; ++i) sum += mHistory[(mHistoryAt - i + kHistory) % kHistory];
    const float smoothed = sum / static_cast< float >(window);
    mPending -= smoothed;
    return delta * direct + smoothed;
  }

  void ClearSmoothing() {
    for (float& h : mHistory) h = 0.f;
    mHistoryAt = 0;
    mPending = 0.f;
  }

  static constexpr int kHistory = 16;
  float mHistory[kHistory] = {};
  int mHistoryAt = 0;
  float mPending = 0.f;  // smoothed turn taken in but not yet returned
  float mFlickAngle = 0.f;
  float mFlickLeft = 0.f;
  float mLastAngle = 0.f;
  bool mHeld = false;
  bool mBlocked = false;
};

} // namespace PortFlickStick
