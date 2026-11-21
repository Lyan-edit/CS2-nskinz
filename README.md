[![nSkinzCS2 offline demo cosmetic toolkit](./docs/assets/nskinz-banner.svg?v=0.7.6)](./docs/assets/nskinz-banner.svg?v=0.7.6)

<h3 align="center">Offline Counter-Strike 2 cosmetic overrides for HLAE demo playback</h3>

Map-resource diagnostics were assisted by [Source 2 Viewer](https://s2v.app) ([ValveResourceFormat](https://github.com/ValveResourceFormat/ValveResourceFormat)).

<p align="center">
  <a href="https://github.com/Lyan-edit/CS2-nskinz/blob/main/README.md"><img src="https://img.shields.io/badge/English-README-1f6feb?style=flat-square" alt="English README"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/blob/main/README.zh-CN.md"><img src="https://img.shields.io/badge/简体中文-README-555?style=flat-square" alt="简体中文 README"></a>
</p>

<p align="center">
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases/tag/v0.7.6"><img src="https://img.shields.io/badge/release-v0.7.6-2ea043?style=flat-square" alt="Latest stable release"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases"><img src="https://img.shields.io/github/downloads/Lyan-edit/CS2-nskinz/total?style=flat-square&label=downloads" alt="Total downloads"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz"><img src="https://img.shields.io/github/stars/Lyan-edit/CS2-nskinz?style=flat-square" alt="Stars"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/network/members"><img src="https://img.shields.io/github/forks/Lyan-edit/CS2-nskinz?style=flat-square" alt="Forks"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/issues"><img src="https://img.shields.io/github/issues/Lyan-edit/CS2-nskinz?style=flat-square" alt="Open issues"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square" alt="C++20">
  <img src="https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=flat-square" alt="Windows x64">
  <img src="https://img.shields.io/badge/HLAE-offline%20demo-6f42c1?style=flat-square" alt="HLAE offline demo">
</p>

<p align="center">
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases/download/v0.7.6/Lyan_nskinz-v0.7.6-EN.dll"><img src="https://img.shields.io/badge/Download-English%20DLL-2ea043?style=for-the-badge" alt="Download English DLL"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases/download/v0.7.6/Lyan_nskinz-v0.7.6-zh-CN.dll"><img src="https://img.shields.io/badge/下载-中文%20DLL-0969da?style=for-the-badge" alt="Download Chinese DLL"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases/tag/v0.7.6"><img src="https://img.shields.io/badge/Release-v0.7.6-2ea043?style=for-the-badge" alt="v0.7.6 release notes"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/issues/new/choose"><img src="https://img.shields.io/badge/Report%20an%20issue-f85149?style=for-the-badge" alt="Report an issue"></a>
</p>

> **Safety first:** nSkinzCS2 is intended only for offline demo playback started through HLAE with `-insecure`. Never load it on VAC-protected or online servers. The author does not guarantee account safety for use outside this scope.

## What's new in v0.7.6

Use live changes for guns, every knife mode, stock T/CT gloves and other glove models, and all five sticker slots. Charms use Temporary Demo reconstruction. Restore Workshop maps and fixed transparent radars. Start each process with an empty applied session so saved knife/glove settings cannot activate when only a gun is applied. Retain right-side HUD names, current-holder targeting and model-specific previews.

See the [bilingual release notes](docs/releases/v0.7.6.md). The locally inspected client is **CS2 2000948 / 1.42.0.0**. The user accepted knives, gloves and HUD behavior in the preceding test build. No CS2/HLAE process was started for this release; other recordings, rendered results and frame times still require acceptance testing.

## Features and application routes

| Module | How to use | Route |
| --- | --- | --- |
| Guns | Select the current holder XUID, firearm, finish, seed, wear and name; apply that gun | Live |
| Knives | Finish-only detects the current model and filters compatible paints; Model + Finish selects a target model | Both modes live |
| Gloves | Finish-only filters the current glove model; use Model + Finish for stock T/CT gloves or model changes | Both modes live |
| Stickers | Configure any of five slots, including wear/scale/rotation/position; apply one slot or explicitly apply all five; empty slots clear | Live |
| Charms | Configure holder, firearm, charm and blue placement preview; apply that charm | Reconstruct from the original Demo |
| Workshop maps | Subscribe/download in Steam, scan and choose a playable map, then apply | Reload the current Demo; no cosmetic XUID required |
| HUD | Synchronize full right-side weapon/knife names after appearance/model changes | Native HUD bridge retained |

Each Apply button submits only its item. Other edits remain staged; Save/Load edits the editor document only. Item quality follows the integrated engine's automatic policy rather than an ineffective manual control.

## Quick start

1. Fully exit CS2/HLAE carrying an older DLL. Start offline Demo playback through HLAE with `-insecure`.
2. Download one language DLL, place it in a writable folder and run (replace with the actual path):

```text
mirv_loadlibrary "D:\Lyan_nskinz-v0.7.6-EN.dll"
```

3. Press `Insert`, enter the current holder's XUID, configure an appearance and press its Apply button.
4. Guns, knives, gloves and stickers update live. Charm Apply captures the original Demo, generates a temporary file and loads it.
5. The selected sticker is yellow and other configured slots are white. Charms are blue. Preview edits stay staged until that item's Apply button is pressed.

The DLLs embed shaders, the rewriter and 19 reviewed radar references, with the static MSVC runtime. Players do not need Python, an SDK or Visual Studio. Load only one language; do not stack releases, test DLLs or another cosmetic DLL in the same process. See [portable loading](docs/portable-loading.md).

## Settings and sessions

- Editor settings: `nSkinzCS2.json`. Older release configurations remain editable. Back up and rename a test `nSkinzCS2.live-test.json` when migrating.
- Process applications: `nSkinzCS2.session-<CS2 PID>.applied.json`. Every DLL load starts empty, including a reused PID. Historical applied documents never activate a new session.
- Reopening the panel retains explicitly applied items in this process; seeks, map reloads and charm reconstruction retain them. Restarting requires clicking Apply again. Source-recorded cosmetics remain part of the recording.
- Logs: `nSkinzCS2.log` and `nSkinzCS2.ui.log`. The integrated engine keeps its existing `%TEMP%\nSkinzCS2-live-test-0.7.5\` history directory to preserve recorded model failures.
- Temporary Demo planning/execution filters out guns, knives, gloves and stickers. Only charms are reconstructed; original recordings are not overwritten. Map Apply does not require a cosmetic XUID or rewrite cosmetics.

## Workshop maps and transparent radars

Discovery lists playable top-level maps rather than auxiliary skyboxes or prefabs. Apply arms the map redirect and reloads the current Demo. Reviewed radar versions reuse embedded references. The locked Inferno Water standard cannot be regenerated by cleanup/recovery sliders. Other map geometry and recording combinations still need visual acceptance testing.

## Current native control panel

This is an unmodified backbuffer capture of the current MainWindow implementation, with no game connection or keyboard simulation. It retains the five-slot sticker and model-specific placement previews.

![nSkinzCS2 v0.7.6 control panel](./docs/assets/control-panel.png?v=0.7.6)

## Source, validation and attribution

The Release provides bilingual DLLs, a complete binary package, corresponding source and SHA-256 checksums. See [release evidence](docs/releases/v0.7.6.md), [CHANGELOG](CHANGELOG.md) and [release history](docs/releases).

Required third-party licenses and attribution are preserved in [NOTICE](NOTICE.md) and [third-party terms](THIRD_PARTY_NOTICES.md).

For issue reports include the exact DLL, CS2/HLAE versions, selected mode, reproduction steps and redacted logs.

<a name="support"></a>
## ❤️ Support the project

If nSkinzCS2 is useful to your offline demo workflow, optional support is appreciated. The QR images below are the author's provided no-ID payment codes; sponsorship is voluntary and does not provide a support contract or feature guarantee.

| **WeChat Pay** | **Alipay** |
| :---: | :---: |
| ![WeChat Pay QR code](./docs/assets/sponsor/wechat-pay.png) | ![Alipay QR code](./docs/assets/sponsor/alipay.png) |
