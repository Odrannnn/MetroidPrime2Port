// The third-person camera's pure maths (platform/include/port_third_person.h).
#include "port_test.h"
#include "port_third_person.h"

#include <cmath>

using namespace PortThirdPerson;

static bool Near(const Vec3& a, const Vec3& b) {
  return std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f && std::fabs(a.z - b.z) < 1e-5f;
}

int main() {
  const Vec3 eye{10.f, 20.f, 30.f};
  const Vec3 fwd{0.f, 1.f, 0.f}, right{1.f, 0.f, 0.f}, up{0.f, 0.f, 1.f};
  const Params p; // 3.5, 0.9, 0.25
  CHECK(Near(ComputeEye(eye, fwd, right, up, p), Vec3{10.9f, 16.5f, 30.25f}));
  Params q;
  q.distance = 1.f;
  q.shoulder = -1.f;
  q.height = 0.f;
  CHECK(Near(ComputeEye(eye, fwd, right, up, q), Vec3{9.f, 19.f, 30.f}));

  // Framing: unchanged at the reference view; a wider screen moves her out sideways only, a
  // wider FOV both ways; nonsense views keep the settings.
  const Params ref = Framed(p, kRefTanHalfFov, kRefAspect);
  CHECK(std::fabs(ref.shoulder - 0.9f) < 1e-5f && std::fabs(ref.height - 0.25f) < 1e-5f);
  const Params phone = Framed(p, kRefTanHalfFov, kRefAspect * 1.25f);
  CHECK(std::fabs(phone.shoulder - 1.125f) < 1e-5f && std::fabs(phone.height - 0.25f) < 1e-5f);
  CHECK(phone.distance == p.distance);
  const Params wide = Framed(p, kRefTanHalfFov * 2.f, kRefAspect);
  CHECK(std::fabs(wide.shoulder - 1.8f) < 1e-5f && std::fabs(wide.height - 0.5f) < 1e-5f);
  CHECK(Framed(p, 0.f, 1.f).shoulder == p.shoulder && Framed(p, 1.f, NAN).height == p.height);

  const Vec3 pivot{0.f, 0.f, 0.f}, desired{0.f, -4.f, 0.f};
  CHECK(Near(PullIn(pivot, desired, -1.f), desired));            // no hit
  CHECK(Near(PullIn(pivot, desired, 4.f), desired));             // hit at the end: not closer
  CHECK(Near(PullIn(pivot, desired, 5.f), desired));             // hit beyond
  CHECK(Near(PullIn(pivot, desired, 2.f), Vec3{0.f, -1.8f, 0.f})); // 2.0 - 0.2 margin
  CHECK(Near(PullIn(pivot, desired, 0.1f), pivot));              // inside the margin
  CHECK(Near(PullIn(pivot, pivot, 1.f), pivot));                 // degenerate line
  CHECK(std::fabs(PullInLength(4.f, 2.f) - 1.8f) < 1e-5f);

  // Aim: the shot goes from the cannon to the point under the centre ray.
  const Vec3 cannon{1.f, 0.f, 0.f}, camFwd{0.f, 1.f, 0.f};
  CHECK(Near(AimDirection(cannon, Vec3{1.f, 10.f, 0.f}, camFwd), Vec3{0.f, 1.f, 0.f}));
  CHECK(Near(AimDirection(cannon, Vec3{-3.f, 3.f, 0.f}, camFwd), Vec3{-0.8f, 0.6f, 0.f}));
  CHECK(Near(AimDirection(cannon, Vec3{1.f, 0.2f, 0.f}, camFwd), camFwd));  // too near
  CHECK(Near(AimDirection(cannon, Vec3{1.f, -4.f, 0.f}, camFwd), camFwd));  // behind the camera
  CHECK(Near(AimDirection(cannon, cannon, camFwd), camFwd));                 // degenerate
  return PORT_TEST_RESULT();
}
