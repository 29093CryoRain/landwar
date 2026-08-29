// Config.h — 全部魔法数字外置（翻新计划 §3.5 data/config.json）。
// 默认值内置于本结构，JSON 仅作覆盖；缺键保持默认，保证无配置文件也能运行。
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/GameDefs.h"
#include "core/Paths.h"

namespace lw {

// 三分量加权平均（双色势力派生色共用，2026-08 视觉工程改进 ⑫）：
// 逐通道 round(a·wa + b·wb + c·wc)，结果夹到 [0,255]。
inline std::array<int, 3> weightedMix3(const std::array<int, 3>& a, const std::array<int, 3>& b,
                                       const std::array<int, 3>& c, double wa, double wb,
                                       double wc) {
    std::array<int, 3> out{};
    for (int i = 0; i < 3; ++i) {
        const double v = static_cast<double>(a[static_cast<size_t>(i)]) * wa
                         + static_cast<double>(b[static_cast<size_t>(i)]) * wb
                         + static_cast<double>(c[static_cast<size_t>(i)]) * wc;
        out[static_cast<size_t>(i)] = v < 0.0 ? 0 : (v > 255.0 ? 255 : static_cast<int>(v + 0.5));
    }
    return out;
}

struct Config {
    struct Map {
        std::string file = kDefaultMapFile;  // 基线地图（2026-08-02 用户定夺；资产在 data/）
        int width = 105;
        int height = 95;
        int blockSize = 15;
        int panelWidth = 600;
        int capitalMinDistance = 28;
        // 随机图生成：山地格城市权重（普通陆地=1.0，山地×此值；2026-08 从硬编码移入）。
        double cityMountainWeight = 0.3;
        // P12：密铺类型（square/hex/tri）。默认 square → 基线不变；非方形地图由随机图
        // 生成器产出（预装 BMP 恒为方形）。进快照（config 序列化 → 基线变更，已接受）。
        std::string tiling = "square";
        TilingType tilingType() const { return tilingFromName(tiling); }
    } map;

    struct Army {
        double baseSpeed = 0.3;
        double baseSize = 1.1;
        // 反弹角偏置的弧度半区间；每次反弹取一次 unit()，范围为 [-range, range)。
        double bounceJitterRangeRad = 0.03;
        // 首次产兵方向随机，之后每次按此弧度逆时针递增。
        double spawnAngleStep = 1.0;
    } army;

    struct Sea {
        double initialGoSeaProbability = 1.0;
        // P14：timeToUnify 已移除——原仅被旧经济魔法公式引用；goSeaIncrease 为独立常量，不再由它推导。
        double goSeaIncrease = 1.7 / 20000.0;   // 下海概率每 tick 增量 = 1.7/20000
        double goSeaChanceConst = 110.0;        // 下海概率 = (ttime-last+110)*p/17700
        double goSeaChanceDenominator = 17700.0;
        // 2026-08：cyanSeaMult 已删除（旧死键）——势力3 下海概率 ×0.666 统一由
        // factions[3].seaMult 承担（见 initDefaultFactions 与 config.json）。
        // 海陆速度统一（Phase 9）：下海 speed ×seaSpeedMult，登陆 /seaSpeedMult（互逆）。
        double seaSpeedMult = 0.5;
    } sea;

    // 地形基图（P5 改版）：RGB 概率权重编码 + 移动规则。
    // 基图编码（24bit BMP，每像素 BGR，画图软件可直接编辑）：
    //   海/陆确定性：任一分量 < seaChannelMin → 海；否则 → 陆（不掷骰）。
    //   山概率 = ramp(R)、城概率 = ramp(G)、B 闲置（仍参与海陆判定：b<32 → 海）。
    //   ramp(x) = clamp((x - probFloor) / probScale, 0, 1)。
    // 每陆地格独立掷骰：先 rng.chance(p_city) 后 rng.chance(p_mtn)（各消耗一次，固定顺序）。
    // 山是陆地子集：land=true 且 mountain=true；山与城可同格共存。调色板见 .docs/配置说明.md。
    struct Terrain {
        int seaChannelMin = 32;  // 任一分量 < 32 → 海（确定性）
        int probFloor = 128;     // 概率分量 < 128 → 概率 0
        int probScale = 127;     // ramp = (x-128)/127（r=255/g=255 → 1.0）
        double mountainEnterChance = 0.5;  // 陆→山 成功进入概率（失败反弹）
        double mountainSpeedMult = 0.5;    // 山地内速度乘数（进入 ×、离开 /，互逆）
    } terrain;

