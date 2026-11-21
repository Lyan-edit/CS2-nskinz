# 地图材质包内嵌与运行时接入研究

日期：2026-11-21。源码基线：`build/publish-v0.7.6`，HEAD `b0ab04d`（文档提交），对应运行代码发布提交 `286d069`。本次只读源码和公开一手来源，未启动游戏或录制工具，未修改运行代码，未构建、提交或发布。以下“已验证”指静态源码事实，不代表当前游戏版本的运行验证。

## 结论

**只下载 DLL、无需另外下载材质 VPK，在分发层面可实现；现有代码已有二进制内嵌后自动写入缓存的模式。** 但“包内嵌”“引擎能找到文件”“地图实际引用替换材质”是三个独立环节，当前源码没有通用地图 VMAT 替换功能。现有雷达接入证明了隔离缓存目录和 `GAME` 搜索路径可作为接入起点，不能证明挂载任意材质包就会自动替换世界材质。[L1][L2][L3]

## 这份 VPK 的离线检查结果

输入文件：`C:/Users/Lyan/Downloads/catalog.vpk`，6,084,361 字节（约 5.80 MiB），VPK v2。SHA-256：`5e9e4426503ebe9cf070e4d00d9f34a9b00e12b18ba559270d9044ae5efdd0e8`。包共 1,452 项，1,450 个 `vmat_c`、2 个 `vtex_c`，所有项目自包含在此包，CRC 全部通过；没有完整地图、world node 或模型文件。原文件未修改。

| 地图分组 | 材质条目 |
| --- | ---: |
| `de_ancient` | 170 |
| `de_anubis` | 220 |
| `de_cache` | 84 |
| `de_dust2` | 237 |
| `de_inferno` | 73 |
| `de_mirage` | 248 |
| `de_nuke` | 215 |
| `de_overpass` | 203 |

使用已有 Source 2 Viewer CLI 对全部 1,450 个材质的 DATA 完整解码。编译名称尾部带原路径 SHA1 前 10 位，将其与本机原版 `pak01_dir.vpk` 中 23,404 个材质路径比对：**1,450/1,450 唯一匹配，未解析和歧义均为 0**，对应 1,313 个不同原材质。这里验证的是包的命名摘要与本机原材质路径的唯一对应，并不是游戏已经切换材质的验证。

DATA 和 RERL 中的 1,962 个不同纹理依赖均列在本机原版主资源包索引中。当前材质没有引用包内两个共享纹理；打包时可进一步确认是否需要保留。此检查不等于遍历了所有贴图像素和引擎着色器运行结果。

材质分布于 5 类 shader：`csgo_environment_blend.vfx`、`csgo_environment.vfx`、`csgo_complex.vfx`、`csgo_vertexlitgeneric.vfx`、`csgo_lightmappedgeneric.vfx`。集成必须保留各自纹理、混合层、顶点输入和光照参数，不能把全部材质硬改成一个统一 shader。原材质清单没有匹配武器、干员相关路径，但仍应保持明确的地图材质允许列表。

材质位于自定义新目录、使用数字文件名，**不是原始材质虚拟路径的同名覆盖**。目录和编译材质内部名称还带有用户此前要求清理的来源标识，原样放入 DLL 会把这些标识一起发布。正式导入应从解码参数重建为中性身份，同时处理依赖和资源 hash；不能只改 VPK 文件名或对编译二进制做任意字符串替换。

已生成本地候选映射清单：[material-bindings.json](E:/codex项目/CS2-nskinz-main/build/catalog-vpk-analysis/material-bindings.json)。其中 `planned_resource_path` 是设计中的中性目标路径，**并不代表已编译出可发布的归一化资源**。

较稳妥的首条实验路线是：完整原材质映射 → 中性身份重新编译 → 按原虚拟路径制作分地图的独立覆盖目录 → DLL 内嵌 → 用户首次应用自动解包缓存 → 载图前注册 → 重载验证实际读取。每个地图组独立选择，不同时挂载八组潜在共享原路径的材质。[L3][L4][L5][L6]


## 当前代码确实具备的能力

