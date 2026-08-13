// ProductionSystem.h — 产兵系统（翻新计划 §2.7 / Phase 5）。
// 与经济系统分离：消费经济（nextProduceGoal 阈值模型，不扣减 economy）产兵。
// 兵种成本/速度/大小由 config 数据驱动（SpawnSystem 落地）；P11 改版后各势力兵种同价（armyCost = baseCost）。
#pragma once

namespace lw {

class Simulation;

class ProductionSystem {
public:
    // 势力阶段后半：按当前回合顺序对每个势力调用 produce。
    static void update(Simulation& sim);

    // 单势力产兵（原版 solve_economy 的产兵部分）：
    //   for type in 0..5:
    //     while nextProduceGoal[type] <= economy:
    //       选 lstProduceArmyN 最小的城市 → spawnArmy(城市中心, type)
    //       nextProduceGoal[type] += armyCost[type]
    //       Map::City.lastProduceArmyN = ++numArmyProduced  （P13：产兵计数移到城市注册表）
    // 无城市 → 停止该势力全部产兵；达到 maxArmyGuard → 停止（原版池满哨兵）。
    static void produce(Simulation& sim, int factionId);
};

}  // namespace lw
