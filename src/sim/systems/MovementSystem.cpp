#include "sim/systems/MovementSystem.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/MathUtil.h"
#include "core/Simulation.h"
#include "sim/systems/CombatSystem.h"

namespace lw {

namespace {

// Phase 9：反弹角偏置。每 tick 抖动已取消，改为反弹（地图墙/下海失败/征服反弹）时
// 添加一个随机小偏置（±~0.0034 rad），避免规则反射的对称性。
double bounceJitter(const MoveContext& ctx) {
    const auto& a = ctx.config.army;
    return (ctx.rng.get(97) - a.bounceJitterHalfRange) / a.bounceJitterDenominator;
}

// 反弹：按穿越方向翻角（水平 boundaryCode 0/2 → π-角；垂直 1/3 → -角）+ 随机小偏置。
void bounce(MoveContext& ctx, comp::Velocity& vel, int boundaryCode) {
    if (boundaryCode == 0 || boundaryCode == 2) vel.angle = kPi - vel.angle;
    if (boundaryCode == 1 || boundaryCode == 3) vel.angle = -vel.angle;
    vel.angle += bounceJitter(ctx);
}

// P12：沿边直线角（rad）反射方向角 + 随机小偏置（密铺反弹；与方形 bounce 语义一致）。
//   方：边 0/2（竖）线角 90° → π-θ；边 1/3（横）线角 0° → -θ（与 bounce 相同）。
//   六：边 k 线角 = k·60°+90°；三：边 k 线角 = {0°,60°,120°}[k]（正/反同线）。
void bounceLine(MoveContext& ctx, comp::Velocity& vel, double lineAngle) {
    vel.angle = 2.0 * lineAngle - vel.angle;
    vel.angle += bounceJitter(ctx);
}

double edgeLineAngle(const TilingGeom& g, int k) {
    switch (g.type) {
        case TilingType::Square: return (k == 0 || k == 2) ? kPi / 2.0 : 0.0;
        case TilingType::Hex: return kPi / 180.0 * (60.0 * static_cast<double>(k) + 90.0);
        case TilingType::Tri: {
            static const double kA[3] = {0.0, kPi / 3.0, 2.0 * kPi / 3.0};
            return kA[static_cast<size_t>(k % 3)];
        }
        default: return 0.0;  // TODO 半正/Laves：改为按 cellEdge 反算边线角（逐格）
    }
}

// 征服格（下标）归势力 factionId；开拓兵同时按规范序征服全部**边邻格**（P12：
// 与目标格共享公共边的相邻地块一次性征服——方 4（下/上/左/右，顺序同旧版）、六 6、三 3）。
// 固定顺序保证势力8 攻占城市免费兵 RNG 消耗可复现（方形与旧 conquerCell 逐格一致）。
void conquerCell(MoveContext& ctx, int cellIndex, int factionId, ArmyType type) {
    conquerAtIndex(ctx, cellIndex, factionId, static_cast<int>(type));
    if (type == ArmyType::pioneer) {
        const TilingGeom& g = ctx.map.geom();
        for (int k = 0; k < g.neighborCount(); ++k) {
            const int nb = g.neighbor(cellIndex, k);
            if (nb >= 0) conquerAtIndex(ctx, nb, factionId, static_cast<int>(type));
        }
    }
}

// 进入格处理（陆/海/山/征服；goalIdx 已定，goalCell = atIndex(goalIdx)）。方形与密铺共用：
// 反弹路径由调用方注入（方形 bounce(code) / 密铺 bounceLine(edge)）。修改
// vel/speed/remLength/onLand/mtn/hist；RNG 顺序与旧 moveArmy 逐位一致。
// 返回 true = 反弹（未实际进入目标格，调用方应保持原格跟踪）；false = 已进入。
template <typename Bounce>
bool processEnteredCell(MoveContext& ctx, int goalIdx, Bounce&& doBounce, comp::Velocity& vel,
                        comp::Speed& speed, double& remLength, comp::OnLand& onLand,
                        comp::MountainState& mtn, comp::FactionId& fid, comp::UnitType& unit,
                        comp::LandHistory& hist) {
    const auto& cfg = ctx.config;
    const MapCell* goalCell = &ctx.map.atIndex(goalIdx);
    if (!goalCell->land && onLand.value) {
        // 陆/山→海：山→海 先复原山地速度（若该兵种在山地减速），再走原下海路径。
        if (mtn.inMountain) {
            mtn.inMountain = false;
            if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                speed.value /= cfg.terrain.mountainSpeedMult;
                remLength /= cfg.terrain.mountainSpeedMult;
            }
        }
        // 陆→海：概率下海，失败反弹。
        double goSeaChance = (ctx.ttime - hist.lastLandTime + cfg.sea.goSeaChanceConst)
                             * ctx.goSeaProbability / cfg.sea.goSeaChanceDenominator;
        // P7：下海概率 × 势力增益（基线 seaChanceMult=1.0 → 恒等，行为不变）。
        goSeaChance *= ctx.factions[static_cast<size_t>(fid.value)].mods.seaChanceMult;
        if (ctx.rng.chance(goSeaChance)) {
            onLand.value = false;
            speed.value *= cfg.sea.seaSpeedMult;   // 下海 speed ×0.5（永久降为海速）
            remLength *= cfg.sea.seaSpeedMult;     // 剩余移动量按新海速折算
            // 2026-08 用户定夺：去掉原版紧随的 `break`——不再丢弃本 tick 剩余移动，下海后续走
            // （速度与剩余量同步减半，语义自洽；原版 `rem/=2; break` 里 `rem/=2` 因 break 成死代码）。
        } else {
            doBounce();
            return true;
        }
    } else if (goalCell->land && onLand.value) {
        // 陆→陆（含山）：目标非山时先复原离开山地；山地阻挡先判（失败→反弹，不进入不征服）。
        if (mtn.inMountain && !goalCell->mountain) {
            // 山→平地：离开山地，速度复原（若该兵种在山地减速；开拓不减速）。
            mtn.inMountain = false;
            if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                speed.value /= cfg.terrain.mountainSpeedMult;
                remLength /= cfg.terrain.mountainSpeedMult;
            }
        }
        // 山地骰：下一格为山（不管当前格是什么，含陆→山/海→山/山→山）都掷进入概率；
        // 失败→反弹，通过才进入山地（减速，开拓不减速）。
        bool canCross = true;
        if (goalCell->mountain) {
            if (!ctx.rng.chance(mountainEnterChanceFor(cfg, static_cast<int>(unit.type)))) {
                doBounce();
                return true;
            }
            if (!mtn.inMountain) {
                mtn.inMountain = true;
                if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                    speed.value *= cfg.terrain.mountainSpeedMult;
                    remLength *= cfg.terrain.mountainSpeedMult;
                }
            }
        }
        if (canCross) {
            // 攻占敌方/中立领地：先征服目标格（开拓连占边邻格），再掷敌方反弹——
            // 与山地骰【独立计算】：本格既为敌又为山时两骰各掷一次（非先锋 bounceMult=1.0
            // 敌方必然反弹；先锋 0.4 两骰都过才不反弹）。反弹是征服后的"弹回"（原版语义）。
            if (goalCell->belongi != fid.value) {
                conquerCell(ctx, goalIdx, fid.value, unit.type);
                // P7：反弹概率 × 势力增益（基线 bounceChanceMult=1.0 → 恒等，行为不变）。
                double reboundChance =
                    cfg.units[static_cast<int>(unit.type)].bounceMult
                    * ctx.factions[static_cast<size_t>(fid.value)].mods.bounceChanceMult;
                if (ctx.rng.chance(reboundChance)) {
                    doBounce();
                    return true;
                }
            }
        }
    } else if (goalCell->land && !onLand.value) {
        // 海→陆 登陆：目标为山先掷进入概率（失败→反弹留海）；通过后登陆 + 复原陆速
        // + 进山（若减速）+ 征服（开拓连占邻格）。
        if (goalCell->mountain
            && !ctx.rng.chance(mountainEnterChanceFor(cfg, static_cast<int>(unit.type)))) {
            doBounce();
            return true;
        }
        onLand.value = true;
        speed.value /= cfg.sea.seaSpeedMult;   // 复原陆速（=×2）
        remLength /= cfg.sea.seaSpeedMult;
        hist.lastLandTime = ctx.ttime;
        if (goalCell->mountain) {
            mtn.inMountain = true;
            if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                speed.value *= cfg.terrain.mountainSpeedMult;
                remLength *= cfg.terrain.mountainSpeedMult;
            }
        }
        conquerCell(ctx, goalIdx, fid.value, unit.type);
    }
    return false;
}