| 环节 | 已验证事实 | 对本任务的含义 |
| --- | --- | --- |
| DLL 二进制内嵌 | 工程把 `NskinDemoRewriter.rc` 和 `RadarReferences.rc` 编译进产物；前者已有 `RCDATA` → Win32 资源 API → 带指纹缓存文件 → 临时文件改名的实现。[L1] | 可复用资源打包和提取模式；不是还要要求最终用户下载旁置包。 |
| 原生资源落盘 | `MaterializeCompiledRadarOverlay` 验证雷达资源，把独立资源写进 `%TEMP%/nSkinzCS2/radar_overlay_<hash>/`，比较已有字节并回验文件。[L2] | 有独立目录、内容版本和失败诊断模式；该函数严格针对雷达 VTEX，不能直接接收 VMAT 包。 |
| 搜索目录注册 | `MapRemap.cpp` 从 `filesystem_stdio.dll!CreateInterface` 获取 `VFileSystem017`，读取 Open 13、ReadFile 25、AddSearchPath 31、RemoveSearchPath 32；现有注册参数为 `(path, "GAME", 0, 1, 0)`。[L3] | 可研究抽出独立目录注册层；不要复制接口索引后宣称新功能已验证。 |
| 请求路径改变 | 雷达 Mount 成功后保存目标请求虚拟路径及本地绝对资源路径；Open/ReadFile 命中后替换 `file_name`，再交给原函数。[L3][L4] | 雷达不是“仅挂载”：它显式改变请求路径。这一点对新名材质尤其关键。 |
| 地图资源重定向 | Manifest 回调只处理地图运行数据、天空资源和已知失败模型；运行数据分类只允许实体 lump、探针 atlas、cubemap 与已核对特例；天空只允许 `.vmap/.vmap_c`。[L5] | 已有地图 hook 不是通用 `.vmat/.vmat_c` 路径映射器。 |
| 资源 hash 请求 | `HookedRequestResource` 仅对 `vmdl` FourCC 且 hash 非零执行缺失模型判定，其他请求原样转发。[L6] | 不存在“现有函数已经按 VMAT hash 换材质”的依据。 |
| 配置与释放 | `ConfigureMapRemap` 一开始卸载雷达覆盖；有效换图配置要求数字工坊 ID 和安全地图名；挂载发生在换图 hooks 成功后；关闭时移除搜索目录和 hooks。[L7] | 当前 mount 生命周期依赖换图配置。独立材质包不能直接借用这一开关，否则会被雷达/换图配置重置。 |

`TryAddRadarSearchPath` 的返回 `true` 只表示调用未触发 SEH。其目标函数类型返回 `void`，代码没有读回搜索列表或验证实际资源请求命中。因此不能把这个布尔值解释为“已确认材质可加载”。`UnmountRadarOverlay` 也没有验证 `RemoveSearchPath` 的结果，更没有执行通用材质资源缓存清理。[L3]

## 看起来相关、实际尚不能复用的接口

### GlobalResourceSwapper

当前运行入口 `RuntimeController::TryInstallGlobalResourceSwapperEarly` 只标记 `forensic_disabled/off`。这个组件的公开安装函数忽略两个 resource 接口参数，走文件系统诊断探针；探针只采样路径/调用信息后原样调用原函数，并且主动跳过 MapRemap 占用的 ReadFile slot 25。[L8][L9]

文件里保留的 `hkResourceSystemSlot68` 确有参数重写代码，但匹配表只硬编码特定刀模型路径/词元，并没有材质映射目录、材质包选择或世界材质回滚功能。`.vmat` 仅在 `LooksLikeResourceSample` 中被识别为可记录的资源字符串；“可以采样到类似材质的字符串”和“已实现材质替换”应分开表述。[L10]

在此基线整个 `src` 搜索 `materialhash`、`MaterialHash`、`material_hash` 均没有匹配；重点代码也没有通用世界材质身份捕获入口。这是本地检索结果，不能推断游戏本身没有对应内部概念。现有 hash 请求使用的资源名 hash、场景里的材质指针、材质路径和其他工具所称的材质 hash 不能未经验证视为同一标识。[L6][L10][L11]

### Runtime.cpp 的阻塞资源预载

现有 `TryResolveGloveDirectRenderModelHandleAtSafePoint` 保留一个 ResourceSystem013 slot 40 的阻塞 preload 实现：规范化路径，构造 `0xE0` 描述符，在 `+0xD0` 放 MurmurHash64B（seed `0xEDABCDEF`），`+0xD8` 放扩展名，调用后取稳定句柄。该路径带 `allow_experimental_resource_calls` 门控，先对已有模型句柄做两次校准，并要求 Demo、指定 render pass、非 seek 安全窗口与非重入状态。[L11]

