/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Sparky Stereo OS: a 3D scene (the camera hack) that stays flat because the
// desktop does not tell the browser the person's depth limits is said to the
// person in the browser's own notification bar. The browser itself tells the
// document (the chrome-only event MozStereoCanvasNoLimits, once for the
// canvas; a page cannot send it); the parent actor shows the bar.

export class StereoNoticeChild extends JSWindowActorChild {
  handleEvent(event) {
    if (event.type == "MozStereoCanvasNoLimits") {
      this.sendAsyncMessage("StereoNotice:NoLimits");
    }
  }
}
