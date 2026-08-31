// test_economy.cpp — 经济与产兵系统单测（翻新计划 §2.7 / Phase 5）。
// 经济公式结构、城市占比/时间推进的影响；产兵选城、无城市、maxArmyGuard；
// 回合顺序洗牌；20000 tick 浸泡（确定性 + 领土单调 + 扩张 + 产兵/战斗活跃）。
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <entt/entt.hpp>

#include "core/Simulation.h"
#include "sim/components.h"
#include "sim/systems/EconomySystem.h"
#include "sim/systems/ProductionSystem.h"
#include "sim/systems/SpawnSystem.h"
#include "TestUtil.h"

namespace {

using namespace lw;

Config baseCfg() { return lwtest::loadCfg(); }

// 清空全部兵实体（保留特效/地图），得到干净测试场。
void clearArmies(Simulation& sim) {
    auto& reg = sim.registry();
    std::vector<entt::entity> all;
    for (auto e : reg.view<comp::Position, comp::Collider>()) all.push_back(e);
    for (auto e : all) reg.destroy(e);
}

int countFactionArmiesAt(Simulation& sim, int fid, double x, double y) {
    int n = 0;
    for (auto e : sim.registry().view<comp::Position, comp::Collider, comp::FactionId>()) {
        if (sim.registry().get<comp::FactionId>(e).value != fid) continue;
        const auto& p = sim.registry().get<comp::Position>(e);
        if (std::abs(p.x - x) < 1e-9 && std::abs(p.y - y) < 1e-9) ++n;
    }
    return n;
}

int countArmies(Simulation& sim) {
    int n = 0;
    for (auto e : sim.registry().view<comp::Position, comp::Collider>())
        if (!sim.registry().all_of<comp::Dead>(e)) ++n;
    return n;
}

// ---- 经济累积（纯函数，P14 朴素逐 tick 收入模型）----

TEST(Economy, LandIncomeScalesWithLandCount) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.economy = 0.0;
    f.landCount = 10;
    f.cityIds.clear();
    f.cityCount = 0;
    f.capitalState.capitalCityId = -1;  // 无正式首都 → 无 capitalIncome（纯 land 收入）
    EconomySystem::accumulate(sim, 1);
    EXPECT_DOUBLE_EQ(f.economy, cfg.economy.perLandIncome * 10);  // 朴素 land 收入
    EXPECT_DOUBLE_EQ(f.economyRate, cfg.economy.perLandIncome * 10);
}

TEST(Economy, CityIncomeUsesLevelsAlpha) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    const int c1 = sim.map().addCity(1, 10, 10);
    const int c4 = sim.map().addCity(4, 20, 20);
    auto& f = sim.faction(1);
    f.economy = 0.0;
    f.landCount = 0;
    f.cityIds = {c1, c4};
    f.cityCount = 2;
    f.capitalState.capitalCityId = -1;  // 无正式首都 → 纯 city 收入
    EconomySystem::accumulate(sim, 1);
    // cityIncome = perLandIncome × cityBaseMult × (1^alpha + 4^alpha)。
    const double expected = cfg.economy.perLandIncome * cfg.economy.cityBaseMult
                            * (std::pow(1.0, cfg.city.levelIncomeExponent)
                               + std::pow(4.0, cfg.city.levelIncomeExponent));
    EXPECT_NEAR(f.economy, expected, 1e-9);
    EXPECT_NEAR(f.economyRate, expected, 1e-9);
}

TEST(Economy, LandAndCityIncomeCombine) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    const int c1 = sim.map().addCity(1, 10, 10);
    const int c9 = sim.map().addCity(9, 20, 20);
    auto& f = sim.faction(1);
    f.economy = 0.0;
    f.landCount = 3;
    f.cityIds = {c1, c9};
    f.cityCount = 2;
    f.capitalState.capitalCityId = -1;  // 无正式首都 → 纯 land+city 收入
    EconomySystem::accumulate(sim, 1);
    const double expected = cfg.economy.perLandIncome * 3
                            + cfg.economy.perLandIncome * cfg.economy.cityBaseMult
                                  * (std::pow(1.0, cfg.city.levelIncomeExponent)
                                     + std::pow(9.0, cfg.city.levelIncomeExponent));
    EXPECT_NEAR(f.economy, expected, 1e-9);
}

