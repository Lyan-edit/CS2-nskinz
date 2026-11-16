# 0.7.4 工坊地图替换：manifest 特征失效

2026-11-16。本次为 0.7.4 本地修复，兼容基线保持 CS2 2000943／1.41.9.7。

## 证据与复现

本机最新 UI 日志记录：已发现 37 个工坊地图，选中 `3318429218/de_inferno_water`，
固定标准雷达已完成外部编译并发布配置。运行日志随后记录：

```text
header=1, spawn=1, clear=1, server=1, session=1, assign=1,
mapkv=1, failed=1, manifest=0, request=1, find=1, kvexport=1.
Hooks were not installed.
```

旧版 manifest 特征在当前 engine2.dll 中为零匹配，导致地图替换整条钩子链按原有保护策略拒绝安装。
雷达生成成功与地图替换成功是不同环节。

针对真实安装文件的定向回归在修改前失败，明确输出 `manifest_path matches=0`。
测试使用和运行时相同的扫描器，没有启动游戏或安装钩子。

## 根因与修复

当前 `engine2.dll` SHA256：
`dcb5fe13f60f0db9faa365c7b48c3ca107ce56783748bf9ed9ca41fd892af63c`。

原特征尾部为 `74 32 4C 8B B5`，当前为 `74 2B 41 8B 16`。
新特征在可执行代码区唯一命中 `engine2+0x3F574E`。
反汇编及导入表确认该处仍调用 `CUtlSymbolTable::AddString`；调用前 R8 从路径字符串取值，
RDX 指向输出 symbol，RCX 指向 symbol table，R9 清零。原有参数保存／替换桥继续适用。

更新精确特征，保持唯一匹配要求和整链原子安装策略。
将运行时的 11 个入口定位提取为共用解析器，新增原始 PE `.text` 离线检查，
完整回归在提供 `--cs2-root` 时也会运行此检查，覆盖此前未纳入检查的 manifest 入口。

定向命令：

```text
build/Release/nSkinzCS2.Tests.exe --map-remap-only --cs2-root "E:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive"
```

修复后输出全部 11 项 `matches=1`，以及：

```text
map_remap_installed_signatures=PASS; hook_sites=11; native_game_calls=0
```

## 验证和交付边界

- 定向检查在同一安装文件上先失败、后通过。
- 完整 C++ 回归通过，包含原生材质／HUD、地图路径和雷达检查。
- 中英文 x64 DLL 编译、便携导入、六种隔离加载及 WARP 着色器验证通过。
- 修复包保留 fixed8 临时 Demo 重建修复和 19 项多普勒／伽马多普勒标签。
- 固定 Inferno Water 标准图及内嵌雷达资源保持一致。

未启动 CS2／HLAE，也未实际加载工坊地图。加载新 DLL 后的游戏内效果需实测。
退出仍加载旧 DLL 的进程，只加载一个语言版本，重新打开原 Demo 后对地图点击智能应用并等待重载。

原始复现、反汇编、构建与校验日志位于 `build/map-remap-20261116/`。
