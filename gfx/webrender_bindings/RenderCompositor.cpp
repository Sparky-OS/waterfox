/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositor.h"

#include <cmath>
#include <iterator>

#include "gfxConfig.h"
#include "gfxPlatform.h"
#include "GLContext.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/webrender/RenderCompositorLayerNative.h"
#include "mozilla/webrender/RenderCompositorLayersSWGL.h"
#include "mozilla/webrender/RenderCompositorOGL.h"
#include "mozilla/webrender/RenderCompositorSWGL.h"
#include "mozilla/widget/CompositorWidget.h"

#ifdef XP_WIN
#  include "mozilla/webrender/RenderCompositorANGLE.h"
#  include "mozilla/widget/WinCompositorWidget.h"
#endif

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GTK)
#  include "mozilla/webrender/RenderCompositorEGL.h"
#endif

#ifdef MOZ_WAYLAND
#  include "mozilla/StaticMutex.h"
#  include "mozilla/TimeStamp.h"
#  include "mozilla/layers/StereoPolicy.h"
#  include "mozilla/webrender/RenderCompositorNative.h"
#endif

#ifdef XP_DARWIN
#  include "mozilla/webrender/RenderCompositorNative.h"
#endif

namespace mozilla::wr {

void wr_compositor_add_surface(void* aCompositor, wr::NativeSurfaceId aId,
                               const wr::CompositorSurfaceTransform* aTransform,
                               wr::DeviceIntRect aClipRect,
                               wr::ImageRendering aImageRendering,
                               wr::DeviceIntRect aRoundedClipRect,
                               wr::ClipRadius aRoundedClipRadius) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->AddSurface(aId, *aTransform, aClipRect, aImageRendering,
                         aRoundedClipRect, aRoundedClipRadius);
}

void wr_compositor_begin_frame(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CompositorBeginFrame();
}

void wr_compositor_bind(void* aCompositor, wr::NativeTileId aId,
                        wr::DeviceIntPoint* aOffset, uint32_t* aFboId,
                        wr::DeviceIntRect aDirtyRect,
                        wr::DeviceIntRect aValidRect) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->Bind(aId, aOffset, aFboId, aDirtyRect, aValidRect);
}

void wr_compositor_create_surface(void* aCompositor, wr::NativeSurfaceId aId,
                                  wr::DeviceIntPoint aVirtualOffset,
                                  wr::DeviceIntSize aTileSize, bool aIsOpaque) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateSurface(aId, aVirtualOffset, aTileSize, aIsOpaque);
}

void wr_compositor_create_external_surface(void* aCompositor,
                                           wr::NativeSurfaceId aId,
                                           bool aIsOpaque) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateExternalSurface(aId, aIsOpaque);
}

void wr_compositor_create_swapchain_surface(void* aCompositor,
                                            wr::NativeSurfaceId aId,
                                            wr::DeviceIntSize aSize,
                                            bool aIsOpaque,
                                            bool aNeedsSyncDcompCommit) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateSwapChainSurface(aId, aSize, aIsOpaque,
                                     aNeedsSyncDcompCommit);
}

void wr_compositor_resize_swapchain(void* aCompositor, wr::NativeSurfaceId aId,
                                    wr::DeviceIntSize aSize) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->ResizeSwapChainSurface(aId, aSize);
}

void wr_compositor_create_backdrop_surface(void* aCompositor,
                                           wr::NativeSurfaceId aId,
                                           wr::ColorF aColor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateBackdropSurface(aId, aColor);
}

void wr_compositor_create_tile(void* aCompositor, wr::NativeSurfaceId aId,
                               int32_t aX, int32_t aY) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateTile(aId, aX, aY);
}

void wr_compositor_destroy_tile(void* aCompositor, wr::NativeSurfaceId aId,
                                int32_t aX, int32_t aY) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->DestroyTile(aId, aX, aY);
}

