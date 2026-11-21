# 打蜡反光材质

`waxed_reflection.nsm` 由 DLL 的 RCDATA 500 内嵌。玩家只下载 DLL；首次独立应用时自动准备缓存，不需要下载 VPK，也不需要本机资源编译器。

- 格式：`NSKMAT01` + uint32 LE 清单长度 + UTF-8 JSON 清单 + 未压缩文件数据。
- 共 1450 个已编译 VMAT，覆盖 ancient、anubis、cache、dust2、inferno、mirage、nuke、overpass。
- 包 SHA256：`3051b49e4ccee1f6b48af7a08836fc4b1f08f3969d961564689e903d8783b360`。
- `waxed_reflection_sources.zip` 保存对应的 1450 个原生 VMAT 源文件。按地图分组，组内保留原虚拟路径。
- `verification.json` 保存逐项 SHA256、材质身份、编译元数据与参数一致性的离线验收结果。

包不包含游戏原版纹理，材质继续引用本机游戏资源。运行时只通过现有文件读取分发器，把当前匹配地图的已收录材质请求重定向到缓存；不向 GAME 搜索路径全局挂载这些同名资源。

运行缓存按资源包 SHA256 和地图分开，文件名使用虚拟路径的哈希以避免深层目录过长。每次准备核对文件内容，损坏文件从 DLL 修复。关闭功能后移除重定向，需要重载 Demo 才能重新读取原材质。引擎仍保留材质缓存时需要重启。

此离线验证不代表游戏内画面和 GPU 开销已经验收；测试说明见 `docs/development/materials-language-test1.md`。