TEST(Economy, DoubleCountingLandAndCitySeparateOwners) {
    // 城市基建格双计（spec 语义，勿去重）：land 收入归 belongi 势力、city 收入归城市 owner。
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    // 4 级城（2×2 基建格），owner=势力1；其土地 belongi 归势力2。
    const int c = sim.map().addCity(4, 10, 10);
    sim.map().city(c).ownerId = 1;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
            sim.map().at(10 + dx, 10 + dy).belongi = 2;
    auto& f1 = sim.faction(1);
    auto& f2 = sim.faction(2);
    f1.economy = f2.economy = 0.0;
    f1.landCount = 0;
    f1.cityIds = {c};
    f1.cityCount = 1;
    f2.landCount = 4;  // 4 个基建格（belongi=2）
    f2.cityIds.clear();
    f2.cityCount = 0;
    f1.capitalState.capitalCityId = -1;  // 无正式首都 → 双计测试只含 land+city 两项
    f2.capitalState.capitalCityId = -1;
    EconomySystem::accumulate(sim, 1);  // 势力1：仅 city 收入（归 owner）
    EconomySystem::accumulate(sim, 2);  // 势力2：仅 land 收入（归 belongi）
    EXPECT_NEAR(f1.economy, cfg.economy.perLandIncome * cfg.economy.cityBaseMult
                              * std::pow(4.0, cfg.city.levelIncomeExponent), 1e-9);
    EXPECT_NEAR(f2.economy, cfg.economy.perLandIncome * 4, 1e-9);
}

TEST(Economy, DeadFactionGetsZeroIncome) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    const int c = sim.map().addCity(1, 10, 10);
    auto& f = sim.faction(1);
    f.alive = false;
    f.economy = 0.0;
    f.economyRate = 123.0;
    f.landCount = 100;
    f.cityIds = {c};
    f.cityCount = 1;
    EconomySystem::accumulate(sim, 1);
    EXPECT_DOUBLE_EQ(f.economy, 0.0);  // 灭亡势力零收入（含 land + city）
    EXPECT_DOUBLE_EQ(f.economyRate, 0.0);
}

TEST(Economy, EconomyGainBuffMultiplies) {
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    auto& f = sim.faction(1);
    f.economy = 0.0;
    f.landCount = 5;
    f.cityIds.clear();
    f.cityCount = 0;
    f.capitalState.capitalCityId = -1;  // 无正式首都 → 纯 land × 增益
    f.mods.economyGainMult = 2.5;  // P7 增益（P8 经济振兴科技将走它）
    EconomySystem::accumulate(sim, 1);
    EXPECT_NEAR(f.economy, cfg.economy.perLandIncome * 5 * 2.5, 1e-9);
}

TEST(Economy, CityEconomyRatePureFunction) {
    // P14 P8 预留 hook：单城收入率可对任意势力的城求值（不读归属势力状态），供扫荡科技复用。
    Config cfg = baseCfg();
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    const int c1 = sim.map().addCity(1, 10, 10);
    const int c9 = sim.map().addCity(9, 20, 20);
    EXPECT_NEAR(EconomySystem::cityEconomyRate(cfg, sim.map().city(c1)),
                cfg.economy.perLandIncome * cfg.economy.cityBaseMult
                    * std::pow(1.0, cfg.city.levelIncomeExponent), 1e-12);
    EXPECT_NEAR(EconomySystem::cityEconomyRate(cfg, sim.map().city(c9)),
                cfg.economy.perLandIncome * cfg.economy.cityBaseMult
                    * std::pow(9.0, cfg.city.levelIncomeExponent), 1e-12);
    // alpha 变化：n 级城收入 = n^alpha × 1 级城。
    Config cfg2 = baseCfg();
    cfg2.city.levelIncomeExponent = 2.0;
    const double one = EconomySystem::cityEconomyRate(cfg2, sim.map().city(c1));
    EXPECT_NEAR(EconomySystem::cityEconomyRate(cfg2, sim.map().city(c9)), one * 81.0, 1e-9);
}

// ---- 产兵（纯函数）----

