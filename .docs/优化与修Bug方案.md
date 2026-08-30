# 优化与修 Bug 方案（依据 `.docs/下一步完善.txt`）

> 新功能开发告一段落，本阶段集中做**优化与修 Bug**。任务清单见 `.docs/下一步完善.txt`（47 条，
> 混有"询问"与"任务"）。本文档把任务拆成可落地的执行方案，给出排序理由、涉及文件、风险与
> 验收方式；可调整先后顺序（本文已重排，见 §0 排序原则）。
>
> 开发前请先读：`.docs/工程规范.md`（本方案全程遵守）、`.docs/地图尺寸比例映射.md`、
> `.docs/密铺性能优化分析.md`、`.docs/配置说明.md`。行为变化须按新的**语义基线**判断：基线只统计
> 每格势力归属，以及所有兵的类型、势力、位置、速度方向、速度（工程规范 §3，
> `.docs/开发计划.md` §0）。

> 当前进度：Phase 4 已完成。完成 Phase 4 后清理了部分无用代码，并将基线从完整快照 hash 改为上述语义基线；
> 因此下列任务的基线影响需要按实际是否改变这些语义字段重新判断。

---

## 0. 前置结论与排序原则

### 0.1 询问的答案（实现前提，已核实源码）

| 原文条目 | 结论 | 出处 |
|---|---|---|
| 1 放置城市阶段同级不同类数量安排 | 总名额按修正幂律分配，`weight = n_x·(level+β)^-s`（`n_x`=同级形状变体数），最大余数法拆整数名额；同级不同形状**不单独配额**，由 `addCity` 用 rng 从可放下变体中随机选 | `Map.cpp:285-352,757-774` |
| 37 点相邻（共享顶点）查询 | **未实现**。现有接口只有边邻接 `neighbor/neighborRaw/neighborCount` | `Tiling.h:96-112` |
| 39 一个格的中心是什么中心 | **多边形质心**（顶点平均）。方/六/正三角/arch（正多边形）质心=内切圆圆心；Laves 非正多边形质心≠内切圆圆心 | `Tiling.cpp:492-536`；`build_laves4612_new.py:31` |
| 40 山地贴图对准内切圆中心 | 当前渲染用 `cellCenter`（质心），Laves 会偏；需为 Laves 计算真实内切圆圆心 | `MapRenderer.cpp:170` |

### 0.2 排序原则（与 `.docs/下一步完善.txt` 原序号不同）

1. **先做大概率不改行为的事**（配置外置、代码统一、纯渲染/UI）——先核对语义基线，风险最低。
2. **行为调整集中到一段**，统一按工程规范 §3 流程更新基线，避免反复漂移。
3. **接口先行于依赖它的 bug 修复**（点相邻接口 → 山生成 bug；内切圆 → 山地贴图）。
4. **性能优化放最后**（改动面最大、影响最广，且依赖部分前置接口）。
5. **文档/清理收尾**（最后写代码文档才准确；清理需人工判断，故放最后）。

### 0.2.1 新语义基线的影响判定

基线不是“整个模拟状态”的 hash，而是对以下字段计算的 hash：

- 地图每个格子的势力归属 `belongi`；
- 每个兵的类型、势力、位置、速度方向和速度。

按此口径：

- **原则上会改基线**：直接修改上述字段，或修改地图生成、移动、碰撞、占领、产兵等逻辑，
  导致上述字段的结果发生变化的任务。例如速度、产兵方向、山地规则和尺寸导致的地图布局变化。
- **必然不改基线**：只改渲染、UI、输入流程、配置文件组织、配置序列化或快照字段，且不改变模拟
  读取到的数值与执行结果的任务。经济、科技、特效、RNG 状态和实体编号本身不在语义基线中。
- **条件性影响**：代码统一、几何查询和性能优化等任务。如果新旧实现对同一输入给出完全相同的
  地图归属、兵状态和执行顺序，则基线不变；若改变了地图尺寸、格定位、边穿越、碰撞，或改变 RNG
  消耗并进一步导致上述字段变化，就按行为变化处理并更新基线。仅 RNG 状态变化而语义字段不变时，
  不更新语义基线，但应视风险补做完整快照/回放回归。

语义基线未覆盖的状态仍可通过 `Snapshot::serialize` 做补充回归。因此“基线不变”只表示上述语义
字段不变，不表示经济、科技、特效、RNG 或完整快照全部不变。

### 0.3 全局纪律（每阶段都必须遵守）

- **确定性红线**：任何可能影响语义基线的模拟行为变化 → 重跑
  `build-release\landwar.exe --headless --seed 42 --ticks 1000 --summary`（快速烟测），再按
  `tests/test_baseline.cpp` 更新或核对语义基线；只影响未纳入语义基线的状态时，仍应视风险补做
  `Snapshot::serialize` 回归。
