// ProductionAI.cpp — 产兵 AI 实现（开发计划 P1）。
// DefaultFairAI：从原 ProductionSystem::produce 抽出的公平调度堆决策（Phase 9 用户定夺模型），
// 逐条保持行为/顺序一致（含最小堆排序键、least-recent 城市选择），保证 RNG 序列不变。
#include "sim/ai/ProductionAI.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <queue>
#include <vector>

#include "core/Simulation.h"
#include "world/Faction.h"

namespace lw {

namespace {

// 公平调度堆元素：{兵种编号, 单价, 该兵种累计花费, 兵种偏好系数}。
struct ProdItem {
    int type = 0;
    double price = 0.0;  // 单价（= 定义 baseCost × 增益乘数；P11 改版后各势力同价）
    double spent = 0.0;  // 该兵种累计花费
    double pref = 1.0;   // 兵种偏好系数（P11 改版：>1 → 该兵种上花费更多）
};

// 最小堆：**排序键 = (已花费 + 单价) / 兵种偏好系数**（P11 改版，用户定夺）。
// 开局 spent=0 → 键=单价/偏好 → 便宜 + 高偏好的兵先产；产兵后键值升高 → 兵种随键值平衡。
// 偏好越高键越小 → 默认 AI 在该兵种上花费更多（红普通 2、黄炸/激/雷 1.5、五色特色 3 等）。
// 平局按 type 稳定。
struct ProdMinHeap {
    bool operator()(const ProdItem& a, const ProdItem& b) const {
        const double ka = (a.spent + a.price) / (a.pref > 0.0 ? a.pref : 1.0);
        const double kb = (b.spent + b.price) / (b.pref > 0.0 ? b.pref : 1.0);
        if (ka != kb) return ka > kb;  // 最小堆：键小者优先
        return a.type > b.type;
    }
};

// 默认 AI：公平调度堆。纯决策，不消耗 Rng。
class DefaultFairAI final : public ProductionAI {
public:
    std::optional<ProduceRequest> nextProduction(Simulation& sim, int factionId) override {
        auto& faction = sim.faction(factionId);
        if (!faction.alive) return std::nullopt;

        std::priority_queue<ProdItem, std::vector<ProdItem>, ProdMinHeap> heap;
        for (int t = 0; t < kArmyTypeCount; ++t)
            // P7：有效造价 = 定义基数 × 增益乘数（基线 1.0 → 恒等）。
            // P11 改版：键除以兵种偏好系数（faction.unitPreference）。
            heap.push(ProdItem{t,
                               faction.armyCost[static_cast<size_t>(t)]
                                   * faction.mods.costMult[static_cast<size_t>(t)],
                               faction.producedCost[static_cast<size_t>(t)],
                               faction.unitPreference[static_cast<size_t>(t)]});

        if (heap.empty() || faction.economy < heap.top().price) return std::nullopt;

        const int leastRecentIndex = leastRecentCityIndex(sim, factionId);
        if (leastRecentIndex == -1) return std::nullopt;  // 无城市 → 停止该势力全部产兵
        return ProduceRequest{heap.top().type, leastRecentIndex};
    }
};

// 玩家 AI（P2）：读 PlayerIntent（外部输入）决定产兵——仅产选定兵种、在选定城市产。
// 每 tick 可产多个（库存允许即产，仅此一个兵种/一个城市）。不消耗 Rng。
class PlayerAI final : public ProductionAI {
public:
    std::optional<ProduceRequest> nextProduction(Simulation& sim, int factionId) override {
        auto& faction = sim.faction(factionId);
        if (!faction.alive) return std::nullopt;
        const PlayerIntent& intent = sim.playerIntent();
        if (!intent.active || intent.factionId != factionId) return std::nullopt;
        if (intent.selectedType < 0 || intent.selectedType >= kArmyTypeCount) return std::nullopt;
        if (intent.cityX < 0.0 || intent.cityY < 0.0) return std::nullopt;
        // P7：玩家产兵扣费走有效造价（= 定义 baseCost × 增益乘数；P11 改版后各势力同价）。
        const double price =
            faction.armyCost[static_cast<size_t>(intent.selectedType)]
            * faction.mods.costMult[static_cast<size_t>(intent.selectedType)];
        if (faction.economy < price) return std::nullopt;
        // 解析城市 → 下标（removeCity 会 swap+pop，不能持有悬垂下标）。
        // 城市已易主/消失 → 不产（Application 会每帧清空意图，这里是防御）。
        // P13：intent 坐标 = 城市【几何中心】（centerX/Y，2026-08-07 用户要求选取落在产兵城
        // 中央；多格城市唯一标识）；cityIds[j] → 注册表比对。中心匹配用容差（double 比较）。
        for (int j = 0; j < faction.cityCount; ++j) {
            const auto& city = sim.map().city(
                static_cast<size_t>(faction.cityIds[static_cast<size_t>(j)]));
            if (std::fabs(city.centerX() - intent.cityX) < 1e-6 &&
                std::fabs(city.centerY() - intent.cityY) < 1e-6) {
                return ProduceRequest{intent.selectedType, j};
            }
        }
        return std::nullopt;
    }
};

}  // namespace

int leastRecentCityIndex(const Simulation& sim, int factionId) {
    const auto& faction = sim.faction(factionId);
    int leastRecentTicks = 1999999999;
    int leastRecentIndex = -1;
    for (int j = 0; j < faction.cityCount; ++j) {
        const auto& city = sim.map().city(
            static_cast<size_t>(faction.cityIds[static_cast<size_t>(j)]));
        if (leastRecentTicks > city.lastProduceArmyN) {
            leastRecentTicks = city.lastProduceArmyN;
            leastRecentIndex = j;
        }
    }
    return leastRecentIndex;
}

std::unique_ptr<ProductionAI> makeAI(int aiId) {
    switch (aiId) {
        case 0:
            return std::make_unique<DefaultFairAI>();
        case 1:
            return std::make_unique<PlayerAI>();
        default:
            spdlog::warn("unknown aiId={}, falling back to default AI", aiId);
            return std::make_unique<DefaultFairAI>();
    }
}

}  // namespace lw