TEST(Production, FairHeapProducesBalancedTypes) {
    // Phase 9 用户定夺：公平调度堆——按该兵种累计花费升序，各类型轮转产出（不只有普通兵）。
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    auto& f = sim.faction(1);
    // P13：城市注册到 Map 注册表，Faction 持 cityIds。
    const int c1 = sim.map().addCity(1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    f.economy = 6.0;
    f.producedCost.fill(0.0);
    f.armyCost.fill(1.0);
    f.unitPreference.fill(1.0);  // 偏好全 1 → 公平调度堆纯按价格（隔离 P11 偏好影响）
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(f.numArmyProduced, 6);           // 6 兵种各产 1 个（公平轮转）
    EXPECT_DOUBLE_EQ(f.economy, 0.0);          // 库存 6-6=0
    EXPECT_DOUBLE_EQ(f.producedCost[0], 1.0);  // 各类型累计花费均为 1
    EXPECT_DOUBLE_EQ(f.producedCost[5], 1.0);
    EXPECT_EQ(sim.map().city(c1).lastProduceArmyN, 6);
    // 补满库存再产 → 各类型再 1 个。
    f.economy = 6.0;
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(f.numArmyProduced, 12);
    EXPECT_DOUBLE_EQ(f.producedCost[0], 2.0);
}

TEST(Production, EconomyIsRetainedStock) {
    // Phase 9：经济 = 留存值（库存）——产兵扣该兵成本，经济 ≥0（不超支）。
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    auto& f = sim.faction(1);
    const int c1 = sim.map().addCity(1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    f.economy = 3.0;
    f.producedCost.fill(0.0);
    f.armyCost.fill(1.0);
    f.unitPreference.fill(1.0);  // 偏好全 1 → 公平调度堆纯按价格（隔离 P11 偏好影响）
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(f.numArmyProduced, 3);            // 库存 3 够产 3 个（普通/先锋/开拓）
    EXPECT_DOUBLE_EQ(f.economy, 0.0);           // 花光
    EXPECT_GE(f.economy, 0.0);                  // 不为负
    EXPECT_DOUBLE_EQ(f.producedCost[0], 1.0);
}

TEST(Production, PrefersLeastRecentlyProducedCity) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    auto& f = sim.faction(1);
    // P13：注册表城市；城1 lastProduce=0（闲置最久）→ 优先，城2=100 不敌城1。
    const int c1 = sim.map().addCity(1, 10, 10);
    const int c2 = sim.map().addCity(1, 20, 20);
    sim.map().city(c2).lastProduceArmyN = 100;
    f.cityIds = {c1, c2};
    f.cityCount = 2;
    f.economy = 3.0;
    f.producedCost.fill(0.0);
    f.armyCost.fill(1.0);
    f.unitPreference.fill(1.0);  // 偏好全 1 → 公平调度堆纯按价格（隔离 P11 偏好影响）
    ProductionSystem::produce(sim, 1);
    // 城2 lastProduce=100 始终不敌城1 → 全部产在城1。
    EXPECT_EQ(countFactionArmiesAt(sim, 1, 10.5, 10.5), 3);
    EXPECT_EQ(countFactionArmiesAt(sim, 1, 20.5, 20.5), 0);
    EXPECT_EQ(f.numArmyProduced, 3);
}

TEST(Production, NoCitiesProducesNothing) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    auto& f = sim.faction(1);
    f.cityIds.clear();
    f.cityCount = 0;
    f.economy = 100.0;
    f.producedCost.fill(0.0);
    f.armyCost.fill(1.0);
    f.unitPreference.fill(1.0);  // 偏好全 1 → 公平调度堆纯按价格（隔离 P11 偏好影响）
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(countFactionArmiesAt(sim, 1, 0.5, 0.5) + countArmies(sim), 0);
    EXPECT_EQ(f.numArmyProduced, 0);
    EXPECT_DOUBLE_EQ(f.economy, 100.0);  // 未消耗经济
}

TEST(Production, StopsAtMaxArmyGuard) {
    Config cfg = baseCfg();
    cfg.sim.maxArmyGuard = 3;
    Simulation sim(cfg, 42);
    ASSERT_TRUE(sim.init());
    clearArmies(sim);
    auto& f = sim.faction(1);
    const int c1 = sim.map().addCity(1, 10, 10);
    f.cityIds = {c1};
    f.cityCount = 1;
    f.economy = 100.0;
    f.producedCost.fill(0.0);
    f.armyCost.fill(1.0);
    f.unitPreference.fill(1.0);  // 偏好全 1 → 公平调度堆纯按价格（隔离 P11 偏好影响）
    ProductionSystem::produce(sim, 1);
    EXPECT_EQ(countFactionArmiesAt(sim, 1, 10.5, 10.5), 3);  // 达到 guard 即停
    EXPECT_EQ(f.numArmyProduced, 3);
}

