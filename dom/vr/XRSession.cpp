/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/XRSession.h"

#include "VRDisplayClient.h"
#include "VRDisplayPresentation.h"
#include "VRLayerChild.h"
#include "XRBoundedReferenceSpace.h"
#include "XRFrame.h"
#include "XRInputSourceArray.h"
#include "XRNativeOrigin.h"
#include "XRNativeOriginFixed.h"
#include "XRNativeOriginLocal.h"
#include "XRNativeOriginLocalFloor.h"
#include "XRNativeOriginViewer.h"
#include "XRRenderState.h"
#include "XRSystem.h"
#include "XRView.h"
#include "XRViewerPose.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/XRInputSourceEvent.h"
#include "mozilla/dom/XRSessionEvent.h"
#include <algorithm>
#include <cmath>
#include <tuple>

#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/DOMRect.h"
#include "nsContentUtils.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/KeyboardEvent.h"
#include "mozilla/dom/MouseEvent.h"
#include "mozilla/dom/WheelEvent.h"
#include "mozilla/widget/Screen.h"
#include "mozilla/widget/ScreenManager.h"
#include "nsGlobalWindowInner.h"
#include "nsIDOMEventListener.h"
#include "nsIObserverService.h"
#include "nsISupportsPrimitives.h"
#include "nsRefreshDriver.h"
#include "ClientWebGLContext.h"

/**
 * Maximum instances of XRFrame and XRViewerPose objects
 * created in the pool.
 */
const uint32_t kMaxPoolSize = 16;

namespace mozilla::dom {

namespace {

// The viewer's input on the flat stereo display, which has no controllers.
class FlatInputListener final : public nsIDOMEventListener {
 public:
  NS_DECL_ISUPPORTS

  explicit FlatInputListener(XRSession* aSession) : mSession(aSession) {}

  NS_IMETHOD HandleEvent(Event* aEvent) override {
    if (mSession) {
      mSession->HandleFlatInput(*aEvent);
    }
    return NS_OK;
  }

  void Disconnect() { mSession = nullptr; }

 private:
  ~FlatInputListener() = default;
  XRSession* mSession;
};

NS_IMPL_ISUPPORTS(FlatInputListener, nsIDOMEventListener)

constexpr nsLiteralString kFlatInputEvents[] = {u"mousemove"_ns, u"wheel"_ns,
                                               u"keydown"_ns};

}  // namespace

NS_IMPL_CYCLE_COLLECTION_CLASS(XRSession)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(XRSession,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mXRSystem)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mActiveRenderState)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingRenderState)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInputSources)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mViewerPosePool)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFramePool)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFlatCanvas)

  for (uint32_t i = 0; i < tmp->mFrameRequestCallbacks.Length(); ++i) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mFrameRequestCallbacks[i]");
    cb.NoteXPCOMChild(tmp->mFrameRequestCallbacks[i].mCallback);
  }

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(XRSession, DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mXRSystem)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mActiveRenderState)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingRenderState)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mInputSources)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mViewerPosePool)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFramePool)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFlatCanvas)

  tmp->mFrameRequestCallbacks.Clear();

  // Don't need NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER because
  // DOMEventTargetHelper does it for us.
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(XRSession, DOMEventTargetHelper)

already_AddRefed<XRSession> XRSession::CreateInlineSession(
    nsPIDOMWindowInner* aWindow, XRSystem* aXRSystem,
    const nsTArray<XRReferenceSpaceType>& aEnabledReferenceSpaceTypes) {
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(aWindow);
  if (!win) {
    return nullptr;
  }
  Document* doc = aWindow->GetExtantDoc();
  if (!doc) {
    return nullptr;
  }
  nsPresContext* context = doc->GetPresContext();
  if (!context) {
    return nullptr;
  }
  nsRefreshDriver* driver = context->RefreshDriver();
  if (!driver) {
    return nullptr;
  }

  RefPtr<XRSession> session =
      new XRSession(aWindow, aXRSystem, driver, nullptr, gfx::kVRGroupContent,
                    aEnabledReferenceSpaceTypes);
  driver->AddRefreshObserver(session, FlushType::Display, "XR Session");
  return session.forget();
}

already_AddRefed<XRSession> XRSession::CreateImmersiveSession(
    nsPIDOMWindowInner* aWindow, XRSystem* aXRSystem,
    gfx::VRDisplayClient* aClient, uint32_t aPresentationGroup,
    const nsTArray<XRReferenceSpaceType>& aEnabledReferenceSpaceTypes) {
  RefPtr<XRSession> session =
      new XRSession(aWindow, aXRSystem, nullptr, aClient, aPresentationGroup,
                    aEnabledReferenceSpaceTypes);
  return session.forget();
}

