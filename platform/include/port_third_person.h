#pragma once

#include <algorithm>
#include <cmath>

class CStateManager;
class CTransform4f;
class CVector3f;

// An opt-in third-person gameplay camera: while Samus stands unmorphed under the first-person
// camera, the world is drawn from behind her shoulder, her body is shown and the arm cannon
// and helmet are left out. Morph ball, cutscenes, path/spindle cameras, the free camera and
// multiplayer keep their own views. Setting `third_person` (F1 > Video, console `thirdperson`).
// MP1's (../MetroidPrimePort) without its body module: Echoes' single-player Samus animates.
namespace PortThirdPerson {

// Shoulder and height are for the reference view (retail 55 degree FOV at 16:9); Framed() scales
// them to the live one so she keeps her place on the screen beside the crosshair.
struct Params {
  float distance = 3.5f; // metres behind the eye
  float shoulder = 0.9f; // metres to the right (the centre ray, the crosshair, clears her right pad)
  float height = 0.25f;  // metres above the first-person eye (above the body's shoulder)
};
constexpr float kDistanceMin = 0.5f, kDistanceMax = 10.f;
constexpr float kShoulderMin = -2.f, kShoulderMax = 2.f;
constexpr float kHeightMin = -1.f, kHeightMax = 2.f;
constexpr float kWallMargin = 0.2f; // metres kept between the camera and the wall

struct Vec3 {
  float x = 0.f, y = 0.f, z = 0.f;
};

// The view the shoulder and height are set for: tan(27.5 degrees), 16:9.
constexpr float kRefTanHalfFov = 0.52056705f, kRefAspect = 16.f / 9.f;

// `p` for a view of vertical FOV tan(fov/2) = `tanHalfFov` at `aspect`: the offsets grow with
// the half-extents of the view, so a wider screen or FOV doesn't slide her under the crosshair.
inline Params Framed(const Params& p, float tanHalfFov, float aspect) {
  if (!(tanHalfFov > 0.f) || !(aspect > 0.f) || !std::isfinite(tanHalfFov * aspect)) {
    return p;
  }
  Params out = p;
  out.shoulder *= tanHalfFov * aspect / (kRefTanHalfFov * kRefAspect);
  out.height *= tanHalfFov / kRefTanHalfFov;
  return out;
}

// Pure maths (no game state): where the camera wants to be, and where it ends up when a wall
// is hit `hitDistance` along the line from the pivot to it (negative: no hit).
inline Vec3 ComputeEye(const Vec3& eye, const Vec3& forward, const Vec3& right, const Vec3& up,
                       const Params& p) {
  return {eye.x + right.x * p.shoulder + up.x * p.height - forward.x * p.distance,
          eye.y + right.y * p.shoulder + up.y * p.height - forward.y * p.distance,
          eye.z + right.z * p.shoulder + up.z * p.height - forward.z * p.distance};
}

// How far along the pivot->desired line the camera may go.
inline float PullInLength(float fullLength, float hitDistance, float margin = kWallMargin) {
  if (hitDistance >= 0.f && hitDistance < fullLength) {
    return std::max(0.f, hitDistance - margin);
  }
  return fullLength;
}

inline Vec3 PullIn(const Vec3& pivot, const Vec3& desired, float hitDistance,
                   float margin = kWallMargin) {
  const Vec3 d{desired.x - pivot.x, desired.y - pivot.y, desired.z - pivot.z};
  const float full = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
  if (!(full > 0.f)) {
    return desired;
  }
  const float len = PullInLength(full, hitDistance, margin);
  if (len >= full) {
    return desired;
  }
  const float s = len / full;
  return {pivot.x + d.x * s, pivot.y + d.y * s, pivot.z + d.z * s};
}

// The unit direction a shot leaves `origin` to hit `aim` (the point under the camera's centre
// ray). A target nearer than kAimMinDistance, or one behind the camera's facing (between the
// camera and Samus), would turn the shot around or aim it wildly: those fire along `camForward`.
constexpr float kAimMinDistance = 0.5f;
constexpr float kAimRange = 100.f; // the gun's free-aim distance
inline Vec3 AimDirection(const Vec3& origin, const Vec3& aim, const Vec3& camForward) {
  const Vec3 d{aim.x - origin.x, aim.y - origin.y, aim.z - origin.z};
  const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
  if (len >= kAimMinDistance && d.x * camForward.x + d.y * camForward.y + d.z * camForward.z > 0.f) {
    return {d.x / len, d.y / len, d.z / len};
  }
  return camForward;
}

bool Enabled();
void SetEnabled(bool on);
// Third person is drawing this tick (enabled, and the camera and player are in a state it takes).
bool Active();
// Third person may take the view (on, single player, no cinematic or free camera), whatever
// state she is in.
bool ViewAllowed(const CStateManager& mgr);
// Her body is drawn for third person: Active(), or the ball camera has taken over from the
// first-person one for a morph that has not begun yet (she still stands).
bool ShowBody(const CStateManager& mgr);

Params GetParams();
void SetDistance(float metres);
void SetShoulder(float metres);
void SetHeight(float metres);

// Once per game tick, after the camera manager's update.
void Update(CStateManager& mgr, float dt);
// A world is going (~CStateManager): drop the camera state computed from it. `third_person`
// itself is kept.
void Reset();
// Where the camera's centre ray meets the world or a solid actor (the far point if it meets
// nothing), found once per tick; false when not Active().
bool AimPoint(CVector3f& out);
// The camera's tick position (for the console); valid while Active().
void GetCameraPosition(float out[3]);
// The view the world is drawn from: the first-person view moved back to the blended,
// wall-clamped distance; `game` when not active.
CTransform4f View(const CTransform4f& game);
// The same at this tick's camera distance, for what the simulation projects (orbit target
// search, lock-on boxes); `game` when not active.
CTransform4f TickView(const CTransform4f& game);
// Where the camera is (while Active()) or would come to rest (behind her, pulled in at a wall)
// for the first-person view `fp`: the morph ball camera starts there and ends there, so
// morphing leaves and rejoins third person without a cut to the eye. False when third person
// will not take the view once she stands (off, multiplayer, a cinematic, the free camera).
bool MorphCameraPosition(const CStateManager& mgr, const CTransform4f& fp, CVector3f& out);

} // namespace PortThirdPerson
