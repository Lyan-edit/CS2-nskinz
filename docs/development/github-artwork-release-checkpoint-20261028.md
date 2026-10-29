# GitHub 发布与首页图片检查点

记录：2026-10-28。用户要求先保存进度，优先修复控制面板启动异常。

- **v0.6.6 beta 发布暂停**，没有修改版本号，没有创建对应标签／Release，也没有上传该版附件。
- 最近确认的公开版本为 v0.6.5，源码提交 `09870cde54aaa6de26bbe02ca75083005c72ee85`。本轮只做本地开发，没有推送 GitHub。
- 首页封面仍为 `docs/assets/nskinz-banner.svg`，可见版本尚为 v0.5.4；面板图仍为 `docs/assets/control-panel.png`。本轮尚未替换这些文件。
- 原图及 SHA-256 备份在本地 `build/artwork-checkpoint-20261028/`。
- 原生真实面板预览工具已编译：`nSkinzCS2.UiHarness.vcxproj`、`tools/UiPanelHarness.cpp`；输出位于 `build/ui-panel-harness/ReleaseZhCn/`。它使用相同 UI 代码，不加载 CS2 或 Hook。
- 窗口自动化工具启动失败，返回 `failed to write kernel assets`。预览工具已增加测试专用的 DX11 渲染帧 PNG 导出；测试输出暂不作为首页更新发布。
- 尚未完成：新面板图片验收与替换、封面版本更新、后续发布的封面同步／检查流程。

后续恢复图片与发布任务时，须使用当时的实际版本更新封面，并用当前真实 UI 替换旧图；每次发版都应更新封面版本。发布前重新编译、验证附件与对应源码，保留双语 DLL、ZIP、源码及校验清单。

当前优先工作：分析用户提供的两份 v0.6.5 日志，修复 UI 线程在进入 Insert 循环前因 C++ 异常永久退出的问题。刀具模式分流修复仍保留，不覆盖已发布的 v0.6.5 附件。


## 2026-10-29 恢复

用户明确恢复 v0.6.6-beta 发布与首页图片更新。面板启动修复与刀具模式分流均纳入本版；旧备份保留。封面版本已同步，当前真实面板由原生 harness 重新导出并目视检查。发版图片流程见 [release-artwork.md](release-artwork.md)，其检查纳入 CI。历史暂停状态以 2026-10-28 为准。