    // 兵种基础定义（下标 = ArmyType）。
    struct Unit {
        double cost = 1.0;        // 产兵经济成本
        double speedMult = 1.0;   // 速度乘数（先锋2、激光/爆炸/地雷0.6）
        double sizeMult = 1.0;    // 碰撞半径乘数（激光/爆炸1.8、地雷1.4）
        double bounceMult = 1.0;  // 撞敌方领土反弹概率乘数（先锋0.4=60% 不反弹；开拓1.0 恒反弹）
        // 视觉半径（逻辑像素，zoom=1 空间）：army.png 对应子图的目测视觉大小（略小于
        // 最远像素距离，用户目测校准）。点选兵用（Phase 8，按兵种外置表）。
        // 校准值（2026-08，用户目测）：普通12/先锋13/开拓15/激光20/爆炸21/地雷17。
        double visualRadius = 12.0;
        // 山地特性（细节改进，2026-08）：开拓兵进入概率更高（0.5×1.6=0.8 → 阻挡 20%）、
        // 在山地不减速（普通兵进山减速 ×mountainSpeedMult）。
        double mountainEnterMult = 1.0;  // 山地进入概率乘数（开拓 1.6）
        bool   mountainNoSlow = false;   // 山地中不减速（开拓 true）
        // P9 行为抽象：死亡特效与周期动作（spawnArmy 填入 comp::Behavior）。
        // deathEffect 与迁移前硬编码映射一致：laser→laser、bomb→bomb、mine→mine、其余→none。
        DeathEffect deathEffect = DeathEffect::none;  // 死亡时生成的特效
        PeriodicAction periodic = PeriodicAction::none;  // 周期动作（none=无）
        int periodTicks = 0;                            // 动作间隔（tick；periodic 非 none 时有效）
        // P9 射弹参数（periodic 非 none 的发射兵用）：
        double bulletSpeed = 0.0;        // 子弹基础速度（格/tick；手枪=恒定值，霰弹=随机中心）
        double bulletSpeedJitter = 0.0;  // 子弹速度随机半区间（霰弹 ±；手枪 0）
        int    bulletCount = 1;          // 每次齐射子弹数
        int    bulletLifespanTicks = 0;  // 子弹寿命（tick）
        double bulletSize = 0.3;         // 子弹碰撞/绘制半径（格）
        // P9（2026-08-07）散射：向正前方扇形齐射。全角 = bulletSpreadPIFrac·π
        //（霰弹 40° = 2π/9；手枪单发 → 0）。单颗在均布占位上加 ±(frac·半角) 抖动。
        double bulletSpreadPIFrac = 0.0;      // 散射总角（π 的分数；0 = 无散射）
        double bulletSpreadJitterFrac = 0.3;  // 角度抖动（半角的分数，均布上的轻度扰动）
    };
    std::array<Unit, 8> units = {{
        /* normal   */ {1.0, 1.0, 1.0, 1.0, 12.0, 1.0, false, DeathEffect::none,   PeriodicAction::none, 0, 0.0, 0.0, 1, 0,  0.3, 0.0,     0.3},
        /* vanguard */ {3.97, 2.0, 1.0, 0.4, 13.0, 1.0, false, DeathEffect::none,   PeriodicAction::none, 0, 0.0, 0.0, 1, 0,  0.3, 0.0,     0.3},
        /* pioneer  */ {4.03, 1.0, 1.0, 1.0, 15.0, 1.6, true,  DeathEffect::none,   PeriodicAction::none, 0, 0.0, 0.0, 1, 0,  0.3, 0.0,     0.3},
        /* laser    */ {17.0, 0.6, 1.8, 1.0, 20.0, 1.0, false, DeathEffect::laser,  PeriodicAction::none, 0, 0.0, 0.0, 1, 0,  0.3, 0.0,     0.3},
        /* bomb     */ {23.0, 0.6, 1.8, 1.0, 21.0, 1.0, false, DeathEffect::bomb,   PeriodicAction::none, 0, 0.0, 0.0, 1, 0,  0.3, 0.0,     0.3},
        /* mine     */ {13.0, 0.6, 1.4, 1.0, 17.0, 1.0, false, DeathEffect::mine,   PeriodicAction::none, 0, 0.0, 0.0, 1, 0,  0.3, 0.0,     0.3},
        /* pistol   */ {80.0, 0.5, 1.3, 1.0, 14.0, 1.0, false, DeathEffect::none,   PeriodicAction::firePistol,  120, 0.5,     0.0,     1, 90, 0.3, 0.0,     0.3},
        /* shotgun  */ {120.0, 0.5, 1.3, 1.0, 16.0, 1.0, false, DeathEffect::none,  PeriodicAction::fireShotgun, 180, 1.0/3.0, 0.2/3.0, 3, 60, 0.3, 2.0/9.0, 0.3},
    }};

