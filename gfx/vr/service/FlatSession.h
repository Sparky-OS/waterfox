/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_VR_SERVICE_FLATSESSION_H
#define GFX_VR_SERVICE_FLATSESSION_H

#include "VRSession.h"

#include "mozilla/TimeStamp.h"
#include "moz_external_vr.h"

namespace mozilla {
namespace gfx {

// The flat stereo display: the screen itself as an immersive XR device, with
// no headset and no runtime. The viewer sits in front of the screen, which is
// the window both eyes look through; the content process places each eye's
// frustum through that window (dom/vr/XRSession) and the session's canvas
// shows the frame, both eyes side by side, declared to the compositor. This
// session only reports the device and paces the frames.
class FlatSession : public VRSession {
 public:
  FlatSession();
  virtual ~FlatSession();

  bool Initialize(mozilla::gfx::VRSystemState& aSystemState,
                  bool aDetectRuntimesOnly) override;
  void Shutdown() override;
  void ProcessEvents(mozilla::gfx::VRSystemState& aSystemState) override;
  void StartFrame(mozilla::gfx::VRSystemState& aSystemState) override;
  bool StartPresentation() override;
  void StopPresentation() override;
  void VibrateHaptic(uint32_t aControllerIdx, uint32_t aHapticIndex,
                     float aIntensity, float aDuration) override;
  void StopVibrateHaptic(uint32_t aControllerIdx) override;
  void StopAllHaptics() override;

 protected:
  bool SubmitFrameWithoutTexture(
      const mozilla::gfx::VRLayer_Stereo_Immersive& aLayer) override;

 private:
  TimeStamp mLastFrameStart;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // GFX_VR_SERVICE_FLATSESSION_H