- **config 键纪律**（工程规范 §4）：增删 config 键时同步 `Config.h` 默认值、`loadFromJson/toJson`、`validateConfigKeys`、`.docs/配置说明.md`。
- **CMake**：新增 `.cpp` 手动加入 `CMakeLists.txt`（禁 glob）。
- **测试**：每阶段 `ctest --preset default` 全绿；相关测试补用例。
- **少写兜底机制**（工程规范 §8）。
- **旧格式不兼容默认值**：对旧功能做改动时，无特殊说明，一律默认不用兼容旧格式，该删的就删；
  不确定是否还有用的旧文件/旧工具先移入 `rubbish/`（工程规范 §8）。

---

## Phase 1 — 配置与数据外置（不改行为）

### 1.1 清理 `config.json` 的 `map.file/width/height`（原文 5）

- **现状**：菜单已可手动选地图（预装图列表 + 随机图参数），`config.json` 的
  `map.file/width/height` 仅是陈旧默认（`Options`、`Application`、`Headless` 均用自己的值覆盖）。
- **做法**：从 `config.json` 删除 `map.file`、`map.width`、`map.height` 三个键；
  `Config::Map` 默认值保留在代码中（`kDefaultMapFile`、105/95），作为无菜单/无 options 的兜底。
- **涉及**：`data/config.json`、`src/core/Config.cpp`（`validateConfigKeys` 键表移除）、
  `.docs/配置说明.md`、`Config::toJson`（对称）。
- **风险**：低。无行为变化；`Headless` 生成随机密铺图仍用 `cfg.map.width/height` 默认值，不受影响。
- **验收**：`test_config.cpp` 往返测试更新；`ctest` 全绿；语义基线不变。

### 1.2 城市形状/图标缩放统一（原文 2，数据与代码两部分）——✅ 已完成

**数据部分**（不碰模拟行为）：
- `config.json` 的 `city.shapes`（仅服务方形 `{level,w,h}`）移到 `data/city_shapes.json`，
  并用**泛化格式**（`cells = [dx,dy]` 世界偏移 + `levels` + `shapeLevelIndex`）重写；
  `square/hex/tri` 三个密铺也一并写进 `city_shapes.json`（当前该文件只有 10 种半正/Laves）。
- 删除 `config.json` 的 `render.city.iconScale`（仅方形使用，`CityRenderer.cpp:321`）。
  方形改为像其他密铺一样走预计算的 `iconFitScale[tilingName][texLevel]`（`data/city_icon_fits.json` 补充 square 条目）；
  缺失时按 1 倍缩放（见 Phase 3 的 1.3，原文 11）。

**代码部分**：
- `Config::City::square/hex/tri` 从 `initDefaultCity` 内置表改为读 `city_shapes.json`
  （扩展 `Config::loadCityShapes`，当前只填 `sets[]`，需把 square/hex/tri 也写入具名成员）。
- 统一 `Map::shapeCells` / `placeableVariants` 的方形分支（`Map.cpp:645-662,856-866`）
  与六/三/半正/Laves 的世界偏移路径：方形 `cells` 改存世界偏移后走统一解析，
  删除方形专用 `(ax+dx, ay+dy)` 整数路径。
- 同步 `tools/gen_city_shapes_json.py` 生成 square/hex/tri 段。

**涉及**：`data/config.json`、`data/city_shapes.json`、`data/city_icon_fits.json`、
`src/core/Config.h/cpp`、`src/world/Map.cpp`、`src/render/CityRenderer.cpp`、`tools/*`、`tests/test_city.cpp`。
**风险**：中（形状表加载路径变更）。**注意**：square 形状从 `{level,w,h}` 转世界偏移，
  `recomputeCityGeometry` 的方形 `w/h` 推导逻辑需保留。只要城市放置、占领和兵运动结果不变，
  config 序列化变化不会导致新的语义基线漂移。
**基线影响**：否（按当前实现的行为等价性判断；完整快照 hash 即使变化也不代表语义基线必须变化）。
**验收**：`test_city.cpp`、`test_tiling_table.cpp` 全绿；`Determinism.Baseline*` 语义基线不变。

### 1.3 禁用"缩放倍率找不到时计算生成"（原文 11）——✅ 已完成

- **现状**：`CityRenderer::compute` 曾在 `iconFitScale` 缺条目时实时拟合；`iconFitOffsetY`
  缺条目时无偏移。
- **做法**：已删掉实时拟合分支，缺失时直接按基建地块 AABB 使用 **1 倍缩放**，并用
  `spdlog::warn` 记录 `tiling/texLevel`；`iconFitOffsetY` 缺失时默认 0 并告警。每个渲染器实例对
  同一缺失组合只告警一次，避免每帧刷日志。