    struct Faction {
        int id = 0;
        std::array<int, 3> color = {127, 127, 127};   // 主色（双色系统⑫：渲染色主源；旧单色语义保留）
        std::array<int, 3> secondary = {191, 191, 191};  // 副色（双色系统⑫：旧势力默认浅灰）
        // P11 改版（用户定夺）：势力特色**不再影响兵种价格**（原 costDiv 价格折扣已移除），
        // 改由【兵种偏好系数】unitPreference 驱动默认 AI 的产兵分配（见 ProductionAI）。
        // 值 = 各兵种偏好系数（默认 1.0；>1 → 默认 AI 在该兵种上花费更多）。未来与科技挂钩。
        std::array<double, kArmyTypeCount> unitPreference = {1, 1, 1, 1, 1, 1, 1, 1};
        double speedMultAll = 1.0;      // 势力3：全兵速 ×1.5
        double seaMult = 1.0;           // 势力3：下海概率 ×0.666
        double bounceMultAll = 1.0;     // 势力4：全兵反弹概率 ×0.6
        double pioneerSpeedMult = 1.0;  // 势力4：开拓速度 ×2
        int extraLaserBeams = 0;        // 势力5：额外激光条数 +2
        double laserDurationMult = 1.0; // 势力5：激光寿命 ×1.5
        double laserLengthMult = 1.0;   // 势力5：激光长度 ×1.5
        double bombRadius = 2.4;        // 势力6：爆炸半径覆盖（4.08）；其余用 effect.bomb.baseRadius
        double mineTriggerRadius = 1.1; // 势力7：地雷引爆爆炸半径覆盖（1.43）；其余用 effect.mine.triggerBombRadius
        double freeArmyChance = 0.0;    // 势力8：攻占城市免费产兵概率 0.6
    };
    std::vector<Faction> factions;  // 下标即 id（0..8）

    // ---- 双色势力渲染派生色（视觉工程改进 ⑫；纯函数，四舍五入）----
    // 主色 = factions[id].color、副色 = factions[id].secondary。
    // 地块格填色 = 主:副:白 加权平均（render.tileMix）；城市图标填色 = 主:副:黑 加权（render.city.mix）。
    // id 越界 → 中性灰。实现见 Config.cpp（含单测）。
    std::array<int, 3> factionTileColor(int id) const;
    // 地块格填色（±variation 渐变，2026-08 双色密铺分档）：t∈[0,1] 渐变位置（0=副色侧、
    // 1=主色侧、0.5=中点 = factionTileColor(id)）。arch/laves 密铺按格类型/朝向分档取不同 t。
    std::array<int, 3> factionTileColor(int id, double t) const;
    std::array<int, 3> factionCityColor(int id) const;
    std::array<int, 3> factionSpawnArrowColor(int id) const;

