// MovementSystem.h — 军队移动（原版 Solve_army / Army::move，翻新计划 §2.3）。
// 逐兵执行：网格穿越 → 战斗检查 → 边界反弹 → 海陆切换 → 攻占 → 抖动。
#pragma once

#include <entt/entt.hpp>

#include "sim/systems/Systems.h"

namespace lw {

class Simulation;

class MovementSystem {
public:
    // 每 tick 驱动全部活兵移动（重建空间哈希 + 逐兵 moveArmy）。
    static void update(Simulation& sim);

    // 从 Simulation 构建 MoveContext。
    static MoveContext makeContext(Simulation& sim);

    // 单兵单 tick 移动（§2.3 原样移植）。供 update 与单测直接调用。
    static void moveArmy(MoveContext& ctx, entt::entity e);
};

}  // namespace lw