- **涉及**：`src/render/CityRenderer.h/cpp`（保留 `CityIconFitter` 给离线调参工具和单测使用）。
- **风险**：低（纯渲染）。**验收**：`test_city_render.cpp` 的 1 倍兜底用例、相关单测全绿。

### 1.4 长内容外置（原文 4）——✅ 已完成

- **现状**：`config.json` 单文件承载 render/tech/factions/units 等长段。
- **做法**：拆分为多个数据文件，由 `Config::loadFromFile` 按约定子文件加载合并：
  `data/config.json`（核心/模拟）+ `data/render.json` + `data/techs.json` + `data/factions.json`
  + `data/units.json`。加载顺序：先核心再子文件（子文件覆盖），`toJson` 仍输出单对象
  （序列化/快照不变，语义基线不因文件拆分改变——`Config::toJson` 结果不变）。子文件使用带段名的
  根对象，如 `render.json` 为 `{ "render": { ... } }`；缺失子文件时保留核心文件值并告警。
- **涉及**：`src/core/Config.cpp`（`loadFromFile` 合并 + 合并后统一 `validateConfigKeys`）、
  `src/core/Paths.h`（新增子文件路径常量）、`data/*.json`、`.docs/配置说明.md`。
- **风险**：中低（纯数据装配；注意快照 `config` 序列化仍走 `toJson`，键名/默认值不变）。
- **验收**：`test_config.cpp` 往返 + `ctest` 全绿 + 语义基线不变。

---

## Phase 2 — 密铺代码统一与比例映射框架（原则上不改行为）

### 2.1 正方/三角/六边形与 arch/laves 密铺代码统一（原文 2 的代码部分，承接 1.2）——✅ 已完成

统一 `Tiling.h` 语义：正方形也使用周期域参数（`B=1, s=t=1`）。`cellCount`、
`cellIndexAt`、`indexToRowCol` 统一按 `baseCount()` 布局；square 补齐多边形、边和
`crossEdge` 接口；`Map::cellIndexAt` 与 `SpatialHash` 均委托 `TilingGeom`，减少重复特判。
方形移动仍保留 `findNextXY` 专用路径，因为它承载既有边界反射和 RNG 顺序；共用的格进入处理
仍由 `processEnteredCell` 提供。**注意**：方形是性能最关键路径，统一接口不能引入虚调用。

**基线影响**：原则上否。只有当统一实现改变格定位、边穿越、碰撞结果、执行顺序或其它模拟结果，
导致 `belongi` 或兵状态变化时，才更新语义基线；单纯等价重构不更新。
**验收结果**：`Tiling.*`、`TilingTable.*`、地图生成和完整 `ctest` 已通过，现有语义基线保持稳定。

### 2.2 地图尺寸比例映射框架扩展（原文 3）——✅ 已完成

- **做法**：`chooseTableDomain`/`tableDomainParams` 已覆盖全部密铺：
  - square：`B=1, s=t=1, p=q=1, Ra=Rb=1`，保持 `cols=width, rows=height` 恒等映射；
  - hex：`B=1, s=t=1, p/q=13/14, Ra/Rb=14/13`，输出行数天然为偶数；
  - tri：`B=2, s=2, t=1, p/q=4/3, Ra/Rb=3/8`，有效 `Rb` 加倍以保证输出行数为偶数；
  - arch/laves 继续使用工具按几何规格离线搜索的参数；菜单步进/夹取已同步使用 hex/tri 的限制。
- `MapGenerator::normalizedParams` 与 `Map::configure` 均调用同一映射，保证生成文件头和加载几何一致。
- **涉及**：`src/world/tiling/Tiling.cpp/h`、`tools/gen_table_domain_params.py`、
  `src/ui/Menu.cpp`、`src/world/Map.cpp`。
- **风险**：中。hex/tri 的实际周期域尺寸发生变化，已按预期更新语义基线；生成/加载、总格数守恒、
  偶数行和菜单限制均已验证。
- **验收结果**：`test_tiling_table.cpp` 新增全密铺尺寸映射用例；比例参数脚本 `--verify` 与完整
  `ctest --preset default` 均通过。

**基线影响**：square 的恒等映射不改基线；hex/tri 若新比例参数改变实际地图尺寸或格布局，
则会改变 `belongi`，并可能进一步改变兵状态，需要更新语义基线。

---

## Phase 3 — 行为修复与调整（按需更新语义基线）

### 3.1 `bounceJitter` 简化（原文 10）——✅ 已完成

