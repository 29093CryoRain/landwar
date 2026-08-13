// ProductionSystem.cpp — 产兵系统（翻新计划 §2.7 / Phase 5 + 开发计划 P1）。
// 与经济系统分离：消费经济（留存值库存）产兵。决策抽象到 ProductionAI（按 Faction.aiId 分派，
// P1 任务 5）；本系统只负责【执行】：调 AI 拿 ProduceRequest → spawnArmy → 扣库存/累计花费。
// RNG 序列不变：AI 决策不消耗 Rng，spawnArmy 的角度随机是每产一兵的唯一次 Rng 消耗。
#include "sim/systems/ProductionSystem.h"

#include <memory>

#include "core/Simulation.h"
#include "sim/ai/ProductionAI.h"
#include "sim/systems/SpawnSystem.h"
#include "world/Faction.h"

namespace lw {

void ProductionSystem::update(Simulation& sim) {
    for (int playerIdx : sim.turnOrder()) produce(sim, playerIdx + 1);
}

void ProductionSystem::produce(Simulation& sim, int factionId) {
    auto& faction = sim.faction(factionId);
    if (!faction.alive) return;

    auto ai = makeAI(faction.aiId);
    if (!ai) return;

    // 循环向 AI 请求下一个产兵请求，直到 AI 返回空（库存不足/无城市）。
    while (auto req = ai->nextProduction(sim, factionId)) {
        if (req->cityIndex < 0 || req->cityIndex >= faction.cityCount) break;  // 防御：下标失效
        // P7：造价 = 定义基数（P11 改版后各势力同价）× 增益乘数（基线 costMult=1.0 → 恒等）。
        const double price =
            faction.armyCost[static_cast<size_t>(req->type)]
            * faction.mods.costMult[static_cast<size_t>(req->type)];
        // P13：cityIds[cityIndex] → Map 注册表城市；产兵锚点 = 城市中心（baseX+w/2, baseY+h/2）。
        City& city = sim.map().city(
            static_cast<size_t>(faction.cityIds[static_cast<size_t>(req->cityIndex)]));
        // 达到 maxArmyGuard（原版池满返回哨兵）→ 停止该势力全部产兵。
        if (SpawnSystem::spawnArmy(sim, city.centerX(), city.centerY(), factionId,
                                   static_cast<ArmyType>(req->type)) == entt::null) {
            break;
        }
        faction.economy -= price;  // 库存扣成本
        faction.producedCost[static_cast<size_t>(req->type)] += price;
        city.lastProduceArmyN = ++faction.numArmyProduced;
    }
}

}  // namespace lw
