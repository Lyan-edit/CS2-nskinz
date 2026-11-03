# 配置名称、游戏内名称标签与物品品质

## 两个名称输入框

上方输入框对应配置的 `name`，用于左侧配置列表及配置文件中的识别名称；下方对应 `custom_name`，由换肤管线写入物品的自定义名称。两个字段用途不同，因此保留，界面分别明确标为“配置名称”和“游戏内名称标签”，并增加悬停说明。

留空的游戏内名称使用现有默认值 `nSkinz CS2`。刀具右侧 HUD 的完整刀型／涂装名称由名称同步桥单独提交，自定义名称仍保留在原有字段。

## 品质保留的依据

品质对应游戏经济物品的 `m_iEntityQuality`，与贴图清晰度、渲染画质、磨损和皮肤稀有度是不同概念。

- 枪械：`BuildWeaponAppearancePayload` 保留配置品质；实时路径 `ApplyProfileToEconItem` 写入，字段验证过程回读该值；临时 Demo 的 `weapon_fields` 同样序列化所选品质。
- 手套：`BuildGloveAppearanceRequest` 与临时 Demo 计划统一使用 `ResolveCanonicalGloveQuality`。带涂装且选择默认 `0` 时转换为特殊 `3`，这是此前默认 T／CT 手套重建修复的一部分。Rust 重建器写入 `m_EconGloves.m_iEntityQuality`，输出校验同时检查目标品质。
- 部分品质值在某类物品上没有可见差异；选择品质不等于改变磨损，也不能替代独立的 StatTrak 计数配置。已核对实际字段写入与验证路径，未宣称每个枚举值在每类饰品上都有可见效果。

因此保留品质控件，补充用途说明。没有删除默认手套所需的规范化逻辑。

## 中文品质选项

枪械、刀具、手套的品质下拉框统一在显示时按数值 ID 汉化，包含当前本机目录中的全部 15 个品质。旧的英文目录缓存也立即使用中文预览文字与中文选项；配置中继续保存数值 ID。搜索同时接受中文显示名和目录原名称，未来未知 ID 保留目录原文。

| ID | 中文显示 | 原枚举名 |
| --- | --- | --- |
| 0 | 默认 | Default |
| 1 | 纯正 | Genuine |
| 2 | 经典 | Vintage |
| 3 | 特殊（★） | Unusual |
| 4 | 独特 | Unique |
| 5 | 社区 | Community |
| 6 | 开发者 | Developer |
| 7 | 自制 | Self-Made |
| 8 | 定制 | Customized |
| 9 | 计数（StatTrak™） | Strange |
| 10 | 已完成 | Completed |
| 11 | 阴森 | Haunted |
| 12 | 纪念品 | Tournament |
| 13 | 高光 | Highlight |
| 14 | 易变 | Volatile |