- **现状**：`MovementSystem.cpp:43-46` 已改为一次 `rng.unit()` 采样，config 只保留一个弧度半区间。
- **做法**：改为一个 config 键 `army.bounceJitterRangeRad`（弧度半区间，0.03），
  实现 `angle += (rng.unit()*2-1) * rangeRad`。删除 `bounceJitterHalfRange`/`bounceJitterDenominator`。
- **涉及**：`data/config.json`、`Config.h/cpp`、`MovementSystem.cpp`、`.docs/配置说明.md`。
- **风险**：低，但 `get(97)`→`unit()` 消耗 RNG 方式变 → **基线更新**。
- **验收结果**：配置、反弹分支单测和 `test_baseline` 已更新。

### 3.2 长宽"四舍五入"到最近倍数（原文 43）——✅ 已完成

- **现状**：`chooseTableDomain` 的 `snap`（`Tiling.cpp:1148-1153`）与 `Menu.cpp:345-352`
  均已改为四舍五入到 Ra/Rb 倍数。
- **做法**：`snap` 改为四舍五入到最近倍数（`(v + base/2)/base*base`），菜单逻辑同步；
  保留 clamp（32..200）.  hex/tri 偶行/偶列约束会被包含在"2.2 地图尺寸比例映射框架扩展"中。
- **涉及**：`src/world/tiling/Tiling.cpp`、`src/ui/Menu.cpp`、`tools/gen_table_domain_params.py`。
- **风险**：中（用户输入尺寸语义微调可能改变生成地图尺寸，进而改变格归属和兵状态）。对固定的
  120×120 基线参数，若四舍五入前后仍得到相同尺寸，则该基线不变；否则更新语义基线。
- **验收结果**：`test_tiling_table.cpp` 补充并通过"最近倍数"用例；比例工具同步使用相同公式。

### 3.3 产兵方向改为"势力级：首随机 + 后续逆时针递增"（原文 44）——✅ 已完成

> 语义澄清：产兵角是**势力级单一状态**（不是每城独立），因此"切换产兵城不重置"是其自然结果。
> 该势力**首个**兵随机取角；此后每产一兵，角度逆时针旋转 `army.spawnAngleStep`
>（config，默认 1.0 rad），取模 2π。

- **现状**：`SpawnSystem::spawnArmy` 每产一兵 `getRandomAngle`（`SpawnSystem.cpp:24`），
  纯随机、无状态。
- **做法**：
  - `Faction` 新增 `double spawnAngle` + `bool spawnAngleSet`（势力级角度状态；`initFromDef` 重置）。
  - 新局初始化阶段立即为 8 个玩家势力各取一次随机角度并置位，开局渲染即可显示箭头；首次产兵复用该角度。
  - 产兵角获取放 `spawnArmy` 内统一（覆盖普通产兵与势力8 免费兵两条产兵路径）：
    `spawnAngleSet==false` → `angle = getRandomAngle(rng)` 并置位；否则 `angle = spawnAngle`；
    随后 `spawnAngle = fmod(angle + army.spawnAngleStep, 2π)`。
  - 快照序列化 `spawnAngle`/`spawnAngleSet`（读档恢复角度序列）。
  - **RNG 影响**：仅首次产兵消耗 RNG，后续 0 消耗 → RNG 序列变；产兵方向和后续兵状态改变，
    → 语义基线更新。
- **渲染更新**：在产兵城, 以及鼠标接近一个城市(渲染了上下左右四个指示箭头时)时,城市图标处画小箭头指示"下一个产兵方向"。**使用 `data/arrow2.png`**
  可旋转箭头，旋转角 = 该势力当前 `spawnAngle`；箭头中心沿该方向前移
  `sqrt(城市基建地块总面积 / render.city.spawnArrow.areaDivisor) + 实际贴图长度/2`。颜色 = 势力主:副:黑 加权
  `(primary:0.6, secondary:0.1, black:0.3)`，分母也放入 `render.json`，如
  `render.city.spawnArrow`）。玩家模式画在选定产兵城（`PlayerIntent`）；AI 画在该势力将产兵城
  （least-recent）图标。渲染层不消耗 sim RNG。
- **涉及**：`src/world/Faction.h/cpp`、`src/sim/systems/SpawnSystem.cpp`、
  `src/core/Config.h/cpp`（`army.spawnAngleStep` + render 新键）、`data/config.json`、
  `data/arrow.png`（新资产）、`src/render/CityMarkerRenderer.cpp`/`CityRenderer.cpp`（箭头绘制）、
  `src/replay/Snapshot.cpp`、`.docs/配置说明.md`。
- **风险**：中高（产兵方向改变，且去掉每兵 RNG 会改变后续模拟序列 → **语义基线更新**；新增快照字段
  本身不构成基线变化）。
- **验收结果**：`test_army.cpp`/`test_snapshot.cpp` 已覆盖角度序列和快照往返；玩家选中/悬停城市与
  AI least-recent 城市均接入 `arrow2.png` 方向提示，语义基线已更新。

