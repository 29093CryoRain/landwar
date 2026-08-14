# landwar — 领土战争（翻新版）

用 **SDL2 + entt（ECS）+ Dear ImGui** 重写的领土战争实时模拟游戏。8 个 AI 势力在 105×95 格网
（陆地/海洋/城市）上互相征战：产兵、移动、征服、战斗、激光/爆炸/地雷特效。逻辑与渲染完全
解耦，60Hz 固定步长、确定性可复现。
> 游戏概述见 `.docs/游戏概述.txt`。
> 翻新(已实现)见`.docs/翻新计划.md`
> 行为规范以 `.docs/开发计划.md` 。为准
> 设计思路见 `.docs/开发思路.txt`。
> **工程规范（编码约定）见 `.docs/工程规范.md`**——新代码先读它（路径/CMake/确定性/
> config 键/渲染器/UI 约定都在那里收敛）。
本 README 只讲怎么构建/运行/用。

## 构建

环境：MSYS2 **UCRT64**（依赖见 `.docs/environment-setup.md`：SDL2、SDL2_image、nlohmann_json、
spdlog、EnTT、gtest；ImGui 为本地 `_deps/imgui-src`）。编译器 g++ (MinGW UCRT64)。

```bash
cmake --preset default            # 配置（Ninja Debug；生成 build/）
cmake --build --preset default    # 编译 → build/landwar.exe
ctest --preset default            # 单元测试
```

> 两个 build 预设相关文件：`CMakePresets.json`（工具链路径）与 `构建命令.bat`（命令速查）。
> 若 CMake 找不到依赖，检查 `CMakePresets.json` 中 `CMAKE_PREFIX_PATH` 指向的 MSYS2 根。

## 运行

**从项目根运行**（`data/` 相对路径解析；运行期产物写 `userdata/`，见下）：

```bash
./landwar.exe                    # 窗口模式（默认随机种子，每次启动不同；`--seed N` 指定确定性）
```

> **数据目录约定（2026-08 工程改进）**：资产（只读，随源码分发）在 `data/`
> （`map_*.bmp`、`army*.png`、`config.json` 等）；运行期产物（可写、可丢弃、不进版本库）统一在
> `userdata/`：`userdata/options.json`（菜单选项，开始游戏时保存）、
> `userdata/maps/gen_*.bmp`（随机地图生成物，P6 确定性：同 seed+参数再生成同文件）、
> `userdata/screenshots/`（F12 / QA 截图）。程序启动时自动创建 `userdata/` 子目录。

### 窗口内操作

