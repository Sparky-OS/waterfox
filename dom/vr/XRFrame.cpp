/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/XRFrame.h"

#include "VRDisplayClient.h"
#include "mozilla/dom/XRReferenceSpace.h"
#include "mozilla/dom/XRRenderState.h"
#include "mozilla/dom/XRRigidTransform.h"
#include "mozilla/dom/XRView.h"
#include "mozilla/dom/XRViewerPose.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(XRFrame, mParent, mSession)

XRFrame::XRFrame(nsISupports* aParent, XRSession* aXRSession)
    : mParent(aParent),
      mSession(aXRSession),
      mActive(false),
      mAnimationFrame(false) {}

JSObject* XRFrame::WrapObject(JSContext* aCx,
                              JS::Handle<JSObject*> aGivenProto) {
  return XRFrame_Binding::Wrap(aCx, this, aGivenProto);
}

XRSession* XRFrame::Session() { return mSession; }

already_AddRefed<XRViewerPose> XRFrame::GetViewerPose(
    const XRReferenceSpace& aReferenceSpace, ErrorResult& aRv) {
  if (!mActive || !mAnimationFrame) {
    aRv.ThrowInvalidStateError(
        "GetViewerPose can only be called on an XRFrame during an "
        "XRSession.requestAnimationFrame callback.");
    return nullptr;
  }

  if (aReferenceSpace.GetSession() != mSession) {
    aRv.ThrowInvalidStateError(
        "The XRReferenceSpace passed to GetViewerPose must belong to the "
        "XRSession that GetViewerPose is called on.");
    return nullptr;
  }

  if (!mSession->CanReportPoses()) {
    aRv.ThrowSecurityError(
        "The visibilityState of the XRSpace's XRSession "
        "that is passed to GetViewerPose must be 'visible'.");
    return nullptr;
  }

  // TODO (Bug 1616393) - Check if poses must be limited:
  // https://immersive-web.github.io/webxr/#poses-must-be-limited

  bool emulatedPosition = aReferenceSpace.IsPositionEmulated();

  XRRenderState* renderState = mSession->GetActiveRenderState();
  float depthNear = (float)renderState->DepthNear();
  float depthFar = (float)renderState->DepthFar();

  RefPtr<XRViewerPose> viewerPose;

  gfx::VRDisplayClient* display = mSession->GetDisplayClient();
  if (display) {
    // Have a VRDisplayClient
    const gfx::VRDisplayInfo& displayInfo =
        mSession->GetDisplayClient()->GetDisplayInfo();
    const gfx::VRHMDSensorState& sensorState = display->GetSensorState();

    gfx::PointDouble3D viewerPosition = gfx::PointDouble3D(
        sensorState.pose.position[0], sensorState.pose.position[1],
        sensorState.pose.position[2]);
    gfx::QuaternionDouble viewerOrientation = gfx::QuaternionDouble(
        sensorState.pose.orientation[0], sensorState.pose.orientation[1],
        sensorState.pose.orientation[2], sensorState.pose.orientation[3]);

    // The flat stereo display: the viewer sits in front of the screen, turned
    // and moved by the mouse and keys, and each eye looks through the screen.
    const bool flat = mSession->IsFlat();
    XRSession::WindowViews windowViews;
    if (flat) {
      viewerPosition = mSession->FlatPosition();
      viewerOrientation = mSession->FlatOrientation();
      windowViews = mSession->GetWindowViews();
    }

    gfx::Matrix4x4Double headTransform;
    headTransform.SetRotationFromQuaternion(viewerOrientation);
    headTransform.PostTranslate(viewerPosition);

    gfx::Matrix4x4Double originTransform;
    originTransform.SetRotationFromQuaternion(
        aReferenceSpace.GetEffectiveOriginOrientation().Inverse());
    originTransform.PreTranslate(-aReferenceSpace.GetEffectiveOriginPosition());

    headTransform *= originTransform;

    viewerPose = mSession->PooledViewerPose(headTransform, emulatedPosition);

    auto updateEye = [&](int32_t viewIndex, gfx::VRDisplayState::Eye eye) {
      auto offset = flat ? windowViews.mOffset[eye]
                         : displayInfo.GetEyeTranslation(eye);
      auto eyeFromHead = gfx::Matrix4x4Double::Translation(
          gfx::PointDouble3D(offset.x, offset.y, offset.z));
      auto eyeTransform = eyeFromHead * headTransform;
      gfx::PointDouble3D eyePosition;
      gfx::QuaternionDouble eyeRotation;
      gfx::PointDouble3D eyeScale;
      eyeTransform.Decompose(eyePosition, eyeRotation, eyeScale);

      const gfx::VRFieldOfView fov = flat
                                         ? windowViews.mFieldOfView[eye]
                                         : displayInfo.mDisplayState.eyeFOV[eye];
      gfx::Matrix4x4 projection =
          fov.ConstructProjectionMatrix(depthNear, depthFar, true);
      viewerPose->GetEye(viewIndex)->Update(eyePosition, eyeRotation,
                                            projection);
    };

    updateEye(0, gfx::VRDisplayState::Eye_Left);
    updateEye(1, gfx::VRDisplayState::Eye_Right);
  } else if (mSession->IsInlineStereo()) {
    // inline-stereo (WebXR section 3.4): a left and a right view, through the
    // output canvas as a window from the viewer's seat (section 4.2: "MUST
    // compute per-view projection matrices using the output canvas
    // geometry"). The views share the canvas, left half and right half
    // (XRWebGLLayer::GetViewport), and the canvas is declared to hold both.
    const XRSession::WindowViews windowViews = mSession->GetWindowViews();
    viewerPose =
        mSession->PooledViewerPose(gfx::Matrix4x4Double(), emulatedPosition);
    for (int32_t eye = 0; eye < gfx::VRDisplayState::NumEyes; eye++) {
      const gfx::Point3D& offset = windowViews.mOffset[eye];
      viewerPose->GetEye(eye)->Update(
          gfx::PointDouble3D(offset.x, offset.y, offset.z),
          gfx::QuaternionDouble(),
          windowViews.mFieldOfView[eye].ConstructProjectionMatrix(
              depthNear, depthFar, true));
    }
  } else {
    auto inlineVerticalFov = renderState->GetInlineVerticalFieldOfView();
    const double fov =
        inlineVerticalFov.IsNull() ? M_PI * 0.5f : inlineVerticalFov.Value();
    HTMLCanvasElement* canvas = renderState->GetOutputCanvas();
    float aspect = 1.0f;
    if (canvas) {
      aspect = (float)canvas->Width() / (float)canvas->Height();
    }
    gfx::Matrix4x4 projection =
        ConstructInlineProjection((float)fov, aspect, depthNear, depthFar);

    viewerPose =
        mSession->PooledViewerPose(gfx::Matrix4x4Double(), emulatedPosition);
    viewerPose->GetEye(0)->Update(gfx::PointDouble3D(), gfx::QuaternionDouble(),
                                  projection);
  }

  return viewerPose.forget();
}

