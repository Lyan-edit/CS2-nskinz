# 安装、加载与升级

适用版本：v0.7.6。先完成本页，再配置外观。

## 下载什么

从 [v0.7.6 Release](https://github.com/Lyan-edit/CS2-nskinz/releases/tag/v0.7.6) 下载以下任意一个 DLL：

| 文件 | 用途 |
| --- | --- |
| `Lyan_nskinz-v0.7.6-zh-CN.dll` | 中文面板与中文目录名称 |
| `Lyan_nskinz-v0.7.6-EN.dll` | 英文面板与英文目录名称 |
| `Lyan_nskinz-v0.7.6-CS2-2000948-windows-x64.zip` | 两种语言、说明、必要许可证与校验文件的完整包 |
| `Lyan_nskinz-v0.7.6-source.zip` | 对应源码；普通使用无需下载 |
| `SHA256SUMS.txt` | 核对下载文件完整性 |

只加载一种语言。DLL 内嵌面板着色器、挂件重写器和雷达参考资源；普通使用不需要安装 Python、Visual Studio 或单独的 DirectX SDK。

## 第一次加载

1. 将 DLL 解压或保存到可写文件夹，例如 `C:\nSkinzCS2\`。不要直接从压缩包内部加载。
2. 通过 HLAE 启动 CS2，使用 `-insecure`，播放离线 Demo。
3. 在 CS2 控制台执行，路径替换为磁盘上的真实路径：

```text
mirv_loadlibrary "C:\nSkinzCS2\Lyan_nskinz-v0.7.6-zh-CN.dll"
```

4. 返回游戏后按 `Insert`／`Ins` 打开控制面板。部分笔记本需要使用键盘提供的 `Fn` 组合。
5. 填入目标 XUID、启用配置，选择一个外观后点击该项应用按钮。

指令使用一对英文半角双引号。不要把 GitHub 网页地址、ZIP 地址或其它电脑的路径放进加载指令。`ok` 只表示 DLL 被加载，不等于面板、运行时、目标玩家和某项外观都已成功应用。

## 校验下载文件

在 PowerShell 中可执行：

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath 'C:\nSkinzCS2\Lyan_nskinz-v0.7.6-zh-CN.dll'
```

把结果与同一 Release 的 `SHA256SUMS.txt` 比对。不同版本或语言的摘要不能混用。

## 升级与退出

1. 在面板中保存需要保留的编辑配置，并备份 `nSkinzCS2.json`。
2. 完整退出承载旧 DLL 的 CS2；升级时重新启动 HLAE/CS2。
3. 放入新版本 DLL，按新文件名重新加载。不要向仍装有旧 DLL 的进程叠加新版。
4. 打开面板，检查模式、XUID、枪型和涂装；对需要的项目分别点击应用。

关闭面板窗口或隐藏面板不会卸载 DLL，也不会结束当前进程的已应用会话。需要干净基线时，重新启动 CS2 并加载原始 Demo。

保存的配置只用于继续编辑。v0.7.6 新进程不会自动启用之前保存的刀、手套或其它项目，详见[配置与会话](Configuration-and-Diagnostics.md)。

## 加载失败或面板不出现

先核对文件路径、摘要、单一 DLL 和准确的 HLAE/CS2 版本，再收集 `nSkinzCS2.log` 与 `nSkinzCS2.ui.log`。详细流程见[故障排除](Troubleshooting.md)。不要反复叠加加载同一个 DLL，也不要仅凭 `LoadLibrary failed` 就认定缺少运行库。