void wr_compositor_bind_swapchain(void* aCompositor, wr::NativeSurfaceId aId,
                                  const wr::DeviceIntRect* aDirtyRects,
                                  size_t aNumDirtyRects) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->BindSwapChain(aId, aDirtyRects, aNumDirtyRects);
}

void wr_compositor_present_swapchain(void* aCompositor, wr::NativeSurfaceId aId,
                                     const wr::DeviceIntRect* aDirtyRects,
                                     size_t aNumDirtyRects) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->PresentSwapChain(aId, aDirtyRects, aNumDirtyRects);
}

void wr_compositor_destroy_surface(void* aCompositor, NativeSurfaceId aId) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->DestroySurface(aId);
}

void wr_compositor_attach_external_image(void* aCompositor,
                                         wr::NativeSurfaceId aId,
                                         wr::ExternalImageId aExternalImage) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->AttachExternalImage(aId, aExternalImage);
}

void wr_compositor_set_surface_stereo(void* aCompositor,
                                      wr::NativeSurfaceId aId,
                                      uint32_t aStereo) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->SetSurfaceStereo(aId, aStereo);
}

void wr_compositor_set_tile_stereo(void* aCompositor, wr::NativeTileId aId,
                                   bool aStereo) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->SetTileStereo(aId, aStereo);
}

#ifdef MOZ_WAYLAND
// The last answers, for a second: WebRender asks for every stereo picture in
// every frame it builds.
struct StereoPolicyAnswer {
  uint32_t mMark = 0;
  gfx::IntSize mShape;
  uint32_t mAnswer = 0;
  TimeStamp mTime;
};
static StaticMutex sStereoPolicyMutex MOZ_UNANNOTATED;
static StereoPolicyAnswer sStereoPolicyAnswers[8];
static size_t sStereoPolicyNext = 0;
#endif

// How a stereo picture WebRender draws into the page's tiles holds its views
// (webrender::set_stereo_policy): the decision the compositor's layers take
// for a picture given a surface of its own (layers::DecideStereo), for the
// picture's mark and the shape of its place. The answer is 0 for one
// picture; otherwise bits 0 and 1 hold the layout (1 side by side, 2 top and
// bottom), bit 2 the right eye first, bits 3 and 4 the fit (1 a full view in
// the middle of the place's width, 2 of its height), bit 5 views the browser
// drew itself (WebXR, the camera hack), which show the left eye's view where
// they are drawn once.
uint32_t wr_stereo_policy(uint32_t aMark, float aWidth, float aHeight) {
#ifdef MOZ_WAYLAND
  mozilla::StereoMode mode = mozilla::StereoMode::MONO;
  mozilla::StereoSource source = mozilla::StereoSource::NONE;
  bool isVideo = false;
  bool swap = false;
  if (!mozilla::DecodeStereoMark(aMark, &mode, &source, &isVideo, &swap)) {
    return 0;
  }
  const gfx::IntSize shape(int(std::lround(aWidth)), int(std::lround(aHeight)));
  const TimeStamp now = TimeStamp::Now();
  StaticMutexAutoLock lock(sStereoPolicyMutex);
  for (const auto& answer : sStereoPolicyAnswers) {
    if (!answer.mTime.IsNull() && answer.mMark == aMark &&
        answer.mShape == shape &&
        (now - answer.mTime) < TimeDuration::FromSeconds(1)) {
      return answer.mAnswer;
    }
  }
  const layers::StereoDecision decision =
      layers::DecideStereo(mode, source, swap, isVideo, shape);
  uint32_t result = 0;
  if (decision.mPacking != mozilla::StereoMode::MONO) {
    result = layers::IsTopAndBottom(decision.mPacking) ? 2 : 1;
    if (decision.mPacking == mozilla::StereoMode::RIGHT_LEFT ||
        decision.mPacking == mozilla::StereoMode::BOTTOM_TOP) {
      result |= 1 << 2;
    }
    if (decision.mFit == layers::StereoFit::HalfWidth) {
      result |= 1 << 3;
    } else if (decision.mFit == layers::StereoFit::HalfHeight) {
      result |= 2 << 3;
    }
    if (source == mozilla::StereoSource::XR ||
        source == mozilla::StereoSource::CAMERA) {
      result |= 1 << 5;
    }
  }
  sStereoPolicyAnswers[sStereoPolicyNext] =
      StereoPolicyAnswer{aMark, shape, result, now};
  sStereoPolicyNext = (sStereoPolicyNext + 1) % std::size(sStereoPolicyAnswers);
  return result;
#else
  return 0;
#endif
}

