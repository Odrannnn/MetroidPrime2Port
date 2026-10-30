// The JNI entry points of the on-screen controller: every native that
// android/app/src/main/java/org/metroidprime2/port/TouchControlsView.java declares, and nothing
// else. The view is a copy of MetroidPrimePort's, so this is that tree's
// platform/debug_ui/android_jni.cpp touch block, pointed at PortTouch (platform/port_touch.cpp)
// and the F1 overlay. Every one is a static method there, hence the unused `jclass`.

#if defined(__ANDROID__)

#include "input/port_input_devices.h"
#include "port_input_bindings.h"
#include "port_debug.h"
#include "port_overlay.h"
#include "port_photo.h"
#include "port_pointer.h"
#include "port_textures.h"
#include "port_touch.h"
#include "port_radial.h"

#include <jni.h>

#include <string>
#include <vector>

namespace PortDebug {
void SetTexturePackStatus(std::string status); // platform/debug_ui/game_sections.cpp
}

extern "C" {

// --- Settings and layout ---

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchClassic(JNIEnv*, jclass) {
  return PortTouch::Classic() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchTwinStick(JNIEnv*, jclass) {
  return PortTouch::TwinStick() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchColors(JNIEnv*,
                                                                                           jclass) {
  return PortTouch::Colors() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchLabels(JNIEnv*,
                                                                                           jclass) {
  return PortTouch::Labels() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchFloatingStick(JNIEnv*, jclass) {
  return PortTouch::FloatingStick() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchTurbo(JNIEnv*,
                                                                                          jclass) {
  return PortTouch::Turbo() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchSideMarginDp(JNIEnv*, jclass) {
  return PortTouch::SideMarginDp();
}

JNIEXPORT jfloat JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchStickInsetDp(JNIEnv*, jclass) {
  return PortTouch::StickInsetDp();
}

JNIEXPORT jfloat JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchButtonInsetDp(JNIEnv*, jclass) {
  return PortTouch::ButtonInsetDp();
}

JNIEXPORT jfloat JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchOpacity(JNIEnv*,
                                                                                          jclass) {
  return PortTouch::Opacity();
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchControlsEnabled(JNIEnv*, jclass) {
  return PortTouch::ControlsEnabled() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchWheelsEnabled(JNIEnv*, jclass) {
  return PortTouch::WheelsEnabled() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchVisorTapScan(JNIEnv*, jclass) {
  return PortTouch::VisorTapScan() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchAimEnabled(JNIEnv*, jclass) {
  return PortTouch::AimEnabled() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchEditRequested(JNIEnv*, jclass) {
  return PortTouch::TakeEditRequested() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeRequestTouchEdit(JNIEnv*, jclass) {
  PortTouch::RequestEdit();
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeCycleTouchLayout(JNIEnv*, jclass) {
  PortTouch::CycleLayout();
}

// --- Pad, aim and the held buttons ---

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeVirtualButton(
    JNIEnv*, jclass, jint button, jboolean down) {
  PortTouch::SetButton(static_cast< int >(button), down == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeVirtualAxis(
    JNIEnv*, jclass, jint axis, jfloat value) {
  PortTouch::SetAxis(static_cast< int >(axis), static_cast< float >(value));
}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchAim(JNIEnv*, jclass,
                                                                                    jfloat dxDp,
                                                                                    jfloat dyDp) {
  PortTouch::AddAim(static_cast< float >(dxDp), static_cast< float >(dyDp));
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchAimDown(JNIEnv*, jclass, jboolean down) {
  PortTouch::SetAimDown(down == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchSpringSwipe(JNIEnv*, jclass) {
  PortTouch::SpringSwipe();
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchBeamShift(JNIEnv*, jclass, jboolean held) {
  PortTouch::SetBeamShift(held == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchTurboFire(JNIEnv*, jclass, jboolean held) {
  PortTouch::SetTurboFire(held == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeSetTouchDevice(JNIEnv*, jclass, jboolean xbox) {
  PortTouch::NoteDevice(xbox == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTakePhysicalInput(JNIEnv*, jclass) {
  return PortTouch::TakePhysicalInput() ? JNI_TRUE : JNI_FALSE;
}

// --- Wheels and the game state ---

JNIEXPORT jlong JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeRadialBegin(JNIEnv*, jclass, jint kind, jint owner) {
  return PortRadial::BeginTouch(static_cast<PortRadial::Kind>(kind), owner);
}
JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeRadialMove(JNIEnv*, jclass, jlong token,
                                                             jint owner, jfloat x, jfloat y) {
  PortRadial::MoveTouch(token, owner, x, y);
}
JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeRadialEnd(JNIEnv*, jclass, jlong token,
                                                            jint owner, jfloat x, jfloat y) {
  PortRadial::EndTouch(token, owner, x, y);
}
JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeRadialCancel(JNIEnv*, jclass, jlong token, jint owner) {
  PortRadial::CancelTouch(token, owner);
}
JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeRadialCancelAll(JNIEnv*, jclass) {
  PortRadial::CancelAllTouches();
}
JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeRadialCancelLifecycle(JNIEnv*, jclass) {
  PortRadial::CancelTouchLifecycle();
}

JNIEXPORT jint JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeWheelOwned(JNIEnv*,
                                                                                      jclass) {
  return static_cast< jint >(PortTouch::WheelState());
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeRequestVisor(JNIEnv*, jclass, jint visor) {
  PortTouch::RequestVisor(static_cast< int >(visor));
}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeRequestBeam(JNIEnv*,
                                                                                       jclass,
                                                                                       jint beam) {
  PortTouch::RequestBeam(static_cast< int >(beam));
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativePauseScreenOpen(JNIEnv*, jclass) {
  return PortTouch::PauseScreenOpen() ? JNI_TRUE : JNI_FALSE;
}

// --- F1 overlay ---

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeDebugOverlayVisible(JNIEnv*, jclass) {
  return PortTouch::OverlayVisible() ? JNI_TRUE : JNI_FALSE;
}

// Photo mode without the F1 window: every finger flies the camera, so none is a scroll.
JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativePhotoTouch(JNIEnv*, jclass) {
  return PortPhoto::Active() && !PortOverlay::Visible() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeToggleDebugOverlay(JNIEnv*, jclass) {
  PortOverlay::RequestToggle();
}

JNIEXPORT void JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeDebugWheel(JNIEnv*, jclass, jfloat steps) {
  PortOverlay::Wheel(steps);
}

// --- HUD icons ---

// {width, height, ARGB pixels...} of a wheel icon (wheel 0 visor, 1 beam; item in the same order as
// nativeRequestVisor/nativeRequestBeam), or null until the HUD has loaded it.
JNIEXPORT jintArray JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeWheelIcon(
    JNIEnv* env, jclass, jint wheel, jint item) {
  std::vector< uint32_t > icon;
  if (!PortTouch::WheelIcon(wheel, item, icon)) {
    return nullptr;
  }
  const jsize count = static_cast< jsize >(icon.size());
  jintArray out = env->NewIntArray(count);
  if (out == nullptr) {
    return nullptr;
  }
  env->SetIntArrayRegion(out, 0, count, reinterpret_cast< const jint* >(icon.data()));
  return out;
}

// --- Stubs: MP2 has no backend for these (see port_touch.h) ---

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeMessageBoxOpen(JNIEnv*, jclass) {
  return JNI_FALSE; // kFS_Paused covers it; see nativePauseScreenOpen
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchMapTapEnabled(JNIEnv*, jclass) {
  return PortMinimap::Enabled() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeMinimapRect(JNIEnv* env, jclass, jfloatArray out) {
  // x0, y0, x1, y1 as fractions of the screen, then 1 when the minimap is drawn there.
  float rect[5];
  if (!PortMinimap::Enabled() || out == nullptr || env->GetArrayLength(out) < 5 ||
      !PortMinimap::RectFraction(rect)) {
    return JNI_FALSE;
  }
  rect[4] = 1.f;
  env->SetFloatArrayRegion(out, 0, 5, rect);
  return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeMapTap(JNIEnv*, jclass) {
  if (PortMinimap::Enabled()) {
    PortMinimap::RequestTap();
  }
}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeMapPan(JNIEnv*, jclass,
                                                                                  jfloat, jfloat,
                                                                                  jfloat) {}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeMapScreenOpen(JNIEnv*, jclass) {
  return JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeMapZoom(JNIEnv*, jclass,
                                                                                   jfloat) {}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeMapRotate(JNIEnv*,
                                                                                     jclass,
                                                                                     jfloat) {}

JNIEXPORT jboolean JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativePointerMenuActive(JNIEnv*, jclass) {
  // A pointer-driven screen (pause menus, map) is up: the view hands its touches to SDL.
  return PortPointer::ScreenActive() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTapUpdateToast(
    JNIEnv*, jclass, jfloat x, jfloat y) {
  return PortDebug::TapUpdateToast(x, y) ? JNI_TRUE : JNI_FALSE;
}

// Every overlay control's press and release, for touch: bindings in controls.toml
// and the Morph Ball page's and added buttons' own actions.
JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchControl(
    JNIEnv*, jclass, jint control, jboolean down) {
  PortInputDevices::SetTouchControl(static_cast<int>(control), down == JNI_TRUE);
}

// Whether a binding takes over the control, so the overlay skips its own button.
JNIEXPORT jboolean JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchControlBound(
    JNIEnv*, jclass, jint control) {
  return PortInputDevices::TouchControlBound(static_cast<int>(control)) ? JNI_TRUE : JNI_FALSE;
}

// What a ball control does in the ball, for its label: its Morph Ball row's name,
// "" for a ball button left without one, null for any other control.
JNIEXPORT jstring JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchBallLabel(
    JNIEnv* env, jclass, jint control) {
  if (const PortInput::BallRow* row =
          PortInput::FindBallRow(PortInputDevices::TouchBallRow(static_cast<int>(control)))) {
    return env->NewStringUTF(row->touch);
  }
  for (const PortInput::TouchBallControl& c : PortInput::kTouchBallControls) {
    if (c.control == control) {
      return env->NewStringUTF("");
    }
  }
  return nullptr;
}

// The actions a layout editor button can take, as key, label, key, label...
JNIEXPORT jobjectArray JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeTouchButtonActions(
    JNIEnv* env, jclass) {
  std::vector<std::string> items;
  for (int i = 0; i < PortInput::kActionCount; ++i) {
    const auto a = static_cast<PortInput::Action>(i);
    if (!PortInput::TouchButtonAction(a)) {
      continue;
    }
    const PortInput::ActionInfo& info = PortInput::Info(a);
    items.emplace_back(info.key);
    items.emplace_back(info.label);
  }
  jclass stringClass = env->FindClass("java/lang/String");
  if (stringClass == nullptr) {
    return nullptr;
  }
  jobjectArray out = env->NewObjectArray(static_cast<jsize>(items.size()), stringClass, nullptr);
  for (size_t i = 0; out != nullptr && i < items.size(); ++i) {
    jstring s = env->NewStringUTF(items[i].c_str());
    env->SetObjectArrayElement(out, static_cast<jsize>(i), s);
    env->DeleteLocalRef(s);
  }
  env->DeleteLocalRef(stringClass);
  return out;
}

JNIEXPORT jstring JNICALL
Java_org_metroidprime2_port_TouchControlsView_nativeTouchLayout(JNIEnv* env, jclass) {
  return env->NewStringUTF(PortTouch::Layout().c_str());
}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_TouchControlsView_nativeSetTouchLayout(
    JNIEnv* env, jclass, jstring layout) {
  if (layout == nullptr) {
    return;
  }
  const char* chars = env->GetStringUTFChars(layout, nullptr);
  if (chars != nullptr) {
    PortTouch::SetLayout(chars);
    env->ReleaseStringUTFChars(layout, chars);
  }
}

// The texture pack picker (MetroidPrime2Activity.pickTexturePack): progress text from the Java
// copy thread, and the signal that <user root>.new is ready for the next frame to swap in.
JNIEXPORT void JNICALL Java_org_metroidprime2_port_MetroidPrime2Activity_nativeTexturePackStatus(
    JNIEnv* env, jclass, jstring status) {
  const char* chars = env->GetStringUTFChars(status, nullptr);
  if (chars != nullptr) {
    PortDebug::SetTexturePackStatus(chars);
    env->ReleaseStringUTFChars(status, chars);
  }
}

JNIEXPORT void JNICALL Java_org_metroidprime2_port_MetroidPrime2Activity_nativeTexturePackReady(JNIEnv*,
                                                                                               jclass) {
  PortTextures::RequestUserPackReload();
}

} // extern "C"

#endif
