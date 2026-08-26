#include "sim/systems/MovementSystem.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "core/MathUtil.h"
#include "core/Simulation.h"
#include "sim/systems/CombatSystem.h"

namespace lw {

namespace {

using Clock = std::chrono::steady_clock;

// 把一段时间累加到 Movement 内部细分计数器（剖析用；setMoveProfile 语义清晰）。
void setMoveProfile(Simulation& sim, std::uint64_t& slot, Clock::duration d) {
    slot += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
}

// 逐兵子阶段计时（MoveProfile 非空才计；默认/单测 moveProfile=nullptr → 零开销）。
struct MoveTimer {
    std::uint64_t* slot = nullptr;
    Clock::time_point t0;
    MoveTimer(MoveProfile* p, std::uint64_t* s) : slot(p ? s : nullptr) {
        if (slot) t0 = Clock::now();
    }
    ~MoveTimer() {
        if (slot) {
            *slot += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
        }
    }
};

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

double edgeLineAngle(const TilingGeom& g, int index, int k) {
    switch (g.type) {
        case TilingType::Square: return (k == 0 || k == 2) ? kPi / 2.0 : 0.0;
        case TilingType::Hex: return kPi / 180.0 * (60.0 * static_cast<double>(k) + 90.0);
        case TilingType::Tri: {
            static const double kA[3] = {0.0, kPi / 3.0, 2.0 * kPi / 3.0};
            return kA[static_cast<size_t>(k % 3)];
        }
        default: {
            double x0, y0, x1, y1;
            if (g.cellEdge(index, k, x0, y0, x1, y1))
                return std::atan2(y1 - y0, x1 - x0);
            return 0.0;
        }
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

// 方形路径：原版 moveArmy 主体（findNextXY 轴对齐格线穿越 + 边界 bounce）。
static void moveArmySquare(MoveContext& ctx, entt::entity e, comp::Position& pos,
                           comp::Velocity& vel, comp::Speed& speed, comp::OnLand& onLand,
                           comp::MountainState& mtn, comp::Collider& col, comp::FactionId& fid,
                           comp::UnitType& unit, comp::LandHistory& hist) {
    const auto& cfg = ctx.config;
    const auto& map = ctx.map;
    const int w = map.width();

    double remLength = speed.value;
    while (remLength > 0) {
        const auto t_geom = ctx.moveProfile ? Clock::now() : Clock::time_point{};
        const int boundaryCode = math::findNextXY(pos.x, pos.y, vel.angle, remLength, ctx.rng);
        if (ctx.moveProfile)
            ctx.moveProfile->geomNs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t_geom).count());
        // 战斗检查：每跨一格一次（§2.4）。命中则双方标死，本兵本 tick 移动中止。
        {
            MoveTimer ct(ctx.moveProfile, &ctx.moveProfile->combatNs);
            if (CombatSystem::checkAt(ctx, e, pos, col, fid)) return;
        }

        if (boundaryCode == -3) continue;  // 撞角：findNextXY 内已反向（+π+微偏置），继续本 tick 移动
        if (boundaryCode < 0 || boundaryCode > 3) {
            // -2 走完本段：夹取到界内 + 结束点补一次征服，返回。
            if (pos.x < 0) pos.x = kEps;
            if (pos.x > map.width()) pos.x = map.width() - kEps;
            if (pos.y < 0) pos.y = kEps;
            if (pos.y > map.height()) pos.y = map.height() - kEps;
            if (map.at(static_cast<int>(pos.x), static_cast<int>(pos.y)).belongi != fid.value) {
                {
                    MoveTimer ct(ctx.moveProfile, &ctx.moveProfile->conquerNs);
                    conquerAt(ctx, static_cast<int>(pos.x), static_cast<int>(pos.y), fid.value,
                              static_cast<int>(unit.type));
                }
            }
            return;
        }
        // 地图边界（废除环绕后统一反弹：翻角 + 随机小偏置，保留本 tick 剩余移动量继续走）。
        if (boundaryCode == 0 && pos.x <= 0) {
            bounce(ctx, vel, boundaryCode);
            continue;
        }
        if (boundaryCode == 1 && pos.y <= 0) {
            bounce(ctx, vel, boundaryCode);
            continue;
        }
        if (boundaryCode == 2 && pos.x >= map.width()) {
            bounce(ctx, vel, boundaryCode);
            continue;
        }
        if (boundaryCode == 3 && pos.y >= map.height()) {
            bounce(ctx, vel, boundaryCode);
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
        {
            MoveTimer et(ctx.moveProfile, &ctx.moveProfile->enterNs);
            processEnteredCell(ctx, gy * w + gx, doBounce, vel, speed, remLength, onLand, mtn, fid,
                               unit, hist);
        }
    }
    // 夹取到 [0, Map_x]×[0, Map_y]（每 tick 抖动已取消，Phase 9）。
    pos.x = std::clamp(pos.x, 0.0, static_cast<double>(map.width()));
    pos.y = std::clamp(pos.y, 0.0, static_cast<double>(map.height()));
}

// P12 密铺路径：crossEdge 边穿越 + 顶点穿越 + 边线反射反弹 + 边邻征服。
// 边界语义：废除环绕后统一为**反弹**（沿所撞边的直线角反射 + 随机小偏置，与方形一致）。
// 简洁原则：不做位置 clamp 兜底（clamp 会掩盖"军队真到边界"的信号，把贴界顶点误判为
// 仍在格内 → 无限循环卡死）。军队只通过 crossEdge/顶点穿越推进；一旦 worldToCell 无法
// 解析当前位置（贴界顶点/越界），即按反弹处理。
static void moveArmyTiled(MoveContext& ctx, entt::entity e, comp::Position& pos,
                          comp::Velocity& vel, comp::Speed& speed, comp::OnLand& onLand,
                          comp::MountainState& mtn, comp::Collider& col, comp::FactionId& fid,
                          comp::UnitType& unit, comp::LandHistory& hist) {
    const auto& cfg = ctx.config;
    const auto& map = ctx.map;
    const TilingGeom& g = map.geom();
    double remLength = speed.value;
    int cellIdx = g.worldToCell(pos.x, pos.y);
    // 起始位置若恰落顶点/越界（贴界 spawn）：向内微调一步再取（仅起始兜底，非循环）。
    if (cellIdx < 0) {
        const double ox = pos.x, oy = pos.y;
        pos.x = std::clamp(ox, kEps, map.worldWidth() - kEps);
        pos.y = std::clamp(oy, kEps, map.worldHeight() - kEps);
        cellIdx = g.worldToCell(pos.x, pos.y);
    }
    int guard = 0;  // 防死循环（异常路径；正常远用不到）
    while (remLength > 0) {
        if (++guard > 1000) {
            spdlog::warn("moveArmyTiled: stuck guard (pos {:.3f},{:.3f} cell {} rem {:.4f})", pos.x,
                         pos.y, cellIdx, remLength);
            break;
        }
        // 战斗检查：每跨一格一次（§2.4）。
        {
            MoveTimer ct(ctx.moveProfile, &ctx.moveProfile->combatNs);
            if (CombatSystem::checkAt(ctx, e, pos, col, fid)) return;
        }

        const auto t_geom = ctx.moveProfile ? Clock::now() : Clock::time_point{};
        const int crossed =
            (cellIdx >= 0) ? g.crossEdge(cellIdx, pos.x, pos.y, vel.angle, remLength) : -1;
        if (ctx.moveProfile)
            ctx.moveProfile->geomNs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t_geom).count());
        if (crossed == -2) {
            // 走完本段：终点格补一次征服，返回。
            if (cellIdx >= 0 && map.atIndex(cellIdx).belongi != fid.value) {
                MoveTimer conq(ctx.moveProfile, &ctx.moveProfile->conquerNs);
                conquerAtIndex(ctx, cellIdx, fid.value, static_cast<int>(unit.type));
            }
            pos.x = std::clamp(pos.x, 0.0, map.worldWidth());
            pos.y = std::clamp(pos.y, 0.0, map.worldHeight());
            return;
        }
        if (crossed == -1) {
            // 顶点命中/贴边：沿方向 nudge 穿越顶点，重新解析。crossEdge 已把位置推进到顶点
            //（贴顶点/角的测度零位置）；kEps 太小可能仍"贴着"顶点原路返回 → 死锁。
            // 用一个小而有意义的步长（相对格对角线尺度）确保越过顶点进入相邻格。
            const double nudge = std::max(1e-3, 0.02 * std::min(map.worldWidth(),
                                                               map.worldHeight())
                                                      / static_cast<double>(std::max(1, g.cols)));
            pos.x += nudge * std::cos(vel.angle);
            pos.y += nudge * std::sin(vel.angle);
            cellIdx = g.worldToCell(pos.x, pos.y);
            if (cellIdx >= 0) continue;  // 已穿越进入相邻格
            // 顶点两侧都无格：贴地图边界顶点/角。反向 + jitter（与方形撞角语义对应）。
            vel.angle += kPi + 4.0 * kEps;
            vel.angle += bounceJitter(ctx);
            continue;
        }
        // 正常穿越：crossed = 边序号；先判邻格是否越界（地图边界），再进入。
        int nr, nc;
        g.neighborRaw(cellIdx, crossed, nr, nc);
        const bool offX = (nc < 0 || nc >= g.cols);
        const bool offY = (nr < 0 || nr >= g.rows);
        if (offX || offY) {
            // 撞地图边界：边线反射反弹。
            bounceLine(ctx, vel, edgeLineAngle(g, cellIdx, crossed));
            continue;
        }
        // 进入格（六/三角邻格恒为反向；neighbor 返回界内下标）。
        const int nb = g.neighbor(cellIdx, crossed);
        if (nb < 0) {  // 防御（不应发生；界内已判）
            bounceLine(ctx, vel, edgeLineAngle(g, cellIdx, crossed));
            continue;
        }
        const auto doBounce = [&] { bounceLine(ctx, vel, edgeLineAngle(g, cellIdx, crossed)); };
        const bool bounced = [&] {
            MoveTimer et(ctx.moveProfile, &ctx.moveProfile->enterNs);
            return processEnteredCell(ctx, nb, doBounce, vel, speed, remLength, onLand, mtn, fid,
                                      unit, hist);
        }();
        // 反弹 → 保持原格（退回）；成功进入 → 跟踪目标格。
        if (!bounced) cellIdx = nb;
    }
    // 顶点穿越/边界反弹过程中位置可能略越出真实边界（贴界顶点残差），夹回界内——
    // 仅在移动结束后（循环已退出），不会掩盖"撞边界"信号（否则无法反弹，见卡死分析）。
    pos.x = std::clamp(pos.x, 0.0, map.worldWidth());
    pos.y = std::clamp(pos.y, 0.0, map.worldHeight());
}

}  // namespace