### 3.4 科技升级界面空格暂停 bug（原文 41）——✅ 已完成

- **现状**：`Application::applyInputs` 空格先切 `paused_`（可能解除暂停跑一帧），
  随后 `renderFrame` 才因 `researchPending` 强暂停（`Application.cpp:282-284`）。
- **做法**：`applyInputs` 处理 `togglePause` 前判断 `tech.researchPending`：待选期间忽略空格
  切换（或保持暂停）。空格事件仅用于正常游戏；暂停策略抽成 `PausePolicy` 并由输入单测覆盖。
- **涉及**：`src/app/Application.cpp`。
- **风险**：低（UI/交互，不影响模拟）。**验收结果**：`test_input.cpp` 已覆盖待选期间阻止暂停切换，
  并完成构建与完整回归测试。

### 3.5 ESC 键改为"暂停 + 退出确认面板"（原文 42）——✅ 已完成

- **现状**：ESC 双击=暂停（`InputManager.cpp:94-106`），单击不动作。
- **做法**：ESC 单击 → 暂停 + 弹出"是否退出到主菜单"面板（是/否）。选"是" →
  退出到主菜单（清空当前 `sim_`、回菜单屏）；选"否"或确认中再次按 ESC → 解除暂停、关闭面板、继续游戏。
  `InputManager` 将 ESC 翻译为独立动作，`Application` 通过 `exitConfirm_`/`returnToMenu_`
  管理确认状态并复用菜单循环。
- **涉及**：`src/app/InputManager.h/cpp`、`src/app/Application.cpp/h`、`src/ui/Menu.*`。
- **风险**：中（交互重构，涉及应用状态机）。**验收结果**：`test_input.cpp` 已覆盖 ESC
  独立动作及输入状态更新；构建和完整回归测试通过。

### 3.6 游戏整体速度 0.5 倍（原文 46）——✅ 已完成

- **做法**：不加全局倍速开关，按消耗点调整实际配置。移动/子弹/经济/科技每 tick
  产出减半；开火周期、子弹寿命、特效寿命、地雷等待/探测/超时、迁都等待加倍。
  爆炸新增 `effect.bomb.expansionRate=0.5`，使半径按时间半速扩散；激光
  `extendPerTick=1`、寿命加倍但长度不变；地雷半径和激光长度等空间范围不缩小。
  山地子弹寿命惩罚从 2 降为 1，以保持减速后射程口径。
- **涉及**：`data/config.json`、`.docs/配置说明.md`。
- **风险**：高（牵动模拟节奏 → **基线更新**；数值体验需用户验收）。本次作为独立改动保留，
  与其它行为改动分开，便于回退。
- **验收结果**：配置、特效/射弹/产兵/科技测试已同步；square/hex/tri 2500 tick 基线更新为
  `0xaf6854d02438053e` / `0xa7e25ff8f6eea39c` / `0x14dcfd4e063ad524`；完整回归通过。

### 3.7 主面板表格统计扩展（原文 47）——✅ 已完成

- **现状**：`DebugPanel::drawLeaderboard` 已有 6 列（名次/势力/领地/城市/兵力/科技）。
- **做法**：加 3 列——最高等级城、经济生产速度、科技生产速度。**三个数都不再每帧遍历/现算，
  全部缓存到 `Faction`**：
  - `Faction` 新增 `double maxCityLevel`：城市等级放置后不变、仅在**易主**时随 `cityIds`
    变化。`insertCity`/`removeCity` 目前签名只有 `cityId`、拿不到等级，故**扩展签名为
    `insertCity(int cityId, double level)` / `removeCity(int cityId, double level)`**（调用点
    `conquerIndex` 处 city 对象在作用域内，`Faction.cpp:116-117`）；insert →
    `max = std::max(max, level)`，remove 被移除的是当前最高者 → 对剩余 `cityIds` 重扫
    （易主低频、城市数少，开销可忽略）。`initFromDef` 重置为 0。
  - `Faction` 新增 `double economyRate`（本 tick 经济产出 = (普通领地 + 城市 + 首都) × 增益）：
    `EconomySystem::accumulate` 内算出后**先存 `faction.economyRate`，再 `economy += economyRate`**
    （产经济时复用该数，杜绝重复计算）。
  - `Faction` 新增 `double techRate`（本 tick 科技点 = Σcity.level × pointsPerCityLevel × 增益）：
    `TechSystem::update` 内算出后**先存 `faction.techRate`，再 `points += techRate`**
    （产科技时复用该数）。
  - `DebugPanel::drawLeaderboard` 三列直接读 `faction.maxCityLevel/economyRate/techRate`，
    口径与 `EconomySystem`/`TechSystem` 保持一致（每 tick 值；显示如需"每秒"可 ×tickRate）。