| 输入 | 行为 |
|---|---|
| 空格 / ESC 双击（500ms 内） | 暂停 / 继续 |
| `1`/`2`/`3`/`4` | 倍速 1x / 2x / 4x / 8x（**玩家操控时** 1-8 改为选兵种，倍速走 `+`/`-` 与面板） |
| `1`–`8` | **玩家模式**：选兵种（普通/先锋/开拓/激光/爆炸/地雷/手枪/霰弹） |
| `` ` `` | **玩家模式**：不生产（`selectedType=-1`，与图标栏最左空框等价） |
| `+` / `-` | 倍速档循环（0.25x ~ 8x，含 <1x 供测试） |
| `F6` | 步进一个逻辑帧后暂停（再按再跑） |
| 滚轮 | 缩放（锚定光标） |
| 右键拖拽（>6px） | 平移（2026-08-03：拖动屏幕改右键） |
| 左键单击 | 点选兵 / 格 / 己方城市（左侧面板显示详情） |
| `F5` | 重载配置（重建模拟，同种子重启，**破坏当前局面**） |
| `F7` | 按当前 config 重烘焙双色渲染色（`factions[i].color/secondary` + `render.tile`/`render.city.mix`；不重建模拟，⑫） |
| `F12` | 截图 `userdata/screenshots/screenshot_N.png` |
| 窗口关闭 | 退出（ESC 单击不退出） |

> 交互系统细节（坐标空间、ImGui 补丁、点选原理）见 `.docs/交互系统.md`。

> **玩家模式（P2）**：菜单里把某势力 AI 选为「玩家」后，游戏中左键点自己的城市选产兵城
> （鼠标接近己方城市显示四箭头指示，选中的城市有旋转环）；`1`-`6` 或面板图标栏选兵种、
> `` ` `` 不产；该势力只产所选兵种、只在所选城市产。信息栏显示玩家经济与选择摘要。
> **面板系统（P3）**：主面板（左侧，固定）的"面板"区列出各可移动面板的开关；可移动面板
> 可拖动、标题栏有"隐藏"按钮，位置/可见性随 `options.json` 持久化（重启读回）。
> **消息系统（P4）**："消息"面板显示重大事件（势力灭亡/统一等，带事件时间前缀，
> 仅势力名按势力色着色），消息持续留存、超量丢最旧、有消息自动弹出（可隐藏）。
> **地形基图（P5 改版）**：地图 BMP = RGB 概率权重基图，画图软件可直接编辑——任一分量
> `<32` → 海（确定性）；否则 → 陆；**山概率=ramp(R)、城概率=ramp(G)**（ramp(x)=(x-128)/127），
> B 闲置；每陆格掷骰定山/城（种子相关，海陆恒定）。山=陆地子集（可被占领/放城），移动规则
> （细节改进 2026-08）：下一格为山（不管当前格，含海→山/山→山）都掷进入/反弹（普通/先锋 0.5、
> 开拓 0.8）、进山减速（开拓不减速）、出山复原；下一格既为敌又为山时两骰独立计算。兵种重置：
> **先锋**碰敌方领土 60% 不反弹；**开拓**碰敌方领土连占上下左右 4 邻格、山地不减速且难被阻挡。
> 山/城用简笔画线条贴图（`data/mountain.png`/`city.png`，tint 管线按势力色烘焙；**LOD 多尺寸
> 128/64/32/16 预烘焙**，缩放小时按屏上格大小选档避免糊成一团）。现有地图已等效转换
> （`tools/gen_probability_maps.py`）。首都只落在可产城格（基图允许成城的格），不会在"必然无城"
> 的格子出现。调色板见 `.docs/配置说明.md`。
> **随机地图（P6）**：菜单「选择地图…」→「随机地图」填参数（长/宽/陆地占比/山/城密度/
> 强制边缘为海 + 地图种子）实时预览；**RNG 分离**——地图种子（`mapSeed`）驱动随机图生成与
> 所有地图的山/城骰子，主种子
> （`seed`）驱动首都与游戏后续，两套独立（CLI 可用 `--map-seed` 指定地图种子）。地图种子字段
> 在选图页可编辑（首开自动随机）。生成器 `world/MapGenerator` 用 value-noise fBm 造海拔→切海陆→
> 编码概率基图 BMP（生成物 `userdata/maps/gen_<seed>_<w>x<h>_...bmp`，2026-08 起入 userdata/）→ 走现有加载路径开局，确定性
> `(mapSeed, mainSeed, config, map, options)` 唯一决定整局。主面板同时显示地图种子与主种子。
> **增益系统（P7）**：势力特色统一为"增益"（`sim/Buff`）——`Config::Faction` 的修饰符字段
> （速度/下海/反弹/激光/地雷等）在 `initFromDef` 翻译为初始 buff（`source="faction"`），各系统
> 只读聚合面 `Faction.mods`（`FactionMods`，纯函数 `computeMods` 聚合、无 RNG）；绝对基数
> （costDiv/爆炸半径/地雷半径/免费兵概率）保留为定义读取。P7 为纯数值迁移，`state_hash` 不变。
> 未来科技/机制增益=追加 buff（P8）。
> **科技系统（P8，思路"科技细节"）**：科技点=城市产出（每 tick 每座 n 级城 n×`pointsPerCityLevel`
> 点，默认放慢 0.01），攒满**阈值**（初值/步进 1000，config 可调）→ 清点 + 获得一次科研机会。
> AI 势力到阈值随机选一个可升级科技；**玩家**有科研机会时游戏暂停 → **三选一**（升级项旧值黄、
> 新值绿，可跳过）。21 个首批科技（思路"开发时第一批做的科技"）：节流×8（造价降）/行军×6（速度升，
> 取消手枪/霰弹）/大爆炸（爆炸半径，地雷+炸弹偏好）/稳定激光/延长激光/高速射击（手枪兵攻击速度）/
> 弹幕（霰弹兵每次子弹数）/经济振兴/观星台（科技点）。叠加规则=增加加算、减小乘算、条数加和
> （`computeMods`）。排行榜加
> 「科技」列、玩家区科技进度条、科技面板（默认隐藏）、科技消息走事件通道。科技状态入快照（v5 向前兼容）。
> **射弹系统（P9）**：新增**手枪兵**（每 120 tick 向正前方发 1 颗恒速 0.5 格/tick 子弹，成本 80）与
> **霰弹兵**（每 180 tick 向正前方 40° 扇形均布+轻抖动发 3 颗随机速度 1/3±1/15（基准值
> 0.8~1.2 倍，2026-08 细节改进：原 ±1/6 方差过大）格/tick 子弹，
> 成本 120），初始即解锁。子弹穿海陆不减速、**不占领领土**、撞敌双亡、不能进山、超时消失。
> 行为抽象（P9 任务 0）：`DeathSystem` 读 `comp::Behavior`（死亡特效/周期动作来自 config），
> 新 `UnitActionSystem` 触发发射、`ProjectileSystem` 求解子弹。手枪/霰弹/子弹用 `army_base.png`
> 底部新贴图（行 9/10/8，势力色 tint，2026-08-07 用户提供）；**手枪/霰弹兵贴图随运动方向旋转**
> （枪管朝前，2026-08-07），子弹与其他兵不旋转；数字键 7/8 可选。P9 起系统按实体 id 确定性迭代
> → 存档续跑位精确。
> **地图边界贯通（P10）**：菜单选图页「地图边界贯通（环绕）」勾选（存 `options.json`，CLI 用
> `--wrap`）→ 兵碰地图边界**传送对侧**，速度大小方向不变（环绕消耗 0 RNG）。跨边界双渲染：
> 兵距边界 < 绘制尺寸时在 `x±宽/y±高` 副本位置再画一次（含角上对角）；相机 `clampPan` 保证视口
> 不越图界，`MapRenderer` 无需铺贴。兵点选对全部副本取最近。关闭时仍反弹（原行为回归）。
> 默认关闭，`--headless` 基线不变（`0x42d404e8ccc24bc4`；`--wrap` 新基线 `0xde929f83e5b024ff`）。
> **作用范围**：只做**兵**环绕——特效（激光/爆炸）与射弹（P9 子弹）环绕列 P12（子弹碰边界仍
> 销毁；跨边界近距战斗为已知轻微不完善）。快照不含 Options → wrap=ON 存档续跑重启时须同样开启。
> **UI 缩放默认 2.0**（可调 0.6~2.0，菜单与游戏面板共用）。