already_AddRefed<XRPose> XRFrame::GetPose(const XRSpace& aSpace,
                                          const XRSpace& aBaseSpace,
                                          ErrorResult& aRv) {
  if (!mActive) {
    aRv.ThrowInvalidStateError(
        "GetPose can not be called on an XRFrame that is not active.");
    return nullptr;
  }

  if (aSpace.GetSession() != mSession || aBaseSpace.GetSession() != mSession) {
    aRv.ThrowInvalidStateError(
        "The XRSpace passed to GetPose must belong to the "
        "XRSession that GetPose is called on.");
    return nullptr;
  }

  if (!mSession->CanReportPoses()) {
    aRv.ThrowSecurityError(
        "The visibilityState of the XRSpace's XRSession "
        "that is passed to GetPose must be 'visible'.");
    return nullptr;
  }

  // TODO (Bug 1616393) - Check if poses must be limited:
  // https://immersive-web.github.io/webxr/#poses-must-be-limited

  const bool emulatedPosition = aSpace.IsPositionEmulated();
  gfx::Matrix4x4Double base;
  base.SetRotationFromQuaternion(
      aBaseSpace.GetEffectiveOriginOrientation().Inverse());
  base.PreTranslate(-aBaseSpace.GetEffectiveOriginPosition());

  gfx::Matrix4x4Double matrix = aSpace.GetEffectiveOriginTransform() * base;

  RefPtr<XRRigidTransform> transform = new XRRigidTransform(mParent, matrix);
  RefPtr<XRPose> pose = new XRPose(mParent, transform, emulatedPosition);

  return pose.forget();
}

void XRFrame::StartAnimationFrame() {
  mActive = true;
  mAnimationFrame = true;
}

void XRFrame::EndAnimationFrame() { mActive = false; }

void XRFrame::StartInputSourceEvent() { mActive = true; }

void XRFrame::EndInputSourceEvent() { mActive = false; }

// The inline view's projection (WebXR section 4.2: from the inline vertical
// field of view and the output canvas's aspect), built as the immersive
// views' are (VRFieldOfView::ConstructProjectionMatrix, right handed, -z
// forward), so a point in front of the viewer is in front of the camera.
gfx::Matrix4x4 XRFrame::ConstructInlineProjection(float aFov, float aAspect,
                                                  float aNear, float aFar) {
  if (aFov == 0) {
    aFov = 0.5f * M_PI;
  }
  const double vertical = 0.5 * aFov / kRadPerDegree;
  const double horizontal =
      atan(tan(0.5 * aFov) * aAspect) / kRadPerDegree;
  return gfx::VRFieldOfView(vertical, horizontal, vertical, horizontal)
      .ConstructProjectionMatrix(aNear, aFar, true);
}

}  // namespace mozilla::dom