    struct Effect {
        struct Bomb {
            int lifetimeTicks = 8;     // tt>8 消亡（数据配置 16）
            int conquerEveryTicks = 2; // tt%2==0 时占领/击杀（数据配置 4）
            double expansionRate = 1.0; // 爆炸半径时间推进倍率
            double baseRadius = 2.4;   // 炸弹死亡爆炸基础半径（橙6 用势力表 bombRadius 覆盖 4.08）
        } bomb;
        struct Mine {
            double radius = 1.4;           // 静态半径
            int armTicks = 600;            // 10s 后进入可引爆状态
            int checkEveryTicks = 12;      // 每 12 tick 探测
            int timeoutTicks = 3600;       // 60s 超时自爆
            double triggerRadiusMult = 1.1; // 探测半径 = mineRadius*1.1 + army.size
            double triggerBombRadius = 1.1; // 引爆/超时自爆生成的爆炸特效半径（紫7 用势力表覆盖）
        } mine;
        struct Laser {
        int durationTicks = 22;        // 基础寿命（数据配置 44；绿5 ×1.5 = 66）
            double length = 30.0;          // 基础长度（绿5 ×1.5 = 45）
        int extendPerTick = 2;         // 每 tick 至多增长（数据配置 1）
            double killExtraRadius = 0.1;  // 击杀判定额外半径
            double beamSpreadPIFrac = 1.0 / 11.0;  // 绿5 三束的 ±π/11 夹角
        } laser;
    } effect;

    // P9 射弹全局参数（子弹绘制尺寸 + 山地留存惩罚）。
    struct Projectile {
        int drawSize = 32;                  // 子弹 sprite 边长（逻辑像素，zoom=1；= sprite 原生 32）
        int mountainLifespanPenalty = 2;    // 子弹在山地每 tick 额外扣减的寿命
    } projectile;

    // P14 经济重构（开发计划 P14，思路 9.3）：朴素逐 tick 收入模型。
    //   landIncome = perLandIncome × landCount；cityIncome = perLandIncome × cityBaseMult × Σ level^alpha
    //   （alpha = city.levelIncomeExponent）；capitalIncome = capitalBase + cityEconomyRate(正式首都)（P15 接入）。
    // 具体数值**不进开发文档、全部走 config、待实验**（思路 9.3 数值纪律）——此处给占位，验收后调参。
    struct Economy {
        double initialEconomy = 1.1;   // 初始库存（产兵启动资金；保留）
        double perLandIncome = 0.003;  // P14：普通领地每 tick 收益（待实验）
        double cityBaseMult = 8.0;     // P14：1 级城收入 = 该值 × 普通领地（待实验）
        double capitalBase = 0.0;      // P14/P15：正式首都城市收入加成（直接加数值；P15 接入，本阶段占位）
    } economy;