### 命令行

```
landwar [--headless] [--replay [SEED]] [--seed N] [--map-seed N] [--ticks N] [--speed X]
        [--config PATH] [--map PATH] [--save PATH] [--load PATH] [--summary]
        [--screenshot PATH] [--no-menu] [--wrap]
```

| 参数 | 说明 |
|---|---|
| `--headless` | 无头运行（不创建窗口） |
| `--replay [SEED]` | 回放：等价 `--headless --seed SEED` 重跑（模拟完全确定） |
| `--seed N` | 主随机种子（默认 42；驱动首都与后续一切） |
| `--map-seed N` | 地图随机种子（P6；默认=主种子；驱动随机图生成与山/城骰子，独立于主种子） |
| `--ticks N` | 逻辑帧数（默认 20000） |
| `--speed X` | 倍速（无头忽略；窗口模式渲染节奏） |
| `--config PATH` | config.json 路径（默认 `data/config.json`） |
| `--map PATH` | 覆盖地图文件 |
| `--save PATH` | 运行结束写存档快照 |
| `--load PATH` | 从存档继续（自带 config/map/rng，忽略 `--seed/--config/--map`） |
| `--summary` | 终局按势力打印 land/city/army/经济 + `state_hash` |
| `--screenshot PATH` | 窗口模式：跑到 tick=`--ticks` 时截图保存并退出（QA 钩子） |
| `--no-menu` | 窗口模式：跳过菜单直接开始（P1；`--load`/`--screenshot` 自动跳过菜单） |
| `--wrap` | 地图边界贯通（环绕，P10）：兵碰边界传送对侧，速度/方向不变（headless 用；窗口走菜单勾选，存 options.json） |

