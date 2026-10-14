/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/StereoPolicy.h"

#include <dlfcn.h>

#include "mozilla/Logging.h"

static mozilla::LazyLogModule sStereoPolicyLog("StereoPolicy");

namespace mozilla::layers {

const StereoPolicy* GetStereoPolicy() {
  static const StereoPolicy* sStereoPolicy = []() -> const StereoPolicy* {
    void* handle = dlopen("libstereo-policy.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
      MOZ_LOG(sStereoPolicyLog, LogLevel::Debug,
              ("libstereo-policy.so.1 is missing: %s", dlerror()));
      return nullptr;
    }
    static StereoPolicy lib;
    lib.videoLayout = reinterpret_cast<decltype(lib.videoLayout)>(
        dlsym(handle, "stereo_policy_video_layout"));
    lib.videoFullViews = reinterpret_cast<decltype(lib.videoFullViews)>(
        dlsym(handle, "stereo_policy_video_full_views"));
    lib.fileSwapEyes = reinterpret_cast<decltype(lib.fileSwapEyes)>(
        dlsym(handle, "stereo_policy_file_swap_eyes"));
    if (!lib.videoLayout || !lib.videoFullViews) {
      MOZ_LOG(sStereoPolicyLog, LogLevel::Debug,
              ("libstereo-policy.so.1 lacks its video entries"));
      return nullptr;
    }
    return &lib;
  }();
  return sStereoPolicy;
}

const char* StereoLayoutWord(StereoMode aMode) {
  switch (aMode) {
    case StereoMode::MONO:
      return "mono";
    case StereoMode::LEFT_RIGHT:
      return "sideBySideLeftFirst";
    case StereoMode::RIGHT_LEFT:
      return "sideBySideRightFirst";
    case StereoMode::TOP_BOTTOM:
      return "topBottomLeftFirst";
    case StereoMode::BOTTOM_TOP:
      return "topBottomRightFirst";
    case StereoMode::CHECKERBOARD_LEFT_FIRST:
      return "checkerboardLeftFirst";
    case StereoMode::CHECKERBOARD_RIGHT_FIRST:
      return "checkerboardRightFirst";
    case StereoMode::ROWS_LEFT_FIRST:
      return "rowInterleavedLeftFirst";
    case StereoMode::ROWS_RIGHT_FIRST:
      return "rowInterleavedRightFirst";
    case StereoMode::COLUMNS_LEFT_FIRST:
      return "columnInterleavedLeftFirst";
    case StereoMode::COLUMNS_RIGHT_FIRST:
      return "columnInterleavedRightFirst";
    case StereoMode::ANAGLYPH_CYAN_RED:
      return "anaglyphCyanRed";
    case StereoMode::ANAGLYPH_GREEN_MAGENTA:
      return "anaglyphGreenMagenta";
    case StereoMode::LACED_LEFT_FIRST:
      return "blockLacedLeftFirst";
    case StereoMode::LACED_RIGHT_FIRST:
      return "blockLacedRightFirst";
    default:
      return "mono";
  }
}

bool IsSideBySide(StereoMode aMode) {
  return aMode == StereoMode::LEFT_RIGHT || aMode == StereoMode::RIGHT_LEFT;
}

bool IsTopAndBottom(StereoMode aMode) {
  return aMode == StereoMode::TOP_BOTTOM || aMode == StereoMode::BOTTOM_TOP;
}

StereoDecision DecideStereo(StereoMode aMark, StereoSource aSource, bool aSwap,
                            bool aIsVideo, const gfx::IntSize& aShape) {
  StereoDecision decision;
  const bool viewSized = aSource == StereoSource::XR ||
                         aSource == StereoSource::CAMERA ||
                         aSource == StereoSource::CONTAINER_SIZED;
  if (aSource == StereoSource::XR || aSource == StereoSource::CAMERA) {
    // Drawn by the browser, left eye first.
    decision.mPacking = aMark;
  } else if (aSource != StereoSource::NONE || aIsVideo) {
    // The user's choice of "Not 3D" holds no packing, as a "mono" mark.
    const bool user = aSource == StereoSource::USER;
    decision.mMark = user ? (aMark == StereoMode::MONO ? "mono" : nullptr)
                     : aSource == StereoSource::NONE ? nullptr
                                                     : StereoLayoutWord(aMark);
    if (user && aMark != StereoMode::MONO) {
      decision.mUserLayout = IsTopAndBottom(aMark) ? StereoPolicy::kTopAndBottom
                                                   : StereoPolicy::kSideBySide;
      decision.mUserOrder =
          aMark == StereoMode::RIGHT_LEFT || aMark == StereoMode::BOTTOM_TOP
              ? StereoPolicy::kRightFirst
              : StereoPolicy::kLeftFirst;
    }
    if (const auto* lib = GetStereoPolicy()) {
      if (lib->videoLayout(aShape.width, aShape.height, decision.mMark,
                           decision.mUserLayout, decision.mUserOrder, aSwap,
                           &decision.mLayout, &decision.mOrder,
                           &decision.mSource) == 0) {
        const bool rightFirst = decision.mOrder == StereoPolicy::kRightFirst;
        decision.mPacking =
            decision.mLayout == StereoPolicy::kSideBySide
                ? (rightFirst ? StereoMode::RIGHT_LEFT : StereoMode::LEFT_RIGHT)
            : decision.mLayout == StereoPolicy::kTopAndBottom
                ? (rightFirst ? StereoMode::BOTTOM_TOP : StereoMode::TOP_BOTTOM)
                : StereoMode::MONO;
        if (!viewSized && decision.mPacking != StereoMode::MONO &&
            lib->videoFullViews(aShape.width, aShape.height,
                                decision.mLayout) == 1) {
          decision.mFit = IsSideBySide(decision.mPacking)
                              ? StereoFit::HalfWidth
                              : StereoFit::HalfHeight;
        }
      }
    } else if (aSource != StereoSource::NONE) {
      // Without the policy: the mark as it is, half views unless their size
      // is given.
      decision.mPacking = aSwap ? SwapStereoEyes(aMark) : aMark;
    }
  }
  // Other arrangements (checkerboard, rows, columns, anaglyph, frame
  // sequence) are shown flat.
  if (!IsSideBySide(decision.mPacking) && !IsTopAndBottom(decision.mPacking)) {
    decision.mPacking = StereoMode::MONO;
    decision.mFit = StereoFit::None;
  }
  return decision;
}

}  // namespace mozilla::layers