void wr_compositor_start_compositing(void* aCompositor, wr::ColorF aClearColor,
                                     const wr::DeviceIntRect* aDirtyRects,
                                     size_t aNumDirtyRects,
                                     const wr::DeviceIntRect* aOpaqueRects,
                                     size_t aNumOpaqueRects) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->StartCompositing(aClearColor, aDirtyRects, aNumDirtyRects,
                               aOpaqueRects, aNumOpaqueRects);
}

void wr_compositor_end_frame(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CompositorEndFrame();
}

void wr_compositor_get_capabilities(void* aCompositor,
                                    CompositorCapabilities* aCaps) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->GetCompositorCapabilities(aCaps);
}

void wr_compositor_get_window_visibility(void* aCompositor,
                                         WindowVisibility* aVisibility) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->GetWindowVisibility(aVisibility);
}

void wr_compositor_get_window_properties(void* aCompositor,
                                         WindowProperties* aProperties) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->GetWindowProperties(aProperties);
}

void wr_compositor_unbind(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->Unbind();
}

void wr_compositor_deinit(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->DeInit();
}

void wr_compositor_map_tile(void* aCompositor, wr::NativeTileId aId,
                            wr::DeviceIntRect aDirtyRect,
                            wr::DeviceIntRect aValidRect, void** aData,
                            int32_t* aStride) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->MapTile(aId, aDirtyRect, aValidRect, aData, aStride);
}

void wr_compositor_unmap_tile(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->UnmapTile();
}

void wr_partial_present_compositor_set_buffer_damage_region(
    void* aCompositor, const wr::DeviceIntRect* aRects, size_t aNRects) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->SetBufferDamageRegion(aRects, aNRects);
}

/* static */
UniquePtr<RenderCompositor> RenderCompositor::Create(
    const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError) {
  if (aWidget->GetCompositorOptions().UseSoftwareWebRender()) {
#ifdef XP_DARWIN
    // Mac uses NativeLayerCA
    if (!gfxPlatform::IsHeadless()) {
      return RenderCompositorNativeSWGL::Create(aWidget, aError);
    }
#elif defined(MOZ_WAYLAND)
    // Some widgets on Wayland (D&D popups for instance) can't use native
    // compositor due to system limitations.
    if (gfx::gfxVars::UseWebRenderCompositor() &&
        aWidget->GetCompositorOptions().AllowNativeCompositor()) {
      return RenderCompositorNativeSWGL::Create(aWidget, aError);
    }
#endif
    UniquePtr<RenderCompositor> comp =
        RenderCompositorLayersSWGL::Create(aWidget, aError);
    if (comp) {
      return comp;
    }
#if defined(MOZ_WIDGET_ANDROID)
    // On Android, we do not want to fallback from RenderCompositorOGLSWGL to
    // RenderCompositorSWGL.
    if (aWidget->GetCompositorOptions().AllowSoftwareWebRenderOGL()) {
      return nullptr;
    }
#endif
    return RenderCompositorSWGL::Create(aWidget, aError);
  }

#ifdef XP_WIN
  if (gfx::gfxVars::UseWebRenderANGLE()) {
    return RenderCompositorANGLE::Create(aWidget, aError);
  }
#endif

#if defined(MOZ_WAYLAND)
  if (gfx::gfxVars::UseWebRenderCompositor() &&
      aWidget->GetCompositorOptions().AllowNativeCompositor()) {
    if (StaticPrefs::gfx_webrender_layer_compositor()) {
      return RenderCompositorLayerNativeOGL::Create(aWidget, aError);
    } else {
      return RenderCompositorNativeOGL::Create(aWidget, aError);
    }
  }
#endif

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GTK)
  UniquePtr<RenderCompositor> eglCompositor =
      RenderCompositorEGL::Create(aWidget, aError);
  if (eglCompositor) {
    return eglCompositor;
  }