> **菜单（P1/P6）**：窗口模式默认先进入主菜单，可配置势力出场/产兵 AI；点「选择地图…」
> 进入**二级选图界面**——若干预装地图（`data/map_*.bmp`，**每张带预览缩略图**：按当前地图种子
> 掷出山/城后缩放的中立地形图，非原始概率 BMP）+ **随机地图**（填长/宽/海占比/山/城密度，
> 「生成并预览」实时看效果）；「地图随机种子」字段可编辑（首开自动随机，一套驱动预装图骰子与
> 随机图生成）。点「开始游戏」按选项构建模拟（随机图届时确定性重生成）并保存到 `userdata/options.json`
> （下次启动读取作为初始化选项）。**RNG 分离**：地图种子与主种子独立——游戏中主面板同时显示
> `地图种子` 与 `主种子`。`--load`/`--screenshot`/`--no-menu` 跳过菜单。

出现任何模拟控制参数（`--seed/--ticks/--speed/--map/--save/--load/--summary`）会隐含无头；
`--screenshot` 需要窗口/渲染器，强制窗口模式。

示例：

```bash
./landwar.exe --headless --seed 42 --ticks 20000 --summary   # 确定性回归基线
./landwar.exe --seed 7                                        # 指定 seed 的窗口局
./landwar.exe --screenshot shot.png --ticks 10000 --speed 1000  # 跑到 10000 tick 截图
./landwar.exe --save snap.bin --ticks 5000 --seed 1           # 存档
./landwar.exe --load snap.bin --summary                       # 读档续跑
```

## 目录结构

```
new_project_landwar/
  CMakeLists.txt / CMakePresets.json / 构建命令.bat
  data/                map_*.bmp（地形基图：RGB 概率权重，任一分量<32=海、山概率=ramp(R)、城概率=ramp(G)；map_bigIslands.bmp 基线、map_mountains.bmp 用户手绘山/城图）、legacy_old_encoding/（转换前的旧编码原图备份）、army.png（源精灵表）、army_base.png（基础兵表，双色合成 tint：红势力成品渲染 → 按各势力主副色反解重烘焙，⑫）、army_special.png（签名兵种特殊版，**双色系统起停用**）、ring.png/arrow.png（玩家标记美术图）、mountain.png/city.png（山/城简笔画线条贴图）、config.json
  userdata/            运行期产物（可写、可丢弃、不进版本库；启动自动创建）：
                       options.json（菜单选项，开始游戏时保存）、maps/gen_*.bmp（随机地图生成物）、
                       screenshots/（F12 / QA 钩子截图）、two_tone_template.png（双色反解模板调试图，⑫）
  src/
    core/              Config（含**未知键校验**，2026-08）、Simulation、MathUtil、Rng、GameDefs、
                       Paths（路径约定单点，2026-08）、FixedTimestep（固定步长累加器，2026-08）
    world/             Map、Faction、MapGenerator（P6 随机地图生成 → 概率基图 BMP）
    sim/               entt 组件 + ECS 系统（移动/战斗/特效/经济/产兵/死亡/生成）
                       Buff（P7 增益：BuffType/Buff/FactionMods/computeMods/initialFactionBuffs）
    sim/systems/       CombatSystem、MovementSystem、EffectSystem、EconomySystem、
                       ProductionSystem、DeathSystem、SpawnSystem、
                       UnitActionSystem（P9 周期发射）、ProjectileSystem（P9 子弹求解）
    render/            Renderer、SpriteSheet、Camera、Map/Army/Effect/Projectile/CityMarkerRenderer、
                       TintCache（读图→单色 tint→多版本存内存；含 LOD 选档）、
                       RendererUtil（渲染器公共辅助：剔除/精灵尺寸/颜色缩放，2026-08）、
                       MapPreview（P6 地形预览缩略图）、TintMath（纯逻辑可单测）
    app/               Application（主循环）、InputManager（输入翻译）
    ui/                ImGuiSetup（含中文字体）、DebugPanel（主面板）、Menu（P1/P6 菜单+二级选图页）、
                       UiScale（UI 缩放公式唯一入口，2026-08）、UiText（彩色文本统一，2026-08）
    ui/panels/         P3/P4 面板系统：Panel/PanelManager（多面板可隐藏/移动）、MessagePanel（消息）、MessageLog
    replay/            Cli、Headless、Snapshot（存档/回放）
    main.cpp           入口（无头 CLI 或窗口模式）
  tests/               单元测试（gtest，含黄金数据、确定性回归、性能验证）
  tools/               measure_visual_radius（视觉半径量法）、gen_golden_map.
  py（黄金数据）
```

