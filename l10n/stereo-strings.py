"""The strings of the browser's 3D Video and 3D Canvas menus in the edition's languages.

Usage: python3 stereo-strings.py L10N_BASE LOCALE...

L10N_BASE is Waterfox's l10n tree (waterfox/browser/locales). The messages
go after "Loop" in <locale>/browser/browser/browserContext.ftl, where the
video context menu's other strings are; en-US has them in the browser's own
tree. A locale not listed here falls back to English. Idempotent: messages a file
already has stay as they are, missing ones go after the last of them.
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
    "main-context-menu-video-stereo-swap",
    # 3D Canvas (milestone 5)
    "main-context-menu-canvas-stereo",
    "main-context-menu-canvas-stereo-off",
    "main-context-menu-canvas-stereo-on",
    "main-context-menu-canvas-stereo-depth-less",
    "main-context-menu-canvas-stereo-depth-normal",
    "main-context-menu-canvas-stereo-depth-more",
    "main-context-menu-canvas-stereo-depth-most",
)
# The menus' access keys.
ACCESSKEYS = {
    "main-context-menu-video-stereo": "3",
    "main-context-menu-canvas-stereo": "3",
}

# 3D Video, Automatic, Not 3D, Side by Side, Side by Side Right Eye First,
# Top and Bottom, Top and Bottom Right Eye First, Swap Eyes; 3D Canvas, Off,
# On, Less Depth, Normal Depth, More Depth, Most Depth
LABELS = {
    "pl": (
        "Wideo 3D",
        "Automatycznie",
        "Nie 3D",
        "Obok siebie",
        "Obok siebie, najpierw prawe oko",
        "Góra i dół",
        "Góra i dół, najpierw prawe oko",
        "Zamień oczy",
        "Kanwa 3D",
        "Wyłączone",
        "Włączone",
        "Mniejsza głębia",
        "Zwykła głębia",
        "Większa głębia",
        "Największa głębia",
    ),
    "pt-BR": (
        "Vídeo 3D",
        "Automático",
        "Não é 3D",
        "Lado a lado",
        "Lado a lado, olho direito primeiro",
        "Em cima e embaixo",
        "Em cima e embaixo, olho direito primeiro",
        "Inverter os olhos",
        "Canvas 3D",
        "Desligado",
        "Ligado",
        "Menos profundidade",
        "Profundidade normal",
        "Mais profundidade",
        "Profundidade máxima",
    ),
    "fr": (
        "Vidéo 3D",
        "Automatique",
        "Pas en 3D",
        "Côte à côte",
        "Côte à côte, œil droit en premier",
        "Haut et bas",
        "Haut et bas, œil droit en premier",
        "Inverser les yeux",
        "Canvas 3D",
        "Désactivé",
        "Activé",
        "Moins de profondeur",
        "Profondeur normale",
        "Plus de profondeur",
        "Profondeur maximale",
    ),
    "ja": (
        "3D 動画",
        "自動",
        "3D ではない",
        "サイドバイサイド",
        "サイドバイサイド (右目が先)",
        "トップアンドボトム",
        "トップアンドボトム (右目が先)",
        "左右の目を入れ替え",
        "3D キャンバス",
        "オフ",
        "オン",
        "奥行き: 小",
        "奥行き: 標準",
        "奥行き: 大",
        "奥行き: 最大",
    ),
    "ko": (
        "3D 동영상",
        "자동",
        "3D 아님",
        "좌우 배치",
        "좌우 배치, 오른쪽 눈 먼저",
        "상하 배치",
        "상하 배치, 오른쪽 눈 먼저",
        "좌우 눈 바꾸기",
        "3D 캔버스",
        "끄기",
        "켜기",
        "깊이 작게",
        "깊이 보통",
        "깊이 크게",
        "깊이 최대",
    ),
    "zh-CN": (
        "3D 视频",
        "自动",
        "非 3D",
        "左右并排",
        "左右并排，右眼在前",
        "上下排列",
        "上下排列，右眼在前",
        "交换左右眼",
        "3D 画布",
        "关闭",
        "开启",
        "景深较浅",
        "景深标准",
        "景深较深",
        "景深最深",
    ),
    "zh-TW": (
        "3D 影片",
        "自動",
        "非 3D",
        "左右並排",
        "左右並排，右眼在前",
        "上下排列",
        "上下排列，右眼在前",
        "交換左右眼",
        "3D 畫布",
        "關閉",
        "開啟",
        "景深較淺",
        "景深標準",
        "景深較深",
        "景深最深",
    ),
}


def messages(text, labels):
    out = []
    for i, (msg, label) in enumerate(zip(IDS, labels)):
        if f"\n{msg} =" in text:
            continue
        out.append(f"{msg} =\n    .label = {label}\n")
        if msg in ACCESSKEYS:
            out[-1] += f"    .accesskey = {ACCESSKEYS[msg]}\n"
    return "\n" + "\n".join(out) if out else ""


def add(path, labels):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    block = messages(text, labels)
    if not block:
        return False
    anchor = "main-context-menu-media-loop ="
    for msg in IDS:
        if f"\n{msg} =" in text:
            anchor = f"\n{msg} ="
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
            print(f"{path}: 3D Video and 3D Canvas menus")


if __name__ == "__main__":
    main()
