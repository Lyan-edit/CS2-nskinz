"""Update/check the release banner and provenance of the real native UI capture.

Use --capture with the UiHarness --documentation PNG after visually reviewing it.
The image is copied unchanged; this tool does not draw or retouch the UI.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ("src/UI/MainWindow.cpp", "src/UI/MainWindow.hpp", "src/UI/UiText.hpp",
           "src/UI/WeaponPreviewGeometry.generated.hpp", "src/UI/StickerPreviewPlacement.generated.hpp",
           "src/UI/StickerPreviewPlacement.generated.cpp", "src/UI/StickerPreviewMetrics.hpp",
           "tools/UiPanelHarness.cpp", "nSkinzCS2.UiHarness.vcxproj")


def digest(path: Path) -> str:
    # Git checkouts may use CRLF or LF. Capture provenance tracks source text.
    data = path.read_bytes()
    if path.suffix != ".png":
        data = data.replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    version = re.search(r'kVersion = "([^"]+)"', (ROOT / "src/Core/BuildConfig.hpp").read_text()).group(1)
    banner = ROOT / "docs/assets/nskinz-banner.svg"
    picture = ROOT / "docs/assets/control-panel.png"
    record = ROOT / "docs/assets/control-panel.capture.json"
    if args.update:
        text, count = re.subn(r'(<text id="release-version"[^>]*>)v[^<]+?(  /  Windows)',
                              rf'\g<1>v{version}\2', banner.read_text(encoding="utf-8"))
        if count != 1:
            raise ValueError("Banner must contain one release-version text element")
        banner.write_text(text, encoding="utf-8")
    if args.capture:
        data = args.capture.read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError("Expected an unmodified native PNG capture")
        width, height = struct.unpack(">II", data[16:24])
        if width < 1000 or height < 1000:
            raise ValueError("Capture must show the full panel and sticker preview")
        shutil.copyfile(args.capture, picture)
        record.write_text(json.dumps({
            "version": version,
            "method": "Unmodified D3D11 backbuffer PNG from the real MainWindow implementation; UiHarness --documentation",
            "example_configuration": "AK-47, Case Hardened, five identical Titan (Holo) stickers; slot 1 custom placement enabled at zero delta",
            "game_connected": False, "keyboard_events_injected": False,
            "width": width, "height": height, "image_sha256": digest(picture),
            "source_sha256_lf": {name: digest(ROOT / name) for name in SOURCES},
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    text = banner.read_text(encoding="utf-8")
    if not re.search(r'<text id="release-version"[^>]*>v' + re.escape(version) + r'  / ', text):
        raise ValueError("Cover version is stale; run --update for every release")
    metadata = json.loads(record.read_text(encoding="utf-8"))
    if metadata["version"] != version or metadata["image_sha256"] != digest(picture):
        raise ValueError("Control-panel capture/version needs to be refreshed")
    if metadata["source_sha256_lf"] != {name: digest(ROOT / name) for name in SOURCES}:
        raise ValueError("UI source changed; rebuild UiHarness and capture --documentation again")
    for name in ("README.md", "README.zh-CN.md"):
        readme = (ROOT / name).read_text(encoding="utf-8")
        for asset in ("nskinz-banner.svg", "control-panel.png"):
            if f"docs/assets/{asset}?v={version}" not in readme:
                raise ValueError(f"Refresh {asset} cache version in {name}")
    print(f"PASS: v{version} cover, real UI capture/provenance and bilingual README image links")


if __name__ == "__main__":
    main()