## 架构概览

- **确定性模拟**：所有随机来自注入的 `Rng`（mt19937）；`(seed, config, map)` 唯一决定整局。
- **固定步长**：逻辑每 `1/60s` 一步（`sim.tickRate`）；渲染独立帧率，vsync 锁帧。
- **ECS**：兵/特效为 entt 实体；组件见 `src/sim/components.h`；每 tick 各系统按序 update。
- **渲染解耦**：渲染只读 sim 状态，不改 RNG/状态；`Camera` 提供缩放/平移/剔除。
- **统一 tint 管线**：`TintCache` 读入源图 → 按势力色烘焙多版本存内存，两种模式——
  **Multiply**（白/灰源→目标色，玩家标记 `ring.png`/`arrow.png`、P5 山/城线条贴图
  `mountain.png`/`city.png` 用）与 **HueRotate**（彩色源按色相旋转到目标色，精确复现原图
  "色相处理"，白像素自动不动→激光白芯保留）。军队精灵用 `army_base.png`（源列0 红，
  HueRotate）+ `army_special.png`（签名兵种特殊版原样彩色，`kSpecialUnitFaction` 决定谁用）。
  纯渲染层，不影响模拟确定性。
- **存档/回放**：`Snapshot` 全量序列化（实体 id 保持）→ `--save/--load/--replay`。
- 历史设计/验收记录见 `.docs/翻新计划.md`（各 Phase 拆解、§2 行为规格、风险清单）。

## 配置手册

所有魔法数字外置在 `data/config.json`（缺键回退内置默认）。**每个键的含义与出处**见
`.docs/配置说明.md`。运行中按 `F5` 热加载（重建模拟）。
**未知键校验（2026-08）**：打错的键名会在启动/重载时记警告（不再静默失效），详见
`.docs/工程规范.md` §4。
**双色势力渲染（视觉工程改进 ⑫）**：数据源并入 config —— `factions[i].color`=主色、
`factions[i].secondary`=副色（旧势力默认浅灰 191；中立 id0 = 深灰 96 + 浅灰）、
`render.tile`=地块格 主:副:白、`render.city.mix`=城市图标 主:副:黑 混合比例；
改值后按 **F7** 重烘焙肉眼评美（不重建模拟）；键说明见 `.docs/配置说明.md`。

## 测试

`ctest --preset default` 全绿。覆盖：数学工具逐分支、地图黄金数据、移动/战斗/特效/经济/
产兵、存档回放往返、输入翻译、相机、确定性回归（20000 tick 摘要哈希）、性能验证
（2500 兵吞吐 ≥ 60Hz + 空间哈希剪枝）。