    // P13 城市系统（多格基建地块 + 等级 + 幂律分布，思路 9.1）。
    // P12 改版：等级形状表**按密铺**（正方形沿用 w×h 语义；六边形轴向偏移；
    // 三角形世界偏移 + 朝向）。等级集按密铺（方 {1,2,4,6,9}、六 {1,3,4,6,7,9}、
    // 三 {1,2,4,6,8}）；**性质：各形状格数恰 = 等级数**（单测锁定）。
    // 形状格 = 相对锚格中心的**世界单位偏移**（ShapeCell{dx,dy}）——parity-free，
    // 放置时经 worldToCell 解析。三角正/反朝向由锚格奇偶在运行时镜像 dy 处理
    //（Map::shapeCells）；半正/Laves 已烘焙世界帧、锚格朝向由数据保证，运行时不旋转。
    struct City {
        // 形状格（世界单位相对锚格中心的偏移）。
        struct ShapeCell {
            double dx = 0.0;
            double dy = 0.0;
        };
        struct Shape {
            // 锚点基础格限制位掩码（半正/Laves 表驱动；bit b = 允许 b 号基础格作锚）。
            // 0 = 不限制（方/六/三与未限制的形状）。用于"仅限朝向横平竖直的正方形"等规则。
            // JSON 中存为 "anchorBases": [组名或基础格编号的数组]；组名在 baseGroups 中定义
            //（几何完全一致的基础格归为一组）。运行时统一展开为本位掩码。
            std::uint32_t anchorBaseMask = 0;
            std::vector<ShapeCell> cells;  // cells[0] = 锚格（(0,0)）
        };
        // 一个密铺的等级集 + 形状表。levels 为去重等级；shapes 为全部形状变体
        // （同级多形状：Laves 部分等级有两种形状），shapeLevelIndex[s] 指向 levels。
        // 2026-08-16：levels 改 double——半正密铺城市等级 = 面积和（实数）；Laves 等级 = 格数
        // （整数，按 double 存）。levelIndex 用容差匹配（同一实数的 JSON 往返/采样浮点）。
        struct TilingSet {
            std::vector<double> levels;
            std::vector<Shape> shapes;
            std::vector<int> shapeLevelIndex;
            // 贴图等级（1..10，与 levels 等长；空 = 缺省按 lround(levels[i]) 推导）。
            // 特例：Arch31212 的 4 级城（实际等级 3.43）用 4 级贴图（文档"为了与 3 级区分"）、
            // Arch3636 新增 4 级城（实际 4.5）用 4 级贴图——实际等级≠四舍五入，需显式指定。
            std::vector<int> iconLevels;
            int levelIndex(double level) const {
                constexpr double kTol = 1e-9;
                for (int i = 0; i < static_cast<int>(levels.size()); ++i)
                    if (std::fabs(levels[static_cast<size_t>(i)] - level) <= kTol) return i;
                return -1;
            }
            // 实际等级 → 贴图等级（缺省 lround；超范围 clamp 1..10）。
            int iconLevelFor(double level) const {
                const int li = levelIndex(level);
                if (li < 0) return std::clamp(static_cast<int>(std::lround(level)), 1, 10);
                if (li < static_cast<int>(iconLevels.size()) && iconLevels[static_cast<size_t>(li)] > 0)
                    return iconLevels[static_cast<size_t>(li)];
                return std::clamp(static_cast<int>(std::lround(levels[static_cast<size_t>(li)])), 1, 10);
            }
            int firstShapeIndex(double level) const {
                const int li = levelIndex(level);
                if (li < 0) return -1;
                for (int s = 0; s < static_cast<int>(shapeLevelIndex.size()); ++s)
                    if (shapeLevelIndex[static_cast<size_t>(s)] == li) return s;
                return -1;
            }
            int variantCount(double level) const {
                const int li = levelIndex(level);
                if (li < 0) return 0;
                int n = 0;
                for (int s : shapeLevelIndex)
                    if (s == li) ++n;
                return n;
            }
            const Shape* shapeFor(double level, int variant = 0) const {
                const int s0 = firstShapeIndex(level);
                if (s0 < 0) return nullptr;
                const int s = s0 + variant;
                if (s < 0 || s >= static_cast<int>(shapes.size())) return nullptr;
                if (shapeLevelIndex[static_cast<size_t>(s)] != levelIndex(level)) return nullptr;
                return &shapes[static_cast<size_t>(s)];
            }
        };
        // alpha：n 级城经济收入 = n^alpha × 1 级城（>=1，数值待实验；P14 经济重构读取）。
        double levelIncomeExponent = 1.0;
        // gamma：等级幂律指数增量（>0，思路默认 0.5）。采样总指数 s = alpha + gamma。
        // 2026-08 起采样公式改为 .docs/临时文本.txt 的修正幂律：
        //   P(x) ∝ n_x * (x + beta)^{-s}
        // beta 按当前密铺最小等级动态取 (1 - minLevel)/2。
        double levelRankExponent = 0.5;
        // 各密铺等级/形状表（默认从 data/city_shapes.json 加载；代码内置表作无文件兜底。
        // P1.2 起 square/hex/tri/半正/Laves 的 cells 统一为世界偏移，config.json 的 city 段
        // 仍可覆盖。square/hex/tri 保留具名成员以兼容旧 JSON；14 种新密铺放 sets[枚举值]）。
        TilingSet square;  // 默认 1/2/4/6/9：1×1 / 1×2 / 2×2 / 2×3 / 3×3
        TilingSet hex;     // 默认 1/3/4/6/7/9（P12 形状表，世界偏移）
        TilingSet tri;     // 默认 1/2/4/6/8（P12 形状表，世界偏移）
        std::array<TilingSet, kTilingTypeCount> sets;  // 半正/Laves 的等级形状表（默认空）
        // 默认构造：填充全部密铺内置等级/形状表（裸 City{} 即可用，Map::cityConfig_ 等）。
        City();
        // 幂律总指数 s = alpha + gamma（采样用；经济 alpha 仍为 levelIncomeExponent）。纯函数。
        double rankExponent() const { return levelIncomeExponent + levelRankExponent; }
        const TilingSet& setFor(TilingType t) const {
            switch (t) {
                case TilingType::Square: return square;
                case TilingType::Hex: return hex;
                case TilingType::Tri: return tri;
                default: return sets[static_cast<size_t>(static_cast<int>(t))];
            }
        }
        // 等级 → 形状（默认第一变体；找不到返回 nullptr；等级为实数，按容差匹配）。纯函数。
        const Shape* shapeFor(TilingType t, double level) const {
            return setFor(t).shapeFor(level, 0);
        }
    } city;

