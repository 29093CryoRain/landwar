// SpawnSystem.h — 兵/特效实体创建（翻新计划 §2.2 / §3.3）。
#pragma once

#include <entt/entt.hpp>

#include "core/GameDefs.h"
#include "sim/components.h"

namespace lw {

class Simulation;

class SpawnSystem {
public:
    // 完整复刻 §2.2 出生参数（速度/大小链 + 方向轴向吸附）。返回新兵实体；超 maxArmyGuard 返回 entt::null。
    static entt::entity spawnArmy(Simulation& sim, double x, double y, int factionId, ArmyType type);

    // 创建特效实体（位置 clamp 到地图内）。Phase 4 由 EffectSystem 求解。
    static entt::entity spawnEffect(Simulation& sim, double x, double y, int factionId,
                                    EffectType type, double p0, const comp::Creator& creator);

    // 创建射弹实体（P9）：速度/半径来自发射兵种定义（Unit::bullet*），方向/速度由调用方
    // 给定（UnitActionSystem）。复用 Position/Velocity/Speed/FactionId/UnitType/Collider，
    // 另带 comp::Projectile 标志组件。inMountain 按出生格算。
    static entt::entity spawnProjectile(Simulation& sim, double x, double y, int factionId,
                                        ArmyType sourceType, double angle, double speed);
};

}  // namespace lw