另一个材质重绑分支会从当前模型构造 `materials/<模型名>.vmat` 并调用上述函数。这说明本项目已有“在特定安全时点获取材质资源句柄”的代码线索；它处理的是饰品实体的 custom material 向量，**不是地图 world mesh 的材质绑定替换接口**。不能把加载出一个新材质句柄等同于让已编译地图使用它。[L12]

## 外部一手源码能支持到哪里

- Microsoft 官方文档明确支持把任意原始二进制放入 `RCDATA`，再通过 `FindResource` / `LoadResource` / `LockResource` 取出。它支持 DLL 单文件分发的封装方案，不提供 Source 2 从 PE 资源直接加载 VMAT 的接口。[S1][S2]
- `hl2sdk` 的 `cs2` 分支头文件定义了 `PATH_ADD_TO_HEAD=0`、`SEARCH_PATH_PRIORITY_LOOSE_FILES=1`，且 `AddSearchPath` 返回 `void`。它也声明 `AddUGCVPKFile`，注释描述 `ugc:<handle>` 文件名。可用来核对已有参数的意图，但这是该 SDK 项目的接口声明，不是 Valve 对当前客户端 ABI 的稳定承诺；不能用 UGC 声明证明任意本地 VPK 的完整挂载生命周期已可用。[S3]
- HLAE 自身 `SceneSystem.cpp` 有 ResourceSystem 枚举/预载包装、通过场景材质对象 `GetName()` 做过滤、以及天空实体专用材质句柄更新。它提供可进一步核对的真实实现线索；该源码中的索引、布局、签名及天空特例不应直接推广为 nSkinz 当前构建已支持的通用 world material 接口。[S4][S5]

本次没有找到能用来宣称“当前 nSkinz 仅挂载这个新名材质包就会生效”的一手依据；也没有找到已接入本项目的通用 VMAT invalidation/reload/restore API。

## 建议的接入路径（设计推断，尚未实现或运行验证）

1. **先完成离线映射和原资源身份恢复。** 继续以编译元数据与原版路径摘要核对 1450 个材质，解决剩余 12 个解析项，生成“原材质虚拟路径 → 替换内容”的 manifest，并记录地图/地图版本、依赖清单与文件校验。不能按数字顺序猜原材质。对需要去除内部来源名称的资源，要在中性项目路径重新编译并复核；仅更改 VPK 文件名或归档条目路径不会改变编译资源里的字符串。
2. **单独实现资源包封装层。** 用 DLL `RCDATA` 容纳整理后的资产或归档，自动提取到独立的 `nSkinzCS2/map_materials/<版本或内容校验>/` 目录。使用中性的产品命名；如改变虚拟路径，还必须同步编译资源内的依赖和映射。已有代码能证明提取模式可行，但不证明只改归档目录名就能修正 VMAT 内部引用。[L1][L2]
3. **优先验证原虚拟路径的 loose 覆盖目录。** 将重新编译并校验的替换 VMAT 放到 manifest 指定的原虚拟材质路径，保留已核对的原版纹理依赖；不复制游戏 `gameinfo`、shader 包或整张地图。创建目录、写临时文件、校验完成后再发布，并用自己的 mount 状态登记，避免占用 `g_radar_overlay_*`。在载图前按现有 AddSearchPath 路线注册独立目录，从一个材质做读取/画面对照。此方案不要求先新增猜测的世界材质 hook；但查找优先级、实际读取是否经过这些搜索路径仍要验证。嵌入完整 VPK 再调用新的 mount API 是另一项未验证的工作。[L2][L3]
4. **用一个真实材质验证完整闭环，再决定是否需要新的重定向入口。** 证明替换资源被读取、成功解析、实际影响指定世界表面，并验证关闭后的恢复。若原路径覆盖在已核对的加载时机仍无法生效，再研究现有 Open/ReadFile 分发层或经过版本校准的场景绑定入口；前者是否覆盖地图的实际异步/打包读取链尚不明，后者在本项目未实现。现有 Manifest hook 也不能假定所有 VMAT 依赖都会经过，加载出新句柄也不能替代绑定验证。[L4][L5][L11][S4]
5. **验证后扩展到整个 manifest。** 统计命中/缺失/依赖错误，并把逻辑地图、实际显示的目标地图、包版本及有效绑定分开记录。用户只要 DLL 可以成立，但本地会自动产生资源缓存文件；“无需额外下载”不等于“完全不落盘”。