XRSession::XRSession(
    nsPIDOMWindowInner* aWindow, XRSystem* aXRSystem,
    nsRefreshDriver* aRefreshDriver, gfx::VRDisplayClient* aClient,
    uint32_t aPresentationGroup,
    const nsTArray<XRReferenceSpaceType>& aEnabledReferenceSpaceTypes)
    : DOMEventTargetHelper(aWindow),
      mXRSystem(aXRSystem),
      mShutdown(false),
      mEnded(false),
      mRefreshDriver(aRefreshDriver),
      mDisplayClient(aClient),
      mFrameRequestCallbackCounter(0),
      mEnabledReferenceSpaceTypes(aEnabledReferenceSpaceTypes.Clone()),
      mViewerPosePoolIndex(0),
      mFramePoolIndex(0) {
  if (aClient) {
    aClient->SessionStarted(this);
  }
  mActiveRenderState = new XRRenderState(aWindow, this);
  mStartTimeStamp = TimeStamp::Now();
  if (IsImmersive()) {
    mDisplayPresentation =
        mDisplayClient->BeginPresentation({}, aPresentationGroup);
  }
  if (mDisplayClient) {
    mDisplayClient->SetXRAPIMode(gfx::VRAPIMode::WebXR);
  }
  // TODO: Handle XR input sources are no longer available cases.
  // https://immersive-web.github.io/webxr/#dom-xrsession-inputsources
  mInputSources = new XRInputSourceArray(aWindow);
}

XRSession::~XRSession() { MOZ_ASSERT(mShutdown); }

gfx::VRDisplayClient* XRSession::GetDisplayClient() const {
  return mDisplayClient;
}

JSObject* XRSession::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return XRSession_Binding::Wrap(aCx, this, aGivenProto);
}

bool XRSession::IsEnded() const { return mEnded; }

already_AddRefed<Promise> XRSession::End(ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetParentObject();
  NS_ENSURE_TRUE(global, nullptr);

  ExitPresentInternal();

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  NS_ENSURE_TRUE(!aRv.Failed(), nullptr);

  promise->MaybeResolve(JS::UndefinedHandleValue);

  return promise.forget();
}

bool XRSession::IsImmersive() const {
  // Only immersive sessions have a VRDisplayClient
  return mDisplayClient != nullptr;
}

XRVisibilityState XRSession::VisibilityState() const {
  return XRVisibilityState::Visible;
  // TODO (Bug 1609771): Implement changing visibility state
}

// https://immersive-web.github.io/webxr/#poses-may-be-reported
// Given that an XRSession cannot be requested without explicit consent
// by the user, the only necessary check is whether the XRSession's
// visiblityState is 'visible'.
bool XRSession::CanReportPoses() const {
  return VisibilityState() == XRVisibilityState::Visible;
}

// https://immersive-web.github.io/webxr/#dom-xrsession-updaterenderstate
void XRSession::UpdateRenderState(const XRRenderStateInit& aNewState,
                                  ErrorResult& aRv) {
  if (mEnded) {
    aRv.ThrowInvalidStateError(
        "UpdateRenderState can not be called on an XRSession that has ended.");
    return;
  }
  if (aNewState.mBaseLayer.WasPassed() &&
      aNewState.mBaseLayer.Value()->mSession != this) {
    aRv.ThrowInvalidStateError(
        "The baseLayer passed in to UpdateRenderState must "
        "belong to the XRSession that UpdateRenderState is "
        "being called on.");
    return;
  }
  if (aNewState.mInlineVerticalFieldOfView.WasPassed() && IsImmersive()) {
    aRv.ThrowInvalidStateError(
        "The inlineVerticalFieldOfView can not be set on an "
        "XRRenderState for an immersive XRSession.");
    return;
  }
  if (mPendingRenderState == nullptr) {
    mPendingRenderState = new XRRenderState(*mActiveRenderState);
  }
  if (aNewState.mDepthNear.WasPassed()) {
    mPendingRenderState->SetDepthNear(aNewState.mDepthNear.Value());
  }
  if (aNewState.mDepthFar.WasPassed()) {
    mPendingRenderState->SetDepthFar(aNewState.mDepthFar.Value());
  }
  if (aNewState.mInlineVerticalFieldOfView.WasPassed()) {
    mPendingRenderState->SetInlineVerticalFieldOfView(
        aNewState.mInlineVerticalFieldOfView.Value());
  }
  if (aNewState.mBaseLayer.WasPassed()) {
    mPendingRenderState->SetBaseLayer(aNewState.mBaseLayer.Value());
  }
}

XRRenderState* XRSession::RenderState() { return mActiveRenderState; }

XRInputSourceArray* XRSession::InputSources() { return mInputSources; }

Nullable<float> XRSession::GetFrameRate() { return {}; }

