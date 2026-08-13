// ProductionAI.h — 产兵 AI 抽象（开发计划 P1 任务 5）。
// 每个势力按 aiId 分派到对应 AI（Faction.aiId，由 Options 决定）。
// 默认实现（aiId=0）= 现有公平调度堆；玩家 AI（aiId=1）在 P2 实现，本阶段为占位（返回空）。
// 接口约定：nextProduction 不消耗 Rng（公平调度堆本身不依赖随机；玩家 AI 读 PlayerIntent）。
// 产兵动作（spawnArmy 的角度随机）由 ProductionSystem::produce 统一执行，保证 RNG 序列不变。
#pragma once

#include <memory>
#include <optional>

#include "core/GameDefs.h"

namespace lw {

class Simulation;

// 一次产兵请求：在 Faction::cities[cityIndex] 产 type 兵。
struct ProduceRequest {
    int type = 0;        // ArmyType 枚举值
    int cityIndex = -1;  // Faction::cities[] 下标；-1 = 无效
};

class ProductionAI {
public:
    virtual ~ProductionAI() = default;
    // 返回空 = 本 tick 不产。见文件头注：不得消耗 Rng。
    virtual std::optional<ProduceRequest> nextProduction(Simulation& sim, int factionId) = 0;
};

// aiId=0 → DefaultFairAI（公平调度堆，行为与翻新 Phase 9 一致）；
// aiId=1 → PlayerAI 占位（返回空，P2 实现）；
// 未知 aiId → 记警告并回退 DefaultFairAI。
std::unique_ptr<ProductionAI> makeAI(int aiId);

}  // namespace lw