- **涉及**：`src/world/Faction.h/cpp`（3 个缓存字段 + `insertCity`/`removeCity` 签名扩展与维护）、
  `src/sim/systems/EconomySystem.cpp`、`src/sim/systems/TechSystem.cpp`、
  `src/ui/panels/LeaderboardPanel.cpp`、`src/ui/DebugPanel.cpp`、
  `src/replay/Snapshot.cpp`（新增字段序列化）、`.docs/配置说明.md`（无，纯内部字段）。
- **风险**：低（`economyRate/techRate` 每 tick 重算一次并复用，行为不变；`maxCityLevel`
  在 insert/remove 处维护，与城市易主路径一致）。**基线影响**：否。三个缓存字段即使进入快照
  序列化，也不属于语义基线；只有缓存被错误地反馈到产兵、移动或占领逻辑时才会改基线。
- **验收结果**：排行榜已拆为可移动、可隐藏的独立面板；增加最高城、经济/s、科技/s 三列，
  速率由每 tick 缓存按 `sim.tickRate` 换算。`Faction` 缓存由城市易主、经济和科技系统维护，
  快照已升至 v9 并保存缓存字段；相关单测已补齐。

---

## Phase 4 — 密铺接口与新 bug 修复（依赖链）——✅ 已完成

### 4.1 点相邻（共享顶点）查询接口（原文 37；**覆盖全部 17 种密铺，含 arch/laves**）

- **做法**：`TilingGeom` 新增 `int pointNeighborCount(int index)` 与
  `int pointNeighbor(int index, int k)`（返回与 index **共任一顶点**的格下标；含边邻，界外 -1；
  去重——同一邻格可能共享多个顶点，只算一次）。
  - 方 = 8 邻（4 边 + 4 对角）；六 = 6 边邻（六边形顶点仅 3 格共点 → 点邻=边邻）；
    三 = 依顶点价数。
  - **arch/laves（表驱动）**：`loadTable` 现有 `Edge` 建邻只按"边反向重合"
    （`Tiling.cpp:169-200`）；点相邻需**新增顶点邻接表**：遍历每基础格的每个顶点，在
    ±dr,±dc 平移域内找"该顶点与另一基础格某顶点重合"的邻格（含同域相邻格），
    存入 `TilingTable` 新的 `vertexNeighbors`（每基础格一条、去重排序，运行时 O(1) 查表）。
- **涉及**：`src/world/tiling/Tiling.h/cpp`（`TilingTable` 增加顶点邻接表 + 加载时预计算 +
  两个查询方法）。
- **风险**：中低（新增纯几何查询 + 加载期预计算，不影响现有路径；六=边邻已验证）。**基线影响**：否，
  只新增查询接口和预计算表，不接入模拟路径时不改变语义字段。**验收**：
  `test_tiling.cpp` 补各密铺点邻用例（尤其 arch/laves 顶点重合判定的边界/顶点贴点）。

### 4.2 山生成在与海点相邻的格子（原文 38）

- **现状**：`correctMountainCoast`（`Map.cpp:385-418`）只按**边邻**判定"邻海"，
  点相邻（共享顶点）的海格未排除 → 山贴海点。
- **做法**：用 4.1 的点相邻接口补充判定：山格若**点邻**有海 → 置普通陆（方保持原 8 邻语义）。
  同步 `MapGenerator.cpp` 的山资格判断（`generateSquare:242-264`、`generateTiled:470-498`）。
- **涉及**：`src/world/Map.cpp`、`src/world/MapGenerator.cpp`。
- **风险**：中（地图生成和山地通行条件变化，可能改变后续 `belongi` 与兵状态 → **语义基线更新**）。
  **验收**：`test_mapgen.cpp`/`test_mountain.cpp`，并更新 `Determinism.Baseline*`。

### 4.3 格中心语义与内切圆中心（原文 39/40）

- **做法**：`TilingGeom` 新增 `cellArea(index)` 与 `cellIncenter(index)`。
  **对所有密铺统一：每格存储其内切圆圆心**（非 Laves 的直接等于质心）：
  - 方/六/正三角/arch（正多边形）：内切圆圆心 = 质心 = 现有 `cellCenter`（几何对称成立），
    `cellIncenter` 直接返回质心（等价于"存储质心"），无需额外数据。
  - Laves（非正多边形）：质心 ≠ 内切圆圆心，须按多边形计算真实内切圆圆心（法：角平分线
    交点或"各边带权顶点均值"迭代逼近）；面积用鞋带公式。数据在 `TilingTable` 惰性预计算
    每基础格一个，运行时 O(1)。