// 位置夹到世界范围内（密铺用；含贴界内缩，保证 worldToCell 可解析）。
void clampWorld(comp::Position& pos, double ww, double wh) {
    pos.x = std::clamp(pos.x, 0.0, ww);
    pos.y = std::clamp(pos.y, 0.0, wh);
}

// 位置按世界周期环绕（[0, period)）。
void wrapPos(comp::Position& pos, bool offX, bool offY, double ww, double wh, int nc, int nr) {
    if (offX) pos.x += (nc < 0 ? ww : -ww);
    if (offY) pos.y += (nr < 0 ? wh : -wh);
}

// 方形路径：原版 moveArmy 主体（findNextXY 轴对齐格线穿越 + 边界 bounce/wrap）。
static void moveArmySquare(MoveContext& ctx, entt::entity e, comp::Position& pos,
                           comp::Velocity& vel, comp::Speed& speed, comp::OnLand& onLand,
                           comp::MountainState& mtn, comp::Collider& col, comp::FactionId& fid,
                           comp::UnitType& unit, comp::LandHistory& hist) {
    const auto& cfg = ctx.config;
    const auto& map = ctx.map;
    const int w = map.width();

    double remLength = speed.value;
    while (remLength > 0) {
        const int boundaryCode = math::findNextXY(pos.x, pos.y, vel.angle, remLength, ctx.rng);
        // 战斗检查：每跨一格一次（§2.4）。命中则双方标死，本兵本 tick 移动中止。
        if (CombatSystem::checkAt(ctx, e, pos, col, fid)) return;

        if (boundaryCode == -3) continue;  // 撞角：findNextXY 内已反向（+π+微偏置），继续本 tick 移动
        if (boundaryCode < 0 || boundaryCode > 3) {
            // -2 走完本段：夹取到界内 + 结束点补一次征服，返回。
            if (pos.x < 0) pos.x = kEps;
            if (pos.x > map.width()) pos.x = map.width() - kEps;
            if (pos.y < 0) pos.y = kEps;
            if (pos.y > map.height()) pos.y = map.height() - kEps;
            if (map.at(static_cast<int>(pos.x), static_cast<int>(pos.y)).belongi != fid.value) {
                conquerAt(ctx, static_cast<int>(pos.x), static_cast<int>(pos.y), fid.value,
                          static_cast<int>(unit.type));  // P11：占领 credit = 该兵
            }
            return;
        }
        // 地图边界（P10 环绕）：wrap 开启 → 传送对侧（速度/方向不变，0 RNG）；关闭 → 反弹（翻角 + 随机小偏置）。
        // 传送目标取对侧"刚进界"位置（kEps 偏移，避开 findNextXY 的格线 eps 语义），并保留本 tick 剩余
        // 移动量继续走（与原反弹路径同构：continue）。跨过边界后下一格的山/海陆转换在下次格线跨越时正常
        // 评估（边界瞬态格不单独处理，属 P10 已知轻微不完善，见开发计划 P10 记录）。撞角(-3) 保持原处理
        // ——反向 +π+4eps 后继续，若随后撞界自然被本块 wrap 处理（坐标归一）。
        const bool wrapMode = ctx.wrap;
        if (boundaryCode == 0 && pos.x <= 0) {
            if (wrapMode) {
                pos.x = map.width() - kEps;
            } else {
                bounce(ctx, vel, boundaryCode);
            }
            continue;
        }
        if (boundaryCode == 1 && pos.y <= 0) {
            if (wrapMode) {
                pos.y = map.height() - kEps;
            } else {
                bounce(ctx, vel, boundaryCode);
            }
            continue;
        }
        if (boundaryCode == 2 && pos.x >= map.width()) {
            if (wrapMode) {
                pos.x = kEps;
            } else {
                bounce(ctx, vel, boundaryCode);
            }
            continue;
        }
        if (boundaryCode == 3 && pos.y >= map.height()) {
            if (wrapMode) {
                pos.y = kEps;
            } else {
                bounce(ctx, vel, boundaryCode);
            }
            continue;
        }

        // 目标格（沿穿越方向 ±eps 取整，见翻新计划 §2.3）。
        int gx = 0, gy = 0;
        if (boundaryCode == 0) {
            gx = static_cast<int>(pos.x - kEps);
            gy = static_cast<int>(pos.y);
        }
        if (boundaryCode == 1) {
            gx = static_cast<int>(pos.x);
            gy = static_cast<int>(pos.y - kEps);
        }
        if (boundaryCode == 2) {
            gx = static_cast<int>(pos.x + kEps);
            gy = static_cast<int>(pos.y);
        }
        if (boundaryCode == 3) {
            gx = static_cast<int>(pos.x);
            gy = static_cast<int>(pos.y + kEps);
        }
        const auto doBounce = [&] { bounce(ctx, vel, boundaryCode); };
        processEnteredCell(ctx, gy * w + gx, doBounce, vel, speed, remLength, onLand, mtn, fid,
                           unit, hist);
    }
    // 夹取到 [0, Map_x]×[0, Map_y]（每 tick 抖动已取消，Phase 9）。
    pos.x = std::clamp(pos.x, 0.0, static_cast<double>(map.width()));
    pos.y = std::clamp(pos.y, 0.0, static_cast<double>(map.height()));
}

