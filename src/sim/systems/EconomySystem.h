// EconomySystem.h — 经济系统（翻新计划 §2.7 / Phase 5；P14 经济重构）。
// 与产兵系统分离：本系统只做经济累积，不产兵。公式常量全部外置 config.json（economy 段）。
// P14（开发计划，思路 9.3）：废弃旧非线性魔法公式，改朴素逐 tick 收入模型：
//   landIncome    = perLandIncome × landCount                 // 普通领地（不区分山区/是否基建格）
//   cityIncome    = perLandIncome × cityBaseMult × Σ level^alpha  // 每城；level 来自 P13，alpha=city.levelIncomeExponent
//   capitalIncome = capitalBase + cityEconomyRate(正式首都)   // P15 接入；本阶段恒 0
//   income = mods.economyGainMult × (landIncome + cityIncome + capitalIncome)
//   faction.economy += income
// **城市基建格双计**（spec 语义）：既是普通领地（产 landIncome，归该格 belongi 势力）又属城市
// （产 cityIncome，归城市 owner）——两笔按各自归属进账，勿在实现时"顺手"去重。
// 灭亡势力（alive=false）跳过全部收入（含 land + city）。
// 势力阶段由 Simulation::tick 依次调用 EconomySystem::update（洗牌回合顺序 + 累积经济）
// 与 ProductionSystem::update（消费经济产兵），保持原版 Solve_faction 的按势力顺序语义。
#pragma once

namespace lw {

struct City;   // world/Map.h（cityEconomyRate 纯函数签名用）
class Config;
class Simulation;

class EconomySystem {
public:
    // 势力阶段前半：每 tick 随机交换两次回合顺序（原版 Solve_faction 第一行，
    // 每次消耗 get(order.size()-1)），然后按顺序对每个势力调用 accumulate。
    static void update(Simulation& sim);

    // 单势力经济累积（纯函数，可独立测试）：朴素逐 tick 收入（公式见文件头）。
    static void accumulate(Simulation& sim, int factionId);

    // P14 P8 预留 hook：单城收入率（纯函数，无副作用）。
    // = perLandIncome × cityBaseMult × level^alpha（alpha = cfg.city.levelIncomeExponent）。
    // 可对**任意势力的城**求值（不读归属势力状态）→ P8 扫荡科技"占领立即获得该城 N 秒产量"
    // 直接复用；P15 首都 `capitalIncome = capitalBase + cityEconomyRate(正式首都)` 亦复用它。
    static double cityEconomyRate(const Config& cfg, const City& city);
};

}  // namespace lw
