# 材质测试版 2：面板输入交接与暂停重载

日期：2026-11-21。保留现有 UI 布局、配色、编辑方式和独立应用作用域。

## 复现与证据

用户反馈 ReShade 的 F8 面板快捷键在加载 DLL 后无法使用，暂停中的 Demo 也必须手动恢复播放才能重载。

本机实际 ReShade 配置和当次启动日志一致，面板快捷键为 F8。ReShade 运行时从 DLL 加载前持续存在到游戏关闭，日志未显示在 DLL 加载时被重建或销毁。nSkinz 的面板运行在独立进程，其 Win32 消息不会直接进入游戏的 ReShade 输入窗口；[ReShade 6.4.1 输入实现](https://github.com/crosire/reshade/blob/v6.4.1/source/input.cpp)按已注册的窗口处理键盘消息。现有面板只处理 Insert 显隐，隐藏时没有明确返回游戏焦点，也没有交接已到达面板的 F8。

当次材质状态为 Dust2、237 个已准备材质、237 次请求命中、地图匹配。这证明 DLL 的材质文件重定向被使用，不能由此断言最终画面完全来自 DLL，或 ReShade 已关闭。

暂停重载的根因是 `DispatchPlayback` 仅在动态识别的 PostDataUpdate 阶段执行；录制暂停时没有新的实体数据更新包，但仍然绘制画面。此时命令保留在队列，恢复播放后才被取走。

## 红绿测试

先补上真实派发函数和面板交接调用处的回归：

```text
nSkinzCS2.Tests.exe --live-hotpatch-only
FAIL: paused Demo reload must dispatch without network PostDataUpdate callbacks

nSkinzCS2.Tests.exe --material-settings-only
FAIL: F8 received by the nSkinz panel must reach the game overlay input path
```

修复后这两条命令通过。输入测试通过可注入的 Win32 操作边界验证正确进程、窗口、显隐、焦点和原始键消息顺序，既不启动游戏，也不向桌面注入按键。它验证输入交接逻辑，不代表实际 ReShade 画面已经端到端验收。

## 修复

- 重载命令仍在原有已确认的游戏帧线程派发；允许 FrameStart、RenderPass，以及原有数据更新阶段。暂停时不再依赖 PostDataUpdate 包；队列命令只取走一次，仍先应用雷达背景设置。不在后台工作线程调用引擎，不发送自动恢复播放或跳帧命令。
- Insert 收起、点击关闭：面板拥有焦点时，按运行时进程 ID 查找可见的 `SDL_app` 游戏窗口并交还焦点；面板处于后台时不抢其它应用的焦点。
- 当 F8 的首次按下已到达面板时，关闭面板、交还焦点并转发该原始按下消息。物理释放交给游戏处理；重复按下、其它键、其它前台应用、缺失运行时窗口均不转发。未更改 ReShade 配置或预设，也未替换游戏窗口过程。
- 材质原有区域增加运行时重定向状态，区分编辑器勾选与真正应用。ReShade 独立控制；DLL 关闭重定向后仍需重载，已有 GPU 材质缓存可能需要重启才能释放。

## 对比效果

先收起面板返回游戏，用 F8 打开 ReShade，关闭其效果。再在 nSkinz 独立应用 DLL 材质，重载后观察。要检查纯 ReShade，请关闭 DLL 材质并重载；若游戏继续复用旧材质，完整重启后保持 DLL 材质未应用，再开启 ReShade 比较。两条路径互不替对方开关，材质请求命中数也不是 GPU 画面的最终判据。

测试版 2 本次只交付本地 DLL，没有发布 GitHub Release，没有启动 CS2 或 HLAE。实际 F8 面板弹出、暂停重载、反光画面与 GPU 开销仍需在用户环境复测。
