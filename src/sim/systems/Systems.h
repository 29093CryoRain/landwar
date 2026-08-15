// Systems.h — 系统共享上下文与辅助（翻新计划 §3.3）。
// MoveContext：moveArmy/战斗/死亡的全部依赖。生产由 MovementSystem::makeContext 从 Simulation 构建；
// 单测可自建小地图 + MockRng 直接调用 moveArmy（覆盖移动分支）。
#pragma once

#include <entt/entt.hpp>

#include <vector>

#include "core/Config.h"
#include "core/Random.h"
#include "sim/SpatialHash.h"
#include "sim/Statistics.h"
#include "sim/components.h"
#include "world/Faction.h"
#include "world/Map.h"

namespace lw {

struct MoveContext {
    Map& map;
    std::vector<Faction>& factions;
    Rng& rng;
    std::vector<PendingSpawn>& pendingSpawns;
    std::vector<DeathEvent>& deaths;
    entt::registry& registry;
    SpatialHash& spatialHash;
    double goSeaProbability = 1.0;
    int ttime = 0;
    const Config& config;
    // P10 边界贯通开关：wrap 开启时撞地图边界→传送对侧（速度/方向不变，0 RNG）；关闭→反弹。
    // 由 makeContext 从 sim.options().map.wrap 填充；单测自建 MoveContext 可显式设。
    bool wrap = false;
    // P11：统计通道（nullptr = 不记录；单测自建 MoveContext 不设 → 纯移动测试零影响）。
    Statistics* stats = nullptr;
};

// 征服 (x,y) 归势力 factionId（经 MoveContext 转 ConquerContext）。
// P11：unitType = 负责的兵种（>0 才记录占领 credit；移动征服=该兵、特效征服=Creator.unitType）。
inline void conquerAt(MoveContext& ctx, int x, int y, int factionId, int unitType = -1) {
    ConquerContext cc{ctx.map, ctx.factions, ctx.rng, ctx.pendingSpawns,
                      /*freeArmyEnabled=*/true,
                      /*tick=*/static_cast<std::uint64_t>(ctx.ttime)};
    // P11：统计"占领"须界内/陆地/归属实际变更（前置判断，conquer 后 belongi 已改）。
    const bool landChanged =
        x >= 0 && y >= 0 && x < ctx.map.width() && y < ctx.map.height()
        && ctx.map.at(x, y).land && ctx.map.at(x, y).belongi != factionId;
    ctx.factions[static_cast<size_t>(factionId)].conquer(cc, x, y);
    if (ctx.stats && unitType >= 0 && landChanged) ctx.stats->recordLand(factionId, unitType);
}

// P12：按格下标征服（密铺路径；六/三角格 ≠ floor 坐标，征服统一走下标）。
inline void conquerAtIndex(MoveContext& ctx, int index, int factionId, int unitType = -1) {
    ConquerContext cc{ctx.map, ctx.factions, ctx.rng, ctx.pendingSpawns,
                      /*freeArmyEnabled=*/true,
                      /*tick=*/static_cast<std::uint64_t>(ctx.ttime)};
    const bool landChanged =
        index >= 0 && index < ctx.map.cellCount() && ctx.map.atIndex(index).land
        && ctx.map.atIndex(index).belongi != factionId;
    ctx.factions[static_cast<size_t>(factionId)].conquerIndex(cc, index);
    if (ctx.stats && unitType >= 0 && landChanged) ctx.stats->recordLand(factionId, unitType);
}

// 标记死亡 + 记录死亡事件（tick 末由 DeathSystem 销毁并生成死亡效果）。
// P11：killerFactionId/killerUnitType = 击杀者 credit（>=0 才记录击杀；战斗互杀/特效 Creator/
// 子弹发射兵；无击杀者（如子弹消亡）不记录）。
inline void markDead(MoveContext& ctx, entt::entity e, double x, double y, double kx, double ky,
                     int killerFactionId = -1, int killerUnitType = -1) {
    if (ctx.registry.all_of<comp::Dead>(e)) return;  // 防重复击杀一兵
    ctx.registry.emplace<comp::Dead>(e);
    if (ctx.stats && killerFactionId > 0 && killerUnitType >= 0)
        ctx.stats->recordKill(killerFactionId, killerUnitType);
    const auto& ut = ctx.registry.get<comp::UnitType>(e);
    const auto& fid = ctx.registry.get<comp::FactionId>(e);
    ctx.deaths.push_back(DeathEvent{e, ut.type, fid.value, x, y, kx, ky});
}

}  // namespace lw