void XRSession::GetSupportedFrameRates(JSContext*,
                                       JS::MutableHandle<JSObject*> aRetVal) {
  aRetVal.set(nullptr);
}

// https://immersive-web.github.io/webxr/#apply-the-pending-render-state
void XRSession::ApplyPendingRenderState() {
  if (mPendingRenderState == nullptr) {
    return;
  }
  mActiveRenderState = mPendingRenderState;
  mPendingRenderState = nullptr;

  // https://immersive-web.github.io/webxr/#minimum-inline-field-of-view
  const double kMinimumInlineVerticalFieldOfView = 0.0f;

  // https://immersive-web.github.io/webxr/#maximum-inline-field-of-view
  const double kMaximumInlineVerticalFieldOfView = M_PI;

  if (!mActiveRenderState->GetInlineVerticalFieldOfView().IsNull()) {
    double verticalFOV =
        mActiveRenderState->GetInlineVerticalFieldOfView().Value();
    if (verticalFOV < kMinimumInlineVerticalFieldOfView) {
      verticalFOV = kMinimumInlineVerticalFieldOfView;
    }
    if (verticalFOV > kMaximumInlineVerticalFieldOfView) {
      verticalFOV = kMaximumInlineVerticalFieldOfView;
    }
    mActiveRenderState->SetInlineVerticalFieldOfView(verticalFOV);
  }

  // Our minimum near plane value is set to a small value close but not equal to
  // zero (kEpsilon) The maximum far plane is infinite.
  const float kEpsilon = 0.00001f;
  double depthNear = mActiveRenderState->DepthNear();
  double depthFar = mActiveRenderState->DepthFar();
  if (depthNear < 0.0f) {
    depthNear = 0.0f;
  }
  if (depthFar < 0.0f) {
    depthFar = 0.0f;
  }
  // Ensure at least a small distance between the near and far planes
  if (fabs(depthFar - depthNear) < kEpsilon) {
    depthFar = depthNear + kEpsilon;
  }
  mActiveRenderState->SetDepthNear(depthNear);
  mActiveRenderState->SetDepthFar(depthFar);

  XRWebGLLayer* baseLayer = mActiveRenderState->GetBaseLayer();
  if (baseLayer) {
    if (!IsImmersive() && baseLayer->mCompositionDisabled) {
      mActiveRenderState->SetCompositionDisabled(true);
      mActiveRenderState->SetOutputCanvas(baseLayer->GetCanvas());
      // An inline-stereo session draws both views into the canvas, left and
      // right halves; the canvas is declared to hold them.
      if (mInlineStereo) {
        if (const auto& webgl = baseLayer->GetCanvas()->GetWebGLContext()) {
          webgl->SetInlineStereo(true);
        }
      }
    } else {
      mActiveRenderState->SetCompositionDisabled(false);
      mActiveRenderState->SetOutputCanvas(nullptr);
      mDisplayPresentation->UpdateXRWebGLLayer(baseLayer);
      if (IsFlat()) {
        StartFlatPresentation(baseLayer->GetCanvas());
      }
    }
  }  // if (baseLayer)
}

bool XRSession::IsFlat() const {
  return mDisplayClient && mDisplayClient->GetDisplayInfo()
                                   .mDisplayState.eightCC ==
                               gfx::kVRFlatDisplayEightCC;
}

// An immersive session has exclusive access to the device's display
// (WebXR section 3.3): on the flat stereo display that is the whole screen,
// so the session's canvas, which shows its frames, goes full screen.
void XRSession::StartFlatPresentation(HTMLCanvasElement* aCanvas) {
  if (!aCanvas || mFlatCanvas == aCanvas) {
    return;
  }
  StopFlatPresentation();
  mFlatCanvas = aCanvas;
  mFlatFullscreen = false;
  IgnoredErrorResult rv;
  // A session's canvas need not be in the page (the WebXR samples keep theirs
  // detached). On the flat display it is what the viewer sees, so for the
  // session the browser puts it at the end of the page's root element, full
  // screen, and takes it out again when the session ends.
  if (!aCanvas->IsInComposedDoc()) {
    nsGlobalWindowInner* win = GetOwnerWindow();
    Document* doc = win ? win->GetExtantDoc() : nullptr;
    Element* root = doc ? doc->GetRootElement() : nullptr;
    if (root && aCanvas->OwnerDoc() == doc) {
      root->AppendChild(*aCanvas, rv);
      mFlatCanvasInserted = !rv.Failed();
    }
  }
  RefPtr<Promise> promise =
      aCanvas->RequestFullscreen(FullscreenOptions(), CallerType::System, rv);
  NotifyFlatSession(true);
  if (nsGlobalWindowInner* win = GetOwnerWindow()) {
    mFlatInput = new FlatInputListener(this);
    for (const auto& type : kFlatInputEvents) {
      win->AddEventListener(type, mFlatInput, true);
    }
  }
}

