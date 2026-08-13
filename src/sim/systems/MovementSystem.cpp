#include "sim/systems/MovementSystem.h"

#include <algorithm>
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

// 征服 (x,y) 归势力 factionId；若为开拓兵，同时按固定顺序（上/下/左/右）征服目标格 4 个
// 正交邻格（细节改进：开拓兵碰敌方领土时连相邻格一起占领）。邻格越界/同势力由
// Faction::conquer 自行 no-op；固定顺序保证势力8 攻占城市免费兵 RNG 消耗可复现。
void conquerCell(MoveContext& ctx, int x, int y, int factionId, ArmyType type) {
    // P11：占领 credit = 该兵 (faction, type)；开拓连占的 4 邻格同记。
    conquerAt(ctx, x, y, factionId, static_cast<int>(type));
    if (type == ArmyType::pioneer) {
        conquerAt(ctx, x, y + 1, factionId, static_cast<int>(type));
        conquerAt(ctx, x, y - 1, factionId, static_cast<int>(type));
        conquerAt(ctx, x - 1, y, factionId, static_cast<int>(type));
        conquerAt(ctx, x + 1, y, factionId, static_cast<int>(type));
    }
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
    ctx.spatialHash.build(ctx.registry, ctx.map.width(), ctx.map.height());
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

    const auto& cfg = ctx.config;
    const auto& map = ctx.map;

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
        const MapCell* goalCell = nullptr;
        if (boundaryCode == 0)
            goalCell = &map.at(static_cast<int>(pos.x - kEps), static_cast<int>(pos.y));
        if (boundaryCode == 1)
            goalCell = &map.at(static_cast<int>(pos.x), static_cast<int>(pos.y - kEps));
        if (boundaryCode == 2)
            goalCell = &map.at(static_cast<int>(pos.x + kEps), static_cast<int>(pos.y));
        if (boundaryCode == 3)
            goalCell = &map.at(static_cast<int>(pos.x), static_cast<int>(pos.y + kEps));

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
                bounce(ctx, vel, boundaryCode);
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
                    bounce(ctx, vel, boundaryCode);
                    canCross = false;
                } else if (!mtn.inMountain) {
                    mtn.inMountain = true;
                    if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                        speed.value *= cfg.terrain.mountainSpeedMult;
                        remLength *= cfg.terrain.mountainSpeedMult;
                    }
                }
            }
            if (canCross) {
                // 攻占敌方/中立领地：先征服目标格（开拓连占上下左右 4 邻格），再掷敌方反弹——
                // 与山地骰【独立计算】：本格既为敌又为山时两骰各掷一次（非先锋 bounceMult=1.0
                // 敌方必然反弹；先锋 0.4 两骰都过才不反弹）。反弹是征服后的"弹回"（原版语义）。
                if (goalCell->belongi != fid.value) {
                    conquerCell(ctx, goalCell->x, goalCell->y, fid.value, unit.type);
                    // P7：反弹概率 × 势力增益（基线 bounceChanceMult=1.0 → 恒等，行为不变）。
                    double reboundChance =
                        cfg.units[static_cast<int>(unit.type)].bounceMult
                        * ctx.factions[static_cast<size_t>(fid.value)].mods.bounceChanceMult;
                    if (ctx.rng.chance(reboundChance)) {
                        bounce(ctx, vel, boundaryCode);
                    }
                }
            }
        } else if (goalCell->land && !onLand.value) {
            // 海→陆 登陆：目标为山先掷进入概率（失败→反弹留海）；通过后登陆 + 复原陆速
            // + 进山（若减速）+ 征服（开拓连占邻格）。
            bool canCross = true;
            if (goalCell->mountain) {
                if (!ctx.rng.chance(mountainEnterChanceFor(cfg, static_cast<int>(unit.type)))) {
                    bounce(ctx, vel, boundaryCode);
                    canCross = false;
                }
            }
            if (canCross) {
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
                conquerCell(ctx, goalCell->x, goalCell->y, fid.value, unit.type);
            }
        }
    }
    // 夹取到 [0, Map_x]×[0, Map_y]（每 tick 抖动已取消，Phase 9）。
    pos.x = std::clamp(pos.x, 0.0, static_cast<double>(map.width()));
    pos.y = std::clamp(pos.y, 0.0, static_cast<double>(map.height()));
}

}  // namespace lw
