/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_IMAGETYPES_H
#define GFX_IMAGETYPES_H

#include <stdint.h>  // for uint32_t
#include "mozilla/Maybe.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Variant.h"

namespace mozilla {

enum class ImageFormat {
  /**
   * The PLANAR_YCBCR format creates a PlanarYCbCrImage. All backends should
   * support this format, because the Ogg video decoder depends on it.
   * The maximum image width and height is 16384.
   */
  PLANAR_YCBCR,

  /**
   * The NV_IMAGE format creates a NVImage. The PLANAR_YCBCR together with this
   * complete the YUV format family.
   */
  NV_IMAGE,

  /**
   * The SHARED_RGB format creates a SharedRGBImage, which stores RGB data in
   * shared memory. Some Android hardware video decoders require this format.
   * Currently only used on Android.
   */
  SHARED_RGB,

  /**
   * The MOZ2D_SURFACE format creates a SourceSurfaceImage. All backends should
   * support this format, because video rendering sometimes requires it.
   *
   * This format is useful even though a PaintedLayer could be used.
   * It makes it easy to render a cairo surface when another Image format
   * could be used. It can also avoid copying the surface data in some
   * cases.
   */
  MOZ2D_SURFACE,

  /**
   * A MacIOSurface object.
   */
  MAC_IOSURFACE,

  /**
   * An Android SurfaceTexture ID that can be shared across threads and
   * processes.
   */
  SURFACE_TEXTURE,

  /**
   * The D3D9_RGB32_TEXTURE format creates a D3D9SurfaceImage, and wraps a
   * IDirect3DTexture9 in RGB32 layout.
   */
  D3D9_RGB32_TEXTURE,

  /**
   * An Image type carries an opaque handle once for each stream.
   * The opaque handle would be a platform specific identifier.
   */
  OVERLAY_IMAGE,

  /**
   * A share handle to a ID3D11Texture2D.
   */
  D3D11_SHARE_HANDLE_TEXTURE,

  /**
   * A wrapper of ID3D11Texture2D for zero copy hw video decoding.
   * Expected to be used in GPU process.
   */
  D3D11_TEXTURE_ZERO_COPY,

  /**
   * A wrapper around a drawable TextureClient.
   */
  TEXTURE_WRAPPER,

  /**
   * An opaque handle that refers to an Image stored in the GPU
   * process.
   */
  GPU_VIDEO,

  /**
   * The DMABUF format creates a SharedDMABUFImage, which stores YUV
   * data in DMABUF memory. Used by VAAPI decoder on Linux.
   */
  DMABUF,

  /**
   * A Wrapper of Dcomp surface handle, used by the windows media foundation
   * media engine playback.
   */
  DCOMP_SURFACE,
};

// How a picture holds the two views of a stereoscopic picture: every packing
// of the Matroska StereoMode (RFC 9559 section 5.1.4.1.28.3, Table 5) and of
// the H.264 and H.265 frame packing arrangement (Table D-8), with its eye
// order.
enum class StereoMode {
  MONO,
  LEFT_RIGHT,
  RIGHT_LEFT,
  BOTTOM_TOP,
  TOP_BOTTOM,
  CHECKERBOARD_LEFT_FIRST,
  CHECKERBOARD_RIGHT_FIRST,
  ROWS_LEFT_FIRST,
  ROWS_RIGHT_FIRST,
  COLUMNS_LEFT_FIRST,
  COLUMNS_RIGHT_FIRST,
  ANAGLYPH_CYAN_RED,
  ANAGLYPH_GREEN_MAGENTA,
  // Both eyes laced in one block (Matroska) or in alternate frames (Table
  // D-8, frame alternation).
  LACED_LEFT_FIRST,
  LACED_RIGHT_FIRST,
  MAX,
};

// Where a picture's StereoMode comes from, which also tells whether the size
// of one view is known.
enum class StereoSource : uint8_t {
  // No mark: the picture is taken for one view unless the user or, last, its
  // shape says otherwise.
  NONE,
  // The container's mark, with the size of one view (Matroska's StereoMode
  // with its DisplayWidth and DisplayHeight, RFC 9559 section 18.10).
  CONTAINER_SIZED,
  // The container's mark without the size of a view (the MP4 st3d box, or
  // Matroska's StereoMode without DisplayWidth and DisplayHeight).
  CONTAINER,
  // The stream's own mark, the frame packing SEI (H.264 D.2.26, H.265
  // D.3.16), without the size of a view.
  STREAM,
  // The user's choice.
  USER,
  // A WebXR session's frames: an immersive session on the flat stereo
  // display, or an inline-stereo session's canvas.
  XR,
  MAX,
};

// A picture's stereo mark as WebRender carries it with an image to the
// compositor surface it draws the image into (ImageDescriptorFlags::STEREO):
// bit 15 says there is a mark, bits 0 to 4 hold the StereoMode, bits 5 to 7
// the StereoSource and bit 8 whether the picture is video. 0 means none.
inline uint32_t EncodeStereoMark(StereoMode aMode, StereoSource aSource,
                                 bool aIsVideo) {
  return (1u << 15) | uint32_t(aMode) | (uint32_t(aSource) << 5) |
         (aIsVideo ? 1u << 8 : 0u);
}

inline bool DecodeStereoMark(uint32_t aMark, StereoMode* aMode,
                             StereoSource* aSource, bool* aIsVideo) {
  const uint32_t mode = aMark & 0x1f;
  const uint32_t source = (aMark >> 5) & 0x7;
  if (!(aMark & (1u << 15)) || mode >= uint32_t(StereoMode::MAX) ||
      source >= uint32_t(StereoSource::MAX)) {
    return false;
  }
  *aMode = StereoMode(mode);
  *aSource = StereoSource(source);
  *aIsVideo = !!(aMark & (1u << 8));
  return true;
}

namespace layers {

using ContainerFrameID = uint32_t;
constexpr ContainerFrameID kContainerFrameID_Invalid = 0;

using ContainerProducerID = uint32_t;
constexpr ContainerProducerID kContainerProducerID_Invalid = 0;

// int64_t represents a WebRTC NTP timestamp.
using ContainerCaptureTime = Variant<Nothing, TimeStamp, int64_t>;

// int64_t represents a WebRTC Realtime timestamp.
using ContainerReceiveTime = Maybe<int64_t>;

using ContainerRtpTimestamp = Maybe<uint32_t>;

}  // namespace layers

}  // namespace mozilla

#endif