void XRSession::NotifyFlatSession(bool aStarted) {
  // The browser's own UI (the StereoXR actor) shows the hint, the mode and the
  // Exit control while the session lasts; pages do not see this event.
  if (Document* doc = mFlatCanvas ? mFlatCanvas->OwnerDoc() : nullptr) {
    nsContentUtils::DispatchEventOnlyToChrome(
        doc, doc, aStarted ? u"MozStereoXRStart"_ns : u"MozStereoXREnd"_ns,
        CanBubble::eYes, Cancelable::eNo);
  }
}

void XRSession::StopFlatPresentation() {
  if (mFlatCanvas) {
    NotifyFlatSession(false);
  }
  if (mFlatInput) {
    if (nsGlobalWindowInner* win = GetOwnerWindow()) {
      for (const auto& type : kFlatInputEvents) {
        win->RemoveEventListener(type, mFlatInput, true);
      }
    }
    static_cast<FlatInputListener*>(mFlatInput.get())->Disconnect();
    mFlatInput = nullptr;
  }
  if (mFlatCanvas) {
    Document* doc = mFlatCanvas->OwnerDoc();
    if (doc->GetUnretargetedFullscreenElement() == mFlatCanvas) {
      Document::AsyncExitFullscreen(doc);
    }
    if (mFlatCanvasInserted) {
      mFlatCanvas->Remove();
      mFlatCanvasInserted = false;
    }
    mFlatCanvas = nullptr;
  }
}

int32_t XRSession::FlatMode() {
  return std::clamp<int32_t>(StaticPrefs::dom_vr_flat_mode(), 0, 2);
}

void XRSession::AskFlatDepth(int32_t aKind, int32_t aWidthMm, int32_t aHeightMm,
                             uint32_t aStrength, uint64_t aKey) const {
  mFlatDepthAsked = aKey;
  if (!XRE_IsContentProcess()) {
    return;
  }
  ContentChild* child = ContentChild::GetSingleton();
  if (!child) {
    return;
  }
  child->SendGetStereoDisplayDepth(aKind, aWidthMm, aHeightMm,
                                   double(aStrength))
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self = RefPtr{const_cast<XRSession*>(this)}, aKey](
              const std::tuple<int32_t, double, double, int32_t, int32_t>&
                  aAnswer) {
            const auto [ok, sunk, popped, distance, known] = aAnswer;
            if (self->mFlatDepthAsked != aKey) {
              return;
            }
            if (ok != 0) {
              // Said, never silent: the package depends on libstereo-policy1
              // 1.15, so this is a library that was removed or replaced.
              nsContentUtils::LogSimpleConsoleError(
                  u"Stereo: libstereo-policy 1.15 or newer is missing, so the "
                  u"display's depth cannot be read and the flat stereo "
                  u"display shows no views."_ns,
                  "media"_ns, false, true);
              return;
            }
            if (!known && !self->mFlatSizeNoticed) {
              self->mFlatSizeNoticed = true;
              nsContentUtils::LogSimpleConsoleError(
                  u"Stereo: the display's size is not known, so the depth of "
                  u"a 24 inch monitor stands in for it."_ns,
                  "media"_ns, false, true);
            }
            self->mFlatDepth.mReady = true;
            self->mFlatDepth.mKey = aKey;
            self->mFlatDepth.mSunkPx = sunk;
            self->mFlatDepth.mPoppedPx = popped;
            self->mFlatDepth.mViewingDistanceMm = distance;
          },
          [](mozilla::ipc::ResponseRejectReason) {});
}

// The screen plane from the scene the page draws (dom.vr.flat.scene-plane),
// in millimetres: the host looks at the session's frames in their depth
// buffer (WebGLContext::ReadSceneRange) and the plane sits where the nearest
// and the farthest thing have equal and opposite parallax (parallax goes with
// 1 / distance), no nearer than the person's popped limit lets the nearest
// thing come: the display's far parallax is the sunk limit, so an object at
// distance z has a parallax of the sunk limit times (1 - plane / z), and the
// popped limit allows the plane up to nearest * (1 + popped / sunk). A plane
// that moves by more than 15 percent moves half the way. aDefault stands
// until a frame has been looked at.
double XRSession::FlatScenePlaneMm(const double aDefault) const {
  if (!StaticPrefs::dom_vr_flat_scene_plane() || !mActiveRenderState) {
    return aDefault;
  }
  XRWebGLLayer* layer = mActiveRenderState->GetBaseLayer();
  float nearest = 0, farthest = 0;
  if (!layer || !layer->mWebGL ||
      !layer->mWebGL->GetStereoScene(&nearest, &farthest)) {
    return mFlatScenePlaneMm > 0 ? mFlatScenePlaneMm : aDefault;
  }
  double plane = 2000.0 * double(nearest) * double(farthest) /
                 double(nearest + farthest);
  if (mFlatDepth.mReady && mFlatDepth.mSunkPx > 0) {
    plane = std::min(plane, 1000.0 * double(nearest) *
                                (1.0 + mFlatDepth.mPoppedPx /
                                           mFlatDepth.mSunkPx));
  }
  if (mFlatScenePlaneMm <= 0) {
    mFlatScenePlaneMm = plane;
  } else if (std::fabs(plane / mFlatScenePlaneMm - 1.0) > 0.15) {
    mFlatScenePlaneMm += 0.5 * (plane - mFlatScenePlaneMm);
  }
  return mFlatScenePlaneMm;
}

