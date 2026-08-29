// ProjectileSystem.cpp — 射弹求解（开发计划 P9 任务 3）。
// 子弹沿角以 Speed.value 逐格推进（findNextXY 不消耗 RNG → 移动确定性）。
//   穿海陆不减速（无 OnLand 组件）；不占领途经领地（P9 改版，2026-08-07：任何地形照飞）；
//   撞击敌兵 → 敌我双亡（子弹由 DeathSystem 统一销毁，死亡特效 none）；
//   不能从陆进山（下一格为山且当前不在山 → 消失）；在山地留存变短（mountainLifespanPenalty）；
//   超时（lifespanTicks 归零）消失。
#include "sim/systems/ProjectileSystem.h"

#include <algorithm>
#include <vector>


#include "core/MathUtil.h"
#include "core/Simulation.h"
#include "sim/systems/CombatSystem.h"
#include "sim/systems/MovementSystem.h"
#include "sim/systems/Systems.h"

namespace lw {

namespace {

// 最大兵碰撞半径（子弹击杀搜索半径 = 子弹半径 + 最大兵半径）。
// 击杀判定：子弹当前位置处，首个距 < 子弹半径 + 敌半径 的敌方活兵 → 双方标死，返回 true。
// 只击杀兵（跳过其他子弹）；命中后子弹由 DeathSystem 销毁（死亡特效 none）。
bool hitEnemyAt(MoveContext& ctx, entt::entity bullet, const comp::Position& pos,
                const comp::Collider& col, const comp::FactionId& fid) {
    const double search = col.radius + CombatSystem::maxArmyRadius(ctx.config);
    for (auto a : ctx.spatialHash.queryCircle(ctx.registry, pos.x, pos.y, search)) {
        if (a == bullet) continue;
        if (ctx.registry.all_of<comp::Dead>(a)) continue;
        if (ctx.registry.all_of<comp::Projectile>(a)) continue;  // 子弹不互撞
        if (ctx.registry.get<comp::FactionId>(a).value == fid.value) continue;
        const auto& aPos = ctx.registry.get<comp::Position>(a);
        const auto& aCol = ctx.registry.get<comp::Collider>(a);
        if (math::distance(aPos.x, aPos.y, pos.x, pos.y) < col.radius + aCol.radius) {
            // P11：敌兵被子弹击杀 → credit 发射兵 (faction, type)；子弹自身消亡无击杀 credit。
            const int bulletType = static_cast<int>(ctx.registry.get<comp::UnitType>(bullet).type);
            markDead(ctx, a, aPos.x, aPos.y, pos.x, pos.y, fid.value, bulletType);
            markDead(ctx, bullet, pos.x, pos.y, aPos.x, aPos.y);
            return true;
        }
    }
    return false;
}

// 推进一颗子弹。命中/超时/撞山/出界 → 加入 toDestroy。
void solveProjectile(MoveContext& ctx, entt::entity e, std::vector<entt::entity>& toDestroy) {
    auto& reg = ctx.registry;
    auto& proj = reg.get<comp::Projectile>(e);
    auto& pos = reg.get<comp::Position>(e);
    auto& vel = reg.get<comp::Velocity>(e);
    auto& speed = reg.get<comp::Speed>(e);
    auto& fid = reg.get<comp::FactionId>(e);
    auto& col = reg.get<comp::Collider>(e);
    const auto& cfg = ctx.config;

    // 山地留存惩罚：本 tick 在山地 → 额外扣减；随后每 tick 消耗 1，归零消失。
    if (proj.inMountain) proj.lifespanTicks -= cfg.projectile.mountainLifespanPenalty;
    if (proj.lifespanTicks <= 0) { toDestroy.push_back(e); return; }
    --proj.lifespanTicks;
    if (proj.lifespanTicks <= 0) { toDestroy.push_back(e); return; }

    // 出生位置即撞敌（刚发射就贴脸）。
    if (hitEnemyAt(ctx, e, pos, col, fid)) return;

    double rem = speed.value;
    if (ctx.map.tiling() == TilingType::Square) {
        while (rem > 0) {
            const int boundaryCode = math::findNextXY(pos.x, pos.y, vel.angle, rem, ctx.rng);
            if (boundaryCode == -3) continue;  // 撞角：已反向，继续本 tick 移动
            if (boundaryCode == 0 && pos.x <= 0) { toDestroy.push_back(e); return; }
            if (boundaryCode == 1 && pos.y <= 0) { toDestroy.push_back(e); return; }
            if (boundaryCode == 2 && pos.x >= ctx.map.width()) { toDestroy.push_back(e); return; }
            if (boundaryCode == 3 && pos.y >= ctx.map.height()) { toDestroy.push_back(e); return; }
            if (boundaryCode < 0) {
                // -1/-2：走完本段，夹取到界内 + 终点击杀判定。
                pos.x = std::clamp(pos.x, 0.0, static_cast<double>(ctx.map.width()));
                pos.y = std::clamp(pos.y, 0.0, static_cast<double>(ctx.map.height()));
                if (hitEnemyAt(ctx, e, pos, col, fid)) return;
                break;
            }
            // 目标格（沿穿越方向 ±eps 取整，同 MovementSystem §2.3）。
            const MapCell* goalCell = nullptr;
            if (boundaryCode == 0)
                goalCell = &ctx.map.at(static_cast<int>(pos.x - kEps), static_cast<int>(pos.y));
            if (boundaryCode == 1)
                goalCell = &ctx.map.at(static_cast<int>(pos.x), static_cast<int>(pos.y - kEps));
            if (boundaryCode == 2)
                goalCell = &ctx.map.at(static_cast<int>(pos.x + kEps), static_cast<int>(pos.y));
            if (boundaryCode == 3)
                goalCell = &ctx.map.at(static_cast<int>(pos.x), static_cast<int>(pos.y + kEps));
            // 陆→山：子弹消失（山地中继续飞，仅留存惩罚）。
            if (goalCell->mountain && !proj.inMountain) { toDestroy.push_back(e); return; }
            proj.inMountain = goalCell->mountain;
            // 不占领途经领地（P9 改版：子弹只击杀敌兵，不改变领土归属）。
            // 进入格后击杀判定。
            if (hitEnemyAt(ctx, e, pos, col, fid)) return;
        }
        pos.x = std::clamp(pos.x, 0.0, static_cast<double>(ctx.map.width()));
        pos.y = std::clamp(pos.y, 0.0, static_cast<double>(ctx.map.height()));
    } else {
        // P12 密铺：crossEdge 逐格穿越 + **世界范围**夹取（方 width/height 是列/行数，非世界单位
        // ——旧路径会把子弹夹死在 x=width 内，且山地判定取错格 → 穿山/莫名被拦）。
        const TilingGeom& g = ctx.map.geom();
        const double ww = ctx.map.worldWidth(), wh = ctx.map.worldHeight();
        int cellIdx = g.worldToCell(pos.x, pos.y);
        if (cellIdx < 0) {
            pos.x = std::clamp(pos.x, kEps, ww - kEps);
            pos.y = std::clamp(pos.y, kEps, wh - kEps);
            cellIdx = g.worldToCell(pos.x, pos.y);
        }
        while (rem > 0) {
            const int crossed =
                (cellIdx >= 0) ? g.crossEdge(cellIdx, pos.x, pos.y, vel.angle, rem) : -1;
            if (crossed == -2) {
                // 走完本段：夹取 + 终点击杀判定。
                pos.x = std::clamp(pos.x, 0.0, ww);
                pos.y = std::clamp(pos.y, 0.0, wh);
                if (hitEnemyAt(ctx, e, pos, col, fid)) return;
                break;
            }
            if (crossed == -1) {
                // 顶点/贴边：nudge 穿越；出界 → 销毁。
                pos.x += kEps * std::cos(vel.angle);
                pos.y += kEps * std::sin(vel.angle);
                cellIdx = g.worldToCell(pos.x, pos.y);
                if (cellIdx < 0) {
                    toDestroy.push_back(e);
                    return;
                }
                continue;
            }
            int nr, nc;
            g.neighborRaw(cellIdx, crossed, nr, nc);
            if (nr < 0 || nr >= g.rows || nc < 0 || nc >= g.cols) {
                toDestroy.push_back(e);  // 出界销毁
                return;
            }
            const int nb = g.neighbor(cellIdx, crossed);
            if (nb < 0) {
                toDestroy.push_back(e);
                return;
            }
            cellIdx = nb;
            const MapCell& goalCell = ctx.map.atIndex(cellIdx);
            if (goalCell.mountain && !proj.inMountain) {
                toDestroy.push_back(e);
                return;
            }
            proj.inMountain = goalCell.mountain;
            if (hitEnemyAt(ctx, e, pos, col, fid)) return;
        }
        pos.x = std::clamp(pos.x, 0.0, ww);
        pos.y = std::clamp(pos.y, 0.0, wh);
    }
}

}  // namespace

void ProjectileSystem::update(Simulation& sim) {
    MoveContext ctx = MovementSystem::makeContext(sim);
    // 重建空间哈希：UnitActionSystem 刚发射的子弹与已移动的兵都要参与本 tick 查询。
    ctx.spatialHash.build(ctx.registry, ctx.map.geom());  // P12：按密铺格分桶
    auto view = ctx.registry.view<comp::Projectile, comp::Position, comp::Velocity, comp::Speed,
                                  comp::FactionId, comp::Collider>();
    // P9 确定性：按实体 id 降序迭代（与存储序无关 → 读档后 RNG/命中顺序与直跑一致）。
    std::vector<entt::entity> projectiles(view.begin(), view.end());
    std::sort(projectiles.begin(), projectiles.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) > entt::to_integral(b);
    });
    std::vector<entt::entity> toDestroy;
    for (auto e : projectiles) {
        if (!ctx.registry.valid(e)) continue;
        solveProjectile(ctx, e, toDestroy);
    }
    for (auto e : toDestroy) {
        if (ctx.registry.valid(e)) ctx.registry.destroy(e);
    }
}

}  // namespace lw