// ---- 回合顺序 ----

TEST(Economy, TurnOrderIsPermutationAfterShuffle) {
    Simulation sim(baseCfg(), 42);
    ASSERT_TRUE(sim.init());
    EconomySystem::update(sim);  // 洗牌 + 累积
    const auto& order = sim.turnOrder();
    ASSERT_EQ(order.size(), 8u);
    std::vector<bool> seen(8, false);
    for (int v : order) {
        ASSERT_GE(v, 0);
        ASSERT_LT(v, 8);
        seen[static_cast<size_t>(v)] = true;
    }
    for (bool s : seen) EXPECT_TRUE(s);  // 仍是 0..7 的排列
}

// ---- 浸泡 + 确定性（规模可配置）----
// 默认 2000 tick（快速回归）；LANDWAR_SOAK_TICKS=20000 跑全量浸泡。
// P14 经济重构（朴素逐 tick 收入）后，maxAlive/maxEconomy 等具体数值随 config
// （economy.perLandIncome/cityBaseMult，待用户实验）变化，不做数值断言——本测试只锁
// 确定性（同 seed 两次一致）+ 不变量（领土单调、产兵/扩张在发生、兵量有上界）。
TEST(Economy, SoakDeterministic) {
    const char* env = std::getenv("LANDWAR_SOAK_TICKS");
    const int soakTicks = env ? std::atoi(env) : 2000;
    if (soakTicks <= 0) GTEST_SKIP() << "LANDWAR_SOAK_TICKS must be > 0";

    struct Summary {
        int land = 0;
        int alive = 0;
        int maxAlive = 0;
        int maxFactionLand = 0;
        double maxEconomy = 0;
        bool landDecreased = false;
    };
    auto run = [&](std::uint32_t seed) {
        Simulation sim(lwtest::loadCfg(), seed);
        EXPECT_TRUE(sim.init());
        const auto landTotal = [&]() {
            int n = 0;
            for (int id = 1; id <= 8; ++id) n += sim.factions()[static_cast<size_t>(id)].landCount;
            return n;
        };
        const auto maxFactionLand = [&]() {
            int m = 0;
            for (int id = 1; id <= 8; ++id)
                m = std::max(m, sim.factions()[static_cast<size_t>(id)].landCount);
            return m;
        };
        Summary s;
        int prevLand = landTotal();
        for (int t = 0; t < soakTicks; ++t) {
            sim.tick();
            const int land = landTotal();
            if (land < prevLand) s.landDecreased = true;
            prevLand = land;
            s.maxAlive = std::max(s.maxAlive, countArmies(sim));
        }
        s.land = landTotal();
        s.alive = countArmies(sim);
        s.maxFactionLand = maxFactionLand();
        for (int id = 1; id <= 8; ++id)
            s.maxEconomy = std::max(s.maxEconomy, sim.factions()[static_cast<size_t>(id)].economy);
        std::cout << "[soak seed=" << seed << " ticks=" << soakTicks
                  << "] final land=" << s.land << " alive=" << s.alive
                  << " maxAlive=" << s.maxAlive << " maxFactionLand=" << s.maxFactionLand
                  << " maxEconomy=" << s.maxEconomy << '\n';
        return s;
    };

    const auto r1 = run(42);
    const auto r2 = run(42);
    EXPECT_EQ(r1.land, r2.land);            // 确定性：领土终态一致
    EXPECT_EQ(r1.alive, r2.alive);          // 确定性：兵数终态一致
    EXPECT_EQ(r1.maxAlive, r2.maxAlive);    // 确定性：兵峰一致
    EXPECT_EQ(r1.maxFactionLand, r2.maxFactionLand);
    EXPECT_EQ(r1.maxEconomy, r2.maxEconomy);
    EXPECT_FALSE(r1.landDecreased);         // 领土单调不减（征服只会加/转土地）
    EXPECT_GT(r1.land, 8);                  // 领土扩张（初始各势力仅首都 1 格，共 8）
    EXPECT_GT(r1.maxAlive, 20);             // 产兵 + 战斗在发生
    EXPECT_GT(r1.maxFactionLand, 10);       // 至少一势力明显扩张（逐步统一趋势）
    EXPECT_LE(r1.alive, 50000);             // 有上界（maxArmyGuard 兜底）
}

}  // namespace
