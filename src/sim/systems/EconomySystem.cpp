#include "sim/systems/EconomySystem.h"

#include <algorithm>
#include <cmath>

#include "core/Simulation.h"
#include "world/Map.h"

namespace lw {

void EconomySystem::update(Simulation& sim) {
    // 回合顺序随机化（原版 Solve_faction 第一行）：每 tick 随机交换两次。
    // 防空：默认 Simulation（未 init）turnOrder 为空，直接跳过（tick 冒烟测试依赖此）。
    auto& order = sim.turnOrder();
    if (!order.empty()) {
        const int n = static_cast<int>(order.size()) - 1;
        std::swap(order[static_cast<size_t>(sim.rng().get(n))],
                  order[static_cast<size_t>(sim.rng().get(n))]);
    }
    for (int playerIdx : order) accumulate(sim, playerIdx + 1);  // order 存 0..7，势力 id = +1
}

double EconomySystem::cityEconomyRate(const Config& cfg, const City& city) {
    // P14：单城收入率 = perLandIncome × cityBaseMult × level^alpha。
    // alpha = cfg.city.levelIncomeExponent（n 级城收入 = n^alpha × 1 级城）。
    return cfg.economy.perLandIncome * cfg.economy.cityBaseMult
           * std::pow(static_cast<double>(city.level), cfg.city.levelIncomeExponent);
}

void EconomySystem::accumulate(Simulation& sim, int factionId) {
    auto& faction = sim.faction(factionId);
    faction.economyRate = 0.0;
    if (!faction.alive) return;  // 灭亡势力零收入（含 land + city，P4 已 gate）
    const auto& cfg = sim.config();
    // 普通领地收入：不分山区/是否基建格（基建格仍归该格 belongi 势力的 land 收入——双计）。
    const double landIncome = cfg.economy.perLandIncome * faction.landCount;
    // 城市收入：归城市 owner（level^alpha 求和；level 来自 P13 注册表；**含正式首都**——
    // 首都仍是普通城市，其 level^alpha 收入照常计入 cityIncome，另加 capitalIncome 加成）。
    double cityIncome = 0.0;
    for (int cid : faction.cityIds)
        cityIncome += cityEconomyRate(cfg, sim.map().city(cid));
    // P15：正式首都城市收入加成。用户定夺"首都经济加成=直接加一个数值"——spec 公式文字
    // `capitalIncome = capitalBase + cityEconomyRate(正式首都)` 若按字面会把首都的 level^alpha
    // 收入**重复计入**（cityIncome 已含首都），故实现为 **capitalIncome = capitalBase**（flat
    // 加成），首都本体收入照常走 cityIncome。沦陷窗口（无正式首都）→ 无加成。
    // 首都须仍属己方（防御：本 tick 可能刚被攻占）。
    double capitalIncome = 0.0;
    if (faction.capitalState.capitalCityId >= 0
        && sim.map().city(faction.capitalState.capitalCityId).ownerId == faction.id) {
        capitalIncome = cfg.economy.capitalBase;
    }
    // P7：经济产出 × 势力增益（基线 economyGainMult=1.0 → 恒等）。
    faction.economyRate =
        (landIncome + cityIncome + capitalIncome) * faction.mods.economyGainMult;
    faction.economy += faction.economyRate;
}

}  // namespace lw
