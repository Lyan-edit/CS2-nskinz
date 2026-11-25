[![nSkinzCS2 HLAE 离线 Demo 皮肤工具](./docs/assets/nskinz-banner.svg?v=0.7.8)](./docs/assets/nskinz-banner.svg?v=0.7.8)

<h3 align="center">面向 HLAE Demo 回放的 Counter-Strike 2 离线外观工具</h3>

地图资源诊断使用了 [Source 2 Viewer](https://s2v.app)（[ValveResourceFormat](https://github.com/ValveResourceFormat/ValveResourceFormat)）辅助验证。

<p align="center">
  <a href="https://github.com/Lyan-edit/CS2-nskinz/blob/main/README.md"><img src="https://img.shields.io/badge/English-README-555?style=flat-square" alt="English README"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/blob/main/README.zh-CN.md"><img src="https://img.shields.io/badge/简体中文-README-1f6feb?style=flat-square" alt="简体中文 README"></a>
</p>

<p align="center">
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases/tag/v0.7.8"><img src="https://img.shields.io/badge/release-v0.7.8-2ea043?style=flat-square" alt="最新正式版"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases"><img src="https://img.shields.io/github/downloads/Lyan-edit/CS2-nskinz/total?style=flat-square&label=下载量" alt="下载量"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz"><img src="https://img.shields.io/github/stars/Lyan-edit/CS2-nskinz?style=flat-square" alt="Stars"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/network/members"><img src="https://img.shields.io/github/forks/Lyan-edit/CS2-nskinz?style=flat-square" alt="Forks"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/issues"><img src="https://img.shields.io/github/issues/Lyan-edit/CS2-nskinz?style=flat-square" alt="Issues"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square" alt="C++20">
  <img src="https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=flat-square" alt="Windows x64">
  <img src="https://img.shields.io/badge/HLAE-离线%20Demo-6f42c1?style=flat-square" alt="HLAE 离线 Demo">
</p>

<p align="center">
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases/download/v0.7.8/Lyan_nskinz-v0.7.8.dll"><img src="https://img.shields.io/badge/下载-中英文共用%20DLL-0969da?style=for-the-badge" alt="Bilingual DLL"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/releases/tag/v0.7.8"><img src="https://img.shields.io/badge/Release-v0.7.8-2ea043?style=for-the-badge" alt="v0.7.8 发布说明"></a>
  <a href="https://github.com/Lyan-edit/CS2-nskinz/issues/new/choose"><img src="https://img.shields.io/badge/提交问题-f85149?style=for-the-badge" alt="提交问题"></a>
</p>

> **安全提示：** nSkinzCS2 只用于通过 HLAE、带 `-insecure` 启动的离线 Demo 回放。请勿加载到 VAC 保护服务器或任何联机服务器。超出此范围使用造成的账号风险由使用者自行承担。

## v0.7.8 更新

[官方 Wiki 使用手册](https://github.com/Lyan-edit/CS2-nskinz/wiki) · [手册源码与离线阅读](docs/wiki/Home.md)

枪械涂装可直接应用到目标玩家身上携带的所选枪械，无需先切出来。已换肤的枪丢出后保留涂装，后来捡到的其它枪保留原涂装。结果栏显示游戏端是否确认应用。

详见[双语发布说明](docs/releases/v0.7.8.md)。本机核验客户端为 **CS2 2000950 / 1.42.0.1**。用户已在前述测试版验证应用及离手效果。

## 当前功能与应用路径

| 模块 | 使用方法 | 应用路径 |
| --- | --- | --- |
| 枪械 | 填写携带者 XUID，选择枪型和涂装，点击应用到玩家当时身上的这把枪 | 当前 Demo 热修 |
| 刀 | 仅饰面先检测当前刀型并筛选相容涂装；模型＋饰面选择目标刀型，再点刀的应用 | 两种模式都热修 |
| 手套 | 仅饰面筛选当前手套型号涂装；默认 T/CT 或换型号请选择模型＋饰面，再点手套应用 | 两种模式都热修 |
| 贴纸 | 五槽分别设置图案及磨损/缩放/旋转/位置；可只应用当前槽或明确应用全部五槽，空槽表示清除 | 热修 |
| 挂件 | 配置 XUID、枪型、挂件与蓝色预览位置，再点挂件应用；自动挂点保留已有位置，新挂件使用模型默认挂点 | 从原始 Demo 重建临时文件 |
| 创意工坊地图 | 先在 Steam 订阅并下载地图，扫描后选中可游玩的目标，点地图应用 | 配置重映射后重载当前 Demo；不需要饰品 XUID |
| 地图材质 | 地图页底部勾选打蜡反光、选择基准地图，独立应用或恢复 | 内嵌材质＋Demo 重载；无需 XUID 或另下 VPK |
| 语言 | 设置 → 语言 / Language；默认中文，可即时切换英文并保存 | 同一个 DLL，不触发饰品应用 |
| HUD | 刀及枪械涂装/型号应用后同步右侧完整物品名称 | 保留原生 HUD 更新链 |

每个应用按钮只提交当前项目。编辑其它项目不会连带提交；保存/加载仅操作编辑器。热修品质采用已集成引擎的自动策略，面板不提供无效的手动品质选项。

## 快速开始

1. 完整退出加载了旧 DLL 的 CS2/HLAE，使用 HLAE、带 `-insecure` 启动并播放离线 Demo。
2. 从本页下载中英文共用的单个 DLL，放入可写文件夹。在控制台执行（替换为实际路径）：

```text
mirv_loadlibrary "D:\Lyan_nskinz-v0.7.8.dll"
```

3. 按 `Insert` 打开独立面板。填写目标当前持枪者 XUID，选择外观，点击该项的智能应用按钮。
4. 枪械、刀、手套和贴纸直接热修；挂件首次应用会捕获原始 Demo 路径、生成临时文件并加载。
5. 贴纸预览中选中槽为黄色，其它已配置槽为白色，共五槽；挂件为蓝色。拖动/滑轨只改变编辑器预览，点击对应应用才更新游戏。

单个中英文共用 DLL 内嵌着色器、重写器、19 个雷达参考版本和 1450 项地图材质，采用静态 MSVC 运行库，不需要玩家安装 Python、SDK 或 Visual Studio。不要在同一进程叠加正式版、测试版或其它外观修改 DLL。加载问题见[单 DLL 加载说明](docs/portable-loading.md)。

## 配置、启动与日志

- 语言偏好：`nSkinzCS2.settings.json`，默认简体中文；切换语言不会修改或应用饰品。
- 编辑器配置：`nSkinzCS2.json`。旧正式版配置可继续编辑；测试版 `nSkinzCS2.live-test.json` 可备份后复制为正式名称。
- 当前进程应用文件：`nSkinzCS2.session-<CS2 PID>.applied.json`。每次加载 DLL 会初始化为空，不会读取历史整份 `.applied.json` 自动启用刀、手套等项目。
- 关闭/重新打开面板保留本会话已明确应用的项目；回跳、换图或挂件重建后它们继续工作。重新启动后需重新点击各项应用。源 Demo 自带饰品仍属于录制内容。
- DLL 日志：`nSkinzCS2.log`；独立面板日志：`nSkinzCS2.ui.log`。导入运行时的历史/诊断目录继续使用 `%TEMP%\nSkinzCS2-live-test-0.7.5\`，以保留已知失败模型的兼容记录。
- 刀、手套和贴纸不会进入临时 Demo 重写计划；临时 Demo 页只重建挂件。原 Demo 不覆盖。换地图按钮不要求 XUID，也不触发饰品重写。

## 创意工坊地图与透明雷达

扫描只列出可游玩的顶层地图，不把 skybox/prefab 当作独立目标。选择地图并应用后重载同一 Demo。已确认的雷达版本复用内嵌参考图，锁定的 Inferno Water 标准不会被容差或道路恢复控件重新生成。其它地图仍保留透明处理和资源诊断；不同地图烘焙几何体与具体 Demo 组合需要画面验收。

## 打蜡反光地图材质

在地图页向下滚动，勾选“启用打蜡反光”，选择 Ancient / Anubis / Cache / Dust2 / Inferno / Mirage / Nuke / Overpass 对应基准，再点该项独立应用。Mirage Water / Inferno Water 使用对应基准，其它未知工坊变体不自动匹配。DLL 内嵌 1450 个编译材质，无需另下 VPK；缓存校验、解包和损坏修复在工作线程执行。恢复按钮关闭重定向并重载，已有引擎材质缓存可能需要重启。

ReShade 效果独立控制。Insert 收起当前面板会交还游戏焦点，F8 按到面板时也会交还原始按键。对比时先关闭 ReShade 效果再应用 DLL 材质；反向对比需要关闭 DLL 材质并重载／重启。界面显示运行时重定向状态，不把“勾选”或请求命中数当作最终 GPU 画面已经更新。

## 当前原生控制面板

下面是当前 MainWindow 原生渲染导出的真实截图；没有连接游戏或模拟键盘。五槽贴纸及具体枪型预览均保留。

![nSkinzCS2 v0.7.8 控制面板](./docs/assets/control-panel.png?v=0.7.8)

## 源码、验证与授权

Release 提供一个中英文共用 DLL、完整包、对应源码和 SHA-256 校验。验证说明见[发布记录](docs/releases/v0.7.8.md)，历史更新保留在 [CHANGELOG](CHANGELOG.md) 与 [docs/releases](docs/releases)。

第三方组件的必要许可证和署名见 [NOTICE](NOTICE.md) 与 [第三方声明](THIRD_PARTY_NOTICES.md)。

问题反馈请包含准确的 DLL 版本、CS2/HLAE 版本、所用模式、复现步骤及脱敏日志。

<a name="support"></a>
## ❤️ 支持项目

如果 nSkinzCS2 对你的离线 Demo 工作流有帮助，欢迎自愿支持。下方是作者提供的无 ID 收款码；赞助完全自愿，不代表提供技术支持合同或功能保证。

| **微信支付** | **支付宝** |
| :---: | :---: |
| ![微信支付收款码](./docs/assets/sponsor/wechat-pay.png) | ![支付宝收款码](./docs/assets/sponsor/alipay.png) |