#endif

#if defined(MOZ_WIDGET_ANDROID)
  // RenderCompositorOGL is not used on android
  return nullptr;
#elif defined(XP_DARWIN)
  if (gfx::gfxVars::UseWebRenderCompositor() &&
      aWidget->GetCompositorOptions().AllowNativeCompositor()) {
    if (StaticPrefs::gfx_webrender_layer_compositor()) {
      UniquePtr<RenderCompositor> compositor =
          RenderCompositorLayerNativeOGL::Create(aWidget, aError);
      if (compositor) {
        return compositor;
      }
    }
  }
  // Mac uses NativeLayerCA
  return RenderCompositorNativeOGL::Create(aWidget, aError);
#else
  return RenderCompositorOGL::Create(aWidget, aError);
#endif
}

RenderCompositor::RenderCompositor(
    const RefPtr<widget::CompositorWidget>& aWidget)
    : mWidget(aWidget) {}

RenderCompositor::~RenderCompositor() = default;

bool RenderCompositor::MakeCurrent() { return gl()->MakeCurrent(); }

void RenderCompositor::GetCompositorCapabilities(
    CompositorCapabilities* aCaps) {
  if (StaticPrefs::gfx_webrender_compositor_max_update_rects_AtStartup() > 0) {
    aCaps->max_update_rects = 1;
  } else {
    aCaps->max_update_rects = 0;
  }
}

void RenderCompositor::GetWindowVisibility(WindowVisibility* aVisibility) {
#ifdef XP_WIN
  auto* widget = mWidget->AsWindows();
  if (!widget) {
    return;
  }
  aVisibility->is_fully_occluded = widget->GetWindowIsFullyOccluded();
#endif
}

void RenderCompositor::GetWindowProperties(WindowProperties* aProperties) {
  aProperties->is_opaque = true;
}

gfx::DeviceResetReason RenderCompositor::IsContextLost(bool aForce) {
  auto* glc = gl();
  // GetGraphicsResetStatus may trigger an implicit MakeCurrent if robustness
  // is not supported, so unless we are forcing, pass on the check.
  if (!glc || (!aForce && !glc->IsSupported(gl::GLFeature::robustness))) {
    return gfx::DeviceResetReason::OK;
  }
  auto resetStatus = glc->fGetGraphicsResetStatus();
  switch (resetStatus) {
    case LOCAL_GL_NO_ERROR:
      return gfx::DeviceResetReason::OK;
    case LOCAL_GL_INNOCENT_CONTEXT_RESET_ARB:
      return gfx::DeviceResetReason::DRIVER_ERROR;
    case LOCAL_GL_PURGED_CONTEXT_RESET_NV:
      return gfx::DeviceResetReason::NVIDIA_VIDEO;
    case LOCAL_GL_GUILTY_CONTEXT_RESET_ARB:
      return gfx::DeviceResetReason::RESET;
    case LOCAL_GL_UNKNOWN_CONTEXT_RESET_ARB:
      return gfx::DeviceResetReason::UNKNOWN;
    default:
      gfxCriticalError() << "Device reset with WR context unexpected status: "
                         << gfx::hexa(resetStatus);
      break;
  }
  return gfx::DeviceResetReason::OTHER;
}

}  // namespace mozilla::wr