XRSession::WindowViews XRSession::GetWindowViews() const {
  // The window: its size in device pixels and in millimetres.
  RefPtr<widget::Screen> screen =
      widget::ScreenManager::GetSingleton().GetPrimaryScreen();
  double dpi = screen ? screen->GetDPI() : 0.0;
  const bool sizeKnown = dpi > 0.0;
  if (dpi <= 0.0) {
    dpi = 96.0;
  }
  gfx::IntSize pixels;
  HTMLCanvasElement* canvas =
      mActiveRenderState ? mActiveRenderState->GetOutputCanvas() : nullptr;
  if (!IsImmersive() && canvas) {
    const double scale =
        canvas->OwnerDoc()->GetPresContext()
            ? canvas->OwnerDoc()->GetPresContext()->CSSToDevPixelScale().scale
            : 1.0;
    RefPtr<DOMRect> rect = canvas->GetBoundingClientRect();
    pixels = gfx::IntSize(NSToIntRound(float(rect->Width() * scale)),
                          NSToIntRound(float(rect->Height() * scale)));
  } else if (screen) {
    pixels = screen->GetRect().Size().ToUnknownSize();
  }
  if (pixels.IsEmpty()) {
    pixels = gfx::IntSize(1920, 1080);
  }
  const double width = pixels.width * 25.4 / dpi;
  const double height = pixels.height * 25.4 / dpi;

  WindowViews views;
  views.mEyeResolution = pixels;
  double viewer = StaticPrefs::dom_vr_flat_viewer_distance_mm();
  double separation = StaticPrefs::dom_vr_flat_eye_separation_mm();
  double plane = viewer;
  if (separation <= 0) {
    // The depth of the display, under the person's choice (WebXR is in
    // metres, the display's depth in pixels of a 1920 wide view): the
    // screen's own size (that of the display, not of an inline canvas) and
    // the mode decide what is comfortable, the person's strength scales it.
    const int32_t kind = FlatMode() == 0 ? 0 : 2;
    const uint32_t strength = StaticPrefs::dom_vr_flat_depth_strength();
    const gfx::IntSize screenPixels =
        screen ? screen->GetRect().Size().ToUnknownSize() : pixels;
    const int32_t screenWidthMm =
        sizeKnown ? int32_t(lround(screenPixels.width * 25.4 / dpi)) : 0;
    const int32_t screenHeightMm =
        sizeKnown ? int32_t(lround(screenPixels.height * 25.4 / dpi)) : 0;
    const uint64_t key = (uint64_t(kind) << 60) ^
                         (uint64_t(screenWidthMm & 0xfffff) << 40) ^
                         (uint64_t(screenHeightMm & 0xfffff) << 20) ^
                         uint64_t(strength) ^ 0x1000000000000000ull;
    if (key != mFlatDepthAsked) {
      AskFlatDepth(kind, screenWidthMm, screenHeightMm, strength, key);
    }
    if (!mFlatDepth.mReady) {
      views.mReady = false;
      return views;
    }
    if (viewer <= 0) {
      viewer = mFlatDepth.mViewingDistanceMm;
    }
    // The screen plane: where an object has no parallax, in the page's world.
    // The window is that far away and as large as keeps the view's angle, and
    // the eyes are apart by the display's far parallax (its sunk limit) as a
    // fraction of that window: an object at distance z has a parallax of
    // that fraction of the window times (1 - plane / z), so it sinks
    // towards the far limit and pops in front of the plane.
    const double person = StaticPrefs::dom_vr_flat_screen_plane_mm();
    plane = person > 0 ? person : FlatScenePlaneMm(1000.0);
    const double windowWidth = width * plane / viewer;
    separation = mFlatDepth.mSunkPx / 1920.0 * windowWidth;
    // the window as the eyes at the plane see it
  } else {
    // The expert's own physical separation: the screen at the viewer's
    // distance (700 mm unless set), as before the display's depth.
    if (viewer <= 0) {
      viewer = 700.0;
    }
    plane = viewer;
  }
  const double windowWidth = width * plane / viewer;
  const double windowHeight = height * plane / viewer;

  // Each eye's frustum through the window: the window's edge on the eye's
  // own side is nearer to it by half the separation, the other edge farther.
  // VRFieldOfView takes up, right, down, left, in degrees.
  const double ownSide =
      atan((windowWidth - separation) / 2.0 / plane) / kRadPerDegree;
  const double otherSide =
      atan((windowWidth + separation) / 2.0 / plane) / kRadPerDegree;
  const double vertical = atan(windowHeight / 2.0 / plane) / kRadPerDegree;
  views.mFieldOfView[gfx::VRDisplayState::Eye_Left] =
      gfx::VRFieldOfView(vertical, otherSide, vertical, ownSide);
  views.mFieldOfView[gfx::VRDisplayState::Eye_Right] =
      gfx::VRFieldOfView(vertical, ownSide, vertical, otherSide);
  const float offset = float(separation / 2000.0);
  views.mOffset[gfx::VRDisplayState::Eye_Left] = gfx::Point3D(-offset, 0, 0);
  views.mOffset[gfx::VRDisplayState::Eye_Right] = gfx::Point3D(offset, 0, 0);
  return views;
}