MoveContext MovementSystem::makeContext(Simulation& sim) {
    MoveContext ctx{sim.map(),      sim.factions(),       sim.rng(),
                    sim.pendingSpawns(), sim.deaths(),     sim.registry(),
                    sim.spatialHash(),   sim.goSeaProbability(),
                    static_cast<int>(sim.tickCount()), sim.config(),
                    &sim.stats()};  // P11：统计通道（conquerAt/markDead 记录 credit）
    ctx.moveProfile = sim.profile().enabled ? &sim.profile().move : nullptr;  // 剖析开关
    return ctx;
}

void MovementSystem::update(Simulation& sim) {
    MoveContext ctx = makeContext(sim);
    // 性能细分：hash 重建 / 排序 / 逐兵 move（2026-08 分析；非剖析态仅一次分支判断）。
    const bool prof = sim.profile().enabled;
    const auto t_h0 = prof ? Clock::now() : Clock::time_point{};
    ctx.spatialHash.build(ctx.registry, ctx.map.geom());  // P12：按密铺格分桶
    const auto t_h1 = prof ? Clock::now() : Clock::time_point{};
    if (prof) setMoveProfile(sim, sim.profile().moveHashNs, t_h1 - t_h0);
    auto view = ctx.registry
                    .view<comp::Position, comp::Velocity, comp::Speed, comp::OnLand,
                          comp::Collider, comp::FactionId, comp::UnitType, comp::LandHistory>();
    // P9 确定性：按实体 id 降序迭代（等价无回收时视图逆序），与存储序无关 →
    // 快照读档后逐 tick RNG 与直跑一致。移动有 RNG（findNextXY/下海/山地/征服/反弹）。
    std::vector<entt::entity> armies(view.begin(), view.end());
    std::sort(armies.begin(), armies.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) > entt::to_integral(b);
    });
    const auto t_loop = prof ? Clock::now() : Clock::time_point{};
    if (prof) setMoveProfile(sim, sim.profile().moveSortNs, t_loop - t_h1);
    for (auto e : armies) {
        if (ctx.registry.all_of<comp::Dead>(e)) continue;  // 本 tick 已被战斗标死
        moveArmy(ctx, e);
    }
    if (prof) setMoveProfile(sim, sim.profile().moveLoopNs, Clock::now() - t_loop);
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

    MoveTimer loopTimer(ctx.moveProfile, ctx.moveProfile ? &ctx.moveProfile->loopNs : nullptr);
    // P12：正方形走原版路径（findNextXY 轴对齐穿越，零行为变化）；六/三角走密铺路径。
    if (ctx.map.tiling() == TilingType::Square) {
        moveArmySquare(ctx, e, pos, vel, speed, onLand, mtn, col, fid, unit, hist);
    } else {
        moveArmyTiled(ctx, e, pos, vel, speed, onLand, mtn, col, fid, unit, hist);
    }
}

}  // namespace lw