    // P15 首都系统（思路 9.2）：迁都冷却等。数值待实验（只动 config 不动代码）。
    struct Capital {
        // 迁都冷却（tick）：首都沦陷后经此窗口方可正式迁都。= 迁都冷却秒数 × sim.tickRate。
        // 统一规范名 capital.relocationDelayTicks（计划与代码均不写死具体秒数）。
        double relocationDelayTicks = 3600.0;  // 占位 60s @60tick；待实验
    } capital;

    struct Sim {
        double tickRate = 60.0;
        int maxArmyGuard = 50000;  // 防跑飞保险，非实际瓶颈 //实际上现在远小于该数值时, 就卡得不能玩了.
    } sim;

    struct Render {
        int windowWidth = 2175;
        int windowHeight = 1425;
        int spriteSize = 32;
        int armyDrawSize = 48;
        // 山地线稿按原始 mountain.png 参数预烘焙；所有参数仅影响渲染，不消耗模拟 RNG。
        struct Mountain {
            int sourceWidth = 128;           // mountain.png 宽
            int sourceHeight = 128;          // mountain.png 高
            double strokeWidthPx = 6.0;      // mountain.png 线条中心线粗细
            double areaReference = 1.0;      // 面积缩放基准，scale=sqrt(cellArea/此值)
            double colorDarken = 0.08;       // 山线颜色 = 势力色 × 此比例
            std::vector<std::array<double, 4>> segments = {
                {0.0, 119.0, 47.0, 1.0},
                {47.0, 1.0, 92.3, 127.0},
                {79.0, 90.0, 98.0, 53.0},
                {98.0, 53.0, 127.0, 127.0}};
        } mountain;
        // 双色势力渲染混合比例（视觉工程改进 ⑫；JSON: render.tile）：
        // 地块格填色 = 主色:副色:白 加权平均（主副比例接近，默认 0.35:0.35:0.30）。
        // variation（2026-08 双色密铺分档）：arch/laves 密铺按格类型/朝向分档时，
        // 主/副权重围绕 (primary, secondary) 对称摆动的半宽——档位 t∈[0,1] 处权重 =
        //   primary - variation + 2·variation·t （主权重）、
        //   secondary + variation - 2·variation·t （副权重）、白权重恒 white。
        // 即颜色随档位在 [(P-variation, S+variation, W) → (P+variation, S-variation, W)]
        // 闭区间内渐变（t=0 副色侧 / t=0.5 中点 / t=1 主色侧）；t=0.5 时权重恰为无渐变值。
        struct TileMix {
            double primary = 0.35;
            double secondary = 0.35;
            double white = 0.30;
            double variation = 0.1;
        } tileMix;
        // P13 城市渲染常数（render/CityRenderer）：细线围区阈值 / 细线势力色加深系数。
        // 等级图标不再有全局 iconScale 表：所有密铺（含 square）统一走预计算
        // iconFitScale[tilingName][texLevel]；缺失时按 1 倍缩放兜底（P1.2/P1.3）。
        struct City {
            // 屏上格大小阈值（逻辑像素）：屏上格 > 此值 → 高缩放档（画细线围基建地块区域）。
            double lineMinCellPx = 20.0;
            // （2026-08-08：`minIconSizePx` 已移除——普通城市随缩放继续缩小（无下限），
            // 正式首都/候补的保底最小宽改由 `capital.minIconSizePx` 承担。）
            // 细线宽（逻辑像素；仅高缩放绘制基建地块外廓）。
            double lineThickness = 1.5;
            // 高缩放细线颜色加深系数（0-1）：细线颜色 = 势力色 × lineDarken（与图标同势力色处理，
            // 但加深以便与城市图标区分）。0 → 黑，1 → 势力色原色。
            double lineDarken = 0.7;
            // 预计算"贴图最大内接框宽"（世界单位，中心 = 基建地块几何中心，不平移）。
            // 键：tilingName -> 贴图等级(1..10) -> fitW。由交互工具人工调定并经
            // tools/apply_city_icon_fit_scales.py 取同级最小值后写入。
            // 渲染时 fitW = iconFitScale[tiling][texLevel] × cellPx；方形也使用此表，
            // 缺失时按 1 倍缩放兜底（不再乘以 iconScale）。
            std::unordered_map<std::string, std::unordered_map<int, double>> iconFitScale;
            // 贴图竖直平移（世界单位，正=向上；由交互工具逐条目人工调定）。
            // 每项 = 某贴图等级 + 某同级形状变体 + 某**锚基础格类**（baseGroups 一个数组）的偏移，
            // 同类内取均值（2026-08-26 去重：不再按单个 anchorB 展开）。渲染时 iconCy = centerY + value，
            // 命中条件 = iconLevel + variant 匹配 且 城市实际 anchorB ∈ bases。
            struct IconFitOffsetY {
                int iconLevel = 1;        // 贴图等级（1..10）
                int variant = 0;          // 同级形状变体下标（City::shapeVariant）
                std::vector<int> bases;   // 该锚基础格类的全部基础格（baseGroups 一个数组的值）
                double value = 0.0;       // 世界单位偏移（同类均值）
            };
            std::unordered_map<std::string, std::vector<IconFitOffsetY>> iconFitOffsetY;
            // 等级塔图标颜色加深系数（0-1）：图标色 = 势力色 × iconDarken（与玩家指示"城市显示
            // 亮度"×0.8 一致）。城市图标此前用满色势力色，在亮色陆地上对比弱 → 大城看不清
            //（2026-08-07 反馈），加深以提升对比度。0 → 黑，1 → 势力色原色。
            double iconDarken = 0.8;
            // 双色势力渲染（视觉工程改进 ⑫；JSON: render.city.mix）：城市图标填色 =
            // 主色:副色:黑 加权平均（主比例显著高，默认 0.60:0.20:0.20）。
            struct Mix {
                double primary = 0.60;
                double secondary = 0.20;
                double black = 0.20;
            } mix;
            // 产兵方向箭头颜色 = 主色:副色:黑加权平均（arrow2.png）。
            struct SpawnArrow {
                double primary = 0.60;
                double secondary = 0.10;
                double black = 0.30;
                double areaDivisor = 3.0;
            } spawnArrow;
            // 图标中心 = 基建地块几何中心（2026-08-07：移除 offsetY 错开偏移，见 P13 后续修订）。
        } city;

