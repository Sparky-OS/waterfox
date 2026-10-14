/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FlatSession.h"

#include <string.h>

#include "gfxVR.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/gfx/gfxVars.h"
#include "prthread.h"

namespace mozilla::gfx {

// The frames are paced at the display's usual rate.
static constexpr double kFrameInterval = 1000.0 / 60.0;

FlatSession::FlatSession() = default;

FlatSession::~FlatSession() { Shutdown(); }

bool FlatSession::Initialize(mozilla::gfx::VRSystemState& aSystemState,
                             bool aDetectRuntimesOnly) {
  if (!StaticPrefs::dom_vr_enabled() || !StaticPrefs::dom_vr_flat_enabled()) {
    return false;
  }
  // Only while an output shows stereo 3D, or can (nsWaylandDisplay).
  if (!gfxVars::StereoOutputs()) {
    return false;
  }
  if (aDetectRuntimesOnly) {
    aSystemState.displayState.capabilityFlags |=
        VRDisplayCapabilityFlags::Cap_ImmersiveVR;
    return false;
  }

  VRDisplayState& state = aSystemState.displayState;
  strncpy(state.displayName.data(), "Stereo display", kVRDisplayNameMaxLen - 1);
  state.eightCC = kVRFlatDisplayEightCC;
  state.capabilityFlags = VRDisplayCapabilityFlags::Cap_Orientation |
                          VRDisplayCapabilityFlags::Cap_Position |
                          VRDisplayCapabilityFlags::Cap_Present |
                          VRDisplayCapabilityFlags::Cap_StageParameters |
                          VRDisplayCapabilityFlags::Cap_ImmersiveVR;
  state.blendMode = VRDisplayBlendMode::Opaque;

  // The content process replaces the eyes' fields of view and resolution
  // with the window the screen makes (dom/vr/XRSession.cpp); these stand for
  // a 16:9 screen until then.
  const float eyeOffset =
      0.0005f * float(StaticPrefs::dom_vr_flat_eye_separation_mm());
  for (uint32_t eye = 0; eye < VRDisplayState::NumEyes; eye++) {
    state.eyeFOV[eye] = VRFieldOfView(16.9, 26.3, 16.9, 26.3);
    state.eyeTranslation[eye].x =
        eye == VRDisplayState::Eye_Left ? -eyeOffset : eyeOffset;
  }
  state.eyeResolution.width = 1920;
  state.eyeResolution.height = 1080;
  state.nativeFramebufferScaleFactor = 1.0f;
  state.isConnected = true;
  state.isMounted = true;

  // A seated viewer: the eyes 1.2 m above the floor.
  state.stageSize.width = 2.0f;
  state.stageSize.height = 2.0f;
  memset(state.sittingToStandingTransform.data(), 0,
         sizeof(state.sittingToStandingTransform));
  state.sittingToStandingTransform[0] = 1.0f;
  state.sittingToStandingTransform[5] = 1.0f;
  state.sittingToStandingTransform[10] = 1.0f;
  state.sittingToStandingTransform[13] = 1.2f;
  state.sittingToStandingTransform[15] = 1.0f;

  VRHMDSensorState& sensor = aSystemState.sensorState;
  sensor.flags = VRDisplayCapabilityFlags::Cap_Orientation |
                 VRDisplayCapabilityFlags::Cap_Position;
  sensor.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
  sensor.pose.position = {0.0f, 0.0f, 0.0f};
  return true;
}

void FlatSession::Shutdown() {}

void FlatSession::ProcessEvents(mozilla::gfx::VRSystemState& aSystemState) {
  // Nothing to poll: avoid spinning the service thread between frames.
  PR_Sleep(PR_MillisecondsToInterval(1));
}

void FlatSession::StartFrame(mozilla::gfx::VRSystemState& aSystemState) {
  const TimeStamp now = TimeStamp::Now();
  if (!mLastFrameStart.IsNull()) {
    const double elapsed = (now - mLastFrameStart).ToMilliseconds();
    if (elapsed < kFrameInterval) {
      PR_Sleep(PR_MillisecondsToInterval(uint32_t(kFrameInterval - elapsed)));
    }
  }
  mLastFrameStart = TimeStamp::Now();
}

bool FlatSession::StartPresentation() {
  mLastFrameStart = TimeStamp();
  return true;
}

void FlatSession::StopPresentation() {}

bool FlatSession::SubmitFrameWithoutTexture(
    const mozilla::gfx::VRLayer_Stereo_Immersive& aLayer) {
  return true;
}

void FlatSession::VibrateHaptic(uint32_t aControllerIdx, uint32_t aHapticIndex,
                                float aIntensity, float aDuration) {}

void FlatSession::StopVibrateHaptic(uint32_t aControllerIdx) {}

void FlatSession::StopAllHaptics() {}

}  // namespace mozilla::gfx
