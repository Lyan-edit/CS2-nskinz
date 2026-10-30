# 饰品目录漏项修复与全模块核对

日期：2026-10-30。修复纳入 [v0.6.6 正式版](../releases/v0.6.6.md)，保留原 v0.6.6-beta 附件。

## 根因

“运动手套－隐知”在已安装游戏中是 paint `1417`，内部名 `glove_sport_velvet_red`。旧目录把 `9001` 或 `>=10000` 归为手套，其余归枪皮；手套型号再依赖 `sporty_` 等旧前缀。新系列同时改变 ID 范围和内部命名，因此 22 个手套涂装误入枪皮总表，又没有手套兼容映射。旧面板快照可直接看到 `1417` 在 weapon_paint_kits 中，而不在 glove_paint_kits 中。

## 修复

- 依据 items_game 的精确 `[paint]item` 收藏／掉落条目及 VPK 库存图标，建立型号与涂装对应关系；手套图标不再被 `weapon_` 文件名前缀过滤。
- 消除 ID 大小的类型假设。已证实的枪／手套组合优先于材质候选与旧前缀；保留无精确数据时的旧手套兼容信息，矛盾型号证据保持阻止状态。
- 手套从枪皮列表及模型年代元数据中移出；现有 IPC 快照完整保存新增映射。
- 补齐已被重建底层支持、却漏在 UI 名单之外的廓尔喀刀（Kukri，526），包括本地化名称及模型／图标元数据。新增测试要求所有支持的市场刀型都存在于目录，防止静态名单漂移。
- 解析挂件的具名 `base` 模型继承，补全 61 个纪念品挂件的模型元数据；其 ID 本来已在选项中，不宣称这 61 个此前都无法使用。未知／循环基类不猜测模型。

## 已安装文件逐项核对

目录证据来自 CS2 `2000938`／Patch `1.41.9.3` 的本机 VPK，英文与简体中文各核对一次。独立 Python 审计直接读取 KeyValues 与 VPK，不调用 C++ 分类器；对照 C++ 导出的目录、可见列表及映射。

| 模块 | 修复后范围 | 漏项／错误映射 |
| --- | --- | --- |
| 常规枪械 | 34 把、1,471 个有效型号／涂装组合 | 0 |
| 市场刀型 | 20 种、556 个型号／涂装组合 | 0；补上 Kukri 及其 12 个组合 |
| 手套 | 94 个真实涂装 | 0；补上 22 个 |
| 贴纸选项 | 11,642 项（含 0 项） | 0 |
| 挂件 | 143 个非零选项 | 0；模型元数据全部可解析 |

手套列表还保留 0 与 Workshop 占位项，因此界面原始目录条数为 96，不应把占位计入 94 个真实涂装。挂件的“无”项使原始条数为 144。

电击枪 Zeus（31）仍在既有常规枪械支持范围之外；游戏存在 7 个相关涂装，但本次没有仅增加菜单便宣称接通其刷新、重建、贴纸或挂件链。默认刀与特殊模式物品也不冒充市场刀型漏项。

## 22 个手套漏项

| 型号 | ID | 中文名 | 内部名 |
| --- | --- | --- | --- |
| 驾驶手套 | 1398 | 逐沧浪 | `glove_driver_silk_dragon_white` |
| 驾驶手套 | 1399 | 深红鹤服 | `glove_driver_brocade_crane_red` |
| 驾驶手套 | 1400 | 繁花似锦 | `glove_driver_brocade_flower_red` |
| 驾驶手套 | 1401 | 龙拳 | `glove_driver_dragon_scale_red` |
| 驾驶手套 | 1402 | 花园 | `glove_driver_embroidered_flowers_teal` |
| 驾驶手套 | 1404 | 青海波 | `glove_driver_scallop_metallic_blue` |
| 运动手套 | 1405 | 紫罗兰珠绣 | `glove_sport_beadwork_diamonds_purple` |
| 运动手套 | 1406 | 寒霜 | `glove_sport_camo_winter` |
| 运动手套 | 1407 | 炽烈之炎 | `glove_sport_flames_orange` |
| 运动手套 | 1408 | 奶油细条纹 | `glove_sport_pinstripe_black` |
| 运动手套 | 1409 | 赤色追风 | `glove_sport_racer_red` |
| 运动手套 | 1410 | 紫外狂潮 | `glove_sport_tech_gradient_bright` |
| 驾驶手套 | 1412 | 蓝紫点子 | `glove_driver_ostrich_purple` |
| 专业手套 | 1413 | 青柠迷彩 | `glove_specialist_abstract_green` |
| 专业手套 | 1414 | 黑皮书 | `glove_specialist_graffiti_black` |
| 专业手套 | 1415 | 可可伯爵 | `glove_specialist_quilted_black` |
| 专业手套 | 1416 | 日芒 | `glove_specialist_sunburst_red` |
| 运动手套 | 1417 | 隐知 | `glove_sport_velvet_red` |
| 专业手套 | 1437 | 狂澜 | `glove_specialist_wave_blue` |
| 专业手套 | 1438 | 羽袭 | `glove_specialist_quilted_white` |
| 驾驶手套 | 1439 | 针织手套 | `glove_driver_knit_argyle_green` |
| 专业手套 | 1440 | 青云狩 | `glove_specialist_dragon_blue` |

## 验证与使用

- 回归覆盖低 ID 手套、精确掉落／图标组合、纯图标定义、旧前缀兜底、名称碰撞、未来高 ID 枪皮、矛盾证据和 VPK 手套图标读取。
- 94 个手套涂装各生成默认 T／CT 两条临时 Demo 请求，验证计划接受；型号不符被拒绝。没有引入默认手套的实时写入或实体创建路径。
- 挂件模型直接／嵌套继承、失踪基类和循环基类均有回归。
- 完整原生测试、中英文 DLL 便携加载／中文文本及 WARP 渲染检查通过。数据核对和请求生成不等同于逐款游戏内画面验收，也不表示完成新 CS2 客户端全部原生地址适配。

正式版文件位于 `build/Release/Lyan_nskinz v0.6.6 EN.dll` 和 `build/Release/Lyan_nskinz v0.6.6 zh-CN.dll`。退出并重启 CS2／HLAE，只加载一个新 DLL，让启动流程重建目录快照；默认手套配置后从原始录制重新生成临时 Demo。

可重复审计工具：`tools/audit_catalog_coverage.py`。原始游戏提取、前后对比、双语快照及报告保存在本机 `build/glove-catalog-20261030/`，不将游戏数据或私有日志纳入源码。
