/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_STEREOPOLICY_H
#define MOZILLA_LAYERS_STEREOPOLICY_H

#include <cstdint>

#include "ImageTypes.h"
#include "mozilla/gfx/Point.h"

namespace mozilla::layers {

// libstereo-policy (Sparky Stereo OS, stereo-policy.h), the stereo policy all
// of the edition's programs share, in plain C, loaded at run time: which
// packing a video frame holds (its own mark first, then the user's choice,
// then the frame's shape, with the user's Swap Eyes over whichever decided),
// whether its views are full or half, and a file's own Swap Eyes correction.
// The browser reads the marks itself and keeps no copy of the rules.
struct StereoPolicy {
  // enum stereo_policy_layout and stereo_policy_order of stereo-policy.h.
  static constexpr int kLayoutNone = 0;
  static constexpr int kSideBySide = 1;
  static constexpr int kTopAndBottom = 2;
  static constexpr int kLeftFirst = 0;
  static constexpr int kRightFirst = 1;

  int (*videoLayout)(int aWidth, int aHeight, const char* aMark,
                     int aUserLayout, int aUserOrder, int aSwapEyes,
                     int* aLayout, int* aOrder, int* aSource) = nullptr;
  int (*videoFullViews)(int aWidth, int aHeight, int aLayout) = nullptr;
  // The extended attribute user.stereo-policy.swap-eyes of a file: 1, 0, or
  // -1 when it cannot be read. Null in libraries older than 1.4.
  int (*fileSwapEyes)(const char* aPath) = nullptr;
};

// Null without the library.
const StereoPolicy* GetStereoPolicy();

// A picture's mark as the Stereo3DLayout word the policy reads (RFC 9559
// StereoMode and H.264/H.265 Table D-8 packings, with their eye order).
const char* StereoLayoutWord(StereoMode aMode);

bool IsSideBySide(StereoMode aMode);
bool IsTopAndBottom(StereoMode aMode);

// How the views of a picture fill its place.
enum class StereoFit : uint8_t {
  // The place has the shape of one view.
  None,
  // The place has the shape of two full size views side by side, or on top
  // of each other: one view is shown at its shape, centred in it.
  HalfWidth,
  HalfHeight,
};

struct StereoDecision {
  // LEFT_RIGHT, RIGHT_LEFT, TOP_BOTTOM, BOTTOM_TOP, or MONO for one picture.
  StereoMode mPacking = StereoMode::MONO;
  StereoFit mFit = StereoFit::None;
  // What the policy was asked and answered, for logs.
  const char* mMark = nullptr;
  int mUserLayout = StereoPolicy::kLayoutNone;
  int mUserOrder = StereoPolicy::kLeftFirst;
  int mLayout = StereoPolicy::kLayoutNone;
  int mOrder = StereoPolicy::kLeftFirst;
  int mSource = 0;
};

// How a picture holds two views (the same in every program of the edition):
// the stream's frame packing SEI or the container's mark, both read by the
// browser, then the user's choice in the 3D Video menu, then the frame's
// shape; the user's Swap Eyes goes over whichever decided. A mark with the
// size of one view (Matroska's DisplayWidth and DisplayHeight), a WebXR
// session's canvas and the camera hack's canvas show one view over the place;
// otherwise the policy tells whether the views are full (one shown at its
// own shape in the middle of the place) or half. Packings other than side by
// side and top and bottom are shown flat.
StereoDecision DecideStereo(StereoMode aMark, StereoSource aSource, bool aSwap,
                            bool aIsVideo, const gfx::IntSize& aShape);

}  // namespace mozilla::layers

#endif  // MOZILLA_LAYERS_STEREOPOLICY_H