gfx::QuaternionDouble XRSession::FlatOrientation() const {
  // Turn about the vertical axis, then tilt about the viewer's own x axis.
  const gfx::QuaternionDouble yaw(0.0, sin(mFlatYaw / 2.0), 0.0,
                                  cos(mFlatYaw / 2.0));
  const gfx::QuaternionDouble pitch(sin(mFlatPitch / 2.0), 0.0, 0.0,
                                    cos(mFlatPitch / 2.0));
  return yaw * pitch;
}

void XRSession::HandleFlatInput(Event& aEvent) {
  if (!mFlatCanvas) {
    return;
  }
  // A step along the ground in the direction the viewer faces.
  const auto move = [&](double aForward, double aRight) {
    mFlatPosition.x += aRight * cos(mFlatYaw) - aForward * sin(mFlatYaw);
    mFlatPosition.z += -aRight * sin(mFlatYaw) - aForward * cos(mFlatYaw);
  };
  if (aEvent.WidgetEventPtr()->mClass == eWheelEventClass) {
    auto* wheel = static_cast<WheelEvent*>(&aEvent);
    move(-wheel->DeltaY(CallerType::System) * 0.002, 0.0);
  } else if (MouseEvent* mouse = aEvent.AsMouseEvent()) {
    // Dragging with the main button turns the viewer.
    if (mouse->Buttons() & 1) {
      mFlatYaw -= mouse->MovementX() * 0.004;
      mFlatPitch = std::clamp(mFlatPitch - mouse->MovementY() * 0.004, -1.4,
                              1.4);
    }
  } else if (KeyboardEvent* key = aEvent.AsKeyboardEvent()) {
    nsAutoString name;
    key->GetKey(name);
    if (name.EqualsLiteral("Escape")) {
      // Escape always ends the immersive session on the flat display, with
      // or without full screen (WebXR 4.1 "shut down the session":
      // the user agent may end a session at any time, and must when the
      // person asks it to), and the page sees its "end" event.
      aEvent.PreventDefault();
      ExitPresentInternal();
      return;
    }
    if (name.EqualsLiteral("ArrowUp") || name.EqualsLiteral("w")) {
      move(0.1, 0.0);
    } else if (name.EqualsLiteral("ArrowDown") || name.EqualsLiteral("s")) {
      move(-0.1, 0.0);
    } else if (name.EqualsLiteral("ArrowLeft") || name.EqualsLiteral("a")) {
      move(0.0, -0.1);
    } else if (name.EqualsLiteral("ArrowRight") || name.EqualsLiteral("d")) {
      move(0.0, 0.1);
    }
  }
}

void XRSession::WillRefresh(mozilla::TimeStamp aTime) {
  // Inline sessions are driven by nsRefreshDriver directly,
  // unlike immersive sessions, which are driven VRDisplayClient.
  if (!IsImmersive() && !mXRSystem->HasActiveImmersiveSession()) {
    if (nsIGlobalObject* global = GetRelevantGlobal()) {
      if (JSObject* obj = global->GetGlobalJSObject()) {
        js::NotifyAnimationActivity(obj);
      }
    }
    StartFrame();
  }
}