        // P15 首都渲染常数（render/CityRenderer）：首都图标/更粗细线/候补虚化。
        // 放大/缩小行为同普通城市（复用 city.lineMinCellPx 高缩放阈值）。
        struct Capital {
            // 低缩放**正式首都/候补**图标保底最小宽（逻辑像素）：首都不随缩放继续缩小到该值以下
            //（普通城市无此下限、随缩放继续缩小）。2026-08-08 用户定夺：使缩到最小时首都渲染得
            // 比之前更大（原 city.minIconSizePx=12 → 本值 20，数值待实验）。
            int minIconSizePx = 20;
            // 正式首都基建区细线宽（比普通城市粗；高缩放才画）。
            double lineThickness = 2.5;
            // 候补指定新都虚化首都图标透明度（0-1）。
            double designatedAlpha = 0.45;
        } capital;
    } render;

    // 玩家模式（P2）：产兵城选择/悬停指示圈。
    struct Player {
        double hoverCityRadius = 2.0;    // 悬停指示圈触发半径（格；含格本身）
        double markerAlpha = 0.85;       // 指示圈透明度（0-1）
        double markerRotateSpeed = 0.03; // 指示圈旋转角 / tick（rad）
        // 产兵城指示环（P13 后续修订，2026-08-07）：环外径（格）= max(w,h) + markerRingMargin，
        // 随城市基建地块大小自适应（圈住大小不一的城）；外径须 ≥ 对角 √(w²+h²) 才圈住全部
        // 基建格（当前最大形状 3×3 → 对角 4.24，margin≥1.24 即够；默认 1.5）。改值即"自由缩放"。
        double markerRingMargin = 1.5;
        // 悬停四箭头（P13 后续修订，2026-08-07）：箭头尖端半径（格）= max(w,h)/2 + markerArrowGap，
        // 间距可调，能指示大小不一的城（大城箭头外移、不埋进基建地块）。
        double markerArrowGap = 0.6;
    } player;