- **涉及**：`src/world/tiling/Tiling.h/cpp`、`tools/*`（如需生成内切圆辅助数据）。
- **风险**：低（新增查询，不动现有路径）。**基线影响**：否，内切圆/面积只供渲染使用时不改变语义字段。
  **验收**：`test_tiling.cpp`（方/六/三断言
  `cellIncenter == cellCenter`；Laves 断言内切圆圆心到各边距离一致）。

### 4.4 山地贴图按面积缩放 + 对准内切圆中心（原文 39/40 后半）

- **现状**：`MapRenderer::drawTiled` 山地贴图尺寸 = `cellPx`（`MapRenderer.cpp:171`），
  中心 = `cellCenter`（质心）。
- **做法**：初版按面积开平方缩放并使用 `cellIncenter`，但视觉回归发现 Laves 扁格排列不均、
  arch 大小格尺寸反差过强。当前渲染改回 `mountain.png` 原始四段线稿，按格面积平方根缩放
  非线宽内容，使用质心绘制并允许贴图超出格边；参数全部位于 `render.mountain`，运行时不消耗模拟 RNG。
- **涉及**：`src/render/MapRenderer.cpp`。
- **风险**：低（纯渲染）。**验收**：`test_map.cpp`/目测。

**Phase 4 验收结果**：新增接口覆盖方/六/三及 14 种 arch/laves，表驱动顶点邻接加载期预计算并去重；
山地生成与加载修正均采用点邻海；Laves 面积/内切圆中心缓存供几何使用；山地贴图采用质心、
限制面积缩放和格边安全上限。后续地形几何修正另更新山脉梯度与 forceCoast，相关语义基线已同步。

---

## Phase 5 — 性能优化（移动系统 + worldToCell，原文 13-36）

> 依据 `.docs/密铺性能优化分析.md`。目标：`worldToCell` 表驱动分支从
> O(B×9×n) 降到近 O(1)；移动系统（当前性能瓶颈）细化。**必须保持扫描序确定性**
>（b 升序 → r/c 邻域序），否则可能造成 RNG/回放漂移；即使语义基线暂时未变，也要检查完整快照。

### 5.0 非方/六/三密铺临时基线（性能优化前置，原文 13 配套）—— ✅ 已完成

- **结果**：全部 14 种 arch/laves 密铺均已完成优化前后语义 hash 逐位核对，结果记录在
  `.docs/Phase5性能实验记录.md` §5.0。临时基线测试代码已删除，避免进入永久测试目标；
  `tests/test_baseline.cpp` 保留方/六/三永久基线与常规确定性快测。
- **验收**：14 种 hash 一致，正式 `ctest` 全绿。

### 5.1 直线判定优先的解析优化（原文 15-33，不归一化直线方程）—— ✅ 已完成

- arch4.8.8 / laves4.8.8 / arch3.3.4.3.4 / laves3.3.4.3.4：按原文几何描述实现
  "先定正方形区域 → 再与 2~4 条直线判定"（每次判定是半平面符号，直线不归一化）。
- laves4.6.12 / laves3.12.12 / laves3.4.6.4 / laves3.6.3.6 / arch3.6.3.6：按原文
  矩形四分区 + 直线判定（3~5 次符号判定）。
- 以上全部在 `TilingTable` 预计算各基础格的**边法向+常量**（半平面表示），
  查询时零内存分配、无多边形重建。

### 5.2 网格法兜底（原文 31 与 34）—— ✅ 已完成

- arch/laves 3.3.3.3.6、arch 3.12.12、arch 3.4.6.4、arch 4.6.12：对每平移块内格用细密网格
  离线标记"完全包含在某格内"的小块；在线先查网格（命中且完全包含 → 直接返回），
  否则复用 5.1 的直线判定或现查询。
- **原文 34 的手动参数方案**：仅当几何描述确实难实现时才启用——在 `tiling_specs_*.json`
  增加可选 `cells[].halfPlanes`（每格按半平面列表描述），运行时统一走同一分支。
  **默认不引入**（几何已有确定推导），作为兜底文档化。

### 5.3 内存局部性与批量处理（原文 35）—— ⏭ 暂跳过

> 2026-08-30：根据 Phase 5 性能实验，当前 `SpatialHash::build` 与排序只占移动阶段很小比例，
> 且 5.1/5.2 已显著降低 `worldToCell` 成本。本项暂不改代码，实验数据和维护性评估见
> `.docs/Phase5性能实验记录.md`。后续仅在真实大规模战斗中 hash+sort 占移动阶段约 10% 以上时重启评估。

以下条目为保留的后续候选方案，当前不执行。

- `MovementSystem::moveArmyTiled`：把"逐兵 worldToCell/crossEdge"改为**按行批量**
  预取邻接/顶点数据；`SpatialHash::build` 一次遍历内完成桶索引（已是 id 降序一次遍历，
  `SpatialHash.cpp:41-51`），避免重复解格。
