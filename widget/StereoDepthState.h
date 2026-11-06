/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_StereoDepthState_h
#define mozilla_widget_StereoDepthState_h

#include <cstdint>

namespace mozilla::widget {

// What the desktop tells one window about depth (Sparky Stereo OS, CSS depth;
// kde_stereo_depth_v1): whether the window may show depth now (the active
// window), the limits of the output it mainly sits on, which of its sides lie
// on that output's edge and how wide the output is. Each window has its own;
// pages read the state of the window they are shown in.
struct StereoDepthState {
  bool mAwake = true;
  // The limits come from the compositor (otherwise the display default of
  // libstereo-policy stands in).
  bool mFromCompositor = false;
  // Limits in pixels of a view 1920 wide, times 100.
  int32_t mSunk100 = 0;
  int32_t mPopped100 = 0;
  // The window's sides on the output's edge: 1 left, 2 top, 4 right, 8 bottom.
  uint32_t mOutputEdges = 0;
  // The output's width in device pixels, 0 when not known.
  int32_t mOutputWidth = 0;

  bool operator==(const StereoDepthState& aOther) const {
    return mAwake == aOther.mAwake && mFromCompositor == aOther.mFromCompositor &&
           mSunk100 == aOther.mSunk100 && mPopped100 == aOther.mPopped100 &&
           mOutputEdges == aOther.mOutputEdges &&
           mOutputWidth == aOther.mOutputWidth;
  }
  bool operator!=(const StereoDepthState& aOther) const {
    return !(*this == aOther);
  }
};

}  // namespace mozilla::widget

#endif
