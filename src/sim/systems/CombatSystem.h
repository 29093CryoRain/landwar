// CombatSystem.h — 碰撞战斗（翻新计划 §2.4 / Phase 3）。
// 空间哈希等价替换原版 O(n²)；命中即双方标记死亡并记录事件，移动中止。
#pragma once

#include <entt/entt.hpp>

#include "sim/components.h"
#include "sim/systems/Systems.h"

namespace lw {

class CombatSystem {
public:
    // 战斗查询半径：2 * 最大兵半径 + 冗余，保证覆盖一切可能的 size 和。
    static double queryRadius(const Config& cfg);

    // 检查 self 在 (x,y) 处与其他敌方活兵碰撞。命中：双方标死 + 记录事件，返回 true。
    static bool checkAt(MoveContext& ctx, entt::entity self, const comp::Position& pos,
                        const comp::Collider& col, const comp::FactionId& fid);
};

}  // namespace lw