void XRSession::StartFrame() {
  if (mShutdown || mEnded) {
    return;
  }
  ApplyPendingRenderState();

  // Leaving full screen (Escape) leaves the immersive session on the flat
  // display.
  if (mFlatCanvas) {
    const bool fullscreen =
        mFlatCanvas->OwnerDoc()->GetUnretargetedFullscreenElement() ==
        mFlatCanvas;
    if (fullscreen) {
      mFlatFullscreen = true;
    } else if (mFlatFullscreen) {
      ExitPresentInternal();
      return;
    }
  }

  XRWebGLLayer* baseLayer = mActiveRenderState->GetBaseLayer();
  if (!baseLayer) {
    return;
  }
  if (baseLayer->mWebGL) {
    baseLayer->mWebGL->SetXRDepthRange(mActiveRenderState->DepthNear(),
                                       mActiveRenderState->DepthFar());
  }

  if (!IsImmersive() && mActiveRenderState->GetOutputCanvas() == nullptr) {
    return;
  }

  // Determine timestamp for the callbacks
  TimeStamp nowTime = TimeStamp::Now();
  mozilla::TimeDuration duration = nowTime - mStartTimeStamp;
  DOMHighResTimeStamp timeStamp = duration.ToMilliseconds();

  // Create an XRFrame for the callbacks
  RefPtr<XRFrame> frame = PooledFrame();
  frame->StartAnimationFrame();

  baseLayer->StartAnimationFrame();
  nsTArray<XRFrameRequest> callbacks;
  callbacks.AppendElements(mFrameRequestCallbacks);
  mFrameRequestCallbacks.Clear();
  for (auto& callback : callbacks) {
    callback.Call(timeStamp, *frame);
  }

  baseLayer->EndAnimationFrame();
  frame->EndAnimationFrame();
  if (mDisplayPresentation) {
    mDisplayPresentation->SubmitFrame();
  }
}

void XRSession::ExitPresent() { ExitPresentInternal(); }

already_AddRefed<Promise> XRSession::RequestReferenceSpace(
    const XRReferenceSpaceType& aReferenceSpaceType, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetParentObject();
  NS_ENSURE_TRUE(global, nullptr);

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  NS_ENSURE_TRUE(!aRv.Failed(), nullptr);

  if (!mEnabledReferenceSpaceTypes.Contains(aReferenceSpaceType)) {
    promise->MaybeRejectWithNotSupportedError(nsLiteralCString(
        "Requested XRReferenceSpaceType not available for the XRSession."));
    return promise.forget();
  }
  RefPtr<XRReferenceSpace> space;
  RefPtr<XRNativeOrigin> nativeOrigin;
  if (mDisplayClient) {
    switch (aReferenceSpaceType) {
      case XRReferenceSpaceType::Viewer:
        nativeOrigin = new XRNativeOriginViewer(mDisplayClient);
        break;
      case XRReferenceSpaceType::Local:
        nativeOrigin = new XRNativeOriginLocal(mDisplayClient);
        break;
      case XRReferenceSpaceType::Local_floor:
      case XRReferenceSpaceType::Bounded_floor:
        nativeOrigin = new XRNativeOriginLocalFloor(mDisplayClient);
        break;
      default:
        nativeOrigin = new XRNativeOriginFixed(gfx::PointDouble3D());
        break;
    }
  } else {
    // We currently only support XRReferenceSpaceType::Viewer when
    // there is no XR hardware.  In this case, the native origin
    // will always be at {0, 0, 0} which will always be the same
    // as the 'tracked' position of the non-existant pose.
    MOZ_ASSERT(aReferenceSpaceType == XRReferenceSpaceType::Viewer);
    nativeOrigin = new XRNativeOriginFixed(gfx::PointDouble3D());
  }
  if (aReferenceSpaceType == XRReferenceSpaceType::Bounded_floor) {
    space = new XRBoundedReferenceSpace(GetParentObject(), this, nativeOrigin);
  } else {
    space = new XRReferenceSpace(GetParentObject(), this, nativeOrigin,
                                 aReferenceSpaceType);
  }

  promise->MaybeResolve(space);
  return promise.forget();
}

already_AddRefed<Promise> XRSession::UpdateTargetFrameRate(float aRate,
                                                           ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetParentObject();
  NS_ENSURE_TRUE(global, nullptr);

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  NS_ENSURE_TRUE(!aRv.Failed(), nullptr);

  if (mEnded) {
    promise->MaybeRejectWithInvalidStateError(
        "UpdateTargetFrameRate can not be called on an XRSession that has "
        "ended.");
    return promise.forget();
  }

  // https://immersive-web.github.io/webxr/#dom-xrsession-updatetargetframerate
  // TODO: Validate the rate with the frame rates supported from the device.
  // We add a no op for now to avoid JS exceptions related to undefined method.
  // The spec states that user agent MAY use rate to calculate a new display
  // frame rate, so it's fine to let the default frame rate for now.

  promise->MaybeResolve(JS::UndefinedHandleValue);
  return promise.forget();
}

XRRenderState* XRSession::GetActiveRenderState() const {
  return mActiveRenderState;
}

