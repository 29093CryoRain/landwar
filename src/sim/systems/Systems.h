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

struct MoveProfile;  // 定义见 core/Simulation.h（MovementSystem 内部细分计时；nullptr = 关闭）

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
    // P11：统计通道（nullptr = 不记录；单测自建 MoveContext 不设 → 纯移动测试零影响）。
    Statistics* stats = nullptr;
    // 移动内部细分计时（2026-08 性能分析；nullptr = 关闭。单测自建不设 → 无开销）。
    MoveProfile* moveProfile = nullptr;
};

// 按格下标征服（全密铺统一；格坐标 ≠ floor(x,y)，故一律走下标）。
// unitType = 负责的兵种（>=0 才记录占领 credit；移动征服=该兵、特效征服=Creator.unitType）。
// originIndex = 兵进入前的格（>=0 时 conquest 目标格后一并尝试占领它；非移动征服传 -1）。
inline void conquerAtIndex(MoveContext& ctx, int index, int factionId, int unitType = -1,
                           int originIndex = -1) {
    ConquerContext cc{ctx.map,      ctx.factions, ctx.rng, ctx.pendingSpawns,
                      /*freeArmyEnabled=*/true,
                      /*tick=*/static_cast<std::uint64_t>(ctx.ttime),
                      /*originIndex=*/originIndex};
    // 归属是否真的变化，必须**征服前**取样（origin 与目标各不相同、互不影响）。
    const auto landChanged = [&](int idx) {
        return idx >= 0 && idx < ctx.map.cellCount() && ctx.map.atIndex(idx).land
               && ctx.map.atIndex(idx).belongi != factionId;
    };
    const bool targetChanged = landChanged(index);
    const bool originChanged = originIndex != index && landChanged(originIndex);
    ctx.factions[static_cast<size_t>(factionId)].conquerIndex(cc, index);
    if (ctx.stats && unitType >= 0) {
        if (targetChanged) ctx.stats->recordLand(factionId, unitType);
        if (originChanged) ctx.stats->recordLand(factionId, unitType);
    }
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
