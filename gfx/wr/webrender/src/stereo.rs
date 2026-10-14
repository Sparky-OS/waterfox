/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Stereo pictures drawn into picture cache tiles (Sparky Stereo OS).
//!
//! An image whose descriptor carries a stereo mark (ImageDescriptorFlags::STEREO)
//! holds the two views of a stereo picture. Promoted to a compositor surface of
//! its own, the compositor decides how its views are shown. Drawn into the
//! page's tiles instead (a rotation, an opacity, a clip that the compositor
//! cannot take), each tile that shows it is drawn twice, once per eye, into a
//! surface twice as wide: the left eye's drawing on the left, the right eye's on
//! the right, every other primitive the same in both. The compositor is told
//! the tile holds two views side by side, left eye first.
//!
//! The decision of how a picture holds its views is the embedder's (Gecko asks
//! libstereo-policy, as every program of the edition does): it is registered
//! with set_stereo_policy and asked with the picture's mark and its shape.

use api::units::{LayoutRect, LayoutSize};
use crate::spatial_tree::{SpatialNodeIndex, SpatialTree};
use std::sync::atomic::{AtomicUsize, Ordering};

/// The embedder's decision for a stereo mark and a place of `width` by `height`:
/// 0 for one picture; otherwise bits 0-1 the layout (1 side by side, 2 top and
/// bottom), bit 2 the right eye first, bits 3-4 the fit (1: a full view shown
/// at its own shape in the middle of the place's width, 2: of its height),
/// bit 5 views the embedder drew itself (WebXR, its camera hack), which hold
/// one scene: drawn once, they show the left eye's view, never both.
pub type StereoPolicyFn = extern "C" fn(mark: u32, width: f32, height: f32) -> u32;

static STEREO_POLICY: AtomicUsize = AtomicUsize::new(0);

/// Registers the embedder's stereo policy; without one, every image is drawn
/// as one picture.
pub fn set_stereo_policy(policy: StereoPolicyFn) {
    STEREO_POLICY.store(policy as usize, Ordering::Release);
}

/// The shape of a primitive's place as the page shows it: its local size
/// scaled by its spatial node's transform. A video's or a canvas's image is
/// laid out at the picture's own size and scaled to the element's place by
/// a reference frame; the decision is about the place.
pub fn place_shape(
    local: LayoutRect,
    spatial_tree: &SpatialTree,
    node: SpatialNodeIndex,
) -> LayoutRect {
    let (sx, sy) = spatial_tree
        .get_relative_transform(node, spatial_tree.root_reference_frame_index())
        .scale_factors();
    LayoutRect::from_size(LayoutSize::new(local.width() * sx, local.height() * sy))
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub struct StereoDecision {
    pub top_and_bottom: bool,
    pub right_first: bool,
    /// 0: the views fill the place; 1 and 2: one view at its own shape in
    /// the middle of the place's width (1) or height (2).
    pub fit: u8,
    /// Views the embedder drew itself, of one scene.
    pub embedder_views: bool,
}

impl StereoDecision {
    /// The decision for an image's stereo mark shown over a place of the
    /// given size (place_shape), if it holds two views.
    pub fn decide(mark: u32, place: LayoutRect) -> Option<StereoDecision> {
        if mark == 0 {
            return None;
        }
        let policy = STEREO_POLICY.load(Ordering::Acquire);
        if policy == 0 {
            return None;
        }
        let policy: StereoPolicyFn = unsafe { std::mem::transmute(policy) };
        let answer = policy(mark, place.width(), place.height());
        let layout = answer & 0x3;
        if layout != 1 && layout != 2 {
            return None;
        }
        Some(StereoDecision {
            top_and_bottom: layout == 2,
            right_first: answer & 0x4 != 0,
            fit: ((answer >> 3) & 0x3) as u8,
            embedder_views: answer & 0x20 != 0,
        })
    }

    /// The eye whose view a surface drawn once shows: the left eye's, for
    /// views the embedder drew of one scene (the pair squeezed into the
    /// place would be no picture of it); none for other pictures, which show
    /// as they are (a display in a 3D mode of its own may take them so).
    pub fn flat_eye(&self) -> Option<usize> {
        if self.embedder_views {
            Some(0)
        } else {
            None
        }
    }

    /// The part of the picture an eye (0 left, 1 right) sees, as fractions
    /// of the picture: x0, y0, x1, y1.
    pub fn eye_fraction(&self, eye: usize) -> [f32; 4] {
        let second = (eye == 1) != self.right_first;
        let half = if second { 0.5 } else { 0.0 };
        if self.top_and_bottom {
            [0.0, half, 1.0, half + 0.5]
        } else {
            [half, 0.0, half + 0.5, 1.0]
        }
    }

    /// Where an eye's view is drawn in the place.
    pub fn eye_rect(&self, place: LayoutRect) -> LayoutRect {
        let mut rect = place;
        match self.fit {
            1 => {
                let width = place.width() / 2.0;
                rect.min.x += width / 2.0;
                rect.max.x = rect.min.x + width;
            }
            2 => {
                let height = place.height() / 2.0;
                rect.min.y += height / 2.0;
                rect.max.y = rect.min.y + height;
            }
            _ => {}
        }
        rect
    }
}