void XRSession::XRFrameRequest::Call(const DOMHighResTimeStamp& aTimeStamp,
                                     XRFrame& aFrame) {
  RefPtr<mozilla::dom::XRFrameRequestCallback> callback = mCallback;
  callback->Call(aTimeStamp, aFrame);
}

int32_t XRSession::RequestAnimationFrame(XRFrameRequestCallback& aCallback,
                                         ErrorResult& aError) {
  if (mShutdown) {
    return 0;
  }

  int32_t handle = ++mFrameRequestCallbackCounter;

  mFrameRequestCallbacks.AppendElement(XRFrameRequest(aCallback, handle));

  return handle;
}

void XRSession::CancelAnimationFrame(int32_t aHandle, ErrorResult& aError) {
  mFrameRequestCallbacks.RemoveElementSorted(aHandle);
}

void XRSession::Shutdown() {
  mShutdown = true;
  ExitPresentInternal();
  mViewerPosePool.Clear();
  mViewerPosePoolIndex = 0;
  mFramePool.Clear();
  mFramePoolIndex = 0;
  mActiveRenderState = nullptr;
  mPendingRenderState = nullptr;
  mFrameRequestCallbacks.Clear();

  // Unregister from nsRefreshObserver
  if (mRefreshDriver) {
    mRefreshDriver->RemoveRefreshObserver(this, FlushType::Display);
    mRefreshDriver = nullptr;
  }
}

void XRSession::ExitPresentInternal() {
  StopFlatPresentation();
  if (mInlineStereo && mActiveRenderState) {
    if (HTMLCanvasElement* canvas = mActiveRenderState->GetOutputCanvas()) {
      if (const auto& webgl = canvas->GetWebGLContext()) {
        webgl->SetInlineStereo(false);
      }
    }
  }
  if (mInputSources) {
    mInputSources->Clear(this);
  }
  if (mDisplayClient) {
    mDisplayClient->SessionEnded(this);
  }

  if (mXRSystem) {
    mXRSystem->SessionEnded(this);
  }

  if (mActiveRenderState) {
    mActiveRenderState->SessionEnded();
  }

  if (mPendingRenderState) {
    mPendingRenderState->SessionEnded();
  }

  mDisplayPresentation = nullptr;
  if (!mEnded) {
    mEnded = true;

    XRSessionEventInit init;
    init.mBubbles = false;
    init.mCancelable = false;
    init.mSession = this;
    RefPtr<XRSessionEvent> event =
        XRSessionEvent::Constructor(this, u"end"_ns, init);

    event->SetTrusted(true);
    this->DispatchEvent(*event);
  }
}

void XRSession::DisconnectFromOwner() {
  MOZ_ASSERT(NS_IsMainThread());
  Shutdown();
  DOMEventTargetHelper::DisconnectFromOwner();
}

void XRSession::LastRelease() {
  // We don't want to wait for the GC to free up the presentation
  // for use in other documents, so we do this in LastRelease().
  Shutdown();
}

RefPtr<XRViewerPose> XRSession::PooledViewerPose(
    const gfx::Matrix4x4Double& aTransform, bool aEmulatedPosition) {
  RefPtr<XRViewerPose> pose;
  if (mViewerPosePool.Length() > mViewerPosePoolIndex) {
    pose = mViewerPosePool.ElementAt(mViewerPosePoolIndex);
    pose->Transform()->Update(aTransform);
    pose->SetEmulatedPosition(aEmulatedPosition);
  } else {
    RefPtr<XRRigidTransform> transform =
        new XRRigidTransform(static_cast<EventTarget*>(this), aTransform);
    nsTArray<RefPtr<XRView>> views;
    if (IsImmersive() || mInlineStereo) {
      views.AppendElement(new XRView(GetParentObject(), XREye::Left));
      views.AppendElement(new XRView(GetParentObject(), XREye::Right));
    } else {
      views.AppendElement(new XRView(GetParentObject(), XREye::None));
    }
    pose = new XRViewerPose(static_cast<EventTarget*>(this), transform,
                            aEmulatedPosition, views);
    mViewerPosePool.AppendElement(pose);
  }

  mViewerPosePoolIndex++;
  if (mViewerPosePoolIndex >= kMaxPoolSize) {
    mViewerPosePoolIndex = 0;
  }

  return pose;
}

RefPtr<XRFrame> XRSession::PooledFrame() {
  RefPtr<XRFrame> frame;
  if (mFramePool.Length() > mFramePoolIndex) {
    frame = mFramePool.ElementAt(mFramePoolIndex);
  } else {
    frame = new XRFrame(GetParentObject(), this);
    mFramePool.AppendElement(frame);
  }

  return frame;
}

}  // namespace mozilla::dom