本研究优先推荐**原虚拟路径同名覆盖 + 载图前注册 + 重载验证**，因为它沿用本项目已使用的搜索路径能力，并减少为新名素材新增世界绑定 hook 的需要。它仍是一项需要实验确认的推断：不能仅从 `PATH_ADD_TO_HEAD` 的名字承诺一定覆盖成功。[L3][S3]

## 同名覆盖、缓存与重载边界

**已验证事实：** 雷达同时使用内容地址化的虚拟资源名与物理缓存目录，且请求被转向那个绝对缓存资源；换图流程要求下一次 Demo 加载，日志明确提示重载。代码中的 unmount 移除的是搜索路径，而非地图材质实例或资源系统句柄；本次未发现与材质包对应的通用缓存清理逻辑。[L2][L3][L4][L7][L13]

**由此作出的保守推断：** 已加载的同名 VMAT、依赖纹理或场景绑定可能继续保留旧资源；切换搜索目录不应设计为保证即时换肤。即使重载同一 Demo，也不能在没有验证的情况下保证所有材质被释放。首版验收应分别测试冷启动首次载图、已载图后启用、包版本切换、关闭并重载、地图切换和 DLL 卸载；如果观察到缓存残留，产品应明确要求相应重载或重启，而不是无依据地调用猜测的 invalidate 虚表槽。

内容校验目录解决的是磁盘上的版本隔离，不能单独解决引擎按旧虚拟路径或旧句柄缓存的问题。对新名材质，版本化虚拟路径可以减少身份复用，但仍需可靠的绑定切换；对同名覆盖，必须单独验证加载时机和恢复行为。

## 与工坊换图及现有 hooks 的冲突风险

以下是由现有结构推导的设计风险，不是已经观察到的故障：

- 当前换图会从录制源地图切到实际目标地图；按录制源名选择材质包，可能套到不匹配的目标场景。默认应在未知工坊目标上关闭材质覆盖，只有 manifest 明确验证过目标资源版本才允许组合。[L5][L7]
- 同名覆盖可能影响多个地图共用的材质路径，搜索路径本身没有“只影响某个 world surface”的范围。不能按地图按钮全局挂一组文件后默认其他工坊材质不受影响。[L3][S3]
- Open/ReadFile 与 Manifest 已被 MapRemap 使用，新功能若再次对同一地址 `MH_CreateHook`，可能打断现有链路或使回滚删除对方 hook。应使用单一分发层或明确 hook ownership，不另起第二套抢占相同函数。现有 forensic 探针跳过 slot 25 已表明代码在规避这类所有权冲突。[L4][L9]
- `ConfigureMapRemap` 会先卸载 radar overlay；独立材质包的挂载状态不应受一次雷达参数更新影响。关闭材质包要停止新的重定向，再在已验证的安全时点释放/重载；仅删除缓存文件或移除路径不能证明已绑定材质恢复。[L7]
- 目标地图的光照探针、cubemap、实体资源和世界材质不是同一个层。现有代码对特定地图对保留来源 probe atlas，是有范围的特例；材质反光/透明效果还需要在实际目标场景观察，不能承诺材质包等于地图重烘焙。[L5]

## 本轮交付与未完成验证

本轮交付是一份接入研究；运行代码未变动。可以肯定的是 DLL 内嵌提取模式与独立目录基础已经存在，原材质候选映射已全部恢复并与当前本机资源索引唯一匹配。尚需主任务完成或后续单独验证的是：当前客户端及目标地图的映射适用性、依赖闭包与中性路径重建、真正覆盖地图材质的入口、当前版本 ABI、异步读取覆盖率、缓存/回滚，以及工坊换图组合。任何一个未解决时，都不应把“包可提取/可注册目录”报告成“材质功能已完成”。

## 本地证据索引

下列链接均指本次审阅的正式源码；括号内为精确行范围，链接定位范围起点。