// P12 密铺路径：crossEdge 边穿越 + 顶点 nudge + 世界周期环绕 / 边线反射反弹 + 边邻征服。
static void moveArmyTiled(MoveContext& ctx, entt::entity e, comp::Position& pos,
                          comp::Velocity& vel, comp::Speed& speed, comp::OnLand& onLand,
                          comp::MountainState& mtn, comp::Collider& col, comp::FactionId& fid,
                          comp::UnitType& unit, comp::LandHistory& hist) {
    const auto& cfg = ctx.config;
    const auto& map = ctx.map;
    const TilingGeom& g = map.geom();
    const double ww = map.worldWidth(), wh = map.worldHeight();
    double remLength = speed.value;
    int cellIdx = g.worldToCell(pos.x, pos.y);
    // 界外兜底（起始位置贴界/角点）：内缩再取；仍失败 → 反弹。
    if (cellIdx < 0) {
        clampWorld(pos, ww, wh);
        cellIdx = g.worldToCell(pos.x, pos.y);
    }
    int guard = 0;  // 防死循环（异常路径；正常远用不到）
    const auto relocate = [&]() {
        cellIdx = g.worldToCell(pos.x, pos.y);
        if (cellIdx < 0) {  // 贴界/角点防御：内缩后重取
            pos.x = std::clamp(pos.x, kEps, ww - kEps);
            pos.y = std::clamp(pos.y, kEps, wh - kEps);
            cellIdx = g.worldToCell(pos.x, pos.y);
        }
    };
    while (remLength > 0) {
        if (++guard > 10000) {  // 防御：异常路径强制退出（正常穿越每格 ≤ 数次迭代）
            spdlog::warn("moveArmyTiled: stuck guard (pos {:.3f},{:.3f} cell {} rem {:.4f})", pos.x,
                         pos.y, cellIdx, remLength);
            break;
        }
        // 战斗检查：每跨一格一次（§2.4）。
        if (CombatSystem::checkAt(ctx, e, pos, col, fid)) return;

        const int crossed =
            (cellIdx >= 0) ? g.crossEdge(cellIdx, pos.x, pos.y, vel.angle, remLength) : -1;
        if (crossed == -2) {
            // 走完本段：夹取 + 终点格补一次征服，返回。
            clampWorld(pos, ww, wh);
            relocate();
            if (cellIdx >= 0 && map.atIndex(cellIdx).belongi != fid.value)
                conquerAtIndex(ctx, cellIdx, fid.value, static_cast<int>(unit.type));
            return;
        }
        if (crossed == -1) {
            // 顶点命中/贴边：nudge ε 穿越顶点后重定位；界外 → 环绕或反弹（角点退化，
            // 反向 + jitter，与方形 -3 撞角语义对应）。
            pos.x += kEps * std::cos(vel.angle);
            pos.y += kEps * std::sin(vel.angle);
            relocate();
            if (cellIdx >= 0) continue;
            if (ctx.wrap) {
                pos.x = std::fmod(pos.x, ww);
                if (pos.x < 0) pos.x += ww;
                pos.y = std::fmod(pos.y, wh);
                if (pos.y < 0) pos.y += wh;
                relocate();
            } else {
                vel.angle += kPi + 4.0 * kEps;
                vel.angle += bounceJitter(ctx);
                relocate();
            }
            continue;
        }
        // 正常穿越：crossed = 边序号；先判邻格是否越界（边界），再进入。
        int nr, nc;
        g.neighborRaw(cellIdx, crossed, nr, nc);
        const bool offX = (nc < 0 || nc >= g.cols);
        const bool offY = (nr < 0 || nr >= g.rows);
        if (offX || offY) {
            // 地图边界：wrap 开 → 位置按世界周期环绕（速度/方向不变，0 RNG）；关 → 边线反射反弹。
            if (ctx.wrap) {
                wrapPos(pos, offX, offY, ww, wh, nc, nr);
                relocate();
            } else {
                bounceLine(ctx, vel, edgeLineAngle(g, crossed));
            }
            continue;
        }
        // 进入格（六/三角邻格恒为反向；neighbor 返回界内下标）。
        const int nb = g.neighbor(cellIdx, crossed);
        if (nb < 0) {  // 防御（不应发生；界内已判）
            bounceLine(ctx, vel, edgeLineAngle(g, crossed));
            continue;
        }
        const auto doBounce = [&] { bounceLine(ctx, vel, edgeLineAngle(g, crossed)); };
        const bool bounced =
            processEnteredCell(ctx, nb, doBounce, vel, speed, remLength, onLand, mtn, fid, unit,
                               hist);
        // 反弹 → 保持原格（退回）；成功进入 → 跟踪目标格。
        if (!bounced) cellIdx = nb;
    }
    clampWorld(pos, ww, wh);
}

}  // namespace

