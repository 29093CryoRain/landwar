#include "sim/systems/MovementSystem.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "core/Simulation.h"
#include "sim/ConquestRules.h"
#include "sim/systems/CombatSystem.h"

namespace lw {

namespace {

using Clock = std::chrono::steady_clock;

// 把一段时间累加到 Movement 内部细分计数器（剖析用；setMoveProfile 语义清晰）。
void setMoveProfile(std::uint64_t& slot, Clock::duration d) {
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

// Phase 3.1：反弹角偏置。每 tick 抖动已取消，改为反弹（地图墙/下海失败/征服反弹）时
// 添加一个随机小偏置，避免规则反射的对称性；一次 unit() 即完成一次取样。
double bounceJitter(const MoveContext& ctx) {
    return (ctx.rng.unit() * 2.0 - 1.0) * ctx.config.army.bounceJitterRangeRad;
}

// 方向角归一化到 (-π, π]。反弹/反向只关心方向（cos/sin 不变），归一化避免角度跨 tick
// 无界漂移；方/六/三/半正/Laves 统一走此约束（无密铺特判）。
double wrapAngle(double a) {
    a = std::fmod(a, 2.0 * kPi);
    if (a <= -kPi) a += 2.0 * kPi;
    if (a > kPi) a -= 2.0 * kPi;
    return a;
}

// 沿边线角（rad）反射方向角 + 随机小偏置：θ' = 2·lineAngle − θ。
// 方/六/三/半正/Laves 全部走同一条路径（边线角由 cellEdge 端点统一给出，见 edgeLineAngle）。
// 先归一化再叠抖动：方向保持在 (-π, π] 附近，不随多次反弹无界漂移。
void bounceLine(MoveContext& ctx, comp::Velocity& vel, double lineAngle) {
    vel.angle = wrapAngle(2.0 * lineAngle - vel.angle) + bounceJitter(ctx);
}

// 进入格处理的结果（原 bool 的扩展；二期反馈：过河停顿）。
struct EnterResult {
    bool bounced = false;      // true = 反弹（未进入目标格，调用方保持原格跟踪）
    bool riverPaused = false;  // true = 过河通过且已进入目标格 → 调用方把兵推入目标格内侧
    double riverCarry = 0.0;   // 结转到下一 tick 的剩余步长（过河停顿）
};

// 过河停顿：兵停在边上时，沿速度方向推进目标格内一小步（与密铺顶点穿越同一量级：相对格尺寸）。
// 否则下一 tick 的取整 / worldToCell 可能仍解析回原格 → 反复触发同一条河的河骰与停顿。
void nudgeIntoEnteredCell(const TilingGeom& g, comp::Position& pos, double angle) {
    const double nudge = std::max(1e-3, 0.02 * std::min(g.worldWidth(), g.worldHeight())
                                             / static_cast<double>(std::max(1, g.cols)));
    pos.x += nudge * std::cos(angle);
    pos.y += nudge * std::sin(angle);
}

// 穿越边的**线角**（rad）：统一取 cellEdge 两端点 atan2。方/六/三/半正/Laves 同一条路径，
// 无密铺特判（边线角相差 π 对反射等价，因结果会归一化）。
double edgeLineAngle(const TilingGeom& g, int index, int k) {
    double x0, y0, x1, y1;
    if (g.cellEdge(index, k, x0, y0, x1, y1)) return std::atan2(y1 - y0, x1 - x0);
    return 0.0;
}

// 征服格（下标）归势力 factionId；开拓兵同时按规范序征服全部**边邻格**（P12：
// 与目标格共享公共边的相邻地块一次性征服——方 4（上/下/左/右，见 tiling_specs_regular.json
// 的 edgeOrder=[2,0,3,1]、六 6、三 3）。固定顺序保证势力8 攻占城市免费兵 RNG 消耗可复现。
// originIndex = 兵进入前的格（2026-09）：由 conquerIndex 一并尝试占领（顶点穿越绕过
// processEnteredCell 时也要把途经格补上；旧"终点格补占"已删除）。开拓的边邻扩散不重复带
// origin（目标格那一次已占），避免同一格被反复尝试。
void conquerCell(MoveContext& ctx, int cellIndex, int factionId, ArmyType type, int originIndex) {
    MoveTimer conq(ctx.moveProfile, ctx.moveProfile ? &ctx.moveProfile->conquerNs : nullptr);
    conquerAtIndex(ctx, cellIndex, factionId, static_cast<int>(type), originIndex);
    if (type == ArmyType::pioneer) {
        const TilingGeom& g = ctx.map.geom();
        for (int k = 0; k < g.neighborCount(); ++k) {
            const int nb = g.neighbor(cellIndex, k);
            if (nb >= 0) conquerAtIndex(ctx, nb, factionId, static_cast<int>(type));
        }
    }
}

// 进入格处理（陆/海/山/征服；goalIdx 已定，goalCell = atIndex(goalIdx)）。所有密铺共用：
// 反弹路径由调用方注入（bounceLine(边序号)）。修改
// vel/speed/remLength/onLand/mtn/hist；RNG 顺序与旧 moveArmy 逐位一致（河骰 + 过河抖动为新增）。
// 返回 EnterResult：bounced / 过河停顿（见结构体注释）。
template <typename Bounce>
EnterResult processEnteredCell(MoveContext& ctx, int goalIdx, int srcIdx, int edgeK,
                               Bounce&& doBounce, comp::Velocity& vel, comp::Speed& speed,
                               double& remLength, comp::OnLand& onLand, comp::MountainState& mtn,
                               comp::FactionId& fid, comp::UnitType& unit,
                               comp::LandHistory& hist, comp::AllyGate& gate) {
    const auto& cfg = ctx.config;
    const MapCell* goalCell = &ctx.map.atIndex(goalIdx);
    // 兵运动规范（开发文档 §4）：可占领性只由"势力 + 兵种 + 格"决定；海恒不可占领，
    // 但海洋走独立闸门（下海骰），不参与规则 1/2 分流。
    const int ut = static_cast<int>(unit.type);
    const bool srcConquerable = canConquerCell(ctx.map, ctx.factions, fid.value, ut, srcIdx);
    const bool dstConquerable = canConquerCell(ctx.map, ctx.factions, fid.value, ut, goalIdx);
    // 盟友边界闸门（开发文档 §12）：一次"盟友边界尝试" = 从非盟友土地一侧试图进入盟友领土。
    // 河骰 / 山骰失败与盟友规则反弹都发生在这条边界上，一并计入观测；规则 1/3 的直接穿过不计。
    // 惰性判定：只在目标格是盟友陆地时才查（&& 短路）；无联盟 / 非盟友目标 → 恒 false，
    // 热路径上除两次指针比较外零额外开销（盟友陆必 !dstConquerable，故用它做先验早退）。
    const bool dstAllyLand =
        !dstConquerable && goalCell->belongi != fid.value
        && isAlliedLand(ctx.map, ctx.factions, fid.value, goalIdx);
    const bool atAllyBorder =
        dstAllyLand && !isAlliedLand(ctx.map, ctx.factions, fid.value, srcIdx);
    // 边界反弹计数（+1）+ 实际反弹。观测只在"盟友边界"上累加，其余反弹不喂养闸门。
    auto bounceAtBorder = [&] {
        if (atAllyBorder) gate.cusum += 1.0;
        doBounce();
    };
    // Terrain changes are provisional until the target cell is actually entered. A later
    // sea/mountain/enemy rejection bounces the army back into its original cell.
    double nextSpeed = speed.value;
    double nextRemLength = remLength;
    bool nextOnLand = onLand.value;
    bool nextInMountain = mtn.inMountain;
    int nextLastLandTime = hist.lastLandTime;
    // 二期反馈：过河（通过且最终真正进入目标格）→ 方向抖动 + 停顿结转。
    bool crossedRiver = false;
    double riverCarry = 0.0;
    if (!goalCell->land && onLand.value) {
        // 陆/山→海：山→海 先复原山地速度（若该兵种在山地减速），再走原下海路径。
        if (nextInMountain) {
            nextInMountain = false;
            if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                nextSpeed /= cfg.terrain.mountainSpeedMult;
                nextRemLength /= cfg.terrain.mountainSpeedMult;
            }
        }
        // 陆→海：概率下海，失败反弹。
        double goSeaChance = (ctx.ttime - hist.lastLandTime + cfg.sea.goSeaChanceConst)
                             * ctx.goSeaProbability / cfg.sea.goSeaChanceDenominator;
        // P7：下海概率 × 势力增益（基线 seaChanceMult=1.0 → 恒等，行为不变）。
        goSeaChance *= ctx.factions[static_cast<size_t>(fid.value)].mods.seaChanceMult;
        if (ctx.rng.chance(goSeaChance)) {
            nextOnLand = false;
            nextSpeed *= cfg.sea.seaSpeedMult;   // 下海 speed ×0.5（永久降为海速）
            nextRemLength *= cfg.sea.seaSpeedMult;     // 剩余移动量按新海速折算
            // 2026-08 用户定夺：去掉原版紧随的 `break`——不再丢弃本 tick 剩余移动，下海后续走
            // （速度与剩余量同步减半，语义自洽；原版 `rem/=2; break` 里 `rem/=2` 因 break 成死代码）。
        } else {
            bounceAtBorder();
            return {true, false, 0.0};
        }
    } else if (goalCell->land && onLand.value) {
        // 河流系统 §6.1（R4）：河骰最前——失败 → 反弹并立即返回，局部量（nextSpeed/
        // nextRemLength/nextInMountain/nextLastLandTime）一个都不写回，因此速度/剩余长度/
        // 山地状态/占领全部保持原样（"前几个阶段失败不可占领"）。通过则**不减速**，继续走。
        // 河边两侧必为陆地（加载校验），所以只有本分支（陆→陆）可能遇到河。
        if (ctx.map.hasRiverEdge(srcIdx, edgeK)) {
            const double riverChance =
                std::clamp(cfg.river.crossChance
                               * cfg.units[static_cast<int>(unit.type)].riverCrossMult,
                           0.0, 1.0);
            if (!ctx.rng.chance(riverChance)) {
                bounceAtBorder();
                return {true, false, 0.0};
            }
            // 通过：先记下"过河了"，但方向抖动与停顿都**只在最终真正进入目标格时**结算
            //（下面还有山骰/敌方格，被弹回则不算过河）。
            crossedRiver = true;
            riverCarry = remLength;  // 过河瞬间未用完的剩余步长（此后只改 nextRemLength 副本）
        }
        // 陆→陆（含山）：目标非山时先复原离开山地；山地阻挡先判（失败→反弹，不进入不征服）。
        if (nextInMountain && !goalCell->mountain) {
            // 山→平地：离开山地，速度复原（若该兵种在山地减速；开拓不减速）。
            nextInMountain = false;
            if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                nextSpeed /= cfg.terrain.mountainSpeedMult;
                nextRemLength /= cfg.terrain.mountainSpeedMult;
            }
        }
        // 山地骰：下一格为山（不管当前格是什么，含陆→山/海→山/山→山）都掷进入概率；
        // 失败→反弹，通过才进入山地（减速，开拓不减速）。
        if (goalCell->mountain) {
            if (!ctx.rng.chance(mountainEnterChanceFor(cfg, static_cast<int>(unit.type)))) {
                bounceAtBorder();
                return {true, false, 0.0};
            }
            if (!nextInMountain) {
                nextInMountain = true;
                if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                    nextSpeed *= cfg.terrain.mountainSpeedMult;
                    nextRemLength *= cfg.terrain.mountainSpeedMult;
                }
            }
        }
        // 攻占敌方/中立领地：先征服目标格（开拓连占边邻格），再掷敌方反弹——
        // 与山地骰【独立计算】：本格既为敌又为山时两骰各掷一次（非先锋 bounceMult=1.0
        // 敌方必然反弹；先锋 bounceMult<1 两骰都过才不反弹）。反弹是征服后的"弹回"（原版语义）。
        // 不可占领格（盟友 / 禁征服敌土）走兵运动规范：
        //   规则 2（原格可占领）→ 确定反弹、不征服、不掷骰；
        //   规则 1（原格也不可占领）→ 不反弹、不征服，继续前进；
        //   规则 3（目标可占领、原格不可占领）→ 征服后不掷反弹骰。
        if (goalCell->belongi != fid.value) {
            if (!dstConquerable) {
                if (srcConquerable) {
                    // 盟友边界闸门（开发文档 §12）：持续受压已报警 → 本次穿越放行
                    //（不反弹、不征服），并清零统计量；否则按规则 2 确定反弹并喂养闸门。
                    if (atAllyBorder && gate.cusum >= cfg.allyGate.alarmThreshold) {
                        gate.cusum = 0.0;
                    } else {
                        bounceAtBorder();
                        return {true, false, 0.0};
                    }
                }
            } else {
                conquerCell(ctx, goalIdx, fid.value, unit.type, srcIdx);
                if (srcConquerable) {
                    // P7：反弹概率 × 势力增益（基线 bounceChanceMult=1.0 → 恒等，行为不变）。
                    double reboundChance =
                        cfg.units[static_cast<int>(unit.type)].bounceMult
                        * ctx.factions[static_cast<size_t>(fid.value)].mods.bounceChanceMult;
                    if (ctx.rng.chance(reboundChance)) {
                        bounceAtBorder();
                        return {true, false, 0.0};
                    }
                }
            }
        }
    } else if (goalCell->land && !onLand.value) {
        // 海→陆 登陆：目标为山先掷进入概率（失败→反弹留海）；通过后登陆 + 复原陆速
        // + 进山（若减速）+ 征服（开拓连占邻格）。
        if (goalCell->mountain
            && !ctx.rng.chance(mountainEnterChanceFor(cfg, static_cast<int>(unit.type)))) {
            bounceAtBorder();
            return {true, false, 0.0};
        }
        nextOnLand = true;
        nextSpeed /= cfg.sea.seaSpeedMult;   // 复原陆速（=×2）
        nextRemLength /= cfg.sea.seaSpeedMult;
        nextLastLandTime = ctx.ttime;
        if (goalCell->mountain) {
            nextInMountain = true;
            if (slowsInMountain(cfg, static_cast<int>(unit.type))) {
                nextSpeed *= cfg.terrain.mountainSpeedMult;
                nextRemLength *= cfg.terrain.mountainSpeedMult;
            }
        }
        // 规则 3（目标可占领）：登陆 + 征服，不反弹（原格为海=不可占领）。
        // 规则 1（目标也不可占领，如盟友陆）：登陆但不征服、不反弹，见开发文档 §4.4。
        if (dstConquerable) conquerCell(ctx, goalIdx, fid.value, unit.type, srcIdx);
    }
    speed.value = nextSpeed;
    remLength = nextRemLength;
    onLand.value = nextOnLand;
    mtn.inMountain = nextInMountain;
    hist.lastLandTime = nextLastLandTime;
    // 成功进入盟友领土（本次穿越没有被弹回）→ 压力解除：闸门统计量清零。
    if (atAllyBorder) gate.cusum = 0.0;
    if (crossedRiver) {
        // 过河（真正进入目标格）：方向随机抖动一次（对称区间 [-θ, θ]，与反弹抖动同型），
        // 且本 tick 剩余步长归零（停在河边）；未用完的量由调用方结转下一 tick。
        vel.angle += (ctx.rng.unit() * 2.0 - 1.0) * cfg.river.crossAngleJitterRad;
        remLength = 0.0;
        return {false, true, riverCarry};
    }
    return {false, false, 0.0};
}

