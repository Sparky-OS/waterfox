"""Waterfox's own strings that mean "this browser" take the brand name.

Usage: python3 brand-strings.py L10N_BASE LOCALE...

L10N_BASE is Waterfox's l10n tree (waterfox/browser/locales): en-US keeps
its files in en-US/browser/waterfox, the other locales in
<locale>/browser/browser/waterfox. In each message listed below "Waterfox"
becomes { -brand-short-name }. The names of BrowserWorks' services (the
privacy proxy, the privacy relay, the search partners), its funding and
development, and the names of Waterfox's own designs (the Waterfox look,
styles, themes, control icons, tabs, defaults, "Waterfox Exclusive") stay,
as attribution. Idempotent.
"""
import os
import re
import sys

BRAND = "{ -brand-short-name }"
# message id -> how many "Waterfox" to replace from the start (None: all)
MESSAGES = {
    "blocker.ftl": {
        "waterfox-blocker-setting-on-description": None,
        "waterfox-blocker-setting-off-description": None,
        "waterfox-blocker-extension-detected": None,
        "waterfox-blocker-extension-install-warning": None,
        "waterfox-blocker-spotlight-title": None,
        # "... helps support Waterfox" is its funding: kept.
        "waterfox-blocker-spotlight-subtitle": 1,
        "waterfox-blocker-prompt-title": None,
        "waterfox-blocker-reenable-conflict-message": None,
        "waterfox-blocked-page-title": None,
        "waterfox-blocked-page-heading": None,
    },
    "feeds-preferences.ftl": {"waterfox-feeds-opening-reader": None},
    "feeds.ftl": {"feeds-open-article": None},
    "privacy.ftl": {"telemetry-page-subtitle": None},
    "appearance.ftl": {
        "waterfox-appearance-details-group": None,
        "waterfox-opt-panel-icons": None,
        "waterfox-opt-content-heading": None,
    },
    "onboarding.ftl": {
        "onboarding-grassroots-subtitle": None,
        "waterfox-onboarding-privacy-subtitle": None,
        "waterfox-onboarding-default-subtitle": None,
        "waterfox-onboarding-finish-subtitle": None,
    },
}
ID = re.compile(r"^([a-zA-Z][\w-]*)\s*=")


def rewrite(path, messages):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    current, left, changed = None, {}, 0
    for i, line in enumerate(lines):
        m = ID.match(line)
        if m:
            current = m.group(1) if m.group(1) in messages else None
            if current:
                left[current] = messages[current]
                if left[current] is not None and BRAND in line:
                    current = None  # already rewritten
        elif line and not line[0].isspace() and not line.startswith("#"):
            current = None
        if not current or "Waterfox" not in line:
            continue
        count = left[current]
        if count is None:
            new = line.replace("Waterfox", BRAND)
        else:
            new = line.replace("Waterfox", BRAND, count)
            left[current] = count - min(count, line.count("Waterfox"))
            if left[current] == 0:
                current = None
        changed += new != line
        lines[i] = new
    if changed:
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))
    return changed


def main():
    base, locales = sys.argv[1], sys.argv[2:]
    for locale in locales:
        folder = "browser/waterfox" if locale == "en-US" else "browser/browser/waterfox"
        for name, messages in MESSAGES.items():
            path = os.path.join(base, locale, folder, name)
            if os.path.exists(path):
                n = rewrite(path, messages)
                print(f"{locale}/{folder}/{name}: {n} lines")


if __name__ == "__main__":
    main()