    // 消息面板（P4）：消息持续留存（不自动消失），仅超上限丢最旧。
    struct Ui {
        int messageMaxShown = 12;  // 面板同时展示上限（超量立即丢弃最旧）
    } ui;

    // P8 科技系统（思路"科技细节"节为准，覆盖计划 P8 的 pointsEveryTicks 模型）：
    //   科技点 = 城市产出（每 tick 每座 n 级城产 n × pointsPerCityLevel × mods.techGainMult 点）；
    //   阈值门槛：points >= threshold → 清空科技点 + 获得一次科研机会，threshold += thresholdStep。
    //   玩家 3 选 1（candidates 用游戏 Rng 采样）；AI 随机选一个可升级科技。
    //   节奏（用户定夺 2026-08-08 放慢默认）：pointsPerCityLevel 默认 0.01（占位待实验）。
    struct Tech {
        struct Level {  // 一级的效果（**累计幅度**；升级 = 用新 buff 替换旧 buff）
            BuffType type = BuffType::UnitSpeedMult;
            int param = -1;          // 兵种下标（-1 = 全兵；config 用兵种名或 "all"）
            double magnitude = 1.0;  // 累计乘数（条数型为条数）
        };
        struct TechDef {
            std::string id;     // 唯一 id（事件 data / 排序键）
            std::string name;   // 显示名（如"节流·普通兵"）
            std::string desc;   // 效果名（科研弹窗：如"造价"）
            std::vector<Level> levels;  // levels[i] = 第 i+1 级累计效果；maxLevel = size
            std::vector<int> preferenceUnits;  // 每级使默认 AI 这些兵种偏好 +preferencePerLevel
        };
        double thresholdBase = 1000.0;    // 初始阈值（到阈值清点 + 科研机会）
        double thresholdStep = 1000.0;    // 每次科研后阈值提升
        double pointsPerCityLevel = 0.01; // 每级城市每 tick 科技点系数（放慢默认，占位待实验）
        double preferencePerLevel = 0.5;  // 每级默认 AI 兵种偏好加成
        int playerCandidateCount = 3;     // 玩家候选科技数（思路"随机刷新出三个科技"）
        std::vector<TechDef> techs;
        // id → techs 下标（找不到返回 -1）。纯函数。
        int techIndex(const std::string& id) const {
            for (size_t i = 0; i < techs.size(); ++i)
                if (techs[i].id == id) return static_cast<int>(i);
            return -1;
        }
    } tech;

    // 从 JSON 文本解析（覆盖默认值，缺键保持默认）。
    static Config loadFromJson(const std::string& jsonText);
    // 从文件解析；核心文件同目录的 render.json/techs.json/factions.json/units.json
    // 按对应顶层段覆盖核心配置。文件缺失/解析失败时回退默认并记警告。
    static Config loadFromFile(const std::string& path);
    // 从外置 data/city_icon_fits.json 加载 render.city.iconFitScale/iconFitOffsetY（2026-08-26）。
    // 该两块不再读 config.json；缺省空（任何密铺缺 fit 时按 1 倍缩放兜底，P1.2/P1.3）。
    static void loadCityIconFits(Config& c);

    // 序列化完整配置为 JSON（存档/回放用，Phase 6）。
    // 与 loadFromJson 键一一对称，保证 loadFromJson(toJson(cfg)) == cfg（往返无损）。
    std::string toJson() const;

    // 校验已解析配置的类型派生值、数值域和跨字段约束。
    bool validate(std::string* err = nullptr) const;
};

// 单位实际山地进入概率 = 基础 × 兵种乘数（封顶 1.0）。细节改进：开拓兵更高（阻挡概率降低）。
inline double mountainEnterChanceFor(const Config& cfg, int unitType) {
    return std::min(1.0, cfg.terrain.mountainEnterChance
                             * cfg.units[static_cast<size_t>(unitType)].mountainEnterMult);
}

// 单位在山地中是否减速（细节改进：开拓兵不减速）。进山/出山/下海的复原都以此为准。
inline bool slowsInMountain(const Config& cfg, int unitType) {
    return !cfg.units[static_cast<size_t>(unitType)].mountainNoSlow;
}

}  // namespace lw