// 通用移动路径（方/六/三/半正/Laves 统一）：crossEdge 边穿越 + 顶点穿越 + 边线反射反弹 + 边邻征服。
// 边界语义：废除环绕后统一为**反弹**（沿所撞边的直线角反射 + 随机小偏置）。
// 简洁原则：不做位置 clamp 兜底（clamp 会掩盖"军队真到边界"的信号，把贴界顶点误判为
// 仍在格内 → 无限循环卡死）。军队只通过 crossEdge/顶点穿越推进；一旦 worldToCell 无法
// 解析当前位置（贴界顶点/越界），即按反弹处理。
static void moveArmyGeom(MoveContext& ctx, entt::entity e, comp::Position& pos,
                         comp::Velocity& vel, comp::Speed& speed, comp::OnLand& onLand,
                         comp::MountainState& mtn, comp::Collider& col, comp::FactionId& fid,
                         comp::UnitType& unit, comp::LandHistory& hist, comp::MoveCarry& carry,
                         comp::AllyGate& gate) {
    const auto& map = ctx.map;
    const TilingGeom& g = map.geom();
    double remLength = speed.value;
    // 过河停顿结转（二期反馈）：本 tick 起始步长 = 上一 tick 结转的剩余量（不补满），用掉即清零。
    if (carry.value > 0.0) {
        remLength = carry.value;
        carry.value = 0.0;
    }
    // 盟友边界闸门（开发文档 §12）：先按**本 tick 步长**结算泄漏（乘步长 = 结合该兵速度，
    // 判据按"每格尝试次数"计），再在盟友边界反弹处 +1（见 processEnteredCell 的 bounceAtBorder）。
    // CUSUM 反射于 0：S = max(0, S − (c+η)·步长) + 反弹数。放行/成功进入盟友领土时清零。
    const auto& gateCfg = ctx.config.allyGate;
    gate.cusum = std::max(
        0.0, gate.cusum - (gateCfg.leakPerCell + gateCfg.tolerancePerCell) * remLength);
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
            spdlog::warn("moveArmyGeom: stuck guard (pos {:.3f},{:.3f} cell {} rem {:.4f})", pos.x,
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
            // 走完本段：本 tick 不再跨边、没有新的"进入格"事件 → 不征服（2026-09 用户定夺：
            // 删除旧的"终点格补占"）。途经而未走进入格处理的格由进入下一格时携带的
            // originIndex 补占（见 conquerCell / Faction::conquerIndex）。仅夹取位置后返回。
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
            // 顶点两侧都无格：贴地图边界顶点/角。反向 + jitter（纯 π 翻角，无边长可依）。
            vel.angle = wrapAngle(vel.angle + kPi + 4.0 * kEps) + bounceJitter(ctx);
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
        // 进入邻格（neighbor 返回界内下标）。
        const int nb = g.neighbor(cellIdx, crossed);
        if (nb < 0) {  // 防御（不应发生；界内已判）
            bounceLine(ctx, vel, edgeLineAngle(g, cellIdx, crossed));
            continue;
        }
        const auto doBounce = [&] { bounceLine(ctx, vel, edgeLineAngle(g, cellIdx, crossed)); };
        const EnterResult result = [&] {
            MoveTimer et(ctx.moveProfile, &ctx.moveProfile->enterNs);
            return processEnteredCell(ctx, nb, cellIdx, crossed, doBounce, vel, speed, remLength,
                                      onLand, mtn, fid, unit, hist, gate);
        }();
        // 反弹 → 保持原格（退回）；成功进入 → 跟踪目标格。过河停顿再推入目标格内侧一小步
        //（否则下一 tick worldToCell 可能仍解析回原格 → 反复触发同一条河）。
        if (!result.bounced) {
            cellIdx = nb;
            if (result.riverPaused) {
                carry.value = result.riverCarry;
                nudgeIntoEnteredCell(g, pos, vel.angle);
            }
        }
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
                    &sim.stats()};  // P11：统计通道（conquerAtIndex/markDead 记录 credit）
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
    if (prof) setMoveProfile(sim.profile().moveHashNs, t_h1 - t_h0);
    auto view = ctx.registry
                    .view<comp::Position, comp::Velocity, comp::Speed, comp::OnLand,
                          comp::Collider, comp::FactionId, comp::UnitType, comp::LandHistory>();
    // P9 确定性：按实体 id 降序迭代（等价无回收时视图逆序），与存储序无关 →
    // 快照读档后逐 tick RNG 与直跑一致。移动有 RNG（crossEdge 顶点穿越/下海/山地/征服/反弹）。
    std::vector<entt::entity> armies(view.begin(), view.end());
    std::sort(armies.begin(), armies.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) > entt::to_integral(b);
    });
    const auto t_loop = prof ? Clock::now() : Clock::time_point{};
    if (prof) setMoveProfile(sim.profile().moveSortNs, t_loop - t_h1);
    for (auto e : armies) {
        if (ctx.registry.all_of<comp::Dead>(e)) continue;  // 本 tick 已被战斗标死
        moveArmy(ctx, e);
    }
    if (prof) setMoveProfile(sim.profile().moveLoopNs, Clock::now() - t_loop);
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
    // 过河停顿结转（二期反馈）。用 get_or_emplace：单测/旧实体可能未带该组件（正常产兵/读档必带）。
    auto& carry = ctx.registry.get_or_emplace<comp::MoveCarry>(e);
    // 盟友边界闸门统计量（开发文档 §12）；同样用 get_or_emplace 兼容未带该组件的实体。
    auto& gate = ctx.registry.get_or_emplace<comp::AllyGate>(e);

    MoveTimer loopTimer(ctx.moveProfile, ctx.moveProfile ? &ctx.moveProfile->loopNs : nullptr);
    // 全密铺统一走几何路径（方/六/三/半正/Laves 同一条）。
    moveArmyGeom(ctx, e, pos, vel, speed, onLand, mtn, col, fid, unit, hist, carry, gate);
}

}  // namespace lw