MoveContext MovementSystem::makeContext(Simulation& sim) {
    return MoveContext{sim.map(),      sim.factions(),       sim.rng(),
                       sim.pendingSpawns(), sim.deaths(),     sim.registry(),
                       sim.spatialHash(),   sim.goSeaProbability(),
                       static_cast<int>(sim.tickCount()), sim.config(),
                       sim.options().map.wrap,  // P10：边界贯通开关（默认 false → 基线不变）
                       &sim.stats()};  // P11：统计通道（conquerAt/markDead 记录 credit）
}

void MovementSystem::update(Simulation& sim) {
    MoveContext ctx = makeContext(sim);
    ctx.spatialHash.build(ctx.registry, ctx.map.geom());  // P12：按密铺格分桶
    auto view = ctx.registry
                    .view<comp::Position, comp::Velocity, comp::Speed, comp::OnLand,
                          comp::Collider, comp::FactionId, comp::UnitType, comp::LandHistory>();
    // P9 确定性：按实体 id 降序迭代（等价无回收时视图逆序），与存储序无关 →
    // 快照读档后逐 tick RNG 与直跑一致。移动有 RNG（findNextXY/下海/山地/征服/反弹）。
    std::vector<entt::entity> armies(view.begin(), view.end());
    std::sort(armies.begin(), armies.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) > entt::to_integral(b);
    });
    for (auto e : armies) {
        if (ctx.registry.all_of<comp::Dead>(e)) continue;  // 本 tick 已被战斗标死
        moveArmy(ctx, e);
    }
}

void MovementSystem::moveArmy(MoveContext& ctx, entt::entity e) {
    if (ctx.registry.all_of<comp::Dead>(e)) return;
    auto& pos = ctx.registry.get<comp::Position>(e);
    auto& vel = ctx.registry.get<comp::Velocity>(e);
    auto& speed = ctx.registry.get<comp::Speed>(e);
    auto& onLand = ctx.registry.get<comp::OnLand>(e);
    auto& mtn = ctx.registry.get<comp::MountainState>(e);  // P5：山地状态
    auto& col = ctx.registry.get<comp::Collider>(e);
    auto& fid = ctx.registry.get<comp::FactionId>(e);
    auto& unit = ctx.registry.get<comp::UnitType>(e);
    auto& hist = ctx.registry.get<comp::LandHistory>(e);

    // P12：正方形走原版路径（findNextXY 轴对齐穿越，零行为变化）；六/三角走密铺路径。
    if (ctx.map.tiling() == TilingType::Square) {
        moveArmySquare(ctx, e, pos, vel, speed, onLand, mtn, col, fid, unit, hist);
    } else {
        moveArmyTiled(ctx, e, pos, vel, speed, onLand, mtn, col, fid, unit, hist);
    }
}

}  // namespace lw
