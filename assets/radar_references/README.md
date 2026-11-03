# 工坊雷达透明参考库

本目录保存已复查的透明雷达 PNG、原始纹理版本指纹及图像校验值。它们作为资源内嵌到 DLL，其他玩家不需要开发者的临时目录、独立图片或重新调整清理参数。

当前目录收录 19 种不同的雷达纹理版本：All in One、Ancient Night、Anubis N、Cache、Cache D、Cache N R、Dust2 Water、Dust2 Rainy、Inferno D、Inferno Rain、Inferno Water、Minecraft Cache、Minecraft Dust2、Mirage Rain、Mirage Water、Nache、Nuke Rainy、Vertigo Rain、Nirage。完整版本、工坊 ID、尺寸及 SHA-256 见 [catalog.json](catalog.json)。

## 固定标准

Inferno Water 已由用户明确批准并锁定。以 [固定标准记录](../../docs/diagnostics/inferno-water-radar-standard-20261103.md) 为准，不再修改其 PNG、轮廓或校验值。棋盘格仅用于文档预览，运行时文件是真透明 PNG。

## 加载规则

- 普通参考记录通过资源虚拟路径、原始字节长度和 FNV-1a 指纹匹配。清单同时保存源文件 SHA-256，供审计复核。相同地图的不同版本不会仅凭地图名混用。
- `locked` 标准属于用户明确指定的例外：保持固定图不变，源版本变化会记录 `reference_source_changed=1`，不会悄悄换回推断图。
- 已收录版本直接加载内嵌图，道路恢复和黑底容差不重新生成其轮廓。背景不透明度仍是独立显示设置；默认完全透明。
- 参考图使用原生 RGBA8888 纹理，避免 BC7 有损块压缩重新产生暗色边缘。资源编译器可以对存储进行无损压缩，游戏会正常解码。
- 没有自带雷达的地图继续原有资源路径；未收录的新版本保留通用推断路径，不会被伪装成已经核验的参考图。

## 后续维护

1. 运行测试程序的 `--export-workshop-radars`，并设置 `NSKINZ_RADAR_PREVIEW_DIR`，导出本机纹理、候选图及 `inventory.tsv`。
2. 在透明棋盘格和原始轮廓对照图上逐张复查道路、建筑及背景。候选图没有经过复查时不得加入参考库。
3. 用 `tools/build_radar_reference_catalog.py --import-reviewed <inventory.tsv> --candidates <候选目录> --confirm-reviewed` 导入已复查图。该命令不能覆盖已锁定标准。
4. 默认运行 `tools/build_radar_reference_catalog.py`，校验文件并生成 C++ 索引和 RCDATA 资源表。PNG 被改动而清单校验值未更新时会拒绝生成。
5. 运行 Python 目录保护测试及 C++ `--radar-only` 回归。发布前再验证原生编译后的图像与标准一致。

重新生成索引只改变资源引用，不赋予替换用户锁定图像的权限。
