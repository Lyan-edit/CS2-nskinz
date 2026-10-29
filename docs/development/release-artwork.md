# Release artwork / 发版图片同步

每次发版都更新封面，使用当前 UI 的真实原生渲染替换过期面板图；不能用绘制的假面板或旧版截图标作新版。`BuildConfig.hpp` 是版本来源。

1. 修改 `src/Core/BuildConfig.hpp`、DLL TargetName、发布说明和 README 下载链接。
2. 编译 `nSkinzCS2.UiHarness.vcxproj`，配置 `ReleaseZhCn | x64`。它运行相同的 MainWindow 代码，不加载 CS2 或 Hook。可在 EXE 旁放置本机生成的 `nSkinzCS2.catalog.json`，只用于截图，不提交游戏目录文件。
3. 在自己的测试进程中运行 `nSkinzCS2.UiPreview.exe --documentation`，导出 `control-panel.png`。专用宏只在 harness 中启用，提供较高画布以完整展示面板；没有注入键盘操作，也不修改正式 DLL 的默认窗口尺寸。
4. 查看图片，确认完整的控件、五槽贴纸和具体枪型预览清晰，无个人标识。
5. 双语 README 中封面和面板图的 `?v=` 更新为当前版本，再执行：

```text
python tools/update_release_artwork.py --update --capture build/ui-panel-harness/ReleaseZhCn/control-panel.png
python tools/update_release_artwork.py --check
```

工具原样复制 PNG，更新 SVG 版本文字，并记录截图版本、图像摘要及 UI 源码摘要。源码摘要按 LF 归一化，兼容 Windows 与 Linux。更新 README、SVG、PNG 和 `control-panel.capture.json` 后一起提交。`.github/workflows/release-artwork.yml` 检查 main、PR 和版本标签。

The native harness exports the actual UI backbuffer with sample data and no game connection. Review the PNG, update the README image cache versions, then run the two commands above. Every release refreshes the SVG version and capture metadata. CI detects stale versions, modified images or UI source changes that need a new capture. This verifies documentation freshness, not in-game rendering or physical Insert input.