- [L1] [工程资源项](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/nSkinzCS2.vcxproj:252)（252–253）；[内嵌提取实现](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/DemoSkinRewrite.cpp:324)（324–408）；[RCDATA定义](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Resources/NskinDemoRewriter.rc:3)。
- [L2] [雷达编译资源缓存](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/RadarNativeCompiler.cpp:514)（514–583）；[接口限定](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/RadarNativeCompiler.hpp:18)（18–31）。
- [L3] [接口获取、搜索路径与雷达挂载](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:1246)（1246–1448）；[签名与slot常量](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:74)（74–147）。
- [L4] [文件名重定向分发](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:550)（550–667）。
- [L5] [Manifest路径处理](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:1019)（1019–1098）；[天空资源分类](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:1737)（1737–1811）；[地图运行数据分类](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:1814)（1814–1936）。
- [L6] [hash请求只处理缺失vmdl](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:813)（813–853）。
- [L7] [换图配置/挂载时机](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:1994)（1994–2084）；[关闭](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/MapRemap.cpp:2109)（2109–2124）。
- [L8] [运行入口禁用诊断swapper](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/Runtime.cpp:91508)（91508–91517）；[状态文字](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/Runtime.cpp:13829)（13829–13837）。
- [L9] [文件系统探针避让slot25](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/GlobalResourceSwapper.cpp:2051)（2051–2119）；[探针原样转发](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/GlobalResourceSwapper.cpp:8468)（8468–8511）；[安装入口](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/GlobalResourceSwapper.cpp:8585)（8585–8592、8676–8686、8747–8773）。
- [L10] [仅刀模型匹配表](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/GlobalResourceSwapper.cpp:2226)（2226–2335）；[样本字符串分类](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/GlobalResourceSwapper.cpp:2888)（2888–2917）；[旧query处理](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/GlobalResourceSwapper.cpp:8514)（8514–8581）。
- [L11] [特定安全点的资源预载](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/Runtime.cpp:32575)（32575–32794）。
- [L12] [饰品材质名构造和句柄取得](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Cs2/Runtime.cpp:37550)（37550–37624）。
- [L13] [内容地址化雷达虚拟路径](E:/codex项目/CS2-nskinz-main/build/publish-v0.7.6/src/Runtime/RadarAlphaFix.cpp:1297)（1297–1323）。

## 外部来源

- [S1：Microsoft RCDATA resource](https://learn.microsoft.com/en-us/windows/win32/menurc/rcdata-resource)。
- [S2：Microsoft Finding and Loading Resources](https://learn.microsoft.com/en-us/windows/win32/menurc/finding-and-loading-resources)。
- [S3：hl2sdk cs2 public/filesystem.h](https://raw.githubusercontent.com/alliedmodders/hl2sdk/cs2/public/filesystem.h)（访问时 SearchPath 枚举约228–239、AddSearchPath约456–457、UGC声明约624–628；分支会变化，不能拿行号当版本锁）。
- [S4：HLAE SceneSystem.cpp](https://raw.githubusercontent.com/advancedfx/advancedfx/main/AfxHookSource2/SceneSystem.cpp)（访问时110–145枚举与预载、161–179天空专用更新、234–244场景材质字段、825–850读取材质名）。
- [S5：HLAE SceneSystem.h](https://raw.githubusercontent.com/advancedfx/advancedfx/main/AfxHookSource2/SceneSystem.h)（访问时10–26材质对象/预载声明）。


## 面向用户的独立应用设计

新增“地图材质”配置：启用、效果预设（本包为打蜡反光）、当前/目标地图、应用与恢复。保持编辑暂存/显式应用会话；未点击应用不因保存文件而自动加载效果。应用只提交材质这一项，不触发皮肤或挂件应用。

此效果修改的是资源加载和地图场景外观，建议首版按“挂载后重载当前 Demo”实现，不经过 Demo 实体重写。材质独立服务不能依赖一个伪造的工坊 ID 来激活现有换图流程；雷达、地图重映射和材质覆盖应各自拥有生命周期。

用户只需下载 DLL。内嵌字节仍需在运行时自动释放到隔离缓存，以便引擎读资源；这和“用户不另外下载 VPK”兼容，不等于承诺纯内存、零落盘。无需在玩家机器上运行资源编译器，最终发布包应自带已编译数据。

内容校验、解包与映射构建在首次应用/版本变更时执行，不放进逐帧循环。反光 shader 的 GPU 成本、首次材质载入卡顿以及重载/恢复是否能清掉旧资源，都需游戏内测量。本次未启动 CS2/HLAE，未修改 DLL，未把原包或其派生素材提交到 GitHub。
