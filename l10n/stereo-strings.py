"""The strings of the browser's 3D Video menu in the edition's languages.

Usage: python3 stereo-strings.py L10N_BASE LOCALE...

L10N_BASE is Waterfox's l10n tree (waterfox/browser/locales). The messages
go after "Loop" in <locale>/browser/browser/browserContext.ftl, where the
video context menu's other strings are; en-US has them in the browser's own
tree. A locale not listed here falls back to English. Idempotent.
"""
import os
import sys

IDS = (
    "main-context-menu-video-stereo",
    "main-context-menu-video-stereo-auto",
    "main-context-menu-video-stereo-mono",
    "main-context-menu-video-stereo-side-by-side",
    "main-context-menu-video-stereo-side-by-side-right-first",
    "main-context-menu-video-stereo-top-bottom",
    "main-context-menu-video-stereo-top-bottom-right-first",
)

# 3D Video, Automatic, Not 3D, Side by Side, Side by Side Right Eye First,
# Top and Bottom, Top and Bottom Right Eye First
LABELS = {
    "pl": (
        "Wideo 3D",
        "Automatycznie",
        "Nie 3D",
        "Obok siebie",
        "Obok siebie, najpierw prawe oko",
        "Góra i dół",
        "Góra i dół, najpierw prawe oko",
    ),
    "pt-BR": (
        "Vídeo 3D",
        "Automático",
        "Não é 3D",
        "Lado a lado",
        "Lado a lado, olho direito primeiro",
        "Em cima e embaixo",
        "Em cima e embaixo, olho direito primeiro",
    ),
    "fr": (
        "Vidéo 3D",
        "Automatique",
        "Pas en 3D",
        "Côte à côte",
        "Côte à côte, œil droit en premier",
        "Haut et bas",
        "Haut et bas, œil droit en premier",
    ),
    "ja": (
        "3D 動画",
        "自動",
        "3D ではない",
        "サイドバイサイド",
        "サイドバイサイド (右目が先)",
        "トップアンドボトム",
        "トップアンドボトム (右目が先)",
    ),
    "ko": (
        "3D 동영상",
        "자동",
        "3D 아님",
        "좌우 배치",
        "좌우 배치, 오른쪽 눈 먼저",
        "상하 배치",
        "상하 배치, 오른쪽 눈 먼저",
    ),
    "zh-CN": (
        "3D 视频",
        "自动",
        "非 3D",
        "左右并排",
        "左右并排，右眼在前",
        "上下排列",
        "上下排列，右眼在前",
    ),
    "zh-TW": (
        "3D 影片",
        "自動",
        "非 3D",
        "左右並排",
        "左右並排，右眼在前",
        "上下排列",
        "上下排列，右眼在前",
    ),
}


def messages(labels):
    out = []
    for i, (msg, label) in enumerate(zip(IDS, labels)):
        out.append(f"{msg} =\n    .label = {label}\n")
        if i == 0:
            out[-1] += "    .accesskey = 3\n"
    return "\n" + "\n".join(out)


def add(path, labels):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if IDS[0] + " =" in text:
        return False
    block = messages(labels)
    anchor = "main-context-menu-media-loop ="
    at = text.find(anchor)
    if at < 0:
        text = text.rstrip("\n") + "\n" + block
    else:
        end = text.find("\n\n", at)
        end = len(text) if end < 0 else end + 1
        text = text[:end] + block + text[end:]
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return True


def main():
    base, locales = sys.argv[1], sys.argv[2:]
    for locale in locales:
        labels = LABELS.get(locale)
        if not labels:
            continue
        path = os.path.join(base, locale, "browser", "browser", "browserContext.ftl")
        if os.path.exists(path) and add(path, labels):
            print(f"{path}: 3D Video menu")


if __name__ == "__main__":
    main()
