/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export class StereoNoticeParent extends JSWindowActorParent {
  receiveMessage(message) {
    if (message.name != "StereoNotice:NoLimits") {
      return;
    }
    const browser = this.browsingContext.top.embedderElement;
    const win = browser?.ownerGlobal;
    if (!win?.gBrowser) {
      return;
    }
    const box = win.gBrowser.getNotificationBox(browser);
    if (box.getNotificationWithValue("stereo-canvas-no-limits")) {
      return;
    }
    box.appendNotification(
      "stereo-canvas-no-limits",
      {
        label: { "l10n-id": "stereo-canvas-no-limits" },
        priority: box.PRIORITY_INFO_MEDIUM,
      },
      []
    );
  }
}
