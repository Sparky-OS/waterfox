/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global pref */

// The system package manager updates packaged builds.
pref("dom.ipc.forkserver.enable", true);
pref("app.update.enabled", false);
pref("distribution.id", "sparky-stereo-os");
pref("distribution.version", "1.0");
pref("distribution.about", "Sparky Stereo OS package");

// Videos and canvases on Wayland surfaces of their own (WebRender's native
// compositor), where a picture holding both eyes is declared to Stereo KWin.
// Gecko's Linux blocklist turns the compositor off on every device
// (widget/gtk/GfxInfo.cpp, FEATURE_WEBRENDER_COMPOSITOR), so it is forced on.
pref("gfx.webrender.compositor", true);
pref("gfx.webrender.compositor.force-enabled", true);

// The screen as a WebXR immersive device (Sparky Stereo OS): an
// "immersive-vr" session shows its canvas full screen and each eye its view,
// declared to Stereo KWin; inline sessions may ask for "inline-stereo".
pref("dom.vr.enabled", true);
pref("dom.vr.webxr.enabled", true);
pref("dom.vr.flat.enabled", true);
