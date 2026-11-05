/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Sparky Stereo OS: a video site's mark of its videos, read from the keywords
// of the page it declares them in. YouTube took the layout of a 3D upload
// from its keywords (yt3d:enable=LR and the others), and its watch page, like
// Invidious and Piped, still carries them in <meta name="keywords">. This
// actor only hands the page's keywords to its videos (HTMLVideoElement's
// chrome-only stereoSiteKeywords); the tags are read by libstereo-policy,
// the one reader of the edition's programs, in the parent process, and the
// mark counts under the 3D Video menu's choice and under the stream's and
// the container's own marks.
//
// media.stereo.site-marks switches it off; media.stereo.site-marks.hosts
// (comma separated domains, empty for every page) says where it applies.

const PREF = "media.stereo.site-marks";
const HOSTS_PREF = "media.stereo.site-marks.hosts";
// A watch page's keywords are a few hundred bytes; more is not a list of tags.
const MAX_KEYWORDS = 4096;

export class StereoSiteMarkChild extends JSWindowActorChild {
  #enabledFor(doc) {
    if (!Services.prefs.getBoolPref(PREF, true)) {
      return false;
    }
    const hosts = Services.prefs
      .getStringPref(HOSTS_PREF, "")
      .split(",")
      .map(h => h.trim().toLowerCase())
      .filter(Boolean);
    if (!hosts.length) {
      return true;
    }
    let host = "";
    try {
      host = doc.location.hostname.toLowerCase();
    } catch (e) {
      return false;
    }
    return hosts.some(h => host == h || host.endsWith("." + h));
  }

  #keywords(doc) {
    const meta = doc.querySelector('meta[name="keywords" i]');
    const content = (meta && meta.content) || "";
    return content.length <= MAX_KEYWORDS ? content : "";
  }

  #mark(videos, keywords) {
    for (const video of videos) {
      if (video.stereoSiteKeywords !== undefined) {
        video.stereoSiteKeywords = keywords;
      }
    }
  }

  handleEvent(event) {
    const doc = this.document;
    if (!doc || !this.#enabledFor(doc)) {
      return;
    }
    switch (event.type) {
      case "yt-navigate-start":
        // The next video's keywords are not on the page yet: no mark until
        // they are, so a flat video is never shown with the last one's.
        this.#mark(doc.querySelectorAll("video"), "");
        break;
      case "loadstart":
      case "loadedmetadata":
        if (event.target.localName == "video") {
          this.#mark([event.target], this.#keywords(doc));
        }
        break;
      default:
        // DOMContentLoaded, yt-navigate-finish, yt-page-data-updated
        this.#mark(doc.querySelectorAll("video"), this.#keywords(doc));
    }
  }
}