- 单元素/小环形"最近查询缓存"（方案 C）：兵位置逐 tick 连续，命中率极高（`TilingGeom`
  加一个小缓存，命中即返回；不改扫描序）。

### 5.4 移动系统底层优化（原文 36）—— ⏭ 暂不实施

> 2026-08-30：128 档后的 MovePerf 分项数据表明，`SpatialHash` 重建与排序约占移动主循环
> 2%–6%，当前收益不足以抵消增量维护、跨格移动和确定性回归的复杂度。结论详见
> `.docs/Phase5性能实验记录.md` §5.4。本项保留为后续候选，不改代码。

- 剖析基准先行：`test_move_perf.cpp`/`test_perf.cpp` 建各密铺空跑基线；
  优先优化 `crossEdge`（边求交复用半平面预计算）与 `worldToCell` 调用次数
  （方案 D：空间哈希增量维护，属可选，若 5.1-5.3 达标可不做）。

**风险**：高（改动面广）。**基线影响**：原则上否，但仅在新旧 `worldToCell`、`crossEdge` 和移动
结果完全一致时成立。**约束**：逐项完成即跑全量 `ctest` + 语义基线核对；扫描序、格定位、边穿越、
碰撞，或 RNG 消耗改变并导致语义结果变化时，须查实原因，并按实际结果更新语义基线或回退；仅
未纳入语义基线的状态变化则补做完整快照/回放回归。
**验收**：性能基准对比（每密铺 200×200 空跑 N tick 耗时）+ 全部 404+ 测试。

---

## Phase 6 — 文档与清理

### 6.1 项目代码文档（原文 8）

- 新建 `.docs/代码结构说明.md`：逐文件说明（src 分 core/render/sim/ui/world/replay/app），
  含职责、关键入口、依赖关系；data/ 与 tools/ 分组说明。

### 6.2 清理 `.docs` 与 `tools`（原文 45）—— ✅ 已完成归档

- 梳理 `.docs/`（现 16 个文件，含本清理清单）与 `tools/`（原 85 个文件）。
  规则：**确定无用 → 删除**；**不确定 → 移入 `rubbish/`**（仓库已有该目录）。
- 已归档 66 个一次性/探索性工具到 `rubbish/tools/`，4 个历史文档到 `rubbish/docs/`；
  清理清单见 `.docs/清理清单.md`。
- 旧格式兼容策略已写入 `.docs/工程规范.md` §8 与本文档 §0.3：
  **对旧功能做改动时，无特殊说明，一律默认不用兼容旧格式，该删的就删。**

### 6.3 记录城市放置说明（原文 1 的结论落档）

- 将 §0.1 表第 1 行结论写入 `.docs/开发计划.md` 或新文档，作为后续调参依据。

---

## 7. 建议执行顺序汇总（可并行项标注）

| 顺序 | 阶段 | 条目 | 改基线? |
|---|---|---|---|
| 1 | P1.1 | config.json map 清理 | 否 |
| 2 | P1.2 | 城市形状/iconScale 统一 | 否（语义字段不变） |
| 3 | P1.3 | 禁用实时拟合 | 否 |
| 4 | P1.4 | 长内容外置 | 否 |
| 5 | P2.1 | 密铺代码统一（方形并入框架） | 否（保持语义等价） |
| 6 | P2.2 | 比例映射扩展（square/hex/tri） | square 否；hex/tri 是 |
| 7 | P3.1 | bounceJitter 简化 | 是 |
| 8 | P3.2 | 长宽四舍五入 | 条件（固定 120×120 若尺寸不变则否） |
| 9 | P3.3 | 产兵方向 | 是 |
| 10 | P3.5 | ESC 退出面板 | 否 |
| 11 | P3.4 | 科技暂停 bug | 否 |
| 12 | P3.7 | 主面板统计 | 否（缓存/快照字段不计入） |
| 13 | P4.1 | 点相邻接口 | 否 |
| 14 | P4.2 | 山贴海点 bug | 是 |
| 15 | P4.3 | 内切圆/面积 | 否 |
| 16 | P4.4 | 山地贴图 | 否 |
| 17 | P3.6 | 0.5 倍速（独立提交） | 是 |
| 18 | P5.0 | 非方/六/三临时基线 | 否（测试新增） |
| 19 | P5 | 性能优化（多子步） | 条件（语义结果不变则否） |
| 20 | P6 | 文档/清理 | 否 |

> P3.6 与其它行为改动（P3.1-3.3、P4.2）互相独立，若已先完成其它行为改动并更新基线，
> 0.5 倍速单独再更新一次基线即可；两者可并行开发、按序提交。
