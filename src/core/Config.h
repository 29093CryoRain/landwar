// Config.h — 全部魔法数字外置（翻新计划 §3.5 data/config.json）。
// 默认值内置于本结构，JSON 仅作覆盖；缺键保持默认，保证无配置文件也能运行。
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
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
        // P12：密铺类型（square/hex/tri）。默认 square → 基线不变；非方形地图由随机图
        // 生成器产出（预装 BMP 恒为方形）。进快照（config 序列化 → 基线变更，已接受）。
        std::string tiling = "square";
        TilingType tilingType() const { return tilingFromName(tiling); }
    } map;

    struct Army {
        double baseSpeed = 0.3;
        double baseSize = 1.1;
        // 反弹角偏置 = (get(97)-bounceJitterHalfRange)/bounceJitterDenominator（±~0.0034 rad）。
        // 每 tick 抖动已取消（Phase 9）；出生方向为 0~2π 纯随机（思路.txt「随机方向」）。
        double bounceJitterHalfRange = 48.5;
        double bounceJitterDenominator = 14447.0;
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
    std::array<int, 3> factionCityColor(int id) const;

    struct Effect {
        struct Bomb {
            int lifetimeTicks = 8;     // tt>8 消亡
            int conquerEveryTicks = 2; // tt%2==0 时占领/击杀
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
            int durationTicks = 22;        // 基础寿命（绿5 ×1.5 = 33）
            double length = 30.0;          // 基础长度（绿5 ×1.5 = 45）
            int extendPerTick = 2;         // 每 tick 至多增长
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
    // 形状格 = 相对锚格中心的**世界单位偏移**（ShapeCell{dx,dy,orient}，
    // orient：0=正/up、1=反/down；方/六恒 0）——parity-free，放置时经 worldToCell 解析。
    struct City {
        // 形状格（世界单位相对锚格中心的偏移 + 朝向）。
        struct ShapeCell {
            double dx = 0.0;
            double dy = 0.0;
            int orient = 0;  // 0 = 正三角（六边形/正方形恒 0）；1 = 反三角
        };
        struct Shape {
            int anchorN = 0;              // 锚点格边数（0=任意；半正/Laves 混合面时限定锚点格类型）
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
            int levelIndex(double level) const {
                constexpr double kTol = 1e-9;
                for (int i = 0; i < static_cast<int>(levels.size()); ++i)
                    if (std::fabs(levels[static_cast<size_t>(i)] - level) <= kTol) return i;
                return -1;
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
        // beta：等级幂律指数增量（>0，思路默认 0.5）。等级分布 P(L>=n) ∝ n^{-(alpha+beta)}。
        double levelRankExponent = 0.5;
        // 各密铺等级/形状表（默认值内置，JSON 覆盖；square 段兼容旧 {level,w,h} 格式）。
        // square/hex/tri 保留具名成员以兼容旧 JSON；14 种新密铺放 sets[枚举值]。
        TilingSet square;  // 默认 1/2/4/6/9：1×1 / 1×2 / 2×2 / 2×3 / 3×3
        TilingSet hex;     // 默认 1/3/4/6/7/9（P12 形状表，轴向偏移在 Config.cpp 转世界偏移）
        TilingSet tri;     // 默认 1/2/4/6/8（P12 形状表，世界偏移）
        std::array<TilingSet, kTilingTypeCount> sets;  // 半正/Laves 的等级形状表（默认空）
        // 默认构造：填充三密铺内置等级/形状表（裸 City{} 即可用，Map::cityConfig_ 等）。
        City();
        // 幂律总指数 s = alpha + beta（P(L>=n) = n^-s）。纯函数。
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
        int maxArmyGuard = 50000;  // 防跑飞保险，非实际瓶颈
    } sim;

    struct Render {
        int windowWidth = 2175;
        int windowHeight = 1425;
        int spriteSize = 32;
        int armyDrawSize = 48;
        // 双色势力渲染混合比例（视觉工程改进 ⑫；JSON: render.tile）：
        // 地块格填色 = 主色:副色:白 加权平均（主副比例接近，默认 0.35:0.35:0.30）。
        struct TileMix {
            double primary = 0.35;
            double secondary = 0.35;
            double white = 0.30;
        } tileMix;
        // P13 城市渲染常数（render/CityRenderer）：细线围区阈值 / 图标最小尺寸 / 等级图标缩放表 /
        // 细线势力色加深系数。全部外置，mock 相机/缩放可测。
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
            // 等级图标缩放系数表（9 项，下标 = 等级-1，与 data/tower/tower<N>.png 一一对应）。
            // 图标尺寸按"基建地块框"自适应（2026-08-07 反馈）：框 = 形状 w×h 个格子
            //（宽 boxW = w×cellPx、高 boxH = h×cellPx）；保留源纵横比（A=源高/源宽）下能放进
            // 框的最大宽 fitW = min(boxW, boxH/A)；图标宽 = max(minIconSizePx,
            // round(fitW×iconScale[level-1]))，图标高 = round(宽×A)。表值 = 每级"框填充比例"
            //（0~1，<1 → 略小于框，紧贴框边缘不好看；当前默认 0.85）。
            // 当前仅等级 1/2/4/6/9 出城，3/5/7/8 为占位（形状表扩展时直接生效）。
            std::array<double, 9> iconScale = {0.85, 0.85, 0.85, 0.85, 0.85, 0.85, 0.85, 0.85, 0.85};
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
            // 图标中心 = 基建地块几何中心（2026-08-07：移除 offsetY 错开偏移，见 P13 后续修订）。
        } city;

        // P15 首都渲染常数（render/CityRenderer）：首都图标/更粗细线/候补虚化。
        // 放大/缩小行为同普通城市（复用 city.lineMinCellPx 高缩放阈值）。
        struct Capital {
            // 低缩放**正式首都/候补**图标保底最小宽（逻辑像素）：首都不随缩放继续缩小到该值以下
            //（普通城市无此下限、随缩放继续缩小）。2026-08-08 用户定夺：使缩到最小时首都渲染得
            // 比之前更大（原 city.minIconSizePx=12 → 本值 20，数值待实验）。
            int minIconSizePx = 20;
            // 首都图标按等级缩放表（9 项，下标 = 等级-1）。语义同 city.iconScale：
            // 图标尺寸按基建地块框自适应（fitW × iconScale），占位 1.0 = 填满框，待实验。
            std::array<double, 9> iconScale = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
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
    // 从文件解析；文件缺失/解析失败时回退默认并记警告。
    static Config loadFromFile(const std::string& path);

    // 序列化完整配置为 JSON（存档/回放用，Phase 6）。
    // 与 loadFromJson 键一一对称，保证 loadFromJson(toJson(cfg)) == cfg（往返无损）。
    std::string toJson() const;
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
