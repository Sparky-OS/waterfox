/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_STEREO_CAMERA_H_
#define WEBGL_STEREO_CAMERA_H_

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "mozilla/Maybe.h"

namespace mozilla::webgl {

// The camera hack (Sparky Stereo OS): a page that draws a 3D scene with
// WebGL and no WebXR hands its camera to the GPU as a mat4 uniform, the
// projection or the view-projection product. The browser recognises that
// camera and draws the page's view of the scene once per eye with two
// cameras: parallel axes, frustums shifted (off-axis) so that they share one
// window at the zero-parallax distance C, never toed in. In clip space that
// is x' = x + s * (w - C) for each eye, with s = -/+ half the separation, a
// fraction of the view's width (a point at infinity moves by s, a point at
// distance C stays). Everything else (the other uniforms, the shaders, the
// passes into the page's own framebuffers) stays as the page made it.

struct StereoCamera {
  // The page's own value, column-major.
  std::array<float, 16> mono = {};
  // The zero-parallax distance, in the camera's units of depth.
  float convergence = 0.0f;
};

inline bool StereoCameraFinite(const float* aM) {
  for (int i = 0; i < 16; i++) {
    if (!std::isfinite(aM[i])) {
      return false;
    }
  }
  return true;
}

// The camera in a column-major mat4, if it is one: a perspective projection
// (the last row is (0, 0, -1, 0) for WebGL's right-handed convention, or
// (0, 0, 1, 0) for a left-handed one), or a view-projection product (the last
// row is plus or minus the view's third row, a unit vector). The
// zero-parallax distance is aNearFactor times the camera's near distance.
// Lights' matrices (shadow lookups) are left alone by name.
inline Maybe<StereoCamera> ClassifyStereoCamera(const float* aM,
                                                std::string_view aName,
                                                float aNearFactor) {
  if (!StereoCameraFinite(aM)) {
    return Nothing();
  }
  for (const char* skip : {"light", "Light", "shadow", "Shadow"}) {
    if (aName.find(skip) != std::string_view::npos) {
      return Nothing();
    }
  }
  float near = 0.0f;
  if (aM[3] == 0.0f && aM[7] == 0.0f &&
      std::fabs(std::fabs(aM[11]) - 1.0f) < 1e-4f && aM[15] == 0.0f &&
      aM[0] > 0.0f && aM[5] > 0.0f) {
    // z_clip = m10 z + m14 and w = -m11 z: the near plane is where
    // z_clip = -w.
    near = aM[14] / (-aM[11] * aM[10] - 1.0f);
  } else {
    const float norm2 = aM[3] * aM[3] + aM[7] * aM[7] + aM[11] * aM[11];
    if (std::fabs(norm2 - 1.0f) > 2e-3f) {
      return Nothing();
    }
    // Rows 2 and 3 of the product are P22 V2 + P23 V3 and -/+ V2: the same
    // near distance either way.
    const float a = aM[2] * aM[3] + aM[6] * aM[7] + aM[10] * aM[11];
    near = (aM[14] - a * aM[15]) / (-1.0f - a);
  }
  if (!(near > 0.0f) || !std::isfinite(near)) {
    return Nothing();
  }
  StereoCamera camera;
  for (int i = 0; i < 16; i++) {
    camera.mono[i] = aM[i];
  }
  camera.convergence = near * aNearFactor;
  return Some(camera);
}

// One eye's camera: aShift is -s for the left eye, +s for the right eye.
inline std::array<float, 16> StereoCameraEye(const StereoCamera& aCamera,
                                             float aShift) {
  std::array<float, 16> m = aCamera.mono;
  for (int column = 0; column < 4; column++) {
    m[4 * column] += aShift * aCamera.mono[4 * column + 3];
  }
  m[12] -= aShift * aCamera.convergence;
  return m;
}

}  // namespace mozilla::webgl

#endif  // WEBGL_STEREO_CAMERA_H_
