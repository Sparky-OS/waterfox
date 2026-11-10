/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Sparky Stereo OS: what the person sees while an immersive WebXR session
// shows the flat stereo display: a hint on entry (Esc leaves, how the mouse and
// keyboard move the view), the mode the person chose, and a visible Exit
// control, always on the screen, in the person's language.
//
// The browser itself tells the document when such a session begins and ends
// (XRSession, the chrome-only events MozStereoXRStart and MozStereoXREnd; a page
// cannot send them); this actor draws the overlay into the document's anonymous
// content, in the top layer so that it lies over the full screen canvas, and its
// Exit button ends the session through XRSystem.exitFlatSession(), which only
// the browser's own code can call. Escape needs no overlay: XRSession ends the
// session on it, with or without full screen (WebXR Device API, "shut down the
// session").

const HINT_SECONDS = 9;

export class StereoXRChild extends JSWindowActorChild {
  #content = null;
  #popovers = [];
  #focused = null;
  #refocus = null;
  #timer = 0;

  handleEvent(event) {
    if (event.type == "MozStereoXRStart") {
      try {
        this.#show();
      } catch (e) {
        console.error("StereoXR: the overlay could not be drawn", e);
      }
    } else if (event.type == "MozStereoXREnd") {
      this.#hide();
      this.#refocus = this.#focused;
      this.#focused = null;
      this.#restoreFocus();
    } else if (event.type == "fullscreenchange") {
      if (this.#content) {
        // The canvas is put into the top layer after the session began (the
        // fullscreen request is answered later): raise the overlay over it.
        this.#raise();
      }
      this.#restoreFocus();
    }
  }

  // Leaving full screen takes the keyboard focus off the page's element (the
  // Enter button that began the session, for one): give it back once the
  // document is out of full screen, so that the keyboard goes on from where it
  // was (Tab, Enter and Esc alone enter and leave 3D again).
  #restoreFocus() {
    const element = this.#refocus;
    const doc = this.document;
    if (!element || !doc || doc.fullscreenElement) {
      return;
    }
    this.#refocus = null;
    try {
      if (element.isConnected && doc.activeElement != element) {
        element.focus({ preventScroll: true });
      }
    } catch (e) {
      // the element is gone
    }
  }

  #raise() {
    for (const element of this.#popovers) {
      try {
        element.hidePopover();
        element.showPopover();
      } catch (e) {
        // the element is gone (the hint after its time)
      }
    }
  }

  didDestroy() {
    this.#hide();
  }

  #show() {
    this.#hide();
    const doc = this.document;
    this.#focused = doc && doc.activeElement != doc.body ? doc.activeElement : null;
    const win = this.contentWindow;
    if (!doc || !win) {
      return;
    }
    const l10n = new Localization(["browser/browserContext.ftl"], true);
    const [exit, hint, modeName] = l10n.formatValuesSync([
      "stereo-xr-exit",
      "stereo-xr-hint-flat",
      "stereo-xr-mode-tv",
    ]);

    const content = doc.insertAnonymousContent();
    this.#content = content;
    const root = content.root;
    const style = doc.createElement("style");
    style.textContent = `
      .stereo-xr {
        position: fixed; inset: auto; margin: 0; border: 0; padding: 8px 14px;
        background: rgba(0, 0, 0, 0.72); color: #fff;
        font: 16px/1.4 sans-serif; border-radius: 8px;
        display: flex; align-items: center; gap: 14px; overflow: visible;
      }
      #stereo-xr-bar { top: 12px; right: 12px; pointer-events: auto; }
      #stereo-xr-hint { bottom: 24px; left: 50%; transform: translateX(-50%);
                        max-width: 80vw; text-align: center; }
      #stereo-xr-hint[hidden] { display: none; }
      button { font: inherit; color: #fff; background: #b3261e; border: 0;
               border-radius: 6px; padding: 6px 14px; cursor: pointer; }
      button:hover { background: #d0382e; }
      button:focus-visible { outline: 3px solid #fff; }
    `;
    const bar = doc.createElement("div");
    bar.id = "stereo-xr-bar";
    bar.className = "stereo-xr";
    bar.setAttribute("popover", "manual");
    const label = doc.createElement("span");
    label.textContent = modeName;
    const button = doc.createElement("button");
    button.id = "stereo-xr-exit";
    button.textContent = exit;
    button.addEventListener("click", () => {
      try {
        win.navigator.xr.exitFlatSession();
      } catch (e) {
        console.error("StereoXR: the Exit control could not end the session", e);
      }
    });
    bar.append(label, button);
    const hintBox = doc.createElement("div");
    hintBox.id = "stereo-xr-hint";
    hintBox.className = "stereo-xr";
    hintBox.setAttribute("popover", "manual");
    hintBox.textContent = hint;
    root.append(style, bar, hintBox);
    // The top layer, after the full screen canvas went into it: over it.
    this.#popovers = [bar, hintBox];
    bar.showPopover();
    hintBox.showPopover();
    this.#timer = win.setTimeout(() => {
      this.#timer = 0;
      this.#popovers = this.#popovers.filter(e => e != hintBox);
      try {
        hintBox.hidePopover();
      } catch (e) {
        // already gone
      }
    }, HINT_SECONDS * 1000);
  }

  #hide() {
    if (this.#timer) {
      try {
        this.contentWindow.clearTimeout(this.#timer);
      } catch (e) {
        // the window is gone
      }
      this.#timer = 0;
    }
    if (this.#content) {
      try {
        this.document.removeAnonymousContent(this.#content);
      } catch (e) {
        // the document is gone
      }
      this.#content = null;
    }
    this.#popovers = [];
  }
}
